# TCP 소켓 프로토콜 (v1)

C++ 코어(`core/src/net/`)와 Unity 클라이언트 사이의 v1 통신 규격. `CLAUDE.md`에 적힌 대로 v1은 순수 TCP 소켓이고, 프로파일링으로 소켓이 병목으로 확인되면 diff 전송 → 그래도 느리면 네이티브 플러그인 순서로 검토한다 (지금 단계에서 미리 최적화하지 않음).

## 역할

- **C++ 코어가 서버**, 지정된 포트에서 listen한다.
- **Unity가 클라이언트**로 접속한다.
- 연결은 유지한 채로 여러 번의 요청-응답 사이클을 반복한다 (매번 재접속하지 않음).

## 통신 모델: request-response

침식마다 계속 스트리밍하지 않고, Unity가 파라미터를 보낼 때만 코어가 한 번 시뮬레이션을 동기 실행하고 결과를 돌려준다.

```
Unity                          Core
  |── PARAMS (text) ───────────▶|
  |                            | 지형 생성 + 침식 실행 (동기)
  |◀── HEIGHTMAP (binary) ─────|
  |          (or ERROR)        |
  |                            |
  |── PARAMS (다음 조정) ──────▶|
  |◀── HEIGHTMAP ───────────────|
  ...
```

파라미터 슬라이더를 만질 때마다 새 PARAMS를 보내고, 그때마다 전체 heightmap을 다시 받는 식. 처음부터 스트리밍/증분 갱신으로 가지 않는 이유는 v1을 최대한 단순하게 유지해서, 나중에 실제로 느릴 때 "측정 → 병목 특정 → 개선" 과정을 documenting하기 위함 (스펙 §8 리스크 대응 순서와 동일한 논리).

## 메시지 envelope (공통)

```
[4바이트 payload 길이, little-endian uint32][1바이트 메시지 타입][payload]
```

- **little-endian 고정**: 개발 머신(Mac arm64)과 Unity 빌드 타겟 모두 little-endian이라 byte-swap 로직을 지금 단계에서 일반화하지 않음. big-endian 타겟이 실제로 필요해지면 그때 대응.
- 길이 필드는 payload 바이트 수만 카운트한다 (5바이트 헤더 자체는 제외).

## 메시지 타입

| 타입 | 값 | payload 형식 | 방향 |
|---|---|---|---|
| PARAMS | `0x01` | UTF-8 텍스트, `key=value` 줄바꿈 구분 | Unity → Core |
| HEIGHTMAP | `0x02` | `[4바이트 width][4바이트 height][width×height×4바이트 float32, row-major]` | Core → Unity |
| ERROR | `0x03` | UTF-8 텍스트, `error=<메시지>` 한 줄 | Core → Unity |

### PARAMS: 왜 JSON이 아니라 `key=value`인가

처음엔 JSON으로 설계했다가 바꿨다. PARAMS는 항상 **중첩 없는 flat key-value 뭉치**뿐이라 (객체 중첩, 배열, 유니코드 이스케이프 같은 JSON의 일반성이 전혀 필요 없음), 이 형태에 진짜 JSON 파서(+ 그걸 위한 외부 라이브러리)를 쓰는 건 과한 도구였다. 스펙이 명시한 이 프로젝트의 차별점(소켓 아키텍처/물리 엔진/성능 엔지니어링 프로세스) 중 어디에도 "JSON 파싱"은 없어서, 여기 시간 쓰는 것보다 `core/src/tune_cli.cpp`의 `--key=value` 인자 파싱과 같은 포맷을 재사용하는 쪽을 택함 — 파서도 각 줄을 `=` 기준으로 나누기만 하면 되니 라이브러리 없이 직접 짜기 부담 없음.

필드 이름은 `tune_cli.cpp` / `tools/tuner_server.py`에서 이미 검증한 것과 동일하게 맞춘다 — 로컬 튜닝 도구와 실제 와이어 프로토콜의 파라미터 이름이 갈라질 이유가 없음.

공통 지형 생성 필드 + `sim`별 필드, 한 줄에 하나씩 `key=value`, `\n`으로 구분:

```
sim=droplet
width=64
height=64
terrainSeed=42
scale=10.0
octaves=3
persistence=0.5
lacunarity=2.0
numDroplets=700
dropletSeed=42
inertia=0.3
minSlope=0.01
capacityFactor=4.0
erosionFactor=0.3
depositFactor=0.3
gravity=4.0
evaporateRate=0.02
waterThreshold=0.01
maxLifeTime=25
```

`sim=thermal`이면 `numDroplets`~`maxLifeTime` 대신 `talusAngle`, `erosionRate`, `iterations`가 들어간다 (`tuner_server.py`의 `SIM_PARAMS["thermal"]`과 동일).

### HEIGHTMAP

`Heightmap`을 `y*width+x` 순서(`Heightmap`의 기존 flat vector 레이아웃과 동일)로 그대로 직렬화한 raw float32 배열. PGM처럼 0~255로 정규화하지 않고 원본 float 값을 그대로 보낸다 — 정규화는 렌더링 시점에 Unity 쪽에서 필요하면 처리할 문제지, 전송 프로토콜이 값을 손실시킬 이유가 없음.

### ERROR

`sim` 값이 `droplet`/`thermal`이 아니거나, 파싱 실패 등 코어가 요청을 처리 못 했을 때. `tune_cli.cpp`가 이미 알 수 없는 `--sim=`에 대해 stderr + 종료코드 1로 실패하는 것과 같은 상황을 소켓 위에서 표현한 것. 예: `error=unknown sim 'foo' (expected droplet or thermal)`

## 다음 단계

이 문서는 포맷 설계만 다룬다. 실제 소켓 서버 구현(`core/src/net/`)은 파일디스크립터 소유권(RAII)이 걸리는 부분이라 — 스펙에 "Phase 1 net/에서 진짜 소유권 테스트가 온다"고 명시된 대로 — 사용자가 직접 작성한다.
