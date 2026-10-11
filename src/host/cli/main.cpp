// Command-line host for re2DJ.
//
// The original HDD contents arrive as a directory path, never as a disk image
// and never from a fixed location inside the repository. Everything this entry
// point does is orchestration: it validates the directory, scans it, resolves a
// target profile, and enters an available platform execution backend.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/logger.h>

#include "re2dj/exe/pe_image.h"
#include "re2dj/hdd/chd_image_locator.h"
#include "re2dj/hdd/hdd_root.h"
#include "re2dj/hdd/hdd_scan.h"
#include "re2dj/storage/guest_path.h"
#include "re2dj/storage/fat32_chd.h"
#include "re2dj/graphics/color_depth.h"
#include "re2dj/graphics/post_shader_catalog.h"
#include "re2dj/graphics/present_sync.h"
#include "re2dj/hle/hex_bytes.h"
#include "re2dj/logging/logging.h"
#include "re2dj/target/target_profile.h"
#include "re2dj/version.h"

// Both hosts run the original PE32 in this process through the shared
// in-process runner (tasks 446 to 449).
#if defined(__linux__) || defined(_WIN32)
#define RE2DJ_IN_PROCESS_HOST 1
#include "launcher_session.h"
#include "re2dj/launcher/launcher_settings.h"
#include "re2dj/config/hardlock_secret_config.h"
#include "re2dj/hle/guest_devices.h"
#include "re2dj/hle/hardlock/device_material.h"
#include "re2dj/platform/native/child_run_options.h"
#include "re2dj/platform/native/original_runner.h"
#include "re2dj/platform/sdl/host_audio.h"
#include "re2dj/platform/sdl/host_presentation.h"
#endif
#if defined(__linux__)
#include "re2dj/platform/linux/host_process_launcher.h"
using HostProcessLauncher = re2dj::platform::linux::LinuxHostProcessLauncher;
using re2dj::platform::linux::WriteGuestExitCode;
#elif defined(_WIN32)
#include "re2dj/platform/windows/guest_process_entry.h"
#include "re2dj/platform/windows/host_process_launcher.h"
using HostProcessLauncher = re2dj::platform::windows::WindowsHostProcessLauncher;
using re2dj::platform::windows::WriteGuestExitCode;
#endif

