#include "re2dj/platform/sdl/host_presentation.h"

#include <cmath>
#include <optional>

#include <SDL3/SDL.h>
#include <spdlog/logger.h>

#include "../native/game_controls.h"
#include "host_keyboard.h"
#include "re2dj/graphics/sdl3_opengl_backend.h"
#include "re2dj/input/gamepad.h"
#include "re2dj/input/pad_exit_chord.h"
#include "re2dj/input/pad_osd_chord.h"
#include "re2dj/input/virtual_keys.h"
#include "re2dj/logging/logging.h"
#include "re2dj/ui/display_controls.h"
#include "re2dj/ui/osd.h"
#include "re2dj/version.h"

namespace re2dj::platform::sdl
{
namespace
{

namespace input = re2dj::input;

// SDL's left, middle, right buttons as the guest's left, right, middle, with
// their virtual keys.
void SetMouseButton(hle::HostInputState* state, std::uint8_t button, bool down)
{
    int index = -1;
    int virtual_key = 0;
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        index = 0;
        virtual_key = input::kVkLButton;
        break;
    case SDL_BUTTON_RIGHT:
        index = 1;
        virtual_key = input::kVkRButton;
        break;
    case SDL_BUTTON_MIDDLE:
        index = 2;
        virtual_key = input::kVkMButton;
        break;
    default:
        return;
    }
    state->mouse_buttons[static_cast<std::size_t>(index)] = down;
    state->virtual_keys.set(static_cast<std::size_t>(virtual_key), down);
}

}  // namespace


SdlHostPresentation::SdlHostPresentation() = default;

SdlHostPresentation::~SdlHostPresentation() = default;

bool SdlHostPresentation::ShowGuestWindow(std::uint32_t guest_window,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            std::string* error)
{
    if (backend_ != nullptr)
    {
        // One host window stands for the guest's; a second window or size
        // is not modelled yet.
        if (guest_window != guest_window_)
        {
            *error = "the host presents one guest window";
            return false;
        }
        return true;
    }
    auto backend = std::make_unique<graphics::Sdl3OpenGlBackend>();
    const std::string title = WindowTitle(VersionString(), 0.0);
    graphics::Sdl3OpenGlWindowConfig config;
    // The render target has the display's own size; the window around it is
    // sized by the window policy once it is open.
    config.width = width;
    config.height = height;
    config.title = title.c_str();
    config.resizable = true;
    config.centered = true;
    config.post_shader = post_shader_.c_str();
    config.present_sync = present_sync_;
    // Nothing is drawn yet, so the first frame is the guest's cleared screen.
    if (!backend->Initialize(config, error))
    {
        return false;
    }
    backend_ = std::move(backend);
    backend_->SetKeepAspect(keep_aspect_);
    guest_window_ = guest_window;
    logical_width_ = width;
    logical_height_ = height;
    backend_->SetEventObserver([this](const void* sdl_event) { HandleEvent(sdl_event); });
    if (osd_ == nullptr)
    {
        osd_ = std::make_unique<ui::Osd>();
        osd_->SetInfoLines(osd_info_lines_);
        // The window's own controls first (#14), then, in the Windows
        // host's order, the game's own controls and the colour depth every
        // run offers.
        AddDisplayToggles();
        native::AddGameControls(osd_.get());
        ui::AddColorDepthToggle(osd_.get());
    }
    backend_->SetPresentOverlay(osd_.get());
    osd_->SetRendererIdentity(backend_->renderer_identity());
    ReportRenderer();
    osd_->SetPostShaderControl(backend_->post_shader_control());
    ReportPostShader();
    if (!ApplyWindowMode(error) || !backend_->ClearRenderTarget(0, error) || !backend_->Present(error))
    {
        backend_.reset();
        return false;
    }
    ReportColorDepth();
    // The gamepads, from here on read after every frame; a refusal keeps the
    // keyboard working.
    std::string gamepad_error;
    const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
    if (!gamepads_.Initialize(&gamepad_error))
    {
        if (logger != nullptr)
        {
            logger->warn("input: no gamepads: {}", gamepad_error);
        }
    }
    else if (logger != nullptr)
    {
        logger->info("input: gamepads ready, {} connected", gamepads_.open_count());
    }
    error->clear();
    return true;
}

