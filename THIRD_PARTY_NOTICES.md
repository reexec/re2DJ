# 서드파티 고지 / Third-Party Notices

`third_party/libchdr/`에는 MAME CHD 읽기를 위해 vendoring한 `libchdr`와 허용된 codec dependency가 있습니다. 공용 graphics build와 Windows x86 audio build는 아래 소스를 CMake FetchContent로 고정 commit에서 내려받으며 source distribution의 license notice를 그대로 유지합니다.

`AGENTS.md`의 라이선스 정책에 따라 GPL, LGPL, AGPL 같은 전염성 라이선스 코드는 도입하지 않습니다.

*`third_party/libchdr/` vendors `libchdr` and its permissively licensed codec dependencies for MAME CHD reads. The shared graphics build and Windows x86 audio build fetch the sources below from pinned commits through CMake FetchContent and preserve each source distribution's license notice. Per the license policy in `AGENTS.md`, copyleft-licensed code such as GPL, LGPL, or AGPL is not introduced.*

| 이름 / Name | 버전 / Version | 라이선스 / License | 경로 / Path |
| --- | --- | --- | --- |
| spdlog | 1.15.3 | MIT | [upstream license](https://github.com/gabime/spdlog/blob/v1.15.3/LICENSE) |
| SDL | 3.4.14 (`147a8ee3`) | zlib | [upstream license](https://github.com/libsdl-org/SDL/blob/release-3.4.14/LICENSE.txt) |
| SDL_mixer | 3.2.4 (`72a81869`) | zlib | [upstream license](https://github.com/libsdl-org/SDL_mixer/blob/release-3.2.4/LICENSE.txt) |
| Dear ImGui | 1.92.9 (`01380c57`) | MIT | [upstream license](https://github.com/ocornut/imgui/blob/v1.92.9/LICENSE.txt) |
| libchdr | current master snapshot (`rtissera/libchdr`) | BSD-3-Clause; codec dependencies listed below | [upstream license](https://github.com/rtissera/libchdr/blob/master/LICENSE.txt) |
| LZMA decoder (libchdr dependency) | 26.02 | Public domain | [`third_party/libchdr/deps/lzma-26.02/LICENSE`](third_party/libchdr/deps/lzma-26.02/LICENSE) |
| miniz (libchdr dependency; also the launcher update's zip and inflate reader) | 3.1.2 | Public domain / Unlicense terms in source | [`third_party/libchdr/deps/miniz-3.1.2/miniz.h`](third_party/libchdr/deps/miniz-3.1.2/miniz.h) |
| Zstandard decoder (libchdr dependency) | 1.5.7 | BSD-3-Clause terms selected from the upstream dual-license notice | [`third_party/libchdr/deps/zstd-1.5.7/zstddeclib.c`](third_party/libchdr/deps/zstd-1.5.7/zstddeclib.c) |
| AMD FidelityFX Super Resolution 1.0 (EASU and RCAS algorithms, ported into the built-in `fsr` screen shader, #26, #27) | 1.0 | MIT | [`src/graphics/post_shaders/fsr/`](src/graphics/post_shaders/fsr/) (notice in each file), [upstream license](https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/license.txt) |
| ScaleFX by Sp00kyFox (the five passes from libretro glsl-shaders, the built-in `scalefx` screen shader, #27) | 2017-03-01 | MIT | [`src/graphics/post_shaders/scalefx/`](src/graphics/post_shaders/scalefx/) (notice in each file), [upstream](https://github.com/libretro/glsl-shaders/tree/master/edge-smoothing/scalefx) |
| Anime4K by bloc97 (Upscale CNN x2 S weights, converted into the built-in `anime4k` screen shader, #27) | 3.2 | MIT | [`src/graphics/post_shaders/anime4k/`](src/graphics/post_shaders/anime4k/) (notice in each file), [upstream license](https://github.com/bloc97/Anime4K/blob/master/LICENSE) |
| GNU Unifont (ASCII glyphs) | 15.1.05 | SIL OFL 1.1 terms selected from the upstream OFL-1.1 / GPL-2.0+ dual license; glyph data only | [`third_party/unifont/`](third_party/unifont/README.md) |
| dr_flac (libchdr dependency) | 0.13.3 | Public domain / MIT-0 terms in source | [`third_party/libchdr/include/dr_libs/dr_flac.h`](third_party/libchdr/include/dr_libs/dr_flac.h) |
| Galmuri (Galmuri14, Galmuri11 Bold, GalmuriMono11; project site font) | 2.40.3 woff2 files | SIL OFL 1.1, Reserved Font Name "Galmuri" | [`docs/sites/static/fonts/OFL.txt`](docs/sites/static/fonts/OFL.txt) |
| markdown-it-py (site build only) | 4.2.0 | MIT | [upstream license](https://github.com/executablebooks/markdown-it-py/blob/v4.2.0/LICENSE) |
| mdit-py-plugins (site build only) | 0.6.1 | MIT | [upstream license](https://github.com/executablebooks/mdit-py-plugins/blob/v0.6.1/LICENSE) |
| mdurl (site build only) | 0.1.2 | MIT | [upstream license](https://github.com/executablebooks/mdurl/blob/master/LICENSE) |
| Jinja2 (site build only) | 3.1.6 | BSD-3-Clause | [upstream license](https://github.com/pallets/jinja/blob/3.1.6/LICENSE.txt) |
| MarkupSafe (site build only) | 3.0.3 | BSD-3-Clause | [upstream license](https://github.com/pallets/markupsafe/blob/3.0.3/LICENSE.txt) |