namespace
{

// Exit codes. Kept distinct so scripts can tell a bad invocation from a bad
// dump from a feature that simply is not built yet.
constexpr int kExitOk = 0;
constexpr int kExitUsage = 1;
constexpr int kExitHddError = 2;
constexpr int kExitNotImplemented = 3;
constexpr int kExitLoggingError = 4;

// Where cfg/re2dj.ini lives, relative to the current directory as the
// launcher has it (#12, #14).
constexpr const char* kSettingsDirectory = "cfg";

struct LoggingLifetime
{
    ~LoggingLifetime()
    {
        re2dj::logging::Shutdown();
    }
};

std::string FormatLogMessage(const char* format, va_list arguments)
{
    va_list sizing_arguments;
    va_copy(sizing_arguments, arguments);
    const int length = std::vsnprintf(nullptr, 0, format, sizing_arguments);
    va_end(sizing_arguments);
    if (length <= 0)
    {
        return {};
    }
    std::string message(static_cast<std::size_t>(length) + 1, '\0');
    std::vsnprintf(message.data(), message.size(), format, arguments);
    message.resize(static_cast<std::size_t>(length));
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
    {
        message.pop_back();
    }
    return message;
}

void LogError(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const std::string message = FormatLogMessage(format, arguments);
    va_end(arguments);
    re2dj::logging::GetLogger()->error("{}", message);
}

// Run output goes through the logger, so it reaches the console and the log
// file alike; only --help, --version, and usage text stay on stdout.
void LogInfo(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const std::string message = FormatLogMessage(format, arguments);
    va_end(arguments);
    re2dj::logging::GetLogger()->info("{}", message);
}

// Lowercase hex of bytes, as one run.
std::string HexBytes(const std::uint8_t* bytes, std::size_t count)
{
    std::string text;
    text.reserve(count * 2);
    char pair[3] = {};
    for (std::size_t index = 0; index < count; ++index)
    {
        std::snprintf(pair, sizeof(pair), "%02x", static_cast<unsigned>(bytes[index]));
        text += pair;
    }
    return text;
}

void LogFatal(const char* classification, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const std::string message = FormatLogMessage(format, arguments);
    va_end(arguments);
    re2dj::logging::Fatal(classification, message);
}

struct Options
{
    std::filesystem::path hdd_directory;
    bool linux_in_process_first_import = false;
    bool linux_in_process_first_resolver = false;
    bool linux_in_process_getversion_call = false;
    bool linux_in_process_createfile_call = false;
    bool linux_in_process_continue = false;
    bool hold_window = false;
    // A CHD path that replaces the profile's executable, as a launcher's
    // child process names it, and the rest of what the launcher gave the
    // child (task 431).
    std::string guest_executable;
    std::string guest_command_line;
    std::string guest_current_directory;
    std::vector<std::uint8_t> guest_startup_reserved;
    int guest_exit_code_fd = -1;
    // 0 runs until the guest exits or its window is closed.
    std::uint32_t call_limit = 0;
    // Calls the API log records in full; 0 for all.
    std::uint32_t api_log_calls = 32768;
    std::filesystem::path io_config;
    std::string target_id;
    std::string resolve_path;
    float audio_gain_db = 0.0f;
    bool audio_gain_explicit = false;
    bool fullscreen_explicit = false;
    bool color_depth_explicit = false;
    bool image_dump = false;
    unsigned image_dump_delay_ms = 0;
    bool fullscreen = false;
    // --keep-aspect / --stretch (#14); without them cfg/re2dj.ini, then on.
    bool keep_aspect_explicit = false;
    bool keep_aspect = true;
    re2dj::graphics::ColorDepth color_depth = re2dj::graphics::ColorDepth::k16;
    // --post-shader (task 455); without it RE2DJ_POST_SHADER, then none.
    bool post_shader_explicit = false;
    std::string post_shader;
    // --vsync on|off|adaptive; on (the display's retrace) by default.
    re2dj::graphics::PresentSync present_sync = re2dj::graphics::PresentSync::kVerticalSync;
    bool list_targets = false;
    bool run = false;
    bool positional_target = false;
    bool target_option_explicit = false;
    bool show_help = false;
    bool show_version = false;
};

// Stages the profile's executable and its launcher's children out of the CHD
// into the temporary directory a run starts them from: the Windows product's
// real CreateProcessA finds a child, and its current directory, on disk.
bool PrepareChdStaging(const re2dj::storage::Fat32Volume& volume,
                       std::string_view profile_id,
                       const std::string& executable_relative_path,
                       const std::vector<std::string>& child_executable_paths,
                       std::filesystem::path* staging_root,
                       std::string* error)
{
    if (staging_root == nullptr || error == nullptr)
    {
        if (error != nullptr)
        {
            *error = "invalid CHD staging output";
        }
        return false;
    }
    std::error_code code;
    const auto base = std::filesystem::temp_directory_path(code);
    if (code)
    {
        *error = "cannot determine temporary directory for CHD staging";
        return false;
    }
    const std::filesystem::path root = base / "re2dj" / "chd" / profile_id;
    re2dj::storage::GuestPath parsed;
    if (!re2dj::storage::ParseGuestPath(executable_relative_path, &parsed) ||
        parsed.kind != re2dj::storage::GuestPathKind::kRelative ||
        !re2dj::storage::NormalizeGuestPath(&parsed) || parsed.components.empty())
    {
        *error = "invalid CHD executable path: " + executable_relative_path;
        return false;
    }
    std::filesystem::path executable_output = root;
    for (const std::string& component : parsed.components)
    {
        executable_output /= component;
    }
    std::string materialize_error;
    if (!volume.MaterializeFile(
            executable_relative_path, executable_output, &materialize_error))
    {
        *error = executable_relative_path + ": " + materialize_error;
        return false;
    }
    for (const std::string& child_relative : child_executable_paths)
    {
        re2dj::storage::GuestPath child_parsed;
        if (!re2dj::storage::ParseGuestPath(child_relative, &child_parsed) ||
            child_parsed.kind != re2dj::storage::GuestPathKind::kRelative ||
            !re2dj::storage::NormalizeGuestPath(&child_parsed) || child_parsed.components.empty())
        {
            *error = "invalid CHD child executable path: " + child_relative;
            return false;
        }
        std::filesystem::path child_output = root;
        for (const std::string& component : child_parsed.components)
        {
            child_output /= component;
        }
        if (!volume.MaterializeFile(child_relative, child_output, &materialize_error))
        {
            *error = child_relative + ": " + materialize_error;
            return false;
        }
    }
    *staging_root = root;
    return true;
}

int ResolveOneChdPath(const re2dj::storage::Fat32Volume& volume,
                      const std::string& text)
{
    re2dj::storage::GuestPath parsed;
    if (!re2dj::storage::ParseGuestPath(text, &parsed) ||
        !re2dj::storage::NormalizeGuestPath(&parsed))
    {
        LogError("cannot parse guest path '%s'", text.c_str());
        return kExitUsage;
    }
    if (parsed.drive_letter != 'D' || parsed.components.empty() ||
        !re2dj::storage::EqualsIgnoreAsciiCase(parsed.components.front(), "ez2dj"))
    {
        LogError("CHD guest path must be under D:\\ez2dj");
        return kExitUsage;
    }
    const std::string guest_path = re2dj::storage::GuestPathToString(parsed);
    parsed.components.erase(parsed.components.begin());
    const std::string relative = "EZ2DJ/" +
                                  re2dj::storage::GuestPathToRelativeString(parsed);
    re2dj::storage::Fat32Entry entry;
    std::string error;
    const bool found = volume.Find(relative, &entry, &error);
    LogInfo("guest path : %s", guest_path.c_str());
    LogInfo("relative   : %s", relative.c_str());
    LogInfo("chd path   : %s", found ? relative.c_str() : "<not found>");
    return found ? kExitOk : kExitHddError;
}

void PrintUsage()
{
    std::printf(
        "%s - run the original EZ2DJ executable on modern hosts\n"
        "\n"
        "Usage:\n"
        "  re2dj                 Open the launcher: pick a profile and options\n"
        "                        (kept in cfg/re2dj.ini). RE2DJ_LAUNCHER=0 prints\n"
        "                        this text instead.\n"
        "  re2dj <profile-id> [options]\n"
        "  re2dj --hdd <directory> [options]\n"
        "\n"
        "Options:\n"
        "  --hdd <directory>   Extracted original HDD contents. For a CHD profile,\n"
        "                      this may instead be a directory containing one .chd.\n"
        "  --target <id>       Target profile to select. Defaults to the first\n"
        "                      detected candidate.\n"
        "  --list-targets      List target profiles found in the directory.\n"
        "  --resolve <path>    Resolve one guest path (for example\n"
        "                      \"C:\\\\EZ2DJ\\\\DATA\\\\SONG.EZ\") and exit.\n"
        "  --run               Start the selected guest executable.\n"
        "  --hold-window       Keep the guest's window open after the run stops,\n"
        "                      until it is closed.\n"
        "  --guest-executable <path>\n"
        "                      Run this CHD executable instead of the profile's\n"
        "                      own (for example EZ2DJ/EZ2DJ6th.EXE). One below the\n"
        "                      profile executable's directory keeps that\n"
        "                      directory as its root, as a launcher's child does.\n"
        "  --guest-startup-reserved <hex>\n"
        "                      The STARTUPINFO reserved bytes the executable\n"
        "                      starts with, as a launcher gives its child.\n"
        "  --guest-current-directory <path>\n"
        "                      The guest path the executable starts in.\n"
        "  --call-limit <n>   Stop the run after n guest API calls, for\n"
        "                      diagnostics and regression runs. By default the run\n"
        "                      goes on until the guest exits or its window is closed.\n"
        "  --api-log-calls <n> Record the first n guest API calls in the API\n"
        "                      log (default 32768; 0 records every call).\n"
        "  --linux-in-process-first-import\n"
        "                      In-process diagnostic: complete only the first import in-process.\n"
        "  --linux-in-process-first-resolver\n"
        "                      In-process diagnostic: observe first dynamic GetProcAddress request.\n"
        "  --linux-in-process-getversion-call\n"
        "                      In-process diagnostic: observe the resolved GetVersion thunk call.\n"
        "  --linux-in-process-createfile-call\n"
        "                      In-process diagnostic: observe the resolved CreateFileA thunk call.\n"
        "  --linux-in-process-continue\n"
        "                      In-process diagnostic: run on the kernel32 facade until the\n"
        "                      first unhandled import, unresolved lookup, or fault.\n"
        "  --audio-gain-db <dB>\n"
        "                      Output gain (-24..+18, default 0).\n"
        "  --image-dump        Save the main image once it is mapped and again at the\n"
        "                      first import after the delay, under\n"
        "                      logs/image-dumps/<target> (diagnostic; tens of MB).\n"
        "  --image-dump-delay <milliseconds>\n"
        "                      Wait before the second image dump (default 5000).\n"
        "  --fullscreen        Start in monitor-sized borderless fullscreen.\n"
        "  --windowed          Override a profile's fullscreen default.\n"
        "                      Without either, cfg/re2dj.ini's [Video] fullscreen\n"
        "                      decides, then the profile. Alt+Enter, a double\n"
        "                      click or the OSD switches it; that choice is kept.\n"
        "  --keep-aspect       Keep the display's 4:3 shape with black bars (the\n"
        "                      default, or cfg/re2dj.ini's [Video] keep_aspect).\n"
        "  --stretch           Stretch the display over the whole window instead.\n"
        "                      The OSD switches it; that choice is kept.\n"
        "  --color-depth <16|32>\n"
        "                      How deep the host keeps colours. '16' shows the\n"
        "                      original's 16-bit picture (default); '32' keeps\n"
        "                      24-bit images and blends at 8 bits per channel.\n"
        "                      The game still sees a 16-bit display. The OSD's\n"
        "                      '32-bit color' switches it while running.\n"
        "  --post-shader <id>, --post-shader=<id>\n"
        "                      Screen post-processing shader: 'none' (default),\n"
        "                      the built-in 'crt' or 'scanline', or a .glsl file\n"
        "                      name in shaders/. Overrides RE2DJ_POST_SHADER. The\n"
        "                      OSD switches it while running.\n"
        "  --vsync <on|off|adaptive>\n"
        "                      When a frame is shown: at the display's retrace\n"
        "                      ('on', the default), at once ('off', for measuring\n"
        "                      the most frames a run can make; it may tear), or\n"
        "                      at the retrace unless the frame is late.\n"
        "  --io-config <path>  Keyboard and gamepad I/O mapping INI for the selected target.\n"
        "                      Overrides only the entries it lists; the built-in\n"
        "                      mapping covers the rest.\n"
        "  --version           Print the version and exit.\n"
        "  --help              Print this message and exit.\n"
        "  --                  End the options: what follows is read as the profile id.\n"
        "\n"
        "The HDD directory is read only. Supported execution paths route\n"
        "guest writes to a separate overlay directory.\n",
        re2dj::VersionBanner("re2DJ", re2dj::VersionString()).c_str());
}

#if defined(RE2DJ_IN_PROCESS_HOST)
void PrintFaultObservation(const re2dj::platform::native::OriginalRunResult& result)
{
    const auto& fault = result.fault_observation;
    if (!fault.observed)
    {
        return;
    }
    LogInfo("fault address   : 0x%08x, signal code=%u, cpu error=0x%08x",
                fault.fault_address.value(), fault.signal_code, fault.cpu_error_code);
    LogInfo("fault registers : eax=%08x ebx=%08x ecx=%08x edx=%08x",
                fault.eax, fault.ebx, fault.ecx, fault.edx);
    LogInfo("                  esi=%08x edi=%08x ebp=%08x eflags=%08x",
                fault.esi, fault.edi, fault.ebp, fault.eflags);
    if (fault.instruction_window_observed)
    {
        const std::uint32_t instruction_offset =
            result.instruction_pointer.value() - fault.instruction_window_address.value();
        LogInfo("fault code      : 0x%08x (EIP +%u)",
                    fault.instruction_window_address.value(),
                    instruction_offset);
        for (std::size_t index = 0; index < fault.instruction_window.size(); index += 16)
        {
            const std::size_t count = std::min<std::size_t>(16, fault.instruction_window.size() - index);
            LogInfo("                  %s",
                    HexBytes(fault.instruction_window.data() + index, count).c_str());
        }
    }
    if (fault.stack_words_observed)
    {
        std::string words;
        char word[10] = {};
        for (std::uint32_t index = 0; index < fault.stack_word_count; ++index)
        {
            std::snprintf(word, sizeof(word), " %08x", fault.stack_words[index]);
            words += word;
        }
        LogInfo("fault stack     :%s", words.c_str());
    }
    if (fault.fs_base.value() != 0)
    {
        if (fault.seh_frame_observed)
        {
            LogInfo("fault seh       : teb=0x%08x frame=0x%08x handler=0x%08x next=0x%08x",
                        fault.fs_base.value(),
                        fault.seh_frame_address.value(),
                        fault.seh_handler.value(),
                        fault.seh_next);
            if (fault.seh_handler_window_observed)
            {
                LogInfo("seh handler code: 0x%08x", fault.seh_handler.value());
                for (std::size_t index = 0; index < fault.seh_handler_window.size(); index += 16)
                {
                    const std::size_t count =
                        std::min<std::size_t>(16, fault.seh_handler_window.size() - index);
                    LogInfo("                  %s",
                            HexBytes(fault.seh_handler_window.data() + index, count).c_str());
                }
            }
        }
        else
        {
            LogInfo("fault seh       : teb=0x%08x frame=0x%08x (no frame registered)",
                        fault.fs_base.value(),
                        fault.seh_frame_address.value());
        }
    }
}

void PrintInstructionTrace(const re2dj::platform::native::OriginalRunResult& result)
{
    const auto& trace = result.instruction_trace;
    if (!trace.armed)
    {
        return;
    }
    LogInfo("instruction trace: breakpoint=0x%08x started=%u frames=%u limit=%u",
                trace.breakpoint.value(),
                trace.started ? 1U : 0U,
                trace.frame_count,
                trace.limit_reached ? 1U : 0U);
    for (std::uint32_t index = 0; index < trace.frame_count; ++index)
    {
        const auto& frame = trace.frames[index];
        LogInfo("  #%03u eip=%08x esp=%08x eax=%08x ebx=%08x ecx=%08x edx=%08x",
                    index,
                    frame.instruction_pointer.value(),
                    frame.stack_pointer.value(),
                    frame.eax,
                    frame.ebx,
                    frame.ecx,
                    frame.edx);
        LogInfo("       esi=%08x edi=%08x ebp=%08x eflags=%08x",
                    frame.esi,
                    frame.edi,
                    frame.ebp,
                    frame.eflags);
    }
}

void PrintCreateFileObservation(const re2dj::platform::native::OriginalRunResult& result)
{
    const auto& observation = result.create_file_observation;
    if (!observation.observed)
    {
        return;
    }
    LogInfo("CreateFileA name : %s (0x%08x)",
                observation.file_name_observed ? observation.file_name.c_str() : "<unavailable>",
                observation.file_name_address.value());
    LogInfo("access / share   : 0x%08x / 0x%08x",
                observation.desired_access,
                observation.share_mode);
    LogInfo("security / disp  : 0x%08x / 0x%08x",
                observation.security_attributes.value(),
                observation.creation_disposition);
    LogInfo("flags / template : 0x%08x / 0x%08x",
                observation.flags_and_attributes,
                observation.template_file.value());
}

const char* DescribeIdentity(re2dj::runtime::GuestAddress registry,
                             re2dj::runtime::GuestAddress guest)
{
    return guest.value() == 0 ? " (never requested)"
           : guest == registry ? " (match)"
                               : " (MISMATCH)";
}

void PrintResolverIdentity(const re2dj::platform::native::OriginalRunResult& result)
{
    const auto& identity = result.resolver_identity;
    if (identity.prepared)
    {
        LogInfo("kernel32 facade  : registry 0x%08x, guest saw 0x%08x%s",
                    identity.registry_kernel32_base.value(),
                    identity.kernel32_base.value(),
                    DescribeIdentity(identity.registry_kernel32_base, identity.kernel32_base));
        LogInfo("CreateFileA addr : registry 0x%08x, static IAT 0x%08x, guest saw 0x%08x%s%s",
                    identity.registry_create_file.value(),
                    identity.static_create_file_slot.value(),
                    identity.create_file_address.value(),
                    DescribeIdentity(identity.registry_create_file, identity.create_file_address),
                    identity.static_imports_rebound ? "" : " (no static rebind)");
        LogInfo("GetVersion addr  : registry 0x%08x, guest saw 0x%08x%s",
                    identity.registry_get_version.value(),
                    identity.get_version_address.value(),
                    DescribeIdentity(identity.registry_get_version, identity.get_version_address));
    }
    if (result.unhandled_import_observed)
    {
        LogInfo("unhandled import : %s", result.unhandled_import.c_str());
    }
    if (result.unhandled_dynamic_request_observed)
    {
        LogInfo("unresolved lookup: %s",
                    result.unhandled_dynamic_request.c_str());
    }
}

// A guest ANSI string in quotes, preceded by a space. Guest text may be CP949
// rather than UTF-8, so bytes outside printable ASCII appear as \xNN, keeping
// the original bytes recoverable instead of guessing an encoding.
std::string QuoteGuestText(const std::string& text)
{
    std::string quoted = " \"";
    char escape[5] = {};
    for (const char character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte > 0x7E)
        {
            std::snprintf(escape, sizeof(escape), "\\x%02x", byte);
            quoted += escape;
        }
        else
        {
            quoted.push_back(character);
        }
    }
    quoted.push_back('"');
    return quoted;
}

