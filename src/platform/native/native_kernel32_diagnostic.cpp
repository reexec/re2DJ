#include "native_kernel32_diagnostic.h"
#include "native_host_services.h"

#include <cstring>
#include <limits>
#include <string>
#include <utility>


#include <cerrno>

#include "native_guest_seh.h"
#include "native_guest_threads.h"
#include "native_low_memory.h"
#include "native_pe_session.h"
#include "re2dj/hle/modules/advapi32_module.h"
#include "re2dj/hle/win32_time.h"
#include "re2dj/hle/modules/kernel32_module.h"
#include "re2dj/hle/modules/resolve_only_modules.h"
#include "re2dj/hle/modules/user32_module.h"
#include "re2dj/hle/modules/ddraw_module.h"
#include "re2dj/hle/modules/dinput_module.h"
#include "re2dj/hle/modules/dsound_module.h"
#include "re2dj/hle/modules/gdi32_module.h"
#include "re2dj/hle/modules/winmm_module.h"
#include "re2dj/hle/modules/wtsapi32_module.h"

namespace re2dj::platform::native
{
namespace
{

constexpr std::size_t kMaximumGuestStringLength = 260;
constexpr std::string_view kKernel32Module = "kernel32.dll";
// Room for the guest heap blocks facade calls hand out, such as WTS buffers.
constexpr std::uint32_t kGuestHeapSize = 1024U * 1024U;
// Room for VirtualAlloc regions and HeapCreate heaps, plus one allocation
// granule so the first region can start on a 64 KiB boundary. Pages are
// backed only once the guest touches them.
constexpr std::uint32_t kGuestPrivateArenaSize =
    512U * 1024U * 1024U + hle::GuestProcess::kAllocationGranularity;

bool StackContains(const NativeImportGateEvent& event,
                   std::uint32_t address,
                   std::size_t size)
{
    return address >= event.guest_stack_limit && address <= event.guest_stack_base &&
           size <= event.guest_stack_base - address;
}

std::string FormatGate(const runtime::ImportGate& gate)
{
    return gate.by_ordinal ? gate.module + "!#" + std::to_string(gate.ordinal)
                           : gate.module + "!" + gate.name;
}

}  // namespace

NativeKernel32Diagnostic::NativeKernel32Diagnostic(std::uint32_t image_base,
                                                   std::uint32_t image_size)
    : image_base_(image_base), image_size_(image_size)
{
}

NativeKernel32Diagnostic::~NativeKernel32Diagnostic()
{
    ReleaseNativeLowMemory(&heap_);
    ReleaseNativeLowMemory(&private_arena_);
    if (stop_stub_ != 0)
    {
        SetNativeHostTrapRange(0, 0);
        HostUnmap(reinterpret_cast<void*>(static_cast<std::uintptr_t>(stop_stub_)), HostPageSize());
    }
}

bool NativeKernel32Diagnostic::Setup(NativePeSession* session, void* context, std::string* error)
{
    auto* state = static_cast<NativeKernel32Diagnostic*>(context);
    if (session == nullptr || state == nullptr || error == nullptr)
    {
        return false;
    }
    std::vector<hle::modules::GuestModuleDescriptor> descriptors = {
        hle::modules::MakeKernel32ModuleDescriptor(),
        hle::modules::MakeUser32ModuleDescriptor(),
        hle::modules::MakeAdvapi32ModuleDescriptor(),
        hle::modules::MakeWtsapi32ModuleDescriptor(),
        hle::modules::MakeWinmmModuleDescriptor(),
        hle::modules::MakeGdi32ModuleDescriptor(),
        hle::modules::MakeDdrawModuleDescriptor(),
        hle::modules::MakeDsoundModuleDescriptor(),
        hle::modules::MakeDinputModuleDescriptor()};
    for (hle::modules::GuestModuleDescriptor& descriptor :
         hle::modules::MakeResolveOnlyModuleDescriptors())
    {
        descriptors.push_back(std::move(descriptor));
    }
    for (hle::modules::GuestModuleDescriptor& descriptor : descriptors)
    {
        if (!state->modules_.Add(std::move(descriptor),
                                 session->mutable_gates(),
                                 NativeImportGateBridgeAddress(),
                                 NativeImportGateCleanupAddress(),
                                 kDefaultNativeGuestModuleBase,
                                 error))
        {
            return false;
        }
    }
    if (state->heap_.memory == nullptr &&
        !MapNativeLowMemory(kGuestHeapSize, HostProtection::kReadWrite, &state->heap_, error))
    {
        return false;
    }
    state->process_.SetHeapRegion(state->heap_.address, state->heap_.size);
    // Facade modules are images too: the guest may change their pages'
    // protection and patch their exports, as it can with a system DLL.
    for (const hle::modules::RegisteredGuestModule* module : state->modules_.registry().modules())
    {
        exe::PeImageInfo facade_info;
        std::string facade_error;
        if (!exe::ReadPeImageInfo(reinterpret_cast<const std::uint8_t*>(
                                      static_cast<std::uintptr_t>(module->base.value())),
                                  module->image_size,
                                  &facade_info,
                                  &facade_error))
        {
            *error = "cannot read a guest facade's headers: " + facade_error;
            return false;
        }
        std::vector<hle::GuestImageSection> sections;
        for (const exe::PeSection& section : facade_info.sections)
        {
            sections.push_back({section.virtual_address, section.virtual_size, section.characteristics});
        }
        state->process_.AddImageRegion(module->base.value(), module->image_size, sections);
    }
    if (state->private_arena_.memory == nullptr &&
        !MapNativeLowMemory(kGuestPrivateArenaSize,
                            HostProtection::kReadWrite,
                            &state->private_arena_,
                            error))
    {
        return false;
    }
    state->process_.SetPrivateArena(state->private_arena_.address, state->private_arena_.size);
    if (!RebindNativeGuestModuleImports(session->mutable_import_thunks(),
                                        state->modules_.registry(),
                                        &state->rebindings_,
                                        error))
    {
        return false;
    }

    const hle::modules::GuestModuleRegistry& registry = state->modules_.registry();
    const hle::modules::RegisteredGuestModule* module = registry.FindModule("kernel32");
    const hle::modules::RegisteredGuestExport* get_version =
        module == nullptr ? nullptr : registry.FindExport(module->base, "GetVersion");
    const hle::modules::RegisteredGuestExport* create_file =
        module == nullptr ? nullptr : registry.FindExport(module->base, "CreateFileA");
    if (module == nullptr || get_version == nullptr || create_file == nullptr ||
        state->modules_.FindGate(kKernel32Module, "GetProcAddress") == nullptr ||
        state->modules_.FindGate(kKernel32Module, "GetVersion") == nullptr ||
        state->modules_.FindGate(kKernel32Module, "CreateFileA") == nullptr)
    {
        *error = "kernel32 facade registration is incomplete";
        return false;
    }
    for (const NativeImportSlotBinding& slot : session->mutable_import_thunks()->slots)
    {
        // Read the slot back while the image is still mapped, so the static
        // path is evidence rather than the value we believe we wrote.
        if (slot.gate.name == "CreateFileA" && slot.slot != nullptr)
        {
            std::uint32_t written = 0;
            std::memcpy(&written, slot.slot, sizeof(written));
            state->static_create_file_slot_ = runtime::GuestAddress(written);
        }
    }
    state->session_gates_ = session->mutable_gates()->gates();
    state->teb_ = session->bootstrap().Teb();
    // The TEB's ClientId, as Windows fills it: the process, then the thread.
    const std::uint32_t client_id[2] = {hle::GuestProcess::kProcessId, hle::GuestProcess::kThreadId};
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(state->teb_ + 0x20)), client_id,
                sizeof(client_id));
    state->thread_ids_[state->teb_] = hle::GuestProcess::kThreadId;
    state->registry_kernel32_base_ = module->base;
    state->registry_get_version_ = get_version->thunk_address;
    state->registry_create_file_ = create_file->thunk_address;
    error->clear();
    return true;
}

