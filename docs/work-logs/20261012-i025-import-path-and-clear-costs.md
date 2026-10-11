# #25 작업 로그 — in-process 러너의 import 경로와 DirectDraw Clear 비용 줄이기 / #25 work log — cutting the in-process runner's import path and DirectDraw Clear costs

설계: [20261012-i025-import-path-and-clear-costs.md](../design/20261012-i025-import-path-and-clear-costs.md) · 지시서: [20261012-i025-import-path-and-clear-costs.md](../work-orders/20261012-i025-import-path-and-clear-costs.md)

## 2026-10-12 — 측정 방법 / How it was measured

- PC: [분석 문서](../analysis/windows-in-process-performance.md)와 같은 Ryzen 5 5600X, RTX 4090, Windows 11.
- 처리량: Release(x86, MSVC)를 `re2dj <target> --windowed --vsync off`로 40초 띄우고, 1초마다 창 제목의 FPS와 모든 `re2dj` 프로세스의 CPU 시간·메모리를 모아 15초 이후를 평균했습니다. 입력 없음(어트랙트 화면). 바꾸기 전과 후 빌드를 타깃마다 번갈아 돌렸습니다(측정 스크립트는 저장소 밖).
- 프로파일: RelWithDebInfo 빌드(PDB 있음)를 vsync off로 띄우고, 바깥의 Python 스크립트가 2ms마다 게스트 프로세스의 스레드를 하나씩 멈춰 `Wow64GetThreadContext`와 dbghelp `StackWalk64`로 스택을 읽었습니다. 샘플 사이에 CPU 시간이 늘지 않은 스레드는 빼서, 실제로 CPU를 쓴 곳만 셌습니다.

  *Same PC as the [analysis document](../analysis/windows-in-process-performance.md) (Ryzen 5 5600X, RTX 4090, Windows 11). Throughput: Release (x86, MSVC) run as `re2dj <target> --windowed --vsync off` for 40 s, sampling every second the window title's FPS and the CPU time and memory of every `re2dj` process and averaging after 15 s, with no input (attract screens), the builds before and after alternating per target (the scripts stay outside the repository). Profile: a RelWithDebInfo build (with its PDB) run with vsync off; an outside Python script suspended the guest process's threads one at a time every 2 ms and read their stacks with `Wow64GetThreadContext` and dbghelp's `StackWalk64`, leaving out threads whose CPU time had not grown since the last sample, so only where CPU went is counted.*

## 프로파일 / Profile

- 4th, 바꾸기 전(샘플 234개, 87%가 게스트 메인 스레드): `NativeKernel32Diagnostic::FindFacadeExport` 포함 29%, 그 아래 `GuestModuleRegistry::FindExport` 25%, `FindModule` 20%, `NormalizeModuleName` 17%(leaf 10%), `AsciiLower` 6%. `GateName`·`FormatGate` 7%, `operator new`·`malloc` 9%. DirectDraw `Clear` → `FillSurface` 23%(leaf `FillSurface` 10%, `memset` 7%).
- 4th, 게이트 캐시 뒤(샘플 342개): export 조회와 이름 정규화가 상위에서 사라졌고, `memset` 13%, `FillSurface` 12%가 가장 컸습니다. 나머지는 GPU 드라이버(`nvoglv32` 15%, `win32u` 12%)와 게스트 코드(6%)입니다.

  *4th before (234 samples, 87% on the guest main thread): `NativeKernel32Diagnostic::FindFacadeExport` 29% inclusive, under it `GuestModuleRegistry::FindExport` 25%, `FindModule` 20%, `NormalizeModuleName` 17% (10% leaf) and `AsciiLower` 6%; `GateName` and `FormatGate` 7%, `operator new` and `malloc` 9%; DirectDraw `Clear` → `FillSurface` 23% (10% leaf `FillSurface`, 7% `memset`). 4th after the gate cache (342 samples): the export lookup and name normalization left the top, `memset` 13% and `FillSurface` 12% were the largest, the rest the GPU driver (`nvoglv32` 15%, `win32u` 12%) and guest code (6%).*

## 처리량 / Throughput (vsync off, FPS, 두 번의 평균 / mean of two runs)