void PrintApiCalls(const re2dj::platform::native::OriginalRunResult& result)
{
    if (result.api_call_count > result.api_calls.size())
    {
        LogInfo("api calls       : %u (first and last %zu logged)",
                result.api_call_count,
                result.api_calls.size());
    }
    else
    {
        LogInfo("api calls       : %u", result.api_call_count);
    }
    std::uint32_t previous_sequence = 0;
    char part[48] = {};
    for (const auto& call : result.api_calls)
    {
        if (call.sequence > previous_sequence + 1)
        {
            LogInfo("  ... %u calls omitted", call.sequence - previous_sequence - 1);
        }
        previous_sequence = call.sequence;
        std::string arguments;
        for (std::uint32_t index = 0; index < call.argument_count; ++index)
        {
            std::snprintf(part, sizeof(part), index == 0 ? " args=%08x" : ",%08x", call.arguments[index]);
            arguments += part;
        }
        if (call.text_observed)
        {
            arguments += QuoteGuestText(call.text);
        }
        if (call.second_text_observed)
        {
            arguments += QuoteGuestText(call.second_text);
        }
        if (call.handled)
        {
            std::snprintf(part, sizeof(part), " -> eax=%08x", call.eax);
        }
        else
        {
            std::snprintf(part, sizeof(part), " -> UNHANDLED");
        }
        char thread[24] = {};
        if (call.thread_id != 0)
        {
            std::snprintf(thread, sizeof(thread), "[thread %04x] ", call.thread_id);
        }
        LogInfo("  #%04u %s%-32s ret=%08x%s%s",
                call.sequence,
                thread,
                call.name.c_str(),
                call.return_address,
                arguments.c_str(),
                part);
    }
}

bool PrintContinuationBoundary(const re2dj::platform::native::OriginalRunResult& result)
{
    using re2dj::platform::native::OriginalRunBoundary;
    switch (result.boundary)
    {
    case OriginalRunBoundary::kContinuationUnhandledImport:
        LogInfo("continuation    : stopped at unhandled import %s, return 0x%08x",
                    result.continuation_stop_detail.c_str(),
                    result.import_return_address);
        break;
    case OriginalRunBoundary::kContinuationUnresolvedLookup:
        LogInfo("continuation    : stopped at unresolved lookup %s, return 0x%08x",
                    result.continuation_stop_detail.c_str(),
                    result.import_return_address);
        break;
    case OriginalRunBoundary::kContinuationCallLimit:
        LogInfo("continuation    : stopped at the call limit after %s, return 0x%08x",
                    result.continuation_stop_detail.c_str(),
                    result.import_return_address);
        break;
    case OriginalRunBoundary::kContinuationHostClosed:
        LogInfo("continuation    : host window closed, after %s",
                    result.continuation_stop_detail.c_str());
        break;
    case OriginalRunBoundary::kContinuationFault:
        LogInfo("continuation    : guest fault signal %u, EIP 0x%08x",
                    result.status_code,
                    result.instruction_pointer.value());
        PrintFaultObservation(result);
        break;
    case OriginalRunBoundary::kProcessExit:
        LogInfo("continuation    : process exit, %s",
                    result.continuation_stop_detail.c_str());
        break;
    default:
        return false;
    }
    PrintResolverIdentity(result);
    return true;
}

// Whether an in-process --run takes the in-process continuation: named explicitly,
// or by default when no other in-process diagnostic was requested.
bool IsContinuationRun(const Options& options)
{
    return options.linux_in_process_continue ||
           (!options.linux_in_process_first_import && !options.linux_in_process_first_resolver &&
            !options.linux_in_process_getversion_call && !options.linux_in_process_createfile_call);
}

// The devices an in-process run provides: the profile's device path and,
// when the profile allows it, the user's Hardlock material from cfg. Nothing
// derived from that material is printed or logged.
bool BuildGuestDevices(const re2dj::target::TargetProfile& profile,
                            re2dj::hle::GuestDeviceConfig* config,
                            std::string* error)
{
    namespace hardlock = re2dj::hle::hardlock;
    *config = {};
    const re2dj::target::TargetLptdiPolicy& lptdi = profile.run_defaults.lptdi;
    if (!lptdi.device_mock_enabled)
    {
        return true;
    }
    config->device_path_prefix = lptdi.device_mock_path_prefix;
    if (!lptdi.hardlock_cfg_material_default)
    {
        return true;
    }
    hardlock::HardlockMaterialSources sources;
    sources.profile_id = profile.id;
    sources.use_profile_cfg = true;
    sources.config_path = re2dj::config::DefaultHardlockSecretConfigPath();
    sources.default_map_path = re2dj::config::DefaultHardlockTransformMapPath(profile.id);
    hardlock::HardlockDeviceMaterial material;
    if (!hardlock::ResolveHardlockDeviceMaterial(sources, &material, error))
    {
        return false;
    }
    if (material.device_enabled)
    {
        config->hardlock = hardlock::MakeHardlockDeviceOptions(material);
    }
    return true;
}

void PrintDeviceActivity(const re2dj::platform::native::OriginalRunResult& result)
{
    const re2dj::hle::hardlock::HardlockDeviceActivity& activity = result.device_activity;
    LogInfo("hardlock material: %s", result.hardlock_material_applied ? "applied" : "none");
    LogInfo("hardlock requests: total=%u initialize=%u handshake=%u descriptor=%u "
                "transform=%u other=%u rejected=%u last=%s/%s",
                activity.total,
                activity.initialize,
                activity.handshake,
                activity.descriptor,
                activity.transform,
                activity.other,
                activity.rejected,
                activity.last_kind,
                activity.last_outcome);
    if (result.legacy_io_reads + result.legacy_io_writes + result.legacy_io_unanswered != 0)
    {
        LogInfo("legacy io       : reads=%u writes=%u unanswered=%u first=%s 0x%04x",
                result.legacy_io_reads,
                result.legacy_io_writes,
                result.legacy_io_unanswered,
                result.legacy_io_first_read ? "in" : "out",
                static_cast<unsigned>(result.legacy_io_first_port));
    }
}

// The host window an in-process run shows the guest's window in, made when the
// guest takes the display and kept for the process's life.
std::unique_ptr<re2dj::platform::sdl::SdlHostPresentation> g_presentation;
#if defined(RE2DJ_SDL_HOST_AUDIO)
// Where an in-process run's sound plays, kept for the process's life like the
// window.
std::unique_ptr<re2dj::platform::sdl::SdlHostAudio> g_audio;
// Starts the guest's child processes, as other runs of this program with
// this run's own options.
std::unique_ptr<HostProcessLauncher> g_process_launcher;
std::vector<std::string> g_child_base_arguments;
#endif

