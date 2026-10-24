# #26 설계: 내장 화면 셰이더 `fsr`(AMD FSR 1 EASU)

이슈: [#26](https://github.com/reexec/re2DJ/issues/26) · 바탕: [작업 455 화면 후처리 셰이더](20261005-455-post-process-shaders.md)

## 배경

게임은 640×480을 그리고, 후처리 단계가 그것을 창 크기로 키웁니다. 기본 `none`은 선형 보간이라 4K 전체 화면(4.5배)에서 글자와 로고 경계가 번집니다. DLSS처럼 여러 프레임을 모으는 업스케일러는 모션 벡터와 깊이가 필요해 2D 게임에 쓸 수 없고 NVIDIA 전용입니다. 프레임 하나만 보는 AMD FSR 1의 업스케일 단계(EASU)는 그런 정보 없이 단일 pass로 돌 수 있고 MIT 라이선스입니다.

## 바꾸는 것

* `src/graphics/post_shaders/fsr.glsl`: EASU를 libretro 형식의 GLSL 1.20 단일 pass로 옮깁니다. `TextureSize`(논리 해상도)로 출력 화소 중심의 원본 위치를 구하고, 주변 12화소의 luma로 네 사분면의 경계 방향·길이를 bilinear 가중으로 모읍니다. 그 방향으로 늘인 Lanczos2 근사 커널로 12화소를 섞은 뒤, 가까운 네 화소의 최소·최대로 잘라 고리 잡티를 막습니다. 원본 `ffx_fsr1.h`의 MIT 고지를 파일 머리에 둡니다. 두 번째 단계(RCAS 선명화)는 다단계 pass가 필요해 넣지 않습니다.
* 내장 목록: CMake가 `fsr.glsl`을 읽어 `kPostShaderBuiltinFsr`로 넣고, 카탈로그가 `crt`·`scanline` 다음에 `fsr`를 둡니다. OSD와 런처의 셰이더 목록에 그대로 나옵니다.
* `THIRD_PARTY_NOTICES.md`, 도움말, README, 가이드.

## 검증

1. 단위 테스트 `post_shader_test.cpp`: 내장 셋의 순서, `fsr`가 파싱되고 매개변수가 없음.
2. `re2dj_opengl_post_shader_probe` 12개, Windows x86 Debug·Release, WSL Linux x64·x86·clang.
3. 실제 실행: `--post-shader fsr`로 로그에 적용이 남고, 4K 전체 화면에서 `none`과 같은 장면을 찍어 비교합니다. vsync off 처리량으로 비용을 잽니다.

---

# #26 Design: The Built-in Screen Shader `fsr` (AMD FSR 1 EASU)

Issue: [#26](https://github.com/reexec/re2DJ/issues/26) · Basis: [task 455 post-processing shaders](20261005-455-post-process-shaders.md)

## Background

The game draws 640×480 and the post-processing stage scales it to the window; the default `none` is linear, so fullscreen at 4K (4.5x) text and logo edges blur. Upscalers that gather frames, like DLSS, need motion vectors and depth, which a 2D game lacks, and are NVIDIA-only. AMD FSR 1's upscaling pass (EASU) looks at one frame, runs as a single pass without such data, and is MIT-licensed.

## Changes

* `src/graphics/post_shaders/fsr.glsl`: EASU ported to a single libretro-style GLSL 1.20 pass. From `TextureSize` (the logical resolution) it finds each output pixel centre's source position, gathers the edge direction and length of the four quadrants from the 12 surrounding pixels' luma with bilinear weights, blends the 12 pixels with a Lanczos2 approximation stretched along that direction, and clamps to the nearest four pixels' minimum and maximum so it does not ring. `ffx_fsr1.h`'s MIT notice heads the file. The second pass (RCAS sharpening) needs multiple passes and is left out.
* The built-ins: CMake reads `fsr.glsl` into `kPostShaderBuiltinFsr`, and the catalog puts `fsr` after `crt` and `scanline`, so it appears in the OSD's and the launcher's shader lists.
* `THIRD_PARTY_NOTICES.md`, the help, README and the guide.

## Verification

1. Unit test `post_shader_test.cpp`: the three built-ins in order, `fsr` parsing with no parameters.
2. `re2dj_opengl_post_shader_probe` (12 checks), Windows x86 Debug and Release, WSL Linux x64, x86 and clang.
3. Real runs: `--post-shader fsr` logs it applied; fullscreen at 4K the same scene is captured under `none` and `fsr` and compared; vsync-off throughput measures the cost.
