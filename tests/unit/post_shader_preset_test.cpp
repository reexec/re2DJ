#include "re2dj/graphics/post_shader_preset.h"

#include <cstdint>
#include <map>
#include <string>

#include "test_support.h"

namespace
{

using re2dj::graphics::ParsePostShaderPreset;
using re2dj::graphics::PostShaderFileReader;
using re2dj::graphics::PostShaderPass;
using re2dj::graphics::PostShaderPassSize;
using re2dj::graphics::PostShaderPreset;
using re2dj::graphics::PostShaderScaleType;
using re2dj::graphics::SinglePassPreset;

// Files a preset may name, by relative path.
PostShaderFileReader ReaderOf(std::map<std::string, std::string> files)
{
    return [files = std::move(files)](const std::string& path, std::string* text, std::string* error)
    {
        const auto found = files.find(path);
        if (found == files.end())
        {
            *error = "no file " + path;
            return false;
        }
        *text = found->second;
        return true;
    };
}

// Keys are read with comments and quotes; a pass with no scale keeps its
// input's size, an unscaled last pass draws into the window.
void CheckParse(re2dj::test::Context& context)
{
    const char* kText =
        "shaders = \"3\"   # three passes\n"
        "shader0 = a.glsl\n"
        "scale_type0 = source\n"
        "scale0 = 2.0\n"
        "float_framebuffer0 = true\n"
        "filter_linear0 = false\n"
        "shader1 = sub/b.glsl\n"
        "scale_type_x1 = absolute\n"
        "scale_x1 = 320\n"
        "scale_type_y1 = viewport\n"
        "scale_y1 = 0.5\n"
        "shader2 = c.glsl\n"
        "filter_linear2 = true\n";
    PostShaderPreset preset;
    std::string error;
    RE2DJ_CHECK(context, ParsePostShaderPreset(kText, ReaderOf({{"a.glsl", "A"}, {"sub/b.glsl", "B"}, {"c.glsl", "C"}}),
                                               &preset, &error));
    RE2DJ_CHECK_EQ(context, preset.passes.size(), std::size_t{3});
    if (preset.passes.size() != 3)
    {
        return;
    }
    const PostShaderPass& a = preset.passes[0];
    RE2DJ_CHECK_EQ(context, a.text, std::string("A"));
    RE2DJ_CHECK(context, a.scale_type_x == PostShaderScaleType::kSource && a.scale_type_y == PostShaderScaleType::kSource);
    RE2DJ_CHECK_EQ(context, a.scale_x, 2.0f);
    RE2DJ_CHECK(context, a.float_framebuffer && !a.filter_linear && !a.to_window);
    const PostShaderPass& b = preset.passes[1];
    RE2DJ_CHECK_EQ(context, b.path, std::string("sub/b.glsl"));
    RE2DJ_CHECK(context, b.scale_type_x == PostShaderScaleType::kAbsolute && b.scale_type_y == PostShaderScaleType::kViewport);
    RE2DJ_CHECK_EQ(context, b.scale_x, 320.0f);
    RE2DJ_CHECK_EQ(context, b.scale_y, 0.5f);
    RE2DJ_CHECK(context, !b.float_framebuffer && b.filter_linear && !b.to_window);
    const PostShaderPass& c = preset.passes[2];
    RE2DJ_CHECK(context, c.filter_linear && c.to_window);

    // Sizes from a 640x480 input shown at 2880x2160.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    PostShaderPassSize(a, 640, 480, 2880, 2160, &width, &height);
    RE2DJ_CHECK(context, width == 1280 && height == 960);
    PostShaderPassSize(b, 1280, 960, 2880, 2160, &width, &height);
    RE2DJ_CHECK(context, width == 320 && height == 1080);
    PostShaderPassSize(c, 320, 1080, 2880, 2160, &width, &height);
    RE2DJ_CHECK(context, width == 2880 && height == 2160);
}

// What is refused: no count, a count out of range, a missing shader line, a
// file the reader lacks, an unknown scale type, a scale that is not a
// positive number, a flag that is not true or false. An unscaled middle pass
// keeps its input's size rather than drawing into the window.
void CheckErrors(re2dj::test::Context& context)
{
    const PostShaderFileReader reader = ReaderOf({{"a.glsl", "A"}, {"b.glsl", "B"}});
    PostShaderPreset preset;
    std::string error;
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shader0 = a.glsl\n", reader, &preset, &error));
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shaders = 0\n", reader, &preset, &error));
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shaders = 17\n", reader, &preset, &error));
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shaders = 2\nshader0 = a.glsl\n", reader, &preset, &error));
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shaders = 1\nshader0 = missing.glsl\n", reader, &preset, &error));
    RE2DJ_CHECK(context, error.find("missing.glsl") != std::string::npos);
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shaders = 1\nshader0 = a.glsl\nscale_type0 = window\n", reader, &preset, &error));
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shaders = 1\nshader0 = a.glsl\nscale0 = -1\n", reader, &preset, &error));
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shaders = 1\nshader0 = a.glsl\nscale0 = two\n", reader, &preset, &error));
    RE2DJ_CHECK(context, !ParsePostShaderPreset("shaders = 1\nshader0 = a.glsl\nfloat_framebuffer0 = yes\n", reader, &preset, &error));
    // A scale without a scale type is ignored, as libretro reads it.
    RE2DJ_CHECK(context, ParsePostShaderPreset("shaders = 2\nshader0 = a.glsl\nshader1 = b.glsl\nscale1 = 3\n", reader, &preset, &error));
    RE2DJ_CHECK(context, preset.passes.size() == 2 && !preset.passes[0].to_window && preset.passes[1].to_window);
    RE2DJ_CHECK(context, ParsePostShaderPreset("shaders = 2\nshader0 = a.glsl\nshader1 = b.glsl\nscale_type1 = source\nscale1 = 3\n", reader, &preset, &error));
    RE2DJ_CHECK(context, preset.passes.size() == 2 && !preset.passes[0].to_window && !preset.passes[1].to_window);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    PostShaderPassSize(preset.passes[0], 640, 480, 1280, 960, &width, &height);
    RE2DJ_CHECK(context, width == 640 && height == 480);
    PostShaderPassSize(preset.passes[1], 640, 480, 1280, 960, &width, &height);
    RE2DJ_CHECK(context, width == 1920 && height == 1440);
}

// libretro's quirks: a '#' inside a quoted value is not a comment, a UTF-8
// BOM is skipped, and the unsuffixed scale keys win over their _x/_y forms.
void CheckLibretroRules(re2dj::test::Context& context)
{
    const PostShaderFileReader reader = ReaderOf({{"pass#1.glsl", "P"}, {"a.glsl", "A"}});
    PostShaderPreset preset;
    std::string error;
    RE2DJ_CHECK(context, ParsePostShaderPreset("shaders = 1\nshader0 = \"pass#1.glsl\" # the file\n", reader, &preset, &error));
    RE2DJ_CHECK(context, preset.passes.size() == 1 && preset.passes[0].text == "P");
    RE2DJ_CHECK(context, ParsePostShaderPreset("\xEF\xBB\xBFshaders = 1\nshader0 = a.glsl\n", reader, &preset, &error));
    RE2DJ_CHECK(context, preset.passes.size() == 1 && preset.passes[0].text == "A");
    RE2DJ_CHECK(context, ParsePostShaderPreset("shaders = 1\nshader0 = a.glsl\nscale_type_x0 = absolute\nscale_type0 = source\n"
                                               "scale_x0 = 4.0\nscale0 = 2.0\nfilter_linear0 = false\n",
                                               reader, &preset, &error));
    RE2DJ_CHECK(context, preset.passes.size() == 1 && preset.passes[0].scale_type_x == PostShaderScaleType::kSource &&
                             preset.passes[0].scale_x == 2.0f && preset.passes[0].scale_y == 2.0f);
    RE2DJ_CHECK(context, preset.passes[0].filter_specified && !preset.passes[0].filter_linear);
    RE2DJ_CHECK(context, ParsePostShaderPreset("shaders = 1\nshader0 = a.glsl\n", reader, &preset, &error));
    RE2DJ_CHECK(context, preset.passes.size() == 1 && !preset.passes[0].filter_specified);
}

// A single file is one pass into the window; a size never falls below one
// pixel.
void CheckSinglePass(re2dj::test::Context& context)
{
    const PostShaderPreset preset = SinglePassPreset("my.glsl", "text");
    RE2DJ_CHECK_EQ(context, preset.passes.size(), std::size_t{1});
    RE2DJ_CHECK(context, preset.passes[0].to_window && preset.passes[0].text == "text");
    PostShaderPass tiny;
    tiny.scale_type_x = PostShaderScaleType::kSource;
    tiny.scale_x = 0.001f;
    tiny.scale_y = 0.001f;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    PostShaderPassSize(tiny, 640, 480, 0, 0, &width, &height);
    RE2DJ_CHECK(context, width == 1 && height == 1);
    PostShaderPassSize(preset.passes[0], 640, 480, 0, 0, &width, &height);
    RE2DJ_CHECK(context, width == 1 && height == 1);
}

}  // namespace

void RunPostShaderPresetTests(re2dj::test::Context& context)
{
    CheckParse(context);
    CheckErrors(context);
    CheckLibretroRules(context);
    CheckSinglePass(context);
}
