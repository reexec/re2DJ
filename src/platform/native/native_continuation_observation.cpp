#include "re2dj/platform/native/original_runner.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>


#include <spdlog/logger.h>

#include "game_controls.h"
#include "native_host_services.h"
#include "native_image_dump.h"
#include "native_in_process_runner.h"
#include "native_kernel32_diagnostic.h"
#include "native_pe_session.h"
#include "native_legacy_io.h"
#include "re2dj/hle/api_call_record.h"
#include "re2dj/hle/host_presentation.h"
#include "re2dj/hle/modules/guest_module.h"
#include "re2dj/logging/logging.h"

namespace re2dj::platform::native
{
namespace
{

bool ReadExecutable(const std::filesystem::path& path,
                    std::vector<std::uint8_t>* bytes,
                    std::string* error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        *error = "cannot open the selected guest executable";
        return false;
    }
    bytes->assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (stream.bad() || bytes->empty())
    {
        *error = "cannot read the selected guest executable";
        return false;
    }
    return true;
}

// Indices of up to two ANSI string arguments for facade exports that take
// them, -1 for none. A by-ordinal GetProcAddress request fails the string
// read and records none.
struct StringArguments
{
    int first = -1;
    int second = -1;
};

StringArguments StringArgumentIndices(std::string_view export_name)
{
    if (export_name == "GetModuleHandleA" || export_name == "CreateFileA" || export_name == "FindFirstFileA" ||
        export_name == "SetCurrentDirectoryA" || export_name == "GetFileAttributesA" ||
        export_name == "LoadLibraryA" || export_name == "GetEnvironmentVariableA")
    {
        return {0, -1};
    }
    if (export_name == "GetProcAddress" || export_name == "DrawTextA" || export_name == "wsprintfA")
    {
        return {1, -1};
    }
    if (export_name == "MessageBoxA")
    {
        // lpText, then lpCaption.
        return {1, 2};
    }
    return {};
}

struct ContinuationContext
{
    ContinuationContext(std::uint32_t image_base, std::uint32_t image_size)
        : kernel32(image_base, image_size)
    {
    }

