#ifndef RE2DJ_PLATFORM_SDL_HOST_PRESENTATION_H_
#define RE2DJ_PLATFORM_SDL_HOST_PRESENTATION_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "re2dj/graphics/color_depth.h"
#include "re2dj/graphics/present_sync.h"
#include "re2dj/graphics/window_policy.h"
#include "re2dj/hle/host_presentation.h"
#include "re2dj/input/pad_exit_chord.h"
#include "re2dj/input/pad_osd_chord.h"
#include "re2dj/input/sdl3_gamepad_reader.h"

namespace re2dj::graphics
{
class Sdl3OpenGlBackend;
}

namespace re2dj::ui
{
class Osd;
}

namespace re2dj::platform::sdl
{

// The in-process hosts' presentation (task 447): one SDL3/OpenGL window, made the first time
// the guest takes the display, titled like the Windows product's window, that
// the guest's frames are drawn into through the shared render backend. The
// window follows the shared window policy (graphics/window_policy.h): twice
// the display's size to begin with, Alt+1..3 for the scale, a double click
// or Alt+Enter for fullscreen, the shape kept or stretched (#14), and the
// frame rate in the title. Keys, mouse buttons and the
// pointer over the window become the guest's input state, and so do the
// gamepads SDL finds, read after every frame's event pump (task 444); leaving
// the window lets go of everything held, since this host sees keys only
// while focused.
//
// The window carries the same on-screen display as the Windows host's
// (ui/osd.h): backtick, or LT+RT+Y on a pad, shows and hides it, and neither
// reaches the guest; while it is shown the mouse buttons and the pads belong
// to it.
class SdlHostPresentation final : public hle::HostPresentation
{
public:
    SdlHostPresentation();
    ~SdlHostPresentation() override;

    SdlHostPresentation(const SdlHostPresentation&) = delete;
    SdlHostPresentation& operator=(const SdlHostPresentation&) = delete;

    // Whether the window opens in fullscreen, as --fullscreen or the profile
    // asks; set before the guest takes the display.
    void SetStartFullscreen(bool fullscreen) { fullscreen_ = fullscreen; }
    // Whether the display keeps its shape with bars or fills the window
    // (#14); set before the guest takes the display.
    void SetStartKeepAspect(bool keep_aspect) { keep_aspect_ = keep_aspect; }
    // Called with the fullscreen and keep-aspect state whenever the user
    // changes either in the window (the OSD, Alt+Enter, a double click), and
    // only then: not for the state the window opens with, nor for a change
    // the window refused. The CLI keeps them in cfg/re2dj.ini.
    void SetDisplayPreferencesObserver(std::function<void(bool fullscreen, bool keep_aspect)> observer)
    {
        display_observer_ = std::move(observer);
    }
    // The OSD's information lines, as the Windows host shows them; set before
    // the guest takes the display.
    void SetOsdInfoLines(std::vector<std::string> lines) { osd_info_lines_ = std::move(lines); }
    // The post-processing shader the window opens with (task 455), chosen by
    // the CLI; `none` by default.
    void SetPostShader(std::string id) { post_shader_ = std::move(id); }
    // When a present returns, chosen by the CLI's --vsync; the display's
    // retrace by default.
    void SetPresentSync(graphics::PresentSync value) { present_sync_ = value; }

    bool ShowGuestWindow(std::uint32_t guest_window,
                         std::uint32_t width,
                         std::uint32_t height,
                         std::string* error) override;

    void SetRetainBetweenFrames(bool retain) override;
    bool ClearTarget(std::uint16_t rgb565, std::string* error) override;
    bool ClearTargetColor(std::uint32_t xrgb, std::string* error) override;
    bool ReadTarget(std::uint32_t x,
                    std::uint32_t y,
                    std::uint32_t width,
                    std::uint32_t height,
                    std::span<std::uint8_t> pixels,
                    std::uint32_t pitch,
                    std::string* error) override;
    bool WriteTarget(std::uint32_t x,
                     std::uint32_t y,
                     std::uint32_t width,
                     std::uint32_t height,
                     std::span<const std::uint8_t> pixels,
                     std::uint32_t pitch,
                     std::string* error) override;
    bool Draw(const graphics::LegacyDrawCommand& command,
              const graphics::LegacyFixedFunctionState& state,
              std::uint32_t logical_width,
              std::uint32_t logical_height,
              const graphics::LegacyTextureView* texture,
              std::string* error) override;
    bool Present(std::string* error) override;
    void DiscardTexture(std::uint64_t identity) override;
    bool CloseRequested() const override { return close_requested_; }
    const hle::HostInputState& Input() const override { return input_; }
    // The primary display's desktop mode, read through SDL.
    bool DesktopDisplayMode(hle::HostDisplayMode* mode, std::string* error) const override;