// Ends an in-process run's host services whichever way main returns: keeps the
// window on screen after everything else is reported when --hold-window asked
// for it, then releases the services before main returns. Left to static
// destruction they went after state of other translation units they still
// use, and a run whose guest left a thread behind (Remember 1st's sound
// thread) ended with SIGSEGV after ExitProcess (task 440).
struct InProcessHostLifetime
{
    bool hold_window = false;
    ~InProcessHostLifetime()
    {
        if (hold_window && g_presentation != nullptr && g_presentation->opened())
        {
            LogInfo("host window     : kept open until it is closed (--hold-window)");
            g_presentation->HoldUntilClosed();
        }
#if defined(RE2DJ_SDL_HOST_AUDIO)
        g_process_launcher.reset();
        g_audio.reset();
#endif
        g_presentation.reset();
    }
};

// The I/O board bindings for an in-process run: the built-in defaults with the
// --io-config INI's entries over them (task 444). The whole file is read and
// both games' sections resolved, so an error in either shows before the run.
bool LoadIoBindings(const std::filesystem::path& io_config,
                         re2dj::input::IoBindings* bindings,
                         std::string* error)
{
    std::ifstream stream(io_config, std::ios::binary);
    if (!stream)
    {
        *error = "cannot read --io-config " + io_config.string();
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::string detail;
    if (!re2dj::input::LoadEz2DjIoBindings(text, &bindings->ez2dj, &detail) ||
        !re2dj::input::LoadEz2DancerIoBindings(text, &bindings->ez2dancer, &detail))
    {
        *error = "--io-config " + io_config.string() + ": " + detail;
        return false;
    }
    return true;
}

// Runs the guest in this process on the guest facades, on Linux (both widths)
// and on Windows x86.
bool RunInProcessOriginal(const Options& options,
                      const re2dj::target::TargetProfile& profile,
                      const std::filesystem::path& executable_path,
                      const re2dj::exe::PeImageInfo& image_info,
                      const std::filesystem::path& chd_image,
                      const std::filesystem::path& hdd_directory,
                      re2dj::platform::native::OriginalRunResult* result,
                      std::string* error)
{
    namespace native_platform = re2dj::platform::native;
    namespace sdl_platform = re2dj::platform::sdl;
    if (IsContinuationRun(options))
    {
        native_platform::OriginalRunEnvironment environment;
        if (!BuildGuestDevices(profile, &environment.devices, error))
        {
            return false;
        }
        environment.module_path = re2dj::target::GuestExecutablePath(profile);
        // What a launcher gave this run as its child, and where the guest's
        // own CreateProcessA starts children: another run of this program.
        environment.startup.command_line = options.guest_command_line;
        environment.startup.reserved = options.guest_startup_reserved;
        environment.current_directory = options.guest_current_directory;
        if (g_process_launcher == nullptr)
        {
            g_process_launcher = std::make_unique<HostProcessLauncher>(g_child_base_arguments);
        }
        environment.process_launcher = g_process_launcher.get();
        // The profile's I/O board contract, as the Windows runtime applies it
        // by default.
        const re2dj::target::TargetLptdiPolicy& lptdi = profile.run_defaults.lptdi;
        environment.legacy_io.enabled = lptdi.legacy_io_ports && lptdi.legacy_io_ports_default;
        environment.legacy_io.word_width = lptdi.legacy_io_width == re2dj::target::LegacyIoWidth::kWord;
        environment.legacy_io.in_rva = lptdi.legacy_io_in_rva;
        environment.legacy_io.out_rva = lptdi.legacy_io_out_rva;
        // The keys and gamepad controls the board follows: the built-in
        // defaults unless an INI overrides some of them.
        if (options.io_config.empty())
        {
            LogInfo("io config       : built-in defaults");
        }
        else if (!lptdi.legacy_io_ports)
        {
            LogInfo("io config       : ignored for profile '%s' because legacy I/O is disabled",
                    profile.id.c_str());
        }
        else if (!LoadIoBindings(options.io_config, &environment.io_bindings, error))
        {
            return false;
        }
        else
        {
            LogInfo("io config       : %s", options.io_config.string().c_str());
        }
        if (g_presentation == nullptr)
        {
            g_presentation = std::make_unique<sdl_platform::SdlHostPresentation>();
        }
        // The window starts fullscreen and keeps the display's shape as the
        // command line asks, else as cfg/re2dj.ini keeps from the last change
        // (#14), else as the profile and the default say. A change the user
        // makes in the window is kept there for the next run.
        {
            const re2dj::launcher::LauncherSettingsLoad stored =
                re2dj::launcher::LoadLauncherSettings(kSettingsDirectory);
            for (const std::string& warning : stored.warnings)
            {
                re2dj::logging::GetLogger()->warn("settings: {}", warning);
            }
            const re2dj::launcher::DisplayPreferences display = re2dj::launcher::ResolveDisplayPreferences(
                options.fullscreen_explicit ? std::optional<bool>(options.fullscreen) : std::nullopt,
                options.keep_aspect_explicit ? std::optional<bool>(options.keep_aspect) : std::nullopt,
                stored.settings,
                profile.run_defaults.fullscreen);
            g_presentation->SetStartFullscreen(display.fullscreen);
            g_presentation->SetStartKeepAspect(display.keep_aspect);
            LogInfo("display         : %s, %s", display.fullscreen ? "fullscreen" : "windowed",
                    display.keep_aspect ? "keep aspect" : "stretched");
            g_presentation->SetDisplayPreferencesObserver([](bool fullscreen, bool keep_aspect) {
                re2dj::launcher::DisplayPreferences changed;
                changed.fullscreen = fullscreen;
                changed.keep_aspect = keep_aspect;
                std::string error;
                if (re2dj::launcher::SaveDisplayPreferences(kSettingsDirectory, changed, &error))
                {
                    LogInfo("display         : %s, %s (kept in %s)", fullscreen ? "fullscreen" : "windowed",
                            keep_aspect ? "keep aspect" : "stretched",
                            re2dj::launcher::LauncherSettingsPath(kSettingsDirectory).string().c_str());
                }
                else
                {
                    re2dj::logging::GetLogger()->warn("display: {}", error);
                }
            });
        }
        // The OSD shows what the Windows host's shows.
        g_presentation->SetOsdInfoLines(
            {re2dj::VersionBanner("re2DJ", re2dj::VersionString()) + " - Build " + __DATE__,
             "Target Profile : " + profile.id,
             "Executable : " + std::filesystem::path(profile.executable_relative_path).filename().string()});
        // The screen shader: --post-shader, then RE2DJ_POST_SHADER, then none.
        // A launcher's child inherits both the command line and the
        // environment, so it opens with the same one.
        const std::string post_shader = re2dj::graphics::ChoosePostShader(
            options.post_shader_explicit ? &options.post_shader : nullptr,
            std::getenv(re2dj::graphics::kPostShaderVariable));
        g_presentation->SetPostShader(post_shader);
        g_presentation->SetPresentSync(options.present_sync);
        if (options.present_sync != re2dj::graphics::PresentSync::kVerticalSync)
        {
            LogInfo("present sync    : %s", re2dj::graphics::PresentSyncName(options.present_sync));
        }
        if (post_shader != re2dj::graphics::kPostShaderNoneId)
        {
            LogInfo("post shader     : %s%s", post_shader.c_str(),
                    options.post_shader_explicit ? "" : " (RE2DJ_POST_SHADER)");
        }
        environment.presentation = g_presentation.get();
        // Autoplay only for the exact build it was confirmed in; a launcher's
        // child picks by its own executable (task 436).
        environment.autoplay_flag_rva = re2dj::target::ArmedAutoplayFlagRva(profile.game_controls, image_info.timestamp);
        // --image-dump on the in-process runner (task 449): the dumps go
        // beside the logs, named by this run's time and executable, so a
        // launcher's child writes its own.
        if (options.image_dump)
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
            const auto milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
            char stamp[32] = {};
            std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&seconds));
            char stem[96] = {};
            std::snprintf(stem, sizeof(stem), "%s-%03d-%s", stamp, static_cast<int>(milliseconds),
                          executable_path.stem().string().c_str());
            environment.image_dump.enabled = true;
            environment.image_dump.delay_ms = options.image_dump_delay_ms != 0 ? options.image_dump_delay_ms : 5000;
            environment.image_dump.directory = std::filesystem::current_path() / "logs" / "image-dumps" / profile.id;
            environment.image_dump.stem = stem;
            environment.image_dump.target_id = profile.id;
            environment.image_dump.re2dj_version = re2dj::VersionString();
        }
        LogInfo("game controls   : autoplay %s (build 0x%08x)",
                environment.autoplay_flag_rva != 0 ? "armed"
                : profile.game_controls.empty()    ? "not declared"
                                                   : "declared for another build",
                static_cast<unsigned>(image_info.timestamp));
        // The colour depth: --color-depth, or the profile's default. The OSD
        // may change it while the guest runs.
        const re2dj::graphics::ColorDepth color_depth =
            options.color_depth_explicit ? options.color_depth : profile.run_defaults.color_depth;
        re2dj::graphics::SelectColorDepth(color_depth);
        LogInfo("colour depth    : %s-bit", re2dj::graphics::ColorDepthName(color_depth));