| 타깃 | 바꾸기 전 | 게이트 캐시 | 변화 | | 게이트 캐시 | + `Clear` | 변화 |
|---|---|---|---|---|---|---|---|
| 4th | 570 | 694 | +22% | | 639 | 750 | +17% |
| 1st SE | 895 | 953 | +6% | | — | — | — |
| 5th | 653 | 846 | +30% | | 674 | 849 | +26% |
| 6th | 501 | 717 | +43% | | 624 | 758 | +21% |
| 2nd MOVE | 790 | 1,000 | +26% | | — | — | — |

- 두 비교는 다른 시각에 따로 쟀고, 같은 빌드(게이트 캐시)도 두 번째 비교에서 4th 639·5th 674·6th 624로 첫 비교와 달랐습니다. 같은 빌드의 한 번 한 번도 5th 765와 540처럼 차이가 큽니다. 그래서 각 비교 안에서 번갈아 잰 차이만 근거로 삼습니다.
- 두 번째 비교는 시간을 줄이려고 `Clear` 비용이 프로파일에 나온 4th와 5th·6th만 쟀습니다. 1st SE와 2nd MOVE가 `Clear`를 얼마나 부르는지는 재지 않았습니다.
- 메모리(working set, private bytes)는 바뀌지 않았습니다(예: 6th 473 MB, 최대 1,455 MB).

  *The two comparisons were taken at different times; the same build (the gate cache) gave 4th 639, 5th 674 and 6th 624 in the second against higher in the first, and single runs of one build differ as much as 765 and 540 for 5th, so only the alternating difference within each comparison counts. To save time the second covered only 4th, whose profile showed `Clear`, and 5th and 6th; how often 1st SE and 2nd MOVE call `Clear` was not measured. Memory (working set, private bytes) did not change (for example 6th 473 MB, at most 1,455 MB).*

## 구현 / Implementation

- `NativeGuestModuleSet`: `binding_index_`, `FindBinding`, `FindExport(address)`, `FindGate(address)`·`Dispatch`가 색인 사용, 인자 16개까지 `std::array`.
- `NativeKernel32Diagnostic::FindFacadeExport`: `GuestExportDescriptor*`를 바인딩에서.
- `native_continuation_observation.cpp`: `ContinuationContext::gates`(이름·export·`StringArguments`), 호출 기록을 마지막에 move.
- `ddraw_surface7.cpp` `FillSurface`: `std::vector<std::uint16_t>(n, color)`.
- `--vsync`: `main.cpp`의 옵션·도움말·로그(`present sync : immediate`), `SdlHostPresentation::SetPresentSync`, README.

  *`NativeGuestModuleSet`: `binding_index_`, `FindBinding`, `FindExport(address)`, `FindGate(address)` and `Dispatch` on the index, up to 16 arguments in a `std::array`. `NativeKernel32Diagnostic::FindFacadeExport` returns the binding's `GuestExportDescriptor*`. The observation path: `ContinuationContext::gates` (name, export, `StringArguments`) and the call record moved at the end. `FillSurface` in `ddraw_surface7.cpp`: `std::vector<std::uint16_t>(n, color)`. `--vsync`: the option, help and log line (`present sync : immediate`) in `main.cpp`, `SdlHostPresentation::SetPresentSync`, README.*

## 검증 / Verification

- Windows x86 Debug(MSVC): 전체 빌드 성공, `re2dj_unit_tests` checks 6,438, failures 0, CTest 4개 통과. Release·RelWithDebInfo 빌드 성공.
- WSL Linux `linux-x64-debug`·`linux-x86-debug`·clang(`build/linux-x64-clang`, 모두 경고를 오류로): 빌드 성공, CTest 각 5개 통과.
- 실제 실행(Release, 기본 설정): 4th와 6th를 30초씩 띄워 창 제목 60.0fps, 로그에 경고·오류 없음.

  *Windows x86 Debug (MSVC): full build, 6,438 checks with 0 failures, 4 CTest tests passed; Release and RelWithDebInfo build. WSL Linux `linux-x64-debug`, `linux-x86-debug` and clang (`build/linux-x64-clang`), all with warnings as errors: build and pass 5 CTest tests each. Real runs (Release, default settings): 4th and 6th for 30 s each show 60.0 fps in the title and no warning or error in the log.*