    NativeKernel32Diagnostic kernel32;
    std::vector<OriginalApiCall> calls;
    std::deque<OriginalApiCall> tail_calls;
    std::uint32_t call_count = 0;
    std::uint32_t call_limit = 0;
    std::uint32_t api_log_calls = kOriginalApiLogFullCalls;
    hle::HostPresentation* presentation = nullptr;
    // Calls each guest thread is handling; above one, the guest is inside a
    // guest call.
    std::map<std::uint32_t, std::uint32_t> depths;
    // What each import gate is, worked out the first time the guest calls
    // it: every import passes here, and a gate does not change.
    struct GateInfo
    {
        std::string name;
        const hle::modules::GuestExportDescriptor* facade_export = nullptr;
        StringArguments strings;
    };
    std::unordered_map<std::uint32_t, GateInfo> gates;
    bool stop_requested = false;
    bool stop_redirect_failed = false;
    OriginalRunBoundary stop = OriginalRunBoundary::kStopped;
    std::string stop_detail;
    // --image-dump: the request, what the dumps say about the build, where
    // the image is, and when the entry dump was taken.
    const OriginalRunEnvironment::ImageDumpRequest* image_dump = nullptr;
    NativeImageDumpAttribution dump_attribution;
    const void* dump_image = nullptr;
    std::uint64_t dump_start_ms = 0;
    bool resumed_dump_pending = false;
};

void WriteImageDump(ContinuationContext* state, const char* point, std::uint32_t delay_ms)
{
    state->dump_attribution.delay_milliseconds = delay_ms;
    std::string dump_error;
    const bool written = WriteNativeImageDump(state->dump_image, state->image_dump->directory, state->image_dump->stem,
                                              point, state->dump_attribution, &dump_error);
    const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
    if (logger == nullptr)
    {
        return;
    }
    if (written)
    {
        logger->info("image dump      : {} written to {}", point,
                     (state->image_dump->directory / (state->image_dump->stem + "." + point + ".image.bin")).string());
    }
    else
    {
        logger->warn("image dump      : {} failed: {}", point, dump_error);
    }
}

// The run's setup, then the entry dump: the image is mapped and its imports
// bound, and nothing of the guest has run yet.
bool SetupWithImageDump(NativePeSession* session, void* opaque, std::string* error)
{
    auto* state = static_cast<ContinuationContext*>(opaque);
    if (!NativeKernel32Diagnostic::Setup(session, &state->kernel32, error))
    {
        return false;
    }
    if (state->image_dump != nullptr && state->image_dump->enabled)
    {
        state->dump_image = session->image().memory;
        WriteImageDump(state, "entry", 0);
        state->dump_start_ms = HostMonotonicMilliseconds();
        state->resumed_dump_pending = true;
    }
    return true;
}

// Writes the start of one call to the API log: its name, return address,
// arguments, and string arguments. Calls the guest makes from a guest call
// (a window procedure, say) come before their parent's outcome, indented by
// depth.
void LogApiCallHead(const OriginalApiCall& call, std::uint32_t depth)
{
    const std::shared_ptr<spdlog::logger> logger = logging::GetApiLogger();
    if (logger == nullptr)
    {
        return;
    }
    const std::string indent(depth * 4, ' ');
    std::string arguments;
    char word[12] = {};
    char thread[24] = {};
    if (call.thread_id != 0)
    {
        std::snprintf(thread, sizeof(thread), "[thread %04x] ", call.thread_id);
    }
    for (std::uint32_t index = 0; index < call.argument_count; ++index)
    {
        std::snprintf(word, sizeof(word), index == 0 ? "%08x" : ", %08x", call.arguments[index]);
        arguments += word;
    }
    char head[256] = {};
    std::snprintf(head,
                  sizeof(head),
                  "#%04u %s%s ret=%08x args=(%s)",
                  call.sequence,
                  thread,
                  call.name.c_str(),
                  call.return_address,
                  arguments.c_str());
    logger->info("{}{}", indent, head);
    // String arguments read for the call summary, whether or not the handler
    // itself reads them.
    if (call.text_observed)
    {
        logger->info("{}      arg   \"{}\"", indent, call.text);
    }
    if (call.second_text_observed)
    {
        logger->info("{}      arg   \"{}\"", indent, call.second_text);
    }
}

// Writes the rest of one call to the API log: what the handler read, wrote,
// and called, and its outcome. DeviceIoControl buffers carry Hardlock data
// derived from the user's material, so only their lengths are logged.
void LogApiCallOutcome(const NativeKernel32Diagnostic& kernel32,
                       const OriginalApiCall& call,
                       bool facade_call,
                       const NativeImportGateResult* output,
                       std::uint32_t depth)
{
    const std::shared_ptr<spdlog::logger> logger = logging::GetApiLogger();
    if (logger == nullptr)
    {
        return;
    }
    const std::string indent(depth * 4, ' ');
    if (!facade_call)
    {
        logger->info("{}      -> UNHANDLED: no facade export", indent);
        return;
    }
    const bool withhold = call.name == "kernel32.dll!DeviceIoControl";
    for (const std::string& line : hle::FormatApiCallEvents(kernel32.last_record(), withhold))
    {
        logger->info("{}      {}", indent, line);
    }
    if (!call.handled)
    {
        logger->info("{}      -> UNHANDLED: {}", indent, kernel32.dispatch_error());
        return;
    }
    char result[96] = {};
    std::snprintf(result,
                  sizeof(result),
                  "      -> eax=%08x edx=%08x last_error=%u",
                  output->eax,
                  output->edx,
                  kernel32.LastError());
    logger->info("{}{}", indent, result);
}

void RequestStop(ContinuationContext* state,
                 const NativeImportGateEvent& event,
                 OriginalRunBoundary boundary,
                 std::string detail)
{
    state->stop_requested = true;
    state->stop = boundary;
    state->stop_detail = std::move(detail);
    state->stop_redirect_failed = !state->kernel32.RedirectReturnToStop(event);
}

bool HandleContinuationGate(const NativeImportGateEvent& event,
                            NativeImportGateResult* output,
                            void* opaque)
{
    auto* state = static_cast<ContinuationContext*>(opaque);
    if (output == nullptr || state == nullptr || state->stop_requested)
    {
        return false;
    }
    // The resumed dump at the first import once the delay has passed: the
    // guest is stopped in an import, and its image is surely mapped.
    if (state->resumed_dump_pending)
    {
        const std::uint64_t elapsed = HostMonotonicMilliseconds() - state->dump_start_ms;
        if (elapsed >= state->image_dump->delay_ms)
        {
            state->resumed_dump_pending = false;
            WriteImageDump(state, "resumed", static_cast<std::uint32_t>(elapsed));
        }
    }

    OriginalApiCall call;
    call.sequence = ++state->call_count;
    const std::uint32_t thread_id = state->kernel32.CurrentThreadId();
    call.thread_id = thread_id == hle::GuestProcess::kThreadId ? 0 : thread_id;
    std::uint32_t& depth = state->depths[thread_id];
    auto gate = state->gates.find(event.gate_address);
    if (gate == state->gates.end())
    {
        ContinuationContext::GateInfo info;
        info.name = state->kernel32.GateName(event);
        info.facade_export = state->kernel32.FindFacadeExport(event);
        if (info.facade_export != nullptr)
        {
            info.strings = StringArgumentIndices(info.facade_export->name);
        }
        gate = state->gates.emplace(event.gate_address, std::move(info)).first;
    }
    call.name = gate->second.name;
    call.return_address = event.instruction_pointer;
    const hle::modules::GuestExportDescriptor* facade_export = gate->second.facade_export;
    if (facade_export != nullptr)
    {
        call.argument_count = (std::min)(facade_export->argument_count,
                                         static_cast<std::uint32_t>(call.arguments.size()));
        const std::size_t argument_bytes = call.argument_count * sizeof(std::uint32_t);
        if (event.stack_pointer >= event.guest_stack_limit &&
            event.stack_pointer <= event.guest_stack_base &&
            sizeof(std::uint32_t) + argument_bytes <=
                event.guest_stack_base - event.stack_pointer)
        {
            std::memcpy(call.arguments.data(),
                        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                            event.stack_pointer + sizeof(std::uint32_t))),
                        argument_bytes);
        }
        else
        {
            call.argument_count = 0;
        }
        const StringArguments& strings = gate->second.strings;
        if (strings.first >= 0)
        {
            call.text_observed = state->kernel32.ReadArgumentString(
                event, static_cast<std::uint32_t>(strings.first), &call.text);
        }
        if (strings.second >= 0)
        {
            call.second_text_observed = state->kernel32.ReadArgumentString(
                event, static_cast<std::uint32_t>(strings.second), &call.second_text);
        }
    }

    const bool logged = state->api_log_calls == 0 || call.sequence <= state->api_log_calls;
    if (logged)
    {
        LogApiCallHead(call, depth);
    }
    else if (call.sequence == state->api_log_calls + 1)
    {
        const std::shared_ptr<spdlog::logger> logger = logging::GetApiLogger();
        if (logger != nullptr)
        {
            logger->info("... calls after #{} are not recorded here; the last {} are reported with the result",
                         state->api_log_calls, kOriginalApiCallLogTail);
        }
    }
    ++depth;
    call.handled = state->kernel32.Dispatch(event, output);
    --depth;
    call.eax = call.handled ? output->eax : 0;
    // The call a run stops on is recorded past the log's limit too, so its
    // reason is there to read.
    if (!logged && !call.handled)
    {
        LogApiCallHead(call, depth);
    }
    if (logged || !call.handled)
    {
        LogApiCallOutcome(state->kernel32, call, facade_export != nullptr, output, depth);
    }
    // The record goes into the run's lists last, by move: it is copied for
    // every import otherwise.
    const auto remember = [state, &call]()
    {
        if (state->calls.size() < kOriginalApiCallLogHead)
        {
            state->calls.push_back(std::move(call));
        }
        else
        {
            state->tail_calls.push_back(std::move(call));
            if (state->tail_calls.size() > kOriginalApiCallLogTail)
            {
                state->tail_calls.pop_front();
            }
        }
    };

    if (!call.handled)
    {
        RequestStop(state, event, OriginalRunBoundary::kContinuationUnhandledImport, call.name);
        remember();
        return false;
    }
    // A null GetModuleHandleA result is a legitimate Win32 outcome the guest
    // may tolerate, so it is only logged. A null GetProcAddress or LoadLibraryA
    // result, including one for a null module handle, leaves the guest without
    // something Windows would have given it, unless the name is declared
    // absent on Windows too.
    const bool resolver = facade_export != nullptr &&
                          facade_export->name == "GetProcAddress";
    const bool loader = facade_export != nullptr &&
                        facade_export->name == "LoadLibraryA";
    const bool absent =
        call.text_observed &&
        ((resolver && state->kernel32.IsAbsentExport(call.arguments[0], call.text)) ||
         (loader && hle::modules::IsAbsentGuestModule(call.text)));
    if (loader && call.eax == 0 && !absent)
    {
        RequestStop(state,
                    event,
                    OriginalRunBoundary::kContinuationUnresolvedLookup,
                    "LoadLibraryA(" + (call.text_observed ? call.text : std::string("?")) + ")");
    }
    else if (resolver && call.eax == 0 && !absent)
    {
        const std::string requested = call.text_observed
            ? call.text
            : "#" + std::to_string(call.arguments[1] & 0xFFFFU);
        char module[16];
        std::snprintf(module, sizeof(module), "%08x", call.arguments[0]);
        RequestStop(state,
                    event,
                    OriginalRunBoundary::kContinuationUnresolvedLookup,
                    "GetProcAddress(" + std::string(module) + ", " + requested + ")");
    }
    else if (state->presentation != nullptr && state->presentation->CloseRequested())
    {
        RequestStop(state, event, OriginalRunBoundary::kContinuationHostClosed, call.name);
    }
    else if (state->call_limit != 0 && state->call_count >= state->call_limit)
    {
        RequestStop(state, event, OriginalRunBoundary::kContinuationCallLimit, call.name);
    }
    remember();
    return true;
}

}  // namespace

