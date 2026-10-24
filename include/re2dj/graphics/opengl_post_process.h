#ifndef RE2DJ_GRAPHICS_OPENGL_POST_PROCESS_H_
#define RE2DJ_GRAPHICS_OPENGL_POST_PROCESS_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "re2dj/graphics/post_shader_control.h"

namespace re2dj::graphics
{

// The presentation-only post-processing pass of the SDL3/OpenGL backend
// (task 455). The guest draws into a render target at its logical size, and
// Present draws that target into the window as one quad; with a shader
// selected, that quad is drawn through the shader instead. Nothing the guest
// can read changes, nothing is copied, and with no shader selected the pass
// issues no GL call.
//
// Every method runs on the presenting thread with the backend's OpenGL
// context current, except the destructor, which touches no GL so that it is
// safe after the context is gone; call Shutdown while it is still current.
class OpenGlPostProcess final : public PostShaderControl
{
public:
    // Attribute slots the program binds before linking. They are the slots
    // the backend's own program uses for position, colour and texture
    // coordinates, so Present's vertex arrays feed either program.
    static constexpr std::uint32_t kVertexCoordSlot = 0;
    static constexpr std::uint32_t kColorSlot = 1;
    static constexpr std::uint32_t kTexCoordSlot = 2;

    OpenGlPostProcess();
    ~OpenGlPostProcess() override;

    OpenGlPostProcess(const OpenGlPostProcess&) = delete;
    OpenGlPostProcess& operator=(const OpenGlPostProcess&) = delete;

    // Resolves the GL entry points and lists the shaders. False leaves the
    // pass inert and `message` says why.
    bool Initialize(std::string* message);
    void Shutdown();

    // Whether a shader is selected, so Present draws through it.
    bool active() const;

    // Runs the chain for one present. `source_texture` is the guest's picture,
    // bound on unit 0 by the caller, at `input_*` (its logical size, also
    // reported as TextureSize); the picture lands in the window viewport at
    // `viewport_*` of `output_*` pixels. Every pass but the last is drawn
    // here into a target of its own (#27); on return the last pass's program
    // is current with its input on unit 0 and the window viewport set, and
    // the caller draws the unit square with VertexCoord, COLOR and TexCoord
    // in the slots above, then makes its own program current again. False,
    // issuing no GL call, when no shader is selected; also false, the shader
    // dropped and last_error set, when a pass's target cannot be made, with
    // the window target, the viewport and the caller's texture on unit 0
    // restored and no program current, so the caller makes its own current
    // before drawing.
    bool UseForPresent(std::uint32_t source_texture,
                       std::uint32_t input_width,
                       std::uint32_t input_height,
                       std::int32_t viewport_x,
                       std::int32_t viewport_y,
                       std::uint32_t output_width,
                       std::uint32_t output_height);
    // How many passes the selected shader has; 0 with none selected.
    std::size_t pass_count() const;

    const std::vector<PostShaderEntry>& catalog() const override;
    const std::string& shader_directory() const override;
    const std::string& active_id() const override;
    const std::string& last_error() const override;
    bool Select(std::string_view id) override;
    bool Reload() override;
    std::vector<PostShaderParameter>& parameters() override;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace re2dj::graphics

#endif  // RE2DJ_GRAPHICS_OPENGL_POST_PROCESS_H_
