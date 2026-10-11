# TODO

현재 진행 중인 작업과 아직 결정되지 않은 항목만 기록합니다. 완료 항목은 [구현 완료 항목](IMPLEMENTED.md)으로 이동합니다.

*This file contains only active work and unresolved items. Completed items are moved to [Implemented](IMPLEMENTED.md).*

두 호스트 모두 원본을 re2dj 자기 프로세스 안에서 실행합니다(작업 446~449). injection 시절의 Windows 진단·검증 항목과 Linux 원본 실행 경로(작업 077·311·340)는 2026-10-09에 정리해 [구현 완료 항목](IMPLEMENTED.md#todo-정리-2026-10-09--todo-cleanup-2026-10-09)으로 옮겼습니다.

*Both hosts run the original inside re2dj's own process (tasks 446 to 449). The injection-era Windows diagnostic and verification items and the Linux original-execution path (tasks 077, 311 and 340) were cleaned up on 2026-10-09 and moved to [Implemented](IMPLEMENTED.md#todo-정리-2026-10-09--todo-cleanup-2026-10-09).*

## 현재 진행 / In progress

- [ ] 작업 127 — ez2dj4th Hardlock Function `0x0e` 변환 독립 복원
  - [x] Git-ignore `cfg/hardlock.ini` 기본 경로와 profile별 memory-only 적재
  - [x] 실제 `0x468`/active-console `0x450` 및 synthetic 분기의 matching `0x44c/0x458` 재확인
  - [x] 플랫폼 중립 IOCTL sequence/shape/descriptor 검증 oracle 구현
  - [x] 작업 128 — 서명된 vendor driver로 네 IOCTL framing과 device-transport 경계 독립 확인
  - [ ] 원본 실행 또는 허용 라이선스 자료로 bit-level transform 근거 확보
  - [ ] 외부 설정의 memory-only seed를 사용하는 플랫폼 중립 transform 구현
  - [ ] 알려진 입출력 vector와 원본 다음 경계로 응답 검증

  *Task 127 — Git-ignored default configuration, memory-only profile loading, real `0x468`/active-console `0x450` reacquisition, and a platform-neutral sequence/shape/descriptor oracle are complete. Task 128 independently confirms the four IOCTL framing contracts in a signed vendor driver and establishes that Function `0x0e` crosses into device transport rather than a host-side three-seed transform. Independently reconstruct Function `0x0e`, implement it only after a policy-compatible bit-level basis is established, and verify it against a known input/output vector and the original execution's next boundary.*

## 사용자 보류 / Paused by user

- [ ] 작업 111 — ez2dj3rd Hardlock `0x9c402458` Function `0x0e` 응답 판별 경계 복원
  - [ ] 264바이트 in-place descriptor 마지막 8바이트의 반환 뒤 소비 경로 추적
  - [ ] Task 107 seed 후보별 synthetic 응답을 기본 비활성 분석 경계에서 생성·검증
  - [ ] 원본 실행의 다음 분기 oracle로 후보 교집합을 축소하되 실제 seed로 성급히 확정하지 않기
  - 재개 기준: [Hardlock 보류 체크포인트](work-logs/20260901-112-3rd-hardlock-pause-checkpoint.md)의 `0x450` replay, `0x44c` tail 분기와 남은 미확정 목록을 먼저 읽습니다.

  *Task 111 — reconstruct the post-return consumer for the final eight bytes of the 264-byte Function-`0x0e` descriptor, generate and test Task 107 candidate responses only behind a default-off analysis boundary, and reduce their intersection using the original execution's next-branch oracle without prematurely identifying physical seeds. Resume from the [Hardlock pause checkpoint](work-logs/20260901-112-3rd-hardlock-pause-checkpoint.md), which records the `0x450` replay, the `0x44c` tail branch, and unresolved items.*

## 다음 작업 / Next work

- [ ] in-process 러너 성능 분석(2026-10-05 등록, [측정 결과](analysis/windows-in-process-performance.md))
  - [ ] 조건별 반복 측정으로 처리량 차이(4th −17%, 1st SE −22%, 5th −11%, 6th −16%, 2nd MOVE +10%) 확정
  - [ ] vsync off 실행에서 import별 호출 횟수와 샘플링 프로파일로 비용이 큰 경계 찾기
  - [ ] 전역 게스트 잠금의 넘겨주기 비용과 스레드 직렬화 영향 측정
  - [ ] 자주 불리는 단순 import(시간·동기화)의 빠른 경로 검토
  - [ ] Private bytes 증가(최대 1.4 GB)와 6th working set 증가의 원인 확인
  - [ ] 측정용 vsync off 선택(현재 `--vsync`는 거부됨)을 정식 옵션으로 둘지 결정
  - [ ] Linux x86과 x64의 X11 처리량 차이(x86이 51~84%)와 x86 상주 private 메모리(+85~100 MB)의 원인 확인([Linux 비교](analysis/linux-windows-performance.md), #4)
  - [ ] Linux 6th 성능: x64 Debug에서 타이틀 약 40 FPS, x86 Debug에서 모드 선택 전환 중 4.5 FPS

  *In-process runner performance analysis, registered 2026-10-05 ([measurements](analysis/windows-in-process-performance.md)): settle the throughput differences with repeated runs, find the costly boundaries from per-import call counts and a sampling profile of vsync-off runs, measure the global guest lock's hand-over cost and thread serialization, consider fast paths for simple, frequent imports (time, synchronization), explain the private-bytes growth (up to 1.4 GB) and 6th's larger working set, decide whether a vsync-off choice for measuring (`--vsync` is refused now) becomes a real option, explain Linux x86's X11 throughput gap to x64 (51 to 84%) and its extra resident private memory (+85 to 100 MB) ([Linux comparison](analysis/linux-windows-performance.md), #4), and Linux 6th's frame rate in Debug (about 40 fps at the title on x64, 4.5 fps in the mode-select transition on x86).*

- [ ] EZ2DJ 6th 남은 확인
  - [ ] 데모 플레이의 BGA 자리에 보이는 색 노이즈가 원본 연출인지 확인
  - [ ] 광원(`SetLight`·`LightEnable`): 6th는 쓰지 않음. 쓰는 게임이 나오면 측정해 모델

  *EZ2DJ 6th, what is left: check whether the colour noise in the demo's BGA area is the original's own effect; and lights (`SetLight`, `LightEnable`), which 6th does not use, to be measured and modelled when a game does.*

- [ ] 공용 HLE의 남은 모형 / Shared HLE still to model
  - [ ] 게스트 스레드의 남은 것: `TerminateThread`(1st의 스레드 종료 시간 초과 경로, 지금은 resolve-only), `CREATE_SUSPENDED`/`ResumeThread`, `ExitThread`, `GetExitCodeThread`, 스레드별 메시지 큐. import 없이 도는 게스트 코드는 다른 스레드를 막음(설계 417)
  - [ ] `StretchBlt` 확대·축소 대응(설계 421, 지금은 1:1만)
  - [ ] `DrawTextA`의 한글(CP949 2바이트)과 가변 폭: `System` 글꼴은 가변 폭("iW" 18px)이라 Unifont 고정 8px와 글자 배치가 다름. 한글이 필요해지면 Unifont 한글 글리프(16×16)를 더 가져옴
  - [ ] CP949 두 byte 표(현재는 단일 byte만, 두 byte는 정지)
  - [ ] 일광 절약 규칙과 한국어 zone 이름(`GetTimeZoneInformation`)
  - [ ] 키·마우스 창 메시지와 마우스 이동량(`--io-config`는 작업 444에서 두 host 공용)
  - [ ] `SetCursor` 실제 구현(지금은 resolve-only)
  - [ ] 게스트 호출 중 게스트 SEH가 host frame을 건너 unwind하는 경우
  - [ ] 게스트 루트 밖 경로(`windows` 지원 디렉터리)와 디렉터리 dump 실행의 게스트 파일
  - [ ] 게스트 entry 레지스터(EBX 등) 정의. 두 host 모두 host 잔여값을 넘김(작업 356 분석)
  - [ ] Linux 소리의 노이즈(사용자 청취 확인 2026-09-27: 소리는 들리나 노이즈가 섞임). 원인 후보: 스트리밍 링 재채움 시점, 샘플 복사 경계, WSLg PulseAudio 버퍼 크기

  *Shared HLE still to model: the rest of guest threads (`TerminateThread`, resolve-only now, for 1st's thread-shutdown timeout path; `CREATE_SUSPENDED`/`ResumeThread`; `ExitThread`; `GetExitCodeThread`; per-thread message queues; guest code running without imports blocks other threads, design 417); enlarging and shrinking `StretchBlt` (design 421, 1:1 only now); Korean (CP949 double-byte) and proportional text in `DrawTextA`; the CP949 double-byte table; daylight-saving rules and Korean zone names in `GetTimeZoneInformation`; keyboard and mouse window messages and mouse motion (`--io-config` is shared by both hosts since task 444); a real `SetCursor`; guest SEH unwinding across a host frame during a guest call; paths outside the guest root and guest files of directory-dump runs; defined guest entry registers such as EBX; and the noise mixed into Linux sound.*

- [ ] HDD의 CHD에서 곡 로딩·화면 전환 멈춤 줄이기(#9 후속, [작업 로그](work-logs/20261009-i009-chd-file-prefetch.md))
  - [ ] 처음 읽는 작은 파일(키음 등) 하나마다 HDD 접근 25~48ms가 쌓여 캐시 없는 곡 로딩이 1.1~2.9초 멈춤. 디렉터리·FAT나 곡 폴더 단위 미리 읽기 검토
  - [ ] `..\..\ranking\ranking_*.bin`을 여는 `CreateFileA`가 CHD를 읽지 않고도 65~80ms 걸리는 원인 확인

  *Reduce song-loading and screen-transition stalls from a CHD on an HDD (#9 follow-up): each small file read for the first time (key sounds and the like) adds 25 to 48 ms of HDD access, so a cold song load stalls 1.1 to 2.9 s, so consider reading directories, the FAT or whole song folders ahead; and find why `CreateFileA` of `..\..\ranking\ranking_*.bin` takes 65 to 80 ms without reading the CHD.*

- [ ] 3rd present 비용 변화 원인 — 2026-09-17 01:25~01:49 사이 실행 환경 변화 뒤 present가 약 15 ms 블록되고 60 fps로 바뀜. OSD 코드와 무관함은 A/B로 확인([작업 297 로그](work-logs/20260917-297-imgui-osd-autoplay.md))
- [ ] `ez2dj3rd` 입력 슬롯 `0x1b`의 물리 바인딩 — 게임 내장 autoplay 토글([분석](analysis/ez2dj3rd-demo-play.md))
- [ ] 나머지 타깃의 `game_controls` — `game-state-hunt` 스킬로 빌드별 autoplay 플래그 확인. 남은 후보: `ez2dj2nd`(덤프 미수집). 완료: `ez2dj3rd`(작업 297), `ez2dj4th`(작업 300), `ez2dj5th`(작업 301), `ez2dj1stse`(작업 302), `ez2d2m`(작업 305), `ez2dj6th`(작업 436). 변수 없음: `ez2dj1st`(작업 304, 데모 전용 플레이어 장면)

## 분석 미완료 / Analysis remaining

- [ ] `Songs/` 아래 자산 파일 형식 분석
- [ ] 3rd target guest drive and working-directory evidence
- [ ] 실패 경로 continuation buffer(힙 페이지)의 의미 추적 — entry 직후 XOR 루프 대상 버퍼와 `[0x01ed7074]` 플래그 기입 지점
- [ ] 3rd 보호 빌드(`EZ2DJ.EXE`)의 import 표면과 런타임 흐름

## 보류 / Deferred

- [ ] Windows x64 host expansion
- [ ] Native 32-bit Linux kernel에서의 실행 검증

- [ ] Task 096 visual revalidation: confirm scene-transition flicker/fade-out and z-order behavior with the user's current display setup
- [ ] 작업 097 Music Select 좌표·창 pixel viewport 재검증 (창 크기/DPI 변경 포함)

*Task 097's trace shows internally centered logical coordinates for the Music Select composition, while the SDL/OpenGL backend now refreshes its pixel viewport after native child-window or DPI size changes. The exact user-visible artwork position still needs a reproducible Music Select capture.*

- [ ] Task 097 coordinate revalidation: confirm Music Select artwork placement after window resize or DPI changes