    // True once a host window is open.
    bool opened() const { return backend_ != nullptr; }
    std::uint32_t guest_window() const { return guest_window_; }
    std::uint32_t scale() const { return scale_; }
    bool fullscreen() const { return fullscreen_; }
    bool keep_aspect() const { return keep_aspect_; }

    // Keeps the window on screen until the user closes it, so a run that has
    // stopped can still be looked at; returns at once when it was closed.
    void HoldUntilClosed();

private:
    // Acts on one window event Present took from the queue (an SDL_Event).
    void HandleEvent(const void* sdl_event);
    // Sizes the window for the current scale, or makes it fullscreen.
    bool ApplyWindowMode(std::string* error);
    // A new scale or fullscreen state, kept only when the window takes it;
    // false when it was undone.
    bool ChangeWindowMode(std::uint32_t scale, bool fullscreen);
    // A user's fullscreen or keep-aspect change, reported to the observer
    // when it took effect (#14).
    void ChangeFullscreenByUser(bool fullscreen);
    void ChangeKeepAspectByUser(bool keep_aspect);
    void NotifyDisplayPreferences();
    // The OSD's Fullscreen and Keep aspect ratio toggles, first in its list.
    void AddDisplayToggles();
    // Routes an event to the OSD; true when it belongs to the OSD and must
    // not reach the guest.
    bool HandleOsdEvent(const void* sdl_event);
    // Records the render target's colour depth when it differs from what was
    // last recorded, so a run's log says when 32-bit colour took effect.
    void ReportColorDepth();
    // Logs what the window's OpenGL context renders with (#6).
    void ReportRenderer();
    // Logs which post-processing shader the window opened with, or why the
    // one asked for is not applied.
    void ReportPostShader();

    // Declared before the backend, so the backend, which draws it, goes first.
    std::unique_ptr<ui::Osd> osd_;
    std::vector<std::string> osd_info_lines_;
    std::string post_shader_ = "none";
    graphics::PresentSync present_sync_ = graphics::PresentSync::kVerticalSync;
    std::unique_ptr<graphics::Sdl3OpenGlBackend> backend_;
    std::uint32_t guest_window_ = 0;
    std::uint32_t logical_width_ = 0;
    std::uint32_t logical_height_ = 0;
    std::uint32_t scale_ = graphics::kDefaultWindowScale;
    bool fullscreen_ = false;
    bool keep_aspect_ = true;
    // The OSD draws during the present, so its fullscreen choice waits here
    // until the present is over.
    bool fullscreen_request_pending_ = false;
    bool requested_fullscreen_ = false;
    std::function<void(bool fullscreen, bool keep_aspect)> display_observer_;
    bool close_requested_ = false;
    // LT+RT+L3+R3 held on one pad for a second (#20).
    input::PadExitChordTimer exit_chord_;
    // LT+RT+Y on one pad toggles the OSD, and the gate keeps the pads from the
    // guest while it shows (#22).
    input::PadChordEdge osd_chord_;
    input::PadGameGate pad_gate_;
    // Whether the backend's software pacing was already reported.
    bool pacing_reported_ = false;
    // The render target's colour depth last recorded, and whether the
    // driver's refusal of RGB8 was.
    graphics::ColorDepth reported_depth_ = graphics::ColorDepth::k16;
    bool depth_reported_ = false;
    bool true_color_refusal_reported_ = false;
    graphics::FrameRateMeter frame_rate_;
    hle::HostInputState input_;
    // The gamepads, started with the window; a start SDL refuses is logged
    // and leaves the keyboard alone.
    input::Sdl3GamepadReader gamepads_;
};

}  // namespace re2dj::platform::sdl

#endif  // RE2DJ_PLATFORM_SDL_HOST_PRESENTATION_H_
