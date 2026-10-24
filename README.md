# re2DJ

![Language](https://img.shields.io/badge/C%2B%2B-20-00599C)
![Hosts](https://img.shields.io/badge/hosts-Linux%20%7C%20Windows%20x64-0078D4)
![Status](https://img.shields.io/badge/status-experimental-orange)
![License](https://img.shields.io/badge/license-BSD--3--Clause-blue)

re2DJ는 에뮬레이터나 가상 머신을 동원하지 않고, EZ2DJ의 원본 32비트 x86 실행 파일을 Linux와 64비트 Windows에서 실행하기 위한 실험적 런타임입니다. 게임 로직은 원본 코드에 그대로 남겨 두고, 그 주변의 Win32 API·DirectX·하드웨어 경계만 High Level Emulation(HLE)으로 제공합니다.

현재 버전은 [VERSION](VERSION)에서 확인할 수 있습니다.

*re2DJ is an experimental runtime for executing the original 32-bit x86 EZ2DJ executable on Linux and 64-bit Windows without an emulator or a virtual machine. Original game logic stays authoritative; only the surrounding Win32 API, DirectX, and hardware boundaries are replaced with High Level Emulation (HLE). See [VERSION](VERSION) for the current version.*

> [!WARNING]
> 아직 실험 단계입니다. 1st SE, 4th, 5th, 6th, EZ2Dancer 2nd MOVE는 창을 닫을 때까지 실행되지만, 다른 타깃은 아직 모형이 없는 경계에서 멈출 수 있고 소리·입력·화면의 정확성도 계속 검증 중입니다.
>
> *This is still experimental software. 1st SE, 4th, 5th, 6th, and EZ2Dancer 2nd MOVE run until their window is closed, but other targets may stop at a boundary not yet modelled, and the accuracy of sound, input, and graphics is still being verified.*

> [!IMPORTANT]
> 이 저장소는 원본 게임 바이너리나 데이터를 포함하지 않으며 배포하지도 않습니다. 합법적으로 보유한 자산에 대해서만 사용하십시오.
>
> *This repository neither contains nor distributes original game binaries or data. Use it only with assets you legally own.*

## 스크린샷 / Screenshots

re2DJ v0.0.63의 Windows x86 Release 빌드에서 원본 실행 파일이 그린 화면입니다. 640x480 원본 화면을 2배 창에서 캡처해 다시 640x480으로 줄였습니다.

*Screens drawn by the original executables under the Windows x86 Release build of re2DJ v0.0.63, captured from the 2x window and scaled back to the original 640x480.*

| EZ2DJ The 1st Tracks Special Edition (`ez2dj1stse`) | EZ2DJ 4th (`ez2dj4th`) | EZ2DJ 4th — demo play |
| :---: | :---: | :---: |
| ![EZ2DJ 1st SE title](docs/screenshots/ez2dj1stse-title.jpg) | ![EZ2DJ 4th title](docs/screenshots/ez2dj4th-title.jpg) | ![EZ2DJ 4th demo play](docs/screenshots/ez2dj4th-demo-play.jpg) |
| **EZ2DJ 5th Trax (`ez2dj5th`)** | **EZ2DJ 6th Trax (`ez2dj6th`)** | **EZ2Dancer 2nd MOVE (`ez2d2m`)** |
| ![EZ2DJ 5th title](docs/screenshots/ez2dj5th-title.jpg) | ![EZ2DJ 6th title](docs/screenshots/ez2dj6th-title.jpg) | ![EZ2Dancer 2nd MOVE title](docs/screenshots/ez2d2m-title.jpg) |

### 화면 후처리 셰이더 / Post-processing shaders

같은 장면을 `none`, `crt`, `scanline`으로 찍어 같은 부분을 1:1로 잘라 나란히 놓았습니다(왼쪽부터). 주사선은 출력 픽셀 단위 무늬라 줄이면 사라지므로 원본 크기입니다. 1280x960 전체 화면은 [`docs/screenshots/shaders/`](docs/screenshots/shaders/)에 있습니다.

*The same scene under `none`, `crt` and `scanline`, the same part cropped 1:1 and placed side by side (left to right); scanlines are an output-pixel pattern that vanishes when scaled down, so the crops are at full size. The full 1280x960 frames are in [`docs/screenshots/shaders/`](docs/screenshots/shaders/).*

![EZ2DJ 4th title: none, crt, scanline](docs/screenshots/shaders/ez2dj4th-title-crop.png)

![EZ2DJ 6th title: none, crt, scanline](docs/screenshots/shaders/ez2dj6th-title-crop.png)

---

## 주요 특징 / Why re2DJ

* **원본 로직 보존:** 게임플레이를 C++로 재작성하지 않고 원본 x86 코드를 주 실행 경로로 유지합니다.
* **선별적 HLE:** 경계는 Win32 import thunk입니다. 게임이 실제로 호출하는 API만 좁은 범위로 구현합니다.
* **처음부터 멀티플랫폼:** 공용 코어는 호스트 OS 헤더를 포함하지 않으며, Windows/Linux 세부 구현은 플랫폼 계층에 분리합니다.
* **HDD·CHD 입력 경계:** 추출 HDD는 사용자가 지정한 디렉터리로 받고, MAME CHD는 `libchdr` 기반 FAT32 read-only 계층으로 읽습니다. 원본 자산은 저장소에 포함하지 않습니다.
* **원본 무변경 보장:** 게스트의 파일 쓰기는 overlay 디렉터리로 향하므로 원본 덤프는 그대로 유지됩니다.
* **재현 가능한 진척 기록:** 설계, 작업 지시, 분석과 기술 지식을 저장소 문서로 누적합니다.

*The project preserves original x86 game logic, applies narrowly scoped HLE at the Win32 import boundary, keeps the shared core free of host OS headers, accepts extracted HDD contents as a directory and reads MAME CHDs through a libchdr-backed read-only FAT32 layer, routes guest writes to an overlay so the dump stays untouched, and keeps reproducible design and analysis records.*

---

## 동작 방식 / How it works

```mermaid
flowchart LR
    HDD["HDD directory<br/>(user-supplied path)"] --> SCAN["HDD scan<br/>+ target profile"]
    SCAN --> PE["PE32 image reader"]
    PE --> LOAD["PE32 loader"]
    LOAD --> EXEC["in-process runner<br/>(native 32-bit x86)"]
    EXEC -->|import gate| HLE["Win32 / DirectX HLE"]
    HLE --> PLAT["Platform backend<br/>windows / linux"]
```

두 OS 모두 `re2dj --run`이 원본 PE32를 re2dj **자기 프로세스 안에** 매핑해 실행합니다(작업 446~449). 예를 들어 `re2dj ez2dj4th`는 `roms/ez2dj4th`의 CHD에서 `EZ2DJ/EZ2DJ.EXE`를 선택합니다. Linux는 x86·x86-64 제품이 있고 x86-64는 CPU compatibility mode를 씁니다. Windows는 64비트 Windows에서 도는 Win32 x86 제품이며, 시작할 때 자기 자신을 한 번 다시 띄워 게스트 이미지 주소(0x400000)를 확보합니다. 실행은 `kernel32`·`user32`·`gdi32`·DirectX facade와 게스트 SEH를 거칩니다. 3rd, 4th, 1st SE, 5th, 6th, EZ2Dancer 2nd MOVE CHD는 창을 닫을 때까지 실행됩니다(3rd는 창 모드 DirectDraw, #15). 다른 타깃은 아직 모형이 없는 첫 import·lookup·fault에서 멈출 수 있습니다. 두 호스트 모두 실행 중 백틱(`` ` ``) 키나 패드의 `LT`+`RT`+`Y`로 OSD를 열 수 있습니다. 열려 있는 동안 패드는 OSD 조작(D-pad 이동, `A` 선택, `B`로 메뉴나 OSD 닫기)에만 쓰이고 게임에는 들어가지 않습니다(#22). OSD 맨 위의 "Fullscreen"과 "Keep aspect ratio"는 전체 화면과 원본 4:3 비율 유지(끄면 창 전체로 늘림)를 바꾸며, Alt+Enter나 더블클릭으로 바꾼 전체 화면과 함께 `cfg/re2dj.ini`에 저장돼 다음 실행에 쓰입니다(#14). OSD의 "32-bit color"는 24비트 이미지와 반투명 합성을 채널당 8비트로 보여 주는 표시 모드를 켜고 끕니다(`--color-depth`). 화면 후처리 셰이더(내장 `crt`·`scanline`·`fsr`·`scalefx`·`anime4k`, `shaders/`의 `.glsl`과 다단계 `.glslp`)는 `--post-shader`나 OSD로 고릅니다([가이드](docs/guides/post-process-shaders.md)). 자세한 내용은 [ARCHITECTURE.md](ARCHITECTURE.md)를 참고하십시오.

*On both OSes `re2dj --run` maps the original PE32 into re2dj's **own process** and runs it there (tasks 446 to 449); for example, `re2dj ez2dj4th` selects `EZ2DJ/EZ2DJ.EXE` in the CHD under `roms/ez2dj4th`. Linux has x86 and x86-64 products, x86-64 using CPU compatibility mode; Windows has the Win32 x86 product on 64-bit Windows, which starts itself once more at launch to secure the guest image address (0x400000). Runs go through the `kernel32`, `user32`, `gdi32`, and DirectX facades and guest SEH. the 3rd, 4th, 1st SE, 5th, 6th, and EZ2Dancer 2nd MOVE CHDs run until their window is closed (3rd through windowed DirectDraw, #15); other targets may still stop at the first import, lookup, or fault not yet modelled. On both hosts, backtick (`` ` ``) or `LT`+`RT`+`Y` on a pad opens the OSD while running; while it is open the pad drives only the OSD (the d-pad moves, `A` chooses, `B` closes a menu or the OSD) and none of it reaches the game (#22). Its "Fullscreen" and "Keep aspect ratio" at the top switch fullscreen and keeping the original 4:3 shape (off stretches over the window); they, and fullscreen switched with Alt+Enter or a double click, are kept in `cfg/re2dj.ini` for the next run (#14). Its "32-bit color" switches a display mode that shows 24-bit images and translucent compositing at 8 bits per channel (`--color-depth`). Screen post-processing shaders (the built-in `crt`, `scanline`, `fsr`, `scalefx` and `anime4k`, and `.glsl` or multi-pass `.glslp` files in `shaders/`) are chosen with `--post-shader` or in the OSD ([guide](docs/guides/post-process-shaders.md)). See [ARCHITECTURE.md](ARCHITECTURE.md) for details.*

---

## 요구 사항 / Prerequisites

| 호스트 | 필요한 것 |
| --- | --- |
| 64-bit Windows | Visual Studio 2019 이상 또는 Build Tools의 **Desktop development with C++**, CMake 3.20 이상 |
| Linux x86 / x86-64 | GCC 11 이상 또는 Clang 14 이상, CMake 3.20 이상, Ninja, 해당 폭의 SDL3용 X11/Wayland/OpenGL 개발 패키지. x86은 `g++-multilib libc6-dev-i386` 추가 |

SDL3와 SDL_mixer는 CMake가 고정된 zlib 라이선스 버전에서 가져옵니다. 원본 자산 없이도 빌드되고 단위 테스트가 통과합니다.
Ubuntu/WSL의 정확한 패키지 설치 명령은 [Linux SDL3/OpenGL 빌드 가이드](docs/guides/linux-sdl3-build.md)를 참고하세요.

*CMake fetches SDL3 and SDL_mixer from pinned zlib-licensed revisions. The repository builds and passes its unit tests without any original assets. See the [Linux SDL3/OpenGL build guide](docs/guides/linux-sdl3-build.md) for the exact Ubuntu/WSL package command.*

---

## 시작하기 / Getting started

### 1. 저장소 복제 / Clone

```bash
git clone https://github.com/reexec/re2DJ.git
cd re2DJ
```

### 2. 빌드 / Build

```bash
# 64-bit Windows host (Win32 runtime under WOW64)
cmake --preset windows-x86-debug
cmake --build --preset windows-x86-debug
ctest --preset windows-x86-debug

# Linux x86-64
cmake --preset linux-x64-debug
cmake --build --preset linux-x64-debug
ctest --preset linux-x64-debug

# Linux x86
cmake --preset linux-x86-debug
cmake --build --preset linux-x86-debug
ctest --preset linux-x86-debug

```

빌드 산출물은 `build/<preset>/bin/`에 생성됩니다.

*Build output lands in `build/<preset>/bin/`.*

### 3. 원본 자산 준비 / Supply original assets

원본 HDD 내용은 **디렉터리 경로**로 입력받습니다. 디스크 이미지를 직접 마운트하지 않으므로, 이미지를 먼저 풀어 놓은 디렉터리를 그대로 가리키면 됩니다. 경로는 저장소 밖이어도 되고, 어디에 두든 상관없습니다.

*Original HDD contents arrive as a **directory path**. Disk images are not mounted directly, so extract the image first and point at the resulting directory. It may live anywhere, including outside the repository.*

EZ2DJ The 1st Tracks Special Edition 덤프의 실제 구성은 다음과 같습니다.

*A real EZ2DJ The 1st Tracks Special Edition dump looks like this.*

```text
/path/to/ez2dj_hdd/
├── Test.exe           서비스 도구 / service tool
├── ez2dj.ini          난이도·모드·곡 목록 / difficulty, modes, song lists
├── Songs/             68개 곡 디렉터리 / 68 song directories
└── System/            화면별 자산 / per-screen assets
```

> [!NOTE]
> 이 디렉터리는 읽기 전용으로 취급됩니다. 게스트가 파일을 쓰기 시작하면 원본이 아니라 별도 overlay 디렉터리에 기록됩니다.
>
> *The directory is treated as read-only. Once the guest starts writing files, they go to a separate overlay directory rather than to the original.*

### 4. 덤프 확인 / Inspect the dump

```bash
build/linux-x64-debug/bin/re2dj_hdd_probe /path/to/ez2dj_hdd
```

디렉터리를 훑어 모든 `.exe`의 PE 헤더를 읽고, 어떤 파일이 게스트 형식(32비트 x86 PE32)인지 보고합니다.

*It walks the directory, reads the PE headers of every `.exe`, and reports which files are in the guest format — 32-bit x86 PE32.*

### 5. 타깃 확인 / Check the target

```bash
build/linux-x64-debug/bin/re2dj --hdd /path/to/ez2dj_hdd
```

스캔 결과에서 실행 대상을 고르고 그 요약을 출력합니다. `--target <id>`로 다른 후보를 고르고, `--list-targets`로 후보만 나열할 수 있습니다.

*It selects a launch target from the scan and prints a summary. Use `--target <id>` to choose a different candidate and `--list-targets` to list candidates only.*

확인된 덤프는 내장 프로파일이 자동으로 잡습니다. 현재 내장된 디렉터리 프로파일은 EZ2DJ The 1st Tracks와 2nd Trax이고, CHD shortcut은 1st Trax Special Edition, 3rd Trax, 4th, 5th, 6th, 그리고 EZ2DJ가 아닌 EZ2Dancer 2nd MOVE(`ez2d2m`)입니다. 그 밖의 덤프도 스캔으로 감지됩니다.

*A recognised dump is matched by a built-in profile. EZ2DJ The 1st Tracks and 2nd Trax are built in as directory profiles; the 1st Tracks Special Edition, 3rd Trax, 4th, 5th and 6th are built in as CHD shortcuts, along with EZ2Dancer 2nd MOVE (`ez2d2m`), which is a different product rather than an EZ2DJ release. Anything else is still found by scanning.*

```text
targets:
  * ez2dj2nd               ez2dj/EZ2DJ.exe          built-in
```

1st SE는 CHD shortcut이므로 추출 디렉터리 스캔에서는 built-in으로 잡히지 않고 detected 항목으로만 나열됩니다.

*The 1st SE profile is a CHD shortcut, so an extracted-directory scan lists its executables as detected entries rather than claiming the built-in profile.*

```text
targets:
  * ez2dj                  ez2dj.exe                detected
    test                   Test.exe                 detected
    plzpoweroff            PlzPowerOff.exe          detected
```

### 런처 / Launcher

인자 없이 `re2dj`를 실행하면 런처 창이 뜹니다. 현재 디렉터리의 `roms/` 아래에서 내장 프로필마다 실행할 수 있는지와 그 이유를 보여 주고, 고른 게임을 실행합니다. 게임이 끝나면 런처로 돌아옵니다. 전체 화면, 비율 유지, 색 깊이, 화면 셰이더, 소리 크기를 고를 수 있고, 바꾼 값과 마지막 프로필은 `cfg/re2dj.ini`에 남습니다. 런처 창도 전체 화면과 비율 유지를 따릅니다. 키보드·마우스·게임패드로 조작합니다(Enter·패드 A·더블클릭으로 시작, Alt+Enter로 전체 화면, Esc로 종료).

*Run without arguments, `re2dj` opens a launcher window. It shows, for each built-in profile, whether it can run from `roms/` under the current directory and why not, starts the chosen game, and comes back when the game ends. Fullscreen, keep-aspect, colour depth, screen shader and sound gain can be chosen; changed values and the last profile are kept in `cfg/re2dj.ini`, and the launcher window follows fullscreen and keep-aspect too. Keyboard, mouse and gamepads drive it (Enter, the pad's A or a double click starts, Alt+Enter switches fullscreen, Esc quits).*

```bash
cd /path/to/re2DJ          # roms/ and cfg/ are looked up here
build/linux-x64-release/bin/re2dj
```

릴리스 아카이브로 설치했다면 런처가 GitHub의 새 버전을 알리고, **Update** 버튼으로 받아 검증하고 바꾼 뒤 다시 시작합니다(#17). 스팀덱 설치와 끄는 방법은 [런처 업데이트 가이드](docs/guides/launcher-update.md)에 있습니다.

인자를 준 실행은 지금과 같습니다. 인자 없이 사용법만 보려면 `RE2DJ_LAUNCHER=0`을 줍니다. 창을 열 수 없는 환경에서도 사용법을 출력합니다. 근거: [#12 설계](docs/design/20261010-i012-launcher.md).

*Installed from a release archive, the launcher announces a newer GitHub release and its **Update** button downloads, checks and replaces it, then restarts (#17); a Steam Deck install and turning it off are in the [launcher update guide](docs/guides/launcher-update.md).*

*Runs with arguments are unchanged. `RE2DJ_LAUNCHER=0` prints the usage instead, as does a run that cannot open a window. See the [#12 design](docs/design/20261010-i012-launcher.md).*

### 6. 경로 해석 확인 / Check path resolution

```bash
build/linux-x64-debug/bin/re2dj --hdd /path/to/ez2dj_hdd --resolve "C:\EZ2DJ\data\song01.ez"
```

원본은 Windows에서 동작했으므로 게임 코드가 실제 파일명과 다른 대소문자로 파일을 엽니다. 이 옵션은 게스트 경로 하나를 실제 호스트 경로로 해석해 보여 줍니다. 결과는 요청한 철자가 아니라 **디스크에 있는 철자**로 나오므로 호스트가 달라도 같은 값이 나옵니다.

*The original ran on Windows, so game code opens files with a case that need not match the real name. This option resolves one guest path to its real host path. The result carries the **on-disk** spelling rather than the requested one, so it is identical across hosts.*

### 7. 실행 파일 분석 / Analyze the executable

```bash
build/linux-x64-debug/bin/re2dj_pe_analyzer /path/to/ez2dj_hdd/EZ2DJ/Ez2dj.exe
build/linux-x64-debug/bin/re2dj_pe_analyzer --hdd /path/to/ez2dj_hdd "EZ2DJ/Ez2dj.exe"
```

PE 헤더, 섹션 테이블, data directory를 출력합니다. 이미지를 적재하지 않으므로 실행 위험이 없습니다.

*It prints the PE headers, section table, and data directories. The image is never loaded, so nothing is executed.*

### 8. 이미지 적재 확인 / Verify image loading

```bash
build/linux-x64-debug/bin/re2dj_pe_loader --hdd /path/to/ez2dj_hdd "EZ2DJ/Ez2dj.exe"
```

PE32 이미지를 게스트 주소 공간에 적재하고 진입점, TLS directory, import별 합성 gate 주소를 보고합니다. 원본 코드는 실행하지 않습니다. 마지막 인자로 `0x10000000` 같은 load base를 주면 재배치 가능 여부와 재배치 경로를 확인할 수 있습니다.

*It maps the PE32 image into guest memory and reports the entry point, TLS directory, and synthetic gate address for every import. Original code is not executed. An optional final load base such as `0x10000000` exercises relocation or reports that the image cannot be rebased.*

### 9. legacy I/O helper 위치 확인 / Locate the legacy port-I/O helpers

```bash
build/windows-x86/bin/Debug/re2dj_port_helper_scan /path/to/ez2dj_hdd/ez2dj.exe
build/windows-x86/bin/Debug/re2dj_port_helper_scan dump.image.bin 0x00400000
```

게스트가 트랩되지 않은 `in`/`out`으로 privileged fault를 낼 때 런타임이 쓰는 helper 주소를 시그니처로 찾습니다. 입력은 `.text`가 평문인 디스크 파일이거나 `--image-dump`가 만든 복호화 덤프이며, 둘 다 파일 오프셋이 곧 RVA입니다. 두 번째 인자로 image base를 주면 RVA 대신 VA를 출력합니다. 탐색은 구문적이므로 결과는 후보이며, 아무것도 찾지 못하는 것은 보호 빌드를 디스크에서 읽었을 때의 정상 결과입니다.

*It finds, by signature, the helper addresses the runtime uses when the guest raises a privileged fault on an untrapped `in`/`out`. The input is either a disk file whose `.text` is plaintext or a decrypted dump from `--image-dump`; a file offset is the RVA in both. A second argument supplies an image base to print VAs instead of RVAs. The search is syntactic, so each hit is a candidate, and finding nothing is the correct result for a protected build read off disk.*

---

## 명령행 / Command line

```text
re2dj                         런처(인자 없음, RE2DJ_LAUNCHER=0이면 사용법)
re2dj <profile-id> [options]
re2dj --hdd <directory> [options]

  --hdd <directory>   추출한 원본 HDD 내용. 프로파일 shortcut 경로보다 우선.
                      CHD 프로파일에서는 .chd 하나가 있는 디렉터리도 허용.
  --target <id>       사용할 타깃 프로파일. 기본값은 첫 번째 후보.
  --list-targets      후보 타깃 프로파일을 나열하고 종료.
  --resolve <path>    게스트 경로 하나를 해석하고 종료.
  --run               게스트 실행. positional 프로파일은 자동으로 --run을 선택.
  --hold-window       실행이 멈춘 뒤에도 게임 창을 닫을 때까지 유지.
  --audio-gain-db     출력 보정(-24..+18 dB, 기본값 0).
  --fullscreen        monitor 크기 borderless fullscreen 사용.
  --windowed          프로파일의 fullscreen 기본값을 끄기. 둘 다 없으면
                      cfg/re2dj.ini의 [Video] fullscreen, 그다음 프로파일.
                      실행 중 Alt+Enter·더블클릭·OSD로 바꾼 값은 저장됩니다.
  --keep-aspect       원본 4:3 비율을 지키고 남는 곳은 검은 띠(기본값, 또는
                      cfg/re2dj.ini의 [Video] keep_aspect).
  --stretch           그림을 창 전체로 늘림. OSD로 바꾼 값은 저장됩니다.
  --image-dump        매핑된 주 이미지를 진입 전과 지연 뒤에 저장(진단).
  --image-dump-delay <ms>
                      두 번째 덤프까지의 지연(기본값 5000).
  --call-limit <n>    게스트 API 호출 n번 뒤 멈춤(진단·회귀용).
  --color-depth <16|32>
                      호스트가 색을 얼마나 깊게 다룰지 고릅니다. 16은 원본의
                      16비트 화면(기본값), 32는 24비트 이미지와 블렌드를 채널당
                      8비트로 유지합니다. 게임은 계속 16비트 화면을 봅니다.
                      실행 중에는 OSD의 "32-bit color"로 바꿀 수 있습니다.
  --post-shader <id>, --post-shader=<id>
                      화면 후처리 셰이더. none(기본값), 내장 crt·scanline·fsr·scalefx·anime4k,
                      또는 shaders/ 안의 .glsl·.glslp 파일 이름. 타깃 앞뒤 어디든 되고,
                      RE2DJ_POST_SHADER보다 우선하며, 여러 번 주면 마지막 값을
                      씁니다. 실행 중에는 OSD에서 바꿀 수 있습니다.
  --vsync <on|off|adaptive>
                      화면을 언제 보일지. on은 화면 갱신에 맞춤(기본값), off는
                      바로(최대 처리량 측정용, 찢어짐이 보일 수 있음), adaptive는
                      늦은 프레임만 기다리지 않음.
  --io-config <path>  선택한 타깃용 키보드·게임패드 I/O mapping INI. 적힌 항목만
                      내장 기본 매핑을 덮어씁니다.
  --version           버전 출력.
  --help              도움말 출력.
  --                  옵션의 끝. 뒤의 인자는 프로파일 id로 읽습니다.
```

CHD 자체의 header, metadata와 논리 sector를 확인하려면 다음 비실행 도구를 사용합니다.

```bash
re2dj_chd_probe /path/to/ez2dj4th.chd
```

*Use the following tool to inspect a CHD's header, metadata, logical sectors, FAT32 layout, and the 4th PE32 header; `re2dj ez2dj4th` reads the CHD through the same FAT32 boundary.*

```bash
re2dj_chd_probe /path/to/ez2dj4th.chd
```

같은 도구로 이미지 안의 디렉터리를 나열하거나, 파일 하나를 꺼내거나, 하위 트리 전체를 호스트로 펼칠 수 있습니다. 추출은 자산 배치를 사람이 보기 위한 진단 경로이며 실행 입력이 아닙니다. 런타임은 계속 CHD를 직접 읽습니다. 빈 내부 경로는 볼륨 루트를 뜻하고, 읽지 못한 항목이 있으면 경로를 경고로 남긴 뒤 계속 진행하며 0이 아닌 종료 코드로 알립니다.

*The same tool lists a directory inside the image, dumps a single file, or lays a whole subtree onto the host. Extraction is a diagnostic path for reading an asset layout by hand, not an execution input — the runtime keeps reading the CHD directly. An empty inner path means the volume root; entries that cannot be read are reported as warnings, the walk continues, and the exit code is non-zero.*

```bash
re2dj_chd_probe /path/to/ez2d2m.chd --list ez2dancer
re2dj_chd_probe /path/to/ez2d2m.chd --dump ez2dancer/EZ2Dancer.exe ./EZ2Dancer.exe
re2dj_chd_probe /path/to/ez2d2m.chd --extract "" roms/ez2d2m/extracted
```

Windows 제품 실행 예(두 OS의 사용법은 같습니다):

```powershell
.\build\windows-x86\bin\Debug\re2dj.exe ez2dj4th
.\build\windows-x86\bin\Debug\re2dj.exe ez2dj1stse --hdd D:\EZ2DJ\1stSE --list-targets
.\build\windows-x86\bin\Debug\re2dj.exe ez2dj6th --fullscreen
```

프로파일 ID만 주면 그 shortcut 경로(`roms/<프로파일>`)를 쓰고 실행까지 고릅니다. `--hdd`는 그 경로를 덮어씁니다. Windows에서 시작 직후 `re2dj.exe`가 한 번 더 뜨는 것은 게스트 이미지 주소를 예약하기 위한 것입니다([Windows 실행 가이드](docs/guides/windows-x86-runtime.md)). 1st SE·4th 같은 보호 빌드는 로컬 Hardlock 자료(`cfg/hardlock.ini` 등)가 있어야 Hardlock 요청을 지나갑니다. 그 자료는 저장소에 포함하지 않으며 사용자가 직접 확보합니다. 관측 내용은 각 프로파일의 [분석 문서](docs/analysis/README.md)에 있습니다.

*Giving only a profile ID takes its shortcut path (`roms/<profile>`) and selects the run; `--hdd` overrides that path. On Windows, `re2dj.exe` starting once more right after launch reserves the guest image address ([Windows runtime guide](docs/guides/windows-x86-runtime.md)). Protected builds such as 1st SE and 4th need local Hardlock material (`cfg/hardlock.ini` and the rest) to pass their Hardlock requests; that material is not part of the repository and users supply it themselves. The observations are in each profile's [analysis documents](docs/analysis/README.md).*

기본값은 version, build date, SDL3 OpenGL renderer와 FPS를 표시하는 제목 및 1280×960 client 영역을 가진 resize 가능한 일반 창이다. 원본의 640×480 논리 표시는 기본 가로·세로 정확히 2배로 확대된다. 원본 INI를 바꾸지 않고 fullscreen을 선택하려면 `--fullscreen`을 추가한다.

*The default is a normal resizable 1280x960 client-area window whose title shows the version, build date, SDL3 OpenGL renderer, and FPS. The original 640x480 logical display starts at exactly 2x in both dimensions. Add `--fullscreen` to select fullscreen without changing the original INI.*

키보드 입력은 옵션 없이도 동작한다. 기본 매핑이 실행 파일에 내장되어 있으며 EZ2DJ는 `config/ez2dj-io.example.ini`, EZ2Dancer 2nd MOVE는 `config/ez2dancer-io.example.ini`와 같은 값이다. 바꾸고 싶은 항목만 INI에 적어 `--io-config <path>`로 주면 그 항목만 덮어쓰고, 나머지는 기본값을 유지한다. 키를 끄려면 그 항목에 `NONE`을 적는다. EZ2Dancer의 `coin=F5`는 원본 배선이 확정되지 않은 호환 입력이며, 키를 누를 때마다 `0x304` counter를 1 증가시킨다.

*Keyboard input works with no option: the default mapping is built into the executable and matches `config/ez2dj-io.example.ini` for EZ2DJ and `config/ez2dancer-io.example.ini` for EZ2Dancer 2nd MOVE. Passing `--io-config <path>` overrides only the entries the file lists and leaves the rest at their defaults; write `NONE` for an entry to unbind that key. EZ2Dancer's `coin=F5` is a compatibility mapping because the original cabinet wiring is not confirmed; each press increments the `0x304` counter.*

게임패드도 두 OS에서 옵션 없이 동작한다(작업 444, Windows는 작업 449부터). SDL3가 인식하는 패드는 모두 같은 매핑으로 1P를 치며, 기본값은 예제 INI의 `[gamepad]` 섹션과 같다: EZ2DJ는 십자키 왼쪽·위와 `A` `Y` `B`가 1~5번 키, `X`가 페달, 왼쪽 스틱 좌우가 턴테이블, `START`가 시작, `BACK`이 코인, `LB` `RB` `LT` `RT`가 이펙터 1~4다(#14에서 바꿈). 같은 `--io-config` INI의 `[gamepad]` 섹션에 적어 바꾼다. 이름은 `A` `B` `X` `Y` `BACK` `GUIDE` `START` `LSTICK` `RSTICK` `LB` `RB` `LT` `RT` `DPAD_UP` `DPAD_DOWN` `DPAD_LEFT` `DPAD_RIGHT` `PADDLE1`~`4` `LSTICK_LEFT` `LSTICK_RIGHT` `LSTICK_UP` `LSTICK_DOWN` `RSTICK_*`와 `NONE`이다. 스틱과 트리거는 절반 이상 기울이거나 당겼을 때 눌린 것으로 본다. 한 패드에서 `LT`+`RT`+왼쪽 스틱 클릭+오른쪽 스틱 클릭을 1초 동안 함께 누르고 있으면 게임이 끝나고(런처에서 시작했으면 런처로 돌아옴), 런처에서는 런처가 닫힌다. 키보드가 없는 스팀덱용이며, 그동안에도 각 버튼은 게임에 그대로 들어간다(#20).

*A gamepad works with no option on both OSes too (task 444, Windows from task 449): every pad SDL3 recognises plays player 1 under the same mapping, whose defaults are the example INIs' `[gamepad]` section. For EZ2DJ, the d-pad's left and up and `A` `Y` `B` are keys 1 to 5, `X` the pedal, the left stick's left and right the turntable, `START` start, `BACK` coin, and `LB` `RB` `LT` `RT` effectors 1 to 4 (changed in #14). Change them in the `[gamepad]` section of the same `--io-config` INI; the names are `A` `B` `X` `Y` `BACK` `GUIDE` `START` `LSTICK` `RSTICK` `LB` `RB` `LT` `RT` `DPAD_UP` `DPAD_DOWN` `DPAD_LEFT` `DPAD_RIGHT` `PADDLE1` to `4`, `LSTICK_LEFT` `LSTICK_RIGHT` `LSTICK_UP` `LSTICK_DOWN`, `RSTICK_*` and `NONE`. A stick or trigger counts as pressed past half its travel. Holding `LT`+`RT`+left stick click+right stick click together on one pad for a second ends the game (back to the launcher when it started the game) and, in the launcher, closes it; it is for a Steam Deck with no keyboard, and each control still reaches the game meanwhile (#20).*

```powershell
.\build\windows-x86\bin\Debug\re2dj.exe ez2d2m --io-config .\config\ez2dancer-io.example.ini
```

title/demo 음량은 원본 HDD의 `DemoVolume`을 바꾸지 않고 기본 3(0 dB)으로 답합니다. 최종 출력 보정이 따로 필요할 때만 `--audio-gain-db`를 쓰며 기본값은 0 dB입니다.

*The title and demo volume answers the default 3 (0 dB) without changing the original HDD's `DemoVolume`. Use `--audio-gain-db` only for a separate final-output adjustment; its default is 0 dB.*

제품 CLI의 host 진단은 시작과 동시에 stderr에 출력되고 같은 내용이 실행별 `logs/re2dj-YYYYMMDD-HHMMSS-mmm.log`에 기록됩니다. 모든 메시지는 즉시 flush됩니다. 미구현 HLE와 지원되지 않는 실행 경계는 `[critical]` 레벨과 `FATAL <분류>` marker로 남습니다. 게스트 API 호출은 같은 이름의 `.api.log`에 기록됩니다.

*Product-CLI host diagnostics appear on stderr from startup and are mirrored to a per-run `logs/re2dj-YYYYMMDD-HHMMSS-mmm.log`. Every message is flushed immediately. Unimplemented HLE and unsupported execution boundaries carry `[critical]` severity plus a `FATAL <classification>` marker. Guest API calls go to the `.api.log` of the same name.*

종료 코드: `0` 성공, `1` 잘못된 사용, `2` HDD 디렉터리 오류, `3` 지원되지 않는 실행 경로, `4` 로깅 초기화 실패.

*Exit codes: `0` success, `1` usage error, `2` HDD directory error, `3` unsupported execution path, and `4` logging-initialization failure.*

---

## 프로젝트 구조 / Repository layout

| 경로 | 내용 |
| --- | --- |
| `include/re2dj/`, `src/` | C++20 공용 코어: HDD·CHD 입력, 게스트 경로, PE 판독, 타깃 프로파일 |
| `src/platform/{windows,linux}/` | OS별 backend. 루트는 host 비트 폭 중립/공용, 전용 구현은 `x86/`·`x64/` |
| `src/host/cli/` | 명령행 진입점 |
| `src/tools/` | 비실행 분석 도구 |
| `tests/unit/` | 단위 테스트 |
| `roms/`, `overlays/` | 사용자 제공 ROM과 guest overlay 위치. 각 디렉터리의 0바이트 `dir.txt`만 추적하고 나머지는 Git ignore |
| `logs/` | 실행·분석 로그 출력. 전체 Git ignore |
| `docs/analysis/` | 원본 바이너리와 HDD 자산에서 확인한 분석 |
| `docs/kb/` | PE, Win32, x86 배경 지식 |
| `docs/design/`, `docs/work-orders/`, `docs/work-logs/` | 설계와 작업 이력 |

*The separate `roms/` and `overlays/` directories retain only a tracked zero-byte `dir.txt` placeholder in each; all runtime contents are ignored. The entire `logs/` directory is also ignored.*

자세한 구성은 [ARCHITECTURE.md](ARCHITECTURE.md)를 참고하십시오.

---

## 문서와 지원 / Documentation and support

* [프로젝트 헌장](docs/PROJECT_CHARTER.md) — 목표와 비목표
* [아키텍처](ARCHITECTURE.md) — 현재 subsystem과 실행 구조
* [포팅 계획](docs/WIN32_HLE_PORTING_PLAN.md) — 장기 구현 단계
* [바이너리 분석 색인](docs/analysis/README.md) — 확인된 사실과 미확정 질문
* [EZ2DJ import 표면](docs/analysis/ez2dj-import-surface.md) — 정식 빌드가 실제로 호출하는 Win32 API 집합
* [기술 지식 기반](docs/kb/README.md) — PE, Win32, DirectX, x86 배경
* [코딩 스타일](docs/CODING_STYLE.md) — C++20 스타일과 디렉터리 정책
* [작업 규칙](AGENTS.md) — 설계 우선 개발, 문서화와 Git workflow

질문과 재현 가능한 결함 보고는 [GitHub Issues](https://github.com/reexec/re2DJ/issues)에 남겨 주십시오.

---

## 기여 / Contributing

1. 기존 issue와 [현재 분석 상태](docs/analysis/README.md)를 확인합니다.
2. 동작 변경 전에 `docs/design/`에 설계를, `docs/work-orders/`에 구현 계획을 작성합니다.
3. 원본 실행 코드를 주 경로로 유지하고 게임 로직 재구현을 피합니다.
4. 코드 변경에는 범위에 맞는 테스트와 `docs/work-logs/` 작업 로그를 포함합니다.
5. [코딩 스타일](docs/CODING_STYLE.md)과 [AGENTS.md](AGENTS.md)의 전체 규칙을 확인한 뒤 pull request를 제출합니다.

*Review existing issues and the analysis index, document design and work order before behavioral changes, preserve original executable logic, include appropriate tests and a work log, and follow the coding and repository rules before opening a pull request.*

---

## 관련 프로젝트 / Related project

[rePIU](https://github.com/nworkers/rePIU)는 같은 접근을 DOS/4G 기반 Pump It Up 실행 파일에 적용한 프로젝트입니다. re2DJ는 rePIU의 작업 규칙, 문서 구조, 코딩 스타일을 그대로 이어받되, 게스트 형식(Win32 PE32)과 호스트 범위(멀티플랫폼)가 달라 실행 구조는 다르게 설계했습니다. 차이는 [ARCHITECTURE.md](ARCHITECTURE.md) 9절에 정리했습니다.

*[rePIU](https://github.com/nworkers/rePIU) applies the same approach to DOS/4G-based Pump It Up binaries. re2DJ inherits its workflow rules, documentation structure, and coding style, but its guest format (Win32 PE32) and host scope (multiplatform) lead to a different execution design. Section 9 of [ARCHITECTURE.md](ARCHITECTURE.md) lists the differences.*

---

## 라이선스 / License

프로젝트 코드는 [BSD 3-Clause License](LICENSE)를 따릅니다. 서드파티 구성 요소의 출처와 라이선스는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에, 프로젝트가 기대어 선 게임·프로젝트·사람들에 대한 감사는 [CREDITS.md](CREDITS.md)에 있습니다.

원본 EZ2DJ 실행 파일과 자산은 re2DJ에 포함되지 않으며 각 권리자의 조건을 따릅니다.

*Project code is under the [BSD 3-Clause License](LICENSE). Third-party component origins and licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and acknowledgements in [CREDITS.md](CREDITS.md). Original EZ2DJ binaries and assets are not part of re2DJ and remain subject to their owners' terms.*
