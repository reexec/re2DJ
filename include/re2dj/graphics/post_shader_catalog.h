#ifndef RE2DJ_GRAPHICS_POST_SHADER_CATALOG_H_
#define RE2DJ_GRAPHICS_POST_SHADER_CATALOG_H_

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "re2dj/graphics/post_shader_preset.h"

namespace re2dj::graphics
{

// The post-processing shaders a run can choose from (task 455): the ones built
// into the executable and the `.glsl` files and `.glslp` presets (#27) in the
// shader directory. Neither
// the command line nor the OSD needs GL to list them, so this module has none.

// The id that selects no pass at all, and the environment variable read when
// the command line names no shader.
inline constexpr const char* kPostShaderNoneId = "none";
inline constexpr const char* kPostShaderVariable = "RE2DJ_POST_SHADER";

struct PostShaderEntry
{
    // Built-ins are a bare name (`crt`); files are their file name with its
    // extension (`my_crt.glsl`, `my_chain.glslp`), so the two can never collide.
    std::string id;
    bool builtin = false;
    // Empty for a built-in.
    std::filesystem::path path;
};

// `shaders/` under the working directory when it exists, otherwise under
// `executable_directory` when that exists, otherwise the working-directory
// candidate.
[[nodiscard]] std::filesystem::path ResolvePostShaderDirectory(
    const std::filesystem::path& executable_directory);

// Built-ins first in a fixed order, then the directory's regular `.glsl` and
// `.glslp` files sorted by name. A missing or unreadable directory lists built-ins only.
[[nodiscard]] std::vector<PostShaderEntry> ListPostShaders(
    const std::filesystem::path& directory);

// The entry with `id`, or null. `none` is not an entry.
[[nodiscard]] const PostShaderEntry* FindPostShader(
    const std::vector<PostShaderEntry>& entries, std::string_view id);

// The shader's passes: a `.glsl` file or built-in as one pass into the window,
// a `.glslp` preset with its shaders read beside it (embedded ones for a
// built-in). False, with `error` set, when a file cannot be read or the preset
// does not parse.
[[nodiscard]] bool LoadPostShaderPreset(const PostShaderEntry& entry,
                                        PostShaderPreset* preset, std::string* error);

// The shader a run starts with: the command line's id when it gave one,
// otherwise the environment variable's when it is set and not empty,
// otherwise `none`. Whether the id names a shader is decided when the window
// opens and the directory is listed.
[[nodiscard]] std::string ChoosePostShader(const std::string* command_line_id,
                                           const char* environment_value);

}  // namespace re2dj::graphics

#endif  // RE2DJ_GRAPHICS_POST_SHADER_CATALOG_H_