bool NativeKernel32Diagnostic::Dispatch(const NativeImportGateEvent& event,
                                        NativeImportGateResult* output)
{
    stack_base_ = event.guest_stack_base;
    stack_limit_ = event.guest_stack_limit;
    if (modules_.FindGate(runtime::GuestAddress(event.gate_address)) == nullptr)
    {
        if (unhandled_import_.empty())
        {
            const runtime::ImportGate* gate = FindSessionGate(event.gate_address);
            unhandled_import_ = gate == nullptr ? "<unknown gate>" : FormatGate(*gate);
        }
        return false;
    }
    // A handler that calls into the guest dispatches nested imports before it
    // returns, so the record and error stay local until this call is done.
    hle::ApiCallRecord record;
    std::string dispatch_error;
    const hle::RecordingImportCallServices recording(*this, &record);
    const bool handled = modules_.Dispatch(event, output, &dispatch_error, &recording);
    last_record_ = std::move(record);
    dispatch_error_ = std::move(dispatch_error);
    return handled;
}

bool NativeKernel32Diagnostic::CallGuest(hle::GuestCall* call,
                                         std::uint32_t* result,
                                         std::string* error) const
{
    if (call == nullptr || result == nullptr)
    {
        if (error != nullptr) *error = "guest call is null";
        return false;
    }
    std::string call_error;
    const bool called = CallNativeGuestStdcall(call->function,
                                               call->arguments,
                                               call->data,
                                               call->data_argument,
                                               result,
                                               &call_error);
    if (error != nullptr)
    {
        *error = std::move(call_error);
    }
    return called;
}