#if defined(RE2DJ_SDL_HOST_AUDIO)
        // The master gain the Windows host applies: --audio-gain-db, or the
        // profile's default.
        if (g_audio == nullptr)
        {
            const float gain_db = options.audio_gain_explicit ? options.audio_gain_db
                                                              : profile.run_defaults.audio_gain_db.value_or(0.0f);
            auto audio = std::make_unique<sdl_platform::SdlHostAudio>();
            std::string audio_error;
            if (audio->Initialize(std::pow(10.0f, gain_db / 20.0f), &audio_error))
            {
                LogInfo("host audio      : SDL3_mixer, master gain %.1f dB%s%s", static_cast<double>(gain_db),
                        audio->headless_reason().empty() ? "" : ", no playback device: ",
                        audio->headless_reason().c_str());
                g_audio = std::move(audio);
            }
            else
            {
                LogInfo("host audio      : none, sound plays silently (%s)", audio_error.c_str());
            }
        }
        environment.audio = g_audio.get();
#endif
        environment.call_limit = options.call_limit;
        environment.api_log_calls = options.api_log_calls;
        // Guest files come from the CHD or the directory dump, with writes in
        // overlays/<profile> as on the Windows path.
        if (!chd_image.empty() || !hdd_directory.empty())
        {
            environment.files.chd_image = chd_image;
            environment.files.hdd_directory = hdd_directory;
            // The product's directory: a launcher's child keeps the
            // launcher's (task 434).
            environment.files.chd_root =
                profile.working_directory_relative_path.empty()
                    ? std::filesystem::path(profile.executable_relative_path).parent_path().generic_string()
                    : profile.working_directory_relative_path;
            environment.files.guest_root = re2dj::target::GuestRootPath(profile);
            environment.files.overlay_root = std::filesystem::current_path() / "overlays" / profile.id;
        }
        const bool ran = native_platform::RunOriginalInProcessContinuation(
            executable_path, image_info, environment, result, error);
        // A child run reports its guest's exit code to the launcher that
        // started it: the ExitProcess code, 0 when the host window was closed,
        // and 0xFFFFFFFF for a run that stopped.
        if (options.guest_exit_code_fd >= 0)
        {
            std::uint32_t code = 0xFFFFFFFFU;
            if (ran && result->boundary == native_platform::OriginalRunBoundary::kProcessExit)
            {
                code = static_cast<std::uint32_t>(result->status_code);
            }
            else if (ran && result->boundary == native_platform::OriginalRunBoundary::kContinuationHostClosed)
            {
                code = 0;
            }
            WriteGuestExitCode(options.guest_exit_code_fd, code);
        }
        return ran;
    }
    if (options.linux_in_process_createfile_call)
    {
        return native_platform::RunOriginalInProcessCreateFileCall(
            executable_path, image_info, result, error);
    }
    if (options.linux_in_process_getversion_call)
    {
        return native_platform::RunOriginalInProcessGetVersionCall(
            executable_path, image_info, result, error);
    }
    if (options.linux_in_process_first_resolver)
    {
        return native_platform::RunOriginalInProcessFirstResolver(
            executable_path, image_info, result, error);
    }
    return native_platform::RunOriginalInProcessFirstImport(executable_path, image_info, result, error);
}
#endif

constexpr std::string_view kEndOfOptions = "--";
constexpr std::string_view kPostShaderEquals = "--post-shader=";

bool TakeValue(int argc, char** argv, int* index, std::string_view name, std::string* out)
{
    if (*index + 1 >= argc)
    {
        LogError("%s requires a value", std::string(name).c_str());
        return false;
    }
    ++(*index);
    *out = argv[*index];
    return true;
}

// A profile id given without --target, which also selects --run.
bool TakePositionalTarget(std::string_view argument, Options* options)
{
    if (options->positional_target)
    {
        LogError("only one profile id may be specified");
        return false;
    }
    options->positional_target = true;
    if (!options->target_option_explicit)
    {
        options->target_id = std::string(argument);
    }
    options->run = true;
    return true;
}

bool ParseOptions(int argc, char** argv, Options* options)
{
    // After `--` nothing is read as an option (rePIU task 771's rule).
    bool options_ended = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (options_ended)
        {
            if (!argument.empty() && !TakePositionalTarget(argument, options))
            {
                return false;
            }
            continue;
        }
        if (argument == kEndOfOptions)
        {
            options_ended = true;
        }
        else if (argument == "--help" || argument == "-h")
        {
            options->show_help = true;
        }
        else if (argument == "--version")
        {
            options->show_version = true;
        }
        else if (argument == "--list-targets")
        {
            options->list_targets = true;
        }
        else if (argument == "--run")
        {
            options->run = true;
        }
        else if (argument == "--linux-in-process-first-import")
        {
            options->linux_in_process_first_import = true;
        }
        else if (argument == "--linux-in-process-first-resolver")
        {
            options->linux_in_process_first_resolver = true;
        }
        else if (argument == "--linux-in-process-getversion-call")
        {
            options->linux_in_process_getversion_call = true;
        }
        else if (argument == "--linux-in-process-createfile-call")
        {
            options->linux_in_process_createfile_call = true;
        }
        else if (argument == "--linux-in-process-continue")
        {
            options->linux_in_process_continue = true;
        }
        else if (argument == "--hold-window")
        {
            options->hold_window = true;
        }
        else if (argument == "--guest-executable")
        {
            if (!TakeValue(argc, argv, &index, argument, &options->guest_executable))
            {
                return false;
            }
        }
        else if (argument == "--guest-command-line")
        {
            if (!TakeValue(argc, argv, &index, argument, &options->guest_command_line))
            {
                return false;
            }
        }
        else if (argument == "--guest-current-directory")
        {
            if (!TakeValue(argc, argv, &index, argument, &options->guest_current_directory))
            {
                return false;
            }
        }
        else if (argument == "--guest-startup-reserved" || argument == "--guest-exit-code-fd")
        {
            std::string value;
            if (!TakeValue(argc, argv, &index, argument, &value))
            {
                return false;
            }
            if (argument == "--guest-startup-reserved")
            {
                if (!re2dj::hle::DecodeHexBytes(value, &options->guest_startup_reserved))
                {
                    LogError("--guest-startup-reserved takes whole bytes in hex");
                    return false;
                }
            }
#if defined(RE2DJ_IN_PROCESS_HOST)
            else
            {
                options->guest_exit_code_fd = std::atoi(value.c_str());
            }
#endif
        }
        else if (argument == "--api-log-calls")
        {
            std::string value;
            if (!TakeValue(argc, argv, &index, argument, &value))
            {
                return false;
            }
            try
            {
                std::size_t parsed = 0;
                const unsigned long parsed_value = std::stoul(value, &parsed);
                if (parsed != value.size() || parsed_value > 0xFFFFFFFFUL)
                {
                    throw std::out_of_range("api log calls");
                }
                options->api_log_calls = static_cast<std::uint32_t>(parsed_value);
            }
            catch (const std::exception&)
            {
                LogError("--api-log-calls must be a number of calls");
                return false;
            }
        }
        else if (argument == "--call-limit")
        {
            std::string value;
            if (!TakeValue(argc, argv, &index, argument, &value))
            {
                return false;
            }
            try
            {
                std::size_t parsed = 0;
                const unsigned long parsed_value = std::stoul(value, &parsed);
                if (parsed != value.size() || parsed_value == 0 || parsed_value > 0xFFFFFFFFUL)
                {
                    throw std::out_of_range("call limit");
                }
                options->call_limit = static_cast<std::uint32_t>(parsed_value);
            }
            catch (const std::exception&)
            {
                LogError("--call-limit must be a positive number of calls");
                return false;
            }
        }
        else if (argument == "--hdd")
        {
            std::string value;
            if (!TakeValue(argc, argv, &index, argument, &value))
            {
                return false;
            }
            options->hdd_directory = std::filesystem::path(value);
        }
        else if (argument == "--audio-gain-db")
        {
            std::string value;
            if (!TakeValue(argc, argv, &index, argument, &value))
            {
                return false;
            }
            try
            {
                std::size_t parsed = 0;
                options->audio_gain_db = std::stof(value, &parsed);
                if (parsed != value.size() || !std::isfinite(options->audio_gain_db) ||
                    options->audio_gain_db < -24.0f || options->audio_gain_db > 18.0f)
                {
                    throw std::out_of_range("audio gain");
                }
            }
            catch (const std::exception&)
            {
                LogError("--audio-gain-db must be between -24 and +18 dB");
                return false;
            }
            options->audio_gain_explicit = true;
        }
        else if (argument == "--image-dump")
        {
            options->image_dump = true;
        }
        else if (argument == "--image-dump-delay" && index + 1 < argc)
        {
            options->image_dump_delay_ms =
                static_cast<unsigned>(std::strtoul(argv[++index], nullptr, 10));
            options->image_dump = true;
        }
        else if (argument == "--fullscreen")
        {
            options->fullscreen = true;
            options->fullscreen_explicit = true;
        }
        else if (argument == "--windowed")
        {
            options->fullscreen = false;
            options->fullscreen_explicit = true;
        }
        else if (argument == "--keep-aspect" || argument == "--stretch")
        {
            options->keep_aspect = argument == "--keep-aspect";
            options->keep_aspect_explicit = true;
        }
        else if (argument == "--color-depth")
        {
            std::string value;
            if (!TakeValue(argc, argv, &index, argument, &value))
            {
                return false;
            }
            if (!re2dj::graphics::ParseColorDepthName(value, &options->color_depth))
            {
                LogError("--color-depth must be 16 or 32");
                return false;
            }
            options->color_depth_explicit = true;
        }
        else if (argument == "--vsync")
        {
            std::string value;
            if (!TakeValue(argc, argv, &index, argument, &value))
            {
                return false;
            }
            if (value == "on")
            {
                options->present_sync = re2dj::graphics::PresentSync::kVerticalSync;
            }
            else if (value == "off")
            {
                options->present_sync = re2dj::graphics::PresentSync::kImmediate;
            }
            else if (value == "adaptive")
            {
                options->present_sync = re2dj::graphics::PresentSync::kAdaptive;
            }
            else
            {
                LogError("--vsync must be on, off or adaptive");
                return false;
            }
        }
        else if (argument == "--post-shader" || argument.rfind(kPostShaderEquals, 0) == 0)
        {
            // `--post-shader <id>` or `--post-shader=<id>`; a repeated option
            // takes the last value.
            if (argument == "--post-shader")
            {
                if (!TakeValue(argc, argv, &index, argument, &options->post_shader))
                {
                    return false;
                }
            }
            else
            {
                options->post_shader = std::string(argument.substr(kPostShaderEquals.size()));
            }
            if (options->post_shader.empty())
            {
                LogError("--post-shader needs a shader id, or none");
                return false;
            }
            options->post_shader_explicit = true;
        }
        else if (argument == "--io-config")
        {
            std::string value;
            if (!TakeValue(argc, argv, &index, argument, &value))
            {
                return false;
            }
            std::error_code ec;
            const auto absolute_path = std::filesystem::absolute(value, ec);
            options->io_config = ec ? std::filesystem::path(value) : absolute_path;
        }
        else if (argument == "--target")
        {
            if (!TakeValue(argc, argv, &index, argument, &options->target_id))
            {
                return false;
            }
            options->target_option_explicit = true;
        }
        else if (argument == "--resolve")
        {
            if (!TakeValue(argc, argv, &index, argument, &options->resolve_path))
            {
                return false;
            }
        }
        else if (!argument.empty() && argument.front() != '-')
        {
            if (!TakePositionalTarget(argument, options))
            {
                return false;
            }
        }
        else
        {
            LogError("unknown argument '%s'", argv[index]);
            return false;
        }
    }
    return true;
}

