cmake_minimum_required(VERSION 3.20)

project(re2DJ LANGUAGES C CXX)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RE2DJ_VERSION LIMIT_COUNT 1)
string(STRIP "${RE2DJ_VERSION}" RE2DJ_VERSION)
if(NOT RE2DJ_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must use major.minor.patch format")
endif()
message(STATUS "re2DJ version ${RE2DJ_VERSION}")
# The build configuration (Debug, Release, ...) the window title shows; see
# re2dj/version.h.
add_compile_definitions(RE2DJ_BUILD_CONFIG="$<CONFIG>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(spdlog 1.15.3 EXACT CONFIG QUIET)
if(NOT spdlog_FOUND)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(RE2DJ_BUILD_TESTS "Build the unit test suite" ON)
option(RE2DJ_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(RE2DJ_ENABLE_SDL3_AUDIO "Build the SDL3/SDL3_mixer audio backend" ON)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
# Gamepads read through the gamepad subsystem (task 444); hidapi serves the
# pads evdev does not describe, and loads no libusb.
set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
# Sound plays through SDL3_mixer on the 32-bit Windows runtime and on Linux.
if(RE2DJ_ENABLE_SDL3_AUDIO AND ((WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4) OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(RE2DJ_SDL3_AUDIO_ACTIVE ON)
else()
    set(RE2DJ_SDL3_AUDIO_ACTIVE OFF)
endif()
if(RE2DJ_SDL3_AUDIO_ACTIVE)
    set(SDL_AUDIO ON CACHE BOOL "" FORCE)
else()
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG 147a8ee32dbf9ac02f3794964490687b6bbda1bc
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

# Dear ImGui ships no CMake project, so only its sources are fetched and the
# on-screen display library below builds the parts it uses. The platform
# backends are deliberately left out: the OSD feeds input itself so the
# shared core never depends on a host windowing API.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 01380c579715e62fb9a8d6ec0502c4ea83bfde6e  # v1.92.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Win32 x86 and Linux x86 are supported product-host configurations. Other
# non-Windows 32-bit configurations remain reserved for explicit probes.
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING
        "Configuring an unsupported 32-bit non-Windows host build.")
endif()

# The OS-neutral in-process runner both OS backends build (task 446).
set(RE2DJ_NATIVE_RUNNER_SOURCES
    src/platform/native/game_controls.cpp
    src/platform/native/native_continuation_observation.cpp
    src/platform/native/native_create_file_observation.cpp
    src/platform/native/native_dynamic_thunk.cpp
    src/platform/native/native_fault_observation.cpp
    src/platform/native/native_getversion_observation.cpp
    src/platform/native/native_guest_module_image.cpp
    src/platform/native/native_guest_module_set.cpp
    src/platform/native/native_guest_seh.cpp
    src/platform/native/native_guest_threads.cpp
    src/platform/native/native_image_dump.cpp
    src/platform/native/native_import_thunks.cpp
    src/platform/native/native_in_process_runner.cpp
    src/platform/native/native_instruction_trace.cpp
    src/platform/native/native_kernel32_diagnostic.cpp
    src/platform/native/native_legacy_io.cpp
    src/platform/native/native_pe_image.cpp
    src/platform/native/native_pe_session.cpp
    src/platform/native/original_runner.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(re2dj_linux_native_backend STATIC
        ${RE2DJ_NATIVE_RUNNER_SOURCES}
        src/platform/sdl/host_presentation.cpp
        src/platform/sdl/host_keyboard.cpp
        src/platform/linux/host_process_launcher.cpp
        src/platform/linux/native_host_services.cpp
        src/platform/linux/self_process.cpp
        src/platform/linux/https_download.cpp
    )
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_sources(re2dj_linux_native_backend PRIVATE
            src/platform/linux/x86/native_import_bridge.cpp
            src/platform/linux/x86/native_low_memory.cpp
            src/platform/linux/x86/native_process_bootstrap.cpp
        )
        target_compile_options(re2dj_linux_native_backend PRIVATE -fno-omit-frame-pointer)
        add_executable(re2dj_linux_native_in_process_probe
            src/platform/linux/x86/native_in_process_probe.cpp
            src/platform/native/native_thread_probe.cpp
            src/platform/native/native_probe_fixture.cpp
        )
        target_link_libraries(re2dj_linux_native_in_process_probe
            PRIVATE re2dj_linux_native_backend re2dj_warnings
        )
        add_executable(re2dj_linux_native_guest_module_probe
            src/platform/linux/x86/native_guest_module_probe.cpp
        )
        target_link_libraries(re2dj_linux_native_guest_module_probe
            PRIVATE re2dj_linux_native_backend re2dj_warnings
        )
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_sources(re2dj_linux_native_backend PRIVATE
            src/platform/linux/x64/native_compat_mode.cpp
            src/platform/linux/x64/native_compat_mode_transition.cpp
            src/platform/linux/x64/native_import_bridge.cpp
            src/platform/linux/x64/native_low_memory.cpp
            src/platform/linux/x64/native_process_bootstrap.cpp
        )
        target_compile_options(re2dj_linux_native_backend PRIVATE -fno-omit-frame-pointer)
        add_executable(re2dj_linux_compat_mode_probe
            src/platform/linux/x64/native_compat_mode_probe.cpp
        )
        target_link_libraries(re2dj_linux_compat_mode_probe
            PRIVATE re2dj_linux_native_backend re2dj_warnings
        )
        add_executable(re2dj_linux_native_in_process_probe
            src/platform/linux/x64/native_in_process_probe.cpp
            src/platform/native/native_thread_probe.cpp
            src/platform/native/native_probe_fixture.cpp
        )
        target_link_libraries(re2dj_linux_native_in_process_probe
            PRIVATE re2dj_linux_native_backend re2dj_warnings
        )
    endif()
    target_link_libraries(re2dj_linux_native_backend
        PUBLIC re2dj_core
        PRIVATE re2dj_sdl3_opengl_backend re2dj_sdl3_gamepad re2dj_osd SDL3::SDL3-static re2dj_logging re2dj_warnings
    )
endif()

# The Windows x86 backend of the same runner (task 448): VEH faults, a shadow
# TEB for the HLE, MSVC naked asm for the transitions.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(re2dj_windows_native_backend STATIC
        ${RE2DJ_NATIVE_RUNNER_SOURCES}
        src/platform/windows/native_host_services.cpp
        src/platform/windows/x86/native_guest_transition.cpp
        src/platform/windows/x86/native_import_bridge.cpp
        src/platform/windows/x86/native_low_memory.cpp
        src/platform/windows/x86/native_process_bootstrap.cpp
        src/platform/windows/x86/native_guest_reservation.cpp
        src/platform/windows/host_process_launcher.cpp
        src/platform/windows/self_process.cpp
        src/platform/windows/https_download.cpp
        src/platform/sdl/host_presentation.cpp
        src/platform/sdl/host_keyboard.cpp
    )
    target_link_libraries(re2dj_windows_native_backend
        PUBLIC re2dj_core
        PRIVATE re2dj_sdl3_opengl_backend re2dj_sdl3_gamepad re2dj_osd SDL3::SDL3-static re2dj_logging re2dj_warnings
                # The launcher's update download (#17).
                winhttp
    )
    if(RE2DJ_BUILD_TESTS)
        add_executable(re2dj_windows_native_in_process_probe
            src/platform/windows/x86/native_in_process_probe.cpp
            src/platform/native/native_thread_probe.cpp
            src/platform/native/native_probe_fixture.cpp
        )
        target_link_libraries(re2dj_windows_native_in_process_probe
            PRIVATE re2dj_windows_native_backend re2dj_warnings
        )
        # As re2dj.exe will be: off the guest's 0x400000, at a fixed base.
        target_link_options(re2dj_windows_native_in_process_probe PRIVATE
            /BASE:0x60000000 /DYNAMICBASE:NO /LARGEADDRESSAWARE
        )
    endif()
endif()

# Research probe for a future Windows x64 backend (task 452): compatibility
# mode and the guest FS in a 64-bit process. Not part of the product or CTest.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND MSVC)
    add_executable(re2dj_windows_x64_compat_mode_probe
        src/platform/windows/x64/native_compat_mode_probe.cpp
    )
    target_link_libraries(re2dj_windows_x64_compat_mode_probe PRIVATE re2dj_warnings)
    # A far return into compatibility mode is not expected to pass a hardware
    # shadow stack.
    target_link_options(re2dj_windows_x64_compat_mode_probe PRIVATE /CETCOMPAT:NO)
endif()

add_library(re2dj_warnings INTERFACE)
if(MSVC)
    target_compile_options(re2dj_warnings INTERFACE /W4 /permissive- /utf-8)
    if(RE2DJ_WARNINGS_AS_ERRORS)
        target_compile_options(re2dj_warnings INTERFACE /WX)
    endif()
    # The CRT "secure" warnings push code toward Microsoft-only replacements,
    # which the portability rules forbid in the shared core.
    target_compile_definitions(re2dj_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(re2dj_warnings INTERFACE -Wall -Wextra -Wpedantic)
    # GCC 12's middle-end warnings misfire inside libstdc++ when optimising:
    # -Wrestrict on "#" + std::to_string(n)
    # (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105651) and -Warray-bounds
    # on std::vector::insert (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=107852),
    # with the stringop warnings of the same family. They stay warnings there
    # rather than errors. The Linux release builds with Debian 12's GCC 12
    # (task 442).
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12
       AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        target_compile_options(re2dj_warnings INTERFACE
            -Wno-error=restrict -Wno-error=array-bounds
            -Wno-error=stringop-overflow -Wno-error=stringop-overread)
    endif()
    if(RE2DJ_WARNINGS_AS_ERRORS)
        target_compile_options(re2dj_warnings INTERFACE -Werror)
    endif()
endif()

# libchdr is the CHD implementation boundary. It carries MAME's BSD-3-Clause
# CHD reader and its permissively licensed codec dependencies, so the core does
# not duplicate the format or codec implementations.
add_library(re2dj_chdr_lzma STATIC
    third_party/libchdr/deps/lzma-26.02/src/LzmaDec.c
)
target_include_directories(re2dj_chdr_lzma PRIVATE
    third_party/libchdr/deps/lzma-26.02/include
)

add_library(re2dj_chdr_miniz STATIC
    third_party/libchdr/deps/miniz-3.1.2/miniz.c
)
target_include_directories(re2dj_chdr_miniz PRIVATE
    third_party/libchdr/deps/miniz-3.1.2
)
# The ZIP reader stays on for the launcher's update (#17), which unpacks the
# Windows release zip from memory; the file-based and writing APIs stay off.
target_compile_definitions(re2dj_chdr_miniz PUBLIC
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
)

add_library(re2dj_chdr_zstd STATIC
    third_party/libchdr/deps/zstd-1.5.7/zstddeclib.c
)
target_include_directories(re2dj_chdr_zstd PRIVATE
    third_party/libchdr/deps/zstd-1.5.7
)

add_library(re2dj_chdr STATIC
    third_party/libchdr/src/libchdr_bitstream.c
    third_party/libchdr/src/libchdr_cdrom.c
    third_party/libchdr/src/libchdr_chd.c
    third_party/libchdr/src/libchdr_codec_avhuff.c
    third_party/libchdr/src/libchdr_codec_cdfl.c
    third_party/libchdr/src/libchdr_codec_cdlz.c
    third_party/libchdr/src/libchdr_codec_cdzl.c
    third_party/libchdr/src/libchdr_codec_cdzs.c
    third_party/libchdr/src/libchdr_codec_flac.c
    third_party/libchdr/src/libchdr_codec_huff.c
    third_party/libchdr/src/libchdr_codec_lzma.c
    third_party/libchdr/src/libchdr_codec_zlib.c
    third_party/libchdr/src/libchdr_codec_zstd.c
    third_party/libchdr/src/libchdr_flac.c
    third_party/libchdr/src/libchdr_huffman.c
)
target_include_directories(re2dj_chdr PUBLIC third_party/libchdr/include)
target_link_libraries(re2dj_chdr
    PRIVATE re2dj_chdr_lzma re2dj_chdr_miniz re2dj_chdr_zstd
)
target_compile_definitions(re2dj_chdr PRIVATE
    WANT_RAW_DATA_SECTOR=1
    WANT_SUBCODE=1
    VERIFY_BLOCK_CRC=1
    LOWRAM_TARGET=0
    _CRT_SECURE_NO_WARNINGS
)

add_library(re2dj_storage_common STATIC
    src/storage/guest_path.cpp
)
target_include_directories(re2dj_storage_common PUBLIC include)
target_link_libraries(re2dj_storage_common PRIVATE re2dj_warnings)

add_library(re2dj_logging STATIC
    src/logging/logging.cpp
)
target_include_directories(re2dj_logging PUBLIC include)
target_link_libraries(re2dj_logging
    PUBLIC spdlog::spdlog
    PRIVATE re2dj_warnings
)

add_library(re2dj_chd_storage STATIC
    src/storage/chd_hunk_cache.cpp
    src/storage/fat32_chd.cpp
    src/storage/fat32_directory_name.cpp
    src/storage/guest_find.cpp
    src/storage/mame_chd.cpp
)
target_include_directories(re2dj_chd_storage
    PUBLIC include
    PRIVATE third_party/libchdr/include
)
target_link_libraries(re2dj_chd_storage
    PUBLIC re2dj_chdr re2dj_storage_common
    PRIVATE re2dj_warnings
)

# The DirectX HLE's shared description of itself and its guest ABI, used by
# the Windows COM facade and the Linux gate facade alike.
add_library(re2dj_directx STATIC
    src/directx/direct3d_description.cpp
    src/directx/direct3d_device.cpp
    src/directx/direct3d_draw.cpp
    src/directx/direct3d_vertex_buffer.cpp
    src/directx/directdraw_description.cpp
    src/directx/directdraw_display.cpp
    src/directx/directdraw_surface.cpp
    src/directx/directinput.cpp
)
target_include_directories(re2dj_directx PUBLIC include)
target_link_libraries(re2dj_directx PUBLIC re2dj_legacy_graphics PRIVATE re2dj_warnings)

add_library(re2dj_legacy_graphics STATIC
    src/graphics/color_depth.cpp
    src/graphics/gl_renderer_identity.cpp
    src/graphics/legacy_draw_command.cpp
    src/graphics/legacy_texture.cpp
    src/graphics/legacy_transform.cpp
    src/graphics/legacy_vertex_buffer.cpp
    src/graphics/present_interval_histogram.cpp
    src/graphics/present_sync.cpp
    src/graphics/present_pacer.cpp
    src/graphics/presentation_filter.cpp
    src/graphics/post_shader_catalog.cpp
    src/graphics/post_shader_preset.cpp
    src/graphics/post_shader_source.cpp
    src/graphics/true_color.cpp
)
target_include_directories(re2dj_legacy_graphics PUBLIC include)
target_link_libraries(re2dj_legacy_graphics PRIVATE re2dj_warnings)

# Task 455, #27: the built-in post-processing shaders are embedded from their
# sources at configure time, so a package never has to ship them. Each file is
# split into raw string pieces of at most 12000 bytes, below MSVC's limit on a
# single literal. Editing one re-runs the configure step.
set(RE2DJ_POST_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/post_shaders")
set(RE2DJ_POST_SHADER_BUILTIN_NAMES
    crt.glsl
    scanline.glsl
    fsr/fsr.glslp
    fsr/easu.glsl
    fsr/rcas.glsl
    scalefx/scalefx.glslp
    scalefx/scalefx-pass0.glsl
    scalefx/scalefx-pass1.glsl
    scalefx/scalefx-pass2.glsl
    scalefx/scalefx-pass3.glsl
    scalefx/scalefx-pass4.glsl
    anime4k/anime4k.glslp
    anime4k/conv0.glsl
    anime4k/conv1.glsl
    anime4k/conv2.glsl
    anime4k/conv3.glsl
    anime4k/depth-to-space.glsl
)
set(RE2DJ_POST_SHADER_BUILTIN_FILES "")
foreach(name IN LISTS RE2DJ_POST_SHADER_BUILTIN_NAMES)
    set(path "${RE2DJ_POST_SHADER_SOURCE_DIR}/${name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" text)
    string(LENGTH "${text}" length)
    set(pieces "")
    set(offset 0)
    while(offset LESS length)
        string(SUBSTRING "${text}" ${offset} 12000 piece)
        string(APPEND pieces "R\"re2dj_glsl(${piece})re2dj_glsl\"\n")
        math(EXPR offset "${offset} + 12000")
    endwhile()
    string(APPEND RE2DJ_POST_SHADER_BUILTIN_FILES "    {\"${name}\",\n${pieces}    },\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${RE2DJ_POST_SHADER_SOURCE_DIR}/post_shader_builtins.inc.in")
configure_file(
    "${RE2DJ_POST_SHADER_SOURCE_DIR}/post_shader_builtins.inc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/re2dj/graphics/post_shader_builtins.inc"
    @ONLY
)
target_include_directories(re2dj_legacy_graphics PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_library(re2dj_sdl3_opengl_backend STATIC
    src/graphics/opengl_post_process.cpp
    src/graphics/sdl3_opengl_backend.cpp
)
target_include_directories(re2dj_sdl3_opengl_backend PUBLIC include)
target_link_libraries(re2dj_sdl3_opengl_backend
    PUBLIC re2dj_legacy_graphics
    PRIVATE SDL3::SDL3-static re2dj_warnings
)

# The SDL3 gamepad reader (task 444): the host's pads as GamepadControls.
add_library(re2dj_sdl3_gamepad STATIC
    src/input/sdl3_gamepad_reader.cpp
)
target_include_directories(re2dj_sdl3_gamepad PUBLIC include)
target_link_libraries(re2dj_sdl3_gamepad
    PRIVATE SDL3::SDL3-static re2dj_warnings
)

add_library(re2dj_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(re2dj_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
# The on-screen display. Platform-neutral: it takes input through its own
# queue and draws through the backend's present overlay point.
add_library(re2dj_osd STATIC
    src/ui/display_controls.cpp
    src/ui/osd.cpp
)
target_include_directories(re2dj_osd PUBLIC include)
target_link_libraries(re2dj_osd
    PUBLIC re2dj_legacy_graphics
    PRIVATE re2dj_imgui re2dj_warnings
)

# ImGui's SDL3 platform backend, for the launcher window only (#12). The OSD
# keeps feeding its own input, so nothing else depends on SDL through ImGui.
add_library(re2dj_imgui_sdl3 STATIC
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
target_link_libraries(re2dj_imgui_sdl3 PUBLIC re2dj_imgui PRIVATE SDL3::SDL3-static)

# The launcher shown when re2dj runs without arguments (#12): the ImGui screen
# and the SDL3 window that hosts it. The catalog and settings it shows live in
# the core.
add_library(re2dj_launcher_ui STATIC
    src/ui/launcher_screen.cpp
    src/platform/sdl/launcher_window.cpp
)
target_include_directories(re2dj_launcher_ui PUBLIC include)
target_link_libraries(re2dj_launcher_ui
    PUBLIC re2dj_core
    PRIVATE re2dj_imgui_sdl3 re2dj_imgui re2dj_sdl3_gamepad re2dj_logging SDL3::SDL3-static re2dj_warnings
)

# Platform-neutral core. Nothing here may include a host OS header; see the
# portability rules in docs/CODING_STYLE.md.
add_library(re2dj_core STATIC
    src/analysis/code_region_score.cpp
    src/audio/directsound_buffer_policy.cpp
    src/audio/directsound_device.cpp
    src/audio/legacy_audio_buffer.cpp
    src/config/hardlock_secret_config.cpp
    src/hle/hardlock/handshake_response.cpp
    src/hle/hardlock/api_descriptor.cpp
    src/hle/hardlock/protocol.cpp
    src/hle/hardlock/device.cpp
    src/hle/hardlock/device_call.cpp
    src/hle/hardlock/device_material.cpp
    src/hle/hardlock/engine.cpp
    src/hle/hardlock/payload_responses.cpp
    src/hle/hardlock/seed_config.cpp
    src/hle/hardlock/transform_responses.cpp
    src/hle/guest_device_path.cpp
    src/hle/api_call_record.cpp
    src/hle/guest_devices.cpp
    src/hle/guest_file_prefetcher.cpp
    src/hle/guest_file_read_journal.cpp
    src/hle/guest_files.cpp
    src/hle/hex_bytes.cpp
    src/hle/private_profile.cpp
    src/hle/wsprintf.cpp
    src/hle/guest_heap.cpp
    src/hle/guest_com.cpp
    src/hle/guest_process.cpp
    src/hle/gdi_raster.cpp
    src/hle/bitmap_file.cpp
    src/hle/guest_gdi.cpp
    src/hle/guest_font.cpp
    src/hle/guest_mixer.cpp
    src/hle/guest_user.cpp
    src/hle/win32_time.cpp
    src/hle/import_dispatcher.cpp
    src/hle/import_event_loop.cpp
    src/hle/modules/advapi32_module.cpp
    src/hle/modules/guest_module.cpp
    src/hle/modules/guest_module_registry.cpp
    src/hle/modules/guest_pe_facade.cpp
    src/hle/modules/kernel32_module.cpp
    src/hle/modules/resolve_only_modules.cpp
    src/hle/modules/ddraw_device7.cpp
    src/hle/modules/ddraw_direct3d7.cpp
    src/hle/modules/ddraw_vertex_buffer7.cpp
    src/hle/modules/ddraw_direct_draw4.cpp
    src/hle/modules/ddraw_module.cpp
    src/hle/modules/ddraw_clipper.cpp
    src/hle/modules/ddraw_surface7.cpp
    src/hle/modules/ddraw_texture2.cpp
    src/hle/modules/dinput_module.cpp
    src/hle/modules/dsound_module.cpp
    src/hle/modules/facade_com.cpp
    src/hle/modules/gdi32_module.cpp
    src/hle/modules/gdi_bitmaps.cpp
    src/hle/modules/gdi_text.cpp
    src/hle/modules/user32_module.cpp
    src/hle/modules/winmm_module.cpp
    src/hle/modules/wtsapi32_module.cpp
    src/device/lptdi_challenge_response.cpp
    src/device/lptdi_response_profile.cpp
    src/input/ez2dj_io_board.cpp
    src/input/ez2dancer_io_board.cpp
    src/input/ez2dancer_io_port_bus.cpp
    src/input/legacy_io_port_bus.cpp
    src/input/legacy_io_trap.cpp
    src/input/virtual_keys.cpp
    src/input/ez2dj_keyboard_map.cpp
    src/input/ez2dancer_keyboard_map.cpp
    src/input/gamepad.cpp
    src/input/io_bindings.cpp
    src/exe/code_scan.cpp
    src/exe/immediate_scan.cpp
    src/exe/pe_image.cpp
    src/hdd/hdd_root.cpp
    src/hdd/chd_image_locator.cpp
    src/hdd/hdd_scan.cpp
    src/launcher/launcher_catalog.cpp
    src/launcher/launcher_settings.cpp
    # The launcher's update check and in-place install (#17).
    src/update/launcher_updater.cpp
    src/update/release_archive.cpp
    src/update/release_info.cpp
    src/update/semantic_version.cpp
    src/update/sha256.cpp
    src/update/update_install.cpp
    src/runtime/address_space.cpp
    src/runtime/pe_loader.cpp
    src/storage/vfs_roots.cpp
    src/storage/vfs_file_table.cpp
    src/target/target_profile.cpp
    src/version.cpp
)

target_include_directories(re2dj_core
    PUBLIC
        include
    PRIVATE
        third_party/libchdr/include
)

target_compile_definitions(re2dj_core
    PRIVATE
        RE2DJ_VERSION="${RE2DJ_VERSION}"
)

target_link_libraries(re2dj_core
    PUBLIC re2dj_directx re2dj_legacy_graphics re2dj_chd_storage
    PRIVATE re2dj_logging re2dj_warnings re2dj_chdr_miniz
)
# The update's archive reader (#17) uses libchdr's miniz directly.
target_include_directories(re2dj_core SYSTEM PRIVATE third_party/libchdr/deps/miniz-3.1.2)

if(RE2DJ_SDL3_AUDIO_ACTIVE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_AIFF OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_WAVE OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_VOC OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_AU OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_FLAC OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_GME OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_MOD OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_MP3 OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_MIDI OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_OPUS OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_VORBIS_STB OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_WAVPACK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_mixer
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
        GIT_TAG 72a81869b45e249e8e67102db4e98dd2441f05a1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3_mixer)
endif()

# The guest's DirectSound plays through SDL3_mixer on both in-process hosts.
if(RE2DJ_SDL3_AUDIO_ACTIVE)
    foreach(backend re2dj_linux_native_backend re2dj_windows_native_backend)
        if(TARGET ${backend})
            target_sources(${backend} PRIVATE
                src/audio/sdl3_mixer_audio_backend.cpp
                src/platform/sdl/host_audio.cpp
            )
            target_include_directories(${backend} PRIVATE src/audio)
            target_link_libraries(${backend} PRIVATE SDL3_mixer::SDL3_mixer-static)
            target_compile_definitions(${backend} PUBLIC RE2DJ_SDL_HOST_AUDIO=1)
        endif()
    endforeach()
    if(RE2DJ_BUILD_TESTS)
        add_executable(re2dj_audio_stream_progress_test
            tests/audio/stream_progress_test.cpp
            src/audio/directsound_device.cpp
            src/audio/legacy_audio_buffer.cpp
            src/audio/sdl3_mixer_audio_backend.cpp
        )
        target_include_directories(re2dj_audio_stream_progress_test PRIVATE include src/audio)
        target_link_libraries(re2dj_audio_stream_progress_test PRIVATE
            SDL3_mixer::SDL3_mixer-static SDL3::SDL3-static)
        add_test(NAME re2dj_audio_stream_progress_test COMMAND re2dj_audio_stream_progress_test)
        set_tests_properties(re2dj_audio_stream_progress_test PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy" TIMEOUT 15)
    endif()
endif()

# std::filesystem needs no separate link library on GCC 9+, Clang with
# libstdc++ 9+ or MSVC, which covers every supported host.

# Command-line host.
add_executable(re2dj
    src/host/cli/main.cpp
    src/host/cli/launcher_session.cpp
)
target_link_libraries(re2dj PRIVATE re2dj_core re2dj_logging re2dj_warnings)
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # The guest image goes at its own base (0x400000 for EZ2DJ), so this
    # program sits elsewhere at a fixed base; it reserves the guest's range
    # and runs on a thread whose stack is the guest's (design 448, task 449).
    target_link_libraries(re2dj PRIVATE re2dj_windows_native_backend re2dj_launcher_ui)
    target_link_options(re2dj PRIVATE /BASE:0x60000000 /DYNAMICBASE:NO /LARGEADDRESSAWARE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(re2dj PRIVATE re2dj_linux_native_backend re2dj_launcher_ui)
endif()

# Non-executing analysis tools.
add_executable(re2dj_hdd_probe
    src/tools/hdd_probe/main.cpp
)
target_link_libraries(re2dj_hdd_probe PRIVATE re2dj_core re2dj_warnings)

add_executable(re2dj_chd_probe
    src/tools/chd_probe/main.cpp
    src/tools/chd_probe/chd_extract.cpp
)
target_link_libraries(re2dj_chd_probe PRIVATE re2dj_core re2dj_warnings)

add_executable(re2dj_pe_analyzer
    src/tools/pe_analyzer/main.cpp
)
target_link_libraries(re2dj_pe_analyzer PRIVATE re2dj_core re2dj_warnings)

add_executable(re2dj_code_score
    src/tools/code_score/main.cpp
)
target_link_libraries(re2dj_code_score PRIVATE re2dj_core re2dj_warnings)

add_executable(re2dj_port_helper_scan
    src/tools/port_helper_scan/main.cpp
)
target_link_libraries(re2dj_port_helper_scan PRIVATE re2dj_core re2dj_warnings)

add_executable(re2dj_pe_loader
    src/tools/pe_loader/main.cpp
)
target_link_libraries(re2dj_pe_loader PRIVATE re2dj_core re2dj_warnings)

if(RE2DJ_BUILD_TESTS)
    enable_testing()
    add_executable(re2dj_unit_tests
        tests/unit/address_space_test.cpp
        tests/unit/code_region_score_test.cpp
        tests/unit/execution_backend_test.cpp
        tests/unit/import_dispatcher_test.cpp
        tests/unit/import_event_loop_test.cpp
        tests/unit/guest_module_registry_test.cpp
        tests/unit/guest_pe_facade_test.cpp
        tests/unit/kernel32_module_test.cpp
        tests/unit/user32_module_test.cpp
        tests/unit/ddraw_module_test.cpp
        tests/unit/dinput_module_test.cpp
        tests/unit/winmm_module_test.cpp
        tests/unit/gdi_raster_test.cpp
        tests/unit/dsound_module_test.cpp
        tests/unit/directx_display_test.cpp
        tests/unit/directx_device_test.cpp
        tests/unit/directx_input_test.cpp
        tests/unit/directx_surface_test.cpp
        tests/unit/version_test.cpp
        tests/unit/launcher_test.cpp
        tests/unit/launcher_update_test.cpp
        tests/unit/guest_process_test.cpp
        tests/unit/kernel32_crt_test.cpp
        tests/unit/api_call_record_test.cpp
        tests/unit/guest_files_test.cpp
        tests/unit/guest_file_prefetcher_test.cpp
        tests/unit/guest_file_read_journal_test.cpp
        tests/unit/logging_test.cpp
        tests/unit/ez2dj_io_board_test.cpp
        tests/unit/ez2dancer_io_board_test.cpp
        tests/unit/guest_path_test.cpp
        tests/unit/hardlock_material_config_test.cpp
        tests/unit/hardlock_handshake_response_test.cpp
        tests/unit/hardlock_protocol_test.cpp
        tests/unit/hardlock_api_descriptor_test.cpp
        tests/unit/hardlock_device_test.cpp
        tests/unit/hardlock_device_material_test.cpp
        tests/unit/hardlock_payload_responses_test.cpp
        tests/unit/hardlock_transform_responses_test.cpp
        tests/unit/hardlock_engine_test.cpp
        tests/unit/code_scan_test.cpp
        tests/unit/port_helper_scan_test.cpp
        tests/unit/immediate_scan_test.cpp
        tests/unit/hdd_root_test.cpp
        tests/unit/main.cpp
        tests/unit/lptdi_challenge_response_test.cpp
        tests/unit/bitmap_file_test.cpp
        tests/unit/lptdi_response_profile_test.cpp
        tests/unit/legacy_io_port_bus_test.cpp
        tests/unit/legacy_io_trap_test.cpp
        tests/unit/legacy_draw_command_test.cpp
        tests/unit/presentation_filter_test.cpp
        tests/unit/legacy_texture_test.cpp
        tests/unit/true_color_test.cpp
        tests/unit/legacy_transform_test.cpp
        tests/unit/legacy_vertex_buffer_test.cpp
        tests/unit/directsound_buffer_policy_test.cpp
        tests/unit/directsound_device_test.cpp
        tests/unit/legacy_audio_buffer_test.cpp
        tests/unit/mame_chd_test.cpp
        tests/unit/fat32_chd_test.cpp
        tests/unit/chd_hunk_cache_test.cpp
        tests/unit/fat32_directory_name_test.cpp
        tests/unit/chd_extract_name_test.cpp
        src/tools/chd_probe/chd_extract.cpp
        tests/unit/pe_image_test.cpp
        tests/unit/pe_loader_test.cpp
        tests/unit/synthetic_pe32.cpp
        tests/unit/temporary_tree.cpp
        tests/unit/present_interval_histogram_test.cpp
        tests/unit/post_shader_test.cpp
        tests/unit/post_shader_preset_test.cpp
        tests/unit/gl_renderer_identity_test.cpp
        tests/unit/present_sync_test.cpp
        tests/unit/window_policy_test.cpp
        tests/unit/ez2dj_keyboard_map_test.cpp
        tests/unit/gamepad_bindings_test.cpp
        tests/unit/guest_find_test.cpp
        tests/unit/target_profile_test.cpp
        tests/unit/vfs_file_table_test.cpp
    )
    target_include_directories(re2dj_unit_tests PRIVATE tests/unit src/tools/chd_probe)
    target_link_libraries(re2dj_unit_tests PRIVATE re2dj_core re2dj_logging re2dj_warnings)
    # Where the tests find repository files such as config/ examples.
    target_compile_definitions(re2dj_unit_tests PRIVATE RE2DJ_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME re2dj_unit_tests COMMAND re2dj_unit_tests)
    if(TARGET re2dj_linux_native_guest_module_probe)
        add_test(NAME re2dj_linux_native_guest_module_probe
                 COMMAND re2dj_linux_native_guest_module_probe)
    endif()
    if(TARGET re2dj_linux_native_in_process_probe)
        add_test(NAME re2dj_linux_native_in_process_probe
                 COMMAND re2dj_linux_native_in_process_probe)
    endif()
    if(TARGET re2dj_windows_native_in_process_probe)
        add_test(NAME re2dj_windows_native_in_process_probe
                 COMMAND re2dj_windows_native_in_process_probe)
    endif()
    if(TARGET re2dj_linux_compat_mode_probe)
        add_test(NAME re2dj_linux_compat_mode_probe
                 COMMAND re2dj_linux_compat_mode_probe)
    endif()

    # Run explicitly on a desktop with an OpenGL driver, not in headless CTest.
    if(TARGET re2dj_sdl3_opengl_backend)
        add_executable(re2dj_opengl_blend_probe tests/graphics/opengl_blend_probe.cpp)
        target_link_libraries(re2dj_opengl_blend_probe
            PRIVATE re2dj_sdl3_opengl_backend SDL3::SDL3-static re2dj_warnings
        )
        add_executable(re2dj_opengl_true_color_probe tests/graphics/opengl_true_color_probe.cpp)
        target_link_libraries(re2dj_opengl_true_color_probe
            PRIVATE re2dj_sdl3_opengl_backend SDL3::SDL3-static re2dj_warnings
        )
        add_executable(re2dj_opengl_post_shader_probe tests/graphics/opengl_post_shader_probe.cpp)
        target_link_libraries(re2dj_opengl_post_shader_probe
            PRIVATE re2dj_sdl3_opengl_backend SDL3::SDL3-static re2dj_warnings
        )
    endif()

    # Drives the gamepad reader with an SDL virtual joystick, so it needs no
    # hardware and no display.
    if(TARGET re2dj_sdl3_gamepad)
        add_executable(re2dj_sdl3_gamepad_test tests/unit/sdl3_gamepad_reader_test.cpp)
        target_include_directories(re2dj_sdl3_gamepad_test PRIVATE tests/unit)
        target_link_libraries(re2dj_sdl3_gamepad_test
            PRIVATE re2dj_sdl3_gamepad re2dj_core SDL3::SDL3-static re2dj_warnings
        )
        add_test(NAME re2dj_sdl3_gamepad_test COMMAND re2dj_sdl3_gamepad_test)
    endif()
endif()