bool NativeKernel32Diagnostic::IsExportCall(const NativeImportGateEvent& event,
                                            std::string_view export_name) const
{
    const runtime::ImportGate* gate = modules_.FindGate(kKernel32Module, export_name);
    return gate != nullptr && gate->address.value() == event.gate_address;
}

bool NativeKernel32Diagnostic::ReadRequestedExportName(const NativeImportGateEvent& event,
                                                       std::string* name)
{
    if (name == nullptr || !StackContains(event, event.stack_pointer, 3 * sizeof(std::uint32_t)))
    {
        return false;
    }
    stack_base_ = event.guest_stack_base;
    stack_limit_ = event.guest_stack_limit;
    const auto* stack =
        reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(event.stack_pointer));
    if ((stack[2] & 0xFFFF0000U) == 0)
    {
        return false;
    }
    std::string error;
    return ReadGuestString(runtime::GuestAddress(stack[2]), name, &error);
}

bool NativeKernel32Diagnostic::ReadArgumentString(const NativeImportGateEvent& event,
                                                  std::uint32_t index,
                                                  std::string* value)
{
    const std::uint32_t offset = (index + 1) * sizeof(std::uint32_t);
    if (value == nullptr ||
        !StackContains(event, event.stack_pointer, offset + sizeof(std::uint32_t)))
    {
        return false;
    }
    stack_base_ = event.guest_stack_base;
    stack_limit_ = event.guest_stack_limit;
    std::uint32_t address = 0;
    std::memcpy(&address,
                reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(event.stack_pointer + offset)),
                sizeof(address));
    std::string error;
    return ReadGuestString(runtime::GuestAddress(address), value, &error);
}

const hle::modules::GuestExportDescriptor* NativeKernel32Diagnostic::FindFacadeExport(
    const NativeImportGateEvent& event) const
{
    return modules_.FindExport(runtime::GuestAddress(event.gate_address));
}

std::string NativeKernel32Diagnostic::GateName(const NativeImportGateEvent& event) const
{
    const runtime::ImportGate* gate = modules_.FindGate(runtime::GuestAddress(event.gate_address));
    if (gate == nullptr)
    {
        gate = FindSessionGate(event.gate_address);
    }
    return gate == nullptr ? "<unknown gate>" : FormatGate(*gate);
}

bool NativeKernel32Diagnostic::PrepareStopStub(std::string* error)
{
    if (stop_stub_ != 0)
    {
        return true;
    }
    const auto page_size = static_cast<std::size_t>(HostPageSize());
    // The guest returns into the stub, so it must be guest-addressable.
    NativeLowMemory mapping;
    std::string mapping_error;
    if (!MapNativeLowMemory(static_cast<std::uint32_t>(page_size),
                            HostProtection::kReadWrite,
                            &mapping,
                            &mapping_error))
    {
        if (error != nullptr)
        {
            *error = "cannot allocate the diagnostic stop stub: " + mapping_error;
        }
        return false;
    }
    void* page = mapping.memory;
    std::memset(page, 0xCC, page_size);
    if (!HostProtect(page, page_size, HostProtection::kReadExecute))
    {
        HostUnmap(page, page_size);
        if (error != nullptr)
        {
            *error = "cannot make the diagnostic stop stub executable";
        }
        return false;
    }
    stop_stub_ = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(page));
    // The stub's INT3 ends the diagnostic; it must not reach a guest SEH frame
    // such as the CRT's.
    SetNativeHostTrapRange(stop_stub_, static_cast<std::uint32_t>(page_size));
    return true;
}

