# #24 작업 지시서 — 읽었던 CHD 경로를 기억해 다음 실행에서 미리 읽기 / #24 work order — remembering the CHD paths read and reading them ahead in later runs

이슈: [#24](https://github.com/reexec/re2DJ/issues/24) · 설계: [20261012-i024-learned-chd-read-ahead.md](../design/20261012-i024-learned-chd-read-ahead.md)

## 절차 / Steps

1. HDD에서 캐시 없는 곡 로딩을 재는 방법을 정하고(Windows 파일 캐시와 드라이브 캐시 비우기), 곡 로딩의 I/O를 폴더별로 나눕니다.
   *Settle how to time a cold song load from the HDD (emptying the Windows file cache and the drive's cache) and split a song load's I/O by directory.*
2. 폴더 통째 미리 읽기를 실험합니다(백그라운드·동기, 조각 수 제한). 효과가 없으면 넣지 않고 결과만 남깁니다.
   *Try reading whole directories ahead (in the background and synchronously, with a limit on scattered pieces); if it does not help, leave it out and record the results.*
3. `guest_file_read_journal.{h,cpp}`: 기록, 사용 횟수, 저장·읽기, `WarmList`, `MergeInto`.
   *The journal: recording, use counts, saving and loading, `WarmList`, `MergeInto`.*
4. `GuestFilePrefetcher`: `Learn`, `Record`, 학습 작업 스레드(게스트가 한가할 때 미리 읽기, 15초마다·종료 때 합쳐 저장). `GuestFiles`: `ForegroundSource`의 기록, `Configure`에서 `Learn`.
   *The prefetcher's `Learn`, `Record` and learning worker (reading ahead while the guest is idle, merging and saving every 15 s and at the end); in `GuestFiles`, recording in `ForegroundSource` and `Learn` from `Configure`.*
5. 단위 테스트, `ARCHITECTURE.md`·`TODO.md`·`IMPLEMENTED.md` 갱신.
   *Unit tests; update `ARCHITECTURE.md`, `TODO.md` and `IMPLEMENTED.md`.*
6. Windows·WSL Linux 빌드와 CTest, 캐시를 비운 실제 6th 실행으로 기록 없음·한 실행·두 실행 분 기록을 비교합니다.
   *Build and run CTest on Windows and WSL Linux, and compare no journal, one run's and two runs' journal in real 6th runs with the caches emptied.*

## 완료 조건 / Done when

곡 로딩의 공통 부분이 이전 실행의 기록으로 미리 읽혀 캐시 없는 곡 로딩이 줄고, 기록이 동시에 도는 두 프로세스에서도 합쳐 저장되며, 단위 테스트와 모든 타깃 빌드가 통과한다.

*The common part of a song load is read ahead from earlier runs' journal so that a cold song load shrinks, the journal is merged and saved even with two processes running at once, and the unit tests and every target's build pass.*
