# TCP 소켓 프로토콜 (v1)

C++ 코어(`core/src/net/`)와 Unity 클라이언트 사이의 v1 통신 규격. `CLAUDE.md`에 적힌 대로 v1은 순수 TCP 소켓이고, 프로파일링으로 소켓이 병목으로 확인되면 diff 전송 → 그래도 느리면 네이티브 플러그인 순서로 검토한다 (지금 단계에서 미리 최적화하지 않음).

## 왜 TCP인가 (UDP를 안 쓴 이유)

- **신뢰성이 협상 불가능하다**: HEIGHTMAP은 시뮬레이션 결과 데이터라, 일부가 유실되면 지형 메시가 깨지거나 잘못된 값으로 잘못 해석될 수 있다. 게임의 위치 동기화처럼 "패킷 하나 놓쳐도 다음 업데이트로 덮이니 무시" 할 수 있는 데이터가 아니다.
- **Heightmap 하나가 UDP 데이터그램 한 개 크기를 넘는다**: UDP 데이터그램은 IP 단편화 없이 안전하게 보낼 수 있는 크기가 실질적으로 매우 작다(대략 1400바이트대). 64×64 heightmap만 해도 16KB — UDP로 하려면 직접 여러 데이터그램으로 쪼개고, 순서를 맞춰 재조립하고, 유실분을 재전송하는 로직을 처음부터 짜야 하는데, 그건 TCP가 커널 안에서 이미 검증된 방식으로 해주는 일이다.
- **이 프로토콜은 실시간 스트리밍이 아니라 request-response다**: 매 프레임 계속 쏟아지는 스트림이 아니라 파라미터가 바뀔 때만 한 번씩 동기 요청-응답이 오간다. UDP가 TCP보다 유리한 지점(낮은 지연시간, 약간의 유실/순서 뒤바뀜을 감수하는 대신 헤드오브라인 블로킹 없음)은 실시간 스트리밍에서나 의미가 있는데, 여긴 그런 지연시간 압박이 없다.

요약: UDP의 장점은 이 프로젝트가 필요로 하지 않고, UDP의 단점(신뢰성 없음, 메시지 크기 제한)은 정확히 이 프로젝트가 감당할 수 없는 것들이라 TCP를 안 쓸 이유가 없었다.

## 역할

- **C++ 코어가 서버**, 지정된 포트에서 listen한다. 포트 9000 고정(`main.cpp`의 `kPort`) — `tools/tuner_server.py`가 이미 쓰는 8765와 겹치지 않게 고른 값.
- **Unity가 클라이언트**로 접속한다.
- 연결은 유지한 채로 여러 번의 요청-응답 사이클을 반복한다 (매번 재접속하지 않음).

## 통신 모델: request-response, 응답은 N개의 HEIGHTMAP + 종료 마커

Unity가 PARAMS를 하나 보내면, 코어는 그 요청 하나에 대해 **HEIGHTMAP을 한 번 이상 순서대로 여러 번** 보내고 마지막에 HEIGHTMAP_DONE(또는 실패 시 ERROR)으로 마무리한다. 매 프레임 계속 쏟아지는 진짜 스트리밍은 아니고(Unity가 다음 PARAMS를 보내기 전까진 코어가 먼저 아무것도 안 보냄), 한 번의 요청-응답 "사이클" 안에서 여러 프레임이 오가는 구조다.

```
Unity                          Core
  |── PARAMS (text) ───────────▶|
  |                            | 지형 생성, 침식을 조금씩(batch) 실행
  |◀── HEIGHTMAP (snapshot 1) ─|
  |◀── HEIGHTMAP (snapshot 2) ─|
  |◀── HEIGHTMAP (snapshot N) ─|
  |◀── HEIGHTMAP_DONE ─────────|
  |          (or ERROR, 중간에 실패하면 스냅샷 없이 바로) |
  |                            |
  |── PARAMS (다음 조정) ──────▶|
  |◀── ...                     |
```

### 왜 이렇게 바뀌었나 — 침식 알고리즘 코드는 안 건드리고 애니메이션을 보여주는 법

처음엔 요청당 HEIGHTMAP 한 개(최종 결과만)였는데, 침식이 실제로 "진행되는" 과정을 보고 싶어서 이 구조로 바꿨다. 아래 두 방법은 택하지 않은 이유:

- **클라이언트 쪽 보간(lerp)**: 이전 결과와 새 결과 사이를 Unity에서 시간에 따라 섞는 방법. 프로토콜/코어 변경이 전혀 없어 제일 쉽지만, 실제 물리 과정이 아니라 "모양이 스르륵 바뀌는" 것처럼 보여 침식 시뮬레이션의 포인트(디테일이 실제로 패이는 과정)를 못 보여줌.
- **erosion 함수에 콜백 추가**: `thermalErode`/`dropletErode` 내부에서 매 iteration/droplet마다 콜백을 호출하게 만드는 방법. 진짜 세밀한 애니메이션이 가능하지만, 이 함수들은 `CLAUDE.md`가 명시한 "알고리즘 코드는 사용자가 직접 작성" 대상이라 — 콜백 추가 자체는 물리 로직을 안 바꾸는 계측(instrumentation)에 가깝긴 해도, 그 경계를 굳이 건드릴 이유가 없었음.

**택한 방법**: `main.cpp`가 같은 `Heightmap&`를 두고 erosion 함수를 **여러 번, 조금씩** 호출한다.

- `thermalErode`는 완전히 결정론적(랜덤 없음)이라, `thermalErode(h, talus, rate, 1)`을 N번 연달아 호출하는 것과 `thermalErode(h, talus, rate, N)`을 한 번 호출하는 게 수학적으로 완전히 동일하다. 매 호출 뒤 스냅샷을 보내면 진짜 침식 과정을 한 스텝씩 그대로 보여주는 것.
- `dropletErode`는 seed로 난수를 쓰지만, 매 배치(batch)마다 다른 seed로 소량씩(`numDroplets / snapshotCount`) 호출하면 각 배치는 **그 시점의 실제 지형 위에서** 흐르는 진짜 새 드롭릿들이다. "seed 하나로 이어지는 단일 스트림"은 아니지만, 매 배치가 그 순간의 진짜 물리 시뮬레이션이라는 점은 같다.

결과: `core/src/erosion/`의 함수 시그니처·내부 로직은 **한 줄도 안 바뀐다**. `main.cpp`(배선 코드)가 기존 함수를 반복 호출하는 방식만 바뀐 것.

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
| HEIGHTMAP_DONE | `0x04` | payload 없음(길이 0) | Core → Unity |

`HEIGHTMAP_DONE`은 "이번 PARAMS에 대한 HEIGHTMAP 스트림이 끝났다"는 신호일 뿐이라 payload가 필요 없다 — envelope의 길이 필드가 그냥 0이 된다. Unity는 `HEIGHTMAP_DONE` 또는 `ERROR`를 받을 때까지 `HEIGHTMAP`을 계속 받아들이고, 받을 때마다 메시를 갱신한다(마지막 HEIGHTMAP이 곧 최종 결과이므로 별도 처리 불필요).

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
snapshotCount=12
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

`snapshotCount`(공통 필드, 기본 12)는 이번 요청에서 몇 개의 중간 HEIGHTMAP 스냅샷을 보낼지 정한다 — `thermal`이면 최대 `min(iterations, snapshotCount)`번, `droplet`이면 `numDroplets`를 `snapshotCount`개 배치로 나눠서 그만큼 보낸다.

### HEIGHTMAP

`Heightmap`을 `y*width+x` 순서(`Heightmap`의 기존 flat vector 레이아웃과 동일)로 그대로 직렬화한 raw float32 배열. PGM처럼 0~255로 정규화하지 않고 원본 float 값을 그대로 보낸다 — 정규화는 렌더링 시점에 Unity 쪽에서 필요하면 처리할 문제지, 전송 프로토콜이 값을 손실시킬 이유가 없음.

### ERROR

`sim` 값이 `droplet`/`thermal`이 아니거나, 파싱 실패 등 코어가 요청을 처리 못 했을 때. `tune_cli.cpp`가 이미 알 수 없는 `--sim=`에 대해 stderr + 종료코드 1로 실패하는 것과 같은 상황을 소켓 위에서 표현한 것. 예: `error=unknown sim 'foo' (expected droplet or thermal)`

## 다음 단계

이 문서는 포맷 설계만 다룬다. 실제 소켓 서버 구현(`core/src/net/`)은 파일디스크립터 소유권(RAII)이 걸리는 부분이라 — 스펙에 "Phase 1 net/에서 진짜 소유권 테스트가 온다"고 명시된 대로 — 사용자가 직접 작성한다.