void PrintProfile(const re2dj::target::TargetProfile& profile, bool selected)
{
    LogInfo("  %c %-22s %-24s %s%s",
                selected ? '*' : ' ',
                profile.id.c_str(),
                profile.executable_relative_path.c_str(),
                profile.detected ? "detected" : "built-in",
                profile.bring_up_target ? ", bring-up only" : "");
}

int ResolveOnePath(const re2dj::hdd::HddRoot& root, const std::string& text)
{
    re2dj::storage::GuestPath parsed;
    if (!re2dj::storage::ParseGuestPath(text, &parsed))
    {
        LogError("cannot parse guest path '%s'", text.c_str());
        return kExitUsage;
    }
    if (!re2dj::storage::NormalizeGuestPath(&parsed))
    {
        LogError("guest path '%s' escapes the HDD directory", text.c_str());
        return kExitUsage;
    }

    const std::string relative = re2dj::storage::GuestPathToRelativeString(parsed);
    std::filesystem::path resolved;
    if (!root.Resolve(relative, &resolved))
    {
        LogInfo("guest path : %s", re2dj::storage::GuestPathToString(parsed).c_str());
        LogInfo("relative   : %s", relative.c_str());
        LogInfo("host path  : <not found>");
        return kExitHddError;
    }

    LogInfo("guest path : %s", re2dj::storage::GuestPathToString(parsed).c_str());
    LogInfo("relative   : %s", relative.c_str());
    LogInfo("host path  : %s", resolved.string().c_str());
    return kExitOk;
}

int RunChdTarget(const Options& options,
                 const std::filesystem::path& chd_path,
                 const re2dj::target::BuiltInTargetProfile& built_in)
{
    std::unique_ptr<re2dj::storage::Fat32Volume> volume;
    std::string error;
    if (!re2dj::storage::Fat32Volume::Open(chd_path, &volume, &error))
    {
        LogError("%s", error.c_str());
        return kExitHddError;
    }
    const std::string_view executable_path = options.guest_executable.empty()
                                                 ? std::string_view(built_in.profile.executable_relative_path)
                                                 : std::string_view(options.guest_executable);
    if (executable_path.empty())
    {
        LogError("CHD profile has no executable path");
        return kExitHddError;
    }
    re2dj::storage::Fat32Entry executable_entry;
    if (!volume->Find(executable_path, &executable_entry, &error) || executable_entry.directory)
    {
        LogError("CHD does not contain %.*s: %s",
                 static_cast<int>(executable_path.size()),
                 executable_path.data(),
                 error.c_str());
        return kExitHddError;
    }
    std::vector<std::uint8_t> executable_bytes;
    if (!volume->ReadFile(executable_path, &executable_bytes, &error))
    {
        LogError("cannot read %.*s from CHD: %s",
                 static_cast<int>(executable_path.size()),
                 executable_path.data(),
                 error.c_str());
        return kExitHddError;
    }
    re2dj::exe::PeImageInfo executable_info;
    if (!re2dj::exe::ReadPeImageInfo(executable_bytes.data(),
                                     executable_bytes.size(),
                                     &executable_info,
                                     &error) ||
        !re2dj::exe::IsGuestExecutable(executable_info))
    {
        LogError("CHD executable is not a PE32 guest image: %s",
                 error.empty() ? "unexpected executable format" : error.c_str());
        return kExitHddError;
    }

    re2dj::target::TargetProfile profile = built_in.profile;
    profile.executable_relative_path = std::string(executable_path);
    // A launcher's child below the profile executable's directory keeps that
    // directory as its root (task 434).
    profile.working_directory_relative_path = re2dj::target::ExecutableWorkingDirectory(
        built_in.profile.executable_relative_path, profile.executable_relative_path);
    profile.detected = false;

    LogInfo("chd image   : %s", chd_path.string().c_str());
    LogInfo("filesystem  : FAT32 label=%s data_lba=%llu clusters=%u",
                volume->info().volume_label.c_str(),
                static_cast<unsigned long long>(volume->info().data_lba),
                volume->info().cluster_count);
    LogInfo("scanned     : CHD-backed FAT32 lookup (directory materialization deferred)");
    LogInfo("executables : 1 selected profile executable");
    if (!options.resolve_path.empty())
    {
        return ResolveOneChdPath(*volume, options.resolve_path);
    }

    LogInfo("targets:");
    PrintProfile(profile, true);
    if (options.list_targets)
    {
        return kExitOk;
    }
    LogInfo("selected target : %s", profile.id.c_str());
    LogInfo("display name    : %s", profile.display_name.c_str());
    LogInfo("executable      : %s", profile.executable_relative_path.c_str());
    LogInfo("working dir     : %s", profile.working_directory_relative_path.c_str());
    LogInfo("guest path      : <not known for this dump>");
    LogInfo("format hint     : %s",
                std::string(re2dj::target::ExecutableFormatHintName(profile.format_hint)).c_str());
    LogInfo("machine         : %s",
                std::string(re2dj::exe::MachineName(executable_info.machine)).c_str());
    LogInfo("magic           : %s",
                std::string(re2dj::exe::MagicName(executable_info.magic)).c_str());
    LogInfo("subsystem       : %s",
                std::string(re2dj::exe::SubsystemName(executable_info.subsystem)).c_str());
    LogInfo("image base      : 0x%08llx",
                static_cast<unsigned long long>(executable_info.image_base));
    LogInfo("entry point rva : 0x%08x", executable_info.entry_point_rva);
    const std::string entry_section(re2dj::exe::EntryPointSectionName(executable_info));
    LogInfo("entry section   : %s%s",
                entry_section.empty() ? "<outside every section>" : entry_section.c_str(),
                re2dj::exe::HasEntryPointOutsideTextSection(executable_info)
                    ? "  (outside .text - likely a protection stub)"
                    : "");
    LogInfo("sections        : %u",
                static_cast<unsigned>(executable_info.sections.size()));
    if (!profile.note.empty())
    {
        LogInfo("note: %s", profile.note.c_str());
    }
    if (!options.run)
    {
        LogInfo("Nothing was executed. Pass --run to stage the PE and enter the available execution backend.");
        return kExitOk;
    }

#if defined(RE2DJ_IN_PROCESS_HOST)
    std::filesystem::path staging_root;
    if (!PrepareChdStaging(*volume,
                           profile.id,
                           profile.executable_relative_path,
                           profile.run_defaults.child_executable_paths,
                           &staging_root,
                           &error))
    {
        LogError("cannot stage CHD executable: %s", error.c_str());
        return kExitHddError;
    }
    const std::filesystem::path staged_executable_path =
        staging_root / profile.executable_relative_path;
    re2dj::platform::native::OriginalRunResult run_result;
    const bool executed = RunInProcessOriginal(
        options, profile, staged_executable_path, executable_info, chd_path, {}, &run_result, &error);
    if (!executed)
    {
        LogFatal("EXECUTION_FAILED", "execution failed: %s", error.c_str());
        return kExitNotImplemented;
    }
    LogInfo("load base       : 0x%08x", run_result.load_base.value());
    LogInfo("entry point     : 0x%08x", run_result.entry_point.value());
    if (IsContinuationRun(options))
    {
        if (run_result.seh_dispatch_count > 0)
        {
            LogInfo("seh dispatched  : count=%u last_handler=0x%08x resumed_eip=0x%08x",
                        run_result.seh_dispatch_count,
                        run_result.last_seh_handler.value(),
                        run_result.last_seh_resumed_eip.value());
        }
        PrintApiCalls(run_result);
        PrintDeviceActivity(run_result);
    }
    if (run_result.boundary == re2dj::platform::native::OriginalRunBoundary::kImportGate)
    {
        if (run_result.by_ordinal)
        {
            LogInfo("first boundary  : import %s!#%u",
                        run_result.module.c_str(),
                        static_cast<unsigned>(run_result.ordinal));
        }
        else
        {
            LogInfo("first boundary  : import %s!%s",
                        run_result.module.c_str(),
                        run_result.name.c_str());
        }
        if (run_result.import_stack_observed)
        {
            LogInfo("stack ret / arg0: 0x%08x / 0x%08x",
                        run_result.import_return_address,
                        run_result.import_first_argument);
        }
        if (run_result.import_first_argument_text_observed)
        {
            LogInfo("arg0 text       : %s", run_result.import_first_argument_text.c_str());
        }
    }
    else if (run_result.boundary ==
             re2dj::platform::native::OriginalRunBoundary::kFirstImportCompleted)
    {
        LogInfo("first completion : return 0x%08x, SIGTRAP EIP 0x%08x",
                    run_result.import_return_address,
                    run_result.instruction_pointer.value());
    }
    else if (run_result.boundary ==
             re2dj::platform::native::OriginalRunBoundary::kFirstResolverObserved)
    {
        LogInfo("first resolver completion: %s, return 0x%08x, SIGTRAP EIP 0x%08x",
                    run_result.import_first_argument_text.c_str(),
                    run_result.import_return_address,
                    run_result.instruction_pointer.value());
        PrintResolverIdentity(run_result);
    }
    else if (run_result.boundary ==
             re2dj::platform::native::OriginalRunBoundary::kGetVersionCalled)
    {
        LogInfo("GetVersion call completion: return 0x%08x, SIGTRAP EIP 0x%08x",
                    run_result.import_return_address,
                    run_result.instruction_pointer.value());
        PrintResolverIdentity(run_result);
        PrintInstructionTrace(run_result);
    }
    else if (run_result.boundary ==
             re2dj::platform::native::OriginalRunBoundary::kGetVersionCallNotReached)
    {
        LogInfo("GetVersion thunk not reached: signal %u, EIP 0x%08x",
                    run_result.status_code,
                    run_result.instruction_pointer.value());
        PrintFaultObservation(run_result);
        PrintInstructionTrace(run_result);
        PrintResolverIdentity(run_result);
        if (run_result.unhandled_dynamic_request_observed)
        {
            LogFatal("HLE_UNIMPLEMENTED",
                     "dynamic request=%s",
                     run_result.unhandled_dynamic_request.c_str());
        }
    }
    else if (run_result.boundary ==
             re2dj::platform::native::OriginalRunBoundary::kCreateFileCalled)
    {
        LogInfo("CreateFileA call completion: return 0x%08x, SIGTRAP EIP 0x%08x",
                    run_result.import_return_address,
                    run_result.instruction_pointer.value());
        PrintCreateFileObservation(run_result);
        PrintResolverIdentity(run_result);
    }
    else if (run_result.boundary ==
             re2dj::platform::native::OriginalRunBoundary::kCreateFileCallNotReached)
    {
        LogInfo("CreateFileA thunk not reached: signal %u, EIP 0x%08x",
                    run_result.status_code,
                    run_result.instruction_pointer.value());
        PrintFaultObservation(run_result);
    }
    else if (!PrintContinuationBoundary(run_result))
    {
        LogInfo("first boundary  : terminal status=%u", run_result.status_code);
    }
    return kExitOk;
#else
    LogFatal("EXECUTION_UNSUPPORTED",
             "CHD-backed execution is not connected to an execution backend on this host");
    return kExitNotImplemented;
#endif
}

}  // namespace

