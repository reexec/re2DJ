#ifndef RE2DJ_PLATFORM_NATIVE_NATIVE_KERNEL32_DIAGNOSTIC_H_
#define RE2DJ_PLATFORM_NATIVE_NATIVE_KERNEL32_DIAGNOSTIC_H_

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "native_guest_module_set.h"
#include "native_import_bridge.h"
#include "native_import_thunks.h"
#include "native_low_memory.h"
#include "re2dj/exe/pe_image.h"
#include "re2dj/hle/api_call_record.h"
#include "re2dj/hle/guest_devices.h"
#include "re2dj/hle/guest_files.h"
#include "re2dj/hle/guest_process.h"
#include "re2dj/hle/import_dispatcher.h"
#include "re2dj/platform/native/original_runner.h"
#include "re2dj/runtime/pe_loader.h"

namespace re2dj::platform::native
{

class NativePeSession;

// Shared state for the Linux in-process diagnostics that run the original
// image against the guest facade modules. It owns facade registration,
// static IAT rebinding, the bounded guest-memory services the kernel32 handlers need, and
// the resolver-identity observation. Diagnostics keep only their own boundary.
class NativeKernel32Diagnostic final : public hle::ImportCallServices
{
public:
    NativeKernel32Diagnostic(std::uint32_t image_base, std::uint32_t image_size);
    ~NativeKernel32Diagnostic() override;

    NativeKernel32Diagnostic(const NativeKernel32Diagnostic&) = delete;
    NativeKernel32Diagnostic& operator=(const NativeKernel32Diagnostic&) = delete;

    // NativePeSessionSetup callback; the context must be a NativeKernel32Diagnostic.
    static bool Setup(NativePeSession* session, void* context, std::string* error);

    // Records the guest stack range for this event and dispatches it through the
    // facade. A gate outside the facade is recorded as the first unhandled
    // import and reported as unhandled so the bridge returns zero.
    bool Dispatch(const NativeImportGateEvent& event, NativeImportGateResult* output);

    // What the last facade dispatch did through its services, and the
    // handler's failure text when it failed.
    const hle::ApiCallRecord& last_record() const { return last_record_; }
    const std::string& dispatch_error() const { return dispatch_error_; }

    // True when the event is a call through the named kernel32 facade export.
    bool IsExportCall(const NativeImportGateEvent& event, std::string_view export_name) const;

    // Reads a by-name GetProcAddress request from the call's second argument.
    // Returns false for an ordinal request or an unreadable name.
    bool ReadRequestedExportName(const NativeImportGateEvent& event, std::string* name);

    // Reads the ANSI string whose address is the call's argument at index.
    bool ReadArgumentString(const NativeImportGateEvent& event,
                            std::uint32_t index,
                            std::string* value);

    // The facade export this event calls, or null for a gate outside the facade.
    const hle::modules::GuestExportDescriptor* FindFacadeExport(
        const NativeImportGateEvent& event) const;

    // "module!name" for any gate the session or facade knows.
    std::string GateName(const NativeImportGateEvent& event) const;

    // Allocates a read/execute page of INT3 used to stop without writing guest code.
    bool PrepareStopStub(std::string* error);

    // Points the caller's return slot at the stop stub, so the thunk's
    // "pop ecx; ...; jmp ecx" lands on INT3 once cleanup is done. The original
    // return address is kept as stopped_return_address().
    bool RedirectReturnToStop(const NativeImportGateEvent& event);

    std::uint32_t stop_stub() const { return stop_stub_; }
    std::uint32_t stopped_return_address() const { return stopped_return_address_; }

    bool ImageContains(std::uint32_t address, std::size_t size) const;

    std::uint32_t image_base() const { return image_base_; }
    std::uint32_t image_size() const { return image_size_; }
    bool prepared() const { return registry_create_file_.value() != 0; }
    runtime::GuestAddress guest_get_version() const { return get_version_address_; }

    void CopyTo(OriginalRunResult* result) const;