void SdlHostPresentation::ReportRenderer()
{
    const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
    if (logger == nullptr)
    {
        return;
    }
    const graphics::GlRendererIdentity identity = backend_->renderer_identity();
    logger->info("presentation: GL renderer: {} | vendor: {} | version: {} | video driver: {}{}",
                 identity.renderer, identity.vendor, identity.version, identity.video_driver,
                 identity.software ? " | software rendering" : "");
}

void SdlHostPresentation::ReportPostShader()
{
    const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
    if (logger == nullptr)
    {
        return;
    }
    graphics::PostShaderControl* control = backend_->post_shader_control();
    if (control == nullptr)
    {
        if (post_shader_ != graphics::kPostShaderNoneId)
        {
            logger->warn("presentation: post shader '{}' not applied: the driver lacks the shader entry points",
                         post_shader_);
        }
        return;
    }
    if (control->active_id() != graphics::kPostShaderNoneId)
    {
        logger->info("presentation: post shader {} ({} parameters)", control->active_id(),
                     control->parameters().size());
    }
    else if (post_shader_ != graphics::kPostShaderNoneId && !post_shader_.empty())
    {
        logger->warn("presentation: post shader '{}' not applied: {}", post_shader_, control->last_error());
    }
}

void SdlHostPresentation::ReportColorDepth()
{
    if (backend_ == nullptr)
    {
        return;
    }
    const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
    const graphics::ColorDepth depth = backend_->render_target_depth();
    if (!depth_reported_ || depth != reported_depth_)
    {
        depth_reported_ = true;
        reported_depth_ = depth;
        if (logger != nullptr)
        {
            logger->info("presentation: {}-bit colour", graphics::ColorDepthName(depth));
        }
    }
    if (!true_color_refusal_reported_ && backend_->true_color_unavailable())
    {
        true_color_refusal_reported_ = true;
        if (logger != nullptr)
        {
            logger->warn("presentation: the OpenGL driver cannot render into RGB8, so colour stays 16-bit");
        }
    }
}

