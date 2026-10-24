# 릴리즈 노트 / Release Notes

## v0.0.75 (2026-10-25)

### 한국어

화면 업스케일 셰이더 `fsr`(AMD FSR 1 EASU+RCAS)·`scalefx`(ScaleFX)·`anime4k`(Anime4K CNN x2 S)를 내장하고, 화면 셰이더가 libretro 형식 `.glslp` preset으로 여러 pass를 잇습니다(#26, #27).

#### 해결된 이슈

- [#26](https://github.com/reexec/re2DJ/issues/26) 내장 화면 셰이더 fsr(AMD FSR 1 EASU) — PR 없음(로컬 머지)
- [#27](https://github.com/reexec/re2DJ/issues/27) 다단계 화면 셰이더(.glslp)와 fsr RCAS·ScaleFX·Anime4K — PR 없음(로컬 머지)

#### 1. 업스케일러 (#26, #27)
- DLSS처럼 여러 프레임을 모으는 업스케일러는 모션 벡터·깊이가 없는 2D 게임에 쓸 수 없어, 프레임 하나만 보는 셋을 넣었습니다. `fsr`는 `ffx_fsr1.h`의 EASU·RCAS를 GLSL 1.20으로 옮겼고(`FSR_SHARPNESS`), `scalefx`는 libretro glsl-shaders의 다섯 pass를 그대로, `anime4k`는 bloc97의 mpv 훅 다섯 개를 변환했습니다(가중치 그대로, y 뒤집음). 셋 다 MIT이며 `THIRD_PARTY_NOTICES.md`에 적었습니다.
- 4K 전체 화면 비교: `fsr`가 가장 선명(RCAS의 밝은 테는 선명도로 조절), `scalefx`는 픽셀아트용이라 CG 글자가 딱딱해짐, `anime4k`는 잡티 없이 적당히 선명. 4th vsync off: `none` 799, `fsr` 688, `scalefx` 538, `anime4k` 565fps.

#### 2. 다단계 파이프라인 (#27)
- `graphics/post_shader_preset.h`: `.glslp` 부분집합(`shaders`, `shaderN`, `scale_typeN`/`_x`/`_y`, `scaleN`/`_x`/`_y`, `filter_linearN`, `float_framebufferN`; libretro 규칙대로 `#`·BOM·우선순위). 내장은 포함 파일 묶음(`kPostShaderBuiltinFiles`, CMake가 17개 파일을 12000바이트 조각으로)이 되었고 `LoadPostShaderPreset`이 preset의 상대 경로를 풉니다.
- `OpenGlPostProcess`: pass마다 프로그램과 FBO 대상(RGBA8/RGBA16F), `UseForPresent`가 체인을 그리고 마지막 pass만 호출자에게 남기며, 배율 있는 마지막 pass는 stock pass가 창에 복사합니다. 텍스처 unit 0 `Texture`, 1 `OrigTexture`, 2부터 `PassPrevN`. 대상 실패 때는 GL 오류를 비우고 창 대상·viewport·unit 0을 되돌립니다.
- 커밋 전 검토(다섯 관점, 지적별 반박 검증 셋)로 확정된 11건을 고쳤습니다. 단일 pass 셰이더의 `OrigTexture`가 빈 unit을 가리키던 회귀, FBO 실패가 Present 전체를 실패시키던 것, 체인 중간 실패의 복원, 파서의 libretro 호환, 문서 불일치.

#### 3. 검증
- Windows x86 Debug: 단위 테스트 6,519건 실패 0(`post_shader_preset_test` 추가), CTest 4개. Release, GL probe 12개. WSL Linux x64·x86·clang(경고를 오류로): CTest 각 5개. 실제 실행: 4th·6th에서 세 셰이더, 사용자 `.glslp`, `OrigTexture` 단일 pass.

### English

The screen upscaling shaders `fsr` (AMD FSR 1 EASU+RCAS), `scalefx` (ScaleFX) and `anime4k` (Anime4K CNN x2 S) are built in, and screen shaders chain passes through libretro-style `.glslp` presets (#26, #27).

#### Resolved issues

- [#26](https://github.com/reexec/re2DJ/issues/26) A built-in fsr screen shader (AMD FSR 1 EASU) — no PR (local merge)
- [#27](https://github.com/reexec/re2DJ/issues/27) Multi-pass screen shaders (.glslp) with fsr RCAS, ScaleFX and Anime4K — no PR (local merge)

#### 1. The upscalers (#26, #27)
- Upscalers that gather frames, like DLSS, cannot serve a 2D game without motion vectors or depth, so three that look at one frame went in: `fsr` ports `ffx_fsr1.h`'s EASU and RCAS to GLSL 1.20 (`FSR_SHARPNESS`), `scalefx` takes libretro glsl-shaders' five passes unchanged, and `anime4k` converts bloc97's five mpv hooks (weights unchanged, y turned over); all MIT, recorded in `THIRD_PARTY_NOTICES.md`.
- Compared fullscreen at 4K: `fsr` is the sharpest (RCAS's light rim adjustable by the sharpness), `scalefx`, made for pixel art, hardens CG lettering, and `anime4k` is moderately sharper without artifacts. 4th with vsync off: `none` 799, `fsr` 688, `scalefx` 538, `anime4k` 565 fps.

#### 2. The multi-pass pipeline (#27)
- `graphics/post_shader_preset.h`: a `.glslp` subset (`shaders`, `shaderN`, `scale_typeN`/`_x`/`_y`, `scaleN`/`_x`/`_y`, `filter_linearN`, `float_framebufferN`; `#`, a BOM and precedence as libretro reads them). Built-ins became embedded file sets (`kPostShaderBuiltinFiles`, CMake embedding 17 files in 12000-byte pieces), and `LoadPostShaderPreset` resolves a preset's relative paths.
- `OpenGlPostProcess`: per pass a program and a framebuffer target (RGBA8/RGBA16F); `UseForPresent` draws the chain and leaves only the last pass to the caller, a scaled last pass copied to the window by the stock pass; texture unit 0 `Texture`, 1 `OrigTexture`, 2 onwards `PassPrevN`; a failed target drains the GL error and restores the window target, viewport and unit 0.
- A pre-commit review (five angles, three refuters per finding) fixed 11 confirmed findings: the `OrigTexture` regression in single-pass shaders, a failed FBO failing the whole presentation, restoring after a mid-chain failure, the parser's libretro compatibility, and document mismatches.

#### 3. Verification
- Windows x86 Debug: 6,519 unit checks with 0 failures (`post_shader_preset_test` added), 4 CTest tests; Release and the GL probe's 12 checks. WSL Linux x64, x86 and clang (warnings as errors): 5 CTest tests each. Real runs: the three shaders on 4th and 6th, a user `.glslp`, and a single-pass `OrigTexture` shader.

## v0.0.74 (2026-10-12)

### 한국어

HDD의 CHD에서 읽었던 경로를 기억해 다음 실행에서 미리 읽고(#24), in-process 러너의 import 경로와 DirectDraw `Clear` 비용을 줄였으며 `--vsync` 옵션을 더했습니다(#25).

#### 해결된 이슈

- [#24](https://github.com/reexec/re2DJ/issues/24) 읽었던 CHD 경로를 기억해 다음 실행에서 미리 읽기(HDD 곡 로딩 멈춤) — PR 없음(로컬 머지)
- [#25](https://github.com/reexec/re2DJ/issues/25) in-process 러너의 import 경로와 DirectDraw Clear 비용 줄이기, --vsync 옵션 — PR 없음(로컬 머지)

#### 1. 읽었던 경로 미리 읽기 (#24)
- `hle::GuestFileReadJournal`이 게스트의 조회·목록·읽기를 경로마다 사용 횟수와 함께 기록하고(10초 넘게 쉬었다 다시 쓰면 한 번 더), `overlay/.re2dj-readahead`에 15초마다와 종료 때 디스크의 기록에 합쳐 저장합니다. 다음 실행의 학습 작업 스레드가 두 번 이상 쓰인 경로를 64MiB까지 게스트가 한가할 때 읽습니다.
- 폴더 통째 미리 읽기는 CHD 안에서 폴더가 흩어져 효과가 없어 넣지 않았습니다(실험은 작업 로그).
- 캐시를 비운 HDD에서 6th 데모 곡 로딩: 기록 없음 2.17·1.33초, 두 실행 분 기록 0.58·0.65초.

#### 2. import 경로와 `Clear` (#25)
- 바깥에서 본 샘플링 프로파일로 import마다의 이름 검색(약 30%)과 `Clear`의 버퍼 세 번 훑기(약 25%)를 찾았습니다. 게이트 주소 색인·캐시, 인자 스택 배열, 한 번 채우기로 vsync off 처리량이 6~43%, `Clear`를 쓰는 4th·5th·6th는 17~26% 더 올랐습니다.
- `--vsync on|off|adaptive`. private bytes(게스트마다 처음부터 커밋하는 512MB arena)와 6th working set(부트스트랩도 러너)의 원인을 분석 문서에 남겼습니다.

#### 3. 검증
- Windows x86 Debug: 단위 테스트 6,438건 실패 0, CTest 4개. Release·RelWithDebInfo 빌드. WSL Linux x64·x86·clang(경고를 오류로): CTest 각 5개.

### English

The CHD paths read are remembered and read ahead in later runs (#24), and the in-process runner's import path and DirectDraw `Clear` cost less, with a `--vsync` option (#25).

#### Resolved issues

- [#24](https://github.com/reexec/re2DJ/issues/24) Remember the CHD paths read and read them ahead in later runs (song-load stalls on an HDD) — no PR (local merge)
- [#25](https://github.com/reexec/re2DJ/issues/25) Cut the in-process runner's import path and DirectDraw Clear costs, and a --vsync option — no PR (local merge)

#### 1. Reading remembered paths ahead (#24)
- `hle::GuestFileReadJournal` records the guest's lookups, listings and reads per path with a use count (one more after 10 s without it) and is saved to `overlay/.re2dj-readahead` every 15 s and at the end, merged into the journal on disk; the next run's learning worker reads the paths used twice or more, up to 64 MiB, while the guest is idle.
- Reading whole directories ahead did not help, the directories lying scattered in the CHD, and was left out (experiments in the work log).
- 6th's demo song loads from an HDD with the caches emptied: 2.17 and 1.33 s with no journal, 0.58 and 0.65 s with two runs' journal.

#### 2. The import path and `Clear` (#25)
- An outside sampling profile found the by-name lookup on every import (about 30%) and `Clear` passing over its buffer three times (about 25%); a gate-address index and cache, arguments in a stack array and a one-pass fill raised vsync-off throughput by 6 to 43%, and by 17 to 26% more for 4th, 5th and 6th, which use `Clear`.
- `--vsync on|off|adaptive`. The analysis document records the causes of the private bytes (a 512 MB arena each guest commits up front) and 6th's working set (its bootstrap runs as a runner too).

#### 3. Verification
- Windows x86 Debug: 6,438 unit checks, 0 failures, 4 CTest tests; Release and RelWithDebInfo build. WSL Linux x64, x86 and clang (warnings as errors): 5 CTest tests each.

## v0.0.73 (2026-10-11)

### 한국어

패드의 LT+RT+Y로 OSD를 열고 패드로 조작합니다(#22).

#### 해결된 이슈

- [#22](https://github.com/reexec/re2DJ/issues/22) 게임패드로 OSD 열기와 조작 LT+RT+Y (rePIU #55) — PR [#23](https://github.com/reexec/re2DJ/pull/23)

#### 1. 게임패드 OSD (#22)
- rePIU #55와 같습니다: 한 패드에서 LT+RT+Y가 눌리는 순간 OSD를 토글합니다(유지 시간 없음, 여러 패드에 나눈 것은 아님). 열려 있는 동안 패드는 OSD 탐색에만 쓰이고 게임에는 가지 않으며, 닫힐 때 누르고 있던 입력은 뗄 때까지 게임에 숨깁니다. 키보드는 그대로 게임에 갑니다.
- `re2dj/input/pad_osd_chord.h`(`IsPadOsdChordDown`, `PadChordEdge`, `PadGameGate`). OSD는 `SetGamepad`로 받은 상태를 ImGui 게임패드 키로 넘기고(`NavEnableGamepad`), 열릴 때 포커스를 주며, 직전 프레임에 열린 메뉴가 없을 때 B로 닫습니다. `SdlHostPresentation::Present`가 토글, 관문, OSD 전달을 맡고, 종료 조합(#20)은 관문 앞 상태로 봅니다.
- re2DJ EZ2DJ 기본 배치에서는 LT·RT·Y가 게임 입력이라 조합을 이루는 동안은 게임에도 들어갑니다(설계에 기록).

#### 2. 검증
- 단위 테스트 `CheckOsdChord`(조합, 가장자리, 관문). Linux x64·x86 Debug(경고를 오류로), clang: CTest 각 5개, 단위 테스트 6,401건 실패 0.
- 실제(Linux, Xbox Series X Controller): LT+RT+Y마다 `OSD shown`/`hidden`, D-pad·A 조작, B로 메뉴와 OSD 닫기, 그동안 게임에 패드 입력 없음을 사용자가 확인.
- CI: Windows x86, Linux x64 gcc·clang, Linux x86 통과.

### English

LT+RT+Y on a pad opens the OSD and the pad drives it (#22).

#### Resolved issues

- [#22](https://github.com/reexec/re2DJ/issues/22) Open and drive the OSD with a gamepad, LT+RT+Y (rePIU #55) — PR [#23](https://github.com/reexec/re2DJ/pull/23)

#### 1. The gamepad OSD (#22)
- As rePIU #55: LT+RT+Y going down on one pad toggles the OSD (no hold time, not split across pads); while it is open the pad only navigates it and none of it reaches the game, input held as it closes staying hidden until released; the keyboard still reaches the game.
- `re2dj/input/pad_osd_chord.h` (`IsPadOsdChordDown`, `PadChordEdge`, `PadGameGate`). The OSD passes the state `SetGamepad` gives it to ImGui's gamepad keys (`NavEnableGamepad`), takes focus as it opens, and closes on B when no menu was open the frame before. `SdlHostPresentation::Present` handles the toggle, the gate and handing the pads to the OSD; the exit chord (#20) reads the state ahead of the gate.
- In re2DJ's EZ2DJ default layout LT, RT and Y are game inputs, so they reach the game while the chord is being formed (recorded in the design).

#### 2. Verification
- Unit test `CheckOsdChord` (the chord, the edge, the gate). Linux x64 and x86 Debug (warnings as errors) and clang: 5 CTest tests each, 6,401 unit checks, 0 failures.
- Real (Linux, an Xbox Series X Controller): `OSD shown`/`hidden` with each LT+RT+Y, the d-pad and A, B closing a menu and the OSD, and no pad input reaching the game meanwhile, confirmed by the user.
- CI: Windows x86, Linux x64 gcc and clang, and Linux x86 pass.

---

## v0.0.72 (2026-10-10)

### 한국어

패드 조합 LT+RT+L3+R3을 1초 누르면 게임과 런처가 끝나고(#20), GitHub Actions를 Node 24 액션과 Ubuntu 26.04 runner로 옮겼습니다(#19).

#### 해결된 이슈

- [#19](https://github.com/reexec/re2DJ/issues/19) GitHub Actions의 Node.js 20 경고와 Ubuntu 26 runner — PR [#21](https://github.com/reexec/re2DJ/pull/21)
- [#20](https://github.com/reexec/re2DJ/issues/20) 패드 조합 LT+RT+L3+R3으로 게임 종료(스팀덱) — PR [#21](https://github.com/reexec/re2DJ/pull/21)

#### 1. 패드 종료 조합 (#20)
- rePIU #52와 같은 조건입니다: 한 패드에서 두 트리거(절반 이상)와 두 스틱 클릭을 1초 이어서 누르면 한 번 발동하고, 떼야 다시 준비됩니다. 여러 패드에 나눈 것은 세지 않습니다.
- 게임은 창 닫기와 같은 `close_requested_` 경로로 끝나 런처로 돌아오고, 런처는 Quit과 같이 닫힙니다. 조합 중에도 입력은 게임에 그대로 전달됩니다.
- `re2dj/input/pad_exit_chord.h`(`IsPadExitChordDown`, `PadExitChordTimer`), `Sdl3GamepadReader::ReadEach()`, 게임(`SdlHostPresentation::Present`)과 런처 창의 타이머.

#### 2. GitHub Actions (#19)
- checkout v7, cache v6, upload-artifact v7, download-artifact v8(Node 24, rePIU #50과 같음). Node.js 20 경고가 없어졌습니다.
- Linux runner를 `ubuntu-26.04`로 고정했습니다(`ci.yml`, `release.yml`, `pages.yml`). Linux 릴리스 바이너리는 Debian 12 컨테이너와 glibc 2.36 상한 그대로이며, 새 runner의 x64 바이너리가 v0.0.71의 것과 바이트 단위로 같았습니다.

#### 3. 검증
- 단위 테스트 `CheckExitChord`. Linux x64·x86 Debug(경고를 오류로), clang: CTest 각 5개, 단위 테스트 6,386건 실패 0.
- CI와 릴리스 수동 실행이 `ubuntu-26.04`에서 annotation 없이 통과, Linux 패키지 `GLIBC_2.36`.
- 실제(Linux, Xbox Series X Controller): 게임 중 1초 유지 → `host window closed` → 런처 복귀(종료 코드 0), 런처에서 1초 유지 → 종료.

### English

LT+RT+L3+R3 held for a second ends the game and the launcher (#20), and GitHub Actions moved to Node 24 actions and Ubuntu 26.04 runners (#19).

#### Resolved issues

- [#19](https://github.com/reexec/re2DJ/issues/19) GitHub Actions Node.js 20 warnings and Ubuntu 26 runners — PR [#21](https://github.com/reexec/re2DJ/pull/21)
- [#20](https://github.com/reexec/re2DJ/issues/20) End the game with the pad combination LT+RT+L3+R3 (Steam Deck) — PR [#21](https://github.com/reexec/re2DJ/pull/21)

#### 1. The pad exit chord (#20)
- As rePIU #52 has it: both triggers (past half) and both stick clicks held on one pad for an unbroken second fire once, re-armed only by a release; the four split across pads do not count.
- The game ends through `close_requested_`, as closing its window does, back to the launcher; the launcher closes as Quit does. The controls still reach the game while held.
- `re2dj/input/pad_exit_chord.h` (`IsPadExitChordDown`, `PadExitChordTimer`), `Sdl3GamepadReader::ReadEach()`, and the timer in the game (`SdlHostPresentation::Present`) and the launcher window.

#### 2. GitHub Actions (#19)
- checkout v7, cache v6, upload-artifact v7 and download-artifact v8 (Node 24, as rePIU #50); the Node.js 20 warnings are gone.
- The Linux runners are pinned to `ubuntu-26.04` (`ci.yml`, `release.yml`, `pages.yml`). The Linux release binaries keep the Debian 12 containers and the glibc 2.36 limit, and the new runner's x64 binary was byte-identical to v0.0.71's.

#### 3. Verification
- Unit test `CheckExitChord`. Linux x64 and x86 Debug (warnings as errors) and clang: 5 CTest tests each, 6,386 unit checks, 0 failures.
- CI and a manual release run passed on `ubuntu-26.04` with no annotation, the Linux packages at `GLIBC_2.36`.
- Real (Linux, an Xbox Series X Controller): a second's hold in game gave `host window closed` and the launcher back (exit code 0); in the launcher it quit.

---

## v0.0.71 (2026-10-10)

### 한국어

런처가 GitHub의 새 릴리스를 알리고 Update 버튼으로 설치한 뒤 다시 시작하며, 릴리스 산출물 이름이 `re2DJ-v*`가 됩니다(#17).

#### 해결된 이슈

- [#17](https://github.com/reexec/re2DJ/issues/17) 런처 자동 업데이트와 산출물 이름 re2DJ-v* (rePIU #48) — PR [#18](https://github.com/reexec/re2DJ/pull/18)

#### 1. 런처 자동 업데이트 (#17)
- rePIU #48의 업데이트 계층을 `re2dj/update/`(공용 코어)로 옮겼습니다: `SemanticVersion`, 최신 릴리스 JSON 파서(`ReleaseInfo`), `Sha256`, tar.gz(`tinfl`)·zip(miniz 메모리 리더) 풀기와 위험한 경로 거부(`ReleaseArchive`), 설치 조건·교체·되돌림·`.re2dj-old` 정리(`UpdateInstall`), 백그라운드 상태 기계(`LauncherUpdater`).
- 플랫폼: `platform/https_download.h`(Linux는 시스템 `curl`을 `posix_spawnp`로, https만 따름; Windows는 WinHTTP), `self_process.h`의 `SelfExecutablePath`·`ReplaceSelfProcess`(Linux `execve`)·`RunExecutableAndWait`·`ClearEnvironmentVariable`.
- 런처: 제목 아래 알림 줄·Update/Retry·진행 막대, staged가 되면 창을 닫고 세션이 설치한 뒤 다시 시작(Linux는 같은 PID, Windows는 자식). 실행 파일 옆 `VERSION`이 빌드 버전과 같을 때만 설치하고, 남은 `.re2dj-old`는 다음 시작 때 지웁니다. 끄기는 `[Launcher] check_updates = 0`, `RE2DJ_UPDATE_CHECK=0`, 시험용 `RE2DJ_UPDATE_CURRENT_VERSION`.
- rePIU와 다르게, 공용 코어가 OS 코드를 링크하지 않도록 다운로드 함수는 런처 세션이 넘기고, 자산 이름은 대소문자를 무시해 이전 릴리스의 `re2dj-v…`도 찾습니다. miniz의 ZIP 읽기를 켰습니다(`MINIZ_NO_ARCHIVE_APIS` 제거).

#### 2. 산출물 이름 (#17)
- `package_release.sh`·`.ps1`, `release.yml`의 workflow artifact, 사이트 안내 문구를 `re2DJ-v<ver>-<platform>`로 바꿨습니다. tar의 최상위 폴더도 같은 이름이고, 실행 파일은 `re2dj`(`re2dj.exe`) 그대로입니다.

#### 3. 검증
- 단위 테스트 `launcher_update_test`(rePIU probe 13개 묶음, 소문자 자산 이름 포함), `launcher_test`의 `check_updates`. Linux x64·x86 Debug(경고를 오류로)·Release, clang: CTest 각 5개, 단위 테스트 6,365건 실패 0. Release의 NEEDED는 그대로입니다.
- 실제(Linux x64 Release): v0.0.70 릴리스로 알림 → Update → 9개 파일 교체 → 다시 시작, 같은 PID가 새 실행 파일로 바뀜. 빌드 트리에서는 버튼 없음, 남은 `.re2dj-old` 2개 정리, 확인 끄기. `package_release.sh`로 `re2DJ-v0.0.70-linux-x64.tar.gz` 생성.
- CI: Windows x86, Linux x64 gcc·clang, Linux x86 통과. Windows의 실제 교체는 사용자 확인으로 남았습니다.
- 가이드: [런처 업데이트와 스팀덱 설치](docs/guides/launcher-update.md).

### English

The launcher announces a new GitHub release, installs it with an Update button and restarts, and release artifacts are named `re2DJ-v*` (#17).

#### Resolved issues

- [#17](https://github.com/reexec/re2DJ/issues/17) Launcher self-update and re2DJ-v* artifact names (rePIU #48) — PR [#18](https://github.com/reexec/re2DJ/pull/18)

#### 1. Launcher self-update (#17)
- rePIU #48's update layer moved into `re2dj/update/` (shared core): `SemanticVersion`, the latest-release JSON parser (`ReleaseInfo`), `Sha256`, unpacking tar.gz (`tinfl`) and zip (miniz's memory reader) with dangerous paths refused (`ReleaseArchive`), the install condition, replacement, rollback and `.re2dj-old` removal (`UpdateInstall`), and the background state machine (`LauncherUpdater`).
- Platform: `platform/https_download.h` (Linux runs the system `curl` through `posix_spawnp`, following https only; Windows uses WinHTTP), and `SelfExecutablePath`, `ReplaceSelfProcess` (Linux `execve`), `RunExecutableAndWait` and `ClearEnvironmentVariable` in `self_process.h`.
- Launcher: a notice under the title with Update/Retry and a progress bar; once staged the window closes and the session installs and restarts (the same PID on Linux, a child on Windows). Only a release install whose `VERSION` next to the executable matches the build is replaced, and leftover `.re2dj-old` files go at the next start. Off with `[Launcher] check_updates = 0` or `RE2DJ_UPDATE_CHECK=0`; `RE2DJ_UPDATE_CURRENT_VERSION` for testing.
- Unlike rePIU, the launcher session passes the download function so the shared core links no OS code, and asset names are matched ignoring case, finding earlier releases' `re2dj-v…`. miniz's ZIP reading is switched on (`MINIZ_NO_ARCHIVE_APIS` dropped).

#### 2. Artifact names (#17)
- `package_release.sh` and `.ps1`, `release.yml`'s workflow artifacts and the site text now say `re2DJ-v<ver>-<platform>`; the tar's top folder has the same name, and the executable stays `re2dj` (`re2dj.exe`).

#### 3. Verification
- Unit test `launcher_update_test` (rePIU's probe, 13 groups, a lower-case asset name included) and `check_updates` in `launcher_test`. Linux x64 and x86 Debug (warnings as errors), Release and clang: 5 CTest tests each, 6,365 unit checks, 0 failures; the Release NEEDED lists are unchanged.
- Real (Linux x64 Release): against the v0.0.70 release, notice, Update, nine files replaced and a restart, the same PID turning into the new executable; a build tree shows no button, two leftover `.re2dj-old` files were removed, the check turns off; `package_release.sh` wrote `re2DJ-v0.0.70-linux-x64.tar.gz`.
- CI: Windows x86, Linux x64 gcc and clang, and Linux x86 pass. A real replacement on Windows is left for the user to check.
- Guide: [launcher updates and a Steam Deck install](docs/guides/launcher-update.md).

---

## v0.0.70 (2026-10-10)

### 한국어

전체 화면과 비율 유지를 게임 OSD와 런처에서 바꾸고 `cfg/re2dj.ini`에 저장하며(#14), EZ2DJ 3rd가 창 모드 DirectDraw로 실행됩니다(#15).

#### 해결된 이슈

- [#14](https://github.com/reexec/re2DJ/issues/14) 전체 화면·비율 유지 옵션과 저장, 런처 게임패드 동작 (rePIU #45·#34) — PR [#16](https://github.com/reexec/re2DJ/pull/16)
- [#15](https://github.com/reexec/re2DJ/issues/15) ez2dj3rd가 user32 GetClientRect에서 멈춤(창 모드 DirectDraw) — PR [#16](https://github.com/reexec/re2DJ/pull/16)

#### 1. 전체 화면·비율 유지 (#14)
- `window_policy.h`의 `ComputePresentRect`: 비율 유지면 기존 `FitPresentation`(4:3과 검은 띠), 아니면 창 전체. 백엔드 그리기(`SetKeepAspect`)와 마우스 좌표 변환이 함께 씁니다.
- `SdlHostPresentation`: OSD 맨 앞의 "Fullscreen"(present가 끝난 뒤 적용)·"Keep aspect ratio" 토글, Alt+Enter(Enter는 EZ2DJ의 2P 턴테이블이라 게임에 넘기지 않음), 더블클릭. 사용자 조작으로 실제로 바뀐 때만 observer를 부르고, CLI가 `cfg/re2dj.ini`를 다시 읽어 `[Video] fullscreen`·`keep_aspect` 두 키만 바꿔 저장합니다.
- 모든 실행의 시작 값은 명령줄(`--fullscreen`/`--windowed`, 새 `--keep-aspect`/`--stretch`) → `cfg/re2dj.ini` → 기본값(프로필의 전체 화면, 비율 유지 켬) 순입니다. 로그에 `display : …`로 남습니다.
- 런처: Options 맨 위 두 체크박스, 창의 전체 화면(시작·체크박스·Alt+Enter, Alt+Enter는 ImGui로 넘기지 않음), 비율 유지 시 960×640 비율 영역을 가운데에 두고 바깥은 검게. 표에서 Space 시작을 없앴습니다(rePIU #34).
- EZ2DJ 게임패드 기본값을 `config/ez2dj-io.example.ini`의 값으로 올렸습니다: 1P 키 `DPAD_LEFT` `DPAD_UP` `A` `Y` `B`, 페달 `X`, 이펙터 `LB` `RB` `LT` `RT`.

#### 2. 3rd의 창 모드 DirectDraw (#15)
- 3rd 덤프는 `"FullScreen" = 0`이라 `SetCooperativeLevel(hwnd, DDSCL_NORMAL | DDSCL_FPUSETUP)`로 창 모드를 쓰고, 매 프레임 오프스크린을 primary로 `Blt`합니다. resolve-only였던 `user32!GetClientRect`에서 멈췄습니다.
- user32 `GetClientRect`·`ClientToScreen`, 새 `IDirectDrawClipper` facade(`ddraw_clipper.cpp`: `CreateClipper`(DX7·DX6), `SetHWnd`·`GetHWnd`), 서피스 `SetClipper`·`GetClipper`(참조를 잡고 서피스와 함께 놓음). clip list는 모델링하지 않습니다.

#### 3. 검증
- 단위 테스트: `ComputePresentRect`, 설정의 `keep_aspect`·시작 값 우선순위·`SaveDisplayPreferences`, 게임패드 기본값, `GetClientRect`·`ClientToScreen`, clipper와 참조 수. Linux x64·x86 Debug(경고를 오류로)·Release, clang: CTest 각 5개, 단위 테스트 6,351건 실패 0.
- 실제 실행(Linux x64·x86): 시작 값 우선순위, 게임 중 8번의 전환이 모두 저장되고 런처의 다른 키를 지킴, 런처의 전체 화면·비율 유지 배치·Space·패드 A, 늘린 게임 화면과 Alt+Enter 때 턴테이블이 눌리지 않음을 사용자가 화면으로 확인. 3rd는 사용자가 곡(`redocean`) 플레이까지 확인했고, x86에서도 모드 선택까지 경고 없이 진행.
- CI: Windows x86, Linux x64 gcc·clang, Linux x86 통과.

### English

Fullscreen and keep-aspect change in the game OSD and the launcher and are kept in `cfg/re2dj.ini` (#14), and EZ2DJ 3rd runs through windowed DirectDraw (#15).

#### Resolved issues

- [#14](https://github.com/reexec/re2DJ/issues/14) Fullscreen and keep-aspect options, kept in cfg, and launcher pad behaviour (rePIU #45, #34) — PR [#16](https://github.com/reexec/re2DJ/pull/16)
- [#15](https://github.com/reexec/re2DJ/issues/15) ez2dj3rd stops at user32 GetClientRect (windowed DirectDraw) — PR [#16](https://github.com/reexec/re2DJ/pull/16)

#### 1. Fullscreen and keep-aspect (#14)
- `ComputePresentRect` in `window_policy.h`: the existing `FitPresentation` (4:3 with black bars) when keeping the shape, the whole window otherwise, shared by the backend's drawing (`SetKeepAspect`) and the mouse mapping.
- `SdlHostPresentation`: "Fullscreen" (applied after the present) and "Keep aspect ratio" toggles first in the OSD, Alt+Enter (its Enter, EZ2DJ's 2P turntable, withheld from the game) and the double click. The observer is called only when a user action actually changed a value, and the CLI re-reads `cfg/re2dj.ini` and saves it with just `[Video] fullscreen` and `keep_aspect` changed.
- Every run starts from the command line (`--fullscreen`/`--windowed`, the new `--keep-aspect`/`--stretch`), then `cfg/re2dj.ini`, then the defaults (the profile's fullscreen, keep-aspect on), logged as `display : …`.
- Launcher: both checkboxes at the top of Options; its window follows fullscreen (at start, the checkbox and Alt+Enter, which never reaches ImGui) and with keep-aspect lays out a centred 960×640-shaped area with black outside; Space no longer starts a row (rePIU #34).
- EZ2DJ's gamepad defaults became `config/ez2dj-io.example.ini`'s values: player 1's keys `DPAD_LEFT` `DPAD_UP` `A` `Y` `B`, the pedal `X`, the effectors `LB` `RB` `LT` `RT`.

#### 2. 3rd's windowed DirectDraw (#15)
- The 3rd dump has `"FullScreen" = 0`, so it runs windowed through `SetCooperativeLevel(hwnd, DDSCL_NORMAL | DDSCL_FPUSETUP)` and blits an offscreen surface onto the primary every frame; it stopped at the resolve-only `user32!GetClientRect`.
- user32 `GetClientRect` and `ClientToScreen`, a new `IDirectDrawClipper` facade (`ddraw_clipper.cpp`: `CreateClipper` for DX7 and DX6, `SetHWnd`, `GetHWnd`), and surface `SetClipper` and `GetClipper` (holding the reference and releasing it with the surface); clip lists are not modelled.

#### 3. Verification
- Unit tests: `ComputePresentRect`; the settings' `keep_aspect`, start-up precedence and `SaveDisplayPreferences`; the gamepad defaults; `GetClientRect` and `ClientToScreen`; the clipper and its reference counts. Linux x64 and x86 Debug (warnings as errors), Release and clang: 5 CTest tests each, 6,351 unit checks, 0 failures.
- Real runs (Linux x64 and x86): the start-up precedence; eight in-game switches all saved with the launcher's other keys kept; the launcher's fullscreen, keep-aspect layout, Space and pad A; and the stretched game picture with no turntable press on Alt+Enter, all checked on screen by the user. The user played 3rd through a song (`redocean`), and x86 reached mode select with no warning.
- CI: Windows x86, Linux x64 gcc and clang, and Linux x86 pass.

---

## v0.0.69 (2026-10-10)

### 한국어

인자 없이 실행하면 프로필을 고르고 옵션을 정해 게임을 실행하는 런처가 뜹니다(#12).

#### 해결된 이슈

- [#12](https://github.com/reexec/re2DJ/issues/12) 인자 없이 실행했을 때 띄우는 런처 — PR [#13](https://github.com/reexec/re2DJ/pull/13)

#### 1. 런처 (#12)
- rePIU 런처(설계 500·502)를 옮겼습니다. SDL3 창과 OpenGL 2.1 컨텍스트 안에 Dear ImGui(SDL3·OpenGL3 backend)로 그립니다. 키보드·마우스·게임패드로 조작합니다.
- 내장 프로필마다 명령줄 shortcut과 같은 기본 경로 규칙으로 `Ready`·`No CHD`·`Several CHDs`·`No directory`와 사유를 보여 줍니다. CHD 판정 `FindChdImage`는 `hdd/chd_image_locator`로 옮겨 명령줄과 함께 씁니다.
- 고른 프로필은 자기 실행 파일을 새 프로세스로 실행합니다(Linux `posix_spawn("/proc/self/exe")`, Windows `CreateProcessA`). 런처가 올린 GPU 드라이버가 게스트 주소 공간을 차지하지 않게 하고, Windows x86의 시작 시 주소 예약도 그대로 지킵니다. 게임 중에는 런처 창과 GL 컨텍스트를 닫고, 끝나면 다시 열어 직전 종료 코드를 보여 줍니다.
- 설정은 `cfg/re2dj.ini`의 `[Launcher] last_profile`, `[Video] fullscreen`·`color_depth`·`post_shader`, `[Audio] gain_db`입니다. 고른 값만 `--fullscreen`/`--windowed`, `--color-depth`, `--post-shader=`, `--audio-gain-db` 옵션으로 넘기므로 고르지 않은 옵션은 기본값과 우선순위가 그대로입니다. 잘못된 값은 경고 후 무시합니다.
- 인자가 있으면 지금과 같습니다. `RE2DJ_LAUNCHER=0`이거나 창을 열 수 없으면(디스플레이 없음, GL 실패) 경고 뒤 사용법을 출력합니다.

#### 2. 검증
- 단위 테스트 `launcher_test`: CHD 판정 다섯 경우, 카탈로그 상태와 사유, 설정 파싱·왕복·잘못된 값, 자식 인자 순서. Linux x64·x86 Debug(경고를 오류로)·Release, clang: CTest 각 5개, 단위 테스트 6,268건 실패 0.
- 실제 실행(Linux x64·x86, Wayland, RTX 4090): 8개 프로필 `Ready`, 6th 실행 뒤 런처 복귀(종료 코드 0), 32비트·`scanline`·−6dB가 자식 로그에 적용되고 설정 파일에 저장, 마지막 프로필에서 커서 시작. `RE2DJ_LAUNCHER=0`, `SDL_VIDEO_DRIVER=dummy`, x86 + NVIDIA 네이티브 Wayland 기본 설정(작업 459의 `egl-wayland2` 문제)에서 사용법 출력.
- CI: Windows x86, Linux x64 gcc·clang, Linux x86 통과. Windows 실제 실행은 아직 확인 전입니다.

### English

A bare run opens a launcher that picks a profile and options and runs the game (#12).

#### Resolved issues

- [#12](https://github.com/reexec/re2DJ/issues/12) A launcher shown when run without arguments — PR [#13](https://github.com/reexec/re2DJ/pull/13)

#### 1. The launcher (#12)
- A port of rePIU's launcher (designs 500 and 502), drawn with Dear ImGui (SDL3 and OpenGL3 backends) in an SDL3 window with an OpenGL 2.1 context, driven by keyboard, mouse and gamepads.
- Each built-in profile shows `Ready`, `No CHD`, `Several CHDs` or `No directory` and the reason, judged by the command-line shortcut's default-path rules; the CHD rule `FindChdImage` moved to `hdd/chd_image_locator` for both to share.
- The chosen profile runs as a new process of the program's own executable (Linux `posix_spawn("/proc/self/exe")`, Windows `CreateProcessA`), so the GPU driver the launcher loaded never takes guest address space and the Windows x86 start-up reservation still holds. During the game the launcher's window and GL context are closed; afterwards they reopen with the last exit code.
- Settings live in `cfg/re2dj.ini`: `[Launcher] last_profile`, `[Video] fullscreen`, `color_depth` and `post_shader`, `[Audio] gain_db`. Only chosen values are passed, as `--fullscreen`/`--windowed`, `--color-depth`, `--post-shader=` and `--audio-gain-db`, so unchosen options keep their defaults and precedence; bad values are warned about and ignored.
- With arguments nothing changes. With `RE2DJ_LAUNCHER=0`, or when no window can open (no display, a GL failure), a warning and the usage are printed.

#### 2. Verification
- Unit test `launcher_test`: five CHD lookups, catalog states and reasons, settings parsing, round trip and bad values, and the child argument order. Linux x64 and x86 Debug (warnings as errors), Release and clang: 5 CTest tests each, 6,268 unit checks, 0 failures.
- Real runs (Linux x64 and x86, Wayland, RTX 4090): eight profiles `Ready`, 6th run and the launcher back afterwards (exit code 0), 32-bit, `scanline` and −6 dB applied in the child's logs and saved, the cursor starting on the last profile; `RE2DJ_LAUNCHER=0`, `SDL_VIDEO_DRIVER=dummy` and x86 with NVIDIA on native Wayland by default (task 459's `egl-wayland2` problem) print the usage.
- CI: Windows x86, Linux x64 gcc and clang, and Linux x86 pass. A real Windows run is still to be checked.

---

## v0.0.68 (2026-10-09)

### 한국어

게임이 나눠 읽는 CHD 파일을 백그라운드에서 미리 읽어, HDD에서 6th를 플레이할 때의 멈칫거림과 배경음 끊김을 없앴습니다(#9).

#### 해결된 이슈

- [#9](https://github.com/reexec/re2DJ/issues/9) CHD 파일 백그라운드 미리 읽기로 플레이 중 멈춤 줄이기 — PR [#10](https://github.com/reexec/re2DJ/pull/10)

#### 1. 원인 (#9)
- 6th는 소리 스레드 없이 메인 스레드가 매 프레임 배경음 버퍼(약 2.04초)를 보고, 약 133ms마다 `.ezw`에서 22,528바이트를 `ReadFile`로 읽어 채웁니다(재생 위치보다 약 1.5초 앞섬). 한 곡 플레이 70초 동안의 파일 접근은 이 배경음 읽기 532번뿐이었습니다.
- 4KB LZMA hunk의 CHD를 HDD에서 처음 읽는 조각마다 `chd_read`가 디스크를 기다려, 메인 스레드가 25~482ms씩 멈췄습니다. 배경음 출력(SDL 스트림)에는 덮어쓰기나 공백이 없었습니다.

#### 2. CHD 파일 미리 읽기 (#9)
- `hle::GuestFilePrefetcher`: 작업 스레드 하나가 파일을 처음부터 16KiB씩 메모리에 읽고 채운 길이를 atomic으로 공개합니다. 열린 작업의 메모리는 합쳐서 256MiB까지입니다.
- `GuestFiles`: 읽기 전용으로 연 이미지 파일의 첫 `Read` 뒤 256KiB 이상 남으면 시작하고, `Read`는 미리 읽은 범위를 먼저 쓰며, `CloseHandle`이 취소합니다. 한 번에 다 읽는 파일은 미리 읽지 않습니다.
- 게스트의 source 호출은 `ForegroundSource` wrapper를 거치고, 그동안 작업 스레드는 다음 조각을 시작하지 않습니다. 첫 실행에서 작업 스레드가 CHD 잠금을 조각마다 다시 잡아 게임의 4KB 읽기가 273ms를 기다린 것을 막습니다.
- CHD handle은 하나만 씁니다(6th CHD의 hunk map이 handle마다 약 60MB). Windows·Linux 공용 코드입니다.

#### 3. 검증
- 단위 테스트 `guest_file_prefetcher_test`: 나눠 읽기, 한 번에 읽는 파일·작은 파일 제외, 미리 읽는 중의 직접 읽기, `Close` 취소, 메모리 한도와 실패, 게스트 우선, 붙잡힌 상태의 종료.
- Windows x86 Debug: 단위 테스트 6193건 실패 0건, 30번 반복 실패 없음. WSL Linux x64·x86 Debug: CTest 각 5개 통과.
- 실제 실행(Windows Release, CHD가 HDD): SpaceMix 플레이 약 1분 40초 동안 25ms를 넘는 `ReadFile`이 12번(최대 481.9ms)에서 0번으로, 40ms를 넘는 프레임이 5번(최대 485.3ms)에서 0번으로 줄었습니다. 어트랙트 35분 실행에서도 곡 재생 중 느린 읽기가 없었고, 사용자가 SpaceMix 세 곡을 플레이해 멈칫거림·끊김이 없음을 확인했습니다.
- 남은 것: 곡 로딩·화면 전환은 처음 읽는 작은 파일마다 HDD 접근이 쌓여 캐시 없이 1.1~2.9초 멈출 수 있습니다([TODO](docs/TODO.md)).
- 개발 기록: [배경음이 끊기던 이유](docs/post/2026-10-09-000009-chd-file-prefetch-wip.md).

#### 4. 문서
- `docs/TODO.md`에서 완료됐거나 in-process 러너로 대상이 사라진 항목을 `docs/IMPLEMENTED.md`로 옮겼고, Windows in-process 러너에서 게임패드 입력이 게임까지 동작함을 재확인해 작업 445 로그에 남겼습니다.

### English

CHD files the game reads in pieces are read ahead in the background, ending the stutter and music break-up of playing 6th from an HDD (#9).

#### Resolved issues

- [#9](https://github.com/reexec/re2DJ/issues/9) Prefetch CHD files in the background to avoid stalls during play — PR [#10](https://github.com/reexec/re2DJ/pull/10)

#### 1. The cause (#9)
- 6th has no sound thread: its main thread checks the music buffer (about 2.04 s) every frame and refills it about every 133 ms with 22,528 bytes read from an `.ezw` through `ReadFile`, about 1.5 s ahead of the play cursor. Over 70 s of one song, those 532 music reads were the only file access.
- Each piece of the CHD (4 KB LZMA hunks) read for the first time from the HDD had `chd_read` wait on the disk, stalling the main thread for 25 to 482 ms. The music output (the SDL stream) showed no overwrite or gap.

#### 2. Prefetching CHD files (#9)
- `hle::GuestFilePrefetcher`: one worker thread reads a file from the start into memory 16 KiB at a time and publishes the filled length atomically; open jobs hold at most 256 MiB together.
- `GuestFiles`: starts when at least 256 KiB of an image file opened read-only remain after its first `Read`, serves `Read` from the prefetched range first, and cancels on `CloseHandle`. Files read whole at once are not prefetched.
- The guest's source calls go through the `ForegroundSource` wrapper, and the worker starts no piece meanwhile; this stops what the first run showed, the worker retaking the CHD lock piece after piece while a 4 KB game read waited 273 ms.
- One CHD handle is used (6th's CHD hunk map costs about 60 MB per handle). Windows and Linux share the code.

#### 3. Verification
- Unit test `guest_file_prefetcher_test`: pieced reads, no prefetch for whole or small files, direct reads during a prefetch, cancel on `Close`, the memory budget and failure, the guest going first, and shutdown while held.
- Windows x86 Debug: 6193 unit checks, 0 failures, 30 repeated runs without a failure. WSL Linux x64 and x86 Debug: 5 CTest tests each pass.
- Real runs (Windows Release, CHD on an HDD): over about 1 min 40 s of SpaceMix play, `ReadFile`s over 25 ms went from 12 (up to 481.9 ms) to 0 and frames over 40 ms from 5 (up to 485.3 ms) to 0. A 35-minute attract run had no slow read during a song either, and the user played three SpaceMix songs with no stutter or break-up.
- Left: song loading and screen transitions can still pause 1.1 to 2.9 s when cold, each small file read for the first time adding HDD access ([TODO](docs/TODO.md)).
- Dev log: [Why the Music Broke Up](docs/post/2026-10-09-000009-chd-file-prefetch-wip.md).

#### 4. Documents
- Items in `docs/TODO.md` that were finished, or whose target went away with the in-process runner, moved to `docs/IMPLEMENTED.md`, and gamepad input reaching the game on the Windows in-process runner was rechecked and recorded in task 445's log.

---

## v0.0.67 (2026-10-06)

### 한국어

OSD의 프로세스 정보 아래에 OpenGL renderer를 표시합니다(#6).

#### 해결된 이슈

- [#6](https://github.com/reexec/re2DJ/issues/6) OSD에 OpenGL renderer 표시 — PR [#7](https://github.com/reexec/re2DJ/pull/7)

#### 1. OSD의 Renderer 절 (#6)
- rePIU OSD(rePIU #5)처럼 프로세스 정보 줄 바로 아래 "Renderer" 구분선에 `GL_RENDERER`, `Vendor`(`GL_VENDOR`), `OpenGL`(`GL_VERSION`), `Video driver`(`SDL_GetCurrentVideoDriver()`)를 표시합니다.
- `IsSoftwareGlRenderer`가 llvmpipe·softpipe·swrast·GDI Generic·WARP(Microsoft Basic Render Driver)·SwiftShader를 소프트웨어 렌더러로 판정하면 경고 색과 "Software rendering: no 3D acceleration" 문구로 구분합니다.
- `Sdl3OpenGlBackend`가 GL context를 만든 직후 값을 GL·SDL 없는 `graphics::GlRendererIdentity`에 담고, `SdlHostPresentation`이 OSD에 넘기며 `presentation: GL renderer: …` 로그를 남깁니다. `glGetString`도 다른 GL 진입점처럼 `SDL_GL_GetProcAddress`로 얻습니다.

#### 2. 검증
- 단위 테스트 `gl_renderer_identity_test`: 소프트웨어 이름(대문자 변형 포함)은 true, NVIDIA·Intel Arc·AMD·`D3D12 (NVIDIA …)`·빈 문자열·`unknown`은 false, 빈 값의 `unknown` 대체.
- Windows x86 Debug: 단위 테스트 6158건 실패 0건. 6th 실행에서 로그 `GL renderer: NVIDIA GeForce RTX 4090/PCIe/SSE2 | vendor: NVIDIA Corporation | version: 4.6.0 NVIDIA 616.56 | video driver: windows`와 OSD 캡처를 확인했습니다.
- Linux x64 Debug(WSL): 단위 테스트 6155건 실패 0건.
- CI(브랜치 push): `windows-x86`, `linux-x86`, `linux-x64` gcc·clang.

### English

The OSD shows the OpenGL renderer below the process information (#6).

#### Resolved issues

- [#6](https://github.com/reexec/re2DJ/issues/6) Show the OpenGL renderer in the OSD — PR [#7](https://github.com/reexec/re2DJ/pull/7)

#### 1. The OSD's Renderer section (#6)
- As in rePIU's OSD (rePIU #5), a "Renderer" separator right below the process-information lines shows `GL_RENDERER`, `Vendor` (`GL_VENDOR`), `OpenGL` (`GL_VERSION`) and `Video driver` (`SDL_GetCurrentVideoDriver()`).
- When `IsSoftwareGlRenderer` judges it a software rasterizer — llvmpipe, softpipe, swrast, GDI Generic, WARP (Microsoft Basic Render Driver) or SwiftShader — it is set apart in a warning colour with "Software rendering: no 3D acceleration".
- `Sdl3OpenGlBackend` puts the values into the GL- and SDL-free `graphics::GlRendererIdentity` right after creating its GL context, and `SdlHostPresentation` hands them to the OSD and logs a `presentation: GL renderer: …` line. `glGetString` is reached through `SDL_GL_GetProcAddress` like every other GL entry point.

#### 2. Verification
- Unit test `gl_renderer_identity_test`: software names (an upper-case variant included) are true; NVIDIA, Intel Arc, AMD, `D3D12 (NVIDIA …)`, the empty string and `unknown` are false; empty values become `unknown`.
- Windows x86 Debug: 6158 unit checks, 0 failures; a 6th run logs `GL renderer: NVIDIA GeForce RTX 4090/PCIe/SSE2 | vendor: NVIDIA Corporation | version: 4.6.0 NVIDIA 616.56 | video driver: windows`, and the OSD capture was checked.
- Linux x64 Debug (WSL): 6155 unit checks, 0 failures.
- CI (branch push): `windows-x86`, `linux-x86` and `linux-x64` gcc and clang.

---

## v0.0.66 (2026-10-05)

### 한국어

릴리스 노트에 그 버전이 해결한 이슈와 커밋 ID가 보이도록 했습니다(#3). 같은 PC에서 Windows와 Linux x86·x64의 성능을 비교해 정리했습니다(#4).

#### 해결된 이슈

- [#3](https://github.com/reexec/re2DJ/issues/3) 릴리스 노트에 해결된 이슈와 커밋 ID 표시 — PR [#5](https://github.com/reexec/re2DJ/pull/5)
- [#4](https://github.com/reexec/re2DJ/issues/4) Windows와 Linux x86·x64 성능 비교 — PR [#5](https://github.com/reexec/re2DJ/pull/5)

#### 1. 릴리스 노트의 이슈와 커밋 (#3)
- 노트 파일(`docs/release-notes/vX.md`, 이 문서)의 한국어·영어 본문에 "해결된 이슈" 절을 두고, 이슈 `#N`과 PR을 링크로 적습니다.
- 태그를 push하면 `release.yml`이 `scripts/release/release_refs.py`로 직전 태그 이후 커밋의 ID·제목·닫은 이슈·PR 표를 만들어 GitHub Release 본문 끝에 붙입니다. squash 커밋 ID는 머지 뒤에야 생기기 때문입니다. 이슈 제목과 PR을 읽도록 워크플로에 `issues: read`, `pull-requests: read` 권한을 더했습니다.
- v0.0.65 노트에도 #1, PR #2, 커밋 `26a9151`을 넣었습니다.

#### 2. Windows와 Linux 성능 비교 (#4)
- 작업 453과 같은 PC·같은 조건(다섯 타깃, vsync on/off 40초, 2배 창)으로 Linux x86·x64 Release를 쟀습니다. 결과는 `docs/analysis/linux-windows-performance.md`에 있습니다.
- vsync on(기본)에서는 세 빌드 모두 평균 60 fps입니다. CPU는 Linux x64가 코어 하나의 4~13%로 Windows(9~17%)의 절반 안팎입니다.
- vsync off 처리량은 Linux x64가 Windows in-process의 ×0.98~×3.37, Linux x86이 ×0.54~×1.03입니다.
- Linux x86이 느린 주된 이유는 NVIDIA 32비트에서 쓰는 `egl-wayland` v1 표시 경로입니다. X11(`SDL_VIDEO_DRIVER=x11`)로 띄우면 처리량이 1.6~1.7배이고, Linux 빌드 가이드에 이 내용을 적었습니다.

#### 3. 검증
- `release_refs.py`: v0.0.65는 #1·PR #2·`26a9151`, 이슈가 없던 v0.0.64는 "없음"으로 나옵니다. `gh` 인증이 없으면 이슈 번호만 링크합니다. `release.yml`은 YAML과 `bash -n` 검사를 통과했습니다. 실제 Release 본문은 다음 태그 push에서 만들어집니다.
- 성능: x86·x64 × 다섯 타깃 × vsync on/off × 2회(40회)와 표시 경로 대조 9회. 측정용 패치는 되돌리고 다시 빌드했습니다.
- CI(브랜치 push): `windows-x86`, `linux-x86`, `linux-x64` gcc·clang이 모두 성공했습니다.

### English

Release notes now show the issues and commit IDs a version resolves (#3), and the performance of Windows and Linux x86 and x64 on one PC is compared (#4).

#### Resolved issues

- [#3](https://github.com/reexec/re2DJ/issues/3) Show resolved issues and commit IDs in release notes — PR [#5](https://github.com/reexec/re2DJ/pull/5)
- [#4](https://github.com/reexec/re2DJ/issues/4) Compare Windows with Linux x86 and x64 performance — PR [#5](https://github.com/reexec/re2DJ/pull/5)

#### 1. Issues and commits in release notes (#3)
- Note files (`docs/release-notes/vX.md`, this document) carry a "Resolved issues" part in both the Korean and English bodies, linking each issue `#N` and its PR.
- When a tag is pushed, `release.yml` builds a table of the commits since the previous tag — ID, subject, issues closed and PR — with `scripts/release/release_refs.py` and appends it to the GitHub Release body, since a squash commit's ID exists only after the merge. The workflow gains `issues: read` and `pull-requests: read` to read issue titles and PRs.
- The v0.0.65 notes now name #1, PR #2 and commit `26a9151`.

#### 2. Windows and Linux performance (#4)
- Linux x86 and x64 Release were measured on task 453's PC under its conditions (five targets, vsync on and off for 40 s, the 2x window); the results are in `docs/analysis/linux-windows-performance.md`.
- With vsync on (the default) all three builds average 60 fps; Linux x64 uses 4 to 13% of one core, about half of Windows' 9 to 17%.
- With vsync off, Linux x64 reaches ×0.98 to ×3.37 of the Windows in-process runner and Linux x86 ×0.54 to ×1.03.
- Linux x86 is held back mainly by the `egl-wayland` v1 presentation path used for NVIDIA's 32-bit driver; on X11 (`SDL_VIDEO_DRIVER=x11`) it runs 1.6 to 1.7 times faster, as the Linux build guide now says.

#### 3. Verification
- `release_refs.py` gives #1, PR #2 and `26a9151` for v0.0.65 and "none" for v0.0.64, which had no issues, and links issue numbers alone without `gh` authentication; `release.yml` passes YAML and `bash -n` checks, and the real Release body is built at the next tag push.
- Performance: x86 and x64 × five targets × vsync on and off × 2 (40 runs) plus nine presentation-path control runs; the measuring patch was reverted and the builds rebuilt.
- CI (branch push): `windows-x86`, `linux-x86` and `linux-x64` gcc and clang all pass.

---

## v0.0.65 (2026-10-05)

### 한국어

데스크톱 Linux 실기(Ubuntu 26.04, GNOME Wayland, RTX 4090)에서 v0.0.62~v0.0.64를 검증했습니다(작업 458). 그 과정에서 새 컴파일러와 배포판에서만 드러나는 문제 세 가지를 고쳤습니다(작업 459·460). 이번부터 작업은 GitHub 이슈로 관리하고, CI는 모든 브랜치 push에서 돕니다(#1).

#### 해결된 이슈

- [#1](https://github.com/reexec/re2DJ/issues/1) 브랜치 push CI와 GitHub 이슈·PR 기반 작업 규칙 — PR [#2](https://github.com/reexec/re2DJ/pull/2), 커밋 [`26a9151`](https://github.com/reexec/re2DJ/commit/26a9151d57726f898d58bfd34aeb7bf663a4daf0)
- 작업 458·459·460은 이슈 기반 이전에 시작한 작업이라 이슈 번호가 없고, 같은 커밋에 들어 있습니다.

#### 1. Linux 실기 검증 (작업 458)
- 후처리 셰이더와 명령행 10가지 경우, 6th 런처→자식(`--` 처리 포함), 패키지 glibc 가드를 확인했습니다.
- 실물 패드 대신 uinput 가상 Xbox 360 패드를 썼습니다. 연결·분리 인식, 버튼·스틱·십자키·트리거 매핑, 그리고 6th에서 BACK(코인)이 키보드 F5와 같은 코인 효과음을 내는 것까지 확인했습니다.
- 관찰 사항: 창이 가려지거나 최소화되면 GNOME이 프레임을 1 Hz로 줄여 게임도 초당 1프레임으로 느려집니다. 또 코인을 넣은 뒤 START로 타이틀을 넘기지 못했는데, 키보드도 같아 패드 문제는 아닙니다. 두 가지 모두 원인을 확정하지 않았습니다.

#### 2. GCC 15에서 Linux x86 (작업 459)
- GCC 15는 이름 없는 namespace 안의 `extern "C"` 변수를 맹글링합니다. 그래서 GS 선택자 변수를 찾지 못해 x86 링크가 실패했고, 정의를 namespace 밖으로 옮겼습니다.
- import 브리지가 게스트 스택(4바이트 정렬)에서 바로 호스트 코드를 불러, 32비트 Mesa LLVM의 SSE 명령(`movdqa`)에서 SIGSEGV가 났습니다. 이제 호스트 코드를 부르기 전에 스택을 16바이트로 정렬합니다.
- NVIDIA 595의 32비트 `egl-wayland2`에서는 Wayland 창 표면이 만들어지지 않습니다. 드라이버 쪽 문제라, 우회 방법(egl-wayland v1 지정 또는 X11)을 Linux 빌드 가이드에 적었습니다.

#### 3. clang 20 이상 (작업 460)
- spdlog 1.14.1에 들어 있던 fmt 10.2.1이 clang 21에서 컴파일되지 않아, spdlog 1.15.3(fmt 11.2)으로 올렸습니다. fmt 11.2에서 deprecated가 된 `fmt::localtime`은 `std::localtime`으로 바꿨습니다.
- 10월 19일부터 GitHub의 `ubuntu-latest`가 Ubuntu 26(clang 21, GCC 15)으로 바뀌므로, 459·460이 없으면 그날부터 CI가 실패했을 것입니다.

#### 4. 작업 흐름과 CI (#1)
- CI가 `main`만이 아니라 모든 브랜치 push와 수동 실행에서 Windows x86·Linux x64(gcc·clang)·Linux x86을 빌드하고 테스트합니다.
- 작업은 GitHub 이슈(`#N`)로 만들고, `main` 머지는 PR squash merge로 합니다(`AGENTS.md`).

#### 5. 검증
- Ubuntu 26.04: GCC 15.2 x64·x86 Debug(경고를 오류로)와 Release, clang 21.1.8 x64 Debug 빌드가 경고 없이 성공했고, CTest 5개가 모두 통과했습니다. GL probe도 x64·x86 모두 통과했습니다.
- 실제 게임: x64와 x86에서 4th, 6th 런처→자식을 실행해 셰이더 적용과 정상 종료를 확인했습니다.
- CI(브랜치 push): `windows-x86`(MSVC), `linux-x86`(GCC 12), `linux-x64` gcc·clang 18 모두 성공했습니다.

### English

v0.0.62 to v0.0.64 were validated on a desktop Linux machine (Ubuntu 26.04, GNOME Wayland, RTX 4090) (task 458), which turned up three problems seen only with newer compilers and distributions (tasks 459 and 460). From this release tasks are GitHub issues and CI runs on every branch push (#1).

#### Resolved issues

- [#1](https://github.com/reexec/re2DJ/issues/1) Branch-push CI and an issue- and PR-based task workflow — PR [#2](https://github.com/reexec/re2DJ/pull/2), commit [`26a9151`](https://github.com/reexec/re2DJ/commit/26a9151d57726f898d58bfd34aeb7bf663a4daf0)
- Tasks 458, 459 and 460 began before issues and have no issue numbers; they are in the same commit.

#### 1. Linux desktop validation (task 458)
- Ten shader and command-line cases, 6th's launcher → child (including `--`) and the package glibc guard were checked.
- A uinput virtual Xbox 360 pad stood in for a physical one: hot-plug, the button, stick, d-pad and trigger mapping, and on 6th BACK (the coin) playing the same coin sound as the keyboard's F5.
- Observations: a covered or minimised window has its frames cut to 1 Hz by GNOME, slowing the game to one frame a second; START did not get past the title after coins, with the keyboard behaving the same, so not a pad issue; neither cause is settled.

#### 2. Linux x86 with GCC 15 (task 459)
- GCC 15 mangles an `extern "C"` variable inside an unnamed namespace, so the x86 link could not find the GS selector variable; its definition moved out of the namespace.
- The import bridge called host code straight on the guest stack (4-byte aligned), and an SSE instruction (`movdqa`) in 32-bit Mesa's LLVM raised SIGSEGV; the stack is now aligned to 16 bytes before host code.
- NVIDIA 595's 32-bit `egl-wayland2` cannot create Wayland window surfaces; this is a driver matter, and the Linux build guide gives workarounds (pinning egl-wayland v1, or X11).

#### 3. clang 20 and later (task 460)
- fmt 10.2.1, bundled in spdlog 1.14.1, does not compile with clang 21, so spdlog moves to 1.15.3 (fmt 11.2); `fmt::localtime`, deprecated in fmt 11.2, is replaced with `std::localtime`.
- GitHub's `ubuntu-latest` becomes Ubuntu 26 (clang 21, GCC 15) from October 19; without 459 and 460, CI would fail from that day.

#### 4. Workflow and CI (#1)
- CI builds and tests Windows x86, Linux x64 (gcc, clang) and Linux x86 on every branch push and by hand, not only on `main`.
- Tasks are GitHub issues (`#N`), and merges into `main` are PR squash merges (`AGENTS.md`).

#### 5. Verification
- Ubuntu 26.04: GCC 15.2 x64 and x86 Debug (warnings as errors) and Release and clang 21.1.8 x64 Debug build without warnings and pass all 5 CTest tests; the GL probes pass on x64 and x86.
- Real games: 4th and 6th's launcher → child on x64 and x86, with shaders applied and clean exits.
- CI (branch push): `windows-x86` (MSVC), `linux-x86` (GCC 12) and `linux-x64` gcc and clang 18 all pass.

---

## v0.0.64 (2026-10-05)

### 한국어

게임 화면에 CRT 모니터 느낌을 입히는 후처리 셰이더가 생겼습니다(작업 455~457). rePIU v0.0.200·v0.0.201과 같은 형식과 명령행 규칙입니다. CI의 Linux clang 빌드도 고쳤습니다(작업 454).

#### 1. 화면 후처리 셰이더 (작업 455)
- 형식은 libretro 단일 pass GLSL입니다. 내장 `crt`(곡률, 가우시안 빔 주사선, aperture grille 마스크, 비네트)와 `scanline`이 실행 파일에 들어 있고, `shaders/*.glsl` 파일도 목록에 함께 나옵니다.
- 고르는 법: `--post-shader <id>`가 먼저이고, 없으면 `RE2DJ_POST_SHADER`, 둘 다 없으면 `none`입니다. 실행 중에는 백틱 OSD의 Screen shader 메뉴에서 바꾸고, Reload로 다시 읽고, 매개변수 슬라이더로 조절합니다.
- 6th처럼 런처가 자식을 띄우는 타깃은 자식도 같은 셰이더로 열립니다.
- 표시 단계에서만 적용됩니다. 게임이 그린 640x480 화면을 창에 그리는 마지막 단계를 셰이더로 바꾸므로 복사가 없고, 게임이 읽는 화면은 그대로입니다. `none`이면 이전과 GL 호출이 같습니다.
- 4th에서 세 셰이더 모두 약 60fps이고, CPU 차이는 측정 오차 범위입니다.

#### 2. 명령행 규칙 (작업 457)
- rePIU 작업 771과 같게, `--post-shader=<id>` 형식을 쓸 수 있습니다. 값이 없거나 비어 있으면 exit 1이고, 여러 번 주면 마지막 값을 씁니다.
- `--`는 옵션의 끝입니다. 뒤에 오는 인자는 프로파일 id로 읽습니다.

#### 3. 문서와 사이트 (작업 456)
- 셰이더 비교 화면(4th 타이틀·데모 플레이, 6th 타이틀의 `none`·`crt`·`scanline`)을 `docs/screenshots/shaders/`에 두고 README에 넣었습니다.
- 개발 기록 글 두 편을 냈습니다: 후처리 셰이더, Win32를 주입에서 직접 로딩으로 바꾼 과정(작업 446~453).
- 개발 기록 지침은 re2DJ가 그린 화면 캡처를 싣도록 바꿨습니다. 원본 자산은 계속 싣지 않습니다.

#### 4. CI (작업 454)
- 쓰이지 않는 상수 하나가 clang의 `-Wunused-const-variable`(경고를 오류로)에 걸려, 10월 1일부터 `linux-x64 (clang)` 작업이 실패하고 gcc 작업도 취소되고 있었습니다. 상수를 지웠습니다.

#### 5. 검증
- Windows x86 Debug·Release와 WSL Linux x64(clang·gcc)·x86 빌드(경고를 오류로), CTest 통과.
- 새 GL probe `re2dj_opengl_post_shader_probe`가 Windows(RTX 4090)와 WSLg(llvmpipe)에서 12/12. 2배 창에서 `scanline`이 밝은 행과 어두운 행을 번갈아 내고, `crt` 모서리가 검고, 셰이더 뒤에도 게임 그리기가 그대로인지 확인합니다.
- 실제 게임: Windows 4th·6th에서 명령행·환경 변수·사용자 파일·없는 id(경고 후 `none`)·`=` 형식·`--`, Linux 4th에서 `crt`.
- 확인하지 못한 것: OSD에서 마우스로 셰이더를 바꾸고 Reload하고 슬라이더를 움직이는 동작.

---

### English

Post-processing shaders now put a CRT monitor look on the game's picture (tasks 455 to 457), in the format and with the command-line rules of rePIU v0.0.200 and v0.0.201. The Linux clang build in CI is fixed as well (task 454).

#### 1. Post-processing shaders (task 455)
- The format is libretro single-pass GLSL. The built-in `crt` (curvature, Gaussian beam scanlines, an aperture-grille mask, a vignette) and `scanline` are embedded in the executable, and `shaders/*.glsl` files join the list.
- Choosing: `--post-shader <id>` first, then `RE2DJ_POST_SHADER`, then `none`. While running, the backtick OSD's Screen shader menu switches, reloads and adjusts parameters with sliders.
- A target whose launcher starts a child, such as 6th, opens the child with the same shader.
- It applies at presentation only: the last step, drawing the game's 640x480 picture into the window, goes through the shader, so nothing is copied and the picture the game reads is untouched; under `none` the GL calls are the same as before.
- On 4th all three hold about 60 fps, with CPU differences within noise.

#### 2. Command-line rules (task 457)
- As in rePIU task 771, `--post-shader=<id>` works too; a missing or empty value exits 1, and a repeated option takes the last value.
- `--` ends the options; what follows is read as the profile id.

#### 3. Documents and the site (task 456)
- Shader comparison screens (the 4th title and demo play and the 6th title under `none`, `crt` and `scanline`) are in `docs/screenshots/shaders/` and the README.
- Two dev-log posts: post-processing shaders, and moving Win32 from injection to direct loading (tasks 446 to 453).
- The dev-log guideline now allows captures of what re2DJ draws; original assets stay out.

#### 4. CI (task 454)
- An unused constant tripped clang's `-Wunused-const-variable` (warnings as errors), failing the `linux-x64 (clang)` job since October 1 and cancelling the gcc job with it; the constant is gone.

#### 5. Verification
- Windows x86 Debug and Release and WSL Linux x64 (clang, gcc) and x86 builds (warnings as errors) with CTest pass.
- The new GL probe `re2dj_opengl_post_shader_probe` passes 12/12 on Windows (RTX 4090) and WSLg (llvmpipe), checking alternating bright and dark rows under `scanline` in a 2x window, a black `crt` corner, and the game's drawing unaffected after the shader.
- Real games: on Windows, 4th and 6th with the command line, the variable, a user file, an unknown id (a warning, then `none`), the `=` form and `--`; on Linux, 4th with `crt`.
- Not checked: switching, Reload and the sliders with the mouse in the OSD.

---

## v0.0.63 (2026-10-05)

### 한국어

Windows도 Linux와 같은 in-process 러너로 원본 실행 파일을 돌립니다(작업 446~450). 원본을 별도 Windows 프로세스로 띄워 DLL을 주입하던 경로는 지웠습니다.

#### 1. in-process 러너로 통합 (작업 446~449)
- OS 중립 러너를 `src/platform/native/`로 옮기고, 메모리·시계·잠자기는 OS 계약(`native_host_services.h`)으로 분리했습니다(446).
- SDL3 창·키보드·소리 host를 `src/platform/sdl/`로 옮겨 두 OS가 함께 씁니다(447).
- Windows x86 backend(448): 그림자 TEB와 fs:0 SEH 체인 동기화, VEH 기반 fault 배달, MSVC naked asm 게스트 전환.
- Windows CLI 전환(449): `re2dj.exe`가 일시 정지 상태로 자신을 다시 띄워 게스트 이미지 주소 0x400000을 예약하고, 게스트는 16 MiB 스택의 전용 스레드에서 돕니다. 6th 런처의 자식 실행도 같은 방식입니다. `--image-dump`는 `logs/image-dumps/<target>/`에 entry·resumed 두 시점을 씁니다.

#### 2. 주입 경로 제거 (작업 450)
- 주입 runtime DLL, Windows 전용 COM facade·창·OSD 경계, 관련 도구와 테스트(73개 파일)를 지웠습니다. Windows 패키지에는 `re2dj.exe`만 들어갑니다.
- `--demo-volume`, `--audio-volume-trace`, `--guest-wait-trace`, `--vsync`는 이제 알 수 없는 옵션으로 거부됩니다.
- GitHub의 v0.0.61 Windows 패키지는 이 PC에서 Windows Defender가 `re2dj.exe`를 악성으로 판정해 격리했습니다. 주입이 없어져 그 원인으로 보이는 동작이 사라졌지만, 릴리스 패키지가 판정되지 않는지는 아직 확인하지 않았습니다.

#### 3. 개발 도구 (작업 451~452)
- `game-state-hunt`의 `guest_memory.py`가 Windows에서도 명령줄로 `re2dj.exe` 게스트 프로세스를 찾습니다(451).
- Windows x64 호환 모드 조사 probe(452, 제품 밖): 64비트 프로세스 안에서 32비트 코드 진입·복귀와 FS 처리 방식을 확인했습니다.

#### 4. 성능과 스크린샷 (작업 453)
- 주입 경로(v0.0.62)와 비교하면 vsync off 최대 처리량이 4th −17%, 1st SE −22%, 5th −11%, 6th −16%, 2nd MOVE +10%입니다. 기본 설정(vsync on)에서는 다섯 타깃 모두 60fps를 유지하고 CPU는 코어 하나 기준 2~4%p 늘었습니다. 자세한 내용은 [측정 문서](docs/analysis/windows-in-process-performance.md)에 있고, 추가 분석은 TODO로 남겼습니다.
- README와 프로젝트 사이트 소개에 1st SE, 4th, 5th, 6th, 2nd MOVE의 스크린샷을 넣었습니다(`docs/screenshots/`).

#### 5. 검증
- Windows x86 Debug·Release 빌드와 CTest, WSL Linux x64·x86 Debug 빌드와 CTest 통과.
- Windows 실게임: 4th, 1st SE, 5th, 6th(런처 → 자식), 2nd MOVE가 in-process로 실행되고, 성능 측정에서 각 40초씩 vsync on/off로 돌았습니다.
- 확인하지 못한 것: Windows 게임패드 실제 입력, 창을 손으로 닫는 종료와 `--fullscreen`·OSD 조작, 릴리스 workflow가 만든 Windows 패키지.

---

### English

Windows now runs the original executables through the same in-process runner as Linux (tasks 446 to 450); the path that started the original as its own Windows process and injected a DLL is gone.

#### 1. One in-process runner (tasks 446 to 449)
- The OS-neutral runner moved to `src/platform/native/`, with memory, clocks and sleeping behind an OS contract (`native_host_services.h`) (446).
- The SDL3 window, keyboard and sound hosts moved to `src/platform/sdl/`, shared by both OSes (447).
- The Windows x86 backend (448): a shadow TEB kept in step with the fs:0 SEH chain, fault delivery through a VEH, and guest transitions in MSVC naked asm.
- The Windows CLI switch (449): `re2dj.exe` starts itself again suspended to reserve the guest image address 0x400000, and the guest runs on a dedicated thread with a 16 MiB stack; the 6th launcher's child runs the same way. `--image-dump` writes the entry and resumed dumps to `logs/image-dumps/<target>/`.

#### 2. The injection path removed (task 450)
- The injected runtime DLL, the Windows-only COM facades and window and OSD boundaries, and their tools and tests (73 files) are gone; the Windows package holds only `re2dj.exe`.
- `--demo-volume`, `--audio-volume-trace`, `--guest-wait-trace` and `--vsync` are now refused as unknown options.
- On this PC Windows Defender quarantined `re2dj.exe` from the v0.0.61 Windows package on GitHub as malicious. Without injection the behaviour that likely caused it is gone, but whether the release package passes has not been checked yet.

#### 3. Developer tools (tasks 451 and 452)
- `guest_memory.py` of `game-state-hunt` finds the `re2dj.exe` guest process by its command line on Windows too (451).
- A Windows x64 compatibility-mode probe (452, outside the product) confirmed entering and leaving 32-bit code inside a 64-bit process and how FS behaves there.

#### 4. Performance and screenshots (task 453)
- Against the injection path (v0.0.62), peak vsync-off throughput is −17% for 4th, −22% for 1st SE, −11% for 5th, −16% for 6th and +10% for 2nd MOVE. At the default (vsync on) all five targets hold 60 fps and CPU rises by 2 to 4 points of one core. Details are in the [measurement document](docs/analysis/windows-in-process-performance.md); further analysis is left as TODO.
- The README and the project site's introduction show screenshots of 1st SE, 4th, 5th, 6th and 2nd MOVE (`docs/screenshots/`).

#### 5. Verification
- The Windows x86 Debug and Release builds with CTest, and the WSL Linux x64 and x86 Debug builds with CTest, pass.
- Real games on Windows: 4th, 1st SE, 5th, 6th (launcher → child) and 2nd MOVE run in-process, each running 40 s with vsync on and off in the performance measurements.
- Not checked: real gamepad input on Windows, closing the window by hand, `--fullscreen` and the OSD, and the Windows package the release workflow builds.

---

## v0.0.62 (2026-10-04)

### 한국어

Linux host가 게임패드를 받습니다(작업 444). 스팀덱을 겨냥한 첫 단계입니다.

#### 1. 게임패드 입력 (작업 444)
- SDL3가 인식하는 패드는 모두 같은 매핑으로 1P를 칩니다. 기본값: `X` `Y` `B` `A` `RB`가 1~5번 키, `LB` 페달, 왼쪽 스틱 좌우가 턴테이블, `START` 시작, `BACK` 코인, 십자키가 이펙터 1~4. 2P는 비어 있습니다.
- 컨트롤 이름(`A` `B` `X` `Y` `BACK` `GUIDE` `START` `LSTICK` `RSTICK` `LB` `RB` `LT` `RT` `DPAD_*` `PADDLE1`~`4` `LSTICK_*` `RSTICK_*` `NONE`)과 바인딩 로더는 host 중립이며, 예제 INI 두 개에 `[gamepad]` 섹션이 생겼습니다. 스틱·트리거는 절반 이상에서 눌림입니다.
- `--io-config`가 Linux에서도 적용됩니다(키보드·게임패드·`step`). 런처의 자식(6th)에도 전달됩니다.
- SDL joystick·HIDAPI를 켰습니다. 릴리스 실행 파일의 NEEDED는 그대로(`libm` `libc` `ld-linux`)이며 libudev는 실행 시 `dlopen`합니다.

#### 2. Windows host (작업 445, 원복)
- Windows 연결을 시도했으나 실제 패드 눌림이 게임에 닿지 않아 원복했습니다. Windows는 이전처럼 키보드만 받습니다. 기록은 작업 445 문서에 있습니다.

#### 3. 검증
- Linux x64 CTest 5개(새 `re2dj_sdl3_gamepad_test`는 SDL 가상 조이스틱으로 검사), 6th 실행에서 `io config` 적용과 `gamepads ready` 로그, Windows x86 빌드·테스트 통과. 실제 패드로 Linux에서 치는 확인은 아직입니다.

---

### English

The Linux host reads gamepads (task 444), the first step toward the Steam Deck.

#### 1. Gamepad input (task 444)
- Every pad SDL3 recognises plays player 1 under one mapping. Defaults: `X` `Y` `B` `A` `RB` are keys 1 to 5, `LB` the pedal, the left stick's left and right the turntable, `START` start, `BACK` coin, the d-pad effectors 1 to 4; player 2 is unbound.
- The control names (`A` `B` `X` `Y` `BACK` `GUIDE` `START` `LSTICK` `RSTICK` `LB` `RB` `LT` `RT` `DPAD_*` `PADDLE1` to `4` `LSTICK_*` `RSTICK_*` `NONE`) and the bindings loader are host-neutral, and both example INIs gain a `[gamepad]` section. Sticks and triggers count as pressed past half their travel.
- `--io-config` now applies on Linux too (keys, gamepad and `step`), and the launcher's child (6th) inherits it.
- SDL joystick and HIDAPI are on. The release executable's NEEDED is unchanged (`libm` `libc` `ld-linux`); libudev is `dlopen`ed at run time.

#### 2. The Windows host (task 445, reverted)
- A Windows wiring was tried, but real pad presses never reached the game, so it was reverted; Windows takes the keyboard only, as before. The record is in the task 445 documents.

#### 3. Verification
- Five Linux x64 CTest tests (the new `re2dj_sdl3_gamepad_test` drives an SDL virtual joystick), a 6th run logging the applied `io config` and `gamepads ready`, and the Windows x86 build and tests. Playing on Linux with a real pad is still to be checked.

---

## v0.0.61 (2026-10-03)

### 한국어

Linux에서 Remember 1st를 마치고 6th로 돌아올 때 re2dj가 끝날 수 있던 문제를 고쳤습니다(작업 443).

#### 1. 자식 run은 실행 중인 그 실행 파일로 (작업 443)
- Linux launcher는 자식마다 `readlink("/proc/self/exe")`로 얻은 경로를 실행했습니다. 실행 도중 실행 파일이 다시 빌드되어 교체되자 그 경로가 `(deleted)`가 되어, 1st 뒤 6th를 다시 띄우는 `CreateProcessA`가 실패했습니다.
- 이제 `/proc/self/exe` 자체를 실행합니다. 파일이 교체되거나 지워져도 자식이 뜨고, 부모와 자식은 같은 빌드입니다.

#### 2. 확인된 동작
- v0.0.60 태그의 Release workflow가 성공해, 처음으로 Windows x86·Linux x64·Linux x86 패키지가 게시됐습니다.
- Linux에서 6th → Remember 1st → 게임 한 판 → 6th 복귀를 사용자가 확인했습니다.

#### 3. 검증
- 실행 파일을 지운 재현에서 6th 자식이 뜹니다. Linux x64 CTest 4개가 통과합니다.

---

### English

Fixed re2dj sometimes ending on Linux when returning to 6th after Remember 1st (task 443).

#### 1. A child run from the very executable running (task 443)
- The Linux launcher ran each child from the path `readlink("/proc/self/exe")` gave. When the executable was rebuilt and replaced mid-run that path read `(deleted)`, and the `CreateProcessA` starting 6th again after 1st failed.
- It now runs `/proc/self/exe` itself, so a child starts even after the file is replaced or removed, and parent and child are the same build.

#### 2. Observed behaviour
- The Release workflow on the v0.0.60 tag succeeded and published the Windows x86, Linux x64 and Linux x86 packages for the first time.
- The user confirmed 6th → Remember 1st → one game → back to 6th on Linux.

#### 3. Validation
- With the executable deleted, the reproduction starts the 6th child; all 4 Linux x64 CTest tests pass.

---

## v0.0.60 (2026-10-03)

### 한국어

Linux x86-64·x86 패키지를 릴리스마다 배포합니다(작업 442). EZ2DJ 6th의 Remember 1st가 Linux에서 처음부터 끝까지 동작하고(작업 434, 437~440), 6th에서 Autoplay를 쓸 수 있습니다(작업 436). v0.0.59 이후 main에 들어간 프로젝트 사이트와 CREDITS(작업 432·433)도 이 버전에 포함됩니다.

#### 1. Linux 릴리스 패키지 (작업 442)
- Release workflow를 version, Windows x86, Linux matrix, publish job으로 나눴습니다. 세 플랫폼이 모두 성공해야 태그 release를 한 번 게시합니다.
- Linux는 Debian 12 컨테이너(x86은 `i386/debian`)에서 libstdc++와 libgcc를 정적 링크해 빌드합니다. `scripts/package_release.sh`가 실행 파일 폭, C 런타임만 링크하는지, glibc 2.36 한도를 검사하고 tar.gz를 만듭니다.
- 처음으로 workflow 전체가 통과하기까지 다음을 고쳤습니다.
  - GCC 12의 libstdc++ 오탐 경고는 GCC 12에서만 오류로 보지 않습니다.
  - Windows job은 runner에 없는 `Visual Studio 18 2026` preset 대신 기본 생성기로 configure합니다.
  - Windows VFS probe가 열린 trace 파일을 지우다 예외로 죽던 정리 순서를 고쳤습니다.
- CI에 linux-x86 job과 오디오 헤더를 더했습니다. 사이트 다운로드 페이지는 플랫폼별 패키지를 보여 줍니다. Windows zip에도 `THIRD_PARTY_NOTICES.md`와 `CREDITS.md`를 넣었습니다.

#### 2. 6th의 Remember 1st (작업 434, 437~440)
- 프로필이 launcher의 자식 실행 파일 목록을 갖습니다. 자식 run은 launcher의 guest root를 그대로 씁니다. Windows launcher probe는 자식을 차례로 따라갑니다.
- 6th는 0x100으로 끝나기 전에 1st의 `bookkeeping.ini`에 크레딧을 써서 넘깁니다(`FreePlay` 쓰기, `DeleteFileA`, `Coins`·`PlayCoins`·`ContinueCoins`·`GameLevel` 쓰기).
  - Linux에 `DeleteFileA`를 더했습니다. 원본 이미지 파일을 지우면 overlay의 `.re2dj-deleted`에 표시합니다.
  - overlay 경로는 NTFS처럼 대소문자를 구분하지 않습니다.
- 1st는 게임 한 판 뒤 다음 순서로 끝나고, launcher가 6th를 다시 실행합니다: 정리 → `IDirectDraw4::RestoreDisplayMode` → `WM_DESTROY` → `PostQuitMessage` → `WM_QUIT` → 종료 코드 0x105. 이 경로에 필요한 `RestoreDisplayMode`, `PostQuitMessage`, `PeekMessageA`의 `WM_QUIT`, `DefWindowProcA(WM_DESTROY)`를 더했습니다.
- 게스트 스레드를 남긴 Linux run이 끝날 때 정적 소멸 중 SIGSEGV로 죽던 문제를 고쳤습니다. host 서비스를 `main` 안에서 해제합니다.

#### 3. 6th Autoplay (작업 436)
- `EZ2DJ6th.EXE`(`0x411f6d44`)의 autoplay 플래그는 `[0x008896ac]`입니다. 5th와 같은 구조입니다.
- 동봉 1st(`0x411bbf5c`)는 데모 전용 장면을 써서, 켜고 끌 변수가 없습니다.
- 프로필의 `game_controls`는 빌드별 목록이 되어, 실행 중인 실행 파일의 선언만 무장합니다. Linux OSD에 Autoplay 토글을 더했고, Windows launcher 자식에도 OSD 정보와 주소를 넘깁니다.

#### 4. 그 밖에
- Wayland가 창 위치 지정을 거절해도 Linux 실행이 멈추지 않습니다(작업 435).
- EZ2DJ 턴테이블 `step` 기본값이 예제 INI와 같은 2입니다(작업 441).
- 프로젝트 사이트와 CREDITS 페이지를 추가했습니다(작업 432·433).

#### 5. 검증
- Release workflow에서 Windows x86, Linux x86-64, Linux x86의 Release 빌드·테스트·패키징이 통과합니다. 단위 테스트 5,934건(Linux x64)이 통과합니다.
- CI가 만든 Linux 두 패키지로 6th를 실행했습니다. 사용자가 Linux에서 6th → Remember 1st → 6th 복귀와 6th Autoplay를 확인했습니다.

---

### English

Linux x86-64 and x86 packages ship with each release (task 442). EZ2DJ 6th's Remember 1st works end to end on Linux (tasks 434, 437 to 440), and 6th has Autoplay (task 436). The project site and CREDITS that reached main after v0.0.59 (tasks 432, 433) are part of this version.

#### 1. Linux release packages (task 442)
- The Release workflow splits into version, Windows x86, a Linux matrix and publish jobs, publishing a tag's release once all three platforms succeed.
- Linux builds in Debian 12 containers (`i386/debian` for x86) with libstdc++ and libgcc linked statically. `scripts/package_release.sh` checks the executable's width, that it links only the C runtime and the glibc 2.36 limit, then makes the tar.gz.
- Getting the whole workflow to pass for the first time also fixed the following:
  - GCC 12's libstdc++ false-positive warnings are not errors on GCC 12.
  - The Windows job configures with the default generator rather than the `Visual Studio 18 2026` preset the runner lacks.
  - The Windows VFS probe's cleanup no longer dies on an exception while removing an open trace file.
- CI gains a linux-x86 job and the audio headers, the site's download page lists per-platform packages, and the Windows zip carries `THIRD_PARTY_NOTICES.md` and `CREDITS.md`.

#### 2. 6th's Remember 1st (tasks 434, 437 to 440)
- A profile lists its launcher's child executables, a child run keeps the launcher's guest root, and the Windows launcher probe follows the children in turn.
- Before ending with 0x100, 6th hands its credits to 1st's `bookkeeping.ini`: it writes `FreePlay`, calls `DeleteFileA`, then writes `Coins`, `PlayCoins`, `ContinueCoins` and `GameLevel`.
  - Linux gains `DeleteFileA`; an original image file it deletes is marked in the overlay's `.re2dj-deleted`.
  - Overlay paths ignore case as NTFS does.
- After one game 1st ends in this order, and the launcher runs 6th again: clean-up → `IDirectDraw4::RestoreDisplayMode` → `WM_DESTROY` → `PostQuitMessage` → `WM_QUIT` → exit code 0x105. `RestoreDisplayMode`, `PostQuitMessage`, `WM_QUIT` from `PeekMessageA` and `DefWindowProcA(WM_DESTROY)` were added for it.
- A Linux run that left a guest thread behind no longer dies with SIGSEGV during static destruction; the host services are released inside `main`.

#### 3. 6th Autoplay (task 436)
- `EZ2DJ6th.EXE` (`0x411f6d44`) keeps its autoplay flag at `[0x008896ac]`, with 5th's structure.
- The bundled 1st (`0x411bbf5c`) plays its demo in dedicated scenes and has no switchable variable.
- A profile's `game_controls` became a per-build list, so only the running executable's declaration is armed. The Linux OSD gains the Autoplay toggle, and Windows launcher children receive the OSD information and address.

#### 4. Also
- A Linux run no longer stops when Wayland refuses to place the window (task 435).
- The EZ2DJ turntable `step` defaults to 2, as in the example INI (task 441).
- The project site and its credits page were added (tasks 432, 433).

#### 5. Validation
- The Release workflow passes the Release build, tests and packaging for Windows x86, Linux x86-64 and Linux x86, and all 5,934 unit checks pass (Linux x64).
- Both CI-built Linux packages ran 6th, and the user confirmed on Linux 6th → Remember 1st → back to 6th, and 6th's Autoplay.

---

## v0.0.59 (2026-09-30)

### 한국어

EZ2DJ 6th가 Linux x86·x64에서 launcher의 자식 프로세스를 거쳐 실행됩니다(작업 431). 6th의 모드 선택 화면에 빠져 있던 모드별 그림도 두 host에서 나옵니다(작업 430).

#### 1. Direct3D 조명과 깊이 기본값 (작업 430)
- 6th 모드 선택은 모드별 로고를 조명이 켜진 `D3DVERTEX`로 그리면서 `ZFUNC`를 설정하지 않았습니다. 그래서 core의 초기 비교 함수 0 때문에 그리기가 모두 실패했습니다.
- 새 장치의 render state를 Windows 11 측정값으로 맞췄습니다(`ZWRITEENABLE` 켬, `ZFUNC` LESSEQUAL, `ALPHAFUNC` ALWAYS). Direct3D 7 장치는 조명이 켜진 상태로 시작합니다.
- 광원이 없을 때 `D3DVERTEX`의 색은 emissive + ambient × 재질 ambient와 diffuse alpha입니다. 측정대로 자르고 반올림합니다.
- Windows DX7 facade가 `SetMaterial`을 보관하고 `GetMaterial`에 답합니다. Linux에는 `GetMaterial`을 더했습니다.

#### 2. Linux 6th와 자식 프로세스 (작업 431)
- 6th 프로파일의 실행 파일은 launcher입니다. launcher는 `CreateProcessA`로 `EZ2DJ6th.EXE`를 실행하며 `STARTUPINFO.lpReserved2`로 숫자를 넘기고, 자식의 종료 코드를 기다립니다.
- 게스트 이미지는 모두 같은 base에 적재되므로, Linux에서는 자식을 별도 host 프로세스로 실행합니다. 공용 `HostProcessLauncher`가 부모의 옵션과 자식의 실행 파일, 명령줄, 현재 디렉터리, reserved 바이트로 `/proc/self/exe`를 시작하고, 자식은 32비트 종료 코드를 pipe로 돌려줍니다.
- `CreateProcessA`, `GetExitCodeProcess`, `SetPriorityClass`, 자식 핸들의 wait와 `CloseHandle`, `GetStartupInfoA`의 reserved 바이트, `GetKeyState`, `GetFullPathNameA`, 32비트 `StretchDIBits`를 Windows 11 측정대로 구현했습니다.
- 실행이 멈춘 호출은 API 로그 한도를 넘어서도 사유와 함께 기록합니다.

#### 3. 확인된 동작
- Win32 6th: 모드 선택 화면에 Ruby Mix, Remember 1st, Street Mix 로고가 나옵니다. Windows 4th·5th 타이틀도 정상입니다.
- Linux x64: 6th가 타이틀, 코인 투입, 모드 선택, Ruby Mix 소개 화면까지 진행하고 시간 제한까지 멈추지 않습니다. 창을 닫으면 launcher가 `ExitProcess(0)`으로 끝납니다.
- Linux x86: 모드 선택 화면까지 나오고 시간 제한까지 멈추지 않습니다.

#### 4. 남은 일
- 6th 데모 플레이의 BGA 자리에 보이는 색 노이즈가 원본 연출인지 확인하지 못했습니다.
- Linux x86 Debug는 모드 선택 전환 중 4.5 FPS까지 떨어집니다.
- 첫 코인 키는 창에 초점이 옮겨지는 동안 빠질 수 있습니다.

#### 5. 검증
- Windows x86 CTest 6개와 Linux x64·x86 CTest 4개가 통과합니다(단위 5,773 / 5,770 checks).

---

### English

EZ2DJ 6th runs on Linux x86 and x64 through its launcher's child process (task 431), and the per-mode pictures missing from 6th's mode select now show on both hosts (task 430).

#### 1. Direct3D lighting and depth defaults (task 430)
- 6th's mode select draws its per-mode logos as lit `D3DVERTEX` geometry without setting `ZFUNC`, so every draw failed on the core's initial comparison of 0.
- A new device's render states now match Windows 11 measurements (`ZWRITEENABLE` on, `ZFUNC` LESSEQUAL, `ALPHAFUNC` ALWAYS), and a Direct3D 7 device starts lit.
- With no light, a `D3DVERTEX` takes emissive + ambient × material ambient and the diffuse alpha, clamped and rounded as measured.
- The Windows DX7 facade keeps `SetMaterial` and answers `GetMaterial`; Linux gains `GetMaterial`.

#### 2. Linux 6th and child processes (task 431)
- 6th's profile executable is a launcher that starts `EZ2DJ6th.EXE` with `CreateProcessA`, passing a number in `STARTUPINFO.lpReserved2`, then waits for its exit code.
- Every guest image loads at the same base, so on Linux the child runs as another host process: the shared `HostProcessLauncher` starts `/proc/self/exe` with the parent's options and the child's executable, command line, current directory, and reserved bytes, and the child writes its 32-bit exit code back over a pipe.
- `CreateProcessA`, `GetExitCodeProcess`, `SetPriorityClass`, waits and `CloseHandle` on the child's handles, `GetStartupInfoA`'s reserved bytes, `GetKeyState`, `GetFullPathNameA`, and 32-bit `StretchDIBits` follow Windows 11 measurements.
- The call a run stops on is logged with its reason past the API log's limit.

#### 3. Observed behaviour
- Win32 6th: mode select shows the Ruby Mix, Remember 1st, and Street Mix logos; the Windows 4th and 5th titles are fine.
- Linux x64: 6th reaches its title, takes coins, enters mode select, and goes on to the Ruby Mix introduction, running until the timeout; closing the window ends the launcher with `ExitProcess(0)`.
- Linux x86: mode select shows, and it runs until the timeout.

#### 4. Follow-ups
- Whether the colour noise in the BGA area of 6th's demo is the original's own effect is unconfirmed.
- Linux x86 Debug drops to 4.5 FPS during the mode-select transition.
- The first coin key can be lost while focus moves to the window.

#### 5. Validation
- All 6 Windows x86 CTest tests and all 4 Linux x64/x86 CTest tests pass (5,773 / 5,770 unit checks).

---

## v0.0.58 (2026-09-30)

### 한국어

32비트 트루컬러 표면 모드를 선택 옵션으로 추가했습니다(작업 429). 게스트는 그대로 16비트 화면과 RGB565 표면을 쓰고, host가 표면마다 32비트 사본을 함께 유지해 더 부드러운 색으로 그립니다. 기본값은 지금과 같은 16비트입니다.

#### 1. 트루컬러 표면 (작업 429)
- **색 사본**
  - 표면마다 host 쪽 XRGB8888 사본을 둡니다.
  - GDI, Blt/BltFast, 색 채우기, Clear, 텍스처 Load, Unlock이 이 사본을 16비트 표면과 함께 갱신합니다.
  - 렌더 타깃은 선택에 따라 `GL_RGB565`와 `GL_RGB8` 사이를 오가며 내용을 옮깁니다. `GL_RGB8`을 쓸 수 없으면 16비트로 남습니다.
- **선택 방법**
  - `--color-depth 16|32`로 고릅니다(기본 16).
  - 실행 중에는 OSD의 "32-bit color" 토글로 바꿉니다.
  - 공용 core(`color_depth`, `true_color`, `ui::AddColorDepthToggle`)를 Windows와 Linux가 함께 씁니다.
- **Linux OSD**: Linux host에도 Windows와 같은 OSD(백틱, 마우스, 정보 줄)가 생겼습니다.
- **Windows**
  - DX6 facade가 32bpp DIB 사본을 씁니다.
  - 그래픽 trace에 선택한 색 깊이와 렌더 타깃 깊이를 기록합니다.

#### 2. 확인된 효과
- Linux 1st SE에서 16비트는 금속 배경과 어두운 장면에 가로 띠와 색 얼룩이 보였습니다. 32비트에서는 부드러운 그라데이션으로 나옵니다.
- 같은 프레임의 한 영역에서 고유 색 수가 16비트 61개에서 32비트 351개로 늘었습니다.

#### 3. 미확정
- 실제 마우스로 OSD 토글을 눌러 실행 중에 전환되는지는 두 host 모두 아직 보지 못했습니다. 실행 중 전환 자체는 실제 GL에서 true-color probe로 확인했습니다.
- Windows 32비트 화면은 작업 당시 데스크톱이 잠겨 있어 캡처하지 못했습니다.

#### 4. 검증
- Windows x86 CTest 6개와 Linux x64·x86 CTest 4개가 통과합니다(단위 5644 / 5641 checks).
- true-color probe는 두 host에서 모두 통과합니다(13 checks).
- Windows blend probe의 "white mask preserves background" 한 검사는 이 변경 전 main에서도 같은 값으로 실패합니다. host 드라이버의 필터링 결과로 추정합니다.

---

### English

An optional 32-bit true-color surface mode is added (task 429). The guest keeps its 16-bit display and RGB565 surfaces; the host keeps a 32-bit copy of each surface alongside and draws from it with smoother colour. The default stays 16-bit, as before.

#### 1. True-color surfaces (task 429)
- **Colour copy**
  - Each surface can carry a host-side XRGB8888 copy.
  - GDI, Blt/BltFast, colour fills, Clear, texture Load, and Unlock keep that copy in step with the 16-bit surface.
  - The render target moves between `GL_RGB565` and `GL_RGB8` with the selection, carrying its contents, and stays 16-bit when `GL_RGB8` is unavailable.
- **Choosing it**
  - `--color-depth 16|32` selects it (default 16).
  - The OSD's "32-bit color" toggle switches it while running.
  - Windows and Linux share the cores (`color_depth`, `true_color`, `ui::AddColorDepthToggle`).
- **Linux OSD**: The Linux host now has the same OSD as Windows (backtick, mouse, information lines).
- **Windows**
  - The DX6 facade keeps a 32bpp DIB copy.
  - The graphics trace records the selected depth and the render target's depth.

#### 2. Observed effect
- On Linux 1st SE, 16 bits showed horizontal bands and colour blotches on metal backgrounds and dark scenes; 32 bits shows smooth gradients.
- In one area of the same frame, the number of distinct colours rose from 61 at 16 bits to 351 at 32 bits.

#### 3. Unresolved
- Whether a real mouse click on the OSD toggle switches depth while running has not been seen on either host yet. The run-time switch itself was confirmed on real GL by the true-color probe.
- The Windows 32-bit picture was not captured, because the desktop was locked during the task.

#### 4. Validation
- All 6 Windows x86 CTest tests and all 4 Linux x64/x86 CTest tests pass (5644 / 5641 unit checks).
- The true-color probe passes on both hosts (13 checks).
- One Windows blend-probe check, "white mask preserves background", fails with the same value on main before this change. It is inferred to be the host driver's filtering result.

---

## v0.0.57 (2026-09-30)

### 한국어

Windows command prompt용 빌드 bat를 Win32 Debug용과 Release용 두 개로 정리했습니다. Release 스크립트가 테스트 단계에서 항상 실패하던 문제도 고쳤습니다.

#### 1. 빌드 bat 정리
- `scripts\build_win32_debug.bat`: Win32 Debug를 configure하고 빌드합니다. 결과물은 `build\windows-x86\bin\Debug`에 생깁니다.
- `scripts\build_win32_release.bat`: Win32 Release를 configure하고 빌드한 뒤 CTest를 실행합니다. `-SkipTests`를 주면 테스트를 건너뜁니다. 결과물은 `build\windows-x86\bin\Release`에 생깁니다.
- 기존 `build_win32.bat`와 `build_release.bat`는 이 두 파일로 대체했습니다. 둘 다 어느 작업 디렉터리에서나 실행할 수 있습니다.

#### 2. 빌드 디렉터리 수정
- `build.ps1`과 `build_release.ps1`은 빌드 디렉터리를 `build\<preset 이름>`으로 가정했습니다. 그런데 `windows-x86-debug` preset은 `build\windows-x86`에 빌드합니다. 그래서 Release CTest 단계가 없는 디렉터리를 찾다가 실패했고, 두 스크립트가 안내하는 결과물 경로도 틀렸습니다.
- 이제 두 스크립트는 `CMakePresets.json`에서 preset의 `binaryDir`을 읽습니다.

#### 3. 검증
- 두 bat를 저장소 밖 디렉터리에서 실행했습니다. Debug 빌드가 성공했고, Release는 빌드와 CTest 6개가 모두 통과했습니다.

---

### English

The Windows command-prompt build batch files are now two: one for Win32 Debug and one for Win32 Release. A bug that always failed the Release script at its test step is fixed.

#### 1. Build batch files
- `scripts\build_win32_debug.bat` configures and builds Win32 Debug into `build\windows-x86\bin\Debug`.
- `scripts\build_win32_release.bat` configures and builds Win32 Release into `build\windows-x86\bin\Release`, then runs CTest. `-SkipTests` skips the tests.
- They replace `build_win32.bat` and `build_release.bat`. Both run from any working directory.

#### 2. Build directory fix
- `build.ps1` and `build_release.ps1` assumed the build directory was `build\<preset name>`, but the `windows-x86-debug` preset builds into `build\windows-x86`. The Release CTest step therefore looked for a directory that did not exist and failed, and both scripts printed the wrong output path.
- Both scripts now read the preset's `binaryDir` from `CMakePresets.json`.

#### 3. Validation
- Both batch files were run from a directory outside the repository. The Debug build succeeded, and Release built and passed all 6 CTest tests.

---

## v0.0.56 (2026-09-29)

### 한국어

EZ2Dancer 2nd MOVE(ez2d2m)가 Linux x86·x64에서 실행됩니다. 타이틀 화면을 거쳐, 코인을 넣으면 곡 선택과 플레이까지 진행합니다. EZ2Dancer 보드의 키 배치는 Windows와 Linux가 함께 쓰는 공용 core로 옮겼습니다. 새 동작은 Windows 11에서 측정한 값을 따릅니다.

#### 1. Linux ez2d2m 실행 (작업 427)
- **창**: `CreateWindowExA`가 `WS_BORDER`를 받습니다. `DefWindowProcA`의 `WM_NCCALCSIZE`는 측정대로 테두리 창의 사각형을 사방 1픽셀씩 줄입니다.
- **예외 보고**: `UnhandledExceptionFilter`가 처리되지 않은 게스트 예외의 코드·주소·인자를 실행 결과에 남기고 멈춥니다.
- **EZ2Dancer 보드**: Linux IO trap이 word 폭 보드에 답합니다. 전에는 word 폭 보드가 설정되면 trap이 꺼졌습니다. 키 배치 표는 공용 `ez2dancer_keyboard_map`으로 옮겨 Windows 입력 코드도 같이 씁니다. 게임은 입력 helper 두 곳(`0xb169`, `0xb4cb`)에서 보드를 읽기 때문에, 읽기는 opcode로 판정합니다.

#### 2. 열리지 않은 COM1 (작업 428)
- ez2d2m은 `COM1`을 열지 못해도 그 무효 핸들로 계속 씁니다. 코인을 넣을 때마다 overlapped `WriteFile`을 부르므로, Linux에서는 코인을 넣는 순간 멈췄습니다.
- 무효 핸들에 대한 다음 호출은 Windows 11 측정대로 실패합니다.
  - overlapped `ReadFile`·`WriteFile`
  - 시리얼 함수 8개(`SetCommState`, `GetCommState`, `SetCommTimeouts`, `PurgeComm`, `SetupComm`, `SetCommMask`, `ClearCommError`, `WaitCommEvent`)
  - `GetOverlappedResult`
- `CloseHandle(INVALID_HANDLE_VALUE)`는 성공합니다.

#### 3. Windows에 영향을 주는 변경
- EZ2Dancer 키 배치를 공용 표에서 읽습니다. 기본 키는 바뀌지 않았습니다.
- 작업 427은 코인이 오르지 않는다고 기록했지만, 키가 전달되지 않아 생긴 오판이었습니다. 코인은 두 host 모두에서 크레딧을 올립니다.

#### 4. 검증
- Windows x86 CTest 6개와 Linux x64·x86 CTest 4개가 통과합니다. 단위 검사는 5529 / 5526 checks입니다.
- Linux ez2d2m은 두 폭에서 코인을 넣은 뒤에도 시간 제한까지 멈추지 않았습니다. x64는 곡 선택 화면까지 진행했습니다.

---

### English

EZ2Dancer 2nd MOVE (ez2d2m) runs on Linux x86 and x64. It passes its title screen and, with a coin in, goes on to music select and play. The EZ2Dancer board's key bindings moved into a shared core used by both Windows and Linux. New behaviour follows values measured on Windows 11.

#### 1. Running ez2d2m on Linux (task 427)
- **Window**: `CreateWindowExA` takes `WS_BORDER`. As measured, `DefWindowProcA`'s `WM_NCCALCSIZE` insets a bordered window's rectangle by one pixel on each side.
- **Exception report**: `UnhandledExceptionFilter` records the code, address, and parameters of an unhandled guest exception in the run result and stops.
- **EZ2Dancer board**: The Linux IO trap answers the word-wide board; before, the trap turned itself off for a word-wide board. The key-binding table moved into the shared `ez2dancer_keyboard_map`, which the Windows input code uses too. The game reads the board through two input helpers (`0xb169` and `0xb4cb`), so reads are recognised by opcode.

#### 2. The COM1 port that does not open (task 428)
- ez2d2m keeps writing through its `COM1` handle even when the port failed to open. It calls overlapped `WriteFile` for every coin, so on Linux it stopped the moment a coin went in.
- These calls on an invalid handle now fail as measured on Windows 11:
  - overlapped `ReadFile` and `WriteFile`;
  - the eight serial functions (`SetCommState`, `GetCommState`, `SetCommTimeouts`, `PurgeComm`, `SetupComm`, `SetCommMask`, `ClearCommError`, `WaitCommEvent`);
  - `GetOverlappedResult`.
- `CloseHandle(INVALID_HANDLE_VALUE)` succeeds.

#### 3. Changes that reach Windows
- EZ2Dancer key bindings are read from the shared table; the default keys are unchanged.
- Task 427 recorded that a coin did not raise the credit count. That was a misreading, because the key had not reached the game; a coin raises the count on both hosts.

#### 4. Validation
- All 6 Windows x86 CTest tests and all 4 Linux x64/x86 CTest tests pass (5529 / 5526 unit checks).
- Linux ez2d2m ran on both widths without stopping until the timeout, even after coins went in; x64 got as far as music select.

---

## v0.0.55 (2026-09-29)

### 한국어

Linux x86·x64에서 EZ2DJ 4th, 1st SE, 5th가 창을 닫을 때까지 돌아갑니다. 화면, 소리, 키보드·마우스 입력, IO 보드가 모두 연결됐습니다. 1st도 DirectX 6 초기화와 소리 스레드까지 진행합니다. 새로 만든 규칙은 대부분 Windows와 Linux가 함께 쓰는 공용 core에 두었고, Windows 11에서 측정한 값을 따릅니다.

#### 1. Linux 4th를 끝까지 (작업 384~403)
- **입력**: DirectInput core를 공용으로 옮기고 Linux `dinput.dll`을 추가했습니다. IO 보드 포트 판정도 공용 trap core로 옮겨, Linux 두 폭의 signal handler가 답합니다. SDL의 키·마우스·커서가 `GetAsyncKeyState`, DirectInput, `GetCursorPos`, IO 보드로 들어갑니다.
- **소리**: winmm mixer 하나를 모델링하고(Windows 11 host 측정), DirectSound 버퍼 제어를 core로 옮겼습니다. Linux는 SDL3_mixer로 소리를 냅니다(`--audio-gain-db`).
- **화면**: 그리기 규칙(draw 계획, 고정 기능 상태, 변환, fade)을 `direct3d_draw.h` core로 옮겼습니다. Linux 창은 공용 SDL3/OpenGL backend로 그립니다. 기본 2배 크기, Alt+1/2/3, 더블클릭 전체 화면, 제목 FPS, `--fullscreen`/`--windowed`를 지원합니다.
- **표면과 GDI**: 표면 DC, `StretchDIBits`, `EnumSurfaces`/`RestoreAllSurfaces`, DX7 vertex buffer, `CreateSolidBrush`·`FillRect`·`SetTextColor`·`SetBkMode`·`DrawTextA`(GNU Unifont 8×16 ASCII 글리프, OFL 1.1)를 추가했습니다.
- **파일과 메시지**: 현재 디렉터리, `GetFileType`, `FindFirstFileA`(제품 VFS 목록 규칙 공유), `GetFileAttributesA`를 추가했습니다. `PeekMessageA`/`DispatchMessageA`와 메시지 큐(WM_PAINT, WM_TIMER)도 구현했습니다.
- **실행**: 호출 한도 없이 창을 닫을 때까지 실행합니다. 호출 한도는 `--call-limit`로만 겁니다.

#### 2. Linux 1st (작업 404~419)
- **게스트 예외**: 게스트 예외를 게스트의 SEH 체인으로 넘기고 `RtlUnwind`를 구현했습니다. Linux x86에서 게스트가 `%gs`를 바꿔 생기던 coredump도 고쳤습니다.
- **kernel32·user32**: CRT 시작 함수(critical section, TLS, Interlocked, `IsBadReadPtr`), `ShowWindow`, `EnumDisplaySettingsA`, `Sleep`, `HeapValidate`를 추가했습니다. `GetPrivateProfileIntA`/`StringA`/`SectionNamesA`는 공용 INI core로 처리합니다. `wsprintfA`는 가변 인자를 읽어 측정한 서식 규칙으로 처리합니다.
- **DirectX 6**: `DirectDrawEnumerateA`, `DirectDrawCreate`, `IDirectDraw4`, `IDirect3D3`, `IDirectDrawSurface4`, `IDirect3DDevice3`, `IDirect3DViewport3`를 추가했습니다. `FindDevice`, Z 형식, `GetCaps`, `D3DVIEWPORT2` 변환은 Windows DX6 facade와 공용 core를 함께 씁니다.
- **게스트 스레드**: `CreateThread`로 만든 게스트 스레드가 host 스레드에서 돕니다. 게스트 코드와 HLE는 게스트 잠금 하나로 한 번에 한 스레드만 실행하고, 잠금은 import 안에서만 넘어갑니다. x64에서는 transition 상태를 스레드끼리 넘깁니다. 메인이 아닌 스레드의 fault나 `ExitProcess`는 프로세스를 끝냅니다. `SetThreadPriority`, 스레드 핸들 대기, 스레드별 ID·last error도 들어갔습니다.

#### 3. Linux 1st SE와 5th (작업 420~426)
- **1st SE**: CHD로 실행합니다. BMP 파일 읽기(`LoadImageA`와 GDI 비트맵), DX6 texture(`IDirect3DTexture2`), `Blt`/`BltFast`, DX6 vertex buffer를 추가했습니다.
- **소프트웨어 페이싱**: vsync를 요청해도 swap이 막지 않는 host(WSLg)에서는 공용 backend가 화면 주기에 맞춰 present를 기다립니다. 1st SE가 112 FPS 대신 60 FPS로 돕니다. vsync가 실제로 막는 Windows에서는 켜지지 않습니다.
- **표면 Lock**: `Lock`/`Unlock` 규칙을 공용 core(`PlanLock`)로 옮겼습니다. 화면에 내보내는 렌더 타깃을 Lock하면 GL 그림을 되읽고, Unlock하면 다시 올립니다. 4th의 F1(TEST) 테스트 모드 메뉴가 Linux와 Windows 모두에서 보입니다. 전에는 두 host 모두 검은 화면이었습니다.
- **5th**: `StretchDIBits`가 8비트 팔레트 DIB를 받습니다(측정). 5th가 Linux 두 폭에서 창을 닫을 때까지 돕니다.

#### 4. Windows에 영향을 주는 변경
- 4th의 F1 테스트 모드 화면이 나옵니다(작업 425).
- `GetFileAttributesA`를 게스트 이미지 기준으로 답합니다. 전에는 host 현재 디렉터리 기준이었습니다(작업 403).
- 공용 core로 옮긴 규칙은 각 작업에서 Windows 실제 실행 기록을 변경 전후로 비교해, 같게 유지됐음을 확인했습니다.

#### 5. 검증
- Windows x86 CTest 6개, Linux x64·x86 CTest 4개가 통과합니다(단위 5360 / 5357 checks). Linux in-process probe에 게스트 스레드 합성 검사가 포함됩니다.
- Linux 4th·1st SE·5th는 두 폭에서 시간 제한까지 멈추지 않았습니다. 1st는 두 폭에서 `user32!LoadImageA`까지 진행합니다.

---

### English

On Linux x86 and x64, EZ2DJ 4th, 1st SE, and 5th now run until their window is closed, with picture, sound, keyboard and mouse input, and the IO board all connected. 1st gets through DirectX 6 initialization and its sound thread. Most new rules live in shared cores used by both Windows and Linux and follow values measured on Windows 11.

#### 1. Linux 4th end to end (tasks 384–403)
- **Input**: The DirectInput core is shared and Linux gains `dinput.dll`. IO-board port decisions moved into a shared trap core, answered by both Linux widths' signal handlers. SDL keys, mouse, and cursor reach `GetAsyncKeyState`, DirectInput, `GetCursorPos`, and the IO board.
- **Sound**: One winmm mixer is modelled (measured on a Windows 11 host), DirectSound buffer controls moved into the core, and Linux plays sound through SDL3_mixer (`--audio-gain-db`).
- **Picture**: The drawing rules (draw plan, fixed-function state, transforms, fade) moved into the `direct3d_draw.h` core. The Linux window draws through the shared SDL3/OpenGL backend, with a default 2x scale, Alt+1/2/3, double-click fullscreen, the title FPS, and `--fullscreen`/`--windowed`.
- **Surfaces and GDI**: Surface DCs, `StretchDIBits`, `EnumSurfaces`/`RestoreAllSurfaces`, DX7 vertex buffers, and `CreateSolidBrush`, `FillRect`, `SetTextColor`, `SetBkMode`, and `DrawTextA` (GNU Unifont 8×16 ASCII glyphs, OFL 1.1).
- **Files and messages**: The current directory, `GetFileType`, `FindFirstFileA` (sharing the product VFS listing rules), and `GetFileAttributesA`; `PeekMessageA`/`DispatchMessageA` with a message queue (WM_PAINT, WM_TIMER).
- **Running**: A run goes on until the window is closed; a call limit applies only with `--call-limit`.

#### 2. Linux 1st (tasks 404–419)
- **Guest exceptions**: Guest exceptions are delivered to the guest's SEH chain, and `RtlUnwind` is implemented. A Linux x86 coredump caused by the guest changing `%gs` is fixed.
- **kernel32 and user32**: CRT start-up functions (critical sections, TLS, Interlocked, `IsBadReadPtr`), `ShowWindow`, `EnumDisplaySettingsA`, `Sleep`, and `HeapValidate`. `GetPrivateProfileIntA`/`StringA`/`SectionNamesA` go through a shared INI core. `wsprintfA` reads its variadic arguments and follows the measured formatting rules.
- **DirectX 6**: `DirectDrawEnumerateA`, `DirectDrawCreate`, `IDirectDraw4`, `IDirect3D3`, `IDirectDrawSurface4`, `IDirect3DDevice3`, and `IDirect3DViewport3`. `FindDevice`, the depth format, `GetCaps`, and the `D3DVIEWPORT2` transform share a core with the Windows DX6 facade.
- **Guest threads**: Guest threads from `CreateThread` run on host threads of their own. One guest lock lets only one thread run guest code or the HLE at a time, and it changes hands only inside imports. On x64 the transition state is handed between threads. A fault or `ExitProcess` in a thread other than the main one ends the process. `SetThreadPriority`, waits on thread handles, and per-thread IDs and last errors are included.

#### 3. Linux 1st SE and 5th (tasks 420–426)
- **1st SE**: It runs from its CHD. Bitmap files (`LoadImageA` with GDI bitmaps), DX6 textures (`IDirect3DTexture2`), `Blt`/`BltFast`, and DX6 vertex buffers are added.
- **Software pacing**: On a host whose swap does not block despite vsync (WSLg), the shared backend waits out the display period after each present, so 1st SE runs at 60 FPS instead of 112. It stays off on Windows, where vsync does block.
- **Surface Lock**: The `Lock`/`Unlock` rules moved into a shared core (`PlanLock`). Locking the render target that is presented reads the GL picture back, and unlocking writes it back, so 4th's F1 (TEST) test-mode menu shows on both Linux and Windows; before, both hosts showed a black screen.
- **5th**: `StretchDIBits` takes 8-bit palettized DIBs (measured), and 5th runs on both Linux widths until its window is closed.

#### 4. Changes that reach Windows
- 4th's F1 test-mode screen now shows (task 425).
- `GetFileAttributesA` answers from the guest image; it used to query relative to the host's current directory (task 403).
- For each rule moved into a shared core, the task compared real Windows run records before and after the change and confirmed they stayed the same.

#### 5. Validation
- All 6 Windows x86 CTest tests and all 4 Linux x64/x86 CTest tests pass (5360 / 5357 unit checks); the Linux in-process probe includes synthetic guest-thread checks.
- Linux 4th, 1st SE, and 5th ran on both widths without stopping until the timeout; 1st gets as far as `user32!LoadImageA` on both widths.

---

## v0.0.54 (2026-09-26)

### 한국어

Linux에서 실제 4th가 DirectDraw 표면을 만들고 Direct3D 장치를 설정한 뒤, 글꼴 파일을 읽고 DirectSound를 초기화합니다. 두 폭 모두 API 호출 1,872번 뒤 `dinput.dll!DirectInputCreateA`에서 멈춥니다. 화면은 아직 검은색입니다. 그리기와 화면 표시는 이후 단계에서 다룹니다. DirectX core에 표면과 장치를 옮겼고, DirectSound에도 Windows와 Linux가 함께 쓰는 core를 만들었습니다.

#### 1. DirectX core 3·4단계 (작업 381·382)
- **표면**: `CreateSurface` 규칙(flip 주 표면과 back buffer, depth, RGB565 texture, offscreen), pitch, 표면 설명, attach를 core로 옮겼습니다. Linux `IDirectDrawSurface7`은 픽셀을 게스트 메모리에 둡니다. 게스트 COM 객체는 이제 다른 facade 객체의 참조와 게스트 자원을 갖고, 사라질 때 함께 돌려줍니다.
- **장치**: `IDirect3D7::CreateDevice` 규칙과 장치 상태를 core로 옮겼습니다. 장치 상태는 초기값, render·texture stage state, transform, 장면, viewport입니다. Linux `IDirect3DDevice7`은 게임의 장치 설정 호출을 모두 처리합니다. 그 순서와 값은 Windows 기록과 같습니다.
- **SDK 검사**: Windows `static_assert`가 core 상수 네 개를 바로잡았습니다. 대상은 `DDCAPS2_NOPAGELOCKREQUIRED`, `DDERR_CANNOTATTACHSURFACE`, `D3DERR_SCENE_IN_SCENE`, `D3DERR_SCENE_NOT_IN_SCENE`입니다.

#### 2. DirectSound와 창 조회 (작업 383)
- **DirectSound core**: `re2dj::audio`에 게스트 ABI와 버퍼 생성·caps·복제·lock 분할 규칙을 두었습니다. Windows facade와 `LegacyAudioBuffer`가 이 core를 씁니다.
- **Linux `dsound.dll`**: `DirectSoundCreate`, `IDirectSound` 전체, `IDirectSoundBuffer`의 생성·caps·형식·lock·unlock을 구현했습니다. sample은 게스트 메모리에 두고, 복제본은 원본과 sample을 공유합니다. Linux의 소리 출력은 아직 없습니다.
- **user32**: `GetForegroundWindow`를 구현했습니다. `GetWindowLongA`는 Windows 11에서 측정한 last error 규칙을 따릅니다.

#### 3. 검증
- Windows 실제 4th의 그래픽 기록과 오디오 생성·복제 기록은 각 작업의 변경 전후가 같습니다.
- Linux x86과 x64의 실행 기록은 주소만 맞추면 같습니다.

---

### English

On Linux the real 4th now creates its DirectDraw surfaces, sets up its Direct3D device, reads its font files, and initializes DirectSound, stopping at `dinput.dll!DirectInputCreateA` after 1,872 API calls on both widths. The screen is still black; drawing and presentation come in a later phase. Surfaces and the device moved into the DirectX core, and DirectSound gets a core of its own shared by Windows and Linux.

#### 1. DirectX core phases 3 and 4 (tasks 381–382)
- **Surfaces**: The `CreateSurface` rules (a flipping primary with its back buffer, depth, RGB565 textures, offscreen), the pitch, surface descriptions, and attachments moved into the core. Linux's `IDirectDrawSurface7` keeps its pixels in guest memory. Guest COM objects can now hold references to other facade objects and guest resources, and return them when they go.
- **Device**: The `IDirect3D7::CreateDevice` rules and the device state moved into the core. The device state covers the initial values, render and texture stage states, transforms, scenes, and the viewport. Linux's `IDirect3DDevice7` handles all of the game's device setup calls, in the same order and with the same values as the Windows record.
- **SDK checks**: Windows `static_assert`s corrected four core constants: `DDCAPS2_NOPAGELOCKREQUIRED`, `DDERR_CANNOTATTACHSURFACE`, `D3DERR_SCENE_IN_SCENE`, and `D3DERR_SCENE_NOT_IN_SCENE`.

#### 2. DirectSound and window queries (task 383)
- **DirectSound core**: `re2dj::audio` holds the guest ABI and the rules for buffer creation, caps, duplication, and how a lock divides a buffer. The Windows facade and `LegacyAudioBuffer` use this core.
- **Linux `dsound.dll`**: `DirectSoundCreate`, all of `IDirectSound`, and `IDirectSoundBuffer`'s creation, caps, format, lock, and unlock. Samples live in guest memory, and duplicates share their source's samples. Linux has no sound output yet.
- **user32**: `GetForegroundWindow` is implemented, and `GetWindowLongA` follows the last-error rules measured on Windows 11.

#### 3. Validation
- On Windows, the real 4th's graphics record and audio creation and duplication records match the pre-change build for each task.
- Linux x86 and x64 produce the same run record once addresses are normalized.

---

## v0.0.53 (2026-09-26)

### 한국어

Linux에서 실제 4th가 보호 envelope을 지나 원본 CRT와 WinMain으로 들어가고, 게임 창을 만든 뒤 DirectDraw 초기화까지 진행합니다. 두 폭 모두 API 호출 1,824번 뒤 `IDirectDraw7::CreateSurface`에서 멈추고, 호스트 화면에는 게임 창이 뜹니다. Windows와 Linux가 함께 쓰는 DirectX 공용 core도 이 릴리즈에서 시작합니다.

#### 1. Hardlock과 보호 envelope (작업 360~364, 367)
- **Hardlock HLE 공용화**: 설정 조립, `DeviceIoControl` 완료 규칙, 장치 경로 판정을 공용 core로 옮겼습니다. Windows 실제 4th의 Hardlock 기록은 변경 전후가 같습니다. WTS class 4는 `WTSSessionId`로 바로잡았습니다.
- **게스트 장치와 process 환경**: Linux `kernel32` facade가 `\\.\FEnteDev`를 열고 `DeviceIoControl`을 처리합니다. `GuestProcess`는 게스트의 ID, error mode, heap, image·`VirtualAlloc` 영역의 page 보호 기록을 갖습니다. `advapi32`, `wtsapi32` facade도 추가했습니다.
- **envelope 두 번째 층**: 원본 import 표 재구성, `ExitProcess` hook, `Read/WriteProcessMemory`, thread timer를 처리해 원본 진입점까지 갑니다. 복호화 루프의 descriptor·transform 수가 Windows와 같습니다.

#### 2. 원본 CRT에서 WinMain까지 (작업 368~371)
- **MSVC CRT 시작**: heap, 시작 정보, 명령줄·환경, code page 949(실측), module 경로를 제공합니다. stop stub은 게스트 SEH에서 제외했습니다.
- **정적 초기화**: 이름 없는 event, host 시계와 시간 export, `GetTimeZoneInformation`을 구현했습니다. 게임 자신의 Hardlock 로그인도 통과합니다.
- **게스트 파일과 winmm**: CHD와 overlay(copy-on-write)로 게스트 파일을 제공하고(`EZ2DJ.ini` 등), `timeBeginPeriod`/`timeGetTime`을 구현했습니다.
- **API 호출 기록**: facade 호출마다 게스트에서 읽은 입력, 게스트에 쓴 출력, last error, 반환값을 `logs/re2dj-<시각>.api.log`에 남깁니다. Hardlock buffer는 길이만 남깁니다.

#### 3. 창과 DirectDraw (작업 372~374, 377, 380)
- **게스트 호출**: HLE handler가 window procedure 같은 게스트 함수를 끝까지 실행할 수 있습니다. x86은 직접 부르고, x64는 중첩 compat-mode 전환을 씁니다. 게스트가 그 사이에 부르는 API는 중첩 호출로 처리되며, API log에 들여써서 남습니다.
- **창 생성**: `RegisterClassA`, `CreateWindowExA`, `DefWindowProcA`, `UpdateWindow`와 `gdi32!GetStockObject`를 구현했습니다. 창 생성 메시지 순서와 인자는 Windows 11에서 측정한 값을 따릅니다.
- **DirectDraw 진입**: facade COM 객체(vtable은 `"<인터페이스>::<메서드>"` export)를 도입했습니다. `DirectDrawEnumerateExA`(모니터 하나), `DirectDrawCreateEx`, `IDirectDraw7`, `IDirect3D7`의 열거와 caps, `SetCooperativeLevel`, `SetDisplayMode`가 동작합니다.
- **DirectX 공용 core (`re2dj_directx`)**: 32비트 게스트 ABI 구조체, caps, 장치·형식·표시 모드 열거, 협조 수준·표시 모드 규칙을 Windows COM facade와 Linux gate facade가 함께 씁니다. Windows는 SDK와의 구조·상수 일치를 `static_assert`로 검사하며, 실제 4th의 DirectX 기록은 변경 전후가 같습니다.
- **Linux 호스트 창**: 게임이 `SetCooperativeLevel`을 부를 때 SDL3/OpenGL 창(640×480)이 뜹니다. 아직 그리는 것이 없어 검은 화면입니다. `--hold-window`를 주면 실행이 멈춘 뒤에도 창이 남습니다.

#### 4. 제품 표시와 로깅 (작업 365·366, 375·376)
- **버전 머리말**: 창 제목, OSD, `--version`, `--help`, 실행 로그, 진단 도구가 모두 `re2DJ v0.0.53 (Win/x86 Debug)`처럼 OS·아키텍처·빌드 형식을 함께 보여 줍니다. 창 제목은 두 플랫폼이 같은 함수로 만듭니다.
- **spdlog**: CLI 실행 출력과 Windows injected runtime 로그를 spdlog로 옮겼습니다.

#### 5. 구조 정리 (작업 378·379)
- **native helper IPC 제거**: Linux i386 helper와 `--linux-helper`, Windows native helper(선택 빌드), helper protocol, 관련 preset과 script를 지웠습니다. Linux는 두 폭 모두 in-process로만 실행합니다.
- **플랫폼 경계**: Linux 코드와 target이 `src/platform/windows`의 파일을 참조하지 않도록 정리했습니다. 공용 probe fixture는 `src/platform/native_probe_fixture`로 옮겼습니다.

#### 6. 기타
- **수정**: Windows `re2dj_windows_vfs_runtime_probe`의 crash를 고쳤습니다(작업 362). v0.0.52의 알려진 문제입니다.
- **확인 필요**: WSLg에서 Linux 창의 닫기 버튼으로 `--hold-window`가 풀리는지는 사용자 확인 항목입니다.

---

### English

On Linux the real 4th now passes its protection envelope into the original CRT and WinMain, creates its game window, and proceeds through DirectDraw initialization, stopping at `IDirectDraw7::CreateSurface` after 1,824 API calls on both widths with the game window on the host screen. This release also starts the DirectX core shared by Windows and Linux.

#### 1. Hardlock and the protection envelope (tasks 360–364, 367)
- **Shared Hardlock HLE**: Configuration assembly, `DeviceIoControl` completion rules, and device-path decisions moved into the shared core; the real 4th's Hardlock record on Windows is unchanged. WTS class 4 is corrected to `WTSSessionId`.
- **Guest devices and process environment**: The Linux `kernel32` facade opens `\\.\FEnteDev` and serves `DeviceIoControl`. `GuestProcess` keeps the guest's IDs, error mode, heaps, and page protections for image and `VirtualAlloc` regions; `advapi32` and `wtsapi32` facades joined.
- **The envelope's second layer**: Import-table reconstruction, the `ExitProcess` hook, `Read/WriteProcessMemory`, and thread timers carry the guest to the original entry point, with descriptor and transform counts matching Windows.

#### 2. From the original CRT to WinMain (tasks 368–371)
- **MSVC CRT start-up**: Heaps, start-up info, command line and environment, code page 949 (measured), and module paths; the stop stub is excluded from guest SEH.
- **Static initialization**: Unnamed events, a host clock with the time exports, and `GetTimeZoneInformation`; the game's own Hardlock login passes too.
- **Guest files and winmm**: Guest files come from the CHD with a copy-on-write overlay (`EZ2DJ.ini` and others), with `timeBeginPeriod`/`timeGetTime`.
- **API call log**: Every facade call records the inputs read from the guest, the outputs written back, the last error, and the return value in `logs/re2dj-<time>.api.log`; Hardlock buffers keep only their lengths.

#### 3. Windows and DirectDraw (tasks 372–374, 377, 380)
- **Guest calls**: HLE handlers can run a guest function such as a window procedure to completion — directly on x86, through a nested compatibility-mode transition on x64. APIs the guest calls meanwhile dispatch as nested calls, indented in the API log.
- **Window creation**: `RegisterClassA`, `CreateWindowExA`, `DefWindowProcA`, `UpdateWindow`, and `gdi32!GetStockObject`, with the creation messages and arguments measured on Windows 11.
- **DirectDraw entry**: Facade COM objects (vtables filled from `"<interface>::<method>"` exports); `DirectDrawEnumerateExA` (one monitor), `DirectDrawCreateEx`, `IDirectDraw7`, and `IDirect3D7`'s enumerations and caps, `SetCooperativeLevel`, and `SetDisplayMode` work.
- **Shared DirectX core (`re2dj_directx`)**: The 32-bit guest ABI structures, caps, device/format/display-mode enumerations, and cooperative-level and display-mode rules are shared by the Windows COM facade and the Linux gate facade. Windows checks the structures and constants against the SDK with `static_assert`, and the real 4th's DirectX record on Windows is unchanged.
- **Linux host window**: An SDL3/OpenGL window (640×480) opens when the game calls `SetCooperativeLevel`; it is black until drawing arrives. `--hold-window` keeps it open after the run stops.

#### 4. Product naming and logging (tasks 365–366, 375–376)
- **Version banner**: The window title, OSD, `--version`, `--help`, run log, and diagnostic tools all show the OS, architecture, and build type, as in `re2DJ v0.0.53 (Win/x86 Debug)`; both platforms build the window title with one function.
- **spdlog**: CLI run output and the Windows injected runtime's logs moved to spdlog.

#### 5. Structure (tasks 378–379)
- **Native helper IPC removed**: The Linux i386 helper and `--linux-helper`, the Windows native helper (optional build), the helper protocol, and their presets and scripts are gone; Linux runs in-process only, on both widths.
- **Platform boundary**: Linux code and targets no longer reference files under `src/platform/windows`; the shared probe fixture moved to `src/platform/native_probe_fixture`.

#### 6. Other
- **Fix**: The Windows `re2dj_windows_vfs_runtime_probe` crash is fixed (task 362), the known issue of v0.0.52.
- **To confirm**: Whether the Linux window's close button releases `--hold-window` under WSLg is left for the user to confirm.

---

## v0.0.52 (2026-09-24)

### 한국어

Linux에서 원본 PE32를 별도 helper 없이 같은 프로세스 안에서 실행합니다. x86·x86-64 두 host 모두 실제 4th CHD의 보호 stub을 Hardlock 오류 대화상자와 `ExitProcess(9)`까지 원본 코드로 진행합니다.

#### 1. 게스트 PE 호환 모듈 (작업 340~344, 348)
- **module registry와 PE32 facade**: `kernel32`, `user32` 같은 Win32 DLL을 DLL별 export 명세에서 만든 실제 PE32 facade image로 게스트에 보여 줍니다. 정적 IAT와 동적 `GetModuleHandleA`/`GetProcAddress`가 같은 export thunk 주소로 모입니다. 이전의 pseudo handle(`0x7F000001`)은 제거했습니다.
- **facade export**: `kernel32`의 `GetModuleHandleA`, `GetProcAddress`, `GetVersion`, `CreateFileA`, `ExitProcess`와 `user32`의 `GetActiveWindow`, `MessageBoxA`를 제공합니다.
- **종료 계약**: HLE 반환 구조 `ImportReturn`에 "게스트로 돌아가지 않고 프로세스가 끝난다"는 `exit_process`/`exit_code`를 추가했습니다. `ExitProcess`가 이를 쓰며, Linux in-process 실행은 이를 정상 종료로 보고합니다.

#### 2. Linux in-process 실행 (작업 345·349~352, 2026-09-22~23)
- **연속 실행과 게스트 SEH**: facade 위에서 원본을 계속 실행합니다. 첫 미처리 import, 미해석 lookup, fault, 종료에서 멈추고, 그때까지의 API 호출 기록을 출력합니다. 게스트 자신의 `INT3`는 게스트가 등록한 SEH handler로 전달하고, handler가 고친 CONTEXT로 재개합니다.
- **플랫폼 트리 비트 폭 분리**: `src/platform/linux/`를 두 폭 공용 루트와 `x86/`·`x64/` 구현으로 나눴습니다.

#### 3. Linux x86-64 compatibility-mode 실행 (작업 353~357)
- **같은 프로세스 실행**: x86-64 host가 CPU compatibility mode(CS `0x23`)로 32비트 게스트 코드를 직접 실행합니다. 게스트 FS(TEB)와 glibc TLS가 충돌하지 않도록, host로 돌아오는 모든 경로에서 host FS base를 복원합니다(`wrfsbase` 또는 `arch_prctl`).
- **x86과 같은 코드 경로**: PE session, import thunk, runner, facade, kernel32 진단, 게스트 SEH, instruction trace를 두 폭이 공유합니다. 실제 4th CHD의 진단 다섯 개가 x86과 같은 결과를 냅니다.
- **기본 실행 전환**: Linux의 `re2dj --run`은 이제 두 폭 모두 in-process로 실행합니다. 별도 i386 helper는 `--linux-helper <path>`로 고르는 진단 fallback입니다.

#### 4. 기타
- **로깅 표준화**: 런타임 로그를 spdlog 기반으로 통일했습니다(작업 345, 2026-09-21).
- **저장소 정책**: 런타임 산출물 ignore 정책(작업 346)과 플랫폼 비트 폭 디렉터리 규칙(작업 347)을 정했습니다.
- **수정**: x86 bootstrap이 게스트 FS용 TLS GDT 슬롯을 반환하지 않던 누수를 고쳤습니다. 한 프로세스에서 세 번째 실행부터 실패하던 문제입니다.
- **알려진 문제**: Windows `re2dj_windows_vfs_runtime_probe`는 이 릴리즈 이전부터 실패합니다(TODO의 기존 항목).

---

### English

Linux now runs the original PE32 in the same process without a separate helper. On both x86 and x86-64 hosts, the real 4th CHD's protection stub runs on original code through to its Hardlock error dialog and `ExitProcess(9)`.

#### 1. Guest PE compatibility modules (tasks 340–344, 348)
- **Module registry and PE32 facades**: Win32 DLLs such as `kernel32` and `user32` appear to the guest as real PE32 facade images built from per-DLL export declarations. Static IAT slots and dynamic `GetModuleHandleA`/`GetProcAddress` converge on the same export-thunk addresses; the former pseudo handle (`0x7F000001`) is gone.
- **Facade exports**: `kernel32` provides `GetModuleHandleA`, `GetProcAddress`, `GetVersion`, `CreateFileA`, and `ExitProcess`; `user32` provides `GetActiveWindow` and `MessageBoxA`.
- **Exit contract**: The HLE return structure `ImportReturn` gains `exit_process`/`exit_code`, meaning the call does not return because the process ends. `ExitProcess` uses it, and Linux in-process runs report it as a normal exit.

#### 2. Linux in-process execution (tasks 345 and 349–352, 2026-09-22–23)
- **Continuation and guest SEH**: The original keeps running on the facades until the first unhandled import, unresolved lookup, fault, or exit, printing the API call record up to that point. The guest's own `INT3` is delivered to its registered SEH handler and resumed with the CONTEXT the handler edited.
- **Platform tree split by host width**: `src/platform/linux/` is divided into a root shared by both widths and `x86/`/`x64/` implementations.

#### 3. Linux x86-64 compatibility-mode execution (tasks 353–357)
- **Same-process execution**: The x86-64 host runs 32-bit guest code directly in CPU compatibility mode (CS `0x23`). To keep the guest FS (TEB) from colliding with glibc TLS, every path back to the host restores the host FS base (`wrfsbase` or `arch_prctl`).
- **One code path with x86**: Both widths share the PE session, import thunks, runner, facades, kernel32 diagnostic, guest SEH, and instruction trace; all five real-4th-CHD diagnostics match x86.
- **Default run switched**: Linux `re2dj --run` now runs in-process on both widths; the separate i386 helper is a diagnostic fallback selected with `--linux-helper <path>`.

#### 4. Other
- **Logging**: Standardized runtime logging on spdlog (task 345, 2026-09-21).
- **Repository policy**: Defined the runtime-artifact ignore policy (task 346) and the platform bit-width directory rules (task 347).
- **Fix**: Fixed an x86 bootstrap leak that never returned the guest-FS TLS GDT slot, which made a third run in one process fail.
- **Known issue**: The Windows `re2dj_windows_vfs_runtime_probe` fails independently of this release (an existing TODO item).

---

## v0.0.48 (2026-09-18)

### 한국어

- **문서 보완**: 작업 291~297에서 확인한 원본 분석 결과(보호 빌드 복호화 시점, helper RVA 대조, 3rd 설정 레지스트리·데모·autoplay 플래그, 프레임 pacing)를 `EXE_DESIGN`에 누적하고, Win32 커서·자식 창 입력 배경 문서와 후속 TODO를 추가했습니다. 코드 변경은 없습니다.

---

### English

- **Documentation**: Accumulated the original-analysis findings of tasks 291-297 (protected-build decryption timing, helper RVA cross-check, 3rd settings registry, demo and autoplay flags, frame pacing) into `EXE_DESIGN`, and added a background topic on Win32 cursor and child-window input plus follow-up TODO entries. No code change.

---

## v0.0.47 (2026-09-17)

### 한국어

- **Dear ImGui 기반 On-Screen Display(OSD) 추가**: 실행 중 백틱(`` ` ``) 키로 토글할 수 있는 가벼운 OSD를 도입했습니다. 숨김 상태에서는 ImGui 프레임을 구성하지 않아 렌더링 비용이 발생하지 않습니다. 화면 상단에 버전, 빌드 일시, 타깃 프로파일, 실행 파일 이름을 표시합니다.
- **EZ2DJ 3rd Trax 자율 연주(Autoplay) 토글 지원**: 복호화 덤프 분석으로 확인된 내부 autoplay 플래그 주소(`0x00629508`)를 타깃 프로파일의 `game_controls`에 등록했습니다. 런처는 실행 파일의 빌드 timestamp(`0x3bca98a3`)가 일치할 때만 주소를 주입 런타임에 전달해 안전하게 무장하며, 곡 시작 전에 OSD에서 체크하면 해당 곡이 자동으로 연주됩니다. 데모 오버레이나 음소거 등 데모 플레이 부작용이 없습니다.
- **마우스 커서 표시 복구**: 3rd가 커서를 숨기더라도 클라이언트 영역 위에서 마우스 커서가 유지되어 OSD를 조작할 수 있도록 했습니다.

---

### English

- **Dear ImGui On-Screen Display (OSD)**: Introduced a lightweight OSD toggled with backtick (`` ` ``). No ImGui frame is built while hidden, incurring zero rendering overhead. Displays version, build timestamp, target profile, and executable name across the top of the window.
- **Autoplay Toggle for EZ2DJ 3rd Trax**: Registered the internal autoplay flag address (`0x00629508`) in target profile `game_controls`. Armed only when the executable build timestamp (`0x3bca98a3`) matches. Ticking Autoplay before song start plays the song autonomously without demo-play side effects.
- **Mouse Cursor Restoration**: Restored the arrow cursor over the client area to ensure easy interaction with the OSD despite 3rd hiding the cursor.

---

## v0.0.31 (2026-09-07)

### 한국어

런타임 핫 경로 성능 개선: CHD/FAT32 판독 캐시, OpenGL draw 경계 고정 비용 제거, draw 경로 진단 게이트

게스트가 관찰하는 바이트, 픽셀, 상태는 바뀌지 않습니다. 성능 특성만 바뀝니다.

#### 1. CHD/FAT32 판독 캐시 (작업 219)
- **CHD hunk 캐시 추가**: 압축 해제된 hunk의 LRU 캐시 `re2dj::storage::ChdHunkCache`를 저장소 계층의 독립 구성요소로 추가했습니다. `libchdr`의 `chd_read`는 호출마다 다시 압축을 풀기 때문에, 512바이트 sector 하나를 읽을 때마다 4,096바이트 hunk 전체를 LZMA 해제하던 비용을 이 계층이 흡수합니다.
- **FAT32 조회 캐시 추가**: `Fat32Volume`에 디렉터리 항목, 해석된 경로, 파일 클러스터 체인 캐시를 넣었습니다. `ReadFileRange`가 호출마다 경로를 다시 해석하고 FAT 체인을 첫 클러스터부터 다시 걷던 제곱 동작을 제거했고, 중간 클러스터 버퍼 없이 목적지 버퍼로 직접 판독합니다.
- **판독 경로 직렬화**: 게스트가 여러 스레드에서 파일 API를 호출하므로 공개 판독 API를 `std::mutex`로 직렬화했습니다. 이전에는 잠금이 없었고 `libchdr`의 내부 버퍼가 공유 상태였습니다.
- 실제 4th CHD에 대한 `re2dj_chd_probe` 출력이 변경 전후 바이트 단위로 동일함을 확인했습니다.

#### 2. OpenGL draw 경계 고정 비용 제거 (작업 220)
- draw 1회마다 반복되던 `SDL_GL_MakeCurrent` 1회, `glGetUniformLocation` 5회, 정점 속성 배열 활성/비활성 6회, `glTexParameteri` 4회, `std::vector` 힙 할당 1회를 제거했습니다.
- uniform location은 프로그램 링크 직후 한 번만 조회하고, 정점 변환 버퍼는 재사용하며, 텍스처 샘플러 상태는 값이 실제로 바뀔 때만 설정합니다.
- draw별 `glGetError`는 초기 256 draw와 진단 실행으로 한정합니다. `Present`의 프레임 단위 검사는 그대로 유지하므로 지속적인 GL 실패는 계속 검출됩니다.

#### 3. draw 경로 진단 게이트 (작업 221)
- **`--graphics-draw-diagnostics` 옵션 추가**: draw 경로 안에서 실행되던 `ReportDrawDiagnostic`, `ReportLateDrawDiagnostic`, `ReportTransformDiagnostic`을 새 스위치 뒤로 옮겼습니다. 기본값은 꺼짐이며 제품 실행 경로는 이 비용을 지불하지 않습니다.
- 실제 4th CHD 실행에서 `.ddraw.log`가 21,117줄에서 631줄로 줄었고, 텍스처 전 픽셀 스캔과 긴 레코드 포맷팅을 유발하던 draw 단위 항목 20,480건이 사라졌습니다. 초기화 진단은 그대로 기록됩니다.
- draw 단위 증거가 필요한 조사에서는 이 옵션을 명시적으로 켭니다. 관련 가이드와 분석 문서를 함께 갱신했습니다.

#### 4. 문서
- [런타임 핫 경로 성능 설계](docs/design/20260907-219-runtime-performance-hot-paths.md), 작업 지시 3건, 작업 로그 3건을 추가했습니다.
- `libchdr`에 hunk 캐시가 없다는 일반 기술 배경을 [docs/kb/mame-chd-hunk-decompression.md](docs/kb/mame-chd-hunk-decompression.md)에 정리했습니다.
- 4th CHD의 codec 목록과 hunk 수를 분석 문서에 반영하고, CHD 파일 이름이 인식 조건이 아니라는 점을 명시했습니다.

---

### English

Runtime hot-path performance: CHD/FAT32 read caches, per-draw fixed cost removal in the OpenGL boundary, and a gate for the draw-path diagnostics.

The bytes, pixels, and state the guest observes are unchanged; only performance characteristics change.

#### 1. CHD/FAT32 Read Caches (Task 219)
- **CHD hunk cache**: Added `re2dj::storage::ChdHunkCache`, an LRU of decompressed hunks, as its own component in the storage layer. `libchdr`'s `chd_read` decompresses on every call, so reading one 512-byte sector fully decompressed a 4,096-byte LZMA hunk; that cost is now absorbed here.
- **FAT32 lookup caches**: Added directory-entry, resolved-path, and cluster-chain caches to `Fat32Volume`. This removes the quadratic behavior where `ReadFileRange` re-resolved the path and re-walked the FAT chain from the first cluster on every call, and reads now go straight into the caller's destination without an intermediate cluster buffer.
- **Serialized read path**: The public read API is now guarded by a `std::mutex`, since the guest calls the file APIs from several threads; previously there was no lock and `libchdr`'s internal buffers were shared state.
- Verified that `re2dj_chd_probe` output on the real 4th CHD is byte-identical before and after.

#### 2. OpenGL Draw Boundary Fixed Cost (Task 220)
- Removed the per-draw `SDL_GL_MakeCurrent`, five `glGetUniformLocation` lookups, six vertex-attribute-array toggles, four `glTexParameteri` calls, and one vector allocation.
- Uniform locations are resolved once after link, the vertex conversion buffer is reused, and texture sampler state is applied only when a value actually changes.
- The per-draw `glGetError` now runs for the first 256 draws and during diagnostic runs; `Present` keeps its unconditional per-frame check, so a persistently broken GL state is still detected.

#### 3. Draw-Path Diagnostic Gate (Task 221)
- **Added `--graphics-draw-diagnostics`**: `ReportDrawDiagnostic`, `ReportLateDrawDiagnostic`, and `ReportTransformDiagnostic` now sit behind a switch that defaults to off, so the product execution path does not pay for them.
- On the real 4th CHD the `.ddraw.log` dropped from 21,117 lines to 631, removing the 20,480 per-draw entries that drove whole-surface texel scans and long record formatting. Initialization diagnostics still record.
- Investigations that need draw-level evidence turn the option on explicitly; the related guides and analysis documents were updated accordingly.

#### 4. Documentation
- Added the [runtime hot-path performance design](docs/design/20260907-219-runtime-performance-hot-paths.md), three work orders, and three work logs.
- Recorded the absence of a hunk cache in `libchdr` as general background in [docs/kb/mame-chd-hunk-decompression.md](docs/kb/mame-chd-hunk-decompression.md).
- Recorded the 4th CHD codec set and hunk count in the analysis document, and noted that the CHD file name is not part of recognition.

---

## v0.0.30 (2026-09-06)

### 한국어

EZ2DJ 3rd Trax (MAME CHD) 완전 실행(그래픽 60 FPS, 사운드, 키보드 조작) 지원 및 DirectDraw 7 HLE 계층 구현

#### 1. EZ2DJ 3rd Trax 완전 실행 지원
- **MAME CHD 기반 VFS 마운트**: `roms/ez2dj3rd/ez2dj3rd.chd` 이미지를 직접 인식하여 FAT32 파일시스템 상의 `EZ2DJ/EZ2DJ.EXE` 및 리소스(BG, Sound, System)를 동적으로 읽어 실행하도록 지원.
- **오디오 출력**: DirectSound HLE 스트리밍 링 버퍼를 통해 BGM 및 키음의 정상 출력을 확인.

#### 2. DirectDraw 7 HLE 및 윈도우 모드 프레젠테이션
- **DirectDraw 7 계층 구현**: `DirectDrawCreateEx` thunk 및 `IDirectDraw7` 인터페이스 구현.
- **클리퍼 및 프라이머리 서피스 분리**: `CreateClipper` 및 `SetHWnd` 지원, 백버퍼 없는 단독 `PrimarySurface` 생성 지원.
- **윈도우 모드 60 FPS 프레임 표시**: 윈도우 모드 데스크톱 화면 좌표계를 수용하고, `PrimarySurface->Blt` 호출 시 OpenGL FBO 버퍼를 호스트 SDL 창으로 스왑(`Present`)하도록 연동하여 안정적인 60 FPS 화면 갱신을 달성.

#### 3. I/O 포트 에뮬레이션 및 키보드 입력 지원
- **3rd 전용 Legacy I/O 헬퍼 RVA 확정**: 메모리 역어셈블 분석을 통해 `in al, dx`(`0x000a9887`) 및 `out dx, al`(`0x000a98bb`) 헬퍼 주소를 확정하고 포트(`0x101`~`0x106`)를 에뮬레이션 버스에 연결.
- **키보드 파싱 확장**: `config/ez2dj-io.example.ini`의 1P 턴테이블 키(`TAB`)를 비롯하여 `ESC`, `SHIFT`, `CTRL`, `ALT`, `BACKSPACE`, `CAPS`, `INSERT`, `DELETE`, `HOME`, `END`, `PAGEUP`, `PAGEDOWN` 등의 파싱 지원 추가.
- **설정 파일 경로 정규화**: `--io-config` 인자를 절대 경로로 자동 변환하여 게스트 작업 디렉터리 경로 불일치 문제 해결. 코인 투입(F5), 스타트(1/2), 건반, 턴테이블, 페달 조작 지원.

#### 4. 릴리즈 빌드 스크립트 추가
- 최적화된 바이너리를 빌드하고 검증하기 위한 `scripts/build_release.bat` 및 `scripts/build_release.ps1` 추가.

---

### English

Full execution support for EZ2DJ 3rd Trax (60 FPS graphics, audio, and keyboard controls) and DirectDraw 7 HLE implementation.

#### 1. Full EZ2DJ 3rd Trax Execution Support
- **MAME CHD-Backed VFS Mount**: Directly mounts `roms/ez2dj3rd/ez2dj3rd.chd`, dynamically reading `EZ2DJ/EZ2DJ.EXE` and game assets (BG, Sound, System) from the FAT32 volume.
- **Audio Playback**: Confirmed pristine BGM and key sound playback through the DirectSound HLE streaming ring buffer.

#### 2. DirectDraw 7 HLE & Windowed Presentation
- **DirectDraw 7 Layer Implementation**: Added `DirectDrawCreateEx` export thunk and `IDirectDraw7` interface wrapper.
- **Clipper & Standalone Primary Surface**: Added support for `CreateClipper`, `SetHWnd`, and standalone `PrimarySurface` creation without backbuffers.
- **Windowed 60 FPS Frame Presentation**: Accepts screen-space desktop coordinates and triggers host SDL window buffer swaps (`Present`) inside `PrimarySurface->Blt`, achieving smooth and stable 60 FPS rendering.

#### 3. I/O Port Emulation & Keyboard Input Support
- **Confirmed 3rd Legacy I/O Helper RVAs**: Identified `in al, dx` (`0x000a9887`) and `out dx, al` (`0x000a98bb`) helper instruction RVAs through memory disassembly, mapping ports `0x101` through `0x106` to the I/O bus.
- **Extended Keyboard Key Parsing**: Added support in `ParseKey` for `TAB` (`p1_positive=TAB`), `ESC`, `SHIFT`, `CTRL`, `ALT`, `BACKSPACE`, `CAPS`, `INSERT`, `DELETE`, `HOME`, `END`, `PAGEUP`, and `PAGEDOWN`.
- **Configuration Path Canonicalization**: Canonicalized `--io-config` paths to absolute paths, resolving guest working directory path mismatches and enabling full control for Coin (F5), Start (1/2), Keys, Turntables, and Pedal.

#### 4. Release Build Scripts
- Added `scripts/build_release.bat` and `scripts/build_release.ps1` for building and verifying optimized Release binaries.