bool NativeKernel32Diagnostic::RedirectReturnToStop(const NativeImportGateEvent& event)
{
    if (stop_stub_ == 0 || !StackContains(event, event.stack_pointer, sizeof(std::uint32_t)))
    {
        return false;
    }
    auto* slot = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(event.stack_pointer));
    std::memcpy(&stopped_return_address_, slot, sizeof(stopped_return_address_));
    std::memcpy(slot, &stop_stub_, sizeof(stop_stub_));
    return true;
}

bool NativeKernel32Diagnostic::ImageContains(std::uint32_t address, std::size_t size) const
{
    if (address < image_base_)
    {
        return false;
    }
    const std::uint32_t offset = address - image_base_;
    return offset <= image_size_ && size <= image_size_ - offset;
}

void NativeKernel32Diagnostic::CopyTo(OriginalRunResult* result) const
{
    if (result == nullptr)
    {
        return;
    }
    OriginalResolverIdentity& identity = result->resolver_identity;
    identity.prepared = prepared();
    identity.registry_kernel32_base = registry_kernel32_base_;
    identity.registry_get_version = registry_get_version_;
    identity.registry_create_file = registry_create_file_;
    identity.kernel32_base = kernel32_base_;
    identity.get_version_address = get_version_address_;
    identity.create_file_address = create_file_address_;
    identity.static_imports_rebound = !rebindings_.empty();
    identity.static_create_file_slot = static_create_file_slot_;
    result->unhandled_dynamic_request_observed = !unhandled_dynamic_request_.empty();
    result->unhandled_dynamic_request = unhandled_dynamic_request_;
    result->unhandled_import_observed = !unhandled_import_.empty();
    result->unhandled_import = unhandled_import_;
}

bool NativeKernel32Diagnostic::ReadGuestString(runtime::GuestAddress address,
                                               std::string* value,
                                               std::string* error) const
{
    if (value == nullptr || error == nullptr || address.value() == 0)
    {
        if (error != nullptr)
        {
            *error = "invalid guest string request";
        }
        return false;
    }
    value->clear();
    for (std::size_t index = 0; index < kMaximumGuestStringLength; ++index)
    {
        const std::uint32_t current = address.value() + static_cast<std::uint32_t>(index);
        if (!GuestRangeReadable(current, 1))
        {
            *error = "guest string lies outside readable diagnostic ranges";
            return false;
        }
        const char value_byte =
            *reinterpret_cast<const char*>(static_cast<std::uintptr_t>(current));
        if (value_byte == '\0')
        {
            error->clear();
            return !value->empty();
        }
        value->push_back(value_byte);
    }
    *error = "guest string exceeds the diagnostic limit";
    return false;
}

runtime::GuestAddress NativeKernel32Diagnostic::FindGuestModule(std::string_view name) const
{
    const hle::modules::RegisteredGuestModule* module = modules_.registry().FindModule(name);
    if (module == nullptr)
    {
        if (!hle::modules::IsAbsentGuestModule(name))
        {
            RecordUnresolvedLookup("GetModuleHandleA(" + std::string(name) + ")");
        }
        return {};
    }
    if (module->base == registry_kernel32_base_)
    {
        kernel32_base_ = module->base;
    }
    return module->base;
}

runtime::GuestAddress NativeKernel32Diagnostic::FindGuestExport(runtime::GuestAddress module,
                                                                std::string_view name) const
{
    const hle::modules::RegisteredGuestExport* export_entry =
        modules_.registry().FindExport(module, name);
    if (export_entry == nullptr)
    {
        if (!modules_.registry().IsAbsentExport(module, name))
        {
            RecordUnresolvedLookup("GetProcAddress(" + std::string(name) + ")");
        }
        return {};
    }
    if (name == "GetVersion")
    {
        get_version_address_ = export_entry->thunk_address;
    }
    else if (name == "CreateFileA")
    {
        create_file_address_ = export_entry->thunk_address;
    }
    return export_entry->thunk_address;
}

