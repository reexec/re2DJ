# #27 작업 지시서 — 다단계 화면 셰이더와 `fsr` RCAS·`scalefx`·`anime4k` / #27 work order — multi-pass screen shaders with `fsr` RCAS, `scalefx` and `anime4k`

이슈: [#27](https://github.com/reexec/re2DJ/issues/27) · 설계: [20261025-i027-multipass-screen-shaders.md](../design/20261025-i027-multipass-screen-shaders.md)

## 절차 / Steps

1. `post_shader_preset.{h,cpp}`: `.glslp` 부분집합 파서, 단일 pass, pass 크기.
   *The `.glslp` subset parser, the single pass and pass sizes.*
2. 카탈로그를 내장 파일 묶음과 preset 로딩으로 바꾸고, CMake가 목록의 파일을 조각으로 넣게 합니다.
   *Turn the catalog into embedded file sets with preset loading, and have CMake embed the listed files in pieces.*
3. `OpenGlPostProcess`: pass별 프로그램·FBO 대상, `UseForPresent`가 체인을 그리고 마지막 pass만 호출자에게 남김, stock pass.
   *Per-pass programs and targets in `OpenGlPostProcess`, `UseForPresent` drawing the chain and leaving the last pass to the caller, the stock pass.*
4. 내장 preset: `fsr`(EASU+RCAS), `scalefx`(libretro 원본), `anime4k`(mpv 훅 변환).
   *The built-in presets: `fsr` (EASU+RCAS), `scalefx` (libretro's files), `anime4k` (converted from the mpv hooks).*
5. 테스트, 도움말·README·가이드·고지·IMPLEMENTED, 빌드·probe·실제 비교·비용.
   *Tests, the help, README, guide, notices and IMPLEMENTED; builds, the probe, real comparisons and the cost.*

## 완료 조건 / Done when

세 내장 preset이 OSD와 `--post-shader`로 골라지고 4K 전체 화면에서 각자의 결과를 내며, `shaders/`의 `.glslp`도 같은 길로 돌고, 단일 pass 셰이더와 probe가 그대로 통과하며, 모든 타깃 빌드와 테스트가 통과한다.

*The three built-in presets can be chosen in the OSD and with `--post-shader` and give their own results fullscreen at 4K, a `.glslp` in `shaders/` runs the same way, single-pass shaders and the probe still pass, and every target's build and tests pass.*