bool SdlHostPresentation::HandleOsdEvent(const void* sdl_event)
{
    if (osd_ == nullptr)
    {
        return false;
    }
    const auto* event = static_cast<const SDL_Event*>(sdl_event);
    switch (event->type)
    {
    case SDL_EVENT_KEY_DOWN:
        if (event->key.scancode != SDL_SCANCODE_GRAVE)
        {
            return false;
        }
        // Holding the key repeats it; only the first press toggles.
        if (!event->key.repeat)
        {
            osd_->ToggleVisible();
        }
        return true;
    case SDL_EVENT_KEY_UP:
        // The rest of the backtick keystroke belongs to the toggle as well.
        return event->key.scancode == SDL_SCANCODE_GRAVE;
    case SDL_EVENT_MOUSE_MOTION:
    {
        // The OSD draws in window pixels, which SDL's window coordinates are
        // scaled by on a high-density display. The guest keeps the pointer.
        SDL_Window* window = SDL_GetWindowFromEvent(event);
        const float density = window == nullptr ? 1.0f : SDL_GetWindowPixelDensity(window);
        osd_->QueueMousePosition(event->motion.x * density, event->motion.y * density);
        return false;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        const int button = event->button.button == SDL_BUTTON_LEFT    ? 0
                           : event->button.button == SDL_BUTTON_RIGHT ? 1
                                                                      : -1;
        if (button >= 0)
        {
            osd_->QueueMouseButton(button, event->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        return osd_->visible();
    }
    default:
        return false;
    }
}

bool SdlHostPresentation::DesktopDisplayMode(hle::HostDisplayMode* mode, std::string* error) const
{
    return graphics::Sdl3OpenGlBackend::QueryDesktopDisplayMode(
        &mode->width, &mode->height, &mode->bits_per_pixel, &mode->refresh_hz, error);
}

bool SdlHostPresentation::ApplyWindowMode(std::string* error)
{
    if (fullscreen_)
    {
        return backend_->SetFullscreen(true, error);
    }
    return backend_->SetFullscreen(false, error) &&
           backend_->ResizeWindow(logical_width_ * scale_, logical_height_ * scale_, error);
}

bool SdlHostPresentation::ChangeWindowMode(std::uint32_t scale, bool fullscreen)
{
    const std::uint32_t previous_scale = scale_;
    const bool previous_fullscreen = fullscreen_;
    scale_ = scale;
    fullscreen_ = fullscreen;
    std::string error;
    if (!ApplyWindowMode(&error))
    {
        // As the Windows host does, a mode the window cannot take is undone.
        scale_ = previous_scale;
        fullscreen_ = previous_fullscreen;
        ApplyWindowMode(&error);
        return false;
    }
    return true;
}

void SdlHostPresentation::ChangeFullscreenByUser(bool fullscreen)
{
    if (fullscreen != fullscreen_ && ChangeWindowMode(scale_, fullscreen))
    {
        NotifyDisplayPreferences();
    }
}

void SdlHostPresentation::ChangeKeepAspectByUser(bool keep_aspect)
{
    if (keep_aspect == keep_aspect_)
    {
        return;
    }
    keep_aspect_ = keep_aspect;
    if (backend_ != nullptr)
    {
        backend_->SetKeepAspect(keep_aspect);
    }
    NotifyDisplayPreferences();
}

void SdlHostPresentation::NotifyDisplayPreferences()
{
    if (display_observer_)
    {
        display_observer_(fullscreen_, keep_aspect_);
    }
}

void SdlHostPresentation::AddDisplayToggles()
{
    ui::OsdToggle fullscreen;
    fullscreen.label = "Fullscreen";
    fullscreen.context = this;
    fullscreen.read = [](void* context, bool* value) {
        const auto* self = static_cast<const SdlHostPresentation*>(context);
        *value = self->fullscreen_request_pending_ ? self->requested_fullscreen_ : self->fullscreen_;
        return true;
    };
    // The OSD draws inside the present, where the window must not change;
    // Present applies the request once it is over.
    fullscreen.write = [](void* context, bool value) {
        auto* self = static_cast<SdlHostPresentation*>(context);
        self->fullscreen_request_pending_ = true;
        self->requested_fullscreen_ = value;
        return true;
    };
    osd_->AddToggle(fullscreen);

    ui::OsdToggle keep_aspect;
    keep_aspect.label = "Keep aspect ratio";
    keep_aspect.context = this;
    keep_aspect.read = [](void* context, bool* value) {
        *value = static_cast<const SdlHostPresentation*>(context)->keep_aspect_;
        return true;
    };
    keep_aspect.write = [](void* context, bool value) {
        static_cast<SdlHostPresentation*>(context)->ChangeKeepAspectByUser(value);
        return true;
    };
    osd_->AddToggle(keep_aspect);
}

void SdlHostPresentation::HandleEvent(const void* sdl_event)
{
    // A pad coming or going is the reader's, and worth a line in the log.
    std::string gamepad_name;
    bool gamepad_added = false;
    if (gamepads_.HandleEvent(sdl_event, &gamepad_name, &gamepad_added))
    {
        const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
        if (logger != nullptr)
        {
            logger->info("input: gamepad {}: {} ({} connected)",
                         gamepad_added ? "added" : "removed",
                         gamepad_name.empty() ? "unnamed" : gamepad_name,
                         gamepads_.open_count());
        }
        return;
    }
    if (HandleOsdEvent(sdl_event))
    {
        return;
    }
    const auto* event = static_cast<const SDL_Event*>(sdl_event);
    switch (event->type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        close_requested_ = true;
        break;
    case SDL_EVENT_KEY_UP:
        SetHostKey(&input_, event->key.scancode, false);
        break;
    case SDL_EVENT_KEY_DOWN:
    {
        // Alt+Enter switches fullscreen (#14). Its Enter is kept from the
        // guest: EZ2DJ's default mapping makes Enter the 2P turntable.
        if ((event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) &&
            (event->key.mod & SDL_KMOD_ALT) != 0)
        {
            if (!event->key.repeat)
            {
                ChangeFullscreenByUser(!fullscreen_);
            }
            break;
        }
        // Held for the guest whatever else the key does, as Windows'
        // GetAsyncKeyState also sees the keys of a window shortcut.
        SetHostKey(&input_, event->key.scancode, true);
        if (event->key.repeat || (event->key.mod & SDL_KMOD_ALT) == 0)
        {
            break;
        }
        std::uint32_t scale = 0;
        switch (event->key.key)
        {
        case SDLK_1:
        case SDLK_KP_1:
            scale = 1;
            break;
        case SDLK_2:
        case SDLK_KP_2:
            scale = 2;
            break;
        case SDLK_3:
        case SDLK_KP_3:
            scale = 3;
            break;
        default:
            break;
        }
        if (graphics::IsWindowScale(scale))
        {
            // In fullscreen the scale waits until the window returns.
            if (fullscreen_)
            {
                scale_ = scale;
            }
            else
            {
                ChangeWindowMode(scale, false);
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        SetMouseButton(&input_, event->button.button, true);
        // Every second click of a run is a double click, as Windows reports
        // WM_LBUTTONDBLCLK.
        if (event->button.button == SDL_BUTTON_LEFT && event->button.clicks >= 2 && event->button.clicks % 2 == 0)
        {
            ChangeFullscreenByUser(!fullscreen_);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        SetMouseButton(&input_, event->button.button, false);
        break;
    case SDL_EVENT_MOUSE_MOTION:
    {
        int window_width = 0;
        int window_height = 0;
        SDL_Window* window = SDL_GetWindowFromEvent(event);
        if (window != nullptr && SDL_GetWindowSize(window, &window_width, &window_height))
        {
            // The pointer over the drawn display, in the display's own units;
            // over the bars it lies outside it, as over a window's frame.
            const graphics::PresentRect fit = graphics::ComputePresentRect(
                window_width, window_height, logical_width_, logical_height_, keep_aspect_);
            if (fit.width > 0 && fit.height > 0)
            {
                input_.cursor_window = guest_window_;
                input_.cursor_x = static_cast<std::int32_t>(
                    std::floor((event->motion.x - static_cast<float>(fit.x)) * static_cast<float>(logical_width_) /
                               static_cast<float>(fit.width)));
                input_.cursor_y = static_cast<std::int32_t>(
                    std::floor((event->motion.y - static_cast<float>(fit.y)) * static_cast<float>(logical_height_) /
                               static_cast<float>(fit.height)));
            }
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    {
        const std::uint32_t window = input_.cursor_window;
        const std::int32_t x = input_.cursor_x;
        const std::int32_t y = input_.cursor_y;
        input_ = {};
        input_.cursor_window = window;
        input_.cursor_x = x;
        input_.cursor_y = y;
        break;
    }
    default:
        break;
    }
}

void SdlHostPresentation::SetRetainBetweenFrames(bool retain)
{
    if (backend_ != nullptr)
    {
        backend_->SetRetainBetweenFrames(retain);
    }
}

bool SdlHostPresentation::ClearTarget(std::uint16_t rgb565, std::string* error)
{
    if (backend_ == nullptr)
    {
        *error = "no host window to clear";
        return false;
    }
    return backend_->ClearRenderTarget(rgb565, error);
}

bool SdlHostPresentation::ClearTargetColor(std::uint32_t xrgb, std::string* error)
{
    if (backend_ == nullptr)
    {
        *error = "no host window to clear";
        return false;
    }
    return backend_->ClearRenderTargetColor(xrgb, error);
}

bool SdlHostPresentation::ReadTarget(std::uint32_t x,
                                       std::uint32_t y,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::span<std::uint8_t> pixels,
                                       std::uint32_t pitch,
                                       std::string* error)
{
    if (backend_ == nullptr)
    {
        *error = "no host window to read";
        return false;
    }
    return backend_->ReadRenderTarget(x, y, width, height, pixels, pitch, error);
}

bool SdlHostPresentation::WriteTarget(std::uint32_t x,
                                        std::uint32_t y,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        std::span<const std::uint8_t> pixels,
                                        std::uint32_t pitch,
                                        std::string* error)
{
    if (backend_ == nullptr)
    {
        *error = "no host window to write";
        return false;
    }
    return backend_->WriteRenderTarget(x, y, width, height, pixels, pitch, error);
}

bool SdlHostPresentation::Draw(const graphics::LegacyDrawCommand& command,
                                 const graphics::LegacyFixedFunctionState& state,
                                 std::uint32_t logical_width,
                                 std::uint32_t logical_height,
                                 const graphics::LegacyTextureView* texture,
                                 std::string* error)
{
    if (backend_ == nullptr)
    {
        *error = "no host window to draw into";
        return false;
    }
    return backend_->Draw(command, state, logical_width, logical_height, texture, error);
}

bool SdlHostPresentation::Present(std::string* error)
{
    if (backend_ == nullptr)
    {
        *error = "no host window to present";
        return false;
    }
    if (!backend_->Present(error))
    {
        return false;
    }
    if (fullscreen_request_pending_)
    {
        fullscreen_request_pending_ = false;
        ChangeFullscreenByUser(requested_fullscreen_);
    }
    // The pump inside Present brought SDL's pad state up to date.
    const std::vector<input::GamepadControls> pads = gamepads_.ReadEach();
    input::GamepadControls held;
    for (const input::GamepadControls& pad : pads)
    {
        held |= pad;
    }
    // LT+RT+Y on one pad shows or hides the OSD (#22). While it shows, the
    // pads drive it and the guest sees none of them; what is held as it
    // closes stays hidden from the guest until let go.
    if (osd_ != nullptr && osd_chord_.Update(input::IsPadOsdChordDown(pads)))
    {
        osd_->ToggleVisible();
        const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
        if (logger != nullptr)
        {
            logger->info("input: gamepad OSD chord (LT+RT+Y): OSD {}", osd_->visible() ? "shown" : "hidden");
        }
    }
    const bool osd_shown = osd_ != nullptr && osd_->visible();
    pad_gate_.SetSuppressed(osd_shown, held);
    input_.gamepad = pad_gate_.Apply(held);
    if (osd_shown)
    {
        osd_->SetGamepad(held);
    }
    // LT+RT+L3+R3 held on one pad for a second closes the game as the
    // window's close button does (#20).
    if (exit_chord_.Update(input::IsPadExitChordDown(pads), SDL_GetTicks()) && !close_requested_)
    {
        close_requested_ = true;
        const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
        if (logger != nullptr)
        {
            logger->info("input: gamepad exit chord (LT+RT+L3+R3) held for 1 s");
        }
    }
    ReportColorDepth();
    if (!pacing_reported_ && backend_->software_pacing_engaged())
    {
        pacing_reported_ = true;
        const std::shared_ptr<spdlog::logger> logger = logging::GetLogger();
        if (logger != nullptr)
        {
            logger->info("presentation: the swap does not block, so presents are paced at the display rate");
        }
    }
    const std::optional<double> rate = frame_rate_.Present(SDL_GetPerformanceCounter(), SDL_GetPerformanceFrequency());
    if (rate.has_value())
    {
        backend_->SetTitle(WindowTitle(VersionString(), *rate).c_str());
    }
    return true;
}

void SdlHostPresentation::DiscardTexture(std::uint64_t identity)
{
    if (backend_ != nullptr)
    {
        backend_->DiscardTexture(identity);
    }
}

void SdlHostPresentation::HoldUntilClosed()
{
    if (backend_ == nullptr)
    {
        return;
    }
    // Waits on the events directly, as no Present runs any more to hand
    // them to the observer.
    SDL_Event event = {};
    while (!close_requested_)
    {
        if (!SDL_WaitEventTimeout(&event, 250))
        {
            continue;
        }
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            return;
        }
    }
}

}  // namespace re2dj::platform::sdl
