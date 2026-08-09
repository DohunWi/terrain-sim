# Benchmarks

이 디렉터리는 RL 환경 평가와 성능 엔지니어링의 공개 증거를 보관한다.

평가 seed, 실험 운영, 명명 규칙, plot 작성 원칙의 기준 문서는 [`docs/evaluation-protocol.md`](../docs/evaluation-protocol.md)다. 이 README는 디렉터리 역할만 설명하며 규칙을 복제하지 않는다.

```text
benchmarks/
├── evaluations/             # episode별 구조화된 JSON/CSV와 지형 분석 결과
├── experiments/             # 가설·통제변수·결과·결론을 담은 실험 문서
├── plots/                   # 평가 데이터에서 재생성 가능한 그래프
├── EXPERIMENT_TEMPLATE.md   # 정식 실험 기록 템플릿
└── README.md                # 이 인덱스
```

## 작업 시작점

- 새 실험: `EXPERIMENT_TEMPLATE.md`를 복사해 `experiments/<experiment-id>.md` 생성
- 평가 실행: `training/README.md` 참고
- 결과·plot 이름: `docs/evaluation-protocol.md`의 명명 규칙 참고
- 모델과 raw log: `training/artifacts/`에 로컬 보관하고 Git에서 제외

원시 평가 결과, 실험 문서와 plot은 동일한 의미 기반 experiment ID를 공유해야 한다.

## Phase 2d-2 공개 증거

- [`perf-heightmap-at-inline`](experiments/perf-heightmap-at-inline.md): accessor 번역 단위 경계의 C++ 실행 시간 영향
- [`physics-integrator-energy-drift`](experiments/physics-integrator-energy-drift.md): 운영 dt에서 적분기 에너지 drift와 force-evaluation trade-off
- [`cpp-correctness-suite__ctest.txt`](evaluations/cpp-correctness-suite__ctest.txt): candidate commit의 native C++ 16/16 실행 로그
- [`docs/phase2d2-engineering-evidence.md`](../docs/phase2d2-engineering-evidence.md): 하드웨어·표·그래프·해석 제한을 묶은 공개 요약
