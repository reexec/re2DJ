# #27 작업 로그 — 다단계 화면 셰이더와 `fsr` RCAS·`scalefx`·`anime4k` / #27 work log — multi-pass screen shaders with `fsr` RCAS, `scalefx` and `anime4k`

설계: [20261025-i027-multipass-screen-shaders.md](../design/20261025-i027-multipass-screen-shaders.md) · 지시서: [20261025-i027-multipass-screen-shaders.md](../work-orders/20261025-i027-multipass-screen-shaders.md)

## 2026-10-25

- 자료: ScaleFX는 libretro `glsl-shaders`의 `edge-smoothing/scalefx`에서 pass 다섯(각 파일에 Sp00kyFox의 MIT 고지)을 그대로 가져오고, preset은 같은 값으로 다시 썼습니다. Anime4K는 `bloc97/Anime4K`의 `glsl/Upscale/Anime4K_Upscale_CNN_x2_S.glsl`(MIT, mpv 훅 다섯 개: 합성곱 4×3×3×3, 4×3×3×8 셋, depth-to-space)을 스크립트로 변환했습니다. 가중치 행렬은 그대로이고, mpv의 `texOff`를 `Texture` 샘플로 바꾸며 y 방향을 뒤집었습니다. FSR 1의 RCAS는 `ffx_fsr1.h`의 알고리즘을 옮겼습니다.
- 구현: `post_shader_preset.{h,cpp}`, 카탈로그의 내장 파일 묶음(`kPostShaderBuiltinFiles`, CMake가 17개 파일을 12000바이트 조각으로 포함)과 `LoadPostShaderPreset`, `OpenGlPostProcess`의 pass별 프로그램·FBO·stock pass와 새 `UseForPresent`(게임 텍스처·viewport를 받음), backend 호출부, 내장 preset 셋, 도움말·README·가이드·고지·IMPLEMENTED.
- 고친 것: 첫 실행에서 `scalefx`가 검게 나왔습니다. 마지막 pass가 `PassPrev5Texture`(게임 그림)를 읽는데, 이전 출력을 묶는 반복문의 상한이 `back < outputs.size()`라 N = i + 1인 그림 자체를 빼먹었습니다. `<=`로 고쳤습니다.
- 확인한 것: `scalefx`의 글자가 다른 셋보다 작아 보여 타이밍을 의심했으나, `none`을 같은 지연으로 두 번 찍어 서로 같음을 확인했고 전체 화면 배치도 정상이었습니다. ScaleFX가 원본 색만 쓰는 픽셀아트 필터라 글자 둘레의 안티앨리어싱 회색을 흰색으로 판정해 글자가 얇아진 것으로, 설계에서 예상한 그대로입니다.
- 사용자 preset: `shaders/myfsr.glslp`(내장 fsr의 preset과 두 파일 복사)로 4th를 띄워 `post shader myfsr.glslp (1 parameters)`가 남는 것을 확인하고 지웠습니다.

  *Sources: ScaleFX is the five passes of libretro `glsl-shaders`' `edge-smoothing/scalefx` unchanged (each file carrying Sp00kyFox's MIT notice) with the preset written anew to the same values; Anime4K is `bloc97/Anime4K`'s `glsl/Upscale/Anime4K_Upscale_CNN_x2_S.glsl` (MIT, five mpv hooks: a 4×3×3×3 convolution, three 4×3×3×8, depth-to-space) converted by a script, the weight matrices unchanged, mpv's `texOff` turned into `Texture` samples with y turned over; FSR 1's RCAS is ported from `ffx_fsr1.h`. Implementation: `post_shader_preset.{h,cpp}`, the catalog's embedded file sets (`kPostShaderBuiltinFiles`, CMake embedding 17 files in 12000-byte pieces) and `LoadPostShaderPreset`, `OpenGlPostProcess`'s per-pass programs, framebuffers and stock pass with the new `UseForPresent` (taking the game's texture and the viewport), the backend's call, the three built-in presets, the help, README, guide, notices and IMPLEMENTED. Fixed: on the first run `scalefx` came out black; its last pass reads `PassPrev5Texture` (the game's picture) and the loop binding earlier outputs stopped at `back < outputs.size()`, leaving out the picture itself at N = i + 1; now `<=`. Checked: `scalefx`'s lettering looked smaller than the others', so timing was suspected, but two `none` captures at the same delay matched each other and the full-screen layout was right; ScaleFX, a pixel-art filter using only the picture's own colours, resolves the anti-aliased grey around letters to white, thinning them, as the design expected. A user preset: 4th started with `shaders/myfsr.glslp` (the built-in fsr's preset and two files copied) logged `post shader myfsr.glslp (1 parameters)`; the copy was removed.*