int RunMain(int argc, char** argv)
{
    LoggingLifetime logging_lifetime;
    re2dj::logging::LoggerOptions logger_options;
    std::filesystem::path log_file;
    std::string logging_error;
    if (!re2dj::logging::Initialize(logger_options, &log_file, &logging_error))
    {
        std::fprintf(stderr, "fatal: cannot initialize logging: %s\n", logging_error.c_str());
        return kExitLoggingError;
    }
    re2dj::logging::GetLogger()->info("{} starting", re2dj::VersionBanner("re2DJ", re2dj::VersionString()));
    re2dj::logging::GetLogger()->info("Log file: {}", log_file.string());
    {
        std::filesystem::path api_log = log_file;
        api_log.replace_extension(".api.log");
        re2dj::logging::GetLogger()->info("API log : {}", api_log.string());
    }

    Options options;
    if (!ParseOptions(argc, argv, &options))
    {
        return kExitUsage;
    }
#if defined(RE2DJ_IN_PROCESS_HOST)
    // A child run gets this run's options; the child options (each with its
    // value) are its own.
    for (int index = 0; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--guest-executable" || argument == "--guest-command-line" ||
            argument == "--guest-current-directory" || argument == "--guest-startup-reserved" ||
            argument == "--guest-exit-code-fd")
        {
            ++index;
            continue;
        }
        // The child options go after these arguments, where a `--` would
        // stop them being read as options. Profile ids never start with '-',
        // so the arguments it guarded read the same without it.
        if (argument == kEndOfOptions)
        {
            continue;
        }
        g_child_base_arguments.emplace_back(argument);
    }
#endif
#if defined(RE2DJ_IN_PROCESS_HOST)
    InProcessHostLifetime host_lifetime;
    host_lifetime.hold_window = options.hold_window;
#endif
    if (options.show_version)
    {
        std::printf("%s\n", re2dj::VersionBanner("re2DJ", re2dj::VersionString()).c_str());
        return kExitOk;
    }
#if defined(RE2DJ_IN_PROCESS_HOST)
    // A bare run opens the launcher (#12); without a window it falls through
    // to the usage text, as before.
    if (re2dj::host::LauncherRequested(argc, std::getenv("RE2DJ_LAUNCHER")))
    {
        const re2dj::host::LauncherSessionResult launched = re2dj::host::RunLauncherSession();
        if (launched.ran)
        {
            return launched.exit_code;
        }
    }
#endif
    if (options.show_help || argc == 1)
    {
        PrintUsage();
        return options.show_help ? kExitOk : kExitUsage;
    }
    const re2dj::target::BuiltInTargetProfile* shortcut =
        options.target_id.empty()
            ? nullptr
            : re2dj::target::FindBuiltInTargetProfileById(options.target_id);
    if (shortcut != nullptr &&
        shortcut->profile.run_defaults.hdd_input_kind ==
            re2dj::target::HddInputKind::kMameChd)
    {
        const std::filesystem::path input =
            options.hdd_directory.empty()
                ? std::filesystem::current_path() /
                      shortcut->profile.run_defaults.default_hdd_image_relative_path
                : options.hdd_directory;
        std::filesystem::path chd_path;
        std::string chd_error;
        if (!re2dj::hdd::FindChdImage(input, &chd_path, &chd_error))
        {
            LogError("%s", chd_error.c_str());
            return kExitHddError;
        }
        return RunChdTarget(options, chd_path, *shortcut);
    }
    if (options.hdd_directory.empty() && !options.target_id.empty())
    {
        const re2dj::target::BuiltInTargetProfile* directory_shortcut =
            re2dj::target::FindBuiltInTargetProfileById(options.target_id);
        if (directory_shortcut == nullptr ||
            directory_shortcut->profile.run_defaults.default_hdd_directory_relative_path.empty())
        {
            LogError("profile '%s' has no default HDD directory; pass --hdd <directory>",
                     options.target_id.c_str());
            return kExitUsage;
        }
        options.hdd_directory = std::filesystem::current_path() /
                                directory_shortcut->profile.run_defaults
                                    .default_hdd_directory_relative_path;
    }
    if (options.hdd_directory.empty())
    {
        LogError("--hdd <directory> is required");
        return kExitUsage;
    }

    re2dj::hdd::HddRoot root;
    std::string error;
    if (!re2dj::hdd::HddRoot::Open(options.hdd_directory, &root, &error))
    {
        LogError("%s", error.c_str());
        return kExitHddError;
    }

    if (!options.resolve_path.empty())
    {
        return ResolveOnePath(root, options.resolve_path);
    }

    const re2dj::hdd::HddScanResult scan = re2dj::hdd::ScanHdd(root);
    const std::vector<re2dj::target::TargetProfile> profiles =
        re2dj::target::BuildTargetProfiles(root, scan);

    LogInfo("hdd root   : %s", scan.root.string().c_str());
    LogInfo("scanned    : %zu directories, %zu files%s",
                scan.directory_count,
                scan.file_count,
                scan.truncated ? " (truncated)" : "");
    LogInfo("executables: %zu", scan.executables.size());

    if (profiles.empty())
    {
        LogError("no 32-bit x86 PE32 executable found under %s",
                 scan.root.string().c_str());
        return kExitHddError;
    }

    const re2dj::target::TargetProfile* selected = nullptr;
    if (options.target_id.empty())
    {
        selected = &profiles.front();
    }
    else
    {
        selected = re2dj::target::FindTargetProfileById(profiles, options.target_id);
        if (selected == nullptr)
        {
            LogError("no target profile with id '%s'", options.target_id.c_str());
            options.list_targets = true;
        }
    }

    LogInfo("targets:");
    for (const re2dj::target::TargetProfile& profile : profiles)
    {
        PrintProfile(profile, selected == &profile);
    }

    if (selected == nullptr)
    {
        return kExitUsage;
    }
    if (options.list_targets)
    {
        return kExitOk;
    }

    LogInfo("selected target : %s", selected->id.c_str());
    LogInfo("display name    : %s", selected->display_name.c_str());
    LogInfo("executable      : %s", selected->executable_relative_path.c_str());
    LogInfo("working dir     : %s",
                selected->working_directory_relative_path.empty()
                    ? "<hdd root>"
                    : selected->working_directory_relative_path.c_str());
    if (selected->guest_drive_letter != '\0')
    {
        LogInfo("guest path      : %c:%s",
                    selected->guest_drive_letter,
                    selected->guest_directory.c_str());
    }
    else
    {
        LogInfo("guest path      : <not known for this dump>");
    }
    LogInfo("format hint     : %s",
                std::string(re2dj::target::ExecutableFormatHintName(selected->format_hint))
                    .c_str());

    const re2dj::hdd::ExecutableEntry* selected_entry = nullptr;
    for (const re2dj::hdd::ExecutableEntry& entry : scan.executables)
    {
        if (entry.relative_path != selected->executable_relative_path)
        {
            continue;
        }
        selected_entry = &entry;
        const re2dj::exe::PeImageInfo& info = entry.pe_info;
        LogInfo("machine         : %s", std::string(re2dj::exe::MachineName(info.machine)).c_str());
        LogInfo("magic           : %s", std::string(re2dj::exe::MagicName(info.magic)).c_str());
        LogInfo("subsystem       : %s",
                    std::string(re2dj::exe::SubsystemName(info.subsystem)).c_str());
        LogInfo("image base      : 0x%08llx",
                    static_cast<unsigned long long>(info.image_base));
        LogInfo("entry point rva : 0x%08x", info.entry_point_rva);
        const std::string entry_section(re2dj::exe::EntryPointSectionName(info));
        LogInfo("entry section   : %s%s",
                    entry_section.empty() ? "<outside every section>" : entry_section.c_str(),
                    re2dj::exe::HasEntryPointOutsideTextSection(info)
                        ? "  (outside .text - likely a protection stub)"
                        : "");
        LogInfo("sections        : %u", static_cast<unsigned>(info.sections.size()));
        break;
    }

    if (!selected->note.empty())
    {
        LogInfo("note: %s", selected->note.c_str());
    }

    if (!options.run)
    {
        LogInfo("Nothing was executed. Pass --run to enter the available execution "
                "backend, or use re2dj_pe_analyzer for a full header dump.");
        return kExitOk;
    }

