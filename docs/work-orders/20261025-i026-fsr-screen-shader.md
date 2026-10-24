# #26 작업 지시서 — 내장 화면 셰이더 `fsr` / #26 work order — the built-in screen shader `fsr`

이슈: [#26](https://github.com/reexec/re2DJ/issues/26) · 설계: [20261025-i026-fsr-screen-shader.md](../design/20261025-i026-fsr-screen-shader.md)

## 절차 / Steps

1. EASU를 `shaders/` 파일로 먼저 옮겨 4th·6th에서 `none`과 비교합니다.
   *Port EASU as a file in `shaders/` first and compare it with `none` on 4th and 6th.*
2. `src/graphics/post_shaders/fsr.glsl`로 옮겨 CMake 내장과 카탈로그에 넣고, 단위 테스트를 맞춥니다.
   *Move it to `src/graphics/post_shaders/fsr.glsl`, embed it through CMake and the catalog, and update the unit test.*
3. THIRD_PARTY_NOTICES, 도움말, README, 가이드(비교 이미지 포함).
   *THIRD_PARTY_NOTICES, the help, README and the guide (with comparison images).*
4. 빌드·테스트·GL probe, 실제 실행의 적용 로그와 비용 측정.
   *Builds, tests and the GL probe; the applied log line in a real run and the cost measured.*

## 완료 조건 / Done when

`--post-shader fsr`와 OSD에서 고를 수 있고, 4K 전체 화면에서 `none`보다 경계가 선명하며, 모든 타깃 빌드와 테스트가 통과한다.

*`fsr` can be chosen with `--post-shader fsr` and in the OSD, edges are sharper than `none` fullscreen at 4K, and every target's build and tests pass.*