## 검토 / Review

- 커밋 전에 다섯 관점(GL 체인, preset 파서, 셰이더 이식, CMake 포함, 문서·테스트)으로 변경을 검토하고 지적마다 세 명이 반박을 시도해 걸러냈습니다(워크플로, 에이전트 47개). 확정 11건을 고쳤고 3건은 반박됐습니다.
  - GL 체인: pass 대상(FBO)을 만들지 못했을 때 GL 오류가 남아 backend의 `Present` 검사가 실패하던 것(이제 실패 경로가 오류를 비우고 창 대상·viewport·unit 0 텍스처를 되돌리며 프로그램을 내림, backend는 거짓 반환 뒤 자기 프로그램을 다시 current로), 단일 pass 경로에서 `OrigTexture`·`PassPrev1Texture`가 비어 있는 unit을 가리키던 회귀(이제 unit 0), 단일 pass `.glslp`의 `filter_linear0`이 무시되던 것(`filter_specified`).
  - preset 파서: 따옴표 안의 `#`, UTF-8 BOM, `scaleN`·`scale_typeN`이 `_x`/`_y`보다 우선하는 libretro 규칙. 테스트 `CheckLibretroRules`.
  - 문서: kb의 6·7절, 설계·작업 로그의 "ScaleFX preset 그대로" 표현(pass 파일만 그대로, preset은 다시 씀), README·가이드 §1의 `.glslp`.
  - 반박된 것: absolute 배율의 반올림(libretro는 내림이지만 정수만 쓰므로 같음), 12000바이트 조각이 UTF-8 글자를 가를 가능성(조각은 이어 붙여지므로 무해), `.glslp` 파일에 고지가 없다는 지적(preset은 재작성한 파일).

  *Before the commit the change was reviewed from five angles (the GL chain, the preset parser, the shader ports, the CMake embedding, documents and tests) with three refuters per finding (a workflow of 47 agents): 11 confirmed findings were fixed and 3 refuted. GL chain: a failed pass target (FBO) left a GL error that tripped the backend's `Present` check (the failure path now drains the error, restores the window target, viewport and the unit 0 texture and unbinds the program, and the backend makes its own program current again after a false return); the single-pass path pointing `OrigTexture` and `PassPrev1Texture` at units nothing is bound on, a regression (now unit 0); a one-pass `.glslp`'s `filter_linear0` being ignored (`filter_specified`). Parser: `#` inside quotes, a UTF-8 BOM, and libretro's rule that `scaleN` and `scale_typeN` win over `_x`/`_y`, with the test `CheckLibretroRules`. Documents: kb sections 6 and 7, the design's and work log's "ScaleFX preset unchanged" wording (the pass files are, the preset was written anew), and `.glslp` in README and guide section 1. Refuted: rounding of absolute scales (libretro truncates, but only integers are used), a 12000-byte piece splitting a UTF-8 character (the pieces are concatenated, so harmless), and a missing notice in the `.glslp` files (the presets are rewritten files).*

## 화질과 비용 / Quality and cost

