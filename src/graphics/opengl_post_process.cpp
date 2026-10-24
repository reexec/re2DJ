#include "re2dj/graphics/opengl_post_process.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <array>
#include <filesystem>

namespace re2dj::graphics
{

struct OpenGlPostProcess::Impl
{
    using CreateShader = GLuint(APIENTRY*)(GLenum);
    using ShaderSource = void(APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
    using CompileShader = void(APIENTRY*)(GLuint);
    using GetShaderiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
    using GetShaderInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
    using DeleteShader = void(APIENTRY*)(GLuint);
    using CreateProgram = GLuint(APIENTRY*)();
    using AttachShader = void(APIENTRY*)(GLuint, GLuint);
    using BindAttribLocation = void(APIENTRY*)(GLuint, GLuint, const char*);
    using LinkProgram = void(APIENTRY*)(GLuint);
    using GetProgramiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
    using GetProgramInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
    using DeleteProgram = void(APIENTRY*)(GLuint);
    using UseProgram = void(APIENTRY*)(GLuint);
    using GetUniformLocation = GLint(APIENTRY*)(GLuint, const char*);
    using Uniform1i = void(APIENTRY*)(GLint, GLint);
    using Uniform1f = void(APIENTRY*)(GLint, GLfloat);
    using Uniform2f = void(APIENTRY*)(GLint, GLfloat, GLfloat);
    using UniformMatrix4fv = void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
    // Multi-pass chains (#27).
    using GenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
    using DeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);
    using BindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
    using FramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using CheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
    using GenTextures = void(APIENTRY*)(GLsizei, GLuint*);
    using DeleteTextures = void(APIENTRY*)(GLsizei, const GLuint*);
    using BindTexture = void(APIENTRY*)(GLenum, GLuint);
    using TexImage2D = void(APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    using TexParameteri = void(APIENTRY*)(GLenum, GLenum, GLint);
    using ActiveTexture = void(APIENTRY*)(GLenum);
    using Viewport = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
    using DrawArrays = void(APIENTRY*)(GLenum, GLint, GLsizei);
    using VertexAttribPointer = void(APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    using GetError = GLenum(APIENTRY*)();

    CreateShader create_shader = nullptr;
    ShaderSource shader_source = nullptr;
    CompileShader compile_shader = nullptr;
    GetShaderiv get_shader_iv = nullptr;
    GetShaderInfoLog get_shader_info_log = nullptr;
    DeleteShader delete_shader = nullptr;
    CreateProgram create_program = nullptr;
    AttachShader attach_shader = nullptr;
    BindAttribLocation bind_attrib_location = nullptr;
    LinkProgram link_program = nullptr;
    GetProgramiv get_program_iv = nullptr;
    GetProgramInfoLog get_program_info_log = nullptr;
    DeleteProgram delete_program = nullptr;
    UseProgram use_program = nullptr;
    GetUniformLocation get_uniform_location = nullptr;
    Uniform1i uniform_1i = nullptr;
    Uniform1f uniform_1f = nullptr;
    Uniform2f uniform_2f = nullptr;
    UniformMatrix4fv uniform_matrix_4fv = nullptr;
    GenFramebuffers gen_framebuffers = nullptr;
    DeleteFramebuffers delete_framebuffers = nullptr;
    BindFramebuffer bind_framebuffer = nullptr;
    FramebufferTexture2D framebuffer_texture_2d = nullptr;
    CheckFramebufferStatus check_framebuffer_status = nullptr;
    GenTextures gen_textures = nullptr;
    DeleteTextures delete_textures = nullptr;
    BindTexture bind_texture = nullptr;
    TexImage2D tex_image_2d = nullptr;
    TexParameteri tex_parameter_i = nullptr;
    ActiveTexture active_texture = nullptr;
    Viewport viewport = nullptr;
    DrawArrays draw_arrays = nullptr;
    VertexAttribPointer vertex_attrib_pointer = nullptr;
    GetError get_error = nullptr;
    // Whether the multi-pass entry points resolved; a single pass into the
    // window needs none of them.
    bool multipass = false;

    // One pass of the chain: its program, uniform slots, target and settings.
    struct Pass
    {
        PostShaderPass settings;
        GLuint program = 0;
        GLint mvp_matrix = -1;
        GLint texture = -1;
        GLint input_size = -1;
        GLint texture_size = -1;
        GLint output_size = -1;
        GLint frame_count = -1;
        GLint frame_direction = -1;
        GLint orig_texture = -1;
        GLint orig_texture_size = -1;
        GLint orig_input_size = -1;
        // PassPrev1 to PassPrev(index + 1): texture, its two sizes.
        std::vector<std::array<GLint, 3>> previous;
        std::vector<GLint> parameter_locations;
        // The target a pass not drawn into the window draws into.
        GLuint framebuffer = 0;
        GLuint color = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    bool initialized = false;
    std::filesystem::path directory;
    std::string directory_text;
    std::vector<PostShaderEntry> catalog;
    std::string active_id = kPostShaderNoneId;
    std::string last_error;
    std::vector<PostShaderParameter> parameters;
    std::vector<Pass> passes;
    // Draws the last pass's output into the window when that pass has a size
    // of its own.
    Pass stock;
    std::uint32_t frames = 0;
};

namespace
{

using Pass = OpenGlPostProcess::Impl::Pass;

constexpr GLenum kVertexShader = 0x8b31;
constexpr GLenum kFragmentShader = 0x8b30;
constexpr GLenum kCompileStatus = 0x8b81;
constexpr GLenum kLinkStatus = 0x8b82;
constexpr GLenum kInfoLogLength = 0x8b84;
constexpr GLenum kFramebuffer = 0x8d40;
constexpr GLenum kColorAttachment0 = 0x8ce0;
constexpr GLenum kFramebufferComplete = 0x8cd5;
constexpr GLenum kTexture0 = 0x84c0;
constexpr GLint kRgba16f = 0x881a;
constexpr GLint kRgba8 = 0x8058;
constexpr GLint kClampToEdge = 0x812f;

// The units a pass's inputs go on: Texture, OrigTexture, then PassPrevN.
constexpr GLint kTextureUnit = 0;
constexpr GLint kOrigUnit = 1;
constexpr GLint kFirstPreviousUnit = 2;

// Orthographic projection of the unit square onto clip space, column-major.
constexpr GLfloat kUnitSquareProjection[16] = {
    2.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 2.0f, 0.0f, 0.0f,
    0.0f, 0.0f, -1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 1.0f,
};

// The pass-through program for a chain whose last pass has its own size.
constexpr const char* kStockShader = R"glsl(
#if defined(VERTEX)
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
uniform mat4 MVPMatrix;
varying vec2 tex_coord;
void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    tex_coord = TexCoord.xy;
}
#elif defined(FRAGMENT)
uniform sampler2D Texture;
varying vec2 tex_coord;
void main()
{
    gl_FragColor = vec4(texture2D(Texture, tex_coord).rgb, 1.0);
}
#endif
)glsl";

// SDL_FunctionPointer rather than void*: C++ does not promise that a function
// pointer fits an object pointer. Some drivers return small sentinel values
// for entry points they lack, which count as missing.
template <typename Function>
bool ResolveOpenGlFunction(const char* name, Function* function)
{
    const SDL_FunctionPointer address = SDL_GL_GetProcAddress(name);
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    if (address == nullptr || value <= 3U || value == ~std::uintptr_t{0})
    {
        return false;
    }
    *function = reinterpret_cast<Function>(address);
    return true;
}

bool ResolveFunctions(OpenGlPostProcess::Impl* gl)
{
    return ResolveOpenGlFunction("glCreateShader", &gl->create_shader) &&
           ResolveOpenGlFunction("glShaderSource", &gl->shader_source) &&
           ResolveOpenGlFunction("glCompileShader", &gl->compile_shader) &&
           ResolveOpenGlFunction("glGetShaderiv", &gl->get_shader_iv) &&
           ResolveOpenGlFunction("glGetShaderInfoLog", &gl->get_shader_info_log) &&
           ResolveOpenGlFunction("glDeleteShader", &gl->delete_shader) &&
           ResolveOpenGlFunction("glCreateProgram", &gl->create_program) &&
           ResolveOpenGlFunction("glAttachShader", &gl->attach_shader) &&
           ResolveOpenGlFunction("glBindAttribLocation", &gl->bind_attrib_location) &&
           ResolveOpenGlFunction("glLinkProgram", &gl->link_program) &&
           ResolveOpenGlFunction("glGetProgramiv", &gl->get_program_iv) &&
           ResolveOpenGlFunction("glGetProgramInfoLog", &gl->get_program_info_log) &&
           ResolveOpenGlFunction("glDeleteProgram", &gl->delete_program) &&
           ResolveOpenGlFunction("glUseProgram", &gl->use_program) &&
           ResolveOpenGlFunction("glGetUniformLocation", &gl->get_uniform_location) &&
           ResolveOpenGlFunction("glUniform1i", &gl->uniform_1i) &&
           ResolveOpenGlFunction("glUniform1f", &gl->uniform_1f) &&
           ResolveOpenGlFunction("glUniform2f", &gl->uniform_2f) &&
           ResolveOpenGlFunction("glUniformMatrix4fv", &gl->uniform_matrix_4fv);
}

bool ResolveMultipassFunctions(OpenGlPostProcess::Impl* gl)
{
    return ResolveOpenGlFunction("glGenFramebuffers", &gl->gen_framebuffers) &&
           ResolveOpenGlFunction("glDeleteFramebuffers", &gl->delete_framebuffers) &&
           ResolveOpenGlFunction("glBindFramebuffer", &gl->bind_framebuffer) &&
           ResolveOpenGlFunction("glFramebufferTexture2D", &gl->framebuffer_texture_2d) &&
           ResolveOpenGlFunction("glCheckFramebufferStatus", &gl->check_framebuffer_status) &&
           ResolveOpenGlFunction("glGenTextures", &gl->gen_textures) &&
           ResolveOpenGlFunction("glDeleteTextures", &gl->delete_textures) &&
           ResolveOpenGlFunction("glBindTexture", &gl->bind_texture) &&
           ResolveOpenGlFunction("glTexImage2D", &gl->tex_image_2d) &&
           ResolveOpenGlFunction("glTexParameteri", &gl->tex_parameter_i) &&
           ResolveOpenGlFunction("glActiveTexture", &gl->active_texture) &&
           ResolveOpenGlFunction("glViewport", &gl->viewport) &&
           ResolveOpenGlFunction("glDrawArrays", &gl->draw_arrays) &&
           ResolveOpenGlFunction("glVertexAttribPointer", &gl->vertex_attrib_pointer) &&
           ResolveOpenGlFunction("glGetError", &gl->get_error);
}

std::string TrimLog(std::string log)
{
    while (!log.empty() && (log.back() == '\0' || log.back() == '\n' || log.back() == '\r'))
    {
        log.pop_back();
    }
    return log;
}

std::string ShaderLog(OpenGlPostProcess::Impl* gl, GLuint shader)
{
    GLint length = 0;
    gl->get_shader_iv(shader, kInfoLogLength, &length);
    std::string log(length > 0 ? static_cast<std::size_t>(length) : 0U, '\0');
    if (length > 0)
    {
        gl->get_shader_info_log(shader, length, nullptr, log.data());
    }
    return TrimLog(std::move(log));
}

std::string ProgramLog(OpenGlPostProcess::Impl* gl, GLuint program)
{
    GLint length = 0;
    gl->get_program_iv(program, kInfoLogLength, &length);
    std::string log(length > 0 ? static_cast<std::size_t>(length) : 0U, '\0');
    if (length > 0)
    {
        gl->get_program_info_log(program, length, nullptr, log.data());
    }
    return TrimLog(std::move(log));
}

GLuint CompileStage(OpenGlPostProcess::Impl* gl, GLenum type, const std::string& source, std::string* error)
{
    const GLuint shader = gl->create_shader(type);
    if (shader == 0)
    {
        *error = "cannot create an OpenGL shader";
        return 0;
    }
    const char* text = source.c_str();
    gl->shader_source(shader, 1, &text, nullptr);
    gl->compile_shader(shader);
    GLint compiled = GL_FALSE;
    gl->get_shader_iv(shader, kCompileStatus, &compiled);
    if (compiled != GL_TRUE)
    {
        *error = std::string(type == kVertexShader ? "vertex" : "fragment") +
                 " stage failed to compile: " + ShaderLog(gl, shader);
        gl->delete_shader(shader);
        return 0;
    }
    return shader;
}

// Links the two stages with the attribute slots the backend's arrays use.
// Zero, with `error` set, when the link fails.
GLuint LinkProgram(OpenGlPostProcess::Impl* gl, GLuint vertex, GLuint fragment, std::string* error)
{
    const GLuint program = gl->create_program();
    if (program == 0)
    {
        *error = "cannot create an OpenGL program";
        return 0;
    }
    gl->attach_shader(program, vertex);
    gl->attach_shader(program, fragment);
    gl->bind_attrib_location(program, OpenGlPostProcess::kVertexCoordSlot, "VertexCoord");
    gl->bind_attrib_location(program, OpenGlPostProcess::kColorSlot, "COLOR");
    gl->bind_attrib_location(program, OpenGlPostProcess::kTexCoordSlot, "TexCoord");
    gl->link_program(program);
    GLint linked = GL_FALSE;
    gl->get_program_iv(program, kLinkStatus, &linked);
    if (linked != GL_TRUE)
    {
        *error = "link failed: " + ProgramLog(gl, program);
        gl->delete_program(program);
        return 0;
    }
    return program;
}

// Compiles one pass's text into a program and finds its uniforms. `index` is
// its place in the chain, which bounds the PassPrevN it can read.
bool BuildPass(OpenGlPostProcess::Impl* gl, std::size_t index, Pass* pass,
               std::vector<PostShaderParameter>* parameters, std::string* error)
{
    PostShaderProgramSource source;
    if (!BuildPostShaderProgramSource(pass->settings.text, &source, error))
    {
        *error = pass->settings.path + ": " + *error;
        return false;
    }
    const GLuint vertex = CompileStage(gl, kVertexShader, source.vertex, error);
    const GLuint fragment = vertex == 0 ? 0 : CompileStage(gl, kFragmentShader, source.fragment, error);
    if (vertex != 0 && fragment != 0)
    {
        pass->program = LinkProgram(gl, vertex, fragment, error);
    }
    if (vertex != 0)
    {
        gl->delete_shader(vertex);
    }
    if (fragment != 0)
    {
        gl->delete_shader(fragment);
    }
    if (pass->program == 0)
    {
        *error = pass->settings.path + ": " + *error;
        return false;
    }
    const auto uniform = [gl, pass](const std::string& name) { return gl->get_uniform_location(pass->program, name.c_str()); };
    pass->mvp_matrix = uniform("MVPMatrix");
    pass->texture = uniform("Texture");
    pass->input_size = uniform("InputSize");
    pass->texture_size = uniform("TextureSize");
    pass->output_size = uniform("OutputSize");
    pass->frame_count = uniform("FrameCount");
    pass->frame_direction = uniform("FrameDirection");
    pass->orig_texture = uniform("OrigTexture");
    pass->orig_texture_size = uniform("OrigTextureSize");
    pass->orig_input_size = uniform("OrigInputSize");
    for (std::size_t back = 1; back <= index + 1; ++back)
    {
        const std::string prefix = "PassPrev" + std::to_string(back);
        pass->previous.push_back({uniform(prefix + "Texture"), uniform(prefix + "TextureSize"), uniform(prefix + "InputSize")});
    }
    // Parameters are shared by name across the chain: the first pass to
    // declare one gives its range.
    for (PostShaderParameter& parameter : source.parameters)
    {
        bool known = false;
        for (const PostShaderParameter& existing : *parameters)
        {
            known = known || existing.name == parameter.name;
        }
        if (!known)
        {
            parameters->push_back(std::move(parameter));
        }
    }
    return true;
}

void ReleasePass(OpenGlPostProcess::Impl* gl, Pass* pass)
{
    if (pass->program != 0)
    {
        gl->delete_program(pass->program);
    }
    if (pass->framebuffer != 0)
    {
        gl->delete_framebuffers(1, &pass->framebuffer);
    }
    if (pass->color != 0)
    {
        gl->delete_textures(1, &pass->color);
    }
    *pass = Pass{};
}

void ReleaseProgram(OpenGlPostProcess::Impl* gl)
{
    for (Pass& pass : gl->passes)
    {
        ReleasePass(gl, &pass);
    }
    gl->passes.clear();
    ReleasePass(gl, &gl->stock);
    gl->parameters.clear();
    gl->active_id = kPostShaderNoneId;
}

void SetFilter(OpenGlPostProcess::Impl* gl, GLuint texture, bool linear)
{
    const GLint filter = linear ? static_cast<GLint>(GL_LINEAR) : static_cast<GLint>(GL_NEAREST);
    gl->bind_texture(GL_TEXTURE_2D, texture);
    gl->tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl->tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

// Gives a pass a target of width by height, made again when the size changes.
bool EnsureTarget(OpenGlPostProcess::Impl* gl, Pass* pass, std::uint32_t width, std::uint32_t height, std::string* error)
{
    if (pass->framebuffer != 0 && pass->width == width && pass->height == height)
    {
        return true;
    }
    if (pass->color == 0)
    {
        gl->gen_textures(1, &pass->color);
    }
    if (pass->framebuffer == 0)
    {
        gl->gen_framebuffers(1, &pass->framebuffer);
    }
    gl->bind_texture(GL_TEXTURE_2D, pass->color);
    gl->tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
    gl->tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
    gl->tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->tex_parameter_i(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->tex_image_2d(GL_TEXTURE_2D, 0, pass->settings.float_framebuffer ? kRgba16f : kRgba8,
                     static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA,
                     pass->settings.float_framebuffer ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    gl->bind_framebuffer(kFramebuffer, pass->framebuffer);
    gl->framebuffer_texture_2d(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, pass->color, 0);
    if (gl->check_framebuffer_status(kFramebuffer) != kFramebufferComplete)
    {
        *error = pass->settings.path + ": cannot draw into a " + std::to_string(width) + "x" + std::to_string(height) +
                 (pass->settings.float_framebuffer ? " floating-point" : "") + " target";
        return false;
    }
    pass->width = width;
    pass->height = height;
    return true;
}

// Makes a pass current and uploads its uniforms. Its Texture is already on
// unit 0.
void SetUniforms(OpenGlPostProcess::Impl* gl, const Pass& pass, std::uint32_t input_width, std::uint32_t input_height,
                 std::uint32_t output_width, std::uint32_t output_height, std::uint32_t orig_width,
                 std::uint32_t orig_height, const std::vector<std::array<std::uint32_t, 2>>& previous_sizes)
{
    gl->use_program(pass.program);
    if (pass.mvp_matrix >= 0)
    {
        gl->uniform_matrix_4fv(pass.mvp_matrix, 1, GL_FALSE, kUnitSquareProjection);
    }
    const auto size = [gl](GLint location, std::uint32_t width, std::uint32_t height)
    {
        if (location >= 0)
        {
            gl->uniform_2f(location, static_cast<GLfloat>(width), static_cast<GLfloat>(height));
        }
    };
    if (pass.texture >= 0)
    {
        gl->uniform_1i(pass.texture, kTextureUnit);
    }
    // Every texture here is exactly its content's size, so the texture size
    // and the content size are the same, as libretro means them.
    size(pass.input_size, input_width, input_height);
    size(pass.texture_size, input_width, input_height);
    size(pass.output_size, output_width, output_height);
    if (pass.orig_texture >= 0)
    {
        gl->uniform_1i(pass.orig_texture, kOrigUnit);
    }
    size(pass.orig_texture_size, orig_width, orig_height);
    size(pass.orig_input_size, orig_width, orig_height);
    for (std::size_t back = 0; back < pass.previous.size() && back < previous_sizes.size(); ++back)
    {
        if (pass.previous[back][0] >= 0)
        {
            gl->uniform_1i(pass.previous[back][0], kFirstPreviousUnit + static_cast<GLint>(back));
        }
        size(pass.previous[back][1], previous_sizes[back][0], previous_sizes[back][1]);
        size(pass.previous[back][2], previous_sizes[back][0], previous_sizes[back][1]);
    }
    if (pass.frame_count >= 0)
    {
        gl->uniform_1i(pass.frame_count, static_cast<GLint>(gl->frames));
    }
    if (pass.frame_direction >= 0)
    {
        gl->uniform_1i(pass.frame_direction, 1);
    }
    for (std::size_t index = 0; index < pass.parameter_locations.size(); ++index)
    {
        if (pass.parameter_locations[index] >= 0)
        {
            gl->uniform_1f(pass.parameter_locations[index], gl->parameters[index].value);
        }
    }
}

// The unit square, drawn into whatever target and viewport are current.
void DrawUnitSquare(OpenGlPostProcess::Impl* gl)
{
    // x, y, z, w; r, g, b, a; s, t. The square's y runs up, as the textures'
    // rows do.
    static constexpr GLfloat kVertices[4][10] = {
        {0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f},
    };
    constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(kVertices[0]));
    gl->vertex_attrib_pointer(OpenGlPostProcess::kVertexCoordSlot, 4, GL_FLOAT, GL_FALSE, kStride, &kVertices[0][0]);
    gl->vertex_attrib_pointer(OpenGlPostProcess::kColorSlot, 4, GL_FLOAT, GL_FALSE, kStride, &kVertices[0][4]);
    gl->vertex_attrib_pointer(OpenGlPostProcess::kTexCoordSlot, 2, GL_FLOAT, GL_FALSE, kStride, &kVertices[0][8]);
    gl->draw_arrays(GL_TRIANGLE_STRIP, 0, 4);
}

std::filesystem::path ExecutableDirectory()
{
    const char* base = SDL_GetBasePath();
    return base != nullptr ? std::filesystem::path(base) : std::filesystem::path();
}

}  // namespace

OpenGlPostProcess::OpenGlPostProcess() : impl_(std::make_unique<Impl>()) {}

OpenGlPostProcess::~OpenGlPostProcess() = default;

bool OpenGlPostProcess::Initialize(std::string* message)
{
    Impl* gl = impl_.get();
    if (gl->initialized)
    {
        return true;
    }
    if (!ResolveFunctions(gl))
    {
        if (message != nullptr)
        {
            *message = "OpenGL shader entry points are unavailable";
        }
        return false;
    }
    gl->multipass = ResolveMultipassFunctions(gl);
    gl->directory = ResolvePostShaderDirectory(ExecutableDirectory());
    gl->directory_text = gl->directory.string();
    gl->catalog = ListPostShaders(gl->directory);
    gl->initialized = true;
    return true;
}

void OpenGlPostProcess::Shutdown()
{
    Impl* gl = impl_.get();
    if (!gl->initialized)
    {
        return;
    }
    ReleaseProgram(gl);
    gl->initialized = false;
}

bool OpenGlPostProcess::active() const
{
    return !impl_->passes.empty();
}

const std::vector<PostShaderEntry>& OpenGlPostProcess::catalog() const
{
    return impl_->catalog;
}

const std::string& OpenGlPostProcess::shader_directory() const
{
    return impl_->directory_text;
}

const std::string& OpenGlPostProcess::active_id() const
{
    return impl_->active_id;
}

const std::string& OpenGlPostProcess::last_error() const
{
    return impl_->last_error;
}

std::vector<PostShaderParameter>& OpenGlPostProcess::parameters()
{
    return impl_->parameters;
}

std::size_t OpenGlPostProcess::pass_count() const
{
    return impl_->passes.size();
}

bool OpenGlPostProcess::Select(std::string_view id)
{
    Impl* gl = impl_.get();
    gl->last_error.clear();
    if (!gl->initialized)
    {
        gl->last_error = "post-processing is not initialized";
        return false;
    }
    ReleaseProgram(gl);
    if (id.empty() || id == kPostShaderNoneId)
    {
        return true;
    }

    const PostShaderEntry* entry = FindPostShader(gl->catalog, id);
    PostShaderPreset preset;
    if (entry == nullptr)
    {
        gl->last_error = "no shader named '" + std::string(id) + "' in " + gl->directory_text + " or built in";
        return false;
    }
    if (!LoadPostShaderPreset(*entry, &preset, &gl->last_error))
    {
        return false;
    }
    const bool chained = preset.passes.size() > 1 || !preset.passes.front().to_window;
    if (chained && !gl->multipass)
    {
        gl->last_error = "this OpenGL has no framebuffer objects for a multi-pass shader";
        return false;
    }
    std::vector<Pass> passes(preset.passes.size());
    std::vector<PostShaderParameter> parameters;
    bool built = true;
    for (std::size_t index = 0; built && index < passes.size(); ++index)
    {
        passes[index].settings = std::move(preset.passes[index]);
        built = BuildPass(gl, index, &passes[index], &parameters, &gl->last_error);
    }
    if (built && !passes.back().settings.to_window)
    {
        gl->stock.settings = SinglePassPreset("stock", kStockShader).passes.front();
        built = BuildPass(gl, passes.size(), &gl->stock, &parameters, &gl->last_error);
    }
    gl->passes = std::move(passes);
    if (!built)
    {
        const std::string error = gl->last_error;
        ReleaseProgram(gl);
        gl->last_error = error;
        return false;
    }
    gl->parameters = std::move(parameters);
    const auto locate = [gl](Pass* pass)
    {
        if (pass->program == 0)
        {
            return;
        }
        for (const PostShaderParameter& parameter : gl->parameters)
        {
            pass->parameter_locations.push_back(gl->get_uniform_location(pass->program, parameter.name.c_str()));
        }
    };
    for (Pass& pass : gl->passes)
    {
        locate(&pass);
    }
    locate(&gl->stock);
    gl->active_id = std::string(id);
    gl->frames = 0;
    return true;
}

bool OpenGlPostProcess::Reload()
{
    Impl* gl = impl_.get();
    if (!gl->initialized)
    {
        return false;
    }
    const std::string id = gl->active_id;
    gl->catalog = ListPostShaders(gl->directory);
    return Select(id);
}

bool OpenGlPostProcess::UseForPresent(std::uint32_t source_texture,
                                      std::uint32_t input_width,
                                      std::uint32_t input_height,
                                      std::int32_t viewport_x,
                                      std::int32_t viewport_y,
                                      std::uint32_t output_width,
                                      std::uint32_t output_height)
{
    Impl* gl = impl_.get();
    if (gl->passes.empty())
    {
        return false;
    }
    // A single pass into the window draws the caller's quad straight away,
    // the texture it bound on unit 0 being its input, and its picture and
    // PassPrev1 too: both name that same texture, so they read unit 0 rather
    // than units nothing is bound on.
    if (gl->passes.size() == 1 && gl->passes.front().settings.to_window)
    {
        const Pass& pass = gl->passes.front();
        if (pass.settings.filter_specified && gl->multipass)
        {
            SetFilter(gl, source_texture, pass.settings.filter_linear);
        }
        SetUniforms(gl, pass, input_width, input_height, output_width, output_height, input_width, input_height,
                    {{input_width, input_height}});
        if (pass.orig_texture >= 0)
        {
            gl->uniform_1i(pass.orig_texture, kTextureUnit);
        }
        if (!pass.previous.empty() && pass.previous.front()[0] >= 0)
        {
            gl->uniform_1i(pass.previous.front()[0], kTextureUnit);
        }
        ++gl->frames;
        return true;
    }

    // Each pass's output, newest last, beginning with the game's picture.
    std::vector<GLuint> outputs = {static_cast<GLuint>(source_texture)};
    std::vector<std::array<std::uint32_t, 2>> sizes = {{input_width, input_height}};
    // OrigTexture is read with the first pass's filter.
    gl->active_texture(kTexture0 + kOrigUnit);
    SetFilter(gl, outputs.front(), gl->passes.front().settings.filter_linear);
    std::string error;
    const std::size_t count = gl->passes.size();
    for (std::size_t index = 0; index <= count; ++index)
    {
        const bool stock = index == count;
        if (stock && gl->stock.program == 0)
        {
            break;
        }
        Pass& pass = stock ? gl->stock : gl->passes[index];
        const std::uint32_t pass_input_width = sizes.back()[0];
        const std::uint32_t pass_input_height = sizes.back()[1];
        std::uint32_t width = output_width;
        std::uint32_t height = output_height;
        const bool to_window = stock || pass.settings.to_window;
        if (!to_window)
        {
            PostShaderPassSize(pass.settings, pass_input_width, pass_input_height, output_width, output_height, &width,
                               &height);
            if (!EnsureTarget(gl, &pass, width, height, &error))
            {
                // Back to what the caller set up: the window target and
                // viewport, its texture on unit 0, and no GL error left for
                // its own check to trip on. Its program is its own to make
                // current again.
                gl->bind_framebuffer(kFramebuffer, 0);
                gl->active_texture(kTexture0);
                gl->bind_texture(GL_TEXTURE_2D, source_texture);
                gl->viewport(viewport_x, viewport_y, static_cast<GLsizei>(output_width),
                             static_cast<GLsizei>(output_height));
                ReleaseProgram(gl);
                gl->use_program(0);
                while (gl->get_error() != GL_NO_ERROR)
                {
                }
                gl->last_error = error;
                return false;
            }
        }
        // Inputs: Texture on unit 0 with this pass's filter, OrigTexture on
        // unit 1, PassPrevN after them.
        // back == outputs.size() is the picture itself, PassPrev(i + 1).
        for (std::size_t back = 1; back <= outputs.size() && back <= pass.previous.size(); ++back)
        {
            if (pass.previous[back - 1][0] >= 0)
            {
                gl->active_texture(kTexture0 + kFirstPreviousUnit + static_cast<GLenum>(back - 1));
                gl->bind_texture(GL_TEXTURE_2D, outputs[outputs.size() - back]);
            }
        }
        gl->active_texture(kTexture0 + kOrigUnit);
        gl->bind_texture(GL_TEXTURE_2D, outputs.front());
        gl->active_texture(kTexture0);
        SetFilter(gl, outputs.back(), stock || pass.settings.filter_linear);
        // PassPrevN of pass i: N = 1 is its input, N = i + 1 the picture.
        std::vector<std::array<std::uint32_t, 2>> previous_sizes;
        for (std::size_t back = 1; back <= sizes.size(); ++back)
        {
            previous_sizes.push_back(sizes[sizes.size() - back]);
        }
        SetUniforms(gl, pass, pass_input_width, pass_input_height, width, height, input_width, input_height,
                    previous_sizes);
        if (to_window)
        {
            // The caller draws the last quad into the window.
            gl->bind_framebuffer(kFramebuffer, 0);
            gl->viewport(viewport_x, viewport_y, static_cast<GLsizei>(output_width), static_cast<GLsizei>(output_height));
            ++gl->frames;
            return true;
        }
        gl->bind_framebuffer(kFramebuffer, pass.framebuffer);
        gl->viewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        DrawUnitSquare(gl);
        outputs.push_back(pass.color);
        sizes.push_back({width, height});
    }
    return false;
}

}  // namespace re2dj::graphics
