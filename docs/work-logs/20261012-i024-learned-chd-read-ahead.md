# #24 작업 로그 — 읽었던 CHD 경로를 기억해 다음 실행에서 미리 읽기 / #24 work log — remembering the CHD paths read and reading them ahead in later runs

설계: [20261012-i024-learned-chd-read-ahead.md](../design/20261012-i024-learned-chd-read-ahead.md) · 지시서: [20261012-i024-learned-chd-read-ahead.md](../work-orders/20261012-i024-learned-chd-read-ahead.md)

## 2026-10-11~12 — 측정 방법 / How it was measured

- 환경: Windows 11, Release x86, `roms/ez2dj6th/6th.chd`(4.4GB, 4KB hunk, LZMA)가 E: HDD(WD Red 4TB, 순차 읽기 약 190MB/s). 관리자 권한은 없다.
- 캐시 비우기: 다른 handle이 없을 때 CHD를 `FILE_FLAG_NO_BUFFERING`으로 한 번 열면 Windows가 그 파일의 캐시를 비운다. 그 뒤 다른 CHD(`ez2dj5.chd`)에서 512MB를 읽어 드라이브 자체 캐시를 밀어냈다. 확인: 무작위 64KB 읽기 40번의 중앙값이 비우기 전 0.01ms, 비운 뒤 16.5ms(처음 읽을 때와 같음).
- 실행: 비운 직후 `re2dj ez2dj6th`를 150초(또는 100초) 띄우고 끝냈다. 이 사이에 로고·타이틀과 어트랙트 데모 두 곡(6curseit — StreetMix, 6refresh — 7StreetMix)의 로딩이 들어간다.
- 임시 계측(커밋하지 않음): 게스트의 source 호출마다 걸린 시간과 그동안의 hunk 캐시 실패 수·시간을 로그에 남겼고, 1초 넘게 끊긴 호출 묶음을 한 로딩으로 묶었다. 아래 "로딩"은 그 묶음의 처음부터 끝까지, "I/O"는 그 안의 source 호출 시간 합이다.

  *Environment: Windows 11, Release x86, `roms/ez2dj6th/6th.chd` (4.4 GB, 4 KB hunks, LZMA) on the E: HDD (WD Red 4 TB, about 190 MB/s sequential), no administrator rights. Emptying the caches: a `FILE_FLAG_NO_BUFFERING` open of the CHD with no other handle makes Windows drop the file's cached pages; 512 MB read from another CHD (`ez2dj5.chd`) then pushes it out of the drive's own cache (40 random 64 KB reads: median 0.01 ms before, 16.5 ms after, as on a first read). Runs: `re2dj ez2dj6th` started right after, for 150 s (or 100 s), covering the logo, the title and the loading of two attract-demo songs (6curseit in StreetMix, 6refresh in 7StreetMix). Temporary instrumentation (not committed) logged each guest source call's time and the hunk-cache misses during it; calls with gaps under 1 s form one load. "Load" below is such a group from start to end, "I/O" the source-call time within it.*

## 기준 / Baseline

