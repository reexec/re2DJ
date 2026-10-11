# 작업 430 작업 로그 — DX7 조명과 깊이 기본값 / Task 430 work log — DX7 lighting and depth defaults

설계: [20260930-430-d3d7-lighting-defaults.md](../design/20260930-430-d3d7-lighting-defaults.md) · 지시서: [20260930-430-d3d7-lighting-defaults.md](../work-orders/20260930-430-d3d7-lighting-defaults.md)

## 2026-09-30

- **작업 전 Win32 6th 상태**
  - 데모, 코인(3코인 1크레딧), 1P 시작, 모드 선택, 곡 선택, 플레이까지 진행했다.
  - 모드 선택 화면에서 모드별 그림만 빠졌다(사용자 보고, 사용자 확인).
  - 첫 실행에서는 시작 직후 화면이 어두웠는데, 이는 화면 전환 중에 캡처된 것이었다.

  *Win32 6th before the task:*
  - *It got through the demo, coins (three coins per credit), 1P start, mode select, music select, and play.*
  - *Only the per-mode pictures were missing from mode select (user report, confirmed with the user).*
  - *A dark screen right after start in the first run was a capture taken mid-transition.*
- **진단 실행**: launcher probe에 제품과 같은 옵션과 `--graphics-draw-diagnostics`를 주어 실행했다(`20260930-014820-412`). 모드 선택 구간에서 확인한 내용은 다음과 같다.
  - `DrawPrimitive` 12,743건 가운데 텍스처 241~273(`fvf=0x112`)만 `unsupported Direct3D3 depth comparison function`으로 실패했다.
  - 게임은 `ZENABLE` 0/1, `LIGHTING` 1, `AMBIENT` `0xffffffff`를 설정하고 `ZFUNC`는 설정하지 않았다.
  - 미구현 기록은 `IDirect3DDevice7::SetMaterial` 하나뿐이었다.

  *A diagnostic run: the launcher probe with the product's options plus `--graphics-draw-diagnostics` (`20260930-014820-412`). In mode select:*
  - *Of 12,743 `DrawPrimitive` calls, only textures 241–273 (`fvf=0x112`) failed, with `unsupported Direct3D3 depth comparison function`.*
  - *The game set `ZENABLE` 0/1, `LIGHTING` 1, and `AMBIENT` `0xffffffff`, but never `ZFUNC`.*
  - *The only unimplemented call recorded was `IDirect3DDevice7::SetMaterial`.*
- **측정**: `rs430.exe`로 새 장치 상태, `lit430.exe`로 광원 없는 조명 색을 측정했다. 결과는 설계의 표와 같다.
  *Measured with `rs430.exe` (a new device's state) and `lit430.exe` (the colour lighting gives with no light); results as in the design's tables.*
- **구현**
  - 공용 core: `InitialDeviceState`의 측정 초기값, `InitialDevice7State`, `UntransformedVertexColor`, `LegacyTransformState::vertex_color`.
  - Windows: DX7 장치 초기화, `SetMaterial`·`GetMaterial`, interop `LegacyDeviceSetMaterial`·`LegacyDeviceGetMaterial`.
  - Linux: DX7 장치 초기화, `GetMaterial`.

  *Implementation:*
  - *The shared core: the measured initial values in `InitialDeviceState`, `InitialDevice7State`, `UntransformedVertexColor`, and `LegacyTransformState::vertex_color`.*
  - *Windows: DX7 device initialisation, `SetMaterial` and `GetMaterial`, and the interop `LegacyDeviceSetMaterial` and `LegacyDeviceGetMaterial`.*
  - *Linux: DX7 device initialisation and `GetMaterial`.*
- **실행 확인**
  - Win32 6th: 모드 선택 화면의 가운데에 Ruby Mix, 왼쪽에 Remember 1st, 오른쪽에 Street Mix 로고가 나온다.
  - Windows 4th·5th: 타이틀 화면이 정상이다(각 30초·45초).

  *Runs:*
  - *Win32 6th: mode select now shows the Ruby Mix logo in the middle, Remember 1st on the left, and Street Mix on the right.*
  - *Windows 4th and 5th: their title screens are fine (30 and 45 seconds).*
- **남은 관찰**: 데모 플레이의 BGA 자리에 색 노이즈가 보인다. 원본 연출인지는 확인하지 못했다(TODO).
  *Still open: colour noise appears in the demo's BGA area; whether it is the original's own effect is unconfirmed (TODO).*
- **테스트 결과**(실패 0):
  - Windows x86: CTest 6개 통과, 단위 5,680 checks.
  - Linux x64·x86: CTest 4개 통과, 단위 5,677 checks.

  *Test results (no failures):*
  - *Windows x86: all 6 CTest tests pass, 5,680 unit checks.*
  - *Linux x64 and x86: all 4 CTest tests pass, 5,677 unit checks.*

## 2026-10-11 — 데모 BGA 확인 / Demo BGA check

- Windows 실행에서 데모 BGA가 아예 나오지 않았다. API 기록에서 데모 중 `BG\` 폴더를 한 번도 열지 않았다.
- 원본은 BGA를 불러오기 전에 프로파일의 `UseBackground`를 읽는다(`0x004187e2`, `0x00412240`). 키가 없으면 값을 쓰지 않아 지역 변수 `[esp+4]`가 스택에 남은 값 그대로다. 그래서 키가 없으면 BGA 여부가 우연에 달린다.
- 당시 `overlays/ez2dj6th/EZ2DJ.ini`에는 `UseBackground`·`UseGameOver`·`FullScreen`·`TestSongName` 등이 빠져 있었다(예전 실행이 기본값으로 다시 저장한 것으로 보임). 게스트 메모리의 KProfile 항목을 읽어, overlay 없이 CHD 원본 INI를 쓰면 `UseBackground = 1`임을 확인했다.
- 사용자 동의로 overlay INI를 지운 뒤 사용자가 데모 BGA가 나오고 상태가 괜찮음을 확인했다. Linux에서 보였던 색 노이즈는 곡 BG 폴더의 `noise0000~0004.abm` 같은 원본 연출로 보고 TODO에서 닫는다.

  *2026-10-11, demo BGA check: on Windows the demo showed no BGA at all, and the API trace never opened the `BG\` folder. Before loading a BGA the original reads `UseBackground` from its profile (`0x004187e2`, `0x00412240`); a missing key leaves the local `[esp+4]` holding whatever the stack had, so without the key the BGA is a matter of chance. `overlays/ez2dj6th/EZ2DJ.ini` lacked `UseBackground`, `UseGameOver`, `FullScreen`, `TestSongName` and others (apparently re-saved with defaults by an earlier run); reading the KProfile entries in guest memory showed `UseBackground = 1` with the CHD's own INI. With the user's agreement the overlay INI was removed, and the user confirmed the demo BGA shows and looks right. The colour noise seen on Linux is taken as the original's effect (the song's BG folder carries `noise0000` to `noise0004.abm`) and closed in the TODO.*