    bool ReadGuestString(runtime::GuestAddress address,
                         std::string* value,
                         std::string* error) const override;
    runtime::GuestAddress FindGuestModule(std::string_view name) const override;
    runtime::GuestAddress FindGuestExport(runtime::GuestAddress module,
                                          std::string_view name) const override;
    runtime::GuestAddress FindGuestExport(runtime::GuestAddress module,
                                          std::uint16_t ordinal) const override;
    // Guest strings and bytes may lie in the image, the guest stack, a live
    // guest heap block, or committed VirtualAlloc pages.
    bool ReadGuestBytes(runtime::GuestAddress address,
                        std::span<std::uint8_t> bytes,
                        std::string* error) const override;
    bool WriteGuestBytes(runtime::GuestAddress address,
                         std::span<const std::uint8_t> bytes,
                         std::string* error) const override;
    bool IsGuestModule(runtime::GuestAddress handle) const override;
    std::string GuestModuleName(runtime::GuestAddress handle) const override;
    hle::GuestDeviceSet* Devices() const override;
    hle::GuestProcess* Process() const override;
    // The host's CLOCK_REALTIME with its local offset, and CLOCK_MONOTONIC.
    bool ReadClock(hle::GuestClockReading* reading) const override;
    void SetLastError(std::uint32_t value) const override;
    std::uint32_t LastError() const override;
    // Calls the guest function on the guest stack below the import being
    // dispatched; its own imports dispatch as nested calls.
    bool CallGuest(hle::GuestCall* call, std::uint32_t* result, std::string* error) const override;
    // The calling guest thread's TEB and ID.
    runtime::GuestAddress ThreadEnvironmentBlock() const override;
    std::uint32_t CurrentThreadId() const override;
    // Sleeps this guest thread on CLOCK_MONOTONIC, letting the others run.
    bool WaitMilliseconds(std::uint32_t milliseconds) const override;
    // A guest thread on a host thread of its own (StartNativeGuestThread),
    // whose TEB carries the process and thread IDs.
    bool StartGuestThread(std::uint32_t start,
                          std::uint32_t parameter,
                          std::uint32_t thread_id,
                          std::string* error) const override;
    // The host's presentation for this run, or null to show nothing.
    void SetPresentation(hle::HostPresentation* presentation) { presentation_ = presentation; }
    hle::HostPresentation* Presentation() const override { return presentation_; }
    void SetAudio(hle::HostAudio* audio) { audio_ = audio; }
    hle::HostAudio* Audio() const override { return audio_; }
    void SetProcessLauncher(hle::HostProcessLauncher* launcher) { process_launcher_ = launcher; }
    hle::HostProcessLauncher* ProcessLauncher() const override { return process_launcher_; }
    // How the guest process was launched (hle::GuestProcess::SetStartup).
    void SetStartup(hle::GuestStartup startup) { process_.SetStartup(std::move(startup)); }

    // The devices the guest may open during this run, sharing the guest
    // process's handle space.
    void ConfigureDevices(hle::GuestDeviceConfig config);
    // The files the guest may open, from the CHD and overlay; false when the
    // CHD cannot be opened.
    bool ConfigureFiles(hle::GuestFileConfig config, std::string* error);
    hle::GuestFiles* Files() const override;

    // Records the mapped image's pages with the Windows loader's protections
    // and its guest path.
    void DescribeImage(const exe::PeImageInfo& image_info, std::string module_path);
    const hle::GuestDeviceSet& devices() const { return devices_; }

    // True when the module at module_handle declares name absent, so a NULL
    // GetProcAddress for it is the answer Windows gives too.
    bool IsAbsentExport(std::uint32_t module_handle, std::string_view name) const;

private:
    const runtime::ImportGate* FindSessionGate(std::uint32_t address) const;
    void RecordUnresolvedLookup(std::string request) const;
    bool GuestRangeReadable(std::uint32_t address, std::size_t size) const;

    std::uint32_t image_base_ = 0;
    hle::HostPresentation* presentation_ = nullptr;
    hle::HostAudio* audio_ = nullptr;
    hle::HostProcessLauncher* process_launcher_ = nullptr;
    std::uint32_t image_size_ = 0;
    mutable std::uint32_t stack_base_ = 0;
    mutable std::uint32_t stack_limit_ = 0;
    NativeGuestModuleSet modules_;
    std::vector<NativeGuestImportRebinding> rebindings_;
    // Snapshot of the session's gates so an unhandled import can be named.
    std::vector<runtime::ImportGate> session_gates_;
    // Registry values captured while preparing the facade.
    runtime::GuestAddress registry_kernel32_base_;
    runtime::GuestAddress registry_get_version_;
    runtime::GuestAddress registry_create_file_;
    // Read back from the rebound static IAT slot while it is still mapped.
    runtime::GuestAddress static_create_file_slot_;
    // Values the guest actually received from the resolver at run time.
    mutable runtime::GuestAddress kernel32_base_;
    mutable runtime::GuestAddress get_version_address_;
    mutable runtime::GuestAddress create_file_address_;
    mutable std::string unhandled_dynamic_request_;
    std::string unhandled_import_;
    std::uint32_t stop_stub_ = 0;
    std::uint32_t stopped_return_address_ = 0;
    std::string dispatch_error_;
    hle::ApiCallRecord last_record_;
    mutable hle::GuestDeviceSet devices_;
    mutable hle::GuestFiles files_;
    // The guest heap's region below 4 GiB, mapped during Setup.
    NativeLowMemory heap_;
    // The VirtualAlloc arena below 4 GiB, mapped during Setup.
    NativeLowMemory private_arena_;
    mutable hle::GuestProcess process_;
    mutable std::uint32_t last_error_ = 0;
    // The main guest thread's TEB page, which the guest may read and write.
    std::uint32_t teb_ = 0;
    // Guest thread IDs by TEB, the main thread's included.
    mutable std::map<std::uint32_t, std::uint32_t> thread_ids_;
};

}  // namespace re2dj::platform::native

#endif  // RE2DJ_PLATFORM_NATIVE_NATIVE_KERNEL32_DIAGNOSTIC_H_
