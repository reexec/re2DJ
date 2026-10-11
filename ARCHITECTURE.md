# 아키텍처 / Architecture

## Linux x86·x64 지원 확장 계획 / Updated plan

Linux x86·x64 제품 호스트는 원본 PE32를 같은 프로세스 안에서 실행합니다. x86은 i386으로 직접 실행하고, x64는 CPU compatibility mode로 실행합니다(작업 353–357). 처음에는 별도 i386 helper가 IPC로 게스트를 실행했지만, 이 helper 경로는 작업 357에서 진단 fallback으로 내려갔고 작업 379에서 Windows의 native helper와 함께 제거되었습니다([설계 / Design](docs/design/20260926-379-remove-native-helper-ipc.md)). 아래 날짜별 절 가운데 helper·IPC·protocol을 다루는 것은 당시의 기록입니다.

*The Linux x86 and x64 product hosts run the original PE32 in their own process: x86 directly as i386, x64 in CPU compatibility mode (Tasks 353–357). A separate i386 helper that ran the guest over IPC came first; Task 357 demoted it to a diagnostic fallback and Task 379 removed it together with the Windows native helper ([design](docs/design/20260926-379-remove-native-helper-ipc.md)). The dated sections below that cover the helper, IPC, or its protocol are records of their time.*

게스트가 부르는 DLL은 플랫폼 중립 `GuestModuleDescriptor`와 `GuestModuleRegistry`로 기술하고, 그 기술로부터 실제 PE32 facade image를 만들어 guest 주소 공간에 mapping한다. 정적 IAT slot 중 registry에 있는 항목은 facade export thunk로 다시 쓰고, `GetModuleHandleA`와 `GetProcAddress`는 같은 registry를 조회한다. 따라서 정적 import와 동적 해석이 한 주소로 합류하며, pseudo handle은 쓰지 않는다. module handler가 호스트 메모리 배치를 알지 않도록 `ImportCall`의 선택적 `ImportCallServices` 경계가 bounded guest 문자열 읽기와 module/export 조회만 제공하고, service가 없는 호출은 기존처럼 0을 반환한다. `ImportReturn.exit_process`는 호출이 게스트로 돌아가지 않고 프로세스가 끝남을 뜻한다. `kernel32!ExitProcess`가 이를 쓰고, IPC dispatcher는 `kStop`으로, Linux in-process bridge는 `ExitNativeGuestProcess`로 실행을 정상 완료시킨다(작업 358). `user32` facade의 `MessageBoxA`는 창을 띄울 서비스가 아직 없으므로 `uType`의 기본 버튼 ID를 돌려주고, continuation이 문구와 제목을 기록한다(작업 359). 재결합은 실행 준비 단계에서만 일어나며 실패 시 원래 thunk로 후퇴하지 않고 준비 자체가 실패한다. `src/platform/linux/` 루트에는 x86·x64 두 host에서 모두 빌드되는 코드만 둔다. PE mapping·import thunk·PE session·fault observation·in-process runner는 두 폭 공용이라 루트에 있고, process bootstrap·import bridge·저주소 할당은 루트 헤더 하나에 선언한 뒤 `x86/`(i386 직접 실행)과 `x64/`(compatibility mode)에 따로 구현한다(작업 354). facade module image·set, dynamic thunk, kernel32 진단도 루트에 있다(작업 355). 게스트 SEH 판단과 instruction trace도 루트에서 공용이고(작업 356), `src/platform/linux/x86/`에는 i386 직접 실행 bootstrap·bridge·저주소 할당과 i386 helper 구성이 남는다. `native_helper_backend.cpp`처럼 x64 host가 i386 helper를 구동하는 경계는 루트에 남는다. 상세 근거는 [게스트 모듈 resolver 합류 설계](docs/design/20260922-344-guest-module-resolver-convergence.md)에 둔다. facade image는 디스크 파일이 아니라 메모리 안에서만 존재한다. export thunk가 image base뿐 아니라 호스트 import bridge 함수와 cleanup 전역의 주소까지 바이트열에 굳히는데, 두 값은 ASLR 아래에서 실행마다 달라지므로 빌드 시점에 알 수 없다. 파일로 만들려면 그 주소들을 슬롯 경유 간접 참조로 바꾸고 `.reloc`을 더해야 하며, 이는 facade를 프로세스 밖에서 공유하거나 배포해야 할 때 치를 비용이다.

*The DLLs a guest calls are described by the platform-neutral `GuestModuleDescriptor` and `GuestModuleRegistry`, and that description builds a real PE32 facade image mapped into the guest address space. Static IAT slots present in the registry are rewritten to facade export thunks, and `GetModuleHandleA` and `GetProcAddress` query the same registry, so static imports and dynamic resolution converge on one address and no pseudo handle is used. To keep module handlers ignorant of host memory layout, an optional `ImportCallServices` boundary on `ImportCall` exposes only bounded guest-string reads and module/export lookup, and calls without services keep returning zero. `ImportReturn.exit_process` marks a call that does not return because the process ends; `kernel32!ExitProcess` sets it, the IPC dispatcher maps it to `kStop`, and the Linux in-process bridges end the run as a normal completion through `ExitNativeGuestProcess` (Task 358). The `user32` facade's `MessageBoxA` returns the default button ID for its `uType`, since no service shows the box yet, while the continuation records its text and caption (Task 359). Rebinding happens only while preparing execution and, on failure, fails preparation instead of falling back to the original thunk. The `src/platform/linux/` root holds only code that builds on both x86 and x64 hosts. PE mapping, import thunks, the PE session, fault observation, and the in-process runner are shared by both widths and live at the root; the process bootstrap, import bridge, and low allocation are declared once in root headers and implemented separately under `x86/` (direct i386 execution) and `x64/` (compatibility mode) (Task 354). Facade module images and sets, dynamic thunks, and the kernel32 diagnostic are also at the root (Task 355); guest SEH decisions and the instruction trace are shared at the root as well (Task 356), leaving only the direct-i386 bootstrap, bridge, low allocation, and the i386 helper configuration under `src/platform/linux/x86/`, while boundaries such as `native_helper_backend.cpp`, where an x64 host drives the i386 helper, stay at the root. See the [guest module resolver convergence design](docs/design/20260922-344-guest-module-resolver-convergence.md). The facade image exists only in memory rather than as a file on disk: an export thunk bakes in not just the image base but the addresses of the host import-bridge function and cleanup global, and both differ per run under ASLR, so neither is knowable at build time. Emitting a file would mean routing those addresses through patched slots and adding a `.reloc` section — the price to pay when the facade has to be shared outside the process or distributed.*

Linux i386 in-process 진단 가운데 resolver를 거치는 세 가지(`--linux-in-process-first-resolver`, `--linux-in-process-getversion-call`, `--linux-in-process-createfile-call`)는 `src/platform/linux/x86/native_kernel32_diagnostic`의 `NativeKernel32Diagnostic` 하나를 공유한다. 이 문맥이 facade 등록과 정적 IAT 재결합, `ImportCallServices`, resolver identity 관측, 그리고 facade 밖 첫 미처리 정적 import와 첫 미해석 `GetProcAddress` 요청 기록을 맡고, 각 진단은 자기가 제한할 gate의 복귀 지점에 `INT3`를 두는 일만 한다. instruction trace와 함께 쓰는 진단은 그 `INT3`를 두기 전에 `StopNativeInstructionTrace`를 호출해야 한다. 그러지 않으면 bridge가 같은 복귀 지점에 trace breakpoint를 다시 걸어 진단의 `INT3`를 single-step으로 소비한다. 근거는 [작업 348 설계](docs/design/20260923-348-early-resolver-diagnostics-convergence.md)에 둔다.

*The three Linux i386 in-process diagnostics that go through the resolver (`--linux-in-process-first-resolver`, `--linux-in-process-getversion-call`, `--linux-in-process-createfile-call`) share one `NativeKernel32Diagnostic` from `src/platform/linux/x86/native_kernel32_diagnostic`. That context owns facade registration and static IAT rebinding, `ImportCallServices`, resolver-identity observation, and recording the first unhandled static import outside the facade and the first unresolved `GetProcAddress` request; each diagnostic only places an `INT3` at the return site of the gate it bounds. A diagnostic that also runs the instruction trace must call `StopNativeInstructionTrace` before placing that `INT3`; otherwise the bridge re-arms a trace breakpoint on the same return site and consumes the diagnostic's `INT3` as a single step. See the [Task 348 design](docs/design/20260923-348-early-resolver-diagnostics-convergence.md).*

Linux x64 제품은 원본 PE32를 별도 helper가 아니라 같은 x86-64 프로세스 안에서 compatibility mode로 실행한다 **[부분 구현]**. `src/platform/linux/x64/native_compat_mode`의 `NativeCompatModeRuntime`이 4 GiB 미만에 guest stack·TEB/PEB·전환 page를 할당하고, `modify_ldt`로 TEB를 base로 하는 게스트 FS descriptor를 만든다. 진입은 `lretq`로 CS `0x23`에 들어가고, 게스트의 import thunk는 i386와 같은 형태로 전환 page의 `gate32`를 부른다. `gate32`는 `lcall $0x33`로 host landing에 들어가서 폭 중립 `NativeImportGateHandler`를 호출한다. host로 돌아오는 모든 경로(정상 복귀, import landing, asm signal entry)는 C 코드 전에 host FS base를 `wrfsbase` 또는 `arch_prctl`로 복원한다. 전환 page는 프로세스 전역이며, x64의 `NativeImportGateBridgeAddress()`가 그 `gate32`를 돌려준다. 그래서 공용 `NativePeSession`·`NativeInProcessRunner`와 facade·kernel32 진단이 x86과 같은 코드로 동작한다(작업 354·355). signal handler는 `ucontext`를 고친 뒤 게스트 FS를 다시 적재하고 sigreturn으로 게스트에 복귀할 수 있다. 그 위에서 게스트 SEH 디스패치(signal handler 안의 중첩 전환)와 instruction trace가 동작한다. INT3 판정·SEH record 구성과 trace 상태기계는 루트의 `native_guest_seh`·`native_instruction_trace`에 두고 두 폭이 함께 쓴다(작업 356). x64 CLI의 in-process 진단 다섯 개는 실제 4th CHD에서 x86과 같은 결과를 낸다. continuation은 `#0016`까지, GetVersion trace는 43 frame으로 같다. [작업 353 설계](docs/design/20260924-353-linux-x64-compat-mode-adapter.md)의 네 단계가 모두 끝났다. CLI의 기본 `--run`도 두 폭 모두 이 in-process continuation을 쓰고, 별도 i386 helper는 `--linux-helper <path>`를 줄 때만 쓰는 진단 fallback이다(작업 357).

*The Linux x64 product runs the original PE32 in compatibility mode inside the same x86-64 process rather than in a separate helper **[Partially implemented]**. `NativeCompatModeRuntime` in `src/platform/linux/x64/native_compat_mode` allocates the guest stack, TEB/PEB, and transition pages below 4 GiB and creates a guest FS descriptor based at the TEB with `modify_ldt`. Entry reaches CS `0x23` with `lretq`; guest import thunks keep the i386 shape and call the transition page's `gate32`, which `lcall`s `$0x33` into a host landing that invokes the width-neutral `NativeImportGateHandler`. Every path back to the host — normal return, import landing, and the asm signal entry — restores the host FS base with `wrfsbase` or `arch_prctl` before any C code. The transition pages are process-global and the x64 `NativeImportGateBridgeAddress()` returns their `gate32`, so the shared `NativePeSession`, `NativeInProcessRunner`, facades, and kernel32 diagnostic run with the same code as x86 (Tasks 354–355); the signal handler can edit `ucontext`, reload the guest FS, and return into the guest through sigreturn, carrying guest SEH dispatch (a nested transition inside the signal handler) and the instruction trace, whose INT3 recognition, SEH record building, and trace state machine live at the root in `native_guest_seh` and `native_instruction_trace`, shared by both widths (Task 356). All five x64 CLI in-process diagnostics match x86 on the real 4th CHD, including the continuation to `#0016` and the 43-frame GetVersion trace, completing all four stages of the [Task 353 design](docs/design/20260924-353-linux-x64-compat-mode-adapter.md). The CLI default `--run` also takes this in-process continuation on both widths; the separate i386 helper is a diagnostic fallback used only with `--linux-helper <path>` (Task 357).*

Linux x86 제품은 게스트를 호스트 스레드에서 그대로 실행한다. 게스트의 import thunk는 `src/platform/linux/x86/native_import_bridge`의 `NativeImportGateBridge`로 들어오고, 브리지는 GS를 호스트 값(`g_native_host_gs_selector`)으로 바꾼 뒤 스택을 16바이트로 정렬하고 C++ 처리기를 부른다. 게스트 스택은 Win32 규약대로 4바이트 정렬뿐이고, 호스트 코드는 i386 System V ABI대로 호출 시점의 16바이트 정렬을 전제하기 때문이다(작업 459, [kb](docs/kb/i386-host-abi-at-guest-boundary.md)). 호스트에서 게스트로 가는 `CallGuestStdcallWords`도 같은 정렬을 한다.

*The Linux x86 product runs the guest directly on a host thread. Guest import thunks enter `NativeImportGateBridge` in `src/platform/linux/x86/native_import_bridge`, which switches GS to the host value (`g_native_host_gs_selector`), aligns the stack to 16 bytes and calls the C++ handler: the guest stack is only 4-byte aligned under the Win32 convention, while host code assumes the i386 System V ABI's 16 bytes at a call (task 459, [kb](docs/kb/i386-host-abi-at-guest-boundary.md)). `CallGuestStdcallWords`, going from the host to the guest, aligns the same way.*

이 문서는 현재 저장소에 구현된 구조와, 그 구조가 향후 확장될 방향을 기술한다. 구현이 바뀌면 같은 작업에서 이 문서를 갱신한다.

*This document describes the structure currently implemented in the repository and how it is intended to grow. Update it in the same task whenever the implementation changes.*

> [!NOTE]
> 아래 표기 규칙을 따른다. **[구현됨]** 은 저장소에 코드가 있고 빌드·테스트로 확인된 것, **[설계됨]** 은 인터페이스만 정해진 것, **[계획]** 은 아직 설계 문서만 있는 것이다.
>
> *Sections are marked **[Implemented]** when code exists and is verified by a build or test, **[Designed]** when only the interface is fixed, and **[Planned]** when only a design note exists.*

---

## 1. 실행 모델 / Execution model

게스트는 32비트 x86 Win32 PE 실행 파일이다. Linux의 x86·x64 제품 CLI는 원본 PE32를 자기 프로세스 안에 매핑해 실행한다. 그 러너의 OS 중립 부분은 작업 446부터 `src/platform/native/`(namespace `re2dj::platform::native`)에 있고, OS가 필요한 일은 `native_host_services.h`(메모리 매핑·보호·해제, 코드 캐시, 페이지 크기, 시계, 잠자기)와 세 backend 계약(`native_process_bootstrap.h`, `native_import_bridge.h`, `native_low_memory.h`)을 거친다. fault는 OS 중립 `NativeFaultKind`로 판단한다. Linux 구현은 `src/platform/linux/`(루트와 `x86/`·`x64/`)에 있다. Windows x86 제품도 작업 449부터 같은 러너로 실행한다. backend는 `src/platform/windows/x86/`(VEH, 그림자 TEB, fs:0 동기화; [작업 448 설계](docs/design/20261004-448-windows-x86-backend.md))이고, re2dj.exe는 0x60000000에 고정되며 시작할 때 자신을 일시 정지 상태로 다시 띄워 게스트 이미지 영역(0x400000~)을 로더보다 먼저 예약한다([작업 449 설계](docs/design/20261004-449-windows-cli-in-process.md)). 예전 주입 경로(`WindowsOriginalProcessBackend`, 주입 런타임, COM facade, 런처 probe)는 작업 450에서 지웠다. 아래 날짜별 절의 Windows 주입 경로 설명은 기록으로만 남는다([작업 446 설계](docs/design/20261004-446-windows-in-process-loader.md)). 아래 날짜별 절의 `src/platform/linux/` 루트 공용 코드는 지금 `src/platform/native/`에 있다.

*The guest is a 32-bit x86 Win32 PE executable. Linux's x86 and x64 product CLIs map the original PE32 into their own process and run it there. From task 446 the runner's OS-neutral part lives in `src/platform/native/` (namespace `re2dj::platform::native`), reaching the OS only through `native_host_services.h` (memory mapping, protection and release, the code cache, the page size, clocks and sleeping) and three backend contracts (`native_process_bootstrap.h`, `native_import_bridge.h`, `native_low_memory.h`), and deciding on faults through the OS-neutral `NativeFaultKind`; the Linux implementations are under `src/platform/linux/` (its root, `x86/` and `x64/`). From task 449 the Windows x86 product runs through the same runner, its backend in `src/platform/windows/x86/` (a VEH, a shadow TEB and fs:0 kept in step; [task 448 design](docs/design/20261004-448-windows-x86-backend.md)); re2dj.exe sits at a fixed 0x60000000 and at launch starts itself again suspended to reserve the guest image range (0x400000 up) ahead of the loader ([task 449 design](docs/design/20261004-449-windows-cli-in-process.md)). The former injection path (`WindowsOriginalProcessBackend`, the injected runtime, the COM facades and the launcher probe) was deleted in task 450, and the dated sections below that describe it remain only as a record ([task 446 design](docs/design/20261004-446-windows-in-process-loader.md)). Shared code the dated sections below place at the `src/platform/linux/` root now lives in `src/platform/native/`.*

## 2026-09-13 presentation filter selection

SDL3/OpenGL `Present` keeps the guest's logical render target and aspect-fit viewport calculation, then selects the final FBO sampler through `PresentationFilter`. Equal positive integer scales such as 640×480, 1280×960, and 1920×1440 use `GL_NEAREST` to preserve source pixel boundaries; all other scales use `GL_LINEAR` to reduce non-integer scaling stair-steps. The policy is platform-neutral and unit-tested, while individual guest texture filters remain driven by the original draw state.

*SDL3/OpenGL `Present` retains the guest logical render target and aspect-fit viewport calculation, then selects the final FBO sampler through `PresentationFilter`. Equal positive integer scales such as 640×480, 1280×960, and 1920×1440 use `GL_NEAREST` to preserve source pixel boundaries; all other scales use `GL_LINEAR` to reduce non-integer scaling stair-steps. The policy is platform-neutral and unit-tested, while individual guest texture filters remain driven by the original draw state.*

HLE 경계는 **Win32 import thunk**다. 로더가 게스트의 import table을 해석할 때, 각 API를 실제 DLL이 아니라 합성 gate 주소로 바인딩한다. 실행 backend가 gate 주소로 제어를 넘기면 C++ 구현이 호출된다.

*The HLE boundary is the **Win32 import thunk**. When the loader parses the guest import table, it binds each API to a synthetic gate address instead of a real DLL. Control transferred to a gate address dispatches into the C++ implementation.*

```mermaid
flowchart LR
    HDD["HDD directory<br/>(user-supplied path)"] --> SCAN["HDD scan<br/>+ target profile"]
    SCAN --> PE["PE32 image reader"]
    PE --> LOAD["Loader:<br/>sections, relocs, imports"]
    LOAD --> MEM["Guest address space<br/>(flat 32-bit)"]
    MEM --> EXEC["x86-32 execution backend"]
    EXEC -->|import gate| HLE["Win32 / DirectX HLE"]
    HLE --> VFS["Virtual file system"]
    HLE --> GFX["Graphics service"]
    HLE --> SND["Audio service"]
    HLE --> INP["Input service"]
    VFS --> HDD
    GFX --> PLAT["Platform backend<br/>windows / linux"]
    SND --> PLAT
    INP --> PLAT
    PLAT --> EXEC
```

---

## 2. 계층과 디렉터리 / Layers and directories