runtime::GuestAddress NativeKernel32Diagnostic::FindGuestExport(runtime::GuestAddress module,
                                                                std::uint16_t ordinal) const
{
    const hle::modules::RegisteredGuestExport* export_entry =
        modules_.registry().FindExport(module, ordinal);
    if (export_entry == nullptr)
    {
        RecordUnresolvedLookup("GetProcAddress(#" + std::to_string(ordinal) + ")");
        return {};
    }
    return export_entry->thunk_address;
}

void NativeKernel32Diagnostic::RecordUnresolvedLookup(std::string request) const
{
    if (unhandled_dynamic_request_.empty())
    {
        unhandled_dynamic_request_ = std::move(request);
    }
}

const runtime::ImportGate* NativeKernel32Diagnostic::FindSessionGate(std::uint32_t address) const
{
    for (const runtime::ImportGate& gate : session_gates_)
    {
        if (gate.address.value() == address)
        {
            return &gate;
        }
    }
    return nullptr;
}

bool NativeKernel32Diagnostic::GuestRangeReadable(std::uint32_t address, std::size_t size) const
{
    if (size == 0)
    {
        return true;
    }
    const bool in_stack = address >= stack_limit_ && address <= stack_base_ &&
                          size <= stack_base_ - address;
    if (size > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    const auto size32 = static_cast<std::uint32_t>(size);
    constexpr std::uint32_t kTebSize = 0x1000;
    const bool in_teb = teb_ != 0 && address >= teb_ && address - teb_ < kTebSize && size32 <= kTebSize - (address - teb_);
    return ImageContains(address, size) || in_stack || in_teb || process_.BlockContains(address, size32) ||
           process_.Accessible(address, size32) || NativeGuestThreadMemoryContains(address, size32);
}

bool NativeKernel32Diagnostic::IsGuestModule(runtime::GuestAddress handle) const
{
    return modules_.registry().FindModule(handle) != nullptr;
}

std::string NativeKernel32Diagnostic::GuestModuleName(runtime::GuestAddress handle) const
{
    const hle::modules::RegisteredGuestModule* module = modules_.registry().FindModule(handle);
    return module == nullptr ? std::string() : module->name;
}

bool NativeKernel32Diagnostic::IsAbsentExport(std::uint32_t module_handle,
                                              std::string_view name) const
{
    return modules_.registry().IsAbsentExport(runtime::GuestAddress(module_handle), name);
}

hle::GuestProcess* NativeKernel32Diagnostic::Process() const
{
    return &process_;
}

bool NativeKernel32Diagnostic::WaitMilliseconds(std::uint32_t milliseconds) const
{
    WaitNativeGuestThread(milliseconds);
    return true;
}

runtime::GuestAddress NativeKernel32Diagnostic::ThreadEnvironmentBlock() const
{
    const NativeGuestThread* thread = CurrentNativeGuestThread();
    return runtime::GuestAddress(thread != nullptr && thread->teb != 0 ? thread->teb : teb_);
}

std::uint32_t NativeKernel32Diagnostic::CurrentThreadId() const
{
    const auto id = thread_ids_.find(ThreadEnvironmentBlock().value());
    return id == thread_ids_.end() ? hle::GuestProcess::kThreadId : id->second;
}

namespace
{

void FinishGuestThread(void* context, std::uint32_t thread_id, std::uint32_t exit_code)
{
    static_cast<hle::GuestProcess*>(context)->FinishThread(thread_id, exit_code);
}

}  // namespace

bool NativeKernel32Diagnostic::StartGuestThread(std::uint32_t start,
                                                std::uint32_t parameter,
                                                std::uint32_t thread_id,
                                                std::string* error) const
{
    NativeGuestThreadStart thread_start;
    thread_start.start = start;
    thread_start.parameter = parameter;
    thread_start.token = thread_id;
    thread_start.on_exit = &FinishGuestThread;
    thread_start.exit_context = &process_;
    std::uint32_t teb = 0;
    if (!StartNativeGuestThread(thread_start, &teb, error))
    {
        return false;
    }
    // The new thread runs only once this one gives up the guest lock, so its
    // TEB is complete before it starts.
    const std::uint32_t client_id[2] = {hle::GuestProcess::kProcessId, thread_id};
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(teb + 0x20)), client_id, sizeof(client_id));
    thread_ids_[teb] = thread_id;
    return true;
}

