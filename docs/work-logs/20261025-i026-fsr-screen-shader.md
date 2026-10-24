# #26 작업 로그 — 내장 화면 셰이더 `fsr` / #26 work log — the built-in screen shader `fsr`

설계: [20261025-i026-fsr-screen-shader.md](../design/20261025-i026-fsr-screen-shader.md) · 지시서: [20261025-i026-fsr-screen-shader.md](../work-orders/20261025-i026-fsr-screen-shader.md)

## 2026-10-25

- 검토: DLSS 2 이후는 모션 벡터·깊이·jitter가 필요하고 NVIDIA 전용이라 2D 게임에 맞지 않습니다. 단일 프레임 공간 업스케일러(FSR 1, NIS), 픽셀아트용(xBR, ScaleFX), 애니메이션 그림용 ML(Anime4K)을 후보로 두었고, 지금의 단일 pass 파이프라인에 바로 들어가는 FSR 1 EASU부터 넣었습니다.
- 구현: `fsr.glsl`(EASU 단일 pass, AMD MIT 고지), CMake 내장(`kPostShaderBuiltinFsr`), 카탈로그 `fsr`, 도움말·README·가이드·THIRD_PARTY_NOTICES, `post_shader_test.cpp`(내장 셋, `fsr`는 매개변수 없음).
- 화질(Windows, RTX 4090, 3840×2160): 같은 장면을 `none`과 `fsr`로 전체 화면에서 찍어 같은 부분을 1:1로 잘랐습니다. 4th 로고 화면의 "AMUSEWORLD"·"CREDIT" 글자와 6th 타이틀의 "EZ2DJ" 로고·금색 링 경계가 `fsr`에서 선명하고, 고리 잡티는 보이지 않았습니다. 6th의 작은 가사 글씨는 차이가 작습니다. 2배 창(1280×960)에서는 차이가 작습니다. 이미지: `docs/screenshots/shaders/ez2dj4th-fsr-4k-crop.png`, `ez2dj6th-fsr-4k-crop.png`(6th 두 장은 애니메이션 중이라 배경이 조금 다름).
- 비용: 4th 4K 전체 화면 vsync off, 30초씩 번갈아 두 번: `none` 902·878fps, `fsr` 866·799fps.

  *Review: DLSS 2 and later need motion vectors, depth and jitter and are NVIDIA-only, so they do not fit a 2D game; single-frame spatial upscalers (FSR 1, NIS), pixel-art ones (xBR, ScaleFX) and ML for drawn art (Anime4K) were the candidates, and FSR 1 EASU, which fits the present single-pass pipeline, went first. Implementation: `fsr.glsl` (single-pass EASU with AMD's MIT notice), the CMake embedding (`kPostShaderBuiltinFsr`), `fsr` in the catalog, the help, README, guide and THIRD_PARTY_NOTICES, and `post_shader_test.cpp` (three built-ins, `fsr` without parameters). Quality (Windows, RTX 4090, 3840×2160): the same scene fullscreen under `none` and `fsr`, the same part cropped 1:1; the "AMUSEWORLD" and "CREDIT" lettering on 4th's logo screen and the "EZ2DJ" logo and gold ring on 6th's title are sharp under `fsr` with no ringing seen, 6th's small lyric text differing little; at the 2x window (1280×960) the difference is small. Images: `docs/screenshots/shaders/ez2dj4th-fsr-4k-crop.png` and `ez2dj6th-fsr-4k-crop.png` (6th's two captures fall at slightly different animation frames). Cost: 4th fullscreen at 4K with vsync off, 30 s each, alternating, twice: `none` 902 and 878 fps, `fsr` 866 and 799 fps.*

## 검증 / Verification

- Windows x86 Debug(MSVC): 단위 테스트 6,444건 실패 0, CTest 4개 통과. Release 빌드, `re2dj_opengl_post_shader_probe` 12개 통과.
- WSL Linux `linux-x64-debug`·`linux-x86-debug`·clang(경고를 오류로): CTest 각 5개 통과.
- 실제 실행: `re2dj ez2dj4th --windowed --post-shader fsr`의 로그 `post shader     : fsr`, `presentation: post shader fsr (0 parameters)`, 경고 없음.
- 사람이 볼 것: 스팀덱처럼 약한 GPU에서의 비용, OSD로 바꿔 보는 느낌.

  *Windows x86 Debug (MSVC): 6,444 unit checks with 0 failures, 4 CTest tests passed; the Release build and `re2dj_opengl_post_shader_probe`'s 12 checks pass. WSL Linux `linux-x64-debug`, `linux-x86-debug` and clang (warnings as errors): 5 CTest tests each. Real run: `re2dj ez2dj4th --windowed --post-shader fsr` logs `post shader     : fsr` and `presentation: post shader fsr (0 parameters)` with no warnings. Left for a person: the cost on a weaker GPU such as a Steam Deck's, and switching to it from the OSD.*