bool RunOriginalInProcessContinuation(const std::filesystem::path& executable_path,
                                      const exe::PeImageInfo& image_info,
                                      const OriginalRunEnvironment& environment,
                                      OriginalRunResult* result,
                                      std::string* error)
{
    if (result == nullptr || error == nullptr)
    {
        return false;
    }

    std::vector<std::uint8_t> file_bytes;
    if (!ReadExecutable(executable_path, &file_bytes, error))
    {
        return false;
    }

    ContinuationContext context(static_cast<std::uint32_t>(image_info.image_base),
                                image_info.size_of_image);
    context.kernel32.ConfigureDevices(environment.devices);
    context.kernel32.SetPresentation(environment.presentation);
    context.kernel32.SetAudio(environment.audio);
    context.kernel32.SetProcessLauncher(environment.process_launcher);
    context.kernel32.SetStartup(environment.startup);
    context.presentation = environment.presentation;
    context.call_limit = environment.call_limit;
    context.api_log_calls = environment.api_log_calls;
    context.image_dump = &environment.image_dump;
    if (environment.image_dump.enabled)
    {
        NativeImageDumpAttribution& attribution = context.dump_attribution;
        attribution.target_id = environment.image_dump.target_id;
        attribution.executable_path = executable_path.string();
        attribution.timestamp = image_info.timestamp;
        attribution.size_of_image = image_info.size_of_image;
        attribution.entry_point_rva = image_info.entry_point_rva;
        attribution.image_base = static_cast<std::uint32_t>(image_info.image_base);
        attribution.file_size = file_bytes.size();
        attribution.file_digest = NativeImageDumpDigest(file_bytes.data(), file_bytes.size());
        attribution.re2dj_version = environment.image_dump.re2dj_version;
    }
    context.kernel32.DescribeImage(image_info, environment.module_path);
    if (!context.kernel32.ConfigureFiles(environment.files, error))
    {
        return false;
    }
    if (!environment.current_directory.empty() && context.kernel32.Files() != nullptr)
    {
        bool outside_root = false;
        if (context.kernel32.Files()->SetCurrentDirectory(environment.current_directory, &outside_root) != 0 ||
            outside_root)
        {
            *error = "the launcher's current directory is not a guest directory: " + environment.current_directory;
            return false;
        }
    }
    if (!context.kernel32.PrepareStopStub(error))
    {
        return false;
    }

    NativeInProcessRunResult run;
    *result = {};
    input::LegacyIoTrapPolicy legacy_io = environment.legacy_io;
    legacy_io.image_base = context.kernel32.image_base();
    ArmAutoplayFlag(environment.autoplay_flag_rva == 0 ? 0
                                                       : context.kernel32.image_base() + environment.autoplay_flag_rva);
    SetNativeLegacyIo(legacy_io,
                      environment.presentation == nullptr ? nullptr : &environment.presentation->Input(),
                      environment.io_bindings);
    const bool completed = RunConfiguredNativePeInProcess(file_bytes,
                                                          image_info,
                                                          context.kernel32.image_base(),
                                                          &HandleContinuationGate,
                                                          &context,
                                                          &SetupWithImageDump,
                                                          &context,
                                                          &run,
                                                          error);
    ClearNativeLegacyIo();
    if (context.resumed_dump_pending)
    {
        const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
        if (logger != nullptr)
        {
            logger->warn("image dump      : resumed skipped, the run ended {} ms after the entry dump (delay {} ms)",
                         HostMonotonicMilliseconds() - context.dump_start_ms, environment.image_dump.delay_ms);
        }
    }
    const NativeLegacyIoActivity legacy_io_activity = NativeLegacyIoActivitySnapshot();
    const std::uint32_t stop_address = context.kernel32.stop_stub();
    if (context.stop_requested && !context.stop_redirect_failed &&
        run.fault.kind == NativeFaultKind::kBreakpoint && run.fault.instruction_pointer == stop_address + 1)
    {
        result->boundary = context.stop;
        result->import_return_address = context.kernel32.stopped_return_address();
        result->instruction_pointer = runtime::GuestAddress(context.kernel32.stopped_return_address());
    }
    else if (completed)
    {
        result->boundary = OriginalRunBoundary::kProcessExit;
        result->status_code = run.exit_code;
        char detail[40];
        std::snprintf(detail,
                      sizeof(detail),
                      run.process_exited ? "ExitProcess(0x%08x)" : "entry returned 0x%08x",
                      run.exit_code);
        context.stop_detail = detail;
    }
    else if (context.kernel32.prepared() && run.fault.status_code != 0)
    {
        result->boundary = OriginalRunBoundary::kContinuationFault;
        result->instruction_pointer = runtime::GuestAddress(run.fault.instruction_pointer);
        result->status_code = run.fault.status_code;
        CopyNativeFaultObservation(run, result);
    }
    else
    {
        const std::string cause = error->empty() ? "" : ": " + *error;
        *error = "continuation did not reach a confirmed diagnostic boundary (calls=" +
                 std::to_string(context.call_count) +
                 ", stop_requested=" + std::to_string(context.stop_requested ? 1 : 0) +
                 ", redirect_failed=" + std::to_string(context.stop_redirect_failed ? 1 : 0) +
                 ", signal=" + std::to_string(run.fault.status_code) +
                 ", eip=" + std::to_string(run.fault.instruction_pointer) + ")" + cause;
        return false;
    }

    const std::uint32_t image_base = context.kernel32.image_base();
    result->load_base = runtime::GuestAddress(image_base);
    result->entry_point = runtime::GuestAddress(image_base + image_info.entry_point_rva);
    result->seh_dispatch_count = run.seh_dispatch_count;
    result->last_seh_handler = runtime::GuestAddress(run.last_seh_handler);
    result->last_seh_resumed_eip = runtime::GuestAddress(run.last_seh_resumed_eip);
    result->api_calls = std::move(context.calls);
    result->api_calls.insert(result->api_calls.end(),
                             context.tail_calls.begin(),
                             context.tail_calls.end());
    result->api_call_count = context.call_count;
    result->continuation_stop_detail = context.stop_detail;
    context.kernel32.CopyTo(result);
    result->device_activity = context.kernel32.devices().activity();
    result->hardlock_material_applied = environment.devices.hardlock.has_value();
    result->legacy_io_reads = legacy_io_activity.reads;
    result->legacy_io_writes = legacy_io_activity.writes;
    result->legacy_io_unanswered = legacy_io_activity.unanswered;
    result->legacy_io_first_port = legacy_io_activity.first_port;
    result->legacy_io_first_read = legacy_io_activity.first_read;
    error->clear();
    return true;
}

}  // namespace re2dj::platform::native