| 계층 / Layer | 경로 / Path | 책임 / Responsibility | 상태 |
| --- | --- | --- | --- |
| HDD 입력 | `include/re2dj/hdd/`, `src/hdd/` | 사용자가 준 디렉터리 검증, 대소문자 무시 경로 해석, 실행 파일 스캔 | **[구현됨]** |
| MAME CHD 입력 | `include/re2dj/storage/mame_chd.h`, `src/storage/mame_chd.cpp` | libchdr 기반 CHD header·metadata·hunk·sector read-only adapter | **[구현됨]** |
| CHD hunk 캐시 | `include/re2dj/storage/chd_hunk_cache.h`, `src/storage/chd_hunk_cache.cpp` | 압축 해제된 hunk의 LRU 캐시. libchdr `chd_read`는 호출마다 다시 압축을 풀므로 반복 판독을 이 계층이 흡수한다 | **[구현됨]** |
| FAT32 CHD 파일시스템 | `include/re2dj/storage/fat32_chd.h`, `src/storage/fat32_chd.cpp` | MBR/BPB/FAT chain/file-range read-only view와 실행 파일 staging. 디렉터리·경로·클러스터 체인 캐시와 판독 API 직렬화 포함 | **[구현됨]** |
| FAT 디렉터리 이름 해독 | `include/re2dj/storage/fat32_directory_name.h`, `src/storage/fat32_directory_name.cpp` | 8.3 짧은 이름, 짧은 이름 체크섬, 긴 이름 슬롯 조립. 이미지 없이 합성 항목으로 시험 가능 | **[구현됨]** |
| 게스트 경로 | `include/re2dj/storage/`, `src/storage/` | Win32 경로 파싱·정규화, 드라이브 문자 매핑, overlay 정책과 파일 테이블 | **[구현됨]** |
| 게스트 현재 디렉터리 | `src/platform/windows/injected_runtime.cpp` | `SetCurrentDirectoryA`/`GetCurrentDirectoryA` HLE와 상대 이름 해석. 호스트 프로세스 작업 디렉터리는 바꾸지 않는다 | **[구현됨]** |
| 게스트 디렉터리 검색 | `src/platform/windows/injected_runtime.cpp` | `FindFirstFileA`/`FindNextFileA`/`FindClose` HLE 및 wildcard(*, ?) 매칭. directory-backed HDD는 게스트 CWD 기준 native 검색 패턴을 사용하고, CHD는 합성 handle을 사용한다 | **[구현됨]** |
| 실행 파일 분석 | `include/re2dj/exe/`, `src/exe/` | PE32 헤더·섹션·디렉터리 판독 | **[구현됨]** (헤더·섹션), **[계획]** (import/reloc) |
| 타깃 프로파일 | `include/re2dj/target/`, `src/target/` | 버전별 실행 파일 경로, 작업 디렉터리, HLE 프로파일 ID | **[구현됨]** (자료구조·감지·1st/2nd/3rd/4th/5th/6th 프로파일) |
| 런타임 | `include/re2dj/runtime/`, `src/runtime/` | 게스트 주소 공간, 레지스터 컨텍스트, 실행 backend 인터페이스 | **[계획]** |
| HLE | `include/re2dj/hle/`, `src/hle/` | import binding registry와 x86 ABI dispatcher. 실제 kernel32/user32/gdi32/ddraw/dsound/dinput binding은 후속 | **[부분 구현]** |
| HLE — Hardlock | `include/re2dj/hle/hardlock/`, `src/hle/hardlock/` | Hardlock 장치 경계. 네 IOCTL 응답, descriptor 판독, 응답 매핑 적용 | **[구현됨]** |
| 런타임 로깅 | `include/re2dj/logging/`, `src/logging/` | spdlog stderr/file multi-sink, 실행별 파일, immediate flush, fatal 분류 | **[구현됨]** |
| 캐비닛 I/O 보드 | `include/re2dj/input/`, `src/input/` | 제품별 보드 모델과 port bus. EZ2DJ는 byte 폭 `0x100`–`0x106`, EZ2Dancer는 word 폭 `0x300`–`0x30c`로 서로 다른 파일에 있다. 폭은 프로파일의 `legacy_io_width`가 정하고, privileged fault handler가 `0x66` prefix를 해석해 해당 bus로 보낸다 | **[구현됨]** |
| 설정 | `include/re2dj/config/`, `src/config/` | INI 파싱, 키 바인딩, 실행 옵션 | **[계획]** |
| SDL host | `src/platform/sdl/` | in-process 러너의 창·OSD·키보드·게임패드·소리. SDL만 쓰며 두 OS 공용(작업 447) | **[구현됨: Linux, Windows]** |
| in-process 러너 | `src/platform/native/` | 원본 PE32를 re2dj 프로세스 안에서 실행하는 OS 중립 러너. OS 헤더 없이 `native_host_services.h`와 backend 계약만 쓴다(작업 446). Windows x86 backend(`src/platform/windows/x86/`, 작업 448)는 VEH, 그림자 TEB, fs:0 동기화로 같은 계약을 구현하며 합성 PE32 probe를 통과한다 | **[구현됨: Linux, Windows]** |
| 플랫폼 | `src/platform/{windows,linux}/` | OS 전용이며 host 비트 폭 중립 또는 x86/x64 공용인 구현. 비트 폭 전용 구현은 각 OS 아래 `x86/`·`x64/`로 분리 | **[부분 구현]** |
| 런처 | `include/re2dj/launcher/`, `src/launcher/`, `src/ui/launcher_screen.cpp`, `src/platform/sdl/launcher_window.cpp`, `src/host/cli/launcher_session.cpp` | 인자 없이 실행했을 때의 프로필 선택 화면(#12). 내장 프로필의 가용 여부와 `cfg/re2dj.ini` 설정은 공용 코어, ImGui 화면과 SDL3 창은 SDL host. 고른 프로필은 자기 실행 파일을 새 프로세스로 실행하고(`platform/self_process.h`) 끝나면 목록으로 돌아온다 | **[구현됨: Linux, Windows]** |
| 런처 업데이트 | `include/re2dj/update/`, `src/update/`, `platform/https_download.h`(`linux/`: 시스템 `curl`, `windows/`: WinHTTP), `platform/self_process.h` | GitHub 최신 릴리스 확인, 이 빌드용 아카이브 받기·SHA-256 검증·풀기(tar.gz는 `tinfl`, zip은 miniz), 실행 파일 옆 `VERSION`이 맞는 릴리스 설치에서만 제자리 교체·되돌림, 다시 시작(Linux `execv`로 같은 PID, Windows는 자식 실행)(#17, rePIU #48) | **[구현됨: Linux, Windows]** |
| 호스트 | `src/host/cli/` | 명령행 진입점 | **[구현됨]** |
| 분석 | `include/re2dj/analysis/`, `src/analysis/` | 바이트 열 통계 판정. 파일도 프로세스도 모르는 순수 측정 | **[구현됨]** |
| 도구 | `src/tools/{hdd_probe,chd_probe,pe_analyzer,code_score}/` | 비실행 HDD·CHD·PE·바이트 통계 분석 도구 | **[구현됨]** |

*The table above maps each layer to its directory, responsibility, and current status.*

플랫폼 디렉터리는 OS와 host 비트 폭을 별도 축으로 표현합니다. `src/platform/windows/`와 `src/platform/linux/` 루트에는 해당 OS 전용이면서 32비트·64비트에 중립인 코드 또는 양쪽이 공유하는 interface·policy·orchestration만 둡니다. 특정 host 비트 폭의 native ABI·register·instruction·pointer width에 종속되는 구현은 각각 `x86/` 또는 `x64/`에 둡니다. 공개 플랫폼 구현 header도 `include/re2dj/platform/<os>/x86/`·`x64/`로 같은 구조를 따릅니다. PE32와 `GuestAddress`처럼 게스트 의미만 32비트인 코드는 host 양쪽에서 공용이면 OS 루트에 남습니다. 현재 Windows x86 runtime과 Linux i386 helper 파일의 실제 이동은 build와 runtime 경계를 함께 갱신하는 후속 구조 작업입니다. 근거: [플랫폼 비트 폭 디렉터리 설계](docs/design/20260921-347-platform-architecture-directories.md).

*Platform directories express OS and host bit width as separate axes. The roots of `src/platform/windows/` and `src/platform/linux/` contain only OS-specific code neutral across 32-bit and 64-bit hosts, or interfaces, policies, and orchestration shared by both. Implementations dependent on a specific host width's native ABI, registers, instructions, or pointer width belong under `x86/` or `x64/`. Public platform-implementation headers mirror the structure under `include/re2dj/platform/<os>/x86/` and `x64/`. Code whose guest meaning alone is 32-bit, such as PE32 handling and `GuestAddress`, stays at the OS root when shared by both hosts. Actual movement of the current Windows x86 runtime and Linux i386-helper files is a later structural task that updates build and runtime boundaries together. See the [platform bit-width directory design](docs/design/20260921-347-platform-architecture-directories.md).*

제품 CLI와 공용 HLE dispatcher의 host 진단 로깅은 **[구현됨]** 상태입니다. `re2dj_logging`은 같은 event를 stderr color sink와 실행별 `logs/re2dj-<timestamp>.log` file sink에 보내고 모든 레벨을 즉시 flush합니다. 패턴은 `[%X.%e] [%8l] [%n] %v`이며, 미등록 import와 지원되지 않는 실행 경계는 spdlog `critical` 및 명시적 `FATAL <분류>`로 기록합니다. Fatal 심각도는 진단 분류이고, ABI-safe continuation 여부와 프로세스 종료 정책은 호출 경계에서 별도로 결정합니다. stdout에는 help·version·사용법만 남습니다. 대상 표, Linux 실행의 호출 기록과 경계, Windows launcher의 오류와 요약은 모두 logger로 갑니다. launcher의 JSONL 실행 기록은 spdlog 파일 sink(`%v`)로 쓰므로 형식이 그대로입니다(작업 365). 게스트 process 안의 injected runtime도 `WriteRuntimeLog`의 네 channel(runtime·VFS·graphics·audio)로 spdlog 파일에 기록합니다. runtime channel은 launcher가 읽는 `OutputDebugStringA` 문구를 그대로 내보낸 뒤 `*.runtime.log`에도 남기고, 기록 I/O는 게스트의 last error를 보존합니다(작업 366). 형식이 고정된 데이터 dump는 이 경로를 쓰지 않습니다. 근거: [설계](docs/design/20260921-345-spdlog-runtime-logging.md), [작업 365 설계](docs/design/20260925-365-host-run-output-spdlog.md), [작업 366 설계](docs/design/20260925-366-injected-runtime-spdlog.md).

*Host diagnostic logging for the product CLI and shared HLE dispatcher is **[Implemented]**. `re2dj_logging` sends each event to both a stderr color sink and a per-run `logs/re2dj-<timestamp>.log` file sink, flushing every level immediately. The pattern is `[%X.%e] [%8l] [%n] %v`; unknown imports and unsupported execution boundaries use spdlog `critical` plus an explicit `FATAL <classification>`. Fatal severity is a diagnostic classification, while ABI-safe continuation and process termination remain decisions of the calling boundary. Only help, version, and usage remain on stdout; the target table, the Linux run's call record and boundary, and the Windows launcher's errors and summary all go to the logger, and the launcher's JSONL run record is written through an spdlog file sink (`%v`), keeping its format (Task 365). The injected runtime inside the guest process also records to spdlog files through the four channels of `WriteRuntimeLog` (runtime, VFS, graphics, audio); the runtime channel still emits the `OutputDebugStringA` text the launcher reads and also writes `*.runtime.log`, and recording I/O preserves the guest's last error (Task 366). Fixed-format data dumps do not use this path. See the [design](docs/design/20260921-345-spdlog-runtime-logging.md) and the [Task 365 design](docs/design/20260925-365-host-run-output-spdlog.md), and the [Task 366 design](docs/design/20260925-366-injected-runtime-spdlog.md).*

게스트 디렉터리 검색은 호스트 프로세스 작업 디렉터리에 의존하지 않는다. 상대 검색 패턴은 directory-backed HDD와 CHD 양쪽에서 논리 게스트 현재 디렉터리와 결합한다. 검색 이름은 **해석 전에** 마지막 구성요소를 패턴으로 떼어 내고 디렉터리 부분만 게스트 경로 해석에 넘긴다. 게스트 경로 파서는 `*`와 `?`를 파일명에 쓸 수 없는 문자로 거부하므로, 이름 전체를 넘기면 해석이 실패하고 현재 디렉터리를 잃는다. 따라서 `SetCurrentDirectoryA("System\Title")` 이후 검색은 directory-backed에서 `ez2dj\System\Title\*.*`, CHD에서 `EZ2DJ/System/Title`을 대상으로 한다. CHD 경로의 앞 구성요소는 고정값이 아니라 프로파일의 `executable_relative_path`에서 온다. `ez2d2m`은 `ez2dancer`이며, 이 값이 고정이던 동안에는 그 target의 CHD 조회가 모두 빗나갔다. CHD에서는 FAT32 디렉터리 항목을 합성 handle에 보관하고, directory-backed native handle의 `FindNextFileA`/`FindClose`는 원래 Win32 API로 전달한다. overlay와 HDD 디렉터리 항목을 병합하는 검색은 아직 범위 밖이다.

*Guest directory enumeration does not depend on the host process working directory. A relative search pattern is combined with the logical guest current directory on both a directory-backed HDD and a CHD. A search name has its final component split off as the pattern **before** resolution, and only the directory part is handed to guest-path resolution: the parser rejects `*` and `?` as filename characters, so passing the whole name fails and loses the current directory. After `SetCurrentDirectoryA("System\\Title")` the search therefore targets `ez2dj\\System\\Title\\*.*` on a directory-backed HDD and `EZ2DJ/System/Title` in a CHD. The leading component of a CHD path is not fixed but comes from the profile's `executable_relative_path`: `ez2d2m` uses `ez2dancer`, and while that value was hard-coded every CHD lookup for that target missed. CHD enumeration stores FAT32 directory entries in a synthetic handle, while `FindNextFileA`/`FindClose` for directory-backed native handles continue through the original Win32 APIs. Merging overlay and HDD directory entries remains out of scope.*

게스트가 자기 루트를 부르는 이름은 프로파일의 `guest_drive_letter`와 `guest_directory`에서 온다. launcher가 그 값을 injected runtime의 `g_re2dj_vfs_guest_root`에 쓰고 bootstrap child에도 같은 값을 전달한다. 드라이브 문자는 제품이 아니라 덤프의 성질이다. 1st SE의 CHD는 `C:\ez2dj`에서 부팅하고 그 앞의 덤프들은 `D:\ez2dj`를 썼으므로, 게스트 경로를 기록하지 않은 프로파일에는 `D:\ez2dj`가 기본값으로 남는다.

*The name the guest uses for its own root comes from the profile's `guest_drive_letter` and `guest_directory`. The launcher writes it to the injected runtime's `g_re2dj_vfs_guest_root` and passes the same value to a bootstrap child. The drive letter is a property of the dump rather than the product: 1st SE's CHD boots from `C:\ez2dj` while earlier dumps used `D:\ez2dj`, which stays the default for profiles that record no guest path.*

설정 계층의 Hardlock 비밀 설정 경계는 **[구현됨]** 상태입니다. `HardlockSecretMaterial`은 선택 프로파일의 module address와 세 seed를 프로세스 memory 안에서만 보유합니다. ez2dj4th는 명시적 옵션이 없으면 Git-ignore된 `cfg/hardlock.ini`의 해당 profile section을 읽으며, 저장소 내부 경로는 `cfg/` 아래만 허용합니다. 명시적인 저장소 외부 경로도 지원합니다. launcher는 값을 명령행이나 로그에 넣지 않고 Windows x86 injected runtime export에 직접 쓰며, 다른 프로파일은 이 설정을 거부합니다.

*The Hardlock secret-configuration boundary in the configuration layer is **[Implemented]**. `HardlockSecretMaterial` retains the selected profile's module address and three seeds only in process memory. Without an explicit option, ez2dj4th reads its profile section from Git-ignored `cfg/hardlock.ini`; repository-internal paths are allowed only below `cfg/`, while explicit external paths remain supported. The launcher writes values directly to Windows x86 injected-runtime exports without placing them on the command line or in logs, and other profiles reject this configuration.*

플랫폼 중립 `HardlockProtocolTracker`도 **[구현됨]** 상태입니다. 확인된 `0x468 → 0x450 → 0x44c → 0x458` 순서와 exact buffer shape를 추적하고 descriptor의 Function, block count 및 configured module-address 일치 여부를 값 비노출 boolean으로 판정합니다. 이 경계는 응답을 합성하지 않습니다. Function `0x0e` bit-level transform과 유효한 `0x450` driver response는 허용 가능한 독립 근거가 없어 아직 **[계획]** 상태입니다.

*The platform-neutral `HardlockProtocolTracker` is also **[Implemented]**. It tracks the confirmed `0x468 → 0x450 → 0x44c → 0x458` order and exact buffer shapes, then validates descriptor function, block count, and configured module-address matching using value-free Booleans. It does not synthesize responses. The Function `0x0e` bit-level transform and a valid `0x450` driver response remain **[Planned]** because no policy-compatible independent basis is available yet.*

Task 130 조사 결과, 2EZConfig-V2는 `API_CRYPT`/Function `0x0e`의 high-level packet 계약을 대조하는 데는 유용하지만 GPL-3.0-or-later 소스이므로 구현 기반으로 사용할 수 없습니다. 현재 유효한 호환 경로 후보는 원본 실행 파일을 변경하지 않고 동적 `CreateFileA`/`DeviceIoControl` 장치 경계 뒤에 profile-selected `HardlockDeviceBackend`를 두는 방식입니다. `FEnteDev` open과 네 IOCTL의 shape/sequence는 확인되었지만, `0x450`의 실제 6바이트 응답과 Function `0x0e`의 8바이트 transform은 아직 미확정이므로 backend가 추측 응답을 생성하지 않는 정책을 유지합니다.

*Task 130 concludes that 2EZConfig-V2 is useful for comparing the high-level `API_CRYPT`/Function `0x0e` packet contract but cannot be used as an implementation basis because its source is GPL-3.0-or-later. The current valid compatibility-path candidate is a profile-selected `HardlockDeviceBackend` behind the dynamic `CreateFileA`/`DeviceIoControl` device boundary, without modifying the original executable. `FEnteDev` opening and the four IOCTL shape/sequence contracts are confirmed, but the real six-byte `0x450` response and eight-byte Function `0x0e` transform remain unresolved; the backend therefore preserves the policy of generating no guessed response.*

Hardlock HLE가 보류된 동안 장치 경계를 관찰하기 위해 플랫폼 중립 `HardlockStubDevice`가 **[구현됨]** 상태로 있습니다. 이 스텁은 동글 에뮬레이션도 우회도 아니며, 선택한 응답에 원본이 어떻게 반응하는지 보는 진단 하네스입니다. Task 134에서 제품 CLI의 합성 응답 옵션을 제거했고, launcher 전용 `--hardlock-stub`으로만 켭니다. 확인된 vendor framing에 맞는 요청만 받아 `0x468`은 성공만, `0x450`은 설정된 replay 또는 요청 보존, `0x44c`/`0x458`은 요청 보존과 status word `0` 정리로 응답합니다. Function `0x0e` payload는 알고리즘 근거가 없으므로 항등으로 통과시키고, 계약을 벗어난 요청은 성공시키지 않습니다. 기본값은 비활성이며 launcher의 명시적 `--hardlock-stub`으로만 켜집니다. 활성 실행은 runtime trace의 `hardlock-stub` 줄과 launcher JSONL `hardlock_stub` 이벤트로 식별되므로, 그 실행에서 관찰한 동작은 원본 동작으로 인용하지 않습니다. 게스트 보호 분기 결과를 강제하는 2단계 우회는 막히는 분기를 계측으로 특정한 뒤 별도로 다룰 **[계획]** 항목입니다.

*While the Hardlock HLE stays deferred, the platform-neutral `HardlockStubDevice` is **[Implemented]** for observing the device boundary. It is neither a dongle emulation nor a bypass but a diagnostic harness for seeing how the original reacts to a chosen response; Task 134 removed the synthetic-response option from the product CLI, leaving only the launcher-only `--hardlock-stub`: it accepts only requests matching the confirmed vendor framing, answering `0x468` with success alone, `0x450` with a configured replay or the preserved request, and `0x44c`/`0x458` with the preserved request plus a cleared status word. The Function `0x0e` payload passes through unchanged because no algorithmic basis exists, and out-of-contract requests are not forced to succeed. It defaults to off and is enabled only by the launcher's explicit `--hardlock-stub` option, never by a profile default. Active runs are identifiable from the runtime trace's `hardlock-stub` line and the launcher's `hardlock_stub` JSONL event, so behavior observed in them is never cited as original behavior. Stage-2 forcing of the guest protection branch stays **[Planned]** until instrumentation identifies the blocking branch.*

실제 CHD bounded 실행으로 스텁이 네 IOCTL을 모두 처리하고 기존 분기 실험의 요청 횟수를 그대로 재현함을 확인했습니다. 다만 제품 CLI 경로는 `ez2dj4th` profile에 `run_detached`가 없어 launcher가 첫 `CreateFileA` handoff에서 원본을 종료하므로 우회를 무장만 하고 실행하지 못합니다. profile의 `run_detached`와 active-console 정책은 별도 결정 사항으로 남아 있습니다.

*A bounded real-CHD run confirms the stub handles all four IOCTLs and reproduces the request counts of the earlier branch experiments. The product CLI path, however, only arms the bypass: `ez2dj4th` carries no `run_detached`, so the launcher terminates the original at the first `CreateFileA` handoff. The profile's `run_detached` and active-console policies remain separate decisions.*

인과 실험 결과 2단계 우회는 취소되었습니다. 4th의 `.text`는 파일에서 암호화되어 있고 실행 중 다시 쓰이며, Function `0x0e` 출력 8바이트만 바꾸면 downstream 실행 경로가 달라집니다. 따라서 뒤집을 보호 분기 하나가 막고 있는 구조가 아니고, 분기 강제로는 올바른 transform을 대체할 수 없습니다. 스텁은 장치 경계 계약 검증과 이후 경로 관찰 수단으로 남습니다.

Task 132는 제품 실행 경로 정책을 정리했습니다. `ez2dj4th` profile은 `run_detached`와 새 WTS 세션 정책(현재 이름 `hle_wts_console_session`, 작업 360에서 `hle_wts_active_console`에서 개명)을 갖습니다. 앞의 것이 없으면 launcher가 첫 VFS 파일 open을 handoff로 보고 원본을 종료하고, 뒤의 것이 없으면 보호 코드가 첫 장치 요청 뒤로 진행하지 않습니다. 두 정책 모두 운영체제 경계에 해당하므로 profile 기본값이며, 장치 경계 스텁은 launcher 전용 진단으로만 켜집니다.

*The causality experiment cancelled stage 2. 4th's `.text` is encrypted on disk and rewritten at runtime, and changing only the eight Function `0x0e` output bytes changes the downstream execution path, so the blocker is not one protection branch to flip and branch forcing cannot substitute for a correct transform. The stub remains as a device-boundary contract check and a way to observe later paths.*

*Task 132 settles the product run policy: the `ez2dj4th` profile now carries `run_detached` and the new WTS session policy (now `hle_wts_console_session`, renamed from `hle_wts_active_console` in Task 360). Without the first, the launcher treats the first VFS file open as the handoff and terminates the original; without the second, the protection does not advance past its first device request. Both are operating-system boundaries and therefore profile defaults, while the device-boundary stub is enabled only as a launcher-only diagnostic.*

Task 133은 사용자 질문 "Hardlock을 건너뛰고 진입점으로 직행할 수 있는가"에 답했습니다. 불가능합니다. `.text` 901,120바이트는 전 구간 엔트로피 `7.997`의 암호문이고 평문 구간이 없으며, 보호 코드가 실행 중 이를 다시 씁니다. 또한 `0x450` 응답은 byte 2·3의 16비트 정확 일치만 검사하므로 통과값은 이미 알고 있고, 남은 blocker는 Function `0x0e` 하나입니다. Hardlock은 게임 앞의 검문소가 아니라 게임을 복호화하는 열쇠 공급원입니다.

*Task 133 answers whether the Hardlock boundary can be skipped to reach the entry point directly: it cannot. `.text`'s 901,120 bytes are ciphertext at entropy `7.997` throughout with no plaintext region, and the protection rewrites them at runtime. The `0x450` response is checked only as a 16-bit exact match on bytes 2 and 3, so a passing value is already known and the remaining blocker is Function `0x0e` alone. Hardlock is the key source that decrypts the game, not a checkpoint in front of it.*

Task 134는 이에 맞춰 이름을 바로잡았습니다. 제품 CLI의 `--hardlock-bypass`를 제거하고, 이 구성요소를 launcher 전용 `--hardlock-stub` 진단 하네스로 재정의했습니다. 같은 작업에서 `--hardlock-descriptor-ids` 진단으로 4th의 module address `0x4c53`과 두 descriptor ID를 두 실행에서 동일하게 확보했습니다. 이 값들은 seed 복구 제약식의 입력입니다.

*Task 134 corrects the naming accordingly: `--hardlock-bypass` is removed from the product CLI and the component is rescoped as the launcher-only `--hardlock-stub` diagnostic harness. The same task used the `--hardlock-descriptor-ids` diagnostic to capture 4th's module address `0x4c53` and both descriptor IDs identically across two runs, as inputs to seed-recovery constraints.*

Task 135는 응답 주입 경계를 추가했습니다. 플랫폼 중립 `hardlock_transform_responses`가 challenge→response 매핑 파일을 읽고, 스텁이 `0x458`의 각 block을 조회해 맞으면 출력에 씁니다. 응답은 이 저장소 밖에서 계산하며 re2DJ는 링크도 실행 중 통신도 하지 않고 데이터 파일만 읽습니다. 같은 작업에서 4th의 36개 challenge가 이미지의 32 KiB 청크 시작 8바이트임을 확정했습니다. `.text` 28개, `.rdata` 2개, `.data` 4개, `.reloc` 2개이며 `.idata`와 `.protect`는 제외됩니다. 따라서 보호 대상은 `.text` 하나가 아니라 이미지 전체입니다.

*Task 135 adds the response injection boundary: the platform-neutral `hardlock_transform_responses` reads a challenge-to-response map file and the stub looks up each `0x458` block, writing the mapped output when found. Responses are computed outside this repository; re2DJ neither links nor communicates with that program at run time and reads only a data file. The same task established that 4th's 36 challenges are the first eight bytes of each 32 KiB chunk of the image — 28 in `.text`, 2 in `.rdata`, 4 in `.data`, and 2 in `.reloc`, excluding `.idata` and `.protect` — so the protected region is the whole image rather than `.text` alone.*

Task 136은 그 경계를 문서화된 계약으로 고정했습니다. [reSoftlock 인터페이스 계약](docs/design/20260902-136-resoftlock-interface-contract.md)이 응답 매핑 파일 형식, challenge 목록과 seed 후보 목록 형식, 요구 실행 모드, 비밀값 취급과 결정성 요구사항을 정의합니다. challenge 유도 규칙이 원본 실행 파일만으로 관찰 목록을 값과 순서까지 재현함을 확인했으므로, 두 프로그램의 접촉면은 응답 매핑 파일 하나로 줄어듭니다.

*Task 136 pins that boundary as a documented contract. The [reSoftlock interface contract](docs/design/20260902-136-resoftlock-interface-contract.md) defines the response map format, the challenge list and seed candidate list formats, the required modes, and the secret-handling and determinism requirements. Because the challenge derivation rule reproduces the observed list from the original executable alone in both value and order, the contact surface between the two programs narrows to a single response map file.*

### Hardlock HLE의 성격 / What the Hardlock HLE is

이 계층은 **원본이 dongle에게 묻는 네 요청에 규격대로 답하는 장치 경계**입니다. 암호 연산은 하지 않습니다.

| 나눔 | 담당 |
| --- | --- |
| 프로토콜, packet framing, descriptor 판독, 형태 검증 | **re2DJ (이 계층)** |
| 응답 값 계산 | 저장소 밖 별도 프로그램 |
| 값 보관 | 사용자의 `cfg/` (Git 제외) |

제공하는 것은 네 IOCTL에 대한 응답과 장치의 존재입니다. `0x468`은 빈 입출력이면 성공, `0x450`은 6바이트 응답, `0x44c`는 status word를 0으로 두고 Function 0에 한해 tail word를 기록, `0x458`은 8바이트 block마다 challenge로 조회한 응답을 씁니다. 게스트가 profile이 지정한 장치 경로를 열면 합성 handle을 돌려주고 그 handle의 파일 API를 가로챕니다.

**dongle 에뮬레이터가 아닙니다.** Function `0x0e` 변환도, seed에서 응답을 유도하는 경로도 이 저장소에 없습니다. 이 방식이 성립하는 이유는 challenge 집합이 실행 파일에서 결정되는 고정값이기 때문입니다. 3rd 32개, 4th 36개이므로 오프라인에서 한 번 계산한 표로 충분합니다. 따라서 한계도 그대로 따라옵니다. 표에 없는 challenge는 추측하지 않고 입력을 그대로 통과시키며(`unmapped` 수로 드러남), 실행 파일이 바뀌면 challenge가 바뀌므로 표를 다시 만들어야 하고, 임의의 challenge를 던지는 프로그램은 지원하지 못합니다.

*The Hardlock HLE is a **device boundary that answers, to specification, the four requests the original asks its dongle**; it performs no cryptography. re2DJ owns the protocol, packet framing, descriptor reading, and shape validation, while a separate program outside this repository computes the response values and the user keeps them under Git-ignored `cfg/`. What it provides is those four answers plus the device's existence: `0x468` succeeds on empty buffers, `0x450` writes a six-byte response, `0x44c` clears the status word and writes the tail word for Function 0 only, and `0x458` replaces each eight-byte block with the response looked up by challenge; opening the profile's device path returns a synthetic handle whose file APIs are intercepted. **It is not a dongle emulator**: neither the Function `0x0e` transform nor any seed-to-response derivation exists here. The approach works only because the challenge set is fixed by the executable — 32 for 3rd and 36 for 4th — so one offline table suffices, and the limits follow directly: a challenge outside the table is passed through unchanged rather than guessed (visible as the `unmapped` count), a different executable needs a new table, and a program issuing arbitrary challenges cannot be served.*

Task 247은 응답 표에 **요청 행**을 추가했습니다. 16자리 행은 지금처럼 블록 하나를 키로 삼는 블록 행이고, 2–16블록을 덮는 더 긴 행은 같은 블록 수의 transform payload 전체에 적용되는 요청 행입니다. 요청 행의 바이트는 `??`일 수 있으며, 입력에서는 비교하지 않고 출력에서는 게스트 값을 유지한다는 뜻입니다. 맞는 요청 행이 있으면 그 요청에는 블록 조회를 하지 않고, 없으면 기존 동작 그대로입니다. 두 행이 같은 payload에 동시에 맞을 수 있는 map은 파싱 단계에서 거절합니다. 필요한 이유는 `ez2d2m`과 6th의 Function `0x0011`(`API_CODE`)입니다. 응답이 블록마다 독립이 아니고, 입력 일부가 실행마다 바뀝니다. 이 계층은 여전히 아무것도 계산하지 않으며 runtime 용량(블록 행 256, 요청 행 8)을 넘는 map은 launcher가 거절합니다. 근거는 [설계](docs/design/20260911-247-hardlock-payload-response-rows.md)에 있습니다.

*Task 247 adds **request rows** to the response table. A sixteen-digit row is still a block row keyed on one block; a longer row covering 2 to 16 blocks is a request row applied to the whole payload of a transform with that many blocks. Its bytes may be `??`, meaning not compared in the input and kept from the guest in the output. A matching request row takes the request without any block lookup, and without one the behaviour is exactly as before. A map in which two rows could match the same payload is rejected at parse time. The need comes from Function `0x0011` (`API_CODE`) on `ez2d2m` and 6th, whose answer is not independent per block and part of whose input changes between runs. The layer still computes nothing, and the launcher refuses a map beyond the runtime capacity of 256 block rows and 8 request rows. The rationale is in [the design](docs/design/20260911-247-hardlock-payload-response-rows.md).*

Task 140은 판별된 재료를 제품 실행 경로로 승격했습니다. `ez2dj3rd`와 `ez2dj4th` profile이 `hardlock_cfg_material_default`를 켜면, launcher 진입점이 `cfg/hardlock-<profile-id>.map`과 `cfg/hardlock.ini`의 선택 키 `response450`·`tail44c`을 읽습니다. 제품 CLI가 같은 진입점을 호출하므로 두 경로가 한 규칙을 공유합니다. 세 재료는 전부 함께 적용되거나 전혀 적용되지 않습니다. 매핑 없이 replay만 적용하면 보호가 모달 대화상자에서 멈추기 때문입니다. 합성 `0x450`/`0x44c` 값은 코드 상수로 올리지 않았고, 저장소에는 경로 규칙과 읽는 코드만 있습니다. 같은 작업에서 3rd profile의 `hle_dynamic_vfs`와 `hle_wts_active_console`(현재 `hle_wts_console_session`)을 켰습니다. 둘이 없으면 3rd 제품 경로가 보호 장치에 도달하지 못합니다.

*Task 140 promotes the identified material to the product execution path. With `hardlock_cfg_material_default` on for the `ez2dj3rd` and `ez2dj4th` profiles, the launcher entry point reads `cfg/hardlock-<profile-id>.map` and the optional `response450` and `tail44c` keys from `cfg/hardlock.ini`; the product CLI calls that same entry point, so both paths share one rule. The three materials are applied all together or not at all, because applying the replay without a map leaves the protection at a modal dialog. No synthetic `0x450` or `0x44c` value was promoted to a code constant — the repository carries only the path convention and the reading code. The same task enabled `hle_dynamic_vfs` and `hle_wts_active_console` (now `hle_wts_console_session`) on the 3rd profile, without which 3rd's product path cannot reach the protection device.*

Task 251은 독자적인 클린룸 C++20 `HardlockEngine`(`re2dj::hle::hardlock::HardlockEngine`)을 HLE 계층에 통합했습니다. 이 엔진은 BSD 3-Clause 라이선스에 따라 독자 구현되었으며, 8바이트 블록 암호화(`CryptBlock`) 및 56바이트 `API_CODE` 페이로드 계산(`CodePayload`)을 실시간으로 수행합니다. `HardlockDevice`는 설정된 시드(`module_address`, `seed1`, `seed2`, `seed3`)가 주어지면 정적 매핑 파일 대신 실시간 암호화 연산으로 동적 응답을 생성하며, 시드가 없을 때는 기존 정적 맵 테이블로 fallback합니다. 또한 `reSoftlock`의 통합 분석 아티팩트(`hardlock_analysis.json`)를 파싱하여 `id_ref` → `id_verify` 변환을 통해 올바른 시드셋을 검증 및 확정하는 `seed_config` 유틸리티를 제공합니다.

*Task 251 integrates a clean-room C++20 `HardlockEngine` (`re2dj::hle::hardlock::HardlockEngine`) into the HLE layer. Implemented independently under the BSD 3-Clause license, this engine performs real-time 8-byte block encryption (`CryptBlock`) and 56-byte `API_CODE` payload generation (`CodePayload`). When configured seeds (`module_address`, `seed1`, `seed2`, `seed3`) are provided, `HardlockDevice` dynamically computes responses in real time without precalculated static maps, falling back to static tables when seeds are absent. It also provides `seed_config` utilities to consume `reSoftlock`'s unified analysis artifact (`hardlock_analysis.json`) and verify/confirm the correct seed set via `id_ref` → `id_verify` transformation.*

작업 360은 Hardlock HLE를 Win32에 붙이던 연결부를 공용 코어로 옮겼습니다. `hle/hardlock/device_material`은 프로파일 정책, `cfg/hardlock.ini`, 기본 응답 map, 명시 옵션을 합쳐 장치 재료를 만듭니다. 명시 옵션이 우선이고, 파일이 없는 것은 오류가 아닙니다. 결과는 `MakeHardlockDeviceOptions`로 장치 옵션이 됩니다. `hle/hardlock/device_call`은 장치 결과를 `DeviceIoControl`의 Win32 반환값(`BOOL`, 반환 byte, last error)으로 바꾸고, 요청 통계와 trace 한 줄을 만듭니다. `hle/guest_device_path`는 게스트 장치 이름의 prefix 판정입니다. Windows launcher와 주입 runtime은 이 셋을 쓰고, 원격 프로세스 전달(전역 변수 pack/unpack)과 고정 배열 용량 검사만 Windows 쪽에 남습니다. 실제 4th의 Hardlock 요청 기록은 변경 전후로 같습니다. 같은 작업에서 WTS 정책의 이름을 `hle_wts_console_session`으로 바로잡았습니다. 보호 코드가 묻는 class 4는 `WTSSessionId`이고, HLE는 현재 세션 번호를 0으로 보고합니다.

*Task 360 moves the wiring that attached the Hardlock HLE to Win32 into the shared core. `hle/hardlock/device_material` combines the profile policy, `cfg/hardlock.ini`, the default response map, and explicit options into device material, with explicit options first and missing files not an error, and `MakeHardlockDeviceOptions` turns it into device options. `hle/hardlock/device_call` maps a device result to the Win32 `DeviceIoControl` result (`BOOL`, bytes returned, last error) and produces request counts and the trace line. `hle/guest_device_path` is the guest device-name prefix match. The Windows launcher and injected runtime use all three, leaving only cross-process transport (global pack/unpack) and the fixed-array capacity check on the Windows side; the real 4th's Hardlock request record is identical before and after. The same task renames the WTS policy to `hle_wts_console_session`: the protection's class-4 query is `WTSSessionId`, and the HLE reports the current session ID as 0.*

작업 361은 이 공용 코드를 Linux facade에 연결했습니다. 플랫폼 중립 `hle::GuestDeviceSet`이 게스트가 여는 장치와 handle(`0x1004`부터 4씩)을 소유하고, Hardlock 요청을 `CompleteHardlockDeviceIoControl`로 처리합니다. `ImportCallServices`는 선택 서비스(guest byte 읽기·쓰기, `Devices()`, last error)를 기본 구현과 함께 얻었습니다. `kernel32` facade는 `CreateFileA`(장치 열기), `DeviceIoControl`(같은 주소·크기의 buffer는 in-place), `CloseHandle`, `GetLastError`, `SetLastError`를 제공합니다. Linux CLI는 대상 프로파일과 사용자 `cfg/hardlock.ini`에서 장치 설정을 만들고, 실행 끝에 재료 적용 여부와 요청 종류별 횟수만 출력합니다. 실제 4th는 Linux 두 폭에서 `\.\FEnteDev`를 열고 initialize `0x9c402468`을 처리한 뒤, `GetProcAddress(user32, "CreateCursor")`에서 멈춥니다.

*Task 361 connects this shared code to the Linux facade. The platform-neutral `hle::GuestDeviceSet` owns the devices the guest opens and their handles (from `0x1004` in steps of four) and answers Hardlock requests through `CompleteHardlockDeviceIoControl`. `ImportCallServices` gains optional services — guest byte reads and writes, `Devices()`, and last error — with default implementations. The `kernel32` facade provides `CreateFileA` (device opens), `DeviceIoControl` (in-place for buffers of the same address and size), `CloseHandle`, `GetLastError`, and `SetLastError`. The Linux CLI builds the device configuration from the target profile and the user's `cfg/hardlock.ini`, printing only whether material was applied and the request counts per kind. On both Linux widths the real 4th opens `\.\FEnteDev`, has its initialize `0x9c402468` answered, and stops at `GetProcAddress(user32, "CreateCursor")`.*

작업 363은 Hardlock API가 시작할 때 쓰는 환경을 더했습니다. 플랫폼 중립 `hle::GuestProcess`는 게스트 process ID(`0x0F00`), error mode, heap을 가집니다. heap의 bookkeeping은 공용 코드가 하고, 영역은 플랫폼이 잡습니다. Linux는 4 GiB 미만에 1 MiB를 잡습니다. handler는 선택 서비스 `Process()`와 `IsGuestModule()`로 이것을 씁니다. 실제 Windows에도 없는 이름은 데이터로 선언합니다. 하나는 module별 `absent_exports`(`kernel32`의 `IsTNT`, `Borland32`)이고, 다른 하나는 `IsAbsentGuestModule`(`wfapi.dll`)입니다. continuation은 이 이름들의 NULL `GetProcAddress`·`LoadLibraryA`에서 멈추지 않습니다. 그 밖의 NULL은 경계로 남습니다. 해석만 관찰된 export(`CreateCursor`, `GetTickCount`, 레지스트리 함수 등)는 공용 `UnimplementedExport`로 주소만 가집니다. 호출되면 export 이름을 남기고 멈춥니다. 새 facade `wtsapi32`는 현재 세션 번호 0을 heap block으로 돌려줍니다. 실제 4th는 Linux 두 폭에서 handshake 두 번과 descriptor 한 번을 마치고, `GetProcAddress(kernel32, "OpenProcess")`에서 멈춥니다.

*Task 363 adds the environment the Hardlock API uses at startup. The platform-neutral `hle::GuestProcess` holds the guest process ID (`0x0F00`), the error mode, and a heap whose bookkeeping is shared while the platform provides the region (1 MiB below 4 GiB on Linux); handlers reach it through the optional `Process()` and `IsGuestModule()` services. Names real Windows lacks too are declared as data — per-module `absent_exports` (`IsTNT` and `Borland32` for `kernel32`) and `IsAbsentGuestModule` (`wfapi.dll`) — and continuation does not stop on a NULL `GetProcAddress`/`LoadLibraryA` for them, while any other NULL remains a boundary. Exports only seen resolved (`CreateCursor`, `GetTickCount`, the registry functions, and so on) get an address through the shared `UnimplementedExport` and stop naming the export when called. The new `wtsapi32` facade returns current session ID 0 in a heap block. On both Linux widths the real 4th completes two handshakes and one descriptor and stops at `GetProcAddress(kernel32, "OpenProcess")`.*

작업 364는 게스트의 자기 process 메모리를 더했습니다. 공용 `GuestHandleAllocator`가 장치와 process handle에 한 공간(`0x1004`부터 4씩)을 줍니다. `GuestProcess`는 image region과 `VirtualAlloc` region의 page별 보호·commit 상태를 기록하고, `VirtualAlloc/Free/Protect`를 판단합니다. image의 보호 값은 Windows loader와 같습니다(쓰기 섹션은 `WRITECOPY`). 기록한 보호 값은 게스트에게 알려 주기만 하고, host 메모리에는 적용하지 않습니다. Linux는 4 GiB 미만에 16 MiB arena를 잡습니다. 실제 4th는 두 폭에서 image 복호화 루프를 끝까지 돕니다. Hardlock 요청은 Windows와 같은 descriptor 37·transform 36입니다. 실행은 두 번째 층의 `GetProcAddress(kernel32, "GetCurrentThreadId")`에서 멈춥니다.

*Task 364 adds the guest's own-process memory. The shared `GuestHandleAllocator` gives devices and process handles one space (from `0x1004` in steps of four). `GuestProcess` records per-page protection and commit state for the image and `VirtualAlloc` regions and decides `VirtualAlloc/Free/Protect`; image protections follow the Windows loader (`WRITECOPY` for writable sections) and are reported to the guest without being applied to host memory. Linux maps a 16 MiB arena below 4 GiB. On both widths the real 4th runs its image decryption loop to the end, with the same 37 descriptors and 36 transforms as Windows, and stops in the second layer at `GetProcAddress(kernel32, "GetCurrentThreadId")`.*

작업 367에서 실제 4th는 envelope를 끝내고 원본 프로그램으로 넘어갑니다. envelope는 원본 import 표(kernel32, user32, gdi32, advapi32, winmm, dsound, dinput, ddraw, avifil32, ws2_32)를 다시 만들고, `ExitProcess`에 hook을 건 뒤, 32.768초 주기의 thread timer를 둡니다. 그 다음 `.text`의 CRT가 `GetVersion`과 `HeapCreate`를 부릅니다. 관찰만 되고 호출되지 않은 export는 공용 `ResolveOnlyExport` 표로 주소만 가집니다. facade module도 `GuestProcess`의 image region이라, 게스트가 system DLL처럼 보호를 바꾸고 패치할 수 있습니다. 이 때문에 facade code section은 host에서 RWX입니다(작업 343의 R/RX 규칙을 대체). `Read/WriteProcessMemory`는 자기 process의 접근 가능한 page를 복사하고, `SetTimer`는 thread timer를 기록합니다. 전달은 message loop가 생길 때 연결합니다.

*In Task 367 the real 4th finishes the envelope and passes to the original program. The envelope rebuilds the original import table (kernel32, user32, gdi32, advapi32, winmm, dsound, dinput, ddraw, avifil32, ws2_32), hooks `ExitProcess`, and sets a 32.768-second thread timer; the CRT in `.text` then calls `GetVersion` and `HeapCreate`. Exports only observed resolved get an address through the shared `ResolveOnlyExport` table. Facade modules are `GuestProcess` image regions too, so the guest can change their protection and patch them as with a system DLL, which makes facade code sections RWX on the host (replacing Task 343's R/RX rule). `Read/WriteProcessMemory` copy accessible pages of the own process, and `SetTimer` records thread timers, whose delivery waits for a message loop.*

작업 368에서 원본 MSVC CRT 시작을 마칩니다. 그 과정에 필요한 것들을 추가했습니다.

- `GuestHeap`이 process heap과 `HeapCreate` heap을 맡습니다. Linux arena는 512 MiB이고 `MAP_NORESERVE`로 잡습니다.
- 주 image의 게스트 경로를 공용 `target::GuestExecutablePath`로 만듭니다. 명령줄, `GetModuleFileNameA`, `GetModuleHandleA(NULL)`이 이것을 씁니다.
- 빈 환경과 code page 949를 제공합니다. 949의 단일 byte 변환, `CT_CTYPE1`, case mapping은 이 host에서 Win32를 직접 불러 잰 값입니다.
- 진단 stop stub의 `INT3`가 게스트 SEH로 가지 않도록 host trap 범위를 둡니다.

실제 4th는 게임 코드의 첫 호출 `CreateEventA`에서 멈춥니다.

*Task 368 completes the original MSVC CRT start-up, adding what it needs:*

- *`GuestHeap` serves the process heap and `HeapCreate` heaps, with a 512 MiB Linux arena mapped `MAP_NORESERVE`.*
- *The main image's guest path comes from the shared `target::GuestExecutablePath`, used by the command line, `GetModuleFileNameA`, and `GetModuleHandleA(NULL)`.*
- *An empty environment and code page 949 are provided; 949's single-byte conversion, `CT_CTYPE1`, and case mapping were measured by calling Win32 on this host.*
- *A host trap range keeps the diagnostic stop stub's `INT3` away from guest SEH.*

*The real 4th stops at the game code's first call, `CreateEventA`.*

작업 369에서 실제 4th는 C++ 정적 초기화와 게임에 link된 Hardlock 로그인을 지나 WinMain에 들어갑니다. 여기에 두 가지를 더했습니다. `GuestProcess`는 이름 없는 event를 가집니다. 게스트 thread가 하나라서 막히는 대기는 멈춥니다. `ImportCallServices::ReadClock`은 host 시계를 게스트 시간(`win32_time`)으로 줍니다. WinMain의 첫 호출은 `winmm!timeBeginPeriod`입니다.

*In Task 369 the real 4th passes C++ static initialization and the game's linked Hardlock login into WinMain. `GuestProcess` now keeps unnamed events, where a blocking wait stops because there is one guest thread, and `ImportCallServices::ReadClock` gives the host clock as guest time (`win32_time`). WinMain's first call is `winmm!timeBeginPeriod`.*

작업 370부터 Linux 실행은 facade 호출을 모두 `logs/re2dj-<시각>.api.log`에 남깁니다. 공용 `RecordingImportCallServices`가 handler의 서비스 사용을 기록하므로, handler를 고치지 않아도 게스트 메모리에서 읽은 입력, 게스트에 쓴 출력, last error 변화, 반환값, 실패 이유가 모두 남습니다. Hardlock 재료에서 나온 `DeviceIoControl` buffer는 길이만 남깁니다.

*From Task 370 a Linux run records every facade call in `logs/re2dj-<time>.api.log`. The shared `RecordingImportCallServices` notes each handler's use of its services, so without handler changes the log shows the inputs read from guest memory, the outputs written back, last-error changes, return values, and failure reasons; `DeviceIoControl` buffers derived from Hardlock material keep only their lengths.*

작업 371은 게스트 파일을 더했습니다. 공용 `GuestFiles`는 Windows 경로와 같은 규칙을 씁니다. 게스트 루트 아래의 파일은 overlay(`overlays/<profile>`)에 있으면 거기서, 없으면 CHD에서 읽습니다. 쓰려고 열면 CHD 파일을 overlay로 복사한 뒤 엽니다. 읽기 source는 `GuestFileSource` interface로 분리해 테스트는 메모리 source를 씁니다. kernel32 `CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointer`, `GetFileSize`, `CloseHandle`이 이것을 쓰고, `winmm`은 별도 module로 시간 함수를 제공합니다. 실제 4th는 `EZ2DJ.ini`를 CHD에서 읽은 뒤 window class 등록(`LoadIconA`)에 닿습니다.

*Task 371 adds guest files. The shared `GuestFiles` follows the Windows path's rules: a file below the guest root is read from the overlay (`overlays/<profile>`) when present and otherwise from the CHD, and opening for write first copies the CHD file into the overlay. The read source sits behind the `GuestFileSource` interface, so tests use a memory source. kernel32 `CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointer`, `GetFileSize`, and `CloseHandle` use it, and `winmm` becomes its own module with the timer functions. The real 4th reads `EZ2DJ.ini` from the CHD and reaches window class registration (`LoadIconA`).*

작업 372는 host handler가 게스트 함수를 부르는 경로를 더했습니다. `ImportCallServices::CallGuest`는 window procedure 같은 stdcall 게스트 함수를 끝까지 실행하고, 그동안 게스트가 부르는 import는 중첩 호출로 dispatch됩니다. x86은 handler가 이미 게스트 stack 위에 있으므로 바로 부르고, x64는 SEH dispatch와 같은 중첩 compat 전환을 씁니다. 이 위에 `GuestUser`(icon, cursor, class, window)와 user32 `RegisterClassA`, `CreateWindowExA`, `DefWindowProcA`, `UpdateWindow`가 있습니다. window 생성은 Windows 11에서 측정한 메시지 순서와 인자를 따릅니다. 실제 4th는 window를 만든 뒤 DirectDraw(`DirectDrawEnumerateExA`)에 닿습니다.

*Task 372 lets a host handler call guest functions. `ImportCallServices::CallGuest` runs a stdcall guest function, such as a window procedure, to completion, with the guest's imports meanwhile dispatched as nested calls; x86 calls directly, as the handler already runs on the guest stack, and x64 uses the nested compatibility-mode transition of SEH dispatch. On it sit `GuestUser` (icons, cursors, classes, windows) and user32 `RegisterClassA`, `CreateWindowExA`, `DefWindowProcA`, and `UpdateWindow`, with window creation following the message order and arguments measured on Windows 11. The real 4th creates its window and reaches DirectDraw (`DirectDrawEnumerateExA`).*

작업 373은 Linux facade의 COM 객체를 더했습니다. 객체는 vtable 포인터 하나짜리 process heap block입니다. vtable은 facade module의 `"<인터페이스>::<메서드>"` export thunk로 채우므로, 메서드 호출도 일반 import처럼 dispatch되고 API log에 이름으로 남습니다. ddraw는 `DirectDrawEnumerateExA`(모니터 하나), `DirectDrawCreateEx`, `IDirectDraw7`을 가집니다. 실제 4th는 `QueryInterface(IID_IDirect3D7)`에 닿습니다.

*Task 373 adds COM objects to the Linux facade: an object is a process-heap block holding its vtable pointer, and the vtable is filled with the facade module's `"<interface>::<method>"` export thunks, so method calls dispatch like any import and appear in the API log by name. ddraw has `DirectDrawEnumerateExA` (one monitor), `DirectDrawCreateEx`, and `IDirectDraw7`; the real 4th reaches `QueryInterface(IID_IDirect3D7)`.*

작업 374부터 DirectX HLE의 의미는 폭 중립 `re2dj_directx` library로 옮겨 갑니다. 이 library는 host OS header 없이 32비트 guest ABI 구조체(`abi.h`)와 facade가 스스로를 설명하는 값을 가집니다. DirectDraw 쪽(`directdraw_description.h`: caps, 표시 모드, 장치 식별자)과 Direct3D 쪽(`direct3d_description.h`: 장치 열거와 caps, depth·texture 형식)은 Windows facade처럼 나뉩니다. Linux의 ddraw facade module도 인터페이스별 파일(`ddraw_module.cpp`, `ddraw_direct3d7.cpp`)과 공용 COM 도우미(`facade_com`)로 나뉩니다. Windows COM facade는 SDK 구조체와의 일치를 `static_assert`로 검사한 뒤 이 값을 복사하고, Linux gate facade는 같은 값을 guest 메모리에 씁니다. 표면, 장치 상태, 그리기, Linux 표시는 이후 단계에서 옮깁니다.

*From Task 374 the DirectX HLE's semantics move into the width-neutral `re2dj_directx` library: without host OS headers it holds the 32-bit guest ABI structures (`abi.h`) and the values the facade reports about itself, split like the Windows facade into DirectDraw's (`directdraw_description.h`: caps, display modes, the device identifier) and Direct3D's (`direct3d_description.h`: the device enumeration and caps, depth and texture formats). Linux's ddraw facade module likewise has a file per interface (`ddraw_module.cpp`, `ddraw_direct3d7.cpp`) and shared COM helpers (`facade_com`). The Windows COM facade copies them after checking the SDK structures match with `static_assert`, and the Linux gate facade writes the same values into guest memory. Surfaces, device state, drawing, and Linux presentation follow in later phases.*

작업 377은 2단계로, 협조 수준과 표시 모드를 옮겼습니다. `directdraw_display.h`의 `DirectDrawDisplay`가 DirectDraw 객체의 창과 모드를 갖고, `SetCooperativeLevel`/`SetDisplayMode` 규칙이 이 상태를 바꿉니다. 호스트가 자기 창에 무엇을 하는지는 callback으로 받습니다. Windows는 Win32 창 모드 정책을 넘기고, Linux는 아직 guest 창이 있는지만 봅니다. Linux facade의 COM 객체는 `GuestComObject::state`에 module별 상태를 둡니다.

*Task 377, phase 2, moves the cooperative level and display mode: `directdraw_display.h`'s `DirectDrawDisplay` holds a DirectDraw object's window and mode, changed only by the `SetCooperativeLevel`/`SetDisplayMode` rules, with the host's own window work passed in as a callback — the Win32 window-mode policy on Windows, and for now only a check that the guest window exists on Linux. Linux facade COM objects keep per-module state in `GuestComObject::state`.*

작업 381은 3단계로, 표면을 옮겼습니다. `directdraw_surface.h`의 `PlanCreateSurface`가 `CreateSurface` 요청으로 표면의 종류·크기·caps를 정하거나 거절합니다. attach 규칙과 표면 설명도 core에 있습니다. 표면 메모리는 host가 정합니다. Windows는 GDI DIB를 쓰고, Linux는 guest `VirtualAlloc` 메모리를 씁니다. Linux의 guest COM 상태는 다른 facade 객체의 참조와 guest 자원을 가질 수 있습니다. 객체가 사라지면 자원을 돌려주고 참조를 놓습니다.

*Task 381, phase 3, moves surfaces: `directdraw_surface.h`'s `PlanCreateSurface` turns a `CreateSurface` request into a surface's kind, size, and caps, or refuses it, and the attachment rules and surface descriptions live in the core too. Surface memory is the host's: GDI DIBs on Windows, guest `VirtualAlloc` memory on Linux. On Linux, guest COM state can hold references to other facade objects and guest resources; when the object goes, it returns the resources and drops the references.*

작업 382는 4단계로, 장치를 옮겼습니다. `direct3d_device.h`의 `DeviceState`는 render state, texture stage state, transform, viewport, 장면 여부를 guest의 index 그대로 담습니다. `CreateDevice` 규칙, 초기 상태, 상태 메서드 규칙도 core에 있습니다. Windows의 DirectX 6/7 장치와 Linux의 `IDirect3DDevice7`이 같은 상태를 씁니다. 그리기가 이 상태로 무엇을 하는지는 host마다 다르며, 5단계에서 다룹니다.

*Task 382, phase 4, moves the device: `direct3d_device.h`'s `DeviceState` holds the render states, texture stage states, transforms, viewport, and whether a scene is open, indexed by the guest's own numbers, with the `CreateDevice` rules, the initial state, and the state methods' rules beside it. Windows' DirectX 6/7 device and Linux's `IDirect3DDevice7` keep the same state; what drawing makes of it is each host's, in phase 5.*

작업 391은 5단계로, 그리기를 옮겼습니다. `direct3d_draw.h`는 어떤 draw를 그리는지, 장치 상태가 어떤 fixed-function 상태와 DX7 변환이 되는지, fade 호환, clear 색을 정합니다. 두 host 모두 공용 `Sdl3OpenGlBackend`로 그립니다. Linux는 `HostPresentation`의 그리기 계약(`ClearTarget`, `Draw`, `Present`, `DiscardTexture`)을 거칩니다. 텍스처는 guest 메모리의 표면이며, identity와 revision으로 host 복사본을 갱신합니다.

*Task 391, phase 5, moves drawing: `direct3d_draw.h` decides which draws are drawn, what fixed-function state and DX7 transform a device's state becomes, the fade compatibility, and the clear color. Both hosts draw through the shared `Sdl3OpenGlBackend`; Linux goes through `HostPresentation`'s drawing contract (`ClearTarget`, `Draw`, `Present`, `DiscardTexture`). Textures are surfaces in guest memory, whose host copies follow an identity and a revision.*

작업 383부터 DirectSound도 같은 방식을 따릅니다. `re2dj/audio/directsound_abi.h`와 `directsound_device.h`는 32비트 guest의 DirectSound 구조체를 담습니다. 버퍼 생성, caps, 복제, lock 분할 규칙도 이 두 헤더에 있습니다. Windows facade는 이 규칙을 쓰고, 재생에는 SDL3_mixer를 씁니다. Linux의 `dsound.dll` module은 sample을 guest 메모리에 둡니다. 그래서 guest의 lock이 그 메모리를 바로 씁니다. Linux 소리 출력은 이후 단계에서 다룹니다.

*From Task 383 DirectSound follows the same pattern. `re2dj/audio/directsound_abi.h` and `directsound_device.h` hold the 32-bit guest's DirectSound structures, together with the rules for buffer creation, caps, duplication, and how a lock divides a buffer. The Windows facade uses these rules and plays through SDL3_mixer. Linux's `dsound.dll` module keeps samples in guest memory, so the guest's locks write that memory directly. Linux sound output comes in a later phase.*

작업 384에서 DirectInput도 `re2dj/directx/directinput.h`의 core를 씁니다. core는 두 가지를 정합니다. 하나는 어떤 장치와 인터페이스에 답하는지이고, 다른 하나는 눌린 키·버튼(`InputSnapshot`)을 guest의 장치 상태 버퍼에 어떻게 배치하는지입니다. host에서 키를 읽는 일은 각 host가 합니다. Windows는 `GetAsyncKeyState`를 쓰고, Linux의 입력 연결은 이후 단계입니다.

*In Task 384 DirectInput joins the core too, in `re2dj/directx/directinput.h`. The core decides two things: which devices and interfaces answer, and how held keys and buttons (`InputSnapshot`) are laid out in the guest's device state buffer. Reading the keys is each host's own work: Windows uses `GetAsyncKeyState`, and Linux input comes in a later phase.*

작업 385부터 IO 보드 포트 입출력도 두 host가 같은 규칙을 씁니다. 게임은 `in`/`out` 명령으로 보드를 직접 읽고 씁니다. 이 명령이 user mode에서 예외를 내면 host가 받습니다. 그 명령이 보드 접근인지는 `re2dj/input/legacy_io_trap.h`가 판정합니다(폭, helper 주소, opcode). 답은 공용 `LegacyIoPortBus`가 합니다. Windows는 이 일을 vectored exception handler에서 하고, Linux는 두 폭의 guest signal handler에서 합니다.

*From Task 385 the I/O board's port access follows the same rules on both hosts. The game reads and writes the board directly with `in`/`out`, which fault in user mode and reach the host. `re2dj/input/legacy_io_trap.h` decides whether such an instruction is a board access (width, helper address, opcode), and the shared `LegacyIoPortBus` answers it. Windows does this in its vectored exception handler; Linux does it in both widths' guest signal handlers.*

작업 380부터 Linux 실행은 호스트 창을 띄웁니다. HLE는 OS 중립 `HostPresentation` 서비스를 통해 게스트 창을 보여 달라고 요청합니다. ddraw `SetCooperativeLevel`의 호스트 정책이 이 요청을 합니다. Linux의 `LinuxHostPresentation`은 공용 SDL3/OpenGL backend로 640×480 창을 만듭니다. 창 제목은 모든 host가 쓰는 `WindowTitle`입니다. 아직 그리는 것이 없어 검은 화면이고, `--hold-window`를 주면 실행이 멈춘 뒤에도 창이 남습니다.

*From Task 380 a Linux run opens a host window: the HLE asks the platform-neutral `HostPresentation` service to show the guest window from ddraw `SetCooperativeLevel`'s host policy, and Linux's `LinuxHostPresentation` makes a 640×480 window with the shared SDL3/OpenGL backend, titled by the `WindowTitle` every host uses. It stays black until drawing arrives, and with `--hold-window` it remains after the run stops.*

작업 435부터 Linux 창의 배율을 바꾼 뒤 다시 가운데로 옮기는 것은 요청입니다. Wayland처럼 window system이 창 위치 지정을 거절하면 위치는 compositor에 맡기고, 창 모드 변경은 크기 변경이 실패할 때만 실패합니다.

*From Task 435 centring the Linux window again after a scale change is a request: when the window system refuses to place the window, as Wayland does, placement is left to the compositor, and a window-mode change fails only when the resize does.*

작업 437부터 Linux `GuestFiles`의 overlay 경로는 NTFS처럼 대소문자를 구분하지 않습니다. 각 구성요소는 정확한 철자가 있으면 그것을, 없으면 대소문자 없이 일치하는 기존 항목을 씁니다. 6th가 `EZ2DJ1st\bookkeeping.ini`에 쓴 것을 1st가 `EZ2DJ1ST`에서 읽기 때문입니다. 게스트가 이미지 파일을 지우면(`DeleteFileA`) 원본을 바꾸지 않고 overlay 루트의 `.re2dj-deleted`에 그 경로를 남깁니다. 그 뒤로 이 run과 이후 run(launcher의 다음 자식)에서 그 파일은 없는 것으로 보입니다.

*From Task 437 the Linux `GuestFiles` overlay paths ignore case as NTFS does: each component takes its exact spelling when present, else an existing entry matching without case, since 6th writes `EZ2DJ1st\bookkeeping.ini` that 1st reads in `EZ2DJ1ST`. When the guest deletes an image file (`DeleteFileA`), the image stays untouched and the path is listed in `.re2dj-deleted` at the overlay root; from then on that file is missing for this run and later ones, such as the launcher's next child.*

#9부터 `GuestFiles`는 게임이 나눠 읽는 이미지 파일을 백그라운드에서 미리 읽습니다(`hle/guest_file_prefetcher.h`). 6th는 소리 스레드 없이 메인 스레드가 133ms마다 배경음 `.ezw`에서 22,528바이트씩 `ReadFile`로 스트리밍합니다. 그래서 HDD에 둔 CHD를 처음 읽는 조각마다 메인 스레드가 디스크를 기다리며 멈췄습니다. 이제 읽기 전용으로 연 이미지 파일의 첫 `Read` 뒤 256KiB 이상 남으면, 작업 스레드 하나가 그 파일을 처음부터 16KiB씩 메모리에 읽고 채운 길이를 공개합니다. 이후 `Read`는 그 범위면 메모리에서, 아니면 지금처럼 source에서 읽습니다. 한 번에 다 읽는 파일은 미리 읽지 않습니다. 게스트의 source 호출은 `ForegroundSource` wrapper를 거치고, 그 호출이 진행 중이면 작업 스레드는 다음 조각을 시작하지 않습니다. 그래서 게스트는 CHD 잠금을 조각 하나만큼만 기다립니다. `CloseHandle`은 작업을 취소하며, 열린 작업의 메모리는 합쳐서 256MiB까지입니다. CHD handle은 하나만 씁니다(6th CHD의 hunk map이 handle마다 약 60MB).

*From #9 `GuestFiles` reads ahead, in the background, image files the game reads in pieces (`hle/guest_file_prefetcher.h`). 6th has no sound thread: its main thread streams 22,528 bytes of the music `.ezw` with `ReadFile` every 133 ms, so each CHD piece read for the first time from an HDD stalled it on the disk. Now, when an image file opened read-only has at least 256 KiB left after its first `Read`, one worker thread reads it from the start into memory 16 KiB at a time and publishes the filled length; later `Read`s copy from memory within it and go to the source as before beyond it. Files read whole at once are not read ahead. The guest's source calls go through the `ForegroundSource` wrapper, and the worker starts no piece while one is under way, so the guest waits on the CHD lock for one piece at most. `CloseHandle` cancels the job, and open jobs hold at most 256 MiB together. One CHD handle is used (6th's CHD hunk map costs about 60 MB per handle).*

#24부터 prefetcher는 게스트가 읽은 것을 기억합니다(`hle/guest_file_read_journal.h`). `ForegroundSource`를 지나는 게스트의 `Find`·`ReadRange`·`ListDirectory`는 경로마다 사용 횟수와 읽은 범위로 기록됩니다. 사용 횟수는 실행마다 한 번, 그리고 10초 넘게 쓰이지 않다가 다시 쓰일 때마다 늘어납니다. 기록은 overlay 루트의 `.re2dj-readahead`에 15초마다와 종료 때 저장되며, 디스크의 기록에 이번 실행의 증가분을 합칩니다. 6th의 부트스트랩과 게임이 같은 overlay를 동시에 쓰기 때문입니다. 다음 실행의 학습 작업 스레드는 두 번 이상 쓰인 경로를 64MiB까지 기록 순서로 다시 읽어 OS 캐시에 올리며, 게스트 호출이나 #9의 파일 작업이 진행 중이면 조각마다 기다립니다. 곡 로딩의 대부분은 곡과 관계없이 같은 파일이라, HDD에서 캐시 없는 곡 로딩이 2.17초에서 0.58초로 줄었습니다([작업 로그](docs/work-logs/20261012-i024-learned-chd-read-ahead.md)).

*From #24 the prefetcher remembers what the guest read (`hle/guest_file_read_journal.h`). The guest's `Find`, `ReadRange` and `ListDirectory` through `ForegroundSource` are recorded per path with a use count and the range read; the count grows once per run and again each time the path is used after 10 s without it. The journal is saved to `.re2dj-readahead` at the overlay root every 15 s and at the end, merging this run's gains into the journal on disk, since 6th's bootstrap and game use the same overlay at once. The next run's learning worker reads the paths used more than once, up to 64 MiB and in journal order, into the OS cache, waiting before each piece while a guest call or #9's file job is under way. Most of a song load is the same files whatever the song, so a cold song load from an HDD fell from 2.17 s to 0.58 s ([work log](docs/work-logs/20261012-i024-learned-chd-read-ahead.md)).*

작업 417부터 Linux 실행은 게스트 스레드를 여럿 가질 수 있습니다. `CreateThread`가 만든 게스트 스레드는 host 스레드 하나에서 돕니다. 스레드마다 게스트 스택, TEB, FS 선택자, alternate signal stack이 따로 있고, PEB는 공유합니다. 게스트 코드와 import 처리(HLE)는 게스트 잠금(`native_guest_threads.h`)을 가진 스레드 하나만 실행합니다. 잠금은 import 안에서만 넘어갑니다. 스레드가 기다릴 때(`Sleep`, 막히는 대기) 넘기고, 다른 스레드가 잠금을 기다리는 중이면 import가 시작될 때도 넘깁니다. 그래서 facade 상태는 스레드 안전할 필요가 없습니다. x64는 공유 transition page의 host 상태를 잠금을 넘길 때 저장하고 복원합니다. 메인이 아닌 스레드의 fault·정지·`ExitProcess`는 프로세스를 끝냅니다. 메인 스레드의 실행이 그 결과로 끝납니다. import 없이 도는 게스트 코드가 다른 스레드를 막는 점은 Windows의 선점형 스케줄링과 다릅니다.

*From Task 417 a Linux run can have several guest threads. Each guest thread `CreateThread` makes runs on a host thread of its own, with its own guest stack, TEB, FS selector, and alternate signal stack, sharing the PEB. Only the thread holding the guest lock (`native_guest_threads.h`) runs guest code or import handling (the HLE), so the facade's state need not be thread-safe. The lock changes hands only inside an import: when a thread waits (`Sleep`, a blocking wait), and as an import starts while another thread waits for the lock. On x64 the shared transition page's host state is saved and restored at each hand-over. A fault, stop, or `ExitProcess` in a thread other than the main one ends the process, and the main thread's run ends with it. Guest code spinning without imports keeps the other threads out, unlike Windows' preemptive scheduling.*

작업 421부터 Linux facade는 파일에서 비트맵을 읽습니다. 공용 `hle/bitmap_file.h`가 BMP 파일(bottom-up `BI_RGB`, 8·24비트)을 DIB로 바꿉니다. `user32!LoadImageA`(`LR_LOADFROMFILE | LR_CREATEDIBSECTION`)는 그 비트를 guest `VirtualAlloc` 메모리에 둡니다. gdi32의 `GetObjectA`, `CreateCompatibleDC`, `SelectObject`, 1:1 `StretchBlt`, `DeleteDC`는 `src/hle/modules/gdi_bitmaps.cpp`에 있고, 두 모듈은 등록만 합니다. 선택된 비트맵의 `DeleteObject`는 선택이 풀릴 때까지 해제를 미룹니다. 규칙은 Windows 11 측정을 따르고, `StretchBlt`의 확대·축소 대응은 미확정이라 멈춥니다.

*From Task 421 the Linux facade loads bitmaps from files. The shared `hle/bitmap_file.h` turns a BMP file (bottom-up `BI_RGB`, 8 or 24 bits) into a DIB, which `user32!LoadImageA` (`LR_LOADFROMFILE | LR_CREATEDIBSECTION`) places in guest `VirtualAlloc` memory. gdi32's `GetObjectA`, `CreateCompatibleDC`, `SelectObject`, 1:1 `StretchBlt`, and `DeleteDC` live in `src/hle/modules/gdi_bitmaps.cpp`, with the two modules only registering them, and `DeleteObject` of a selected bitmap defers its release until it is deselected. The rules follow Windows 11 measurements; `StretchBlt`'s enlarging and shrinking mapping is unresolved and stops.*

작업 422부터 Linux의 DX6 표면은 Windows DX6 facade처럼 `IDirect3DTexture2`를 줍니다. Linux COM 블록은 vtable 하나라서 texture는 표면이 소유한 별도 블록(`ddraw_texture2.cpp`)입니다. 그 블록의 참조는 표면의 참조 수로 셉니다. `IDirect3DDevice3::SetTexture`는 texture를 표면으로 바꿔 DX7 상태를 씁니다. DX6 vertex buffer는 DX7 버퍼에 `IDirect3DVertexBuffer` vtable(앞 8개 method)을 붙인 것입니다. 표면 `Blt`(색 채우기와 같은 크기 복사)와 `BltFast`는 표면 복사 규칙 하나를 공유합니다. back buffer로의 복사는 host에도 textured quad로 그립니다. 이것으로 1st SE CHD는 Linux에서 창을 닫을 때까지 돕니다.

*From Task 422 DX6 surfaces on Linux give `IDirect3DTexture2` as the Windows DX6 facade does. A Linux COM block has one vtable, so the texture is a separate block the surface owns (`ddraw_texture2.cpp`), whose references count on the surface. `IDirect3DDevice3::SetTexture` turns the texture into its surface and uses the DX7 state. A DX6 vertex buffer is a DX7 buffer with `IDirect3DVertexBuffer`'s vtable (its first eight methods). Surface `Blt` (color fill and equal-size copy) and `BltFast` share one surface-copy rule, and a copy onto a back buffer is also drawn on the host as a textured quad. With these, the 1st SE CHD runs on Linux until its window is closed.*

작업 431부터 Linux의 guest `CreateProcessA`는 자식 guest 프로그램을 **별도 host 프로세스**로 실행합니다. 모든 guest 이미지가 같은 기준 주소(0x400000)에 올라가므로, 부모와 자식이 한 주소 공간을 함께 쓸 수는 없습니다.

- **자식 실행**: 공용 `hle::HostProcessLauncher`의 Linux 구현(`LinuxHostProcessLauncher`)이 `/proc/self/exe`를 부모 run의 옵션 그대로 다시 실행합니다. 그 뒤에 자식 요청을 붙여 넘깁니다. 요청은 이미지 안의 실행 파일, 명령줄, 첫 현재 디렉터리, `STARTUPINFO` reserved 바이트입니다.
- **자식이 받는 값**: 자식 run은 그 값을 `hle::GuestStartup`으로 받습니다. 자식의 `GetCommandLineA`와 `GetStartupInfoA`가 그 값을 돌려줍니다.
- **종료 코드**: host 종료 상태에는 8비트만 담기므로, 자식은 32비트 guest 종료 코드를 pipe로 써서 돌려줍니다.
- **부모 쪽 HLE**: 부모의 HLE는 자식의 프로세스 핸들로 기다리고(`WaitForSingleObject`), 종료 코드를 읽습니다(`GetExitCodeProcess`).
- **적용 예**: 6th의 launcher(`EZ2DJ.EXE`)가 이 방식으로 `EZ2DJ6th.EXE`를 실행합니다.

*From Task 431 a guest `CreateProcessA` on Linux runs the child guest program as a **separate host process**. Every guest image loads at the same base (0x400000), so a parent and a child cannot share one address space.*
- ***Starting the child**: the Linux implementation of the shared `hle::HostProcessLauncher` (`LinuxHostProcessLauncher`) runs `/proc/self/exe` again with the parent run's own options, followed by the child's request: the executable in the image, its command line, its first current directory, and the `STARTUPINFO` reserved bytes.*
- ***What the child receives**: the child run takes these as `hle::GuestStartup`, and its `GetCommandLineA` and `GetStartupInfoA` return them.*
- ***Exit code**: a host exit status holds only 8 bits, so the child writes its 32-bit guest exit code back over a pipe.*
- ***The parent's HLE**: it waits on the child's process handle (`WaitForSingleObject`) and reads the exit code (`GetExitCodeProcess`).*
- ***Example**: 6th's launcher (`EZ2DJ.EXE`) starts `EZ2DJ6th.EXE` this way.*

작업 434부터 launcher의 자식이 둘 이상일 수 있습니다. 6th에서 Remember 1st를 고르면 launcher가 동봉된 1st(`EZ2DJ1ST\EZ2DJ.EXE`)를 실행합니다.

- **자식 목록**: 프로필의 `child_executable_paths`가 launcher가 실행하는 이미지 안의 실행 파일들입니다. CLI는 CHD staging에 이 파일들을 모두 꺼냅니다.
- **자식 run의 root**: 자식은 launcher의 guest root를 그대로 씁니다. 공용 `target::ExecutableWorkingDirectory`와 `GuestExecutablePath`가 프로필 실행 파일의 디렉터리 아래 경로를 붙여(`D:\ez2dj\EZ2DJ1ST\Ez2DJ.exe`) launcher가 준 현재 디렉터리가 root 안에 있게 합니다. Linux CLI와 Windows probe가 같은 규칙을 씁니다.
- **INI 쓰기**: 공용 `hle/private_profile.h`의 `UpdatePrivateProfile`이 Windows 11에서 측정한 `WritePrivateProfileStringA` 규칙으로 새 본문을 만들고, Linux kernel32가 `GuestFiles`로 overlay에 씁니다. Windows 제품은 자식의 INI import를 VFS로 이어 overlay 사본에 실제 API로 씁니다.
- **GDI**: 텍스트 그리기는 `src/hle/modules/gdi_text.cpp`로 옮겨 user32와 gdi32가 함께 씁니다. `gdi_bitmaps.cpp`가 `CreateDIBSection`, `CreateDIBitmap`(창 DC의 32비트 DDB), 24비트 비트맵을 받습니다. user32는 창 DC(`GetDC`·`ReleaseDC`)와 `SendMessageA`를 줍니다.
- **Windows launcher probe**: launcher가 만드는 프로세스 중 `child_executable_paths`의 것을 차례로 준비하고, launcher가 끝나면 run을 끝냅니다. 자식의 첫 guest 디렉터리는 host 프로세스의 실제 현재 디렉터리입니다. 진단용 `--startup-reserved <hex>`(CLI `--guest-startup-reserved`, `hle/hex_bytes.h`)로 자식을 단독 실행할 수 있습니다.

*From Task 434 a launcher may have more than one child: choosing Remember 1st in 6th makes the launcher run the bundled 1st (`EZ2DJ1ST\EZ2DJ.EXE`).*
- ***Child list**: a profile's `child_executable_paths` are the executables in the image its launcher starts; the CLI stages them all out of the CHD.*
- ***A child run's root**: a child keeps the launcher's guest root. The shared `target::ExecutableWorkingDirectory` and `GuestExecutablePath` append the path below the profile executable's directory (`D:\ez2dj\EZ2DJ1ST\Ez2DJ.exe`), so the current directory the launcher gives lies inside the root. The Linux CLI and the Windows probe share the rule.*
- ***INI writes**: the shared `UpdatePrivateProfile` in `hle/private_profile.h` builds the new text by the `WritePrivateProfileStringA` rules measured on Windows 11, and the Linux kernel32 writes it into the overlay through `GuestFiles`. The Windows product routes the child's INI imports through the VFS, writing the overlay copy with the real API.*
- ***GDI**: text drawing moves into `src/hle/modules/gdi_text.cpp`, shared by user32 and gdi32; `gdi_bitmaps.cpp` takes `CreateDIBSection`, `CreateDIBitmap` (a window DC's 32-bit DDB) and 24-bit bitmaps; user32 gives window DCs (`GetDC`, `ReleaseDC`) and `SendMessageA`.*
- ***Windows launcher probe**: it prepares each process the launcher makes that is in `child_executable_paths`, in turn, and ends the run with the launcher. A child's first guest directory is the host process's real current directory. The diagnostic `--startup-reserved <hex>` (the CLI's `--guest-startup-reserved`, `hle/hex_bytes.h`) runs a child on its own.*


Task 137은 그 후보를 기계적으로 판정하는 측정기를 추가했습니다. 플랫폼 중립 `re2dj::analysis::ScoreCodeRegion`이 바이트 span 하나에서 Shannon 엔트로피, `55 8b ec` prologue 수, `cc` padding run 수, zero byte 비율을 계산하고 `ciphertext-like` / `code-like` / `indeterminate` 3상태로 보고합니다. 임계값은 확인된 측정치 사이에 둔 휴리스틱임을 코드와 문서에 표기합니다. `re2dj_code_score`가 파일·HDD·CHD 입력을 섹션 또는 청크 단위로 이 함수에 넣습니다. 이 함수는 파일도 프로세스도 모르므로 이후 게스트 memory dump를 같은 경로로 판정할 수 있습니다.

*Task 137 adds the measurement that judges those candidates mechanically. The platform-neutral `re2dj::analysis::ScoreCodeRegion` computes Shannon entropy, `55 8b ec` prologue count, `cc` padding run count, and zero-byte share from a single byte span and reports one of three states — `ciphertext-like`, `code-like`, or `indeterminate` — with the thresholds marked in code and documents as a heuristic placed between confirmed measurements. `re2dj_code_score` feeds file, HDD, and CHD inputs into that function by section or by chunk. Because the function knows nothing of files or processes, a guest memory dump can later be judged through the same path.*

---

## 3. HDD 디렉터리 입력 / HDD directory input **[구현됨]**

원본 HDD 내용은 **디렉터리 경로**로 입력받는다. 이미지 파일(`.img`, `.vhd`)을 직접 마운트하지 않는다. 사용자가 이미지를 풀어 놓은 디렉터리를 그대로 가리키면 된다.

*Original HDD contents arrive as a **directory path**. Image files are not mounted directly; the user points at a directory into which the image was already extracted.*

CHD-backed target은 이 디렉터리 경계와 병렬인 `MameChdImage` read-only block boundary를 사용한다. `Fat32Volume`은 CHD logical sector에서 MBR/BPB/FAT/LFN을 읽고 `EZ2DJ/EZ2DJ.EXE`를 확인한다. Windows x86 launcher는 PE와 profile sibling만 staging하고 CHD 경로를 injected runtime에 전달해 guest read를 pseudo handle로 서비스한다. CHD 실행에서는 부모 CLI가 확인한 staging 기준 executable 상대 경로도 `--target-executable`로 전달하여 launcher의 directory scan이 CHD 전용 프로파일을 잃지 않게 한다.

*CHD-backed targets use a read-only `MameChdImage` block boundary parallel to the directory boundary above. `Fat32Volume` reads MBR/BPB/FAT/LFN data directly from CHD sectors and confirms `EZ2DJ/EZ2DJ.EXE`. The Windows x86 launcher stages the PE and profile siblings, passes the CHD path to the injected runtime, and serves guest reads through pseudo handles. For CHD launches, the parent CLI also forwards the staging-relative executable path as `--target-executable`, so a launcher directory scan cannot lose a CHD-only profile.*

이 경계는 캐시 계층을 함께 갖는다. `MameChdImage`는 압축 해제된 hunk를 `ChdHunkCache`에 LRU로 보관하고, `Fat32Volume`은 디렉터리 항목, 해석된 경로, 파일 클러스터 체인을 캐시한다. 볼륨과 이미지가 모두 read-only이므로 무효화 경로는 없다. 세 캐시는 상한을 넘으면 통째로 비운다. 게스트가 여러 스레드에서 파일 API를 호출하므로 공개 판독 API는 `std::mutex`로 직렬화한다. 근거: [런타임 핫 경로 성능 설계](docs/design/20260907-219-runtime-performance-hot-paths.md).

*The boundary carries a cache layer: `MameChdImage` keeps decompressed hunks in a `ChdHunkCache` LRU, and `Fat32Volume` caches directory entries, resolved paths, and file cluster chains. Both the image and the volume are read-only, so there is no invalidation path; each cache is cleared wholesale when it passes its bound. The public read API is serialized with a `std::mutex` because the guest calls the file APIs from several threads. See the [runtime hot-path performance design](docs/design/20260907-219-runtime-performance-hot-paths.md).*

```
re2dj --hdd /path/to/ez2dj_hdd
```

`re2dj::hdd::HddRoot`가 그 경로를 소유하며 다음을 책임진다.

* 경로 존재와 디렉터리 여부 검증
* 게스트 상대 경로를 호스트 실제 경로로 해석. **대소문자를 무시한다.**
* 해석 결과 캐시

*`re2dj::hdd::HddRoot` owns the path and is responsible for validating it, resolving guest-relative paths to real host paths **case-insensitively**, and caching the result.*

### 왜 대소문자 무시 해석이 필요한가 / Why case-insensitive resolution is required

원본은 Windows에서 동작했으므로 게임 코드가 `DATA\Song01.EZ`처럼 실제 파일명과 대소문자가 다른 문자열로 파일을 열어도 동작했다. Linux 호스트의 파일 시스템은 대소문자를 구분하므로, 그대로 넘기면 열리지 않는다. `HddRoot`는 각 경로 구성 요소를 디렉터리 항목과 대소문자 무시로 대조해 실제 이름을 찾는다.

*The original ran on Windows, so game code could open `DATA\Song01.EZ` with a case that does not match the real file name. Linux host file systems are case-sensitive, so the open would fail. `HddRoot` matches each path component against directory entries case-insensitively to find the real name.*

대조는 항상 디렉터리 나열 결과를 기준으로 한다. 정확히 일치하는 항목이 있으면 그것을 쓰고, 없으면 대소문자 무시로 처음 일치한 항목을 쓴다. 요청된 철자를 먼저 시도하면 Windows에서는 성공하고 Linux에서는 실패해, 같은 덤프가 호스트마다 다른 경로를 내놓는다. 나열 결과는 디렉터리 단위로 캐시한다.

*Matching always goes through the directory listing: an exact match wins, otherwise the first case-insensitive match does. Probing the requested spelling first would succeed on Windows and fail on Linux, so one dump would yield different paths per host. Listings are cached per directory.*

### 쓰기 정책 / Write policy **[구현됨]**

게스트의 파일 쓰기는 원본 디렉터리를 변경하지 않는다. 쓰기는 별도 overlay 디렉터리로 향하며, 읽기는 overlay를 먼저 조회한 뒤 원본으로 내려간다. overlay 우선 조회도 Win32 의미를 보존하기 위해 구성요소별 정확한 이름을 우선하고 없으면 ASCII 대소문자를 무시해 찾는다. `--hdd`는 전체 dump root이고 Windows x86 launcher는 선택된 target profile의 `working_directory_relative_path`를 안전하게 해석해 guest `D:\ez2dj`와 상대 경로의 source mount로 주입한다. 빈 working directory만 dump root 자체를 뜻한다.

*Guest file writes never modify the original directory. Writes go to a separate overlay directory, and reads consult the overlay before falling through to the original. Overlay lookup preserves Win32 semantics by preferring an exact component match and otherwise using an ASCII case-insensitive fallback. `--hdd` denotes the complete dump root; the Windows x86 launcher safely resolves the selected target profile's `working_directory_relative_path` and injects it as the source mount for guest `D:\ez2dj` and relative paths. Only an empty working directory maps directly to the dump root. The runtime clones an existing original file to the overlay before an `OPEN_EXISTING` write, so the original stays unchanged.*

Windows x86 injected VFS는 staging 이후 다시 들어오는 설정된 HDD root 또는 HLE Windows root 아래의 host absolute path도 해당 root 기준의 guest-relative suffix로 재해석한다. 이 경로는 일반 guest-relative 경로와 같은 overlay 우선 읽기와 copy-on-write 쓰기를 사용하며, HDD suffix는 CHD materialization/fallback에도 재사용한다. 설정된 root 밖의 absolute path는 임의 host 파일로 통과시키지 않는다.

*The Windows x86 injected VFS also reinterprets a host-absolute path under the configured HDD or HLE Windows root as the guest-relative suffix beneath that root. It uses the same overlay-first read and copy-on-write write policy as ordinary guest-relative paths, and reuses the HDD suffix for CHD materialization/fallback. Absolute paths outside configured roots are not passed through as arbitrary host files.*

```mermaid
flowchart TD
    R["Guest read: DATA\\SONG.EZ"] --> O{"overlay hit?"}
    O -->|yes| OV["overlay/DATA/SONG.EZ<br/>(case-insensitive)"]
    O -->|no| HD["hdd/DATA/SONG.EZ<br/>(case-insensitive)"]
    W["Guest write: SAVE\\SCORE.DAT"] --> OW["overlay/SAVE/SCORE.DAT"]
```

### Windows x86 이미지 로더 경계 / Windows x86 image-loader boundary **[구현됨]**

원본은 자산 이름을 검색 경로 테이블로 해석한 뒤 `CreateFileA`로 존재를 확인하고, 실제 비트맵은 `USER32!LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION)`로 읽는다. 따라서 VFS는 `KERNEL32!CreateFileA`만으로는 완결되지 않는다. `Re2djVfsLoadImageA`는 문자열 상대경로 + `IMAGE_BITMAP` + `LR_LOADFROMFILE` 조합에만 읽기 전용 HDD/overlay 매핑을 적용하고, resource ID·다른 image type·다른 flag는 원래 `LoadImageA`로 그대로 넘긴다. launcher는 이 패치 준비 상태를 다른 VFS 패치와 분리해 추적하므로, 실패해도 뒤따르는 device 패치를 조용히 건너뛰지 않고 `vfs_image_loader` 진단 이벤트로 드러난다.

원본 `.str` 장면 스크립트 로더는 `FILE_FLAG_NO_BUFFERING`으로 열고 sector 배수가 아닌 파일 크기 전체를 정렬되지 않은 pool 버퍼로 읽는다. Windows 9x는 이 정렬 요구를 강제하지 않았지만 NT 커널은 강제하므로 `ReadFile`이 `ERROR_INVALID_PARAMETER`로 실패한다. VFS `CreateFileA` 경계는 게스트가 기대한 OS 의미를 복원하기 위해 `FILE_FLAG_NO_BUFFERING`만 제거하고 나머지 flag는 그대로 전달한다. 캐싱 정책만 달라지고 게스트가 받는 바이트는 동일하다.

*The original `.str` scene-script loader opens with `FILE_FLAG_NO_BUFFERING` and then reads a whole non-sector-multiple file into an unaligned pool buffer. Windows 9x did not enforce those alignment rules but the NT kernel does, so `ReadFile` fails with `ERROR_INVALID_PARAMETER`. To restore the OS semantics the guest expects, the VFS `CreateFileA` boundary strips `FILE_FLAG_NO_BUFFERING` alone and forwards every other flag; only the caching policy changes while the bytes the guest receives stay identical.*

자산 경계 진단은 `.bmp`와 `.str` 요청에 대해 호출 API, 게스트 요청 경로, 매핑된 호스트 경로, 성공 여부와 Win32 오류를 별도 bounded 로그에 남긴다. 확장자마다 상한을 따로 두어 대량 비트맵 스윕이 드문 script 요청을 가리지 못하게 한다. 파일 내용은 기록하지 않는다. 경로 해석은 게스트 안에서 일어나므로 존재하지 않는 후보에 `ERROR_FILE_NOT_FOUND`를 그대로 돌려주는 것이 계약이며, 진단 자체의 파일 I/O가 게스트가 읽을 last error를 덮지 않도록 보고 뒤에 오류를 확정한다. 확인된 호출 지점과 인자는 [자산 로딩 경로 분석](docs/analysis/ez2dj-asset-loading-path.md)에 있다.

*The original resolves an asset name through a search-path table, probes existence with `CreateFileA`, and then reads the actual bitmap with `USER32!LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION)`, so a `KERNEL32!CreateFileA`-only VFS is incomplete. `Re2djVfsLoadImageA` applies the read-only HDD/overlay mapping solely to the string-relative-path + `IMAGE_BITMAP` + `LR_LOADFROMFILE` combination and forwards resource IDs, other image types, and other flags unchanged. The launcher tracks this patch's readiness separately from the other VFS patches, so a failure surfaces as a `vfs_image_loader` diagnostic event instead of silently skipping the device patches that follow.*

*The asset-boundary diagnostic records the calling API, guest request path, mapped host path, success status, and Win32 error for `.bmp` and `.str` requests in a separate bounded log, with a per-extension bound so a large bitmap sweep cannot hide the rarer script requests. It never records file contents. Because path resolution happens inside the guest, returning `ERROR_FILE_NOT_FOUND` for absent candidates is the contract, and the error is committed after reporting so the diagnostic's own file I/O cannot overwrite what the guest reads back. Confirmed call sites and arguments are in the [asset loading path analysis](docs/analysis/ez2dj-asset-loading-path.md).*

### Windows x86 가상 디바이스 경계 / Windows x86 virtual-device boundary **[구현됨]**

주입 runtime은 launcher의 `--device-mock-lptdi` 정책이 켜졌을 때 `CreateFileA("\\.\LPTDI*")`를 import thunk에서 가로채 예약 범위 `0xFEED0001..0xFEED00FF`의 synthetic handle을 반환한다. 파일 wrapper는 이 핸들에 EOF형 read, 쓰기 거부, seek/size 미지원, `FILE_TYPE_CHAR`, 성공 close 계약을 제공한다. 정책이 꺼져 있거나 다른 경로이면 기존 VFS/host 경로로 내려간다. 기본 `--device-mock-lptdi`는 `DeviceIoControl`을 host에 전달해 실패 baseline을 보존한다. 실험 옵션은 canonical IAT를 runtime wrapper로 바꾼다. zero/full-success mode 외에 `--device-mock-lptdi-response-profile`은 공용 parser가 검증한 code별 exact-size bytes를 주입한다. `--device-mock-lptdi-target-state`는 공용 `lptdi_challenge_response` 변환으로 실행별 두 번째 input seed에 적응하는 response offset 4~11을 만들며, guest XOR 뒤 선택한 8바이트 상태가 남게 한다. 각 IOCTL 정책은 상호 배타적이고 synthetic handle·확인된 code에만 적용된다. `--lptdi-post-ioctl-trace`는 합성 래퍼도 API watch에 등록하고 guest 복귀 후 같은 allocation의 명령을 bounded single-step으로 기록하며, 외부 call은 one-shot return breakpoint로 건너뛴다. syscall resume breakpoint와 trace breakpoint가 같은 주소를 공유하면 원래 바이트 소유권을 trace에 넘기고, trace가 이미 복귀 대기 중일 때 외부 single-step을 중복 처리하지 않는다. launcher는 원본 entry의 첫 `GetVersion` caller에서 `.data` initializer 8 DWORD를 한 번 기록해 복원 여부를 검증한다. API trace는 USER32도 해석해 창·display 초기화의 파일 접근 전 실패를 귀속한다. 다른 handle은 항상 host로 전달한다.

*When launcher policy `--device-mock-lptdi` is enabled, the injected runtime intercepts `CreateFileA("\\.\LPTDI*")` at the import thunk and returns a synthetic handle in reserved range `0xFEED0001..0xFEED00FF`. File wrappers give that handle EOF-style reads, denied writes, unsupported seek/size, `FILE_TYPE_CHAR`, and successful close semantics. Disabled policy and all other paths fall through to the existing VFS/host path. The base option forwards DeviceIoControl to preserve the host-failure baseline, while experimental options replace the canonical IAT slot. Besides zero/full-success modes, `--device-mock-lptdi-response-profile` injects code-specific exact-size bytes validated by the shared parser. `--device-mock-lptdi-target-state` uses the shared `lptdi_challenge_response` transform to adapt response offsets 4 through 11 to each second-input seed, leaving the selected eight-byte state after the guest XOR. IOCTL policies are mutually exclusive and apply only to synthetic handles and confirmed codes. `--lptdi-post-ioctl-trace` also registers the synthetic wrapper as an API watch and records bounded same-allocation guest instructions after return, skipping external calls with one-shot return breakpoints. When a syscall-resume and trace breakpoint share an address, original-byte ownership transfers to the trace, and an external single-step is not reprocessed while that trace already awaits resume. The launcher records the eight-DWORD `.data` initializer once at the original entry's first `GetVersion` caller to verify restoration. API tracing also resolves USER32 and observes window/display startup calls so pre-file initialization failures remain attributable. Other handles always forward to the host.*

### Windows x86 논리 display mode 경계 / Windows x86 logical display-mode boundary **[구현됨]**

launcher의 `--hle-display-mode`는 원본 USER32 import thunk의 `ChangeDisplaySettingsExA`만 주입 runtime wrapper로 바꾼다. wrapper는 관찰된 null-device, 640×480×16, `CDS_UPDATEREGISTRY`, null-reserved 요청만 `DISP_CHANGE_SUCCESSFUL`로 처리하고 host desktop은 변경하지 않는다. 다른 mode 변경과 restore 요청은 host USER32로 전달한다. 이 정책은 guest가 기대하는 표시 mode 계약만 보존하며 실제 surface 출력과 scaling은 후속 그래픽 backend의 책임으로 남긴다.

*Launcher option `--hle-display-mode` replaces only the original USER32 `ChangeDisplaySettingsExA` import thunk with an injected-runtime wrapper. The wrapper returns `DISP_CHANGE_SUCCESSFUL` without changing the host desktop only for the observed null-device 640×480×16 `CDS_UPDATEREGISTRY` request with a null reserved argument. Other mode changes and restore requests forward to host USER32. This preserves the guest-visible mode contract while leaving real surface output and scaling to a later graphics backend.*

`--d3d-init-trace`는 1st SE 원본 초기화 coordinator의 다섯 return site에 일회성 breakpoint를 두고 반환값, DirectDraw/Direct3D COM 전역, phase marker를 기록한 뒤 원본 바이트를 복원한다. 이 진단은 guest 결과를 바꾸지 않는다. 현재 증거는 import-thunk display HLE 다음 경계가 `IDirect3D3::FindDevice` 같은 COM method임을 보여 준다. 후속 graphics HLE는 host COM vtable을 직접 수정하지 않고 `DirectDrawCreate` import에서 시작하는 교체 가능한 proxy interface 계층으로 설계해야 한다.

*`--d3d-init-trace` places one-shot breakpoints at the five return sites of the 1st SE initialization coordinator, records results, DirectDraw/Direct3D COM globals, and phase markers, then restores the original bytes. It does not change guest results. Current evidence places the next boundary after import-thunk display HLE at a COM method such as `IDirect3D3::FindDevice`; the later graphics HLE should therefore use replaceable proxy interfaces rooted at the `DirectDrawCreate` import rather than patching a host COM vtable in place.*

### Direct3D 3 OpenGL HLE / Direct3D 3 OpenGL HLE **[부분 구현 / Partially implemented]**

`IDirect3DTexture2::Load`는 같은 DirectDraw root에 속한 동일 크기 RGB565 texture surface 사이에서 pixel row와 `DDCKEY_SRCBLT`를 destination으로 복사하고 content revision을 증가시킨다. 따라서 GDI로 채운 source texture와 draw에 사용하는 destination texture가 분리된 원본 경로도 surface identity/cache 계약을 유지한다. 크기나 format 변환은 지원하지 않으며 명시적 실패를 반환한다. 상세 계약은 [Direct3D 3 texture Load 복사 설계](docs/design/20260829-088-direct3d-texture-load.md)에 둔다.

*`IDirect3DTexture2::Load` copies pixel rows and `DDCKEY_SRCBLT` between equal-sized RGB565 texture surfaces owned by the same DirectDraw root, then increments the destination content revision. This preserves the surface-identity/cache contract when the original populates one source texture through GDI but draws a separate destination texture. Format conversion and scaling are unsupported and return an explicit failure. See the [Direct3D 3 texture Load copy design](docs/design/20260829-088-direct3d-texture-load.md).*

`DrawPrimitive`의 플랫폼 중립 정점 경로는 기존 screen-space `D3DFVF_TLVERTEX(0x1c4)`뿐 아니라 원본에서 관찰된 32바이트 `D3DVERTEX(0x112)`와 `D3DLVERTEX(0x1e2)`를 지원한다. 변환 전 XYZ는 저장된 Direct3D row-vector world → view → projection matrix와 `D3DVIEWPORT2`를 거쳐 기존 `TransformedLitVertex` 명령으로 정규화된다. `0x112`의 normal은 lighting 증거가 확보될 때까지 건너뛰고 diffuse white를 사용하며, `0x1e2`의 `RESERVED1` 4바이트를 실제 stride와 field offset에 포함한다. 상세 계약은 [변환 전 Direct3D 3 정점 draw 설계](docs/design/20260829-089-untransformed-direct3d-draw.md)에 둔다.

*The platform-neutral `DrawPrimitive` vertex path supports not only the existing screen-space `D3DFVF_TLVERTEX(0x1c4)` but also the observed 32-byte `D3DVERTEX(0x112)` and `D3DLVERTEX(0x1e2)` layouts. Pre-transform XYZ passes through retained Direct3D row-vector world, view, and projection matrices plus `D3DVIEWPORT2`, producing the existing `TransformedLitVertex` command. The `0x112` normal is skipped with white diffuse until lighting evidence is available, while the four-byte `RESERVED1` field in `0x1e2` participates in the actual stride and offsets. See the [untransformed Direct3D 3 vertex draw design](docs/design/20260829-089-untransformed-direct3d-draw.md).*

공용 `LegacyFixedFunctionState`는 원본 Direct3D blend ABI 중 확인된 source/destination color·alpha 계수를 `BlendFactor`로 정규화한다. 목적지 알파 계수 7/8은 각각 `GL_DST_ALPHA`/`GL_ONE_MINUS_DST_ALPHA`로 변환되어 logical framebuffer에 전달되며, decoder와 backend의 매핑은 함께 회귀 검증한다. 이 계수 누락으로 facade가 draw를 사전 거부하여 정점과 texture가 backend에 도달하지 못하는 일이 없도록 한다. 상세 변경은 [목적지 알파 블렌드 지원 설계](docs/design/20260913-264-destination-alpha-blending.md)에 둔다.

*The shared `LegacyFixedFunctionState` normalizes the confirmed Direct3D source/destination color and alpha blend ABI values into `BlendFactor`. Destination-alpha factors 7 and 8 map to `GL_DST_ALPHA` and `GL_ONE_MINUS_DST_ALPHA` before reaching the logical framebuffer, and the decoder/backend mapping is regression-tested together. This prevents the facade from rejecting a draw before its vertices and texture reach the backend because a factor is missing. See the [destination-alpha blend support design](docs/design/20260913-264-destination-alpha-blending.md).*

변환 정점의 `z`는 `LegacyFixedFunctionState::depth_test_enabled`에 따라 backend clip 입력으로 선택된다. depth test가 비활성화된 원본 draw는 `0xCCCCCCCC`와 같은 미초기화 z를 남길 수 있으므로 공용 `ResolveLegacyClipDepth`가 `0.5`를 선택하고, depth test가 활성화된 draw는 guest z를 보존한다. 이는 OpenGL의 depth test와 별개인 clip-volume 검사가 유효하지 않은 z 때문에 textured primitive를 제거하는 문제를 막으며, 정점의 다른 필드와 guest 실행 경로는 바꾸지 않는다. 상세 근거와 검증 상태는 [변환 정점의 OpenGL 클립 깊이 보정 설계](docs/design/20260913-266-transformed-vertex-clip-depth.md)에 둔다.

*Transformed-vertex `z` is selected for backend clipping from `LegacyFixedFunctionState::depth_test_enabled`. Because an original depth-disabled draw can leave z at an uninitialized value such as `0xCCCCCCCC`, the shared `ResolveLegacyClipDepth` selects `0.5`; depth-enabled draws preserve guest z. This prevents OpenGL clip-volume testing, which is separate from the depth test, from removing textured primitives because of invalid z, without changing other vertex fields or the guest execution path. Evidence and verification are in the [transformed-vertex OpenGL clip-depth design](docs/design/20260913-266-transformed-vertex-clip-depth.md).*

`IDirect3DDevice3::DrawIndexedPrimitiveVB`는 facade가 소유한 `LegacyVertexBuffer`와 16-bit index array를 받는다. 공용 `ExpandIndexedVertices`가 모든 index를 descriptor의 vertex count에 대해 범위 검사하고 stride 단위 stream으로 전개한 뒤 기존 transformed/untransformed decoder에 전달한다. draw 중 lock된 buffer, 다른 DirectDraw root의 facade, 잘못된 guest pointer와 범위 밖 index는 명시적으로 실패한다. `D3DPT_TRIANGLELIST`는 공용 `PrimitiveTopology::kTriangleList`로 유지되어 SDL/OpenGL backend의 `GL_TRIANGLES`에 대응한다. 상세 근거는 [작업 095 설계](docs/design/20260829-095-execute-zero-crash.md)에 둔다.

*`IDirect3DDevice3::DrawIndexedPrimitiveVB` consumes a facade-owned `LegacyVertexBuffer` and a 16-bit index array. Shared `ExpandIndexedVertices` bounds-checks every index against the descriptor vertex count, expands a stride-sized stream, and passes it through the existing transformed or untransformed decoder. Locked buffers, facades from another DirectDraw root, invalid guest pointers, and out-of-range indices fail explicitly. `D3DPT_TRIANGLELIST` remains `PrimitiveTopology::kTriangleList` through the common command and maps to `GL_TRIANGLES` in the SDL/OpenGL backend. See the [Task 095 design](docs/design/20260829-095-execute-zero-crash.md).*

그래픽 HLE는 `DirectDrawCreate` 및 `DirectDrawCreateEx` import gate, guest 소유 32비트 COM facade, 플랫폼 중립 legacy graphics core, 교체 가능한 `RenderBackend`의 네 계층으로 구성한다. COM facade는 `IDirectDraw`/`IDirectDraw4`/`IDirectDraw7`/`IDirect3D3`/`IDirect3DDevice3` identity와 vtable, reference count를 보존한다. EZ2DJ 4th Trax는 `GetProcAddress("DirectDrawCreateEx")`로 `IID_IDirectDraw7` 및 `IID_IDirect3D7`을 요청하므로, COM facade는 DirectX 7 진입 인터페이스 질의와 호환 vtable 어댑터를 제공한다. legacy core는 guest surface의 16비트 layout·pitch·lock 상태와 정규화된 fixed-function state를 소유하며 OpenGL type이나 platform context API를 포함하지 않는다.

*The graphics HLE is split into a `DirectDrawCreate` and `DirectDrawCreateEx` import gate, guest-owned 32-bit COM facades, a platform-neutral legacy graphics core, and a replaceable `RenderBackend`. The COM layer preserves `IDirectDraw`/`IDirectDraw4`/`IDirectDraw7`/`IDirect3D3`/`IDirect3DDevice3` interface identity, vtables, and reference counts. EZ2DJ 4th Trax requests `IID_IDirectDraw7` and `IID_IDirect3D7` via `GetProcAddress("DirectDrawCreateEx")`, so the COM facade provides DirectX 7 entry query support and compatible vtable adapters. The common core owns guest-visible surface layouts and normalized fixed-function state without OpenGL or platform context types.*

첫 backend는 desktop OpenGL이다. deprecated OpenGL fixed-function state에 직접 의존하지 않고 내부 shader로 관찰된 Direct3D 3 상태를 재현한다. `FindDevice`는 host HAL 열거를 전달하지 않고 구현된 capability만 선언하는 가상 hardware device를 노출한다. primary/back/depth/texture surface는 논리 객체로 유지하며, 기존 640×480×16 display-mode HLE와 결합해 host desktop mode를 바꾸지 않고 present한다. 상세 단계와 미확정 항목은 [Direct3D 3 OpenGL HLE 설계](docs/design/20260825-061-direct3d3-opengl-hle.md)에 둔다.

Windows x86 COM 연결은 DirectX 버전에 따라 파일이 나뉘지만 구현은 하나다. DirectX는 상위 인터페이스를 하위 인터페이스의 확장으로 정의하며, 이 프로젝트도 그 구조를 그대로 따른다. `IDirectDraw7`과 `IDirectDrawSurface7`은 버전 4 vtable을 슬롯 단위로 반복한 뒤 뒤에 덧붙인 형태이고, `IDirect3DDevice7`은 슬롯을 재배열했을 뿐 이어받은 메서드의 의미는 같다. 따라서 DirectDraw 객체마다 facade 객체는 하나이며, 상위 버전은 그 객체를 감싸는 wrapper가 아니라 같은 객체에 설치된 다른 vtable이다. 동작이 바뀌지 않은 슬롯에는 DirectX 6 함수 포인터가 그대로 들어가고, 바뀌었거나 새로 생긴 슬롯만 DirectX 7 코드를 가진다. 공용 구현과 상태는 DirectX 6 경로인 `src/platform/windows/direct3d3_com_facade.*`가 보유하고, DirectX 7 vtable은 `src/platform/windows/directdraw7_com_facade.*`, `src/platform/windows/direct3d7_com_facade.*`, `src/platform/windows/direct3d7_vertex_buffer_facade.*`에 둔다. 두 계층 사이의 내부 경계는 `src/platform/windows/directdraw_legacy_interop.h`이며, 아직 구현되지 않은 상위 버전 슬롯은 조용히 성공하지 않고 `not-implemented` 한 줄을 남긴다. 위임 규칙과 슬롯별 등급은 [DirectX 7 facade의 DirectX 6 구현 위임 설계](docs/design/20260905-182-directx7-legacy-delegation.md)에 둔다. 실제 렌더러는 `include/re2dj/graphics/sdl3_opengl_backend.h`와 `src/graphics/sdl3_opengl_backend.cpp`의 공용 SDL3/OpenGL backend다. Windows에서는 SDL3가 원본이 만든 HWND를 external window로 감싸고, Linux에서는 같은 backend가 SDL window를 소유할 수 있다. SDL3가 video subsystem, OpenGL context, 함수 해석, drawable 크기와 swap을 제공하므로 WGL과 `opengl32` 직접 의존성은 없다. desktop은 OpenGL 2.1/GLSL 1.20 분기를 사용한다. `LegacyDrawCommand`와 `LegacyFixedFunctionState`는 확인된 XYZRHW, stage-zero modulate, linear filter, alpha test와 blend factor를 전달한다. `LegacyTextureView`는 RGB565 backing, stable identity/revision과 inclusive source color-key 범위를 보존하고 backend는 surface별 texture cache에서 변경 revision만 upload한다. `LegacyVertexBuffer`와 Windows `IDirect3DVertexBuffer`(DX6)와 `IDirect3DVertexBuffer7`(DX7) facade는 XYZ/NORMAL/TEX1 121개 정점을 3,872바이트 storage로 보존한다. 상세 변경은 [SDL3/OpenGL 공용 backend 설계](docs/design/20260827-076-sdl3-opengl-shared-backend.md)에 둔다.

draw 경계는 제품 실행과 조사 실행을 분리한다. `ReportDrawDiagnostic`, `ReportLateDrawDiagnostic`, `ReportTransformDiagnostic`은 draw 경로 안에서 실행되며 dirty 서피스의 전 픽셀을 훑고 긴 레코드를 만든다. 이 셋과 backend의 draw별 `glGetError`는 주입 런타임의 `g_re2dj_graphics_draw_diagnostics` export가 켜졌을 때만 동작한다. 기본값은 0이고, launcher probe의 `--graphics-draw-diagnostics`만 이 값을 켠다. 초기화와 일회성 진단은 스위치와 무관하게 계속 기록한다. 근거: [draw 경로 진단 게이트 작업 로그](docs/work-logs/20260907-221-draw-path-diagnostic-gate.md).

*The draw boundary separates product runs from investigation runs. `ReportDrawDiagnostic`, `ReportLateDrawDiagnostic`, and `ReportTransformDiagnostic` run inside the draw path, scanning every texel of a dirty surface and formatting long records. Those three and the backend's per-draw `glGetError` are active only when the injected runtime's `g_re2dj_graphics_draw_diagnostics` export is set, which defaults to 0 and is raised only by the launcher probe's `--graphics-draw-diagnostics`. Initialization and one-shot diagnostics keep recording regardless. See the [draw-path diagnostic gate work log](docs/work-logs/20260907-221-draw-path-diagnostic-gate.md).*

명시적인 `--graphics-draw-diagnostics` 조사 실행은 complete diagnostic capture로 취급한다. 이 모드에서는 Direct3D의 `LateDraw`, `MusicSelectDiscDraw`, `DrawPrimitive`, `TransformDraw`, `FrameDraws`와 관련 그래픽 진단의 실행 전체 상한을 우회하고, 텍스처별 최초 성공 1회 축약도 사용하지 않는다. 같은 명시적 모드는 상관관계가 필요한 VFS asset/open/file/profile 진단의 상한도 우회한다. 옵션이 없는 실행은 기존 bounded 정책을 유지한다. 그래픽 trace formatter는 필요한 길이를 계산하므로 긴 레코드를 고정 1024바이트에서 자르지 않는다. 이 정책은 로그 관찰만 바꾸며 원본 실행 파일, VFS 반환 바이트, 렌더링 결과는 바꾸지 않는다. 근거: [진단 로그 완전성 설계](docs/design/20260913-265-diagnostic-trace-completeness.md).

*An explicit `--graphics-draw-diagnostics` investigation is treated as complete diagnostic capture. In this mode, the full-run limits for Direct3D `LateDraw`, `MusicSelectDiscDraw`, `DrawPrimitive`, `TransformDraw`, `FrameDraws`, and related graphics diagnostics are bypassed, and successful textured draws are no longer reduced to the first report per texture. The same explicit mode bypasses the VFS asset/open/file/profile diagnostic limits needed for correlation. Runs without the option retain the existing bounded policy. The graphics trace formatter sizes each record before writing, so long records are not cut at 1,024 bytes. This changes observation only; it does not change the original executable, VFS-returned bytes, or rendered output. See the [diagnostic trace completeness design](docs/design/20260913-265-diagnostic-trace-completeness.md).*

re2DJ는 호스트의 표시 모드를 바꾸지 않는다. 원본은 창을 만든 직후 `ChangeDisplaySettingsExA`로 자신의 해상도를 요구하고, 비정상 종료가 그 변경을 되돌리지 못한 채 남긴다. `src/platform/windows/display_mode_boundary.*`가 그 요청을 요청 모양과 무관하게 전부 흡수하고 호스트 API를 부르지 않은 채 `DISP_CHANGE_SUCCESSFUL`을 돌려준다. 원본은 이 반환을 성공·재시작·실패로 나눠 분기하며 실패 분기가 `PostQuitMessage(0)`으로 가므로, 성공을 답해야 게임이 진행한다. 이것은 프로파일 기능이 아니라 호스트 무결성 보장이므로 진단 옵션으로 켜고 끄지 않으며, 정적 IAT 슬롯과 `GetProcAddress` 동적 해석 양쪽에 설치된다. DirectDraw의 `SetDisplayMode`와 `SetCooperativeLevel`은 값만 기록하므로 같은 정책을 이미 지킨다. 따라서 지원되는 표시 형태는 원본 논리 해상도를 확대한 창과, 현재 데스크탑 해상도를 그대로 둔 채 모니터를 덮는 borderless 창 두 가지뿐이다. 근거는 [호스트 표시 모드 불변 정책 설계](docs/design/20260905-183-host-display-mode-policy.md)에 둔다.

*re2DJ never changes the host display mode. The original asks for its own resolution through `ChangeDisplaySettingsExA` right after creating its window, and an abnormal exit leaves that change in place. `src/platform/windows/display_mode_boundary.*` absorbs every such request regardless of its shape, returning `DISP_CHANGE_SUCCESSFUL` without calling the host API - the original branches on that return and its failure branch reaches `PostQuitMessage(0)`, so success is what lets the game run. This is a host-integrity guarantee rather than a profile capability, so it has no diagnostic switch and is installed on both the static IAT slot and the `GetProcAddress` path. DirectDraw's `SetDisplayMode` and `SetCooperativeLevel` only record their values and already honour the same policy. The supported presentations are therefore a scaled window and a borderless window covering the monitor at the unchanged desktop resolution.*

그래픽 HLE 경계의 진단 기록은 `src/platform/windows/graphics_trace_log.*` 한 곳으로 모은다. 이 단위가 런처가 채우는 `g_re2dj_graphics_trace_path` export를 소유하고, append 핸들 하나를 잠금으로 공유하며, 같은 줄을 디버거로도 내보낸다. 디버거가 붙지 않는 제품 실행에서는 이 파일이 유일한 그래픽 증거이므로, COM facade와 창 정책은 `OutputDebugStringA`를 직접 부르지 않고 이 sink를 통해 기록한다.

게스트가 오류를 모달 대화상자로만 알리면 detached 실행에서는 아무도 닫아 줄 수 없고 그 문구도 로그에 남지 않는다. `src/platform/windows/message_box_boundary.*`는 `--hle-message-box` 진단 옵션이 켜졌을 때 `USER32!MessageBoxA` / `MessageBoxW` 진입점을 기록기로 우회시켜 caption과 text를 남기고 기본 응답을 즉시 반환한다. 패킹된 게스트는 import를 export table 순회로 해석하므로 import 슬롯 교체가 아니라 진입점 우회여야 도달한다. 런처가 적재 후에 플래그를 쓰기 때문에 설치는 `DllMain`이 아니라 게스트가 가장 먼저 닿는 HLE 진입점에서 최초 1회 수행한다.

*Windows x86 COM wiring is split by DirectX version across files but has one implementation behind it. DirectX defines each later interface as an extension of the earlier one - `IDirectDraw7` and `IDirectDrawSurface7` repeat their version 4 vtables slot for slot and append to them, and `IDirect3DDevice7` only reorders the methods it inherits - so this project takes that structure literally: there is one facade object per DirectDraw object, and a later version is a different vtable installed on that same object rather than a wrapper around it. A slot whose behavior did not change holds the DirectX 6 function pointer itself; only changed or newly added slots carry DirectX 7 code. The shared implementation and state live in the DirectX 6 path, `src/platform/windows/direct3d3_com_facade.*`, while the DirectX 7 vtables live in `src/platform/windows/directdraw7_com_facade.*`, `src/platform/windows/direct3d7_com_facade.*`, and `src/platform/windows/direct3d7_vertex_buffer_facade.*`. The internal boundary between the two layers is `src/platform/windows/directdraw_legacy_interop.h`, and a later-version slot with no implementation yet records a `not-implemented` line instead of succeeding silently; the per-slot grades are in [the delegation design](docs/design/20260905-182-directx7-legacy-delegation.md). The actual renderer remains the shared SDL3/OpenGL backend in `include/re2dj/graphics/sdl3_opengl_backend.h` and `src/graphics/sdl3_opengl_backend.cpp`.*

*Diagnostic recording for the graphics HLE boundary is centralized in `src/platform/windows/graphics_trace_log.*`, which owns the launcher-filled `g_re2dj_graphics_trace_path` export, shares one locked append handle, and mirrors each line to the debugger. That file is the only graphics evidence a detached product run leaves, so the COM facades and window policy record through this sink rather than calling `OutputDebugStringA` directly.*

*When a guest reports a failure only through a modal dialog, a detached run has nobody to dismiss it and the wording never reaches the log. `src/platform/windows/message_box_boundary.*` redirects the `USER32!MessageBoxA` / `MessageBoxW` entry points to a recorder under the `--hle-message-box` diagnostic option, logging the caption and text and answering immediately. A packed guest resolves imports by walking the export table, so redirecting the entry point rather than an import slot is what reaches it, and because the launcher fills the flag after load, installation happens once from the earliest HLE entry point the guest reaches rather than from `DllMain`.*

Win32 창 정책은 `src/platform/windows/window_mode.*`와 `host_window_shell.*`에 분리한다. 표준 top-level host shell은 DWM, `re2DJ v<version> (<OS>/<arch> <config>) - Build <date> - SDL3 OpenGL - FPS : <value>` 상태 제목, icon, resize/fullscreen과 close를 소유하고, 원본 HWND는 `WS_CHILD`로 host client를 채우며 SDL3/OpenGL external rendering 대상에 그대로 남는다. 성공한 DirectDraw `Flip`은 약 1초마다 소수점 한 자리 FPS를 갱신한다. SDL 초기화 전에는 96-DPI frame으로 1차 배치하고, SDL video가 per-monitor DPI context를 확정한 뒤 `GetDpiForWindow`와 `AdjustWindowRectExForDpi`로 window mode를 다시 적용한다. 144-DPI 검증에서 outer `1302×1016`, host/guest client 1280×960과 DWM 기본 caption을 확인했다. 외부 `--fullscreen`은 같은 host shell을 monitor bounds `WS_POPUP`으로 바꾸며 host display mode는 변경하지 않는다. host close는 guest/SDL teardown을 먼저 호출할 때 확인된 termination 정체를 피하기 위해 current process 자신에게만 `TerminateProcess(..., 0)`을 즉시 사용한다. lifetime worker는 host HWND의 파괴·숨김을 감시하고 최소화는 종료로 오인하지 않는다. 상세 창 정책은 [Win32 창 모드와 메시지 pump 설계](docs/design/20260828-084-window-mode-message-pump.md), 종료 전달은 [Win32 창 닫기와 원본 프로세스 종료 설계](docs/design/20260829-090-window-close-process-exit.md), 제목과 기본 확대는 [설계 091](docs/design/20260829-091-window-title-default-scale.md), DWM/DPI 보정은 [설계 092](docs/design/20260829-092-dwm-caption-preservation.md)에 둔다.

SDL3/OpenGL backend는 guest draw와 clear를 native window default framebuffer에 직접 기록하지 않는다. 초기화 때 논리 display mode 크기의 RGB565 color texture와 depth16 renderbuffer를 framebuffer로 묶고, 모든 guest fixed-function draw를 그 대상에서 수행한다. `Present`만 default framebuffer를 현재 host pixel 크기로 설정하고 blend/depth를 끈 nearest full-screen copy를 수행한다. 따라서 host DPI·resize가 guest texture filtering이나 blend 누적 정밀도를 바꾸지 않는다. framebuffer object entry point 또는 완전한 RGB565/depth16 attachment가 없으면 direct-render fallback 없이 backend 초기화가 실패한다. 상세 근거와 검증 상태는 [RGB565 논리 렌더 대상 설계](docs/design/20260905-195-rgb565-logical-render-target.md)에 둔다. 선택 옵션인 32비트 색 모드에서는 color texture가 `GL_RGB8`이다(아래 "2026-09-29 32비트 트루컬러 표면" 절).

컬러키는 alpha 값이 아니라 **discard 조건**으로 구현한다. Direct3D는 `D3DRENDERSTATE_COLORKEYENABLE`이 켜진 동안 키에 일치하는 texel을 blend factor와 무관하게 버린다. texel alpha만으로 표현하면 `srcblend=ONE`, `dstblend=ZERO`인 복사 blend에서 keyed texel이 키 색 그대로 기록된다. backend는 upload 때 일치 texel의 alpha를 0으로 두고, 게스트 `COLORKEYENABLE` 상태로만 gate되는 shader 분기에서 그 texel을 버린다. 게스트 alpha test 분기는 원래 의미대로 별도로 남으며, 컬러키가 꺼진 draw는 영향을 받지 않는다. 특히 `srcblend=ZERO`, `dstblend=SRCCOLOR`인 곱셈 mask pass는 keyed texel을 버리면 mask가 무의미해지므로 반드시 게스트 상태로만 판단한다.

*Color keying is implemented as a **discard condition**, not as an alpha value. While `D3DRENDERSTATE_COLORKEYENABLE` is set, Direct3D drops key-matching texels regardless of the blend factors; expressing that through texel alpha alone writes them out in the key color under a copy blend of `ONE` and `ZERO`. The backend leaves matching texels at zero alpha during upload and drops them in a shader branch gated solely on the guest's `COLORKEYENABLE`. The guest alpha-test branch keeps its own meaning separately, and draws with keying disabled are unaffected — in particular the multiplicative mask passes using `srcblend=ZERO` and `dstblend=SRCCOLOR`, which would become meaningless if their keyed texels were dropped.*

DirectDraw 2D 경로는 texture뿐 아니라 `DDSCAPS_OFFSCREENPLAIN` surface에도 같은 RGB565 GDI backing과 stable identity/revision을 부여한다. 공용 `CopyRgb565Rectangle` 계약이 동일 크기 사각형과 inclusive source color key를 처리하며, facade의 `Blt`/`BltFast`는 CPU backing을 갱신하고 primary/back destination이면 같은 source 사각형을 OpenGL frame에 합성한다. stretch, ROP과 destination color key처럼 아직 관찰되지 않은 조합은 명시적으로 거절한다.

직접 x86 `IN`/`OUT`에는 교체 가능한 Win32 import가 없으므로 `Ez2DjIoBoard`의 플랫폼 중립 button/turntable/coin/light 상태를 `LegacyIoPortBus`가 raw byte 계약으로 변환하여 `ExecutionBackend` 경계에 둔다. EZ2Dancer는 별도 `Ez2DancerIoBoard`와 `Ez2DancerIoPortBus`가 16비트 port 계약을 담당한다. Windows x86 진단 모드는 확인된 helper RVA와 opcode, port만 debugger의 `EXCEPTION_PRIV_INSTRUCTION`으로 처리한다. 실제 실행용 `--run-detached`는 원본 entry 복원, runtime 주입과 IAT 검증 뒤 debugger를 분리하고, injected runtime의 vectored handler가 같은 제한 계약을 process 안에서 처리한다. 원본 instruction byte는 수정하지 않는다. EZ2DJ 기본 idle bytes는 `ff ff 80 80 00 ff`다. port `0x105`는 keyboard coin false→true마다 1 증가하고 read로 소비되지 않는 modulo-256 counter다. 제품의 선택적 `--io-config`는 host INI의 absolute path를 runtime에 주입한다. 공통 Win32 key parser 뒤에서 byte-wide 프로필은 `Ez2DjKeyboardInput`, word-wide 프로필은 `Ez2DancerKeyboardInput`을 선택한다. Linux host는 같은 INI를 공용 로더 `input/io_bindings`(작업 444)로 읽어 `OriginalRunEnvironment::io_bindings`에 두고, `native_legacy_io`가 항목마다 키 또는 게임패드 컨트롤이 쥐어졌는지 본다. 게임패드는 `input/gamepad.h`의 host 중립 컨트롤 이름과 `input/sdl3_gamepad_reader`(SDL3 gamepad 서브시스템, `re2dj_sdl3_gamepad`)로 들어오며, `SdlHostPresentation`이 매 프레임 이벤트 펌프 뒤에 `HostInputState::gamepad`를 채운다. Windows host는 아직 키보드만 받는다. EZ2Dancer는 `coin` binding을 `0x304`의 rising-edge counter 호환 경로로 제공하며, 원본 register 의미·port·bit·극성은 여전히 미확정이다. 외부 공개 구현에서 교차 확인한 물리 의미는 [EZ2DJ I/O 분석](docs/analysis/ez2dj-io-map.md)과 [EZ2Dancer I/O 분석](docs/analysis/ez2dancer-io-map.md)처럼 원본 확인 사실과 구분한다.

*Direct x86 `IN`/`OUT` has no replaceable Win32 import. `LegacyIoPortBus` therefore adapts platform-neutral state owned by `Ez2DjIoBoard` to the raw-byte contract at the `ExecutionBackend` boundary. EZ2Dancer uses separate `Ez2DancerIoBoard` and `Ez2DancerIoPortBus` components for its 16-bit port contract. The Windows x86 diagnostic path accepts only confirmed helper RVAs, opcodes, and ports; detached product execution uses the equivalent injected vectored handler without modifying original instruction bytes. EZ2DJ idle bytes are `ff ff 80 80 00 ff`, and port `0x105` is a modulo-256 coin counter. Optional product `--io-config` injects an absolute host INI path. Behind a shared Win32 key parser, byte-wide profiles select `Ez2DjKeyboardInput` and word-wide profiles select `Ez2DancerKeyboardInput`. The Linux host reads the same INI through the shared loader `input/io_bindings` (task 444) into `OriginalRunEnvironment::io_bindings`, and `native_legacy_io` treats an entry as pressed when its key or its gamepad control is held. Gamepads arrive through the host-neutral control names of `input/gamepad.h` and `input/sdl3_gamepad_reader` (SDL3's gamepad subsystem, `re2dj_sdl3_gamepad`), and `SdlHostPresentation` fills `HostInputState::gamepad` after every frame's event pump. The Windows host still takes the keyboard only. EZ2Dancer exposes a `coin` binding through a rising-edge counter compatibility path at `0x304`; the original register meaning, port, bit, and polarity remain unresolved. Meanings cross-checked against a public implementation remain distinct from facts confirmed in the original executable.*

*The graphics HLE is split into a `DirectDrawCreate` import gate, guest-owned 32-bit COM facades, a platform-neutral legacy graphics core, and a replaceable `RenderBackend`. The COM layer preserves interface identity, vtables, and reference counts; the common core owns guest-visible surface layouts and normalized fixed-function state without OpenGL or platform context types. Desktop OpenGL is the first backend. Internal shaders reproduce observed Direct3D 3 behavior, while a conservative virtual hardware device replaces host HAL enumeration. Logical primary, back, depth, and texture surfaces present through the existing 640×480×16 display contract without changing the host desktop mode.*

present가 언제 반환하는지는 공용 코어의 `PresentSync`(vsync / immediate / adaptive)가 정한다. 예전에는 swap interval을 한 번도 설정하지 않아 드라이버 기본값에 맡겨져 있었고, 그래서 같은 코드가 호스트마다 다른 present 타이밍으로 동작할 수 있었다. 이제 backend가 명시적으로 설정한 뒤 `SDL_GL_GetSwapInterval` 결과를 host에 보고하며, 드라이버가 요청을 거부해도 초기화는 실패하지 않는다. 기본값은 이전의 실효 동작인 vsync다. 제품 `--vsync on|off|adaptive`와 launcher `--present-sync`가 target profile 기본값을 덮어쓰며, Windows에서는 주입 런타임의 `g_re2dj_present_sync` export를 통해 전달된다. 이 정책은 게스트의 자체 페이싱을 대체하지 않는다. `ez2dj3rd`는 세 정책 모두에서 56~57 fps로 같았고, 그 상한은 vsync가 아니라 게스트 자신이다.

*When a present returns is decided by `PresentSync` (vsync, immediate, adaptive) in the shared core. The swap interval used to be left unset, so the driver default decided it and the same code could present with different timing per host. The backend now sets it explicitly and reports the `SDL_GL_GetSwapInterval` result to the host; a driver that refuses the request does not fail initialization. The default is vsync, the previous effective behavior. The product's `--vsync on|off|adaptive` and the launcher's `--present-sync` override the target-profile default, reaching the guest on Windows through the injected runtime's `g_re2dj_present_sync` export. The policy does not replace a guest's own pacing: `ez2dj3rd` measured 56-57 fps under all three policies, so what caps it is the guest rather than vsync.*

*Only the Windows x86 COM bridge remains platform-specific. The shared SDL3/OpenGL backend wraps the original HWND as an external SDL window on Windows and can own an SDL window on Linux. SDL3 supplies video initialization, context management, GL symbol resolution, drawable sizing, and swapping, removing direct WGL and `opengl32` dependencies. Desktop builds use OpenGL 2.1 with GLSL 1.20. Stable texture identity/revision, inclusive RGB565 source keys, and the observed fixed-function state feed the same per-surface texture cache on both desktop builds. See the [shared SDL3/OpenGL backend design](docs/design/20260827-076-sdl3-opengl-shared-backend.md).*

Win32 host shell은 `Alt+1`, `Alt+2`, `Alt+3`으로 640x480, 1280x960, 1920x1440 windowed client 배율을 선택할 수 있으며, guest child WndProc subclass와 host WndProc 양쪽에서 실제 포커스 경로를 받아 마우스 double-click fullscreen 전환을 처리한다. windowed `WM_SIZING`은 client 4:3 비율을 유지하도록 outer rectangle을 보정하고, SDL3/OpenGL `Present`는 fullscreen monitor가 다른 비율이어도 중앙 4:3 viewport와 검정 letterbox/pillarbox를 사용한다. SDL3가 external HWND를 wrapping하면서 WndProc를 다시 설치할 수 있으므로, 기존 host shell이 재사용될 때도 입력 subclass를 확인하고 필요하면 현재 guest WndProc를 새 원본으로 감싸는 정책을 유지한다.

Alt+3 전환 중에는 host style에 `WS_VISIBLE`을 유지하고 mode transition 상태를 lifetime 검사와 공유한다. 따라서 style/frame 갱신 사이의 일시적인 숨김을 close로 오인하지 않으며, 전환 밖의 지속적인 숨김만 유예 후 종료 조건으로 사용한다.

*The Win32 host shell supports `Alt+1`, `Alt+2`, and `Alt+3` for 640x480, 1280x960, and 1920x1440 windowed client scales. Both the guest-child WndProc subclass and the host WndProc cover the actual focus route, and a client double-click toggles fullscreen. Windowed `WM_SIZING` corrects the outer rectangle to preserve a 4:3 client, while SDL3/OpenGL `Present` uses a centered 4:3 viewport with black letterbox/pillarbox bars on a differently shaped fullscreen monitor. Because SDL3 may replace the WndProc while wrapping an external HWND, reusing an existing host shell also verifies the input subclass and, when necessary, wraps the current guest WndProc as the new delegate. During mode transitions the host style retains `WS_VISIBLE`, and the lifetime checks share the transition state; transient hiding is not treated as close, while sustained hiding outside a transition is given a grace period.*

*Win32 window policy is split between `src/platform/windows/window_mode.*` and `host_window_shell.*`. A standard top-level host shell owns DWM, the `re2DJ v<version> (<OS>/<arch> <config>) - Build <date> - SDL3 OpenGL - FPS : <value>` status title, icon, resize/fullscreen, and close; the original HWND remains the SDL3/OpenGL external rendering target as a `WS_CHILD` filling the host client. Successful DirectDraw `Flip` calls update one-decimal FPS about once per second. The first placement uses the pre-SDL 96-DPI frame, then window mode is reapplied with `GetDpiForWindow` and `AdjustWindowRectExForDpi` after SDL video establishes the per-monitor DPI context. Validation at 144 DPI confirms a `1302x1016` outer window, 1280x960 host/guest clients, and the native DWM caption. External `--fullscreen` changes the same host shell to monitor-bounds `WS_POPUP` without changing the host display mode. Because guest/SDL teardown before termination reproduced the confirmed stall, host close immediately uses `TerminateProcess(..., 0)` only on the current process. A lifetime worker monitors host-HWND destruction/hiding without treating minimization as close. See the [window-mode design](docs/design/20260828-084-window-mode-message-pump.md), [close design](docs/design/20260829-090-window-close-process-exit.md), [title/default-scale design](docs/design/20260829-091-window-title-default-scale.md), and [DWM/DPI design](docs/design/20260829-092-dwm-caption-preservation.md).*

*The SDL3/OpenGL backend does not write guest draws and clears directly to the native-window default framebuffer. Initialization binds an RGB565 color texture and depth16 renderbuffer at the logical display-mode dimensions into a framebuffer, and all guest fixed-function draws run there. Only `Present` selects the default framebuffer at the current host pixel dimensions, disables blend/depth, and performs a nearest full-screen copy. Host DPI and resize therefore cannot change guest texture filtering or blend-accumulation precision. Missing framebuffer-object entry points or an incomplete RGB565/depth16 attachment fails backend initialization without a direct-render fallback. Evidence and verification status are in the [RGB565 logical render-target design](docs/design/20260905-195-rgb565-logical-render-target.md). In the optional 32-bit colour mode the colour texture is `GL_RGB8` (see "2026-09-29 32-bit true-color surfaces" below).*

SDL3/OpenGL backend는 Direct3D facade의 cull mode를 공용 `LegacyFixedFunctionState`로 전달받습니다. triangle draw에 D3D/OpenGL winding 변환과 back-face culling을 적용하며 line draw와 Present pass에서는 culling을 끕니다. 상세 설계는 [Direct3D cull 상태 전달](docs/design/20260905-197-direct3d-cull-forwarding.md)에 둡니다.

Direct3D facade는 guest의 전체 대상 `Clear`와 `DDBLT_COLORFILL`을 같은 logical RGB565 render target clear 경계로 전달합니다. backend가 첫 draw 전에 생성되지 않았으면 clear 색상을 pending 상태로 보존한 뒤 초기화 직후 적용하므로, `DDSCAPS_FLIP` retained-frame 경로에서도 원본의 명시적 화면 초기화가 유실되지 않습니다. 근거와 확인 상태는 [ez2d2m / ez2dj4th graphics clear 분석](docs/analysis/ez2d2m-ez2dj4th-graphics-clear.md)에 둡니다.

*The Direct3D facade sends full-target guest `Clear` and `DDBLT_COLORFILL` through the same logical RGB565 render-target clear boundary. If the backend does not exist before the first draw, the clear color is kept pending and applied immediately after initialization, so explicit guest clears are not lost on the `DDSCAPS_FLIP` retained-frame path. Evidence and confirmation status are in the [ez2d2m / ez2dj4th graphics-clear analysis](docs/analysis/ez2d2m-ez2dj4th-graphics-clear.md).*

*The SDL3/OpenGL backend now receives cull mode through the shared `LegacyFixedFunctionState` from the Direct3D facade. Triangle draws enable `GL_CULL_FACE` with a D3D-to-OpenGL winding conversion; `D3DCULL_NONE/CW/CCW` are forwarded explicitly, while line draws and the Present pass disable culling. See the [Direct3D cull-state forwarding design](docs/design/20260905-197-direct3d-cull-forwarding.md).*

`DecodeLegacyBlendFactor`는 D3D blend enum을 공용 상태로 변환하고 backend는 목적지 색상 인자 `DESTCOLOR`/`INVDESTCOLOR`를 `GL_DST_COLOR`/`GL_ONE_MINUS_DST_COLOR`로 전달합니다. draw 실패 진단은 표면별 첫 실패를 반복 실패 budget과 별도로 기록하고, 첫 성공·실패에 frame과 decode된 화면 bounds를 남깁니다. 실제 RGB565 backend의 다중 pass 합성은 자산 없는 `re2dj_opengl_blend_probe`로 별도 검증합니다. 상세 내용은 [목적지 색상 마스크 합성 설계](docs/design/20260905-198-destination-color-mask-blending.md)에 둡니다.

*`DecodeLegacyBlendFactor` converts D3D blend enums into shared state; the backend maps DESTCOLOR/INVDESTCOLOR to GL_DST_COLOR/GL_ONE_MINUS_DST_COLOR. Diagnostics record each surface's first failure independently of the repeated-failure budget and attribute first successes/failures to frames and decoded screen bounds. The asset-free `re2dj_opengl_blend_probe` separately verifies multipass composition through the actual RGB565 backend. See the [destination-color mask design](docs/design/20260905-198-destination-color-mask-blending.md).*

*The DirectDraw 2D path also gives `DDSCAPS_OFFSCREENPLAIN` surfaces RGB565 GDI backing and stable identity/revision. A common `CopyRgb565Rectangle` contract handles equal-sized rectangles and inclusive source color keys; facade `Blt`/`BltFast` update CPU backing and composite the same source rectangle into the OpenGL frame for primary/back destinations. Unobserved stretch, ROP, and destination-key combinations remain explicit failures.*

*Direct x86 `IN`/`OUT` has no Win32 import to replace, so a platform-neutral raw byte bus under `include/re2dj/input/` forms an execution-backend boundary. Debugger mode handles only confirmed 1st SE helper RVAs, opcodes, and ports. For real execution, `--run-detached` restores and verifies the guest first, detaches the debugger, and lets an injected vectored handler enforce the same narrow contract in-process without changing original instruction bytes.*

---

## 4. 게스트 경로 변환 / Guest path translation **[구현됨]**

게스트가 넘기는 경로는 Win32 문법이다. `re2dj::storage::GuestPath`가 이를 파싱해 다음을 구분한다.

* 드라이브 문자 (`C:\...`)
* 드라이브 상대 (`C:FOO`)
* 루트 상대 (`\FOO`)
* 순수 상대 (`FOO\BAR`)
* UNC (`\\server\share`) — 현재는 거부한다

*Guest paths use Win32 syntax. `re2dj::storage::GuestPath` parses them and distinguishes drive-absolute, drive-relative, root-relative, plain relative, and UNC forms. UNC is rejected for now.*

정규화 단계에서 `.`을 제거하고 `..`을 접으며, 구분자로 `/`와 `\`를 모두 받아들인다. 루트를 벗어나는 `..`은 실패로 처리해 HDD 디렉터리 바깥을 참조하지 못하게 한다.

*Normalization drops `.`, folds `..`, and accepts both `/` and `\` as separators. A `..` that would escape the root fails, so nothing outside the HDD directory is reachable.*

---

## 5. PE32 이미지 판독 / PE32 image reading **[구현됨]**

`re2dj::exe::PeImageInfo`는 원본 실행 파일에서 다음을 읽는다.

* DOS 헤더와 `e_lfanew`
* COFF 파일 헤더: machine, section 수, characteristics
* Optional header: magic(PE32 / PE32+), image base, entry point RVA, section/file alignment, subsystem, DLL characteristics
* Section 테이블: 이름, VA, 가상/원시 크기, 원시 오프셋, 특성
* Data directory: import, relocation, resource, TLS 위치

*`re2dj::exe::PeImageInfo` reads the DOS header, COFF file header, optional header, section table, and data directories from an original executable.*

이 판독기 자체는 **적재하지 않는다.** 파일을 읽어 구조만 보고한다. 실제 적재(섹션 매핑, 재배치, import 바인딩)는 구현된 런타임 계층의 `LoadPe32Image()`가 수행한다.

*This reader does **not** load anything; it reports structure only. The implemented runtime `LoadPe32Image()` performs section mapping, relocation, and import binding.*

HDD 스캔은 이 판독기를 사용해 각 실행 파일을 분류한다. `machine`이 x86(0x014C)이고 magic이 PE32(0x10B)이며 subsystem이 GUI인 항목이 게임 실행 파일 후보다.

*The HDD scan uses this reader to classify each executable. An entry with x86 machine (0x014C), PE32 magic (0x10B), and the GUI subsystem is a game-executable candidate.*

---

## 6. 타깃 프로파일 / Target profiles **[구현됨: 자료구조·실행 기본값]**

버전별 차이는 `re2dj::target::TargetProfile`로 분리한다.

| 필드 | 의미 |
| --- | --- |
| `id` | 명령행에서 고르는 짧은 식별자 |
| `display_name` | 사람이 읽는 이름 |
| `executable_relative_path` | HDD 루트 기준 실행 파일 경로. 지문이 맞을 때 채워진다 |
| `working_directory_relative_path` | 호스트 쪽 작업 디렉터리 |
| `guest_drive_letter` | 게스트가 자신이 실행된다고 믿는 드라이브. 근거가 없으면 `\0` |
| `guest_directory` | 같은 근거의 Win32 디렉터리. 근거가 없으면 빈 문자열 |
| `hle_profile_id` | 적용할 HLE 서비스 집합 |
| `run_defaults` | 프로파일 shortcut의 기본 HDD 경로와 Windows 실행 정책. 명령행 값이 덮어쓴다 |
| `detected` | 내장 표가 아니라 스캔에서 나온 것인지 |
| `bring_up_target` | 캐비닛이 실행한 것이 아니라 개발용인지 |
| `note` | 사람이 읽는 단서와 한계 |

*Version-specific differences live in `re2dj::target::TargetProfile`.*

프로파일의 `run_defaults`는 shortcut 기본 HDD 경로와 해당 원본 빌드에 대해 확인되었거나 명시적인 호환성 기준으로 지정된 Windows HLE·detached 실행 정책을 소유한다. 감지 전용 프로파일은 이 값을 비워 검증되지 않은 실행 정책을 상속하지 않는다. CLI의 `--hdd`, `--target`, `--fullscreen`/`--windowed`, 오디오 값과 I/O 설정은 프로파일 기본값을 덮어쓴다.

*`run_defaults` owns the shortcut HDD path and the Windows HLE/detached execution policy confirmed for that original build or explicitly designated as a compatibility baseline. Detection-only profiles keep it empty, so they cannot inherit an unverified execution policy. CLI `--hdd`, `--target`, `--fullscreen`/`--windowed`, audio values, and I/O settings override the profile defaults.*

`run_defaults.hdd_input_kind`는 추출 디렉터리와 MAME CHD를 구분하고, CHD profile은 `default_hdd_image_relative_path`에 각 버전의 이미지 경로를, `executable_relative_path`에 확인된 CHD 내부 실행파일 경로를 저장한다. Windows x86 CHD 실행은 선택한 실행파일 하나만 profile별 임시 경로에 staging하고 나머지 파일은 CHD-backed VFS에서 지연 읽기한다. 6th는 bootstrap `EZ2DJ.EXE`를 선택하고 `follow_child_process`로 `EZ2DJ6th.EXE` 생성 이벤트를 추적한 뒤 실제 게임 child에 HLE를 주입한다. 5th는 현재 FAT32 reader가 인식하지 못한다.

*`run_defaults.hdd_input_kind` distinguishes extracted directories from MAME CHDs. CHD profiles store the image shortcut in `default_hdd_image_relative_path` and the confirmed internal executable in `executable_relative_path`. Windows x86 stages only that executable under a profile-specific temporary root and leaves all other files to lazy CHD-backed VFS reads. The 6th profile selects the `EZ2DJ.EXE` bootstrap, follows its `EZ2DJ6th.EXE` child through `DEBUG_PROCESS`, and injects the HLE boundary into the actual game child. The current FAT32 reader still does not recognise the 5th image.*

`ez2dj1stse`도 CHD profile이다. 다만 다른 CHD 전환과 달리 CHD 내부 실행파일이 기존 추출 덤프와 같은 빌드가 아니다. CHD는 `.protect` 래퍼(entry RVA `0x01ad1240`, 6 sections, 암호화된 `.text`)이고 추출 덤프는 `.gtide` 래퍼(entry RVA `0x01ad23cf`, 8 sections, 평문 `.text`)다. 이 profile의 실행 기본값은 `.gtide` 기준선이 아니라 CHD 빌드에서 직접 관측한 경계를 따른다. 근거는 두 가지다. 첫째, PE header의 import directory가 `.protect` 안 RVA `0x01aebbd0`으로 옮겨져 원본 `.idata`를 가리키지 않으므로 launcher가 패치할 수 있는 import가 그 표에 있는 것뿐이다. `GetWindowsDirectoryA`, `DirectDrawCreate`, `GetPrivateProfileIntA`가 없어 해당 경계를 끄고, `GetCommandLineA`와 `DSOUND` ordinal `#1`은 있어 유지한다. 둘째, 이 빌드는 `\\.\LPTDI`가 아니라 `\\.\FEnteDev`를 열고 Hardlock IOCTL을 진행하므로 3rd·4th와 같은 device 정책을 쓴다. 자세한 관측은 [ez2dj1stse CHD 파일시스템 분석](docs/analysis/ez2dj1stse-chd-filesystem.md)에 있다.

*`ez2dj1stse` is a CHD profile too, but unlike the other CHD conversions its internal executable is not the same build as the extracted dump. The CHD carries a `.protect` wrapper (entry RVA `0x01ad1240`, six sections, encrypted `.text`) while the extracted dump carries a `.gtide` wrapper (entry RVA `0x01ad23cf`, eight sections, plaintext `.text`). This profile's execution defaults follow the boundary observed directly on the CHD build rather than the `.gtide` baseline, on two grounds. First, the PE header's import directory has moved into `.protect` at RVA `0x01aebbd0` and no longer points at the original `.idata`, so the only imports the launcher can patch are the ones in that table: `GetWindowsDirectoryA`, `DirectDrawCreate`, and `GetPrivateProfileIntA` are absent so those boundaries are disabled, while `GetCommandLineA` and `DSOUND` ordinal `#1` are present and kept. Second, this build opens `\\.\FEnteDev` rather than `\\.\LPTDI` and walks the Hardlock IOCTLs, so it uses the same device policy as 3rd and 4th. The observations are recorded in the [ez2dj1stse CHD filesystem analysis](docs/analysis/ez2dj1stse-chd-filesystem.md).*

IAT 슬롯 조회는 PE header의 import directory를 먼저 보고, 거기서 찾지 못하면 `.idata` section의 표를 추가로 조회한다. 패킹된 이미지는 import directory를 packer section으로 옮기지만 원본 `.idata`는 자기 RVA에 그대로 남는다. 1st SE의 CHD 빌드가 그 경우로, packed 표에는 `DirectDrawEnumerateA`만 있고 `DDRAW.dll!DirectDrawCreate`는 원본 `.idata`에만 있다. header directory만 권위 있는 표로 두어, 보조 표가 파싱되지 않으면 오류가 아니라 불일치로 처리한다. `.idata`의 RVA가 header directory와 같으면 건너뛰므로 패킹되지 않은 이미지의 조회 대상은 늘지 않는다.

*IAT slot lookup searches the PE header's import directory first and the `.idata` section's table only when that finds nothing. A packed image moves its import directory into the packer section while the original `.idata` survives at its own RVA — the case for 1st SE's CHD build, whose packed table holds only `DirectDrawEnumerateA` while `DDRAW.dll!DirectDrawCreate` exists only in the original `.idata`. The header directory stays the sole authoritative table, so a secondary table that does not parse is a non-match rather than an error, and an `.idata` whose RVA equals the header directory's is skipped so unpacked images search nothing extra.*

Windows x86의 공용 32-bit IAT writer는 먼저 `WriteProcessMemory`를 시도하고, 읽기 전용 PE section 때문에 실패하면 child가 suspended인 동안 해당 4-byte 범위만 임시 `PAGE_READWRITE`로 전환하여 다시 쓴 뒤 원래 보호 속성을 복원한다.

*The shared Windows x86 IAT writer first tries `WriteProcessMemory`. If a read-only PE section rejects the write, it temporarily changes only the four-byte range to `PAGE_READWRITE` while the child is suspended, retries the write, and restores the original protection.*

4th에서 확인한 raw-I/O helper RVA는 명령어 trap 위치이므로 다른 버전에 자동 상속하지 않는다. 6th는 공용 CHD/VFS/DirectX/device 경계만 호환성 기준으로 사용하고, 자체 helper 주소가 확인될 때까지 raw-I/O trap을 비활성화한다.

*Raw-I/O helper RVAs confirmed for 4th are instruction-trap locations and are not inherited automatically by another version. The 6th profile reuses only the shared CHD, VFS, DirectX, and device boundaries as a compatibility baseline, leaving raw-I/O traps disabled until its own helper addresses are confirmed.*

프로파일의 `legacy_io_ports`가 꺼진 실행 경로에서는 제품 CLI가 `--io-config`를
launcher에 전달하지 않는다. 따라서 6th처럼 bootstrap child-follow를 사용하는
프로파일에 1st/4th용 설정 파일을 함께 지정해도 launcher의 detached-only 검증에
걸리지 않으며, 해당 옵션은 무시되었다는 안내만 남긴다.

*When a profile disables `legacy_io_ports`, the product CLI does not forward
`--io-config` to the launcher. A shared command may therefore retain the 1st/4th
configuration option for a 6th bootstrap child-follow run without triggering the
launcher's detached-only validation; the CLI reports that the option was ignored.*

### 지문으로 덤프를 식별한다 / Dumps are identified by fingerprint

내장 프로파일은 기본적으로 **실행 파일 이름 + 그 옆에 반드시 있어야 하는 항목 목록**으로 덤프를 식별한다. 파일 크기나 해시는 리비전마다 달라져 정상 덤프를 거부하므로 쓰지 않는다. 다만 다른 프로파일과 이름이 충돌하는 실행 파일 전용 프로파일은 해당 실행 파일의 PE header 조건을 추가할 수 있다.

*A built-in profile normally identifies a dump by an **executable name plus the entries that must sit beside it**. File size and hashes are rejected as keys because they vary per revision and would reject a legitimate dump. An executable-only profile may add PE header constraints when its case-insensitive name collides with another profile.*

| 프로파일 | 실행 파일 | 필수 형제 항목 |
| --- | --- | --- |
| `ez2dj1stse` | `ez2dj/Ez2DJ.exe` inside FAT32 CHD | image shortcut; the CHD build is a `.protect` wrapper, not the `.gtide` build the extracted dump carries |
| `ez2dj1st` | `Ez2DJ.exe` | executable name + entry RVA `0x0199b240` + SizeOfImage `0x019b6000` |
| `ez2dj2nd` | `EZ2DJ.exe` | `EZ2DJ.ini`, `bg`, `sound`, `system` + entry RVA `0x00079550` + SizeOfImage `0x0047d000` |
| `ez2dj3rd` | `EZ2DJ.EXE` | `EZ2DJ.INI`, `FONTKR.DAT`, `BG`, `Sound`, `system` |
| `ez2dj4th` | `EZ2DJ/EZ2DJ.EXE` inside FAT32 CHD | `EZ2DJ.INI`, `FONTKR.DAT`, `FONTEN.DAT`, `BG`, `SOUND`, `SYSTEM` |
| `ez2dj5th` | `EZ2DJ/EZ2DJ.EXE` inside CHD shortcut | 4th-based compatibility profile; filesystem not yet recognised |
| `ez2dj6th` | `EZ2DJ/EZ2DJ6th.EXE` inside FAT32 CHD | `EZ2DJ.EXE` is a bootstrap; remaining contracts are under runtime verification |
| `ez2d2m` | `ez2dancer/EZ2Dancer.exe` inside FAT32 CHD | EZ2Dancer 2nd MOVE, not EZ2DJ: same `.protect` Hardlock envelope, but `DirectDrawCreateEx` graphics and the only word-wide I/O board |

기본적으로 형제 항목은 경로 해석이 대소문자를 무시하는 환경에서 프로파일을 구별하기 위해 사용한다. `ez2dj1st`는 사용자가 지정한 대표 실행 파일 자체의 이름과 PE header만 식별 근거로 사용하고, `ez2dj2nd`는 sibling과 PE header를 함께 사용한다. CHD profile은 image shortcut으로 선택되므로 extracted-directory fingerprint 매칭 대상이 아니다. 이 매칭 정책은 프로파일 선택을 위한 것이며, 버전별 보호·legacy I/O·게스트 부트 계약을 자동으로 확정하지 않는다.

*Normally siblings distinguish profiles when path resolution is case-insensitive. `ez2dj1st` uses only the user-designated representative executable's name and PE header, while `ez2dj2nd` uses siblings and PE headers together. CHD profiles are selected through image shortcuts rather than extracted-directory fingerprint matching. This policy selects a profile; it does not automatically confirm version-specific protection, legacy-I/O, or guest-boot contracts.*

실행 파일을 스캔 결과에서 찾으므로 사용자가 상위 디렉터리를 지정해도 걸린다. 형제 항목은 그 실행 파일의 디렉터리를 기준으로 확인한다.

*Matching searches the scan, so pointing at a parent directory still works; siblings are checked relative to the matched executable's own directory.*

### 순서와 감지 / Ordering and detection

`BuildTargetProfiles()`는 내장 프로파일 중 지문이 맞는 것을 먼저 놓고, 내장이 가져가지 않은 실행 파일에 대해서만 `DetectTargetProfiles()`를 돌린다. 목록의 첫 항목이 기본 타깃이다.

내장 프로파일이 없는 버전의 덤프도 감지만으로 계속 동작한다. 내장 항목은 **실제로 확인한 덤프에 대해서만** 추가한다.

*`BuildTargetProfiles()` puts fingerprint matches first and runs detection only over the executables no built-in claimed; the first entry is the default target. A dump of a version with no built-in profile still works through detection alone. Built-in entries are added **only for dumps that were actually inspected**.*

---

## 7. 런타임 계층 / Runtime layer **[부분 구현됨]**

```mermaid
flowchart TD
    subgraph runtime["re2dj::runtime"]
        GA["GuestAddress<br/>32-bit value type"]
        AS["AddressSpace<br/>page-granular, host-backed"]
        CTX["GuestContext<br/>GPR / EFLAGS / x87 / SSE"]
        BE["ExecutionBackend<br/>interface"]
    end
    BE -.candidate.-> ORIG["Windows original-process loader<br/>suspended-image probe verified"]
    GATE -.-> LNX["Linux in-process runner<br/>x86 native, x64 compatibility mode"]
    AS --> GATE["Import gate region"]
    GATE --> DISP["HLE dispatcher"]
```

* `GuestAddress`는 host pointer로 변환되지 않는 32비트 값 타입이다.
* `AddressSpace`는 게스트의 평탄한 4 GiB 주소 공간 중 실제로 커밋된 페이지만 호스트 메모리에 둔다. 게스트 주소를 호스트 포인터로 노출하지 않고 `Read8/16/32`, `Write8/16/32` 접근자를 통해서만 다룬다.
* `LoadPe32Image()`는 헤더와 섹션을 매핑하고 zero-fill한 뒤, `IMAGE_REL_BASED_HIGHLOW` 재배치를 적용하고 이름/ordinal import를 합성 gate에 바인딩한다. 실패 시 호출자가 제공한 주소 공간과 gate 표는 바뀌지 않는다.
* `ImportGateTable`은 기본적으로 `0xF0000000`부터 16바이트 간격의 주소를 배정한다. 이 범위에는 실제 명령어가 없으며 Stage 3 backend가 HLE dispatcher로 전달한다.
* `ExecutionBackend` event/reply 인터페이스는 이미지 준비, 실행 시작, event 대기, import 완료 응답, 중단 요청을 분리한다. event에는 backend-local thread ID, guest EIP/ESP와 gate 주소만 담고 host pointer를 넣지 않는다. import 응답은 EAX/EDX, stack 정리 byte 수, 계속/중단 action을 전달한다.
* (작업 379에서 제거됨) Windows `NativeHelperBackend`는 x86 helper process와 anonymous pipe protocol v3를 `ExecutionBackend` 뒤에 캡슐화했다. PImpl 공개 header에는 Windows type이 없고, adapter가 packet 순서, 상태 검증과 child 종료를 소유한다. `native_pe_image`는 requested base mapping, `HIGHLOW` relocation, section protection과 process-attach TLS callback을 담당한다. helper는 PE32의 이름/ordinal import를 순회해 `ImportGateTable`의 synthetic gate마다 실행 가능한 x86 thunk를 만들고 실제 thunk 주소를 IAT에 쓴다. load 뒤 module/name/ordinal/gate metadata를 adapter에 보내 `LoadedPeImage.imports`를 채운다. gate가 멈춘 동안 adapter는 guest memory를 읽고 쓴 뒤 EDX:EAX와 동적 stack 정리 크기를 응답한다. protocol은 event 하나를 직렬 처리하며 TLS storage/index와 병렬 guest thread는 후속 확장이다.
* Win32 original-process engine은 `re2dj_windows_original_process_backend` static library에 있으며 제품 `re2dj --run`과 진단 `re2dj_windows_x86_launcher_probe`가 공유한다. 제품 facade는 선택된 built-in profile의 `run_defaults`를 기반으로 command line·Windows directory·VFS/overlay·Direct3D 3·DirectSound·legacy I/O·LPTDI target-state와 detached runtime policy를 구성한다. 현재 실제 실행 정책이 확인된 프로파일은 1st SE와 3rd이며, 미검증 target은 process 생성 전에 거절한다. 3rd는 실제 import에 맞춰 VFS·DirectSound만 기본 활성화하고, command-line/Windows-directory/DirectDraw·display 및 legacy I/O hook은 추정하지 않는다. 진단 executable은 기존 세부 option parser와 trace 기능을 그대로 노출한다.

*The Win32 original-process engine lives in the `re2dj_windows_original_process_backend` static library shared by product `re2dj --run` and the diagnostic `re2dj_windows_x86_launcher_probe`. The product facade builds command-line, Windows-directory, VFS/overlay, Direct3D 3, DirectSound, legacy-I/O, LPTDI target-state, and detached-runtime policy from the selected built-in profile's `run_defaults`. The currently verified execution policies are 1st SE and 3rd; unverified targets are rejected before process creation. The 3rd profile enables only VFS and DirectSound by default to match its observed imports, and does not guess command-line/Windows-directory, DirectDraw/display, or legacy-I/O hooks. The diagnostic executable continues to expose its detailed option parser and trace features.*

프로파일의 `run_defaults.lptdi`는 1st SE와 3rd의 장치 경계를 분리한다. 두 프로파일은 공용 LPTDI challenge-response 변환을 재사용하지만, synthetic device path prefix와 post-XOR target state는 각각 보유한다. 1st SE는 `\\.\\LPTDI`와 확인된 `0900000000000000`을 사용하고, 3rd는 `\\.\\Hardlock`과 별도의 `0000000000000000` target-state probe 값을 사용한다. 3rd 값은 1st SE에서 자동 복사되지 않으며 실제 Hardlock 동글 응답으로 확정된 값도 아니다. launcher는 선택된 프로파일의 path와 state를 injected runtime에 전달하고, Hardlock처럼 정적 IAT에 없는 API는 동적 `GetProcAddress` wrapper로 연결한다. `--hle-io-ports`와 `--device-mock-lptdi*`는 프로파일 정책이 허용할 때만 준비하므로, 3rd의 `UseIOCard=1`을 근거 없이 1st raw I/O HLE로 연결하지 않는다.

*The profile's `run_defaults.lptdi` keeps the 1st SE and 3rd device boundaries separate. Both profiles reuse the shared LPTDI challenge-response transform, but each carries its own synthetic device path prefix and post-XOR target state. 1st SE uses `\\.\\LPTDI` with the confirmed `0900000000000000`; 3rd uses `\\.\\Hardlock` with a separate `0000000000000000` target-state probe value. The 3rd value is not copied from 1st SE and is not confirmed as a physical Hardlock response. The launcher passes the selected path and state to the injected runtime, and dynamically resolved APIs such as Hardlock's device calls use the runtime wrappers. `--hle-io-ports` and `--device-mock-lptdi*` are prepared only when the profile policy allows them, so 3rd's `UseIOCard=1` is not guessed to mean the 1st raw-I/O HLE contract.*

3rd 프로파일의 최신 실제 실행(`20260831-000859-972.jsonl`)은 선택된 `\\.\\FEnteDev` mock 정책과 zero target-state probe를 준비한 뒤 원본 `EZ2DJ.EXE`를 runtime 주입·detached 상태로 유지했다. VFS trace에는 동적 `GetProcAddress` resolver 슬롯 2개와 `\\.\\NTICE`, `\\.\\FEnteDev` 장치 open이 기록되었다. 이후 256바이트 Hardlock descriptor와 264바이트 Function `0x0e` 요청까지는 별도 계측 실행에서 확인했지만, 유효한 암호 응답은 아직 미확정이다.

*The latest 3rd profile run (`20260831-000859-972.jsonl`) prepared the selected `\\.\\FEnteDev` mock policy and zero target-state probe, injected the runtime, detached the original `EZ2DJ.EXE`, and left the process responsive. The VFS trace recorded two dynamic `GetProcAddress` resolver slots and device opens for `\\.\\NTICE` and `\\.\\FEnteDev`. The later 256-byte Hardlock descriptor and 264-byte Function `0x0e` request are confirmed by a separate instrumentation run, but the valid encrypted response remains unresolved.*

플랫폼 중립 `hardlock_api_descriptor` parser는 packed 32-bit `HL_API`의 52바이트 고정 prefix를 raw byte view에서 복사하며 guest `Data` pointer를 역참조하지 않는다. Windows injected runtime은 synthetic `0x9c40244c`/`0x9c402458`에 한해서만 기존 bounded device trace budget 안에 scalar와 `ID_Ref[8]`/`ID_Verify[8]`를 기록한 뒤 기존 response policy를 그대로 실행한다. reserved bytes, block payload와 전체 descriptor는 기록하지 않는다. 두 번의 독립 원본 실행으로 nonzero 3rd ID 필드가 안정적으로 일치함을 확인했으며, 이 값은 식별된 seed나 유효 Function `0x0e` 응답이 아니라 solver 입력으로 유지한다.

*The platform-neutral `hardlock_api_descriptor` parser copies the 52-byte fixed prefix of the packed 32-bit `HL_API` from a raw byte view without dereferencing the guest `Data` pointer. For synthetic `0x9c40244c` and `0x9c402458` only, the Windows injected runtime records scalars plus `ID_Ref[8]` and `ID_Verify[8]` within the existing bounded device-trace budget, then runs the existing response policy unchanged. It records no reserved bytes, block payload, or complete descriptor. Two independent original runs now confirm stable nonzero 3rd ID fields; they remain solver inputs rather than identified seeds or valid Function `0x0e` responses.*

Task 205는 descriptor 분석을 프로파일 제작 때 반복할 수 있도록 launcher 전용 `--hardlock-descriptor-dump <path>`를 추가했습니다. 옵션은 injected runtime의 export에 Git-ignored 로컬 출력 경로와 선택 프로파일 ID를 전달하고, 첫 번째 유효한 256바이트 descriptor에서 `module_address`, `id_ref`, `id_verify`를 한 번 기록합니다. 일반 VFS trace에는 헤더 scalar와 64-bit FNV-1a 해시만 남으며, raw ID는 사용자가 명시한 로컬 파일 외에는 기록하지 않습니다.

*Task 205 adds the launcher-only `--hardlock-descriptor-dump <path>` option so descriptor analysis can be repeated while creating profiles. The option passes a Git-ignored local output path and the selected profile ID to injected-runtime exports, then writes `module_address`, `id_ref`, and `id_verify` once from the first valid 256-byte descriptor. Normal VFS traces retain only header scalars and 64-bit FNV-1a digests; raw IDs are written nowhere except the explicitly selected local file.*

Task 107의 오프라인 SMT 분석은 두 ID 사이의 유일한 5-byte 중간 control 값을 확인했지만, 세 16-bit seed 제약에는 최소 11개의 서로 다른 검증 해가 존재했습니다. 따라서 runtime profile에는 seed 후보를 넣지 않으며, 두 번째 독립 challenge/response 또는 원본 Function `0x0e` 판별 oracle이 확보될 때까지 synthetic response policy를 실제 Hardlock emulation으로 승격하지 않습니다.

*Task 107's offline SMT analysis confirmed a unique five-byte intermediate control value between the two IDs, but the three 16-bit seed constraints admit at least eleven distinct verified models. No seed candidate is therefore added to the runtime profile, and the synthetic response policy is not promoted to physical Hardlock emulation until a second independent challenge/response or an original Function `0x0e` distinguishing oracle is available.*

ez2dj4th는 `\\.\FEnteDev` 장치 경계와 저장소 밖 프로파일 section을 사용합니다. 설정값은 target table, 명령행, 진단 JSONL 및 descriptor trace에 기록하지 않습니다. 외부 설정 mode의 descriptor 진단은 module address와 ID field를 redaction합니다. 현재 구현은 안전한 설정 수명주기와 runtime 주입까지이며, 미문서화 E-Y-E Function `0x0e` 변환은 허용 가능한 근거가 확보될 때까지 미구현입니다.

*ez2dj4th uses the `\\.\FEnteDev` device boundary and an out-of-repository profile section. Configuration values are never written to the target table, command line, diagnostic JSONL, or descriptor trace. Descriptor diagnostics redact the module address and ID fields while external configuration is active. The current implementation completes the secure configuration lifecycle and runtime injection; the undocumented E-Y-E Function `0x0e` transform remains unimplemented until a policy-compatible basis is available.*

3rd 보호 초기화는 동적으로 resolve한 `WTSQuerySessionInformationA`로 현재 session의 `WTSSessionId`(class 4)를 검사합니다. 이전 문서는 이를 `WTSConnectState`로 적었으나 SDK에서 `WTSConnectState`는 8이다(작업 360 정정). Windows injected runtime은 기본적으로 원래 결과를 그대로 전달하고 bounded scalar 진단만 기록합니다. 명시적 `--device-mock-wts-console-session` 분석 옵션에서만 `WTS_CURRENT_SESSION`, class 4, 성공한 정확히 4바이트 결과를 세션 번호 `0`(Windows XP의 콘솔 세션)으로 바꾸며 다른 query와 실패 결과는 보존합니다. 이 HLE로 `0x9c402468` 뒤 `0x9c402450`까지 진행함을 확인했지만, 6바이트 `0x450` 응답과 이후 `0x44c/0x458`은 아직 별도 미확정 Hardlock 경계입니다.

*The 3rd protection initialization checks the current session's `WTSSessionId` (class 4; earlier text called it `WTSConnectState`, which the SDK numbers 8, corrected in Task 360) through dynamically resolved `WTSQuerySessionInformationA`. The Windows injected runtime forwards the original result by default and records only bounded scalar diagnostics. Only the explicit `--device-mock-wts-console-session` analysis option changes a successful, exactly four-byte `WTS_CURRENT_SESSION` class-4 result to session ID `0` (the Windows XP console session); all other queries and failures are preserved. This HLE is confirmed to advance execution from `0x9c402468` to `0x9c402450`, while the six-byte `0x450` response and later `0x44c/0x458` calls remain separate unresolved Hardlock boundaries.*

`hardlock_450_response` 모듈은 정확히 12자리 hex만 6바이트로 parsing합니다. launcher의 기본 비활성 `--device-mock-hardlock-450-response` 분석 옵션은 이 값을 runtime export에 전달하고, injected runtime은 synthetic device의 exact-size `0x9c402450`에만 복사해 6바이트 성공을 반환합니다. 기본 실행과 다른 control code는 변경하지 않습니다. 과거 synthetic oracle `0100fafa0010` replay는 `0x9c40244c` 도달을 재현했지만 실제 driver payload나 제품 기본값으로 간주하지 않습니다.

*The `hardlock_450_response` module parses exactly twelve hex digits into six bytes. The launcher's default-off `--device-mock-hardlock-450-response` analysis option passes those bytes through runtime exports, and the injected runtime copies them only for an exact-size synthetic-device `0x9c402450` call and reports six-byte success. Default execution and other control codes are unchanged. Replaying historical synthetic oracle `0100fafa0010` reproduces reachability of `0x9c40244c`, but the bytes are neither a physical-driver payload nor a product default.*

`hardlock_api_descriptor`는 정확히 256바이트 descriptor의 마지막 word도 bounded scalar로 읽습니다. runtime marker는 전체 reserved 영역 대신 이 `tail_word`만 기존 고정 field 뒤에 기록합니다. launcher의 기본 비활성 `--device-mock-hardlock-44c-tail` 분석 옵션은 user-supplied 16-bit 값을 synthetic device의 exact-size Function 0 `0x9c40244c` output offset `0xfe`에만 쓰고 나머지 254바이트를 보존합니다. synthetic `tail=0x0001`로 원본의 handle-retention 분기를 선택하자 Function 6 `0x44c`와 Function `0x0e` `0x458`에 도달했지만, 이 값은 실제 driver 응답이나 제품 기본값이 아니며 Function `0x0e`의 암호 출력은 여전히 별도 경계입니다.

*`hardlock_api_descriptor` also reads the final word of an exact 256-byte descriptor as a bounded scalar. The runtime marker records only this `tail_word` after the existing fixed fields, not the complete reserved region. The launcher's default-off `--device-mock-hardlock-44c-tail` analysis option writes a user-supplied 16-bit value only at output offset `0xfe` for an exact-size Function-0 `0x9c40244c` on the synthetic device, preserving the other 254 bytes. Synthetic `tail=0x0001` selects the original's handle-retention branch and reaches Function-6 `0x44c` and Function-`0x0e` `0x458`; the value is neither a physical-driver response nor a product default, and the Function-`0x0e` cryptographic output remains a separate boundary.*

`--hardlock-stub`을 켜면 위 두 실험 옵션이 각각의 분기가 아니라 `HardlockStubDevice`의 입력이 됩니다. `0x450` replay는 handshake 응답으로, `0x44c` tail은 Function 0 descriptor tail로 그대로 전달되므로 기존 분기 실험 결과는 유지되고, 나머지 IOCTL은 스텁의 단일 계약을 따릅니다. 스텁을 끄면 injected runtime은 기존 경로를 그대로 사용합니다.

*With `--hardlock-stub` set, those two experiment options become inputs to `HardlockStubDevice` instead of separate branches: the `0x450` replay is forwarded as the handshake response and the `0x44c` tail as the Function-0 descriptor tail, so earlier branch-experiment results still hold while the remaining IOCTLs follow the stub's single contract. With the stub off, the injected runtime keeps its existing paths unchanged.*
* `re2dj_windows_x86_launcher_probe`는 기본 Win32 host에서 원본 EXE를 `DEBUG_ONLY_THIS_PROCESS`로 만들고 entry 직전에 멈춘다. DR0 hardware stop이 전달되지 않는 입력에서는 child memory의 entry 첫 바이트를 일시적으로 `INT3`로 바꾸고 즉시 원복하는 diagnostic fallback으로 정지한다. 이 경계에서 Windows loader가 main image를 `0x00400000`에 배치하고 packed import table을 해석한 상태를 실제 HDD·CHD 입력으로 확인했다. 정지 상태에서 primary thread를 suspend하고 minimal x86 runtime DLL을 remote `LoadLibraryW` thread로 적재해 module base도 확인했다. `--probe-handoff`는 PE table에서 `GetCommandLineA` IAT slot을 찾아 runtime log-and-forward thunk로 교체하고, `--hle-command-line`은 runtime의 process-lifetime buffer에 original basename을 기록해 실제 HLE thunk가 반환하도록 한다. 두 경로 모두 entry 재개 후 debugger output event로 확인했다. x64 `re2dj_windows_original_process_probe`는 보류된 비교 근거로 남긴다.
* `re2dj_windows_x86_launcher_probe`는 target과 원본 EXE를 해석한 뒤 실행별 JSONL 진단 로그를 `logs/windows_x86_launcher_probe/<target-id>/`에 만든다. debug event와 예외 관찰은 `--trace` 여부와 관계없이 즉시 flush되며, `--trace`는 stderr 실시간 표시만 제어한다. 최종 성공 또는 실패 JSON은 생성된 로그 경로를 포함한다. 이 생성 디렉터리는 HDD 및 guest overlay 밖이고 Git ignore 대상이다.
* `re2dj_windows_x86_launcher_probe`의 `--instruction-trace <max-steps>`는 software entry stop에서 EIP와 TF를 설정하고 primary-thread debugger event 뒤 TF를 다시 설정한다. 최대 32개 instruction address와 바이트를 ring buffer에 유지한 뒤 illegal instruction 또는 step limit에서만 JSONL에 기록한다. 이는 protected post-entry control flow의 관찰 도구이며, branch operand나 보호 실패 원인을 자체적으로 해석하지 않는다.
* `re2dj_windows_x86_launcher_probe --scan-fault-references`는 first-chance illegal-instruction event에서 child를 계속하기 전에 committed private/image memory를 bounded scan한다. fault address와 page base의 32-bit reference, 해당 region 속성 및 match summary를 JSONL에 기록한다. 이 결과는 target storage 후보일 뿐 indirect branch caller의 증명은 아니다.
* `re2dj_windows_x86_launcher_probe --api-trace`는 child memory에서 `kernel32.dll`/`kernelbase.dll` export를 해석해 watched API에 software breakpoint를 설치하고, hit마다 caller·args·ANSI 문자열을 JSONL에 남긴 뒤 원래 byte 복원과 TF 1 step으로 삼키고 재무장한다. `DeviceIoControl`은 guest-origin 호출에 한해 8개 인자와 bounded input/output snapshot을 기록하고 thread별 one-shot return breakpoint에서 EAX, bytes-returned, buffer 변화 여부를 기록한다. first-chance illegal-instruction에서는 full register와 segment, 64 dword stack과 main image section 분류, fault page dump, allocation region walk, entry VA 참조 탐색을 기록한다. first-chance access violation에서는 접근 종류·주소와 full register, main-image register pointer 주변 dword, stack return 주소 주변 code bytes를 기록한다. entry 이후 동적으로 적재된 모듈의 unload event에서는 종반 구간을 single-step 수집하며 매 샘플에 GP register trail과 nearest-export 심볼 주석을 붙인다. 수집 중 표준 syscall 스탠자(`mov edx,&thunk; call edx`) 꼬리마다 복귀 주소에 1회용 software breakpoint를 심어(예산 상한 내 재무장) WOW64 게이트 너머의 32비트 복귀를 포착하고 TF를 재무장하며, 복귀 시점 stack 64 word에 심볼 주석을 남긴다. 이 도구는 관찰이며 보호 실패 원인을 자체적으로 판정하지 않는다.
* (작업 379에서 제거됨) OS type을 포함하지 않는 desktop helper protocol v3 header는 `src/platform/native_helper_protocol.h`에 공유했다. Linux x86·x64 `NativeHelperBackend`와 i386 helper는 requested-base PE32 mapping, `HIGHLOW` relocation, TLS process-attach callback, named/ordinal import thunk와 event/memory/completion packet을 합성 image로 검증했고 두 제품 host에 연결했다. `native_process_bootstrap`은 guard page가 있는 1 MiB guest stack, 최소 x86 TEB/PEB와 FS descriptor를 만들며 TLS callback과 entry를 이 환경에서 실행한다. alternate signal stack의 handler는 guest `SIGSEGV`·`SIGBUS`·`SIGILL`·`SIGFPE`·`SIGTRAP` EIP/ESP를 저장한 뒤 control stack에서 `kFault` event를 보낸다. pending import에서는 현재 PE image와 stack을 합친 허용 범위 안에서 최대 64KiB memory transfer를 사용한다. 공용 `ImportDispatcher`는 binding metadata를 찾아 `ExecutionBackend`로 `ESP + 4` dword를 읽고 handler 결과 및 `__stdcall`/`__cdecl` cleanup을 completion으로 보낸다. `kStop`은 helper continuation packet이 아니라 host가 helper를 종료하는 terminal controller action이며 guest `ExitProcess` 의미는 후속 process service에서 별도로 구현한다. 실제 Win32 binding은 아직 등록하지 않는다.
* Linux x86·x64 제품 host는 SDL graphics와 shared service, 그리고 같은 프로세스 안의 PE32 mapping, guest ABI process/thread state와 native x86 실행을 모두 소유한다(작업 379 전에는 별도 i386 helper가 뒤의 셋을 소유했다). guest address는 `ExecutionBackend` memory API로만 전달한다. USER32와 DirectX guest object는 32비트 token/vtable로 표현하고 host-side state에 연결한다. 정식 `ez2dj.exe`의 entry·import·callback·thread·DirectX 경계를 먼저 완성한 뒤 self-modifying/LPTDI/raw-I/O 경계를 추가한다. 상세 단계는 [Linux 원본 실행 경로 설계](docs/design/20260827-077-linux-original-execution.md)와 [x86 product build 설계](docs/design/20260918-309-linux-x86-product-build.md)에 둔다.
* 브라우저용 x86 실행 엔진과 직접 인터프리터는 현재 지원 범위에서 제외한다. 데스크톱 native helper 경계를 우선 확장하며, 브라우저 실행 계층은 별도 제품 목표로 유지하지 않는다.

*`GuestAddress` is a 32-bit value type that cannot become a host pointer. `AddressSpace` keeps only committed pages of the flat 4 GiB guest space in host memory and exposes accessors rather than pointers. `LoadPe32Image()` maps and zero-fills headers and sections, applies `IMAGE_REL_BASED_HIGHLOW` relocations, and binds named or ordinal imports to synthetic gates transactionally. `ImportGateTable` assigns addresses at 16-byte intervals from `0xF0000000`. The implemented `ExecutionBackend` interface separates image preparation, start, event waiting, guest-memory reads/writes, import completion, and stop requests using backend-local thread IDs and guest values only. Windows `NativeHelperBackend` encapsulates the x86 helper process and anonymous-pipe protocol v3 behind that interface; `native_pe_image` owns requested-base mapping, relocations, protection, and process-attach TLS callbacks, while import thunks restore EDX:EAX and dynamic stack cleanup. The primary `re2dj_windows_x86_launcher_probe` creates the original as a loader-owned child main image, confirms a pre-entry fully resolved IAT stop through a temporary child-memory `INT3` fallback, and loads a minimal same-bitness runtime DLL while the primary thread remains suspended; x64 observation remains deferred evidence. The OS-independent protocol-v3 header is shared under `src/platform/`. Linux x86 and x64 product hosts and the production i386 helper validate requested-base PE32 mapping, relocation, process-attach TLS, named and ordinal native import thunks, and event/memory/completion IPC and connect to the product CLI. A Linux pending-import `kStop` is terminal host control that closes transport and terminates the helper without a continuation packet; it is not guest `ExitProcess`. Linux guest TLS callbacks and entry run with a guarded 1 MiB stack, minimal x86 TEB/PEB, and FS selector. Fault handlers capture guest EIP/ESP on an alternate signal stack and send a structured event from the control stack. Each host owns shared Win32 HLE, HDD/VFS/overlay, and SDL services while keeping guest addresses in the backend boundary. Guest USER32 and DirectX objects use 32-bit tokens and vtables backed by host state. The unprotected build brings up imports, callbacks, threads, and DirectX before the protected executable adds self-modification, LPTDI, and raw I/O. Protocol v3 still serializes one event; TLS raw storage/index and parallel guest threads remain later extensions. See the [Linux original-executable design](docs/design/20260827-077-linux-original-execution.md), [Linux x86 product-host design](docs/design/20260918-309-linux-x86-product-build.md), and [guest process bootstrap design](docs/design/20260827-078-linux-guest-process-bootstrap.md). Browser x86 execution engines and custom interpreters remain outside the current desktop-only support scope.*

*After resolving a target and original executable, `re2dj_windows_x86_launcher_probe` creates a per-run JSONL diagnostic log under `logs/windows_x86_launcher_probe/<target-id>/`. Debug events and exception observations flush to that file whether or not `--trace` is set; `--trace` controls only live stderr output. Final success and error JSON identify the log path. The generated directory is outside the HDD and guest overlay and is Git-ignored.*

Win32 `--audio-volume-trace`는 injected runtime의 별도 bounded writer를 사용한다. DirectSound COM facade는 buffer 형식, `SetVolume`, 최초 재생과 재생 중 `Unlock`의 PCM 통계만 기록하고, WINMM import thunk는 호스트 mixer API로 pass-through하면서 control 구조와 scalar 값을 기록한다. 원본 샘플 바이트와 HDD 자산은 로그에 포함하지 않는다.

DirectSound secondary voice는 descriptor에 따라 정적 `MIX_Audio` snapshot과 streaming `SDL_AudioStream`으로 분리된다. `DSBCAPS_LOCHARDWARE` 또는 `DSBCAPS_GETCURRENTPOSITION2`(및 360,448바이트 크기)를 가진 streaming buffer(1st SE, 3rd, 4th)는 동기화된 shadow ring을 소유한다. Play와 Unlock은 guest ring을 shadow로 복사하고, SDL stream get callback은 mixer가 요청한 양만 현재 원형 cursor에서 공급한다. `SDL_LockAudioStream`이 shadow 갱신과 mixer read를 직렬화하므로 guest write와 audio thread가 같은 storage를 동시에 사용하지 않으며, 전체 ring 선큐로 인한 오래된 PCM 재생도 피한다. voice별 cursor, control, Stop/restart와 duplicate의 독립 재생 상태는 기존 neutral buffer 계약을 유지한다.

SDL mixer backend singleton은 injected DLL의 process-lifetime allocation이다. Windows `ExitProcess`는 다른 thread를 제거한 뒤 DLL detach를 수행하므로 atexit 소멸자에서 `SDL_QuitAudio`를 호출하면 WASAPI management thread 응답을 영구 대기한다. backend를 C++ atexit에 등록하지 않고 process가 mixer·stream·device·heap 자원을 회수하게 하며, 원본 `ExitProcess` import와 exit code는 변경하지 않는다. WASAPI exit child와 실제 제품 실행에서 기존 종료 교착 제거를 확인했다. 상세 근거는 [작업 094 설계](docs/design/20260829-094-audio-process-lifetime.md)에 둔다.

Win32 제품의 `src/platform/windows/ini_profile_hle.*`는 원본 main image의 `GetPrivateProfileIntA` import thunk 경계에서 `GAMEASSIGNMENTS/DemoVolume`만 외부 `--demo-volume 0..3` 값으로 재정의한다. 다른 section/key는 호스트 API로 그대로 전달하며 원본 EXE와 HDD INI는 수정하지 않는다. 확인된 원본 profile table은 `[-10000, -2222, -1111, 0]`이고 제품 기본은 profile 3, SDL master gain 기본은 0 dB다. 상세 계약은 [DirectSound 데모 음량 설정 HLE 설계](docs/design/20260829-086-directsound-volume-transition.md)에 둔다.

*Win32 `--audio-volume-trace` uses a separate bounded writer in the injected runtime. The DirectSound COM facade records only buffer formats, `SetVolume`, and PCM statistics at first playback and streaming `Unlock`; WINMM import thunks pass through to host mixer APIs while recording control structures and scalar values. Original sample bytes and HDD assets are never included in the log.*

*DirectSound secondary voices select either a static `MIX_Audio` snapshot or a streaming `SDL_AudioStream` from the observed descriptor. Streaming buffers with `DSBCAPS_LOCHARDWARE` or `DSBCAPS_GETCURRENTPOSITION2` (and 360,448-byte capacity, as seen across 1st SE, 3rd, and 4th) own a synchronized shadow ring. Play and Unlock copy the guest ring into the shadow, while an SDL stream get callback supplies only the amount requested by the mixer from the current circular cursor. `SDL_LockAudioStream` serializes shadow updates and mixer reads, so the audio thread never races guest storage and does not prequeue a complete revolution of stale PCM. Per-voice cursors, controls, Stop/restart, and independent duplicate playback state retain the neutral-buffer contract.*

*The SDL mixer-backend singleton is a process-lifetime allocation inside the injected DLL. Windows `ExitProcess` removes other threads before DLL detach, so calling `SDL_QuitAudio` from an atexit destructor can wait forever for the WASAPI management thread. The backend is not registered with C++ atexit; process teardown reclaims mixer, stream, device, and heap resources while the original `ExitProcess` import and exit code remain unchanged. A WASAPI exit child and a live product run confirm removal of the prior shutdown deadlock. See the [Task 094 design](docs/design/20260829-094-audio-process-lifetime.md).*

*The Win32 product's `src/platform/windows/ini_profile_hle.*` overrides only `GAMEASSIGNMENTS/DemoVolume` at the original main image's `GetPrivateProfileIntA` import-thunk boundary with external `--demo-volume 0..3`. Other section/key requests pass through to the host API, and neither the original EXE nor HDD INI is modified. The confirmed original profile table is `[-10000, -2222, -1111, 0]`; product defaults are profile 3 and 0 dB SDL master gain. See the [DirectSound demo-volume HLE design](docs/design/20260829-086-directsound-volume-transition.md).*

*`re2dj_windows_x86_launcher_probe --instruction-trace <max-steps>` sets EIP and TF at the software-entry stop and rearms TF after primary-thread debugger events. It retains up to 32 instruction addresses and bytes in a ring buffer, writing them to JSONL only on an illegal instruction or step limit. This observes protected post-entry control flow; it does not independently decode branch operands or determine a protection-failure cause.*

*`re2dj_windows_x86_launcher_probe --scan-fault-references` bounded-scans committed private/image memory before continuing a first-chance illegal-instruction event. It records 32-bit references to the fault address and page base, their region properties, and a match summary in JSONL. The result is a target-storage candidate only, not proof of an indirect-branch caller.*

*`re2dj_windows_x86_launcher_probe --api-trace` resolves `kernel32.dll`/`kernelbase.dll` exports from child memory, arms software breakpoints on watched APIs, records caller, arguments, and ANSI strings per hit to JSONL, then swallows and rearms each hit by restoring the original byte and trap-flagging one instruction. For guest-origin DeviceIoControl calls it captures all eight arguments and bounded input/output snapshots, then uses a per-thread one-shot return breakpoint to record EAX, bytes-returned, and buffer changes. On a first-chance illegal instruction it records full registers with segments, a 64-dword stack with main-image section classification, the fault page bytes, an allocation region walk, and an entry-VA reference scan. On a first-chance access violation it records access kind/address, full registers, bounded dword windows around main-image register pointers, and code bytes around stack return addresses. On an unload event of a module loaded after entry it single-steps the unload tail, attaching a GP-register trail and nearest-export symbol annotation to every sample. Each standard syscall stanza tail (`mov edx,&thunk; call edx`) plants a one-shot software breakpoint on its return address — rearmable within a fire budget — catching the 32-bit resume past WOW64 gates and re-arming tracing, with a symbol-annotated 64-word stack dump at each resume. The tool observes only and does not itself adjudicate the protection-failure cause.*

---

## 2026-09-18 Linux native-helper capability handshake / Linux native-helper 기능 협상

Linux product host는 helper fork 직후 `Hello(required_features, optional_features)`를 보내고 `HelloResult(supported_features)`가 모든 required bit를 포함할 때만 image bytes를 보냅니다. protocol v5의 초기 required feature는 import metadata, bounded memory transfer, guest-memory lifecycle입니다. 서로 다른 protocol version은 기존 header 검사에서 거부되고, 같은 version에서 필수 bit가 없으면 `LoadImage` 전에 실패합니다. feature discovery는 startup 계약일 뿐 callback/thread/dynamic import의 runtime fallback이나 구현을 뜻하지 않습니다. 자세한 계약은 [Linux native-helper capability handshake design](docs/design/20260918-317-linux-helper-capability-handshake.md)에 있습니다.

*Immediately after forking the helper, a Linux product host sends `Hello(required_features, optional_features)` and sends image bytes only when `HelloResult(supported_features)` contains every required bit. Protocol v5 initially requires import metadata, bounded memory transfer, and guest-memory lifecycle. Different protocol versions remain rejected by the existing header check; at the same version, a missing required bit fails before `LoadImage`. Feature discovery is a startup contract only, not runtime fallback or implementation of callbacks, threads, or dynamic imports. See the [Linux native-helper capability handshake design](docs/design/20260918-317-linux-helper-capability-handshake.md).*

## 2026-09-18 Import event loop / Import event loop

공용 `RunImportEventLoop()`는 `LoadedPeImage`에 있는 gate만 `ImportDispatcher`로 연속 dispatch하고, process exit·fault·thread exit·stopped event를 terminal result로 반환합니다. 미등록 import, 알려지지 않은 gate, dispatch 실패와 import budget 초과는 backend terminal stop과 오류가 됩니다. 현재 Linux CLI의 first-boundary 관찰 정책은 바꾸지 않았으며, 실제 API binding이 추가될 때 이 loop를 제품 실행 정책에 연결합니다. 자세한 내용은 [Import event loop design](docs/design/20260918-320-import-event-loop.md)에 있습니다.

*Shared `RunImportEventLoop()` dispatches only gates in `LoadedPeImage` through `ImportDispatcher` and returns process-exit, fault, thread-exit, and stopped events as terminal results. An unregistered import, unknown gate, dispatch failure, or exhausted import budget produces backend terminal stop and an error. The current Linux CLI first-boundary observation policy remains unchanged; connect this loop to product execution policy when actual API bindings are added. See the [Import event loop design](docs/design/20260918-320-import-event-loop.md).*

## 2026-09-18 Linux CHD first-boundary staging / Linux CHD 첫 경계 staging

Linux CLI도 platform-neutral `Fat32Volume::MaterializeFile()`로 CHD의 canonical PE를 caller-owned temporary staging에 만들고 i386 helper에 전달합니다. 원본 CHD는 읽기 전용으로 남으며 guest VFS·overlay는 후속 API binding 범위입니다. `ez2dj4th` 실제 CHD는 Linux x64에서 `kernel32.dll!GetModuleHandleA` 첫 gate까지 확인됐습니다. 자세한 내용은 [Linux ez2dj4th first-boundary observation](docs/design/20260918-321-linux-ez2dj4th-first-boundary.md)에 있습니다.

*The Linux CLI now uses platform-neutral `Fat32Volume::MaterializeFile()` to create the CHD's canonical PE in caller-owned temporary staging and pass it to the i386 helper. The original CHD remains read-only; guest VFS and overlay belong to later API-binding work. The real `ez2dj4th` CHD reached the first `kernel32.dll!GetModuleHandleA` gate on Linux x64. See [Linux ez2dj4th first-boundary observation](docs/design/20260918-321-linux-ez2dj4th-first-boundary.md).*

## 2026-09-19 Linux first-import stack observation / Linux 첫 import 스택 관찰

Linux `RunOriginalUntilBoundary()`는 알려진 import gate를 받은 뒤 import completion 전에 helper가 허용한 guest ESP에서 정확히 8바이트를 읽습니다. 결과는 관찰 전용 return slot과 arg0으로 CLI에 표시되며, 읽기에 실패하면 helper를 중지하고 오류를 반환합니다. `ez2dj4th`의 첫 `GetModuleHandleA` gate는 x64/x86에서 같은 `0x00ae028a / 0x00ae0f2c`를 보고했습니다. 이는 API binding이나 guest pointer 역참조가 아닙니다. 자세한 내용은 [Linux import-boundary stack observation](docs/design/20260918-322-linux-import-stack-observation.md)에 있습니다.

*After receiving a known import gate, Linux `RunOriginalUntilBoundary()` reads exactly eight bytes at helper-permitted guest ESP before import completion. The CLI displays the observation-only return slot and arg0; a failed read stops the helper and returns an error. The first `GetModuleHandleA` gate for `ez2dj4th` reported the same `0x00ae028a / 0x00ae0f2c` on x64 and x86. This is neither an API binding nor guest-pointer dereference. See [Linux import-boundary stack observation](docs/design/20260918-322-linux-import-stack-observation.md).*

## 2026-09-19 Linux protected-page probe persistence / Linux 보호 페이지 probe 지속성

Linux native IPC fixture는 8 KiB allocation의 second page를 read-only로 바꾼 뒤 first page에 허용된 쓰기를 수행하고, 다음 import에서 실제 변경된 `{5, 8, 7, 6}`을 읽어 cross-import persistence를 검증합니다. 이 fixture 변경은 helper protocol과 allocation protection 정책을 바꾸지 않습니다. 자세한 내용은 [Linux protected-page probe persistent-write expectation](docs/design/20260919-323-linux-probe-persistent-write.md)에 있습니다.

*The Linux native IPC fixture makes the second page of an 8 KiB allocation read-only, performs an allowed write to the first page, and verifies the actual changed `{5, 8, 7, 6}` across the next import. This fixture change does not alter helper protocol or allocation-protection policy. See [Linux protected-page probe persistent-write expectation](docs/design/20260919-323-linux-probe-persistent-write.md).*

## 2026-09-19 Linux first-import argument text observation / Linux 첫 import 인자 텍스트 관찰

첫 known import의 nonzero arg0는 `ExecutionBackend`를 통해 최대 4096바이트만 읽고 첫 NUL까지 observation text로 보관합니다. NUL이 없거나 read가 실패하면 helper를 중지하며, host pointer 변환이나 무제한 pointer traversal은 하지 않습니다. `ez2dj4th`의 x64/x86 첫 gate는 raw text `kernel32`을 보고했습니다. 이는 module registry나 `GetModuleHandleA` binding이 아닙니다. 자세한 내용은 [Linux first-import argument text observation](docs/design/20260919-324-linux-import-argument-text-observation.md)에 있습니다.

*A nonzero arg0 at the first known import is read only through `ExecutionBackend`, at most 4096 bytes, and retained as observation text through its first NUL. Missing NUL or failed read stops the helper; no host-pointer conversion or unbounded pointer traversal occurs. The first `ez2dj4th` gate reported raw text `kernel32` on x64 and x86. This is not a module registry or `GetModuleHandleA` binding. See [Linux first-import argument text observation](docs/design/20260919-324-linux-import-argument-text-observation.md).*

## 2026-09-19 Single-process x86 execution direction / 단일 프로세스 x86 실행 방향

`ExecutionBackend`의 다음 목표는 Linux x86 native i386 및 Linux x64 x86 compatibility-mode backend에서 guest PE와 HLE를 하나의 runtime process에 두는 것입니다. 현재 helper IPC backend는 진단용 fallback으로 남습니다. Windows x64는 OS WoW64 아래의 i386 runtime process에서 같은 guest/HLE 결합을 사용하며, 64비트 process가 32비트 runtime을 실행용으로 load하는 구조는 채택하지 않습니다. 자세한 내용은 [single-process x86 execution-backend design](docs/design/20260919-325-single-process-execution-backends.md)에 있습니다.

*The next `ExecutionBackend` target places guest PE and HLE in one runtime process through Linux x86 native-i386 and Linux x64 x86-compatibility-mode backends. The current helper IPC backend remains diagnostic fallback. Windows x64 uses the same guest/HLE combination inside an i386 runtime process under OS WoW64; it does not adopt loading 32-bit runtime for execution in a 64-bit process. See [single-process x86 execution-backend design](docs/design/20260919-325-single-process-execution-backends.md).*

## 2026-09-19 Linux native PE image component / Linux native PE image component

Linux `NativePeImage`은 PE32 mapping, section copy, HIGHLOW relocation, unmap 수명주기를 소유합니다. 현재 i386 helper가 이를 사용하고, 후속 in-process backend도 같은 component를 사용합니다. IPC protocol과 import thunk ABI는 이 추출로 변경되지 않았습니다. 자세한 내용은 [Linux native PE image lifecycle extraction](docs/design/20260919-327-linux-native-pe-image.md)에 있습니다.

*Linux `NativePeImage` owns PE32 mapping, section copy, HIGHLOW relocation, and unmap lifecycle. The i386 helper uses it now and the later in-process backend will use the same component. This extraction does not change IPC protocol or import-thunk ABI. See [Linux native PE image lifecycle extraction](docs/design/20260919-327-linux-native-pe-image.md).*

## 2026-09-19 Linux native PE session / Linux native PE session

Linux `NativePeSession`은 image, import thunk region, gate table, guest bootstrap을 함께 소유하고 PE prepare, TLS, entry 실행을 제공합니다. i386 helper는 session 밖에서 IPC만 처리합니다. 이 추출은 단일 프로세스 backend 준비 단계이며 protocol 또는 original target completion을 변경하지 않습니다. 자세한 내용은 [Linux native PE session extraction](docs/design/20260919-328-linux-native-pe-session.md)에 있습니다.

*Linux `NativePeSession` owns image, import thunk region, gate table, and guest bootstrap together, providing PE preparation, TLS, and entry execution. The i386 helper handles only IPC outside the session. This extraction prepares the in-process backend and does not change protocol or original-target completion. See [Linux native PE session extraction](docs/design/20260919-328-linux-native-pe-session.md).*

## 2026-09-19 Linux native import bridge / Linux native import bridge

Linux `NativeImportGateBridge`는 i386 `stdcall` ABI에서 thunk가 보존한 원래 guest return address와 stack 시작을 `NativeImportGateEvent`로 만들고 동기 handler를 호출합니다. handler 결과의 EAX·EDX·callee cleanup byte 수는 thread-local bridge storage를 통해 기존 thunk가 적용합니다. 현재 i386 helper는 IPC adapter handler를 등록하지만, `NativePeSession`은 protocol state를 알지 못하므로 이후 단일 프로세스 backend가 같은 bridge에 HLE handler를 등록할 수 있습니다. 자세한 내용은 [Linux native import bridge extraction](docs/design/20260919-329-linux-native-import-bridge.md)에 있습니다.

*Linux `NativeImportGateBridge` forms a `NativeImportGateEvent` from the original guest return address and stack start preserved by the thunk under the i386 `stdcall` ABI, then invokes a synchronous handler. The handler's EAX, EDX, and callee-cleanup byte count are applied by the existing thunk through thread-local bridge storage. The i386 helper currently registers an IPC-adapter handler, but `NativePeSession` knows no protocol state, allowing a later in-process backend to register an HLE handler on the same bridge. See [Linux native import bridge extraction](docs/design/20260919-329-linux-native-import-bridge.md).*

## 2026-09-19 Linux x86 in-process runner / Linux x86 in-process runner

Linux x86 product library의 `NativeInProcessRunner`는 caller가 제공한 PE bytes, parsed image 정보, import handler를 사용해 `NativePeSession`을 같은 process에서 준비하고 TLS·entry를 실행합니다. handler는 bridge에서 동기 호출되며 result 또는 guest fault가 caller로 돌아옵니다. 제품 CLI는 아직 IPC fallback을 사용하고 actual Win32 binding도 없으며, Linux x64는 compatibility-mode transition 구현 전까지 이 component를 빌드하지 않습니다. 자세한 내용은 [Linux x86 in-process runner design](docs/design/20260919-330-linux-x86-in-process-runner.md)에 있습니다.

*`NativeInProcessRunner` in the Linux x86 product library prepares `NativePeSession` in the same process and runs TLS and entry using caller-provided PE bytes, parsed image information, and import handler. The handler is invoked synchronously by the bridge and returns a result or guest fault to its caller. The product CLI still uses the IPC fallback and has no actual Win32 binding; Linux x64 does not build this component until its compatibility-mode transition exists. See [Linux x86 in-process runner design](docs/design/20260919-330-linux-x86-in-process-runner.md).*

## 2026-09-19 Linux x86 in-process runner probe / Linux x86 in-process runner probe

Linux `re2dj_linux_native_in_process_probe`는 IPC probe와 동일한 relocatable synthetic PE를 `NativeInProcessRunner`에 직접 전달합니다. handler는 guest stack의 두 argument를 확인하고 각각 EAX 42, EAX 43·EDX 1과 4-byte callee cleanup을 반환하며 guest는 TLS state와 합산해 51로 종료합니다. 별도 invalid-instruction fixture는 `NativeGuestFault(SIGILL)`도 확인합니다. x86-64 host에도 같은 이름의 probe(`x64/native_in_process_probe.cpp`)가 있어, 같은 fixture를 compatibility mode로 실행하고 fault 뒤 재실행까지 확인합니다(작업 354). 이는 fixture 기반 ABI 검증이며 actual Win32 API binding이나 CLI 선택 변경은 아닙니다. 자세한 내용은 [Linux x86 in-process runner probe design](docs/design/20260919-331-linux-x86-in-process-probe.md)에 있습니다.

*The Linux `re2dj_linux_native_in_process_probe` passes the same relocatable synthetic PE used by the IPC probe directly to `NativeInProcessRunner`. The handler checks two guest-stack arguments and returns EAX 42, then EAX 43·EDX 1, with four-byte callee cleanup; the guest adds TLS state and exits with 51. A separate invalid-instruction fixture also verifies `NativeGuestFault(SIGILL)`. An x86-64 host builds a probe of the same name (`x64/native_in_process_probe.cpp`) that runs the same fixture in compatibility mode and also re-runs it after the fault (Task 354). This is fixture-based ABI validation, not actual Win32 API binding or a CLI selection change. See [Linux x86 in-process runner probe design](docs/design/20260919-331-linux-x86-in-process-probe.md).*

## 2026-09-18 Linux native-helper capability rejection / Linux native-helper 기능 거부

Linux backend는 feature가 누락된 `HelloResult`를 받으면 `LoadImage`를 보내지 않고 오류를 반환합니다. 이 시작 실패 경로는 양쪽 pipe를 닫고 협조적인 child를 제한된 시간 동안 reap한 뒤, 종료하지 않은 child에만 기존 강제 종료를 적용합니다. Linux 전용 rejection fixture는 metadata feature만 응답하고 host EOF 뒤 `eof-before-load-image`를 기록합니다. x64와 x86 host probe가 이 값을 확인하므로 handshake 성공뿐 아니라 이미지 전송 전 거부 순서도 자동 검증합니다. fixture 상태 파일은 test 환경 변수 전용이며 제품 protocol이나 i386 helper 계약에 포함하지 않습니다. 자세한 내용은 [Linux native-helper capability rejection design](docs/design/20260918-318-linux-helper-capability-rejection.md)에 있습니다.

*When it receives a feature-missing `HelloResult`, the Linux backend returns an error without sending `LoadImage`. This startup-failure path closes both pipes, reaps a cooperative child for a bounded time, and uses the existing forced stop only for a child that remains alive. The Linux-only rejection fixture returns only the metadata feature and records `eof-before-load-image` after host EOF. The x64 and x86 host probes check that value, automatically verifying the rejection order as well as successful handshakes. The fixture status file is test-environment-only and is not part of the product protocol or i386-helper contract. See the [Linux native-helper capability rejection design](docs/design/20260918-318-linux-helper-capability-rejection.md).*

## 2026-09-18 Linux guest memory lifecycle / Linux 게스트 메모리 수명

Linux x86·x64 product host와 공용 i386 helper 사이의 protocol v4는 pending import 중 anonymous committed guest mapping의 allocate·protect·free를 지원한다. helper는 mapping base, page-rounded size, read/write/execute access를 32비트 registry에만 보관하고, `ReadMemory`와 `WriteMemory`는 해당 access를 확인한 뒤 복사한다. 따라서 read-only `mprotect` 뒤의 host write는 helper fault가 아니라 오류 packet으로 거부된다. allocation은 exact base free 또는 helper exit·fault·load failure에 해제되며, requested base·partial protect·reserve/decommit은 아직 구현하지 않는다. 실제 Win32 `VirtualAlloc` 의미는 원본 import 관찰과 공용 memory-policy 설계 뒤 별도로 연결한다. 자세한 계약은 [Linux guest memory lifecycle design](docs/design/20260918-315-linux-guest-memory-lifecycle.md)에 있다.

*Protocol v4 between Linux x86/x64 product hosts and the shared i386 helper supports allocate/protect/free for anonymous committed guest mappings during a pending import. The helper keeps mapping base, page-rounded size, and read/write/execute access only in a 32-bit registry, and `ReadMemory`/`WriteMemory` check that access before copying. A host write after read-only `mprotect` is therefore rejected through an error packet rather than faulting the helper. Allocations are released by exact-base free or helper exit, fault, and load failure; requested bases, partial protection, and reserve/decommit remain unimplemented. Connect real Win32 `VirtualAlloc` semantics separately after original-import observations and shared memory-policy design. See the [Linux guest memory lifecycle design](docs/design/20260918-315-linux-guest-memory-lifecycle.md).*

## 2026-09-18 Linux guest-memory subrange protection / Linux 게스트 메모리 하위 범위 보호

Linux i386 helper의 anonymous guest allocation은 이제 4 KiB page별 access 상태를 보유합니다. allocation 내부의 page-aligned protect 요청은 같은 이전 access를 가진 page 집합에만 적용되며, memory transfer는 닿는 모든 page의 access를 확인합니다. allocation의 원래 base와 전체 size는 유지하므로 free는 여전히 원래 base에서만 허용합니다. 이 transport는 x64·x86 probe에서 8 KiB allocation의 두 번째 page read-only, 첫 page write 성공, 두 번째 page write 거부, restore와 free로 검증했습니다. 이는 Win32 `VirtualProtect` 의미나 원본 API 인자를 확정하지 않습니다. 자세한 계약은 [Linux guest-memory subrange protection design](docs/design/20260918-319-linux-guest-memory-subrange-protection.md)에 있습니다.

*Anonymous guest allocations in the Linux i386 helper now retain access state per 4 KiB page. A page-aligned protect request inside an allocation applies only to pages with the same prior access, and memory transfer checks access on every touched page. The allocation's original base and full size remain intact, so free is still allowed only at the original base. The x64 and x86 probes validate an 8 KiB allocation with its second page read-only, successful first-page write, rejected second-page write, restore, and free. This does not establish Win32 `VirtualProtect` semantics or original API arguments. See the [Linux guest-memory subrange protection design](docs/design/20260918-319-linux-guest-memory-subrange-protection.md).*

## 2026-09-03 EZ2DJ 4th field writer 진단 / EZ2DJ 4th field-writer diagnostic

`ez2dj4th` 전용 `--null-context-field-reference-execution-trace`는 Task 152에서 확인한 네 개의 `+0x11c` write 후보를 `DR0`–`DR3` local execution breakpoint로 관찰한다. primary thread와 CREATE_THREAD event로 보고된 새 thread에 후보를 설치하며, hit에서 candidate index, receiver register/value, 계산된 target, write source/value, `C7` immediate 읽기 결과와 target-field 일치 여부를 bounded JSONL로 기록한다. 원본 후보 instruction은 breakpoint를 잠시 비활성화하고 TF single-step으로 한 번 실행한 뒤 breakpoint를 복구한다. 이 옵션은 기존 slot/object-source/field 하드웨어 watch와 동시에 사용할 수 없으며, target match가 확인될 때까지 field 직접 주입과 Hardlock 응답 변경은 하지 않는다.

*The `ez2dj4th`-specific `--null-context-field-reference-execution-trace` observes the four `+0x11c` write candidates identified by Task 152 as local execution breakpoints in `DR0`–`DR3`. It arms the candidates on the primary thread and on threads reported by CREATE_THREAD events, recording candidate index, receiver register/value, calculated target, write source/value, `C7` immediate-read status, and target-field equality in bounded JSONL. The original candidate instruction is executed once with the breakpoints temporarily disabled and TF single-step, then the breakpoints are restored. The option rejects concurrent slot/object-source/field hardware watches and never injects the field or changes Hardlock responses before a target match is confirmed.*

## 2026-09-03 immediate 참조 스캔 / Immediate reference scan

`re2dj::exe::ScanImmediateReferences`(`include/re2dj/exe/immediate_scan.h`)는 바이트 버퍼에서 지정한 32비트 little-endian 값을 찾아 offset, 값, 직전·직후 각 8바이트를 돌려주는 플랫폼 독립 순수 함수다. 명령 디코더가 아니므로 결과는 syntactic 후보이며, 호출자가 주변 바이트로 명령 형태를 해석한다. 기록 상한을 넘어도 스캔은 계속되어 `total_matches`로 총계를 돌려주고 `capped`로 절단을 알리므로, 로그 크기를 제한하면서도 통계는 온전하다. launcher probe의 `--null-context-object-reference-scan`은 이 함수를 값마다 한 번씩 돌려 복호화된 런타임 `.text`에서 target object, singleton 전역, vtable 참조를 수집하고, 참조 직후가 `call rel32`이면 대상 주소를 계산하며, vtable slot과 호출자 코드 창을 함께 기록한다.

*`re2dj::exe::ScanImmediateReferences` (`include/re2dj/exe/immediate_scan.h`) is a platform-neutral pure function that finds requested 32-bit little-endian values in a byte buffer and returns each match's offset, value, and eight bytes on either side. It is not an instruction decoder, so results are syntactic candidates that the caller interprets from the surrounding bytes. Scanning continues past the record cap and returns the full count through `total_matches` while reporting truncation through `capped`, so log size stays bounded without losing the statistics. The launcher probe's `--null-context-object-reference-scan` runs it once per value to collect target-object, singleton-global, and vtable references from the decrypted runtime `.text`, resolves the target when a reference is followed by `call rel32`, and records vtable slots and caller code windows alongside.*

## 2026-09-03 EZ2DJ 4th 진입 추적 / EZ2DJ 4th entry trace

`ez2dj4th` 전용 `--null-context-entry-trace`는 Task 160이 확정한 초기화 체인의 네 함수 진입을 `DR0`–`DR3` execution breakpoint로 관찰한다. breakpoint가 함수 첫 바이트, 즉 `push ebp` 이전에 걸리므로 `[ESP]`에서 호출자 반환 주소를 읽어 receiver·호출자·진입 순서를 bounded JSONL로 기록한다. hit 후에는 breakpoint를 잠시 끄고 TF single-step으로 한 번 통과한 뒤 복구하므로 원본 실행은 바뀌지 않는다. 이 옵션은 기존 하드웨어 watch와 동시에 사용할 수 없다.

*The `ez2dj4th`-specific `--null-context-entry-trace` observes the four function entries of the initialization chain Task 160 established, as `DR0`–`DR3` execution breakpoints. Because a breakpoint fires on the function's first byte, before `push ebp`, the caller's return address is read from `[ESP]`, and receiver, caller, and entry order are recorded as bounded JSONL. After each hit the breakpoints are disabled, TF steps once past the entry, and they are restored, so the original execution is unchanged. The option cannot be combined with the existing hardware watches.*

## 2026-09-03 함수 시작 검색 / Function-start search

`re2dj::exe::FindPrologueBefore`(`include/re2dj/exe/code_scan.h`)는 바이트 버퍼에서 anchor 앞쪽으로 `55 8b ec`를 찾아 함수 시작 후보와 거리를 돌려주는 플랫폼 독립 순수 함수다. anchor 자신은 제외해 함수 시작이 길이 0으로 보고되지 않게 하며, syntactic 검색이므로 결과는 후보다. launcher probe는 참조 스캔이 끝난 뒤 이 함수로 관심 anchor의 함수 시작을 찾고, 이미 읽어 둔 `.text` 복사본에서 코드 영역을 잘라 기록한다. anchor가 함수 시작 창 밖이면 anchor 중심 창을 추가로 남겨 관심 명령이 빠지지 않게 한다.

같은 헤더의 `re2dj::exe::ListNearBranches`는 지정 범위 안의 `call rel32`, `jmp rel32/rel8`, `jcc rel8`, 두 바이트 `0f 8x jcc rel32`를 선형으로 훑어 목적지까지 계산해 돌려준다. 명령 길이를 디코드하지 않으므로 결과는 후보이며, launcher probe는 이를 raw 코드 창과 대조해 검증한다. 이 목록으로 함수 본문의 조기 이탈 분기를 찾아 실행 breakpoint 대상으로 좁힌다.

*`re2dj::exe::ListNearBranches` in the same header linearly walks a range for `call rel32`, `jmp rel32/rel8`, `jcc rel8`, and the two-byte `0f 8x jcc rel32`, resolving each destination. It does not decode instruction lengths, so results are candidates that the launcher probe validates against raw code windows. The listing narrows a function body's early-exit branches down to execution-breakpoint targets.*

같은 헤더의 `re2dj::exe::ScanRelativeBranches`는 버퍼 안의 `call rel32`·`jmp rel32` 중 계산된 목적지가 지정 주소와 같은 것을 모두 찾는다. 목적지 계산은 CPU와 같이 32비트 wrap을 따르고, 기록 상한을 넘어도 계속 세어 `total_sites`를 유지한다. launcher probe는 이 함수로 두 단계 추적을 수행한다. 먼저 함수 시작을 목적지로 하는 분기에서 incremental-link `jmp` thunk를 찾고, 이어서 그 thunk를 목적지로 하는 `call`을 찾아 실제 호출 지점을 얻는다.

*`re2dj::exe::ScanRelativeBranches` in the same header finds every `call rel32` and `jmp rel32` in a buffer whose computed destination equals a given address, following 32-bit wrap as the CPU does and keeping `total_sites` complete past the record cap. The launcher probe uses it for two-stage tracing: branches to a function start reveal the incremental-link `jmp` thunk, and branches to that thunk give the real call sites.*

*`re2dj::exe::FindPrologueBefore` (`include/re2dj/exe/code_scan.h`) is a platform-neutral pure function that searches a byte buffer backward from an anchor for `55 8b ec`, returning the candidate function start and its distance. It excludes the anchor itself so a function start is never reported with zero length, and the search is syntactic, so results are candidates. After the reference scan, the launcher probe uses it to locate the function starts of the anchors of interest and cuts their code regions from the already-read `.text` copy. When an anchor falls outside the function-start window, an anchor-centered window is recorded as well so the instruction of interest is never missing.*

## 2026-09-03 EZ2DJ 4th 객체 상태 진단 / EZ2DJ 4th object-state diagnostic

`ez2dj4th` 전용 `--null-context-object-state-trace`는 field read 직전 경계 `image_base + 0x001a64c`를 `DR0` execution breakpoint로 잡고, hit마다 receiver register, 최대 8단계 caller frame chain, target object 앞 `0x200`바이트의 구조 요약을 bounded JSONL로 기록한다. frame chain 수집과 객체 window 스캔은 `src/tools/windows_x86_launcher_probe/null_context_object_state.*`에 분리하고, main.cpp에는 breakpoint 조율과 기록만 둔다. guest 메모리는 읽기만 하며, hit 상한에 도달하면 breakpoint를 해제해 원래 실행을 방해하지 않는다. 이 옵션은 기존 하드웨어 watch와 동시에 사용할 수 없다.

*The `ez2dj4th`-specific `--null-context-object-state-trace` sets a `DR0` execution breakpoint at boundary `image_base + 0x001a64c`, just before the field read, and records the receiver register, a caller frame chain of up to eight levels, and a structural summary of the first `0x200` bytes of the target object as bounded JSONL on each hit. Frame-chain collection and object-window scanning live in `src/tools/windows_x86_launcher_probe/null_context_object_state.*`, leaving only breakpoint orchestration and recording in main.cpp. Guest memory is read-only, and the breakpoint is released once the hit limit is reached so the original execution is undisturbed. The option cannot be combined with the existing hardware watches.*

## 2026-09-03 진단 idle 경계 설정 / Diagnostic idle-boundary configuration

launcher probe의 bounded 진단 debug-event loop는 `--diagnostic-idle-timeout <milliseconds>`로 idle 경계를 조정한다. 기본값은 기존 동작과 같은 `5000`이고 허용 범위는 `1000`–`600000`이며, 값은 이 loop의 `WaitForDebugEvent` 대기에만 적용한다. 초기 breakpoint 대기와 unload tail 수집 같은 다른 대기 경로는 바꾸지 않는다. 선택된 값은 `launch` 진단 event의 `diagnostic_idle_timeout_ms`에 기록되므로 로그만으로 관찰 경계를 재구성할 수 있다. 호스트 그래픽·오디오 초기화가 기본 경계보다 오래 걸리는 환경에서는 이 값을 늘려 같은 진단이 같은 구간을 덮게 한다.

*The launcher probe's bounded diagnostic debug-event loop takes its idle boundary from `--diagnostic-idle-timeout <milliseconds>`. The default `5000` matches previous behavior, the accepted range is `1000`–`600000`, and the value applies only to that loop's `WaitForDebugEvent` wait; the initial-breakpoint wait, unload-tail collection, and other wait paths are unchanged. The selected value is recorded as `diagnostic_idle_timeout_ms` in the `launch` diagnostic event so the observation boundary can be reconstructed from the log alone. Raise it on hosts whose graphics and audio initialization takes longer than the default boundary so the same diagnostics cover the same interval.*

## 2026-09-04 IAT 슬롯 역방향 해석 및 EZ2DJ 4th 실패 가상 호출 식별 / IAT slot reverse lookup and EZ2DJ 4th failing virtual call identification

`ResolveIatSlot` (`src/tools/windows_original_process_probe/iat_verifier.h`)은 주어진 IAT slot RVA를 받아 import descriptor를 순회하며 모듈명과 함수명(또는 ordinal)을 역방향 조회한다. launcher probe는 언패킹된 IAT 슬롯 `0x006d1908`과 `0x006d1724`를 각각 `USER32.dll!SetRect`와 `KERNEL32.dll!lstrcpynA`로 확정했다. 또한 실패 지점 `RVA 0x00010a6f`의 가상 호출 `call dword ptr [ecx+0x54]`는 `IDirectDraw4::SetDisplayMode`이며, 현대 호스트 `DDRAW.dll`이 `E_NOTIMPL` (`0x80004001`)을 반환하여 초기화가 중단되었음을 확인했다.

*`ResolveIatSlot` (`src/tools/windows_original_process_probe/iat_verifier.h`) takes an IAT slot RVA and iterates import descriptors to reverse-lookup its module and function name (or ordinal). The launcher probe confirmed unpacked IAT slots `0x006d1908` and `0x006d1724` as `USER32.dll!SetRect` and `KERNEL32.dll!lstrcpynA`. Furthermore, the failing virtual call `call dword ptr [ecx+0x54]` at `RVA 0x00010a6f` was identified as `IDirectDraw4::SetDisplayMode`, where the modern host `DDRAW.dll` returned `E_NOTIMPL` (`0x80004001`), halting initialization.*

## 8. 계획된 HLE 계층 / Planned HLE layer **[계획]**

이 표는 추측이 아니라 정식 `ez2dj.exe`의 import 테이블에서 나왔다. 근거와 전체 목록은 [EZ2DJ import 표면](docs/analysis/ez2dj-import-surface.md)에 있다.

*This table comes from the import table of the canonical `ez2dj.exe` rather than from guesswork. The evidence and full list are in [EZ2DJ Import Surface](docs/analysis/ez2dj-import-surface.md).*


| 모듈 | 대체 대상 | 함수 수 | 우선순위 |
| --- | --- | --- | --- |
| `kernel32` | 파일 I/O, 메모리, INI, 모듈 | 약 40 | 1 |
| `user32` | 창, 메시지 루프, 디스플레이 모드 | 약 12 | 1 |
| `ddraw` | DirectDraw 1~6 표면과 블릿 | COM | 2 |
| `gdi32` | DIB 섹션과 블릿 | 14 | 2 |
| `dsound` | ordinal `#1` = `DirectSoundCreate` | COM | 3 |
| `winmm` | 믹서 볼륨, `timeGetTime` | 8 | 3 |
| `user32` | `GetAsyncKeyState` — 입력 전부 | 1 | 4 |
| `advapi32` | `RegFlushKey` stub | 1 | 4 |
| `kernel32` | 스레드, 이벤트, 임계 구역 | 약 20 | 5 |

정식 `ez2dj.exe`가 loader에게 제시하는 import는 **7개 DLL, 161개 함수**다. 그 가운데 17개는 보호 계층이 더한 것이고 게임 본체의 표면은 144개다. **Direct3D도 DirectInput도 별도 DLL import로는 없다.** Direct3D는 DirectDraw COM의 `QueryInterface` 경로로 도달한다. 3rd·4th·5th는 `DINPUT.dll`, `AVIFIL32.dll`, `WS2_32.dll`을 추가로 쓰고 `USER32` 표면도 커진다. 5th의 원본 import 집합은 4th와 정확히 같고, 6th는 반대로 `ADVAPI32`·`AVIFIL32`·`WS2_32`를 전부 빼서 7 DLL / 137개가 된다. 이렇게 제품마다 표면이 다르므로 버전별 HLE 프로파일이 필요하다.

*The canonical `ez2dj.exe` presents **161 functions from 7 DLLs** to the loader; 17 of them are added by the protection layer, leaving a 144-function game surface. **Neither Direct3D nor DirectInput appears as a separate DLL import** — Direct3D is reached through DirectDraw COM `QueryInterface`. The 3rd, 4th and 5th builds add `DINPUT.dll`, `AVIFIL32.dll` and `WS2_32.dll` and grow the `USER32` surface; 5th's original import set is exactly 4th's, while 6th goes the other way and drops `ADVAPI32`, `AVIFIL32` and `WS2_32` entirely for 7 DLLs / 137 functions. Surfaces differing this much per product is why per-version HLE profiles are needed.*


가장 큰 작업은 함수 개수가 아니라 DirectDraw와 DirectSound의 **COM 인터페이스**다. 게스트 메모리 안에 vtable을 만들어 각 슬롯에 gate 주소를 채워야 한다.

*The largest piece is not the function count but the **COM interfaces** of DirectDraw and DirectSound, which need vtables built inside guest memory with a gate address in each slot.*

각 모듈은 `{이름, ordinal, 인자 개수, 호출 규약, 구현 함수}` 항목의 테이블로 표현한다. 로더는 import 이름을 이 테이블에서 찾아 gate 주소를 배정한다. 구현되지 않은 항목은 gate에 남되 호출되면 이름과 함께 실패를 기록한다. 이렇게 하면 **실제로 필요한 API만** 점진적으로 구현할 수 있다.

*Each module is a table of `{name, ordinal, argument count, calling convention, implementation}` entries. The loader looks up an import name and assigns a gate address. Unimplemented entries still get a gate, and a call logs the name and fails, so only APIs the game actually calls need implementing.*

---

## 9. rePIU와의 구조적 차이 / Structural differences from rePIU

| 항목 | rePIU | re2DJ |
| --- | --- | --- |
| 게스트 실행 형식 | DOS/4GW LE | Win32 PE32 |
| 환경 경계 | DOS/DPMI interrupt, port I/O | Win32 import thunk |
| 실행 방식 | 32비트 Win32 호스트에서 네이티브 실행 + VEH 트랩 | 교체 가능한 backend: 데스크톱 native helper |
| 호스트 | Win32 x86 전용 | Windows x64 / Linux x64 및 i386 |
| 그래픽 경계 | Glide (`glide2x`) | DirectDraw / Direct3D |
| 자산 입력 | MAME ROM ZIP + CHD | HDD 디렉터리 경로 + MAME CHD |
| 플랫폼 디렉터리 | `src/platform/win32/` | `src/platform/{windows,linux}/` |

공통으로 유지하는 것: 설계 우선 워크플로, 한국어 우선 이중 언어 문서, 영어 전용 소스 주석, `VERSION` 기반 버전 관리, BSD 3-Clause 라이선스 정책, 원본 자산 비포함 원칙.

*Shared with rePIU: design-first workflow, Korean-first bilingual documents, English-only source comments, `VERSION`-based versioning, the BSD 3-Clause license policy, and the rule that original assets never enter the repository.*

---

## 10. 빌드 구성 / Build configuration **[구현됨]**

`CMakeLists.txt`는 플랫폼 중립 legacy graphics, 공용 코어, SDL3/OpenGL backend와 host·분석·검증 실행 파일을 만든다. Windows 제품 build는 Win32 runtime만 구성하며 64비트 Windows host에서는 WOW64로 실행한다. 별도 Windows x64 preset·CI target은 제거했다. Linux x86·x64 product preset은 같은 SDL3/OpenGL backend와 Linux native backend를 컴파일하고, Linux i386 helper 전용 preset만 SDL3를 제외한다.

| 타깃 | 내용 |
| --- | --- |
| `re2dj_legacy_graphics` | draw command, texture와 vertex-buffer 공용 정적 라이브러리. 후처리 셰이더의 GL 없는 해석·목록(작업 455)과 내장 셰이더 생성 헤더 포함 |
| `re2dj_storage_common` | Win32 게스트 경로 파싱·정규화·ASCII 대소문자 절첩을 공유하는 정적 라이브러리 |
| `re2dj_chd_storage` | libchdr 기반 MAME CHD와 FAT32 read-only 저장소 계층 |
| `re2dj_core` | 공용 코어 정적 라이브러리 |
| `re2dj_windows_original_process_backend` | Win32 제품 CLI와 진단 launcher가 공유하는 원본-process 실행 engine |
| `re2dj_sdl3_opengl_backend` | Win32·Linux 공용 SDL3/OpenGL 렌더 backend. Present의 후처리 pass(`OpenGlPostProcess`, 작업 455) 포함 |
| `re2dj_imgui` | Dear ImGui 코어와 OpenGL3 렌더러 backend. 플랫폼 backend는 포함하지 않음 |
| `re2dj_osd` | 공용 on-screen display. 입력 큐를 받아 backend의 present overlay 지점에 그림 |
| `re2dj` | 명령행 호스트 |
| `re2dj_hdd_probe` | HDD 디렉터리 스캔 도구 |
| `re2dj_chd_probe` | MAME CHD header·metadata·sector와 FAT32/PE 판독 도구. `--extract`로 하위 트리를 호스트에 펼침 |
| `re2dj_pe_analyzer` | PE32 헤더 분석 도구 |
| `re2dj_code_score` | 바이트 구간이 x86 코드인지 암호문인지 판정하는 도구 |
| `re2dj_pe_loader` | PE32 매핑·재배치·import gate 보고 도구 |
| `re2dj_unit_tests` | CTest에 등록된 단위 테스트 |
| `re2dj_windows_x86_launcher_probe` | Win32 x86 원본 EXE entry·IAT 정지점 검증 도구 |
| `re2dj_windows_product_loader_probe` | Win32 canonical 제품 policy와 미지원 target asset-free 검증 도구 |
| `re2dj_linux_native_in_process_probe` | Linux x86/x86-64 host의 in-process PE32 실행(재배치, TLS, import, fault observation) 합성 검증 도구. CTest 등록 |
| `re2dj_linux_compat_mode_probe` | Linux x86-64 host의 compatibility-mode 전환(진입·복귀, TEB FS, import gate, fault, host TLS 보존) 합성 검증 도구. CTest 등록 |
| `re2dj_linux_capability_rejection_helper` | 필수 feature 누락과 image 전송 전 EOF를 검증하는 Linux 전용 protocol fixture |

외부 의존성은 graphics build에서 FetchContent로 고정하는 zlib 라이선스 SDL 3.4.14와 Windows x86 audio build의 SDL_mixer 3.2.4다. SDL3 video/OpenGL은 Win32·Linux 공용 backend를 제공한다. 추가 mixer codec dependency는 활성화하지 않는다.

*The graphics build fetches pinned zlib-licensed SDL 3.4.14 for the shared Win32/Linux video and OpenGL backend. The Windows x86 audio build additionally fetches SDL_mixer 3.2.4. No optional mixer codec dependencies are enabled. The shared core also links the vendored BSD-3-Clause libchdr reader and its permissively licensed codec dependencies; `re2dj_chd_probe` validates a user-supplied image without storing it. Windows product builds target Win32 and run on 64-bit Windows through WOW64; separate Windows x64 presets and CI targets are removed. Linux has x86 and x86-64 product/host presets plus a separate i386 helper preset; the x86 preset can disable optional XScreenSaver/XTest integrations when multilib development packages are absent.*

`re2dj_storage_common`은 게스트 경로 의미를 한 번만 컴파일하며 `re2dj_chd_storage`를 통해 제품 core와 Win32 injected runtime 양쪽에 제공됩니다. 따라서 FAT32 directory lookup이 사용하는 ASCII 대소문자 절첩 심볼이 original-process DLL 링크에서 빠지지 않습니다.

*`re2dj_storage_common` compiles guest-path semantics once and reaches both the product core and the Win32 injected runtime through `re2dj_chd_storage`. This keeps the ASCII case-folding symbol used by FAT32 directory lookup available when the original-process DLL is linked.*

4th Trax 분석 경계에서는 <code>--api-trace</code>가 static <code>ExitProcess</code>
import가 없는 protected target을 bounded event pump으로 관찰할 수 있다. 실제 4th
trace에서 확인된 첫 동적 resolver 대상은 <code>GetVersion</code>과
<code>CreateFileA</code>이며, 이를 4th 전용 CHD VFS wrapper로 연결하는 것은 별도
검증 작업이다. 진단 boundary는 API 순서와 fault를 기록할 뿐 보호 성공을 판정하지
않는다.

*The 4th Trax diagnostic boundary lets <code>--api-trace</code> observe a protected
target without a static <code>ExitProcess</code> import through a bounded event
pump. The first dynamic resolver targets confirmed in the real 4th trace are
<code>GetVersion</code> and <code>CreateFileA</code>; routing them through a 4th-specific
CHD VFS wrapper is a separate verification task. The diagnostic boundary records
API order and faults but does not adjudicate protection success.*

4th profile의 <code>hle_dynamic_vfs</code> capability는 원본
<code>GetProcAddress</code> IAT를 injected runtime thunk로 연결한다. 실제 CHD
trace에서 <code>GetVersion</code>은 native <code>win32</code>,
<code>CreateFileA</code>는 기존 VFS wrapper의 <code>hle</code> 경로로
확인됐다. asset-open과 보호 응답은 아직 미확정이며, bounded diagnostic의
성공 상태를 게임 실행 성공으로 해석하지 않는다.

*The 4th profile's <code>hle_dynamic_vfs</code> capability routes the original
<code>GetProcAddress</code> IAT through the injected runtime thunk. In the real
CHD trace, <code>GetVersion</code> was confirmed on the native
<code>win32</code> route while <code>CreateFileA</code> reached the existing VFS
wrapper's <code>hle</code> route. Asset opening and the protection response remain
unresolved, and a bounded diagnostic success status is not game-execution
success.*

이 resolver는 profile의 <code>hle_dynamic_vfs</code> 기본값으로만 켜지고 그
기본값은 4th에만 있다. 3rd도 보호 장치를 <code>GetProcAddress</code>로 해석한
<code>CreateFileA</code>로 열기 때문에, resolver가 꺼져 있으면 device mock이
그 open을 보지 못하고 보호가 <code>Hardlock</code> 대화상자 뒤 종료 코드
<code>0x00000009</code>로 끝난다. 138번은 제품 기본값을 바꾸지 않고 관찰만
가능하도록 launcher 진단 flag <code>--hle-dynamic-vfs</code>를 추가했다. 3rd
profile 기본값을 바꿀지는 별도 판단으로 남긴다.

*The resolver is enabled only by the profile's <code>hle_dynamic_vfs</code>
default, which only 4th carries. 3rd also opens its protection device through a
<code>CreateFileA</code> resolved by <code>GetProcAddress</code>, so with the
resolver off the device mock never sees that open and the protection ends with a
<code>Hardlock</code> dialog and exit code <code>0x00000009</code>. Task 138 adds
the launcher diagnostic flag <code>--hle-dynamic-vfs</code> so this can be
observed without changing a product default; whether to change the 3rd profile
default is left as a separate judgement.*

121번 resolver trace는 동적 결과의 주소와 원본 caller를 함께 기록한다. 실제
4th run에서 <code>CreateFileA</code> 반환 주소는 runtime module 범위에 있고
caller는 <code>0x00af09f6</code>였지만, VFS wrapper request는 발생하지
않았다. 따라서 주소 선택은 확인됐고 pointer 호출·ABI 호환·보호 후속 분기는
미확정으로 남는다.

*Task 121's resolver trace records both the dynamic result address and the
original caller. In the real 4th run, the <code>CreateFileA</code> result lay
within the runtime module and its caller was <code>0x00af09f6</code>, but no VFS
wrapper request occurred. Address selection is confirmed; pointer invocation,
ABI compatibility, and protected continuation remain unresolved.*

120번 bounded VFS open trace는 resolver 내부의
<code>CreateFileA:route=hle</code>와 실제
<code>Re2djVfsCreateFileA</code> 진입을 분리한다. 실제 4th CHD trace에는
<code>create-file:stage=request</code>가 없었으므로 현재 HLE 경계는 반환
주소 선택까지 확인됐고 wrapper 호출·파일 open은 미확정이다.

*Task 120's bounded VFS-open trace separates the resolver-internal
<code>CreateFileA:route=hle</code> record from entry into
<code>Re2djVfsCreateFileA</code>. The real 4th CHD trace had no
<code>create-file:stage=request</code> event, so the current HLE boundary is
confirmed only through returned-address selection; wrapper invocation and file
opening remain unresolved.*

122번은 resolver caller의 실행 중 memory window를 읽어
<code>CreateFileA</code> 반환 직후 <code>89 45 dc</code>가 EAX를
<code>[EBP-0x24]</code>에 저장하는 것을 확인했다. 이는 반환값 저장 경계이며,
저장된 pointer의 후속 consumer와 indirect call은 아직 미확정이다.

*Task 122 reads the live resolver-caller memory window and confirms that
<code>89 45 dc</code> immediately after the <code>CreateFileA</code> resolver
return stores EAX at <code>[EBP-0x24]</code>. This is a return-value storage
boundary; the stored-pointer consumer and any indirect call remain unresolved.*

작업 123은 bounded first-chance AV diagnostic에서 fault stack return address
직전의 x86 call encoding과 absolute memory-indirect call의 pointer slot을
읽는다. 실제 4th trace에서 <code>0x00aef7fe</code> 직전
<code>FF 15 F4 0C AF 00</code>와 slot <code>0x00AF0CF4</code>를 확인했고,
slot 값은 <code>0x00000000</code>였다. 이 계층은 zero-pointer indirect call과
<code>EIP=0</code> fault를 귀속할 뿐이며, 값을 채우거나 반환 ABI를 보정하지
않는다. slot의 생성 원인과 그 이전 protected continuation은 아직
미확정이다.

*Task 123 extends the bounded first-chance AV diagnostic to read the x86 call
encoding immediately before a fault-stack return address and the pointer slot
used by an absolute memory-indirect call. The real 4th trace confirmed
<code>FF 15 F4 0C AF 00</code> before <code>0x00aef7fe</code> and a current slot
value of <code>0x00000000</code> at <code>0x00AF0CF4</code>. This layer only
attributes the zero-pointer indirect call and the <code>EIP=0</code> fault; it
does not fill the slot or repair the returned-function ABI. The slot's origin
and the earlier protected continuation remain unresolved.*

작업 124는 fault attribution이 찾은 absolute pointer slot을 main image의
committed readable memory에서만 제한적으로 검색한다. 64 KiB block 경계에는
3-byte overlap을 두며, 최대 64개의 일치 위치에 section과 24-byte runtime
window를 기록한다. child context와 memory는 변경하지 않는다. 실제 4th
trace에서는 <code>0x00AF0CF4</code> 참조 12개와
<code>MOV [0x00AF0CF4], EAX</code> writer 3개를 확인했다. native
baseline에도 동일한 zero slot과 fault가 있으므로 현재 HLE가 직접 만든
결과는 아니다. writer의 실제 실행 여부와 EAX 값은 후속 trace 범위다.

*Task 124 performs a bounded search for an absolute pointer slot identified by
fault attribution, limited to committed readable memory in the main image. It
uses a three-byte overlap across 64 KiB blocks and records the section and a
24-byte runtime window for at most 64 matches. Child context and memory remain
unchanged. The real 4th trace found 12 references to
<code>0x00AF0CF4</code>, including three
<code>MOV [0x00AF0CF4], EAX</code> writers. The same zero slot and fault occur
in the native baseline, so the current HLE did not directly create them.
Writer execution and EAX values are follow-up trace scope.*

작업 125의 <code>--slot-writer-trace</code>는 4th 전용 writer RVA 세 곳을
x86 DR0–DR2 local execution breakpoint로 관찰한다. primary thread와 debug
event로 생성된 새 thread에 breakpoint를 적용하며, hit에서 실행 직전 EAX,
slot 이전 값, instruction bytes와 DR6를 기록한다. hit 처리 시 DR6를 지우고
resume flag만 설정하므로 원본 code, general register, slot data는 변경하지
않는다.

실제 trace는 <code>0x00AEFE62</code> writer가 EAX
<code>0x00B17B00</code>을 zero slot에 기록하고, 이후 VFS wrapper가
<code>\\.\NTICE</code>와 <code>\\.\FEnteDev</code> open을 받는 것을
확인했다. broad API software watch 40개를 함께 설치하면 writer가 실행되지
않고 기존 zero-slot fault가 재현되므로, broad API trace는 4th protected
continuation의 투명한 판정 근거로 사용하지 않는다. 다음 HLE 경계는 두 device
path를 VFS file로 처리하는 것이 아니라 별도 device backend로 분리하는 것이다.

*Task 125 adds <code>--slot-writer-trace</code>, using x86 DR0–DR2 local
execution breakpoints for the three 4th-specific writer RVAs. It arms the
primary thread and threads reported by debug events, recording pre-instruction
EAX, the previous slot value, instruction bytes, and DR6. Hit handling clears
DR6 and sets only the resume flag, leaving original code, general registers,
and slot data unchanged.

The real trace confirms that writer <code>0x00AEFE62</code> stores EAX
<code>0x00B17B00</code> into the zero slot, after which the VFS wrapper receives
opens for <code>\\.\NTICE</code> and <code>\\.\FEnteDev</code>. Installing all
40 broad API software watches prevents the writer from running and reproduces
the zero-slot fault, so broad API tracing is not used as transparent evidence
for the 4th protected continuation. The next HLE boundary is a separate device
backend for these paths rather than treating them as VFS files.*

## 2026-09-03 raw I/O profile refinement

`run_defaults.lptdi`의 raw I/O 정책을 capability, 제품 기본 활성화, executable별 helper RVA로 세분화했다. `LegacyIoPortBus`와 `Ez2DjIoBoard`는 공용으로 유지하며, 1st는 `0x00038987`/`0x000389ab`를 기본 활성화한다. 4th는 확인된 byte read `0x000c3817`만 explicit diagnostic opt-in으로 허용하고 write RVA와 제품 기본 활성화는 비워 둔다. 4th 진단은 이 read를 처리한 뒤 `0x00434137` access violation까지 진행했으며, 응답값은 아직 물리 보드 응답으로 확정하지 않았다.

The raw-I/O policy in `run_defaults.lptdi` is split into capability, product-default activation, and executable-specific helper RVAs. `LegacyIoPortBus` and `Ez2DjIoBoard` remain shared; 1st defaults to `0x00038987`/`0x000389ab`. 4th permits explicit diagnostic opt-in only for confirmed byte-read RVA `0x000c3817`, with no write RVA and no product default. The 4th diagnostic handled this read and advanced to an access violation at `0x00434137`; the response is not yet identified as a physical board response.

## 2026-09-06 ez2dj3rd legacy-I/O fallback

`ez2dj3rd`는 공용 legacy-I/O capability와 제품 기본 옵션을 활성화하지만, 3rd helper 주소가 확인되지 않았으므로 실행 파일별 helper RVA는 모두 0으로 유지한다. `legacy_io_port_range_fallback`은 launcher/runtime에 제한된 경로를 추가한다. byte `IN AL,DX`와 `OUT DX,AL`만 후보이며, 포트 지원 여부는 계속 `LegacyIoPortBus`가 결정한다. 지원되지 않는 포트와 opcode는 원래 예외 경로를 그대로 사용한다. 따라서 제품 backend가 `--io-config`를 더 이상 무시하지 않으면서도 3rd cabinet의 실제 raw-I/O 호출 지점이나 배선을 확정하지 않는다.

## 2026-09-06 ez2dj3rd legacy-I/O fallback

`ez2dj3rd` enables the shared legacy-I/O capability and product default, but keeps both executable-specific helper RVAs at zero because no 3rd helper address has been confirmed. Its `legacy_io_port_range_fallback` adds a narrow launcher/runtime path: only byte `IN AL,DX` and `OUT DX,AL` instructions are eligible, and `LegacyIoPortBus` still decides whether the port is supported. The original exception path remains unchanged for unsupported ports and opcodes. The product backend therefore no longer ignores `--io-config`, while the profile still makes no claim about the 3rd cabinet's actual raw-I/O call site or wiring.

## 2026-09-06 ez2dj2nd execution policy

`ez2dj2nd`는 실행 중 fault로 확인된 실행 파일별 raw-I/O helper RVA `0x000782d7`(`IN AL,DX`)와 `0x0007832b`(`OUT DX,AL`)를 사용합니다. 정적 IAT에 `GetPrivateProfileIntA`가 없으므로 `demo_volume` 기본값은 unset입니다. Windows x86 launcher는 일반 타깃의 `DirectDrawCreateEx`를 주입 runtime HLE thunk로 연결하고, packer 보존이 필요한 `ez2dj4th`에만 patch 생략 예외를 둡니다. handoff 전에 `preparation_status` 진단 이벤트를 기록하고, 앞선 선택적 조회가 오류 문자열을 지운 경우 이름이 있는 fallback 오류를 생성합니다. 이 구조는 실행 파일별 가정을 프로파일/launcher 경계에 두며 Hardlock `id_ref`/`id_verify`는 현재 정책에 포함하지 않습니다.

`ez2dj2nd` now carries executable-specific raw-I/O helper RVAs `0x000782d7` for `IN AL,DX` and `0x0007832b` for `OUT DX,AL`, both confirmed by attached runtime faults. Its `demo_volume` default is unset because its static IAT has no `GetPrivateProfileIntA`. The Windows x86 launcher patches `DirectDrawCreateEx` into the injected HLE runtime for ordinary targets, while retaining the no-write packer exception only for `ez2dj4th`. Before handoff, the launcher records a `preparation_status` event and derives a named fallback error if an earlier optional lookup erased the error text. This keeps per-executable assumptions at the profile/launcher boundary and leaves Hardlock `id_ref`/`id_verify` outside the current policy.*

## 2026-09-06 VFS 파일 읽기 bounded 진단

Windows injected VFS는 VFS trace 경로가 설정된 경우 bounded `ReadFile` 진입/결과 이벤트를 기록합니다. 각 이벤트에는 핸들, 요청 크기, overlapped 여부, 반환값, 전송 바이트 수, 마지막 오류 코드만 포함하며 파일 내용과 Hardlock 자료는 제외합니다. native, CHD-backed, device-mock 경로는 같은 진단 경계를 사용합니다. 진입 이벤트 뒤 결과 이벤트가 없으면 실제 파일 읽기 경계 내부 대기로 분류할 수 있고, 결과 이벤트가 있으면 다음 HLE 경계로 조사를 이동할 수 있습니다. 이 계측은 관찰 전용이며 guest 파일 바이트나 프로파일 동작을 변경하지 않습니다.

## 2026-09-06 bounded VFS file-read diagnostics

The Windows injected VFS now records bounded `ReadFile` entry and result
events when a VFS trace path is configured. Each event includes only the
handle, request size, overlapped flag, result, transferred-byte count, and
last-error value; file contents and Hardlock material are excluded. Native,
CHD-backed, and device-mock paths use the same diagnostic boundary. A missing
result after an entry event identifies a wait inside the underlying file-read
boundary, while a returned result moves the investigation to the next HLE
boundary. The instrumentation is observational and does not alter the guest
file bytes or profile behavior.

## 2026-09-06 처리되지 않은 privileged-instruction 진단

legacy I/O handler가 처리하지 못한 첫 `EXCEPTION_PRIV_INSTRUCTION`은 기존 crash-context 형식으로 기록할 수 있습니다. 이 경로는 예외를 복구하지 않고 `EXCEPTION_CONTINUE_SEARCH`를 유지하므로 port 값을 만들거나 `EIP`를 변경하지 않습니다. 따라서 profile에서 legacy I/O가 꺼진 실행에서도 raw-I/O fault가 injected exception handler까지 전달되었는지 관찰할 수 있습니다. 후속 `ez2dj3rd` 실행에서는 이 예외 기록이 없었고, guest가 `ExitProcess` import에 `0xc0000096`을 전달한 기록만 확인되었습니다.

## 2026-09-06 unhandled privileged-instruction diagnostics

The first unhandled `EXCEPTION_PRIV_INSTRUCTION` can now be recorded using
the existing crash-context format. The path preserves
`EXCEPTION_CONTINUE_SEARCH`, so it does not synthesize a port value or modify
`EIP`. This lets a profile with legacy I/O disabled reveal whether a raw-I/O
fault reached the injected exception handler. The follow-up `ez2dj3rd` run
recorded no such exception and instead only showed the guest passing
`0xc0000096` to the `ExitProcess` import.

## 2026-09-06 ez2dj3rd CHD 입력 정책

`ez2dj3rd`는 이제 MAME CHD 입력 형식을 사용하고 기본 이미지 경로 `roms/ez2dj3rd` 아래에서 CHD를 검색합니다. 내장 프로파일은 확인된 내부 실행 파일 `EZ2DJ/EZ2DJ.EXE`를 사용하며, `RunChdTarget`이 `EZ2DJ` 작업 디렉터리를 계산하고 CHD와 staging 경로를 Windows original-process backend에 전달합니다. 기존 3rd VFS, dynamic resolver, DirectSound, console-session, Hardlock 설정, detached 실행 정책은 유지됩니다. 이는 저장소 입력 소스 변경이며 새로운 Hardlock 또는 그래픽 호환성 주장이 아닙니다.

## 2026-09-06 ez2dj3rd CHD input policy

`ez2dj3rd` now uses the MAME CHD input kind and searches `roms/ez2dj3rd`
for its default image. The built-in profile names the confirmed internal
executable `EZ2DJ/EZ2DJ.EXE`; `RunChdTarget` derives the `EZ2DJ` working
directory and passes the CHD plus staging path to the Windows original-process
backend. The existing 3rd VFS, dynamic resolver, DirectSound, console-session,
Hardlock configuration, and detached-run policy remain unchanged. This is a
storage-source change, not a new Hardlock or graphics compatibility claim.

## 스트리밍 PCM 보충 순서 / Streaming PCM refill order

작업 284에서 소비량만으로 ring 시작점을 다시 공급하던 경로를 제거하고 committed
snapshot 비교를 복원했습니다. 최초 Play는 전체 ring을 공급하고 snapshot을 저장합니다.
이후 Unlock은 현재 ring과 snapshot이 다른 원형 구간만 queue에 추가하며, 변경이 없으면
기존 queue를 유지합니다. stopped track 재생성 및 최초 ring 선공급 정책은 유지합니다.

작업 285의 실제 JAM 로그에서 snapshot 방식도 동일 바이트 Unlock을 시간 진행으로 해석하지
못해 무음 ring만 재생하는 것이 확인되었습니다. 현재 구현은 append-only 보충을 사용하지
않습니다. Play/Unlock이 synchronized shadow ring을 갱신하고 mixer의 get callback이 필요한
PCM만 현재 원형 cursor에서 가져갑니다. 작업 284 설명은 이전 구현의 조사 이력입니다.

Task 285's live JAM log confirmed that snapshot refill also failed to treat an identical-byte
Unlock as timeline progress, leaving only the silent ring in playback. The current implementation
does not use append-only refill. Play/Unlock updates a synchronized shadow ring, and the mixer's
get callback pulls only the PCM it currently needs from the circular cursor. The task 284 text
above is retained as investigation history.

Task 284 removes the path that refilled the ring beginning from consumption alone and
restores committed-snapshot comparison. Initial Play supplies the whole ring and stores a
snapshot. Later Unlock calls append only circular ranges that differ from the snapshot; an
unchanged ring leaves the existing queue intact. Stopped-track recreation and initial-ring
prequeue policy remain.

## 2026-09-17 on-screen display / On-screen display

관련 설계: [Dear ImGui OSD와 autoplay 토글](docs/design/20260917-297-imgui-osd-autoplay.md)

```mermaid
flowchart LR
    hostproc["HostWindowProcedure<br/>백틱 / backtick"] --> osd["ui::Osd"]
    guestproc["GuestWindowProcedure<br/>마우스 / mouse"] --> osd
    launcher["런처 / launcher<br/>ArmedAutoplayFlagRva"] -- "g_re2dj_autoplay_flag_address" --> controls["game_controls"]
    controls -- "정보·토글 / info, toggle" --> osd
    backend["Sdl3OpenGlBackend::Present"] -- "PresentOverlay::DrawOverlay" --> osd
```

`include/re2dj/graphics/present_overlay.h`의 `PresentOverlay`는 backend가 게스트 이미지를 창에 합성한 뒤·swap 직전에 부르는 인터페이스다. backend는 ImGui를 모른다. 공용 `ui::Osd`(`include/re2dj/ui/osd.h`, `src/ui/osd.cpp`)가 이를 구현하며, ImGui 플랫폼 backend 대신 자체 입력 큐를 받으므로 공용 코어에 창 API가 들어오지 않는다. 숨겨져 있으면 UI 프레임을 만들지 않는다.

Windows 쪽 `src/platform/windows/osd_host.*`는 프로세스당 하나인 OSD를 backend 생성 시 설치하고 창 메시지를 넘긴다. 키보드 포커스는 최상위 호스트 창에 있으므로 백틱은 `HostWindowProcedure`에서, 마우스는 자식인 게스트 창의 `GuestWindowProcedure`에서 받는다. `src/platform/windows/game_controls.*`는 런처가 써 준 대상 id·실행 파일 이름과 autoplay 주소로 정보 줄과 토글을 등록한다.

게임 제어 주소는 `TargetProfile::game_controls`에 빌드 timestamp와 함께 선언되고, 런처가 `ArmedAutoplayFlagRva`로 실행 파일 timestamp와 대조해 일치할 때만 런타임에 넘긴다. 다른 빌드에서는 토글이 나타나지 않는다.

작업 436부터 `TargetProfile::game_controls`는 빌드별 선언 목록이다. launcher가 여러 실행 파일을 띄우는 프로필(6th)은 실행 파일마다 항목을 두고, 각 프로세스는 자기 실행 파일 timestamp와 같은 항목만 무장한다. Windows launcher probe는 bootstrap 자식(`PrepareBootstrapChildProcess`)에도 대상 id·실행 파일 이름·autoplay 주소를 쓴다. Linux는 CLI가 run마다 `OriginalRunEnvironment::autoplay_flag_rva`를 고르고, 실행기가 주 이미지 base에 더해 `src/platform/native/game_controls.*`에 무장하며, `SdlHostPresentation`(작업 447 전 `LinuxHostPresentation`)이 OSD에 Autoplay 토글을 더한다. Linux 게스트는 host 프로세스 안에서 자기 주소 그대로 돌므로 토글은 그 주소를 직접 읽고 쓴다.

*From Task 436 `TargetProfile::game_controls` is a per-build list of declarations. A profile whose launcher starts several executables (6th) has an entry per executable, and each process arms only the entry matching its own executable's timestamp. The Windows launcher probe also writes the target id, executable name and autoplay address into bootstrap children (`PrepareBootstrapChildProcess`). On Linux the CLI picks `OriginalRunEnvironment::autoplay_flag_rva` per run, the runner adds it to the main image base and arms it in `src/platform/native/game_controls.*`, and `SdlHostPresentation` (`LinuxHostPresentation` before task 447) adds the Autoplay toggle to the OSD; a Linux guest runs at its own addresses inside the host process, so the toggle reads and writes the address directly.*

*`PresentOverlay` in `include/re2dj/graphics/present_overlay.h` is what the backend calls after compositing the guest image to the window and before swapping; the backend knows nothing of ImGui. The shared `ui::Osd` (`include/re2dj/ui/osd.h`, `src/ui/osd.cpp`) implements it and takes its own input queue instead of an ImGui platform backend, keeping window APIs out of the shared core, and builds no UI frame while hidden. On Windows, `src/platform/windows/osd_host.*` installs the process's single OSD when the backend is created and routes window messages: keyboard focus stays on the top-level host window, so backtick arrives in `HostWindowProcedure`, while mouse input arrives in the child guest window's `GuestWindowProcedure`. `src/platform/windows/game_controls.*` registers the information lines and toggle from the target id, executable name and autoplay address the launcher wrote. Game-control addresses are declared in `TargetProfile::game_controls` with a build timestamp, and the launcher passes one to the runtime only when `ArmedAutoplayFlagRva` matches it against the executable's timestamp, so a different build shows no toggle.*

## 2026-09-29 32비트 트루컬러 표면 / 32-bit true-color surfaces

관련 설계: [32비트 트루컬러 표면 선택 옵션](docs/design/20260929-429-true-color-surfaces.md)

```mermaid
flowchart LR
    option["--color-depth · OSD '32-bit color'"] --> switch["graphics::SelectColorDepth"]
    switch --> facades["ddraw facades<br/>(Linux HLE · Windows COM)"]
    switch --> backend["Sdl3OpenGlBackend"]
    facades -- "RGB565 (게스트 / guest)" --> surface["표면 / surface"]
    facades -- "XRGB8888" --> plane["TrueColorPlane"]
    plane -- "LegacyTextureView::true_color" --> backend
    backend --> target["논리 렌더 타깃 / logical render target<br/>GL_RGB565 ↔ GL_RGB8"]
```

**[구현됨]** 선택 옵션인 32비트 색 모드는 게스트가 보는 16비트 계약을 바꾸지 않는다. 대신 호스트 쪽에 채널당 8비트 사본을 둔다. 공용 `graphics::ColorDepth`와 프로세스 스위치(`color_depth.h`)를 facade와 backend가 연산마다 읽는다. 그래서 제품 `--color-depth 16|32`로 정한 값을 OSD 토글이 실행 중에 바꿀 수 있다. 기본값은 16이다.

- **plane.** 픽셀이 있는 표면마다 XRGB8888 `TrueColorPlane`(`true_color.h`)을 둘 수 있다. plane을 버림으로 5-6-5로 줄이면 언제나 게스트의 RGB565 픽셀과 같다. plane은 32비트 모드에서만 만든다. 한번 생기면 표면이 사라질 때까지 두 모드 모두에서 갱신한다.
- **쓰기 경로.** GDI(Linux는 동시 쓰기, Windows는 32bpp DIB DC와 `ReleaseDC`의 줄임), `Blt`/`BltFast` 복사, 색 채우기, 장치 `Clear`, `IDirect3DTexture2::Load`가 plane을 정밀하게 갱신한다. `Unlock`처럼 게스트가 565를 직접 썼을 수 있는 경로 뒤에는 화해(reconcile)한다. 곧 여전히 같은 565로 줄어드는 plane 픽셀은 두고, 나머지만 넓힌다.
- **backend.** 논리 렌더 타깃은 모드에 따라 `GL_RGB565`와 `GL_RGB8` 사이를 오가며 내용을 옮긴다. 둘 다 알파가 없어 `DESTALPHA`는 1.0이다. `GL_RGB8`을 받지 못하는 드라이버에서는 565로 남는다. 32비트 렌더 타깃은 텍스처 RGB를 plane에서, 색상 키를 565에서 가져온다. 렌더 타깃 Lock의 읽기는 줄이고, 쓰기는 화해한다.
- **OSD.** Linux host에도 Windows와 같은 `ui::Osd`를 설치했다. 백틱이 OSD를 켜고 끄며, 보이는 동안 마우스 버튼은 OSD가 가진다. 두 host 모두 공용 `ui::AddColorDepthToggle`로 토글을 등록한다.
- **예외.** Windows 32비트 모드의 `GetDC`는 32bpp DC를 돌려준다. 실제 GDI가 24비트로 그리게 하는 유일한 방법이다.

***[Implemented]** The optional 32-bit colour mode leaves the guest's 16-bit contract alone and keeps an 8-bit-per-channel copy on the host side. Facades and the backend read the shared `graphics::ColorDepth` and its process switch (`color_depth.h`) on every operation, so the OSD toggle can change at run time what the product's `--color-depth 16|32` set. The default is 16.*

- ***The plane.** Each surface with pixels may carry an XRGB8888 `TrueColorPlane` (`true_color.h`), which always narrows by truncation to the guest's RGB565 pixels. Planes are only made in 32-bit mode; once made, one is kept updated in either mode until its surface goes.*
- ***Write paths.** GDI (dual writes on Linux; a 32bpp DIB DC narrowed at `ReleaseDC` on Windows), `Blt`/`BltFast` copies, colour fills, device `Clear`, and `IDirect3DTexture2::Load` update the plane precisely. Paths where the guest may have written 565 itself, such as `Unlock`, reconcile: plane pixels that still narrow to the same 565 stay, and the rest are widened.*
- ***The backend.** The logical render target moves between `GL_RGB565` and `GL_RGB8` with the mode, carrying its contents; neither has alpha, so `DESTALPHA` reads 1.0. A driver that refuses `GL_RGB8` keeps it at 565. A 32-bit target takes texture RGB from the plane and the colour key from 565; render-target locks narrow on read and reconcile on write.*
- ***The OSD.** The Linux host now installs the same `ui::Osd` as Windows: backtick shows and hides it, and while it is shown the mouse buttons belong to it. Both hosts register the toggle through the shared `ui::AddColorDepthToggle`.*
- ***The exception.** In 32-bit mode, Windows `GetDC` returns a 32bpp DC, which is the only way to have real GDI draw at 24 bits.*

## 2026-10-05 화면 후처리 셰이더 / Post-processing shaders

관련 설계: [화면 후처리 셰이더](docs/design/20261005-455-post-process-shaders.md) · 가이드: [post-process-shaders](docs/guides/post-process-shaders.md)

게임이 그린 화면에 표시 단계에서만 libretro 단일 pass GLSL 셰이더를 적용한다(rePIU 작업 768과 같은 형식). 게임은 논리 해상도의 render target에 그리고 `Present`가 그것을 창의 비율 유지 사각형에 quad 하나로 그리므로, 셰이더가 선택되면 그 quad를 `OpenGlPostProcess`의 program으로 그린다. 복사는 없고, render target과 게스트 readback은 그대로이며, `none`이면 Present의 GL 호출은 이전과 같다. `InputSize`·`TextureSize`는 render target 크기(논리 해상도), `OutputSize`는 그림 사각형의 픽셀 크기다. 내장 `crt`·`scanline`은 configure 때 실행 파일에 들어가고, `shaders/*.glsl`(작업 디렉터리, 없으면 실행 파일 옆)이 목록에 더해진다. 시작 셰이더는 `--post-shader` → `RE2DJ_POST_SHADER` → `none` 순이며, OSD의 Screen shader 메뉴가 실행 중 전환·Reload·매개변수를 맡는다. OSD는 GL 없는 `PostShaderControl` 인터페이스만 안다.

*A libretro single-pass GLSL shader (the format of rePIU task 768) applies to the game's picture at presentation only. The game draws into a render target at its logical resolution and `Present` draws that as one quad into the window's aspect-kept rectangle, so with a shader selected the quad is drawn through `OpenGlPostProcess`'s program: no copy, the render target and guest readbacks untouched, and under `none` Present issues the same GL calls as before. `InputSize` and `TextureSize` are the render target's size (the logical resolution), `OutputSize` the picture rectangle's pixel size. The built-in `crt` and `scanline` are embedded at configure time and `shaders/*.glsl` (the working directory, else beside the executable) join the list. The starting shader is `--post-shader`, then `RE2DJ_POST_SHADER`, then `none`, and the OSD's Screen shader menu switches, reloads and adjusts parameters while running; the OSD knows only the GL-free `PostShaderControl` interface.*

## 2026-10-06 OSD의 OpenGL renderer 표시 / The OpenGL renderer in the OSD

관련 설계: [#6](docs/design/20261006-i006-osd-gl-renderer.md)

`Sdl3OpenGlBackend::Initialize`는 GL context를 current로 만든 직후 `GL_RENDERER`·`GL_VENDOR`·`GL_VERSION`과 `SDL_GetCurrentVideoDriver()`를 GL·SDL 없는 값 구조체 `graphics::GlRendererIdentity`(`gl_renderer_identity.h`)에 담고 `renderer_identity()`로 내준다. `SdlHostPresentation`이 OSD를 붙일 때 `ui::Osd::SetRendererIdentity`로 넘기고 `presentation: GL renderer: …` 한 줄을 로그에 남긴다. OSD는 프로세스 정보 줄 바로 아래 Renderer 절에 그리며, `IsSoftwareGlRenderer`가 소프트웨어 렌더러(llvmpipe, GDI Generic, WARP 등)로 판정하면 경고 색과 문구로 구분한다.

*Right after making its GL context current, `Sdl3OpenGlBackend::Initialize` puts `GL_RENDERER`, `GL_VENDOR`, `GL_VERSION` and `SDL_GetCurrentVideoDriver()` into the GL- and SDL-free `graphics::GlRendererIdentity` (`gl_renderer_identity.h`) and exposes it as `renderer_identity()`. `SdlHostPresentation` hands it to `ui::Osd::SetRendererIdentity` when it attaches the OSD and logs one `presentation: GL renderer: …` line. The OSD draws it in a Renderer section right below the process-information lines, in a warning colour with a note when `IsSoftwareGlRenderer` judges it a software rasterizer (llvmpipe, GDI Generic, WARP and the like).*

## 2026-09-19 Linux x86 real first-import completion / Linux x86 실제 첫 import completion

Linux x86 in-process probe observed `ez2dj4th` returning from first `GetModuleHandleA("kernel32")` to `0x00ae028a`; a temporary guest `INT3` stopped at EIP `0x00ae028b`. This diagnostic does not establish real module-handle or later API compatibility. See [the analysis](docs/analysis/ez2dj4th-linux-inprocess-first-import.md).

## 2026-09-20 Linux x86 CLI in-process first import / Linux x86 CLI in-process first import

Linux x86 CLI exposes first-import diagnostic through `re2dj ez2dj4th --run --linux-in-process-first-import`; helper IPC remains the default execution path. The real CHD reports return `0x00ae028a` and SIGTRAP EIP `0x00ae028b`. See [work log 333](docs/work-logs/20260919-333-linux-cli-inprocess-first-import.md).

## 2026-09-20 Linux x86 dynamic resolver observation / Linux x86 동적 resolver 관측

`--linux-in-process-first-resolver` completes pseudo `GetModuleHandleA("kernel32")` then observes `GetProcAddress(kernel32, "GetVersion")` returning to `0x00af0b99`, with SIGTRAP at `0x00af0b9a`. It does not provide executable resolver results. See [work log 334](docs/work-logs/20260920-334-linux-x86-dynamic-resolver-observation.md).

## 2026-09-20 Linux x86 GetVersion thunk 연속 실행 진단 / Linux x86 GetVersion thunk continuation diagnostic

Linux x86의 `--linux-in-process-getversion-call`은 `GetProcAddress(kernel32, "GetVersion")`에 process-local executable thunk를 반환합니다. thunk는 기존 import bridge ABI로 dynamic gate를 전달하며, 호출되면 zero-argument return과 caller return을 관측합니다. 실제 4th CHD 실행은 thunk에 도달하지 못하고 signal 11, EIP `0x00af0c22`에서 중단했으므로 GetVersion 호출은 확인되지 않았습니다. 자세한 결과는 [작업 로그 335](docs/work-logs/20260920-335-linux-x86-getversion-thunk-observation.md)에 있습니다.

*Linux x86 `--linux-in-process-getversion-call` returns a process-local executable thunk for `GetProcAddress(kernel32, "GetVersion")`. The thunk forwards a dynamic gate through the existing import-bridge ABI and would observe the zero-argument return and caller return if called. The real 4th CHD run did not reach the thunk and stopped at signal 11, EIP `0x00af0c22`, so GetVersion invocation is not confirmed. See [work log 335](docs/work-logs/20260920-335-linux-x86-getversion-thunk-observation.md) for details.*
## 2026-09-20 Linux x86 fault context 관측 / Linux x86 fault-context observation

Linux x86 `NativeProcessBootstrap`은 in-process guest fault에서 signal address/code, x86 error code, 일반 레지스터를 보존합니다. `NativeFaultObservation`은 signal handler를 벗어난 뒤 mapped-image와 guarded-stack 범위를 검사해 최대 24 instruction bytes와 최대 네 stack word만 복사합니다. 실제 4th CHD의 GetVersion thunk 연속 실행은 EIP `0x00af0c22`에서 EAX=0인 `MOV CL, byte ptr [EAX]` null read로 중단했습니다. Task 336에서는 producer가 미확정이었지만 Task 338이 미처리 CreateFileA resolver 반환으로 후속 확정했습니다. [작업 로그 336](docs/work-logs/20260920-336-linux-x86-fault-context.md)를 참조하십시오.

*Linux x86 `NativeProcessBootstrap` preserves the signal address/code, x86 error code, and general registers for an in-process guest fault. After leaving the signal handler, `NativeFaultObservation` range-checks the mapped image and guarded stack, copying at most 24 instruction bytes and four stack words. The real 4th CHD GetVersion thunk continuation stopped on `MOV CL, byte ptr [EAX]` with EAX zero at EIP `0x00af0c22`. Task 336 left the producer unresolved, and Task 338 subsequently identified it as the unhandled CreateFileA resolver return. See [work log 336](docs/work-logs/20260920-336-linux-x86-fault-context.md).*
## 2026-09-20 Linux x86 fault provenance window / Linux x86 fault provenance window

Linux x86 fault observation은 이제 EIP 전 64 bytes와 후 16 bytes, 합계 80 bytes를 bounded window로 보존하고 EIP offset을 출력합니다. 실제 4th CHD에서 `XOR ECX, ECX` 뒤 `JB 0x00aef590`가 CF=0으로 미분기한 후 `MOV CL, byte ptr [EAX]` null read가 발생한 것은 확인됐습니다. 그러나 그 window만으로 EAX의 producer는 확정되지 않았습니다. [작업 로그 337](docs/work-logs/20260920-337-linux-x86-fault-provenance-window.md)을 참조하십시오.

*Linux x86 fault observation now preserves a bounded window of 64 bytes before EIP and 16 after it, 80 bytes total, and prints the EIP offset. On the real 4th CHD, `JB 0x00aef590` after `XOR ECX, ECX` was not taken with CF clear, then `MOV CL, byte ptr [EAX]` made the null read. The EAX producer cannot be established from that window alone. See [work log 337](docs/work-logs/20260920-337-linux-x86-fault-provenance-window.md).*
## Linux x86 resolver-continuation trace

**[Implemented]** Linux i386의 GetVersion-call 진단은 동적 GetProcAddress 반환 주소에서 process-local INT3를 arm하고 최대 128개 guest instruction frame을 기록합니다. trace는 guest image 밖의 import-bridge host 코드에서는 TF를 해제하고 guest return slot에 다시 breakpoint를 설치합니다. 4th CHD 실행은 GetVersion resolver 반환 뒤의 두 번째 GetProcAddress(kernel32, CreateFileA)가 현재 처리되지 않아 EAX=0으로 돌아오고, 그 값이 0x00af0c22 null read로 이어짐을 확인했습니다. 근거는 docs/analysis/ez2dj4th-linux-inprocess-first-import.md와 docs/design/20260920-338-linux-x86-resolver-continuation-trace.md에 있습니다.

* **[Implemented]** The Linux i386 GetVersion-call diagnostic arms a process-local INT3 at the dynamic GetProcAddress return address and records up to 128 guest instruction frames. It clears TF outside the guest image in import-bridge host code and re-arms a breakpoint at the guest return slot. The 4th CHD run confirms that a second GetProcAddress(kernel32, CreateFileA) after the GetVersion resolver is currently unhandled, returns EAX=0, and leads to the 0x00af0c22 null read. Evidence is in docs/analysis/ez2dj4th-linux-inprocess-first-import.md and docs/design/20260920-338-linux-x86-resolver-continuation-trace.md.*

## 2026-09-21 Linux x86 CreateFileA 동적 thunk 관측 / Linux x86 dynamic CreateFileA thunk observation

**[Implemented]** `--linux-in-process-createfile-call`은 확인된 pseudo kernel32 resolver에 GetVersion과 CreateFileA 전용 executable thunk를 제공합니다. `NativeImportGateEvent`는 bootstrap이 소유한 guarded guest stack 범위를 함께 전달하므로 진단 handler가 mapped image 또는 stack 안의 ANSI 문자열만 bounded하게 읽습니다. CreateFileA gate는 7개 stdcall 인자를 보존하고 `INVALID_HANDLE_VALUE`와 28바이트 cleanup을 반환한 뒤 caller return의 process-local breakpoint에서 멈춥니다.

실제 4th CHD에서 원본은 CreateFileA thunk를 호출했습니다. 첫 이름은 guest stack의 `\\.\NTICE`, caller return은 `0x00aeffbc`이며 access `0xc0000000`, share `3`, disposition `3`, flags `0`, null security/template가 확인되었습니다. 이 진단은 파일이나 장치를 열지 않으므로 다음 경계는 공용 guest module/handle service와 Linux VFS/device policy입니다. 근거는 [작업 339 설계](docs/design/20260921-339-linux-x86-createfile-thunk-observation.md)와 [분석](docs/analysis/ez2dj4th-linux-inprocess-first-import.md)에 있습니다.

* **[Implemented]** `--linux-in-process-createfile-call` supplies executable thunks dedicated to GetVersion and CreateFileA through the confirmed pseudo-kernel32 resolver. `NativeImportGateEvent` also carries the guarded guest-stack range owned by the bootstrap, allowing the diagnostic handler to read only bounded ANSI strings inside the mapped image or stack. The CreateFileA gate preserves seven stdcall arguments, returns `INVALID_HANDLE_VALUE` with 28-byte cleanup, and stops at a process-local breakpoint on the caller return.*

*In the real 4th CHD, the original code called the CreateFileA thunk. The first name was `\\.\NTICE` on the guest stack, the caller return was `0x00aeffbc`, and the confirmed scalars were access `0xc0000000`, share 3, disposition 3, flags zero, and null security/template values. This diagnostic opens no file or device, so the next boundary is the shared guest module/handle service plus Linux VFS/device policy. See the [Task 339 design](docs/design/20260921-339-linux-x86-createfile-thunk-observation.md) and [analysis](docs/analysis/ez2dj4th-linux-inprocess-first-import.md).*

## 2026-09-21 게스트 PE 호환 모듈 계획 / Guest PE compatibility module plan

**[계획]** `kernel32`, `user32` 등은 DLL별 독립 export descriptor와 구현 파일로 분리합니다. 공용 `GuestModuleRegistry`가 module identity와 이름/ordinal lookup을 소유하고, 같은 descriptor에서 guest-visible PE32 export facade와 x86 thunk를 생성하여 static IAT와 dynamic `GetProcAddress`가 동일 주소를 사용하게 합니다. Linux i386은 실제 facade base를 `HMODULE`로 사용하고, Windows WoW64 경로는 system DLL을 shadow하지 않은 채 같은 descriptor를 IAT patch와 hooked resolver에 적용합니다. 초기 구현은 registry, facade builder, 확인된 `kernel32` 네 export, resolver 이관으로 나눕니다. 자세한 내용은 [작업 340 설계](docs/design/20260921-340-guest-pe-compatibility-modules.md)에 있습니다.

***[Planned]** Separate `kernel32`, `user32`, and other DLLs into independent export descriptors and implementation files. A shared `GuestModuleRegistry` owns module identity plus name/ordinal lookup, while guest-visible PE32 export facades and x86 thunks are generated from the same descriptors so static IAT binding and dynamic `GetProcAddress` use identical addresses. Linux i386 uses the real facade base as `HMODULE`; the Windows WoW64 path applies the same descriptors to IAT patches and the hooked resolver without shadowing system DLLs. The initial implementation is split into registry, facade builder, four confirmed `kernel32` exports, and resolver migration. See the [Task 340 design](docs/design/20260921-340-guest-pe-compatibility-modules.md).*

## 2026-09-22 게스트 모듈 registry 기반 / Guest module registry foundation

**[구현됨]** 플랫폼 중립 `GuestModuleRegistry`는 명시적인 canonical DLL 이름과 alias를 ASCII 대소문자 무시로, export 이름은 대소문자를 구분하여 해석합니다. 등록 시 export descriptor와 32비트 guest mapping을 검증하고, image range·module name/alias·export name/ordinal 충돌을 transactional하게 거절합니다. 이름과 exact guest base handle lookup은 registry가 소유한 동일한 module view를 반환하며, export 이름/ordinal lookup은 같은 thunk address 및 ABI metadata를 반환합니다. 이 단계는 PE facade image 생성, host mapping, `GetModuleHandleA`/`GetProcAddress` API binding을 아직 수행하지 않습니다. 자세한 계약과 검증은 [작업 341 설계](docs/design/20260921-341-guest-module-registry.md)와 [작업 로그](docs/work-logs/20260921-341-guest-module-registry.md)에 있습니다.

***[Implemented]** The platform-neutral `GuestModuleRegistry` resolves explicit canonical DLL names and aliases ASCII-case-insensitively, while export names remain case-sensitive. Registration validates export descriptors and 32-bit guest mappings, transactionally rejecting image-range, module-name/alias, and export-name/ordinal collisions. Name and exact guest-base-handle lookup return the same registry-owned module view; export name/ordinal lookup returns the same thunk address and ABI metadata. This stage does not yet create a PE facade image, map host memory, or bind the `GetModuleHandleA`/`GetProcAddress` APIs. See the [Task 341 design](docs/design/20260921-341-guest-module-registry.md) and [work log](docs/work-logs/20260921-341-guest-module-registry.md) for the contract and validation.*

## 2026-09-22 게스트 PE32 facade builder / Guest PE32 facade builder

**[구현됨]** `GuestPeFacadeBuilder`는 module descriptor와 32비트 image/bridge/cleanup/gate 주소를 받아 합성 i386 PE32 DLL file image를 만듭니다. image는 read-only `.edata` export directory와 execute/read `.text`를 분리하며, 명시적 ordinal을 보존하고 name-only export에 deterministic ordinal을 배정합니다. EAT gap과 ordinal-only export를 표현하고 named export table은 bytewise 정렬합니다. 각 export RVA는 기존 native import bridge 계약과 같은 19-byte x86 thunk를 가리킵니다. 공용 descriptor validation은 registry와 builder가 공유하며 embedded NUL, duplicate, invalid ABI metadata를 한 계약으로 거절합니다.

이 단계는 file-layout bytes와 descriptor 순서의 thunk RVA/ordinal metadata만 생성합니다. host memory allocation, preferred-base mapping, `.edata` R·`.text` RX protection 적용, 실제 `kernel32` descriptor와 registry 등록은 작업 343의 범위입니다. 자세한 내용은 [작업 342 설계](docs/design/20260922-342-guest-pe-facade-builder.md)와 [작업 로그](docs/work-logs/20260922-342-guest-pe-facade-builder.md)에 있습니다.

***[Implemented]** `GuestPeFacadeBuilder` accepts a module descriptor plus 32-bit image/bridge/cleanup/gate addresses and emits a synthetic i386 PE32 DLL file image. It separates a read-only `.edata` export directory from execute/read `.text`, preserves explicit ordinals, assigns deterministic ordinals to name-only exports, represents EAT gaps and ordinal-only exports, and bytewise-sorts the named-export table. Every export RVA points to a 19-byte x86 thunk following the existing native import-bridge contract. The registry and builder share one descriptor validator for embedded-NUL, duplicate, and invalid-ABI rejection.*

*This stage generates only file-layout bytes and descriptor-ordered thunk-RVA/ordinal metadata. Host-memory allocation, preferred-base mapping, `.edata` R and `.text` RX protection, the real `kernel32` descriptor, and registry registration belong to Task 343. See the [Task 342 design](docs/design/20260922-342-guest-pe-facade-builder.md) and [work log](docs/work-logs/20260922-342-guest-pe-facade-builder.md).*

## 2026-09-22 kernel32 Linux i386 facade mapping / kernel32 Linux i386 facade mapping

**[구현됨]** 공용 `kernel32_module.cpp`는 실제 Linux 진단에서 확인된 `GetModuleHandleA`, `GetProcAddress`, `GetVersion`, `CreateFileA` 네 export와 stdcall 인자 수를 하나의 `GuestModuleDescriptor`로 선언합니다. 이 단계의 resolver 두 함수는 아직 guest string/module context가 없으므로 null failure를 반환하고, `GetVersion`은 기존 진단값 0, `CreateFileA`는 기존 `INVALID_HANDLE_VALUE` 실패와 7인자 ABI만 보존합니다. pseudo module handle을 registry resolver로 바꾸는 작업은 344에 남아 있습니다.

***[Implemented]** Shared `kernel32_module.cpp` declares the four exports confirmed by the real Linux diagnostic — `GetModuleHandleA`, `GetProcAddress`, `GetVersion`, and `CreateFileA` — plus their stdcall argument counts in one `GuestModuleDescriptor`. The two resolver functions return null failure in this stage because guest-string/module context is not connected yet; `GetVersion` preserves the existing diagnostic value zero and `CreateFileA` preserves only the existing `INVALID_HANDLE_VALUE` failure and seven-argument ABI. Replacing the pseudo module handle with registry-backed resolution remains Task 344.*

Linux i386의 내부 `NativeGuestModuleSet`은 descriptor export의 staged `ImportGateTable` binding, registry 수명과 bridge dispatch를 소유하고, `native_guest_module_image`가 facade build·exact-address mapping·page protection을 맡습니다. mapper는 64 KiB 정렬 후보에서 충돌 없는 주소를 선택하고, RW staging 뒤 전체를 R로 바꿉니다. executable section은 작업 343에서 RX였으나, 작업 367부터 게스트의 hook 설치를 위해 RWX입니다. 성공한 facade base와 `base + thunk RVA`는 `GuestModuleRegistry`에 등록되고, bridge event는 bounded guest stack에서 descriptor 인자만 복사해 handler EAX/EDX와 stdcall cleanup으로 돌아갑니다. runtime probe는 첫 후보를 의도적으로 점유하여 fallback base를 확인하고, `/proc/self/maps`의 R header·RWX code 보호와 실제 `GetVersion`/`CreateFileA` facade 호출을 검증합니다. 자세한 내용은 [작업 343 설계](docs/design/20260922-343-kernel32-linux-i386-facade.md)에 있습니다.

*The internal Linux i386 `NativeGuestModuleSet` owns staged `ImportGateTable` binding, registry lifetime, and bridge dispatch, while `native_guest_module_image` owns facade build, exact-address mapping, and page protection. The mapper selects a collision-free 64-KiB-aligned candidate, stages RW, then protects the full image R; executable sections, RX in Task 343, are RWX since Task 367 so the guest can install hooks. The successful facade base and `base + thunk RVA` values enter `GuestModuleRegistry`; bridge events copy only descriptor-declared arguments from the bounded guest stack and return handler EAX/EDX plus stdcall cleanup. The runtime probe deliberately occupies the first candidate to verify fallback, checks R-header and RWX-code protection in `/proc/self/maps`, and executes real facade calls to `GetVersion` and `CreateFileA`. See the [Task 343 design](docs/design/20260922-343-kernel32-linux-i386-facade.md).*

## 2026-09-23 앞선 resolver 진단의 facade 합류 / Early resolver diagnostics convergence

**[구현됨]** `--linux-in-process-first-resolver`와 `--linux-in-process-getversion-call`에서 `0x7F000001` pseudo handle과 `0xF1000001` 진단 전용 `GetVersion` thunk를 제거하고 작업 344의 `kernel32` facade로 옮겼다. 세 resolver 진단은 `NativeKernel32Diagnostic`을 공유하고, identity는 `OriginalRunResult::resolver_identity`로 공용화했다. 실제 4th CHD에서 GetVersion-call 진단은 더 이상 `0x00af0c22` null read로 멈추지 않고 `GetVersion`을 실제로 호출하며, caller 복귀 `0x00aefd82`에서 제한된다. 자세한 내용은 [작업 348 작업 로그](docs/work-logs/20260923-348-early-resolver-diagnostics-convergence.md)와 [분석](docs/analysis/ez2dj4th-linux-inprocess-first-import.md)에 있다.

***[Implemented]** Removed the `0x7F000001` pseudo handle and the `0xF1000001` diagnostic-only `GetVersion` thunk from `--linux-in-process-first-resolver` and `--linux-in-process-getversion-call`, moving both onto Task 344's `kernel32` facade. The three resolver diagnostics share `NativeKernel32Diagnostic`, and identity is shared as `OriginalRunResult::resolver_identity`. On the real 4th CHD the GetVersion-call diagnostic no longer stops at the `0x00af0c22` null read; it actually calls `GetVersion` and is bounded at caller return `0x00aefd82`. See the [Task 348 work log](docs/work-logs/20260923-348-early-resolver-diagnostics-convergence.md) and [analysis](docs/analysis/ez2dj4th-linux-inprocess-first-import.md).*

## 2026-09-23 Linux i386 in-process 연속 실행 진단 / Linux i386 in-process continuation diagnostic

**[구현됨]** `--linux-in-process-continue`는 원본을 `kernel32` facade 위에서 처음 처리할 수 없는 지점까지 실행하고, 그 사이의 API 호출을 순서대로 기록한다(최대 256건, 호출 한도 4096). 멈추는 조건은 facade 밖 정적 import 호출, `GetProcAddress`의 0 반환, guest fault, 호출 한도다. 진단은 guest 코드를 쓰지 않는다. 대신 guest stack의 복귀 slot을 host가 소유한 RX `INT3` stub으로 바꿔 멈춘다. import·facade thunk가 `pop ecx; add esp, [cleanup]; jmp ecx`로 복귀하기 때문에 가능한 방식이다. `GetVersion`은 이제 `kKernel32GuestVersion`(`0x23F00206`)을 반환한다. 세 in-process 진단이 복사하던 fault context 변환은 `CopyNativeFaultObservation` 하나로 모았다. 근거와 실제 4th CHD 결과는 [작업 349 설계](docs/design/20260923-349-linux-inprocess-continuation.md)와 [작업 로그](docs/work-logs/20260923-349-linux-inprocess-continuation.md)에 있다.

***[Implemented]** `--linux-in-process-continue` runs the original on the `kernel32` facade until the first point it cannot handle and records the API calls in order (up to 256 entries, with a 4096-call limit). It stops on a static import outside the facade, a zero `GetProcAddress` result, a guest fault, or the call limit. The diagnostic writes no guest code; it stops by redirecting the guest stack's return slot to a host-owned RX `INT3` stub, which works because import and facade thunks return through `pop ecx; add esp, [cleanup]; jmp ecx`. `GetVersion` now returns `kKernel32GuestVersion` (`0x23F00206`). The fault-context conversion the in-process diagnostics each copied is now one `CopyNativeFaultObservation`. See the [Task 349 design](docs/design/20260923-349-linux-inprocess-continuation.md) and [work log](docs/work-logs/20260923-349-linux-inprocess-continuation.md) for the rationale and real 4th CHD results.*

## 2026-09-23 `user32` facade module / `user32` facade module

**[구현됨]** 공용 `user32_module.cpp`는 `user32.dll`(별칭 `user32`)을 `GuestModuleDescriptor`로 선언한다. export는 실제 4th 실행에서 동적 요청이 확인된 `GetActiveWindow`(stdcall, 인자 0) 하나뿐이다. 창을 만드는 export가 아직 없으므로 게스트 창은 존재할 수 없고, handler는 NULL을 반환한다. 창 생성 export를 추가하는 작업은 이 handler를 window service 조회로 바꿔야 한다. Linux i386 진단 문맥 `NativeKernel32Diagnostic`은 `kernel32` 다음에 `user32` facade를 등록한다. 따라서 `GetModuleHandleA("user32")`는 registry base를 받고, `GetProcAddress`는 두 module에서 export를 찾는다. `user32`의 정적 import(`MessageBoxA`, `UpdateWindow`)는 facade에 없으므로 기존 import gate에 남는다. 자세한 내용은 [작업 352 설계](docs/design/20260923-352-user32-facade-module.md)에 있다.

***[Implemented]** Shared `user32_module.cpp` declares `user32.dll` (alias `user32`) as a `GuestModuleDescriptor` whose only export is `GetActiveWindow` (stdcall, no arguments), the one whose dynamic request a real 4th run confirmed. No window-creating export exists yet, so no guest window can exist and the handler returns NULL; the task that adds a window-creating export must turn it into a window-service query. The Linux i386 diagnostic context `NativeKernel32Diagnostic` registers the `user32` facade after `kernel32`, so `GetModuleHandleA("user32")` receives the registry base and `GetProcAddress` finds exports in both modules. `user32`'s static imports (`MessageBoxA`, `UpdateWindow`) are not in the facade and keep their existing import gates. See the [Task 352 design](docs/design/20260923-352-user32-facade-module.md).*

## 2026-10-03 릴리스 산출물 / Release artifacts

작업 442부터 GitHub Actions `release.yml`은 네 job으로 나뉩니다.

- `version`: VERSION과 태그를 검사합니다.
- `windows-x86`: 지금까지의 Windows 빌드·테스트·zip입니다.
- `linux` matrix(x64, x86): Debian 12 컨테이너에서 `scripts/build_release_linux.sh`로 빌드·테스트·tar.gz를 만듭니다. x86은 `i386/debian:bookworm`의 네이티브 i386 툴체인을 씁니다.
- `publish`: 태그 실행에서 세 플랫폼이 모두 성공하면 산출물을 모아 release를 한 번 만듭니다.

Linux release preset은 libstdc++와 libgcc를 정적으로 링크합니다. `scripts/package_release.sh`는 실행 파일이 C 런타임 라이브러리만 직접 링크하고 glibc 2.36보다 새 심볼을 쓰지 않는지 검사합니다. SDL의 X11·Wayland·GL·오디오 backend는 실행 시점에 `dlopen`되므로 배포 대상에는 그 런타임 라이브러리만 있으면 됩니다. 컨테이너는 게스트 런타임의 `modify_ldt` 때문에 `seccomp=unconfined`로 돕니다. JavaScript action은 32비트 컨테이너 안에서 돌지 못하므로, job은 runner에서 돌고 빌드만 `docker run`으로 컨테이너에 들어갑니다.

*From Task 442 `release.yml` in GitHub Actions has four jobs: `version` checks VERSION against the tag; `windows-x86` is the Windows build, tests and zip as before; a `linux` matrix (x64, x86) builds, tests and makes a tar.gz with `scripts/build_release_linux.sh` in a Debian 12 container, x86 with `i386/debian:bookworm`'s native i386 toolchain; and on a tag run `publish` gathers the artifacts and creates the release once all three platforms succeed. The Linux release presets link libstdc++ and libgcc statically, and `scripts/package_release.sh` checks that the executable links only the C runtime's libraries and uses no glibc symbol newer than 2.36. SDL's X11, Wayland, GL and audio backends are `dlopen`ed at run time, so a target needs only those runtime libraries. The containers run with `seccomp=unconfined` for the guest runtime's `modify_ldt`. JavaScript actions cannot run inside a 32-bit container, so the jobs run on the runner and only the build goes into the container through `docker run`.*