- 기록 없음(#24 이전과 같음) 여섯 번: 첫 곡 2.07~2.29초(I/O 1.78~1.98초), 둘째 곡 0.97~1.45초(I/O 0.69~0.98초).
- 첫 곡 I/O를 폴더별로 나누면 `StreetMix/Panel` 402ms, `BG/6curseit` 400ms, 경로 조회 398ms(처음 보는 디렉터리 항목. `SOUND/6curseit` 조회 하나가 hunk 81개로 179ms), `SOUND/6curseit` 196ms, `RubyMix/Panel` 165ms, `InGameEffector` 140ms, `StreetMix/Panel/6th` 89ms, `ranking` 61ms였다. 곡마다 다른 것은 BG와 SOUND뿐이다.

  *No journal (as before #24), six runs: first song 2.07 to 2.29 s (I/O 1.78 to 1.98 s), second 0.97 to 1.45 s (I/O 0.69 to 0.98 s). The first song's I/O by directory: `StreetMix/Panel` 402 ms, `BG/6curseit` 400 ms, path lookups 398 ms (first-seen directory entries; the `SOUND/6curseit` lookup alone was 81 hunks, 179 ms), `SOUND/6curseit` 196 ms, `RubyMix/Panel` 165 ms, `InGameEffector` 140 ms, `StreetMix/Panel/6th` 89 ms, `ranking` 61 ms. Only BG and SOUND differ per song.*

## 폴더 통째 미리 읽기 실험(넣지 않음) / Reading whole directories ahead (left out)

게임이 폴더의 파일을 처음 열 때 그 폴더 파일들의 hunk를 미리 읽는 방법을 여러 형태로 시험했다(패치는 저장소 밖에 보관).

| 형태 | 첫 곡 로딩 | 둘째 곡 로딩 | 관찰 |
|---|---|---|---|
| #9 작업 스레드 FIFO, hunk 압축 해제 | 2.29초 | 0.97초 | `SOUND/6curseit` 25MB에 1.6초. 큰 배경음 파일 뒤에 줄을 서서 `BG`가 게임보다 늦음 |
| 두 번째 libchdr handle로 hunk 위치만 찾아 1MB씩 순차 읽기 | 3.19초 | 1.26초 | `SOUND` 0.35초로 빨라졌지만 큰 `Panel` 폴더(16~26MB)와 HDD를 다툼 |
| 전용 스레드, 가장 최근 폴더 먼저, 새 폴더가 오면 멈춤 | 약 3.8초 | 1.45초 | 게임·폴더·배경음 세 흐름이 HDD에서 다툼 |
| 게임 스레드에서 동기로 | 약 3.0초 | 1.38초 | 게임 I/O는 줄었으나 폴더 읽기 시간이 더 큼 |
| 동기, CHD 안 8조각 이하 폴더만 | 2.07초 | 1.28초 | 측정 편차 안 |
| 백그라운드, 8조각 이하 폴더만(두 번씩) | 2.06·2.19초 | — | 같은 조건의 기록 없음은 2.11·2.07초 |

- 원인: 폴더의 압축 hunk가 CHD 파일 안에서 흩어져 있다. `SOUND/6curseit`는 17MB가 5조각, `BG/6curseit`는 2.9MB가 6조각으로 거의 한 덩어리지만, `StreetMix/Panel`(13.6MB)은 34조각, `RubyMix/Panel`(21MB)은 47조각, `ranking`(23MB)은 27조각, `System/Title`(9.8MB)은 12조각이다. 게임은 그런 폴더에서 일부만 읽으므로 통째로 읽으면 낭비이고, 한 덩어리인 폴더는 원래 I/O가 크지 않다.
- 위치 찾기에 쓴 두 번째 handle은 6th CHD에서 hunk map 메모리가 크다(#9 설계). 이 방향은 넣지 않는다.

  *Reading a directory's hunks ahead when the game first opens a file in it was tried in several forms (the patch is kept outside the repository): on #9's FIFO worker, decompressing hunks (2.29 s / 0.97 s; 1.6 s for `SOUND/6curseit`'s 25 MB, and `BG` queued behind a large music file, later than the game); locating hunks through a second libchdr handle and reading the file 1 MB at a time (3.19 s / 1.26 s; `SOUND` down to 0.35 s but the large `Panel` directories, 16 to 26 MB, fought the game for the HDD); a dedicated thread taking the newest directory first and setting older ones aside (about 3.8 s / 1.45 s; three streams on one HDD); synchronously on the game's thread (about 3.0 s / 1.38 s; less game I/O, more directory reading); synchronously, only directories in at most 8 pieces of the CHD (2.07 s / 1.28 s, within the noise); in the background, the same limit, twice (2.06 and 2.19 s against 2.11 and 2.07 s without). The cause: a directory's compressed hunks lie scattered in the CHD file. `SOUND/6curseit` (17 MB in 5 pieces) and `BG/6curseit` (2.9 MB in 6) are nearly contiguous, but `StreetMix/Panel` (13.6 MB) is 34 pieces, `RubyMix/Panel` (21 MB) 47, `ranking` (23 MB) 27, `System/Title` (9.8 MB) 12. The game reads part of such directories, so reading them whole is waste, while the contiguous ones never cost much I/O. The second handle used for locating carries a large hunk map on 6th's CHD (#9 design). None of this goes in.*

## 공통 부분 실험 / The common part

- 기준 실행의 첫 곡 로딩에서 곡 폴더(`BG/6curseit`, `SOUND/6curseit`)를 뺀 게스트 호출 237개를 목록으로 만들어, 시작 15초 뒤 백그라운드에서 읽게 했다(임시). 434~489ms에 읽혔고, 첫 곡 로딩이 0.99·0.96초(I/O 0.88·0.81초)로 줄었다.

  *The 237 guest calls of a baseline first-song load outside the song's directories, read in the background 15 s after start (temporary): read in 434 to 489 ms, and the first song load fell to 0.99 and 0.96 s (I/O 0.88 and 0.81 s).*

## 구현 / Implementation

- `include/re2dj/hle/guest_file_read_journal.h`, `src/hle/guest_file_read_journal.cpp`: 항목(종류·경로·`extent`·`uses`), `Record`(10초 간격 규칙), `Load`·`Save`, `WarmList`(2회 이상, 64MiB, 기록 순서), `MergeInto`·`MarkSaved`, 8,192개 상한.
- `GuestFilePrefetcher`: `Learn`, `Record`, `WaitWarmed`, `SaveJournal`, 학습 작업 스레드 `RunLearned`(조각마다 `WaitForQuiet`), 15초마다·소멸자에서 저장(디스크 기록에 합쳐 `.tmp` 뒤 이름 바꾸기, overlay가 없으면 건너뜀).
- `GuestFiles`: `ForegroundSource`의 `Find`·`ReadRange`·`ListDirectory`가 기록, `Configure`가 `overlay/.re2dj-readahead`로 `Learn`, `WaitForPrefetch`가 학습한 읽기도 기다림. `kReadJournalName`.
- 테스트: `guest_file_read_journal_test.cpp`(5개 묶음), `guest_file_prefetcher_test.cpp`의 `CheckLearnedReadAhead`.

  *The journal (`guest_file_read_journal.{h,cpp}`): entries (kind, path, `extent`, `uses`), `Record` with the 10 s rule, `Load` and `Save`, `WarmList` (twice or more, 64 MiB, journal order), `MergeInto` and `MarkSaved`, the 8,192-entry limit. The prefetcher: `Learn`, `Record`, `WaitWarmed`, `SaveJournal`, the learning worker `RunLearned` (`WaitForQuiet` per piece), saving every 15 s and in the destructor (merged into the journal on disk, written to `.tmp` and renamed, skipped without an overlay). `GuestFiles`: `ForegroundSource` records `Find`, `ReadRange` and `ListDirectory`, `Configure` calls `Learn` with `overlay/.re2dj-readahead`, `WaitForPrefetch` also waits for the learned reads; `kReadJournalName`. Tests: `guest_file_read_journal_test.cpp` (five groups) and `CheckLearnedReadAhead` in `guest_file_prefetcher_test.cpp`.*

## 검증 / Verification

- Windows x86 Debug(MSVC): 전체 빌드 성공, `re2dj_unit_tests` checks 6,438, failures 0, CTest 4개 통과. Release 빌드 성공.
- WSL Linux `linux-x64-debug`·`linux-x86-debug`·clang(`build/linux-x64-clang`, 모두 경고를 오류로): 빌드 성공, CTest 각 5개 통과.
- 실제 실행(위 방법, 임시 계측을 얹은 Release):

  | 실행 | 미리 읽기 | 첫 곡 로딩(I/O) | 둘째 곡 로딩(I/O) |
  |---|---|---|---|
  | 1 — 기록 없음 | 없음 | 2.17초(1.83초) | 1.33초(0.98초) |
  | 2 — 1번 실행의 기록 | 483개 경로 10.5MB, 912ms | 1.84초(1.56초) | 1.24초(0.82초) |
  | 3 — 1·2번 실행의 기록 | 1,701개 경로 42MB, 2,718ms | 0.58초(0.29초) | 0.65초(0.31초) |

  2번과 3번에서 로고·타이틀 로딩은 디스크를 기다리지 않았다(캐시 실패가 있는 호출이 남지 않음). 2번의 효과가 작은 것은 데모의 두 곡이 서로 다른 모드라서, 첫 곡의 패널 파일이 1번 실행에서 한 번만 쓰였기 때문이다. 3번은 데모가 매번 같은 곡을 돌아 곡 전용 파일까지 두 번 쓰였으므로, 같은 모드로 처음 고르는 곡을 할 때보다 유리한 경우다. 1번 실행 뒤 기록은 1,701줄(조회 860, 읽기 841)이었다.

  *Windows x86 Debug (MSVC): full build, `re2dj_unit_tests` 6,438 checks with 0 failures, 4 CTest tests passed; the Release build succeeds. WSL Linux `linux-x64-debug`, `linux-x86-debug` and clang (`build/linux-x64-clang`), all with warnings as errors: build and pass 5 CTest tests each. Real runs (the method above, Release with the temporary instrumentation): run 1, no journal: first song 2.17 s (I/O 1.83 s), second 1.33 s (0.98 s); run 2, run 1's journal, 483 paths, 10.5 MB read ahead in 912 ms: 1.84 s (1.56 s) and 1.24 s (0.82 s); run 3, runs 1 and 2's journal, 1,701 paths, 42 MB in 2,718 ms: 0.58 s (0.29 s) and 0.65 s (0.31 s). In runs 2 and 3 the logo and title loads no longer waited on the disk. Run 2 gains little because the demo's two songs use different modes, so the first song's panel files were used once in run 1; run 3 is the favourable case, the demo playing the same songs each time so that even the song-only files were used twice, unlike a song picked for the first time in the same mode. After run 1 the journal held 1,701 lines (860 lookups, 841 reads).*
