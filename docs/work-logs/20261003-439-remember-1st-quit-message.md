# 작업 439 작업 로그 — Remember 1st의 WM_QUIT / Task 439 work log — Remember 1st's WM_QUIT

설계: [20261003-439-remember-1st-quit-message.md](../design/20261003-439-remember-1st-quit-message.md) · 지시서: [20261003-439-remember-1st-quit-message.md](../work-orders/20261003-439-remember-1st-quit-message.md)

## 2026-10-03

- **증상**: 작업 438 뒤 사용자 실행에서 1st 자식이 `RestoreDisplayMode`를 지나 정리, 해제, bookkeeping 저장(9회)을 마치고, 338,800번째 호출 `user32!PostQuitMessage(0)`(`0x00414532`)에서 멈췄다. launcher는 `ExitProcess(0)`으로 끝났다(`20261003-114437-248`, `20261003-114502-346`).
  *Symptom: in the user's run after task 438, the 1st child passed `RestoreDisplayMode`, finished cleaning up, releasing and saving bookkeeping (nine writes), and stopped at call 338,800, `user32!PostQuitMessage(0)` (`0x00414532`); the launcher ended with `ExitProcess(0)` (`20261003-114437-248`, `20261003-114502-346`).*
- **원본 확인**: import 표의 `SendMessageA` 호출은 `0x00414635` 한 곳이다. WinMain은 게임 본체 `0x004217f0` 뒤 `SendMessageA(hwnd, WM_DESTROY, 0, 0)`을 보내고, `0x00414550`(`PeekMessageA(PM_REMOVE)`, `WM_QUIT`이면 0)이 0을 돌려줄 때까지 돈 뒤 `[0x00451f58]`을 돌려준다. 창 프로시저 `0x00414520`은 `WM_DESTROY`(2)에서 `PostQuitMessage(0)`을 부른다. 작업 438에서 추정이던 반환값이 이것으로 정적으로 확인됐다.
  *The original: `SendMessageA` is called at one place, `0x00414635`. After the game body `0x004217f0`, WinMain sends `SendMessageA(hwnd, WM_DESTROY, 0, 0)`, loops until `0x00414550` (`PeekMessageA(PM_REMOVE)`, 0 on `WM_QUIT`) returns 0, then returns `[0x00451f58]`. The window procedure `0x00414520` calls `PostQuitMessage(0)` on `WM_DESTROY` (2). This settles statically the return value task 438 inferred.*
- **구현**: `GuestUser::PostQuit`·`quit_posted`·`quit_code`·`ClearQuit`, user32 `PostQuitMessage`(resolve-only에서 구현 export로), `PeekMessageA`의 `WM_QUIT`.
  *Implementation: `GuestUser::PostQuit`, `quit_posted`, `quit_code` and `ClearQuit`; user32 `PostQuitMessage`, moved from resolve-only to an implemented export; `WM_QUIT` from `PeekMessageA`.*
- **검증**: `scripts/test_all.sh linux-x64-debug`(경고를 오류로) build 성공, CTest 4개 통과. `user32_module_test.cpp`에 `PostQuitMessage`·`WM_QUIT` 검사를 더했다(전체 export 수 42는 그대로).
  *Verification: `scripts/test_all.sh linux-x64-debug` (warnings as errors) builds and passes 4 CTest tests; `user32_module_test.cpp` gained the `PostQuitMessage` and `WM_QUIT` checks (the export count stays 42).*
- **사용자 실행 2**: 1st가 `PostQuitMessage`를 지나 `DefWindowProcA(hwnd, WM_DESTROY)`(`0x00414548`)에서 멈췄다(`20261003-114951-658`, `20261003-115022-405`). `DefWindowProcA`에 `WM_DESTROY`(0을 돌려줌)를 더하고 단위 테스트를 더했다.
  *User run 2: 1st passed `PostQuitMessage` and stopped at `DefWindowProcA(hwnd, WM_DESTROY)` (`0x00414548`) (`20261003-114951-658`, `20261003-115022-405`); `DefWindowProcA` gained `WM_DESTROY` (returning 0), with a unit test.*
- **종료 경로 재현**: 사용자 확인을 기다리며 왕복하지 않도록, 직접 띄운 1st 실행에서 정상 종료 경로를 재현했다.
  - 그 실행의 메모리에만 두 가지를 썼다: 게임을 마쳤다는 표시 `[0x01b0c2c4] = 1`, 타이틀 루프 끝 `je 0x0041f73a`(`0x0041f77d`, 2바이트)를 nop. 그러면 타이틀 장면이 끝날 때 1을 돌려준다.
  - 앞선 시도가 실패한 이유는 게스트가 이미 `0x0041f6d0` 안의 타이틀 루프에 있었기 때문이다(마지막 128개 호출의 반환 주소 `0x0041f764`). 그래서 함수 입구를 바꿔도 효과가 없었다.
  - 결과(`20261003-115321-116`): 정리, bookkeeping, `WM_DESTROY`, `PostQuitMessage`, `WM_QUIT`, CRT 종료를 지나 **`ExitProcess(0x00000105)`**. 다만 host 프로세스가 그 뒤 SIGSEGV로 끝났고, [작업 440](20261003-440-linux-exit-teardown.md)에서 다룬다. 종료 코드는 그 전에 pipe로 보내진다.

  *Reproducing the exit path: rather than another round trip to the user, a directly started 1st run was driven down its normal exit path, writing only into that run's memory: the played flag `[0x01b0c2c4] = 1`, and the title loop's closing `je 0x0041f73a` (`0x0041f77d`, two bytes) turned into nops, so the title scene returns 1 when it ends. Earlier tries failed because the guest was already inside `0x0041f6d0`'s title loop (return address `0x0041f764` in the last 128 calls), so changing the function's entry did nothing. Result (`20261003-115321-116`): through the clean-up, bookkeeping, `WM_DESTROY`, `PostQuitMessage`, `WM_QUIT` and the CRT's exit to **`ExitProcess(0x00000105)`**. The host process then ended with SIGSEGV, handled in [task 440](20261003-440-linux-exit-teardown.md); the exit code goes to the pipe before that.*
- **사용자 확인**: 작업 437~440 뒤 Linux에서 6th → Remember 1st → 게임 한 판 → 6th 복귀를 사용자가 확인했다.
  *User check: after tasks 437 to 440 the user went 6th → Remember 1st → one game → back to 6th on Linux.*
- **Windows 사용자 확인(2026-10-11)**: Windows in-process 러너(x86 Debug)에서 6th → Remember 1st → 게임 한 판 → 6th 복귀를 사용자가 확인했다.
  *Windows user check (2026-10-11): the user went 6th → Remember 1st → one game → back to 6th on the Windows in-process runner (x86 Debug).*