- Windows, RTX 4090, 3840×2160 전체 화면, 같은 장면을 1:1로 잘라 비교(`docs/screenshots/shaders/ez2dj4th-upscalers-4k-crop.png` 3배 확대, `ez2dj6th-upscalers-4k-crop.png` 2배 확대).
  - `fsr`(EASU+RCAS): #26의 EASU만 쓸 때보다 더 선명하고 글자 경계가 또렷합니다. 6th 로고 글자 둘레에 RCAS의 밝은 테가 살짝 보이며 `FSR_SHARPNESS`로 줄일 수 있습니다.
  - `scalefx`: 4th의 안티앨리어싱 글자가 얇은 윤곽으로, 6th의 글자는 딱딱한 외곽선으로 바뀌고 배경 그라데이션이 단색화됩니다. 픽셀아트용이라 EZ2DJ의 CG에는 맞지 않습니다.
  - `anime4k`: `none`보다 적당히 선명하고 잡티가 없습니다. 2배까지만 신경망이 키우고 나머지는 선형이라 `fsr`보다는 부드럽습니다.
- `none`과의 평균 화소 차이(4th 로고 부분, 채널당): `fsr` 1.25, `anime4k` 1.45. 둘 다 그림을 실제로 바꿉니다.
- 비용(4th 4K 전체 화면, vsync off, 30초): `none` 799, `fsr` 688, `scalefx` 538, `anime4k` 565fps. 60fps 기본 설정에서는 모두 여유가 큽니다.

  *Windows, RTX 4090, 3840×2160 fullscreen, the same scene cropped 1:1 (`docs/screenshots/shaders/ez2dj4th-upscalers-4k-crop.png` at 3x, `ez2dj6th-upscalers-4k-crop.png` at 2x). `fsr` (EASU+RCAS) is sharper than #26's EASU alone with crisp lettering, a faint light rim from RCAS around 6th's logo letters that `FSR_SHARPNESS` reduces. `scalefx` turns 4th's anti-aliased lettering into thin outlines and 6th's into hard outlines and posterizes gradients: made for pixel art, not EZ2DJ's CG. `anime4k` is moderately sharper than `none` without artifacts, softer than `fsr` since the network scales only to 2x and the rest is linear. Mean pixel difference from `none` (4th's logo area, per channel): `fsr` 1.25, `anime4k` 1.45, both changing the picture. Cost (4th fullscreen at 4K, vsync off, 30 s): `none` 799, `fsr` 688, `scalefx` 538, `anime4k` 565 fps, all with ample headroom at the 60 fps default.*

## 검증 / Verification

- Windows x86 Debug(MSVC): 전체 빌드, 단위 테스트 6,510건 실패 0(`post_shader_test`의 카탈로그 검사와 새 `post_shader_preset_test` 3묶음), CTest 4개 통과. Release 빌드, `re2dj_opengl_post_shader_probe` 12개 통과.
- WSL Linux `linux-x64-debug`·`linux-x86-debug`·clang(`build/linux-x64-clang`, 모두 경고를 오류로): 빌드 성공, CTest 각 5개 통과.
- 실제 실행: 4th·6th에서 `--post-shader fsr`·`scalefx`·`anime4k`가 각각 `(1 parameters)`·`(3 parameters)`·`(0 parameters)`로 적용되고 경고 없음.

  *Windows x86 Debug (MSVC): full build, 6,510 unit checks with 0 failures (the catalog check in `post_shader_test` and the new `post_shader_preset_test`'s three groups), 4 CTest tests passed; the Release build and `re2dj_opengl_post_shader_probe`'s 12 checks pass. WSL Linux `linux-x64-debug`, `linux-x86-debug` and clang (`build/linux-x64-clang`), all with warnings as errors: build and pass 5 CTest tests each. Real runs: on 4th and 6th `--post-shader fsr`, `scalefx` and `anime4k` apply as `(1 parameters)`, `(3 parameters)` and `(0 parameters)` with no warnings.*