#if defined(RE2DJ_IN_PROCESS_HOST)
    if (selected_entry == nullptr)
    {
        LogError("selected executable metadata is unavailable");
        return kExitHddError;
    }

    std::filesystem::path executable_path;
    if (!root.ResolveFile(selected->executable_relative_path, &executable_path))
    {
        LogError("selected executable is no longer available");
        return kExitHddError;
    }

    re2dj::platform::native::OriginalRunResult run_result;
    const bool executed = RunInProcessOriginal(
        options, *selected, executable_path, selected_entry->pe_info, {}, scan.root, &run_result, &error);
    if (!executed)
    {
        LogFatal("EXECUTION_FAILED", "execution failed: %s", error.c_str());
        return kExitNotImplemented;
    }

    LogInfo("load base       : 0x%08x", run_result.load_base.value());
    LogInfo("entry point     : 0x%08x", run_result.entry_point.value());
    if (IsContinuationRun(options))
    {
        if (run_result.seh_dispatch_count > 0)
        {
            LogInfo("seh dispatched  : count=%u last_handler=0x%08x resumed_eip=0x%08x",
                        run_result.seh_dispatch_count,
                        run_result.last_seh_handler.value(),
                        run_result.last_seh_resumed_eip.value());
        }
        PrintApiCalls(run_result);
        PrintDeviceActivity(run_result);
    }
    switch (run_result.boundary)
    {
    case re2dj::platform::native::OriginalRunBoundary::kImportGate:
    {
        if (run_result.by_ordinal)
        {
            LogInfo("first boundary  : import %s!#%u",
                        run_result.module.c_str(),
                        static_cast<unsigned>(run_result.ordinal));
        }
        else
        {
            LogInfo("first boundary  : import %s!%s",
                        run_result.module.c_str(),
                        run_result.name.c_str());
        }
        LogInfo("gate / eip / esp: 0x%08x / 0x%08x / 0x%08x",
                    run_result.gate_address.value(),
                    run_result.instruction_pointer.value(),
                    run_result.stack_pointer.value());
        if (run_result.import_stack_observed)
        {
            LogInfo("stack ret / arg0: 0x%08x / 0x%08x",
                        run_result.import_return_address,
                        run_result.import_first_argument);
        }
        if (run_result.import_first_argument_text_observed)
        {
            LogInfo("arg0 text       : %s", run_result.import_first_argument_text.c_str());
        }
        const std::string export_name = run_result.by_ordinal
            ? "#" + std::to_string(run_result.ordinal)
            : run_result.name;
        LogFatal("HLE_UNIMPLEMENTED",
                 "execution stopped at import %s!%s",
                 run_result.module.c_str(),
                 export_name.c_str());
        return kExitNotImplemented;
    }
    case re2dj::platform::native::OriginalRunBoundary::kProcessExit:
        if (IsContinuationRun(options))
        {
            PrintContinuationBoundary(run_result);
            return kExitOk;
        }
        LogInfo("first boundary  : process exit (guest status 0x%08x)",
                    run_result.status_code);
        return kExitOk;
    case re2dj::platform::native::OriginalRunBoundary::kFault:
        LogFatal("GUEST_FAULT",
                 "host signal/status %u, eip 0x%08x",
                 run_result.status_code,
                 run_result.instruction_pointer.value());
        return kExitNotImplemented;
    case re2dj::platform::native::OriginalRunBoundary::kStopped:
        LogFatal("EXECUTION_STOPPED", "guest stopped before a supported terminal boundary");
        return kExitNotImplemented;
    case re2dj::platform::native::OriginalRunBoundary::kFirstImportCompleted:
        LogInfo("first import completion: return 0x%08x, SIGTRAP EIP 0x%08x",
                    run_result.import_return_address, run_result.instruction_pointer.value());
        return kExitOk;
    case re2dj::platform::native::OriginalRunBoundary::kFirstResolverObserved:
        LogInfo("first resolver completion: %s, return 0x%08x, SIGTRAP EIP 0x%08x",
                    run_result.import_first_argument_text.c_str(),
                    run_result.import_return_address, run_result.instruction_pointer.value());
        PrintResolverIdentity(run_result);
        return kExitOk;
    case re2dj::platform::native::OriginalRunBoundary::kGetVersionCalled:
        LogInfo("GetVersion call completion: return 0x%08x, SIGTRAP EIP 0x%08x",
                    run_result.import_return_address, run_result.instruction_pointer.value());
        PrintResolverIdentity(run_result);
        PrintInstructionTrace(run_result);
        return kExitOk;
    case re2dj::platform::native::OriginalRunBoundary::kGetVersionCallNotReached:
        LogInfo("GetVersion thunk not reached: signal %u, EIP 0x%08x",
                    run_result.status_code, run_result.instruction_pointer.value());
        PrintFaultObservation(run_result);
        PrintInstructionTrace(run_result);
        PrintResolverIdentity(run_result);
        if (run_result.unhandled_dynamic_request_observed)
        {
            LogFatal("HLE_UNIMPLEMENTED",
                     "dynamic request=%s",
                     run_result.unhandled_dynamic_request.c_str());
        }
        return kExitOk;
    case re2dj::platform::native::OriginalRunBoundary::kCreateFileCalled:
        LogInfo("CreateFileA call completion: return 0x%08x, SIGTRAP EIP 0x%08x",
                    run_result.import_return_address,
                    run_result.instruction_pointer.value());
        PrintCreateFileObservation(run_result);
        PrintResolverIdentity(run_result);
        return kExitOk;
    case re2dj::platform::native::OriginalRunBoundary::kCreateFileCallNotReached:
        LogInfo("CreateFileA thunk not reached: signal %u, EIP 0x%08x",
                    run_result.status_code,
                    run_result.instruction_pointer.value());
        PrintFaultObservation(run_result);
        return kExitOk;
    case re2dj::platform::native::OriginalRunBoundary::kContinuationUnhandledImport:
    case re2dj::platform::native::OriginalRunBoundary::kContinuationUnresolvedLookup:
    case re2dj::platform::native::OriginalRunBoundary::kContinuationFault:
    case re2dj::platform::native::OriginalRunBoundary::kContinuationCallLimit:
    case re2dj::platform::native::OriginalRunBoundary::kContinuationHostClosed:
        PrintContinuationBoundary(run_result);
        return kExitOk;
    }
    LogFatal("EXECUTION_FAILED", "the run ended at an unknown boundary %u",
             static_cast<unsigned>(run_result.boundary));
    return kExitNotImplemented;
#else
    LogFatal("EXECUTION_UNSUPPORTED",
             "--run is not connected to an execution backend on this host");
    return kExitNotImplemented;
#endif
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
    // The guest image's range is held from before the loader ran, and the
    // guest gets a guest-sized stack (task 449).
    return re2dj::platform::windows::RunGuestReadyProcess(&RunMain, argc, argv);
#else
    return RunMain(argc, argv);
#endif
}
