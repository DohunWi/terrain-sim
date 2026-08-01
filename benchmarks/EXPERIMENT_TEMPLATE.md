# `<experiment-id>` — `<실험 제목>`

> 이 파일을 직접 채우지 말고 `experiments/<experiment-id>.md`로 복사해서 사용한다. 명명·seed·채택 규칙은 [`docs/evaluation-protocol.md`](../docs/evaluation-protocol.md)를 따른다.

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-XXX` |
| Created | `YYYY-MM-DD` |
| Planned | `YYYY-MM-DD | N/A (retrospective)` |
| Started | `YYYY-MM-DD | —` |
| Completed | `YYYY-MM-DD | —` |
| Predecessor | `<experiment-id> | none` |

## 상태

`Planned | Running | Accepted | Rejected | Inconclusive`

## 관찰

어떤 수치나 trajectory가 이상했는지 적는다.

## 근거

- 관련 baseline 결과:
- 관련 trajectory:
- 핵심 수치:

## 가설

원인과 기대하는 변화 방향을 결과 확인 전에 적는다.

## 독립변수

이번 실험에서 바꾸는 값 하나를 적는다.

## 통제변수

- observation:
- reward:
- physics:
- terrain:
- PPO 설정:
- training steps:
- training seed:
- evaluation seeds:

## 사전 채택 기준

결과를 보기 전에 채택·기각 기준을 수치 또는 명확한 조건으로 적는다.

## 실행 정보

- commit SHA:
- 실행 명령:
- 모델 위치(로컬, Git 제외):
- 평가 결과:

## 결과

| 지표 | Baseline | Candidate | 변화 |
|---|---:|---:|---:|
| Success rate |  |  |  |
| Out-of-bounds rate |  |  |  |
| Timeout rate |  |  |  |
| Median success steps |  |  |  |
| Mean return |  |  |  |

정량 결과와 trajectory에서 관찰한 행동을 함께 적는다.

## 결론

`Accepted | Rejected | Inconclusive` 중 하나를 선택하고 사전 기준에 따라 이유를 적는다.

## 다음 질문

이번 결과로 생긴 가장 중요한 질문 하나만 적는다.
