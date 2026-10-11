# #25 작업 지시서 — in-process 러너의 import 경로와 DirectDraw Clear 비용 줄이기 / #25 work order — cutting the in-process runner's import path and DirectDraw Clear costs

이슈: [#25](https://github.com/reexec/re2DJ/issues/25) · 설계: [20261012-i025-import-path-and-clear-costs.md](../design/20261012-i025-import-path-and-clear-costs.md)

## 절차 / Steps

1. `--vsync on|off|adaptive`를 CLI에서 SDL host 창 설정까지 연결하고 도움말·README에 적습니다.
   *Wire `--vsync on|off|adaptive` from the CLI to the SDL host's window configuration and document it in the help and README.*
2. vsync off 실행을 바깥에서 샘플링 프로파일해 비용이 큰 함수를 찾습니다.
   *Profile a vsync-off run from outside by sampling and find the costly functions.*
3. 게이트 주소 색인, export descriptor 직접 조회, 인자 스택 배열, 관측 경로의 게이트 캐시와 호출 기록 move.
   *The gate-address index, the direct export-descriptor lookup, arguments in a stack array, and the observation path's gate cache and moved call record.*
4. `FillSurface`를 한 번에 채우는 버퍼로.
   *`FillSurface` with a buffer filled in one pass.*
5. 빌드·테스트, 전후 Release 번갈아 처리량 측정, 분석 문서·TODO·IMPLEMENTED 갱신.
   *Builds and tests, throughput measured with the before and after Release builds alternating, and the analysis document, TODO and IMPLEMENTED updated.*

## 완료 조건 / Done when

동작이 바뀌지 않고(테스트·실행 로그), vsync off 처리량이 측정 편차보다 크게 오르며, 모든 타깃 빌드가 통과한다.

*Behaviour is unchanged (tests and run logs), vsync-off throughput rises by more than the measuring noise, and every target's build passes.*