bool NativeKernel32Diagnostic::ReadClock(hle::GuestClockReading* reading) const
{
    HostWallClock now;
    if (reading == nullptr || !HostReadWallClock(&now) || now.unix_seconds < 0)
    {
        return false;
    }
    reading->utc_file_time = hle::kFileTimeUnixEpoch +
                             static_cast<std::uint64_t>(now.unix_seconds) * hle::kFileTimeTicksPerSecond +
                             static_cast<std::uint64_t>(now.nanoseconds) / 100U;
    reading->local_offset_minutes = now.local_offset_minutes;
    reading->tick_ms = static_cast<std::uint32_t>(HostMonotonicMilliseconds());
    return true;
}

bool NativeKernel32Diagnostic::ReadGuestBytes(runtime::GuestAddress address,
                                              std::span<std::uint8_t> bytes,
                                              std::string* error) const
{
    if (!GuestRangeReadable(address.value(), bytes.size()))
    {
        if (error != nullptr) *error = "guest bytes lie outside readable diagnostic ranges";
        return false;
    }
    if (!bytes.empty())
    {
        std::memcpy(bytes.data(),
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address.value())),
                    bytes.size());
    }
    return true;
}

bool NativeKernel32Diagnostic::WriteGuestBytes(runtime::GuestAddress address,
                                               std::span<const std::uint8_t> bytes,
                                               std::string* error) const
{
    if (!GuestRangeReadable(address.value(), bytes.size()))
    {
        if (error != nullptr) *error = "guest bytes lie outside writable diagnostic ranges";
        return false;
    }
    if (!bytes.empty())
    {
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address.value())),
                    bytes.data(),
                    bytes.size());
    }
    return true;
}

hle::GuestDeviceSet* NativeKernel32Diagnostic::Devices() const
{
    return &devices_;
}

// Each thread's last error lives where Windows keeps it, at TEB+0x34
// (LastErrorValue); before a TEB exists, in the diagnostic.
void NativeKernel32Diagnostic::SetLastError(std::uint32_t value) const
{
    const std::uint32_t teb = ThreadEnvironmentBlock().value();
    if (teb == 0)
    {
        last_error_ = value;
        return;
    }
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(teb + 0x34)), &value, sizeof(value));
}

std::uint32_t NativeKernel32Diagnostic::LastError() const
{
    const std::uint32_t teb = ThreadEnvironmentBlock().value();
    if (teb == 0)
    {
        return last_error_;
    }
    std::uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(teb + 0x34)), sizeof(value));
    return value;
}

void NativeKernel32Diagnostic::ConfigureDevices(hle::GuestDeviceConfig config)
{
    devices_ = hle::GuestDeviceSet(std::move(config));
    devices_.SetHandleAllocator(&process_.handles());
}

bool NativeKernel32Diagnostic::ConfigureFiles(hle::GuestFileConfig config, std::string* error)
{
    files_.SetHandleAllocator(&process_.handles());
    return files_.Configure(std::move(config), error);
}

hle::GuestFiles* NativeKernel32Diagnostic::Files() const
{
    return files_.configured() ? &files_ : nullptr;
}

void NativeKernel32Diagnostic::DescribeImage(const exe::PeImageInfo& image_info,
                                             std::string module_path)
{
    process_.SetMainImage(image_base_, std::move(module_path));
    std::vector<hle::GuestImageSection> sections;
    sections.reserve(image_info.sections.size());
    for (const exe::PeSection& section : image_info.sections)
    {
        sections.push_back({section.virtual_address, section.virtual_size, section.characteristics});
    }
    process_.AddImageRegion(image_base_, image_info.size_of_image, sections);
}

}  // namespace re2dj::platform::native
