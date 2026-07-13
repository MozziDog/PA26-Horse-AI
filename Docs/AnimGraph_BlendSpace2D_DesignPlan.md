# AnimGraph 2D Blend Space (삼각분할 포함) — 설계 & 구현 계획

> 작성일: 2026-07-13 · 대상: AppleJamEngine (자체 엔진, UE 유사 AnimGraph) · 브랜치: `feature/horse-animation`
> 사이클: **설계 확정 대기 (구현 착수 전, 검토 1회 예정)**
> 성격: **여러 머신을 오가며 진행하는 living document.** 각 태스크 완료 시 해당 체크박스를 `[x]`로 갱신하고, 필요 시 "진행 로그"에 한 줄 남긴다.
> 표기: **[사실]** = 코드로 확인된 현황(`파일:라인` 근거) · **[결정]** = 확정 설계(재논의 금지) · **[전제]** = 선행 의존/리스크 · **[열림]** = 검토에서 정할 항목.

---

## 0. 결론 요약 (먼저 읽을 것)

- **무엇을 만드나:** AnimGraph에 **2D Blend Space 노드**(`FAnimNode_BlendSpace`)를 신설. 임의 산점 샘플을 **Delaunay 삼각분할 + 무게중심(barycentric) 가중**으로 연속 블렌드. **1D는 별도 구현 없이 "공선/2샘플 퇴화 케이스"로 흡수.**
- **핵심 통합은 이미 저렴:** State가 `UAnimState::SubGraphOverride`([AnimState.h:61](../AppleJamEngine/Source/Engine/Animation/StateMachine/AnimState.h#L61))라는 **임의 `FAnimNode_Base*` 위임 훅**을 이미 보유. 여기에 BlendSpace 노드를 꽂으면 시간 진행·pose 평가·root motion을 전부 위임. → State Machine 통합 = **딱 2곳 타입 제한 완화**.
- **가장 큰 장애(위상 동기)는 회피:** "**동일 보법 내 클립 길이 동일**" 전제 덕분에 보법 내 방향 블렌드는 sync 인프라 없이 성립. 보법 전환은 기존 StateMachine 축에 맡긴다.
- **규모:** 삼각분할 포함 2D 엔진 기능 ≈ **~3.5~4.5 man-with-Claude day** (콘텐츠 저작·튜닝 제외). 압축이 안 되는 바닥은 **에디터 UX**(§6 참조).
- **작업 단위:** 기능 단위가 아니라 **빌드가 가능하고 테스트(회귀/신규)로 끝나는 단위**로 묶음(§4). 말 locomotion 콘텐츠 저작은 본 계획 범위 밖.

---

## 1. 현황 [사실]

현재 AnimGraph 노드 타입([AnimGraphTypes.h:29-39](../AppleJamEngine/Source/Engine/Animation/Graph/AnimGraphTypes.h#L29-L39)): `OutputPose · SequencePlayer · StateMachine · Slot · LayeredBlendPerBone · BlendListByEnum · VariableGet · RefPose`. **BlendSpace 없음.**

- 가장 근접한 `BlendListByEnum`은 **이산 선택**(Float Selector → `(int)floor+clamp`), 연속 가중 아님. 입력 A/B 2개 하드코딩. → blend space 아님. 단, **노드 lifecycle·RM 외부누적·컴파일러 패턴의 템플릿**으로 재사용.
- 2-pose 가중 블렌드 `FAnimationRuntime::BlendTwoPosesTogether(A,B,Alpha,Out)` **존재** ([AnimationRuntime.h:17](../AppleJamEngine/Source/Engine/Animation/AnimationRuntime.h#L17)). N-pose는 이걸 합성.
- 연속 float 입력 경로(`FAnimGraphRuntimeVariable` + `GetGraphVariableAsFloat` + `MakeFloatReader`) **존재** — 축값 공급에 그대로 사용.
- State는 `SequencePath`(단일 시퀀스) 또는 `SubGraphNodeId`(현재 StateMachine 노드만 허용) 재생. `SubGraphOverride`는 임의 노드 수용하나 컴파일러가 타입을 StateMachine으로 제한([AnimGraphCompiler.cpp:403](../AppleJamEngine/Source/Engine/Animation/Graph/AnimGraphCompiler.cpp#L403)).

---

## 2. 설계 결정 [결정]

1. **독립 asset 아님 → 그래프 노드 내장 샘플 리스트.** UE식 `UBlendSpace` asset(신규 UObject+Factory+ContentBrowser+전용 에디터창)은 +2~3일. 이 엔진은 "AnimGraph가 곧 asset"이라 노드에 샘플을 내장하면 90% 가치를 저렴하게 확보. 교차-그래프 재사용이 실제 필요해지면 그때 asset으로 승격.
2. **2D 단일 노드, 1D는 퇴화 케이스.** `FAnimNode_BlendSpace` 하나만 구현. 샘플이 공선(collinear)·2개 이하면 1D bracket-lerp로 자동 fallback. 노드 두 벌 안 만듦.
3. **가중치 = Delaunay 삼각분할 + barycentric.** 임의 산점 지원. hull 밖 질의는 최근접 edge/vertex 투영. 삼각망은 Initialize 시 1회 build·캐시.
4. **radial은 데이터로 처리.** 방향 wrap은 -1/+1(혹은 대칭 좌표)에 같은 클립을 배치해 해결. 코드에 wrap 로직 없음.
5. **sync 회피 원칙:** blend space의 두 축은 **보법 내(길이 동일) 변수만**(예: X=좌우 strafe, Y=전/후). **보법 전환(길이 상이)은 blend space가 아니라 StateMachine 전환 축**에 유지. → 위상 동기 불필요.
6. **축값 바인딩 (범용 축):** 축은 의미 고정 없이 **범용 (AxisX, AxisY)**. 노드의 X/Y Float 입력 핀 → VariableGet → AnimGraph에 선언된 **Float Variable** → `MakeFloatReader` 람다. State의 SubGraph로 쓰여도 핀 배선은 그래프 링크로 자립 해석되므로 동작(BlendListByEnum의 SelectorFn과 동일 패턴).
7. **미바인딩 축 = 0 → 자동 1D 퇴화 [결정].** AnimGraph에 선언된 variable이 0개거나 해당 축에 variable이 연결/설정되지 않으면 그 축값은 **0으로 평가**. 한 축만 바인딩하면 나머지 축이 상수 0 → 모든 샘플이 그 축에서 동일 → **자연스럽게 1D blend space로 퇴화**(별도 모드 스위치 불필요). `MakeFloatReader`가 이미 None/미발견 시 0 반환하므로 추가 코드 없이 성립.

---

## 3. 전제 & 리스크 [전제]

- **[전제-A] 보법 내 클립 위상 정렬.** 길이 동일은 필요조건이나 충분조건 아님 — 방향 클립들의 발 착지 타이밍이 정규화 위상에서 정렬돼 있어야 블렌드가 깨끗. Synty/Malbers 방향 세트는 보통 정렬 저작. **착수 전 대표 보법(예: Trot 방향 4클립) 1건 육안 확인.**
- **[리스크-B] Delaunay 엣지케이스.** 공선점·중복점·hull 밖 질의·degenerate 삼각형. → 테스트 우선 작성으로 방어(§4 Phase 2).
- **[전제-C] 직렬화 하위호환.** `kAnimGraphAssetVersion` bump 시 구버전 asset 로드 분기 필요(기존 패턴 [AnimGraphAsset.cpp](../AppleJamEngine/Source/Engine/Animation/Graph/AnimGraphAsset.cpp) 존재).

---

## 4. 구현 계획 (빌드 단위별 · 체크박스 = 진행 추적)

> 각 **Build N**은 그 자체로 컴파일이 되고 정의된 테스트(회귀/신규)로 끝나는 단위. 머신을 옮길 때는 Build 경계에서 넘기는 것을 권장.
> 완료 시 `[ ]`→`[x]`. Build 헤더의 `상태`도 갱신.

### Build 1 — 삼각분할 라이브러리 (독립 · 단위 테스트)  `상태: TODO`
> 엔진 의존 없는 순수 수학. 회귀 위험 0, 알고리즘 리스크를 가장 먼저 격리.
- [ ] **1.1** 신규 `Animation/Graph/BlendSpaceTriangulation.{h,cpp}` — 샘플 2D 좌표 → 삼각형 인덱스 목록(Bowyer–Watson 등)
- [ ] **1.2** 포함 삼각형 탐색 + barycentric 3-weight
- [ ] **1.3** convex hull 밖 질의 → 최근접 edge 투영(2-weight) / 최근접 vertex(1-weight) fallback
- [ ] **1.4** 퇴화 처리: 샘플 1개=passthrough, 2개·공선=1D bracket-lerp (→ **1D 흡수 지점**. §2-7의 "미바인딩 축=0"과 함께 1D 사용을 코드 분기 없이 커버)
- [ ] **1.5** **임시 테스트 코드** 작성(준비된 테스트 솔루션 없음 [열림-4 해소]) — 삼각형 내부/경계/hull 밖/공선/중복점에서 가중치 합=1·범위[0,1] 검증. 임시 진입점(예: `#ifdef` 가드 함수 또는 일회성 main 훅)
- [ ] **▶ 테스트(신규):** 위 임시 코드 실행해 통과 확인 → **통과 후 임시 테스트 코드 제거**. **빌드 & 테스트 통과 시 Build 1 완료.**

### Build 2 — 타입 & 직렬화 (회귀 체크)  `상태: TODO`
> 데이터 모델만 추가, 런타임 동작 없음. 목표는 "기존 asset이 안 깨진다".
- [ ] **2.1** `EAnimGraphNodeType::BlendSpace` 추가 ([AnimGraphTypes.h:29-39](../AppleJamEngine/Source/Engine/Animation/Graph/AnimGraphTypes.h#L29-L39))
- [ ] **2.2** `struct FBlendSample { FString SequencePath; float PosX; float PosY; float PlayRate=1; }` + `TArray<FBlendSample> BlendSamples` + 축 범위(`AxisMin/Max`) 필드를 `FAnimGraphNode`에 추가
- [ ] **2.3** `operator<<(FArchive&, FAnimGraphNode&)` 신규 필드 직렬화 + `kAnimGraphAssetVersion` 3→4 + 구버전 로드 분기 [전제-C]
- [ ] **▶ 테스트(회귀):** 빌드 후 기존 AnimGraph asset 로드→저장→컴파일 정상 · 기존 노드/재생 영향 없음 확인. **통과 시 Build 2 완료.**

### Build 3 — 런타임 노드 + 컴파일러 + SM 통합 + 최소 저작 (신규 기능 e2e)  `상태: TODO`
> 첫 playable. 임시 숫자입력 인스펙터로 노드를 만들어 실제 블렌드를 검증(2D 캔버스는 Build 4).
- [ ] **3.1** `FAnimationRuntime` N-pose 가중 블렌드 헬퍼(`BlendTwoPosesTogether` 합성, weight 배열)
- [ ] **3.2** 신규 `Animation/Nodes/AnimNode_BlendSpace.{h,cpp}` (`BlendListByEnum` 템플릿):
  - [ ] Initialize: 샘플별 내부 `FAnimNode_SequencePlayer` 생성·init + 삼각망(Build 1) build
  - [ ] Update: 공유 phase 진행 → 축값(AxisFn) → 활성 샘플·가중치 → 활성 child Update → 가중 RM lerp
  - [ ] Evaluate: 활성 pose N-way 가중 블렌드 · AddReferencedObjects · GetLastRootMotionDelta
- [ ] **3.3** `AnimGraphCompiler.cpp` `case BlendSpace`: FBlendSample→`LoadAnimation`→내부 SequencePlayer, 좌표/축범위 주입 · X/Y Float 핀→VariableGet(선언된 Float Variable)→`MakeFloatReader` 주입 · **미연결 축은 reader 미주입 → 0 평가**(§2-7)
- [ ] **3.4** SubGraph 타입 제한 완화 ([AnimGraphCompiler.cpp:403](../AppleJamEngine/Source/Engine/Animation/Graph/AnimGraphCompiler.cpp#L403)): `StateMachine || BlendSpace`
- [ ] **3.5** 최소 저작 경로: `AddNodeOfType` BlendSpace 케이스(AxisX/AxisY 입력 + Result 출력 핀) + 팔레트 등록 + **임시 숫자입력 인스펙터**(샘플 add/remove·클립 경로·PosX/PosY)
- [ ] **3.6** SubGraph 콤보 필터 완화 ([AnimGraphEditorWidget.cpp:995](../AppleJamEngine/Source/Editor/UI/Asset/Animation/AnimGraphEditorWidget.cpp#L995))
- [ ] **▶ 테스트(신규):** **실제 사용할 말 anim set 그대로** 노드 생성→샘플 배치→축 변수 배선 후 (a) 축값 변화에 pose 연속 블렌드 (b) **한 축만 바인딩 시 1D 퇴화 동작**(§2-7) (c) State SubGraph로 재생 (d) RM 정상 (e) 회귀: 기존 그래프 정상. **통과 시 Build 3 완료.**

### Build 4 — 2D 캔버스 에디터 (신규 기능 + 회귀)  `상태: TODO`
> 저작 UX 승격. 런타임은 그대로. **임시 숫자 인스펙터는 제거하지 않고 당분간 병행 유지 [열림-2 해소]** — 써보고 불필요/불만족 시 그때 대체·제거.
- [ ] **4.1** **2D 캔버스 인스펙터** (임시 숫자 인스펙터와 **병행**): 샘플 드래그 배치, 축 범위 설정, 삼각망 edge draw, 축값 연동 라이브 프리뷰 십자선, 클립 피커(기존 [RenderStateRow](../AppleJamEngine/Source/Editor/UI/Asset/Animation/AnimGraphEditorWidget.cpp#L941) 리스트 편집 패턴 차용)
- [ ] **4.2** 색상/라벨/역할 테이블 항목 정리
- [ ] **4.3** (선택) 노드 body 미니 프리뷰 · Debug 위젯 인스펙션
- [ ] **▶ 테스트(신규+회귀):** 캔버스로 저작한 blend space가 Build 3 런타임과 일치 · 저장/재로드 후 좌표 보존 · 기존 그래프 편집 무영향. **통과 시 Build 4 완료.**

---

## 5. 파일 touch points 요약

| 파일 | 변경 |
|---|---|
| `Animation/Graph/AnimGraphTypes.h` | enum, FBlendSample, FAnimGraphNode 필드 |
| `Animation/Graph/AnimGraphAsset.cpp` | 직렬화, 버전 bump, AddNodeOfType |
| `Animation/Nodes/AnimNode_BlendSpace.{h,cpp}` | **신규** 런타임 노드 |
| `Animation/AnimationRuntime.{h,cpp}` | N-pose 가중 블렌드 헬퍼 |
| `Animation/Graph/BlendSpaceTriangulation.{h,cpp}` | **신규** Delaunay/barycentric (Build 1, 독립 단위테스트) |
| `Animation/Graph/AnimGraphCompiler.cpp` | case BlendSpace, SubGraph 타입 완화 |
| `Editor/UI/Asset/Animation/AnimGraphEditorWidget.cpp` | 팔레트·테이블·SubGraph 필터·2D 캔버스 |

---

## 6. 규모 산정 (man-with-Claude day)

| Build | human-day | **man-with-Claude day** | sticky(압축 안 됨) |
|---|---|---|---|
| 1 삼각분할 (단위테스트) | ~2.5–3 | ~1.0–1.2 | Delaunay 엣지케이스 디버깅 |
| 2 타입 & 직렬화 (회귀) | ~0.5 | ~0.3 | — |
| 3 런타임+컴파일러+SM+최소저작 (e2e) | ~2–2.5 | ~0.9–1.1 | 엔진 GC/tick 통합 검증 |
| 4 2D 캔버스 에디터 | ~2.5–3 | ~1.3–1.5 | 드래그/프리뷰 UX 반복 |
| **합계 (엔진 기능만, 콘텐츠 제외)** | **~7.5–9** | **~3.5–4.1** | |

압축이 안 되는 바닥 = **Build 4 UX**. 삼각분할(Build 1)은 정형 알고리즘이라 man-with-Claude 단위에서 압축이 잘 됨 → 순증분 ~1일. 말 locomotion 콘텐츠 저작·튜닝은 본 계획 범위 밖(별도 산정).

---

## 7. 검토 포인트 — 전부 해소됨 (2026-07-13)

- **[열림-1 → 해소]** 축은 **범용 (AxisX,AxisY)**, Float Variable 바인딩. 미선언·미연결 축=0 → 자동 1D 퇴화. → §2-6, §2-7.
- **[열림-2 → 해소]** 임시 숫자 인스펙터는 Build 4에서 **병행 유지**, 추후 판단. → Build 4.
- **[열림-3 → 해소]** e2e 테스트는 **실제 사용할 말 anim set 그대로** 사용(별도 최소 세트 불필요). 위상 정렬은 그 과정에서 자연 확인. → Build 3 테스트.
- **[열림-4 → 해소]** 준비된 테스트 솔루션 없음 → **임시 테스트 코드 작성 후 통과 확인, 그 뒤 제거**. → Build 1.5 / Build 1 테스트.

**남은 선행 액션:** 검토 승인 시 Build 1부터 착수.

---

## 8. 진행 로그

| 날짜 | 머신 | 변경 | 비고 |
|---|---|---|---|
| 2026-07-13 | — | 설계 문서 최초 작성 | 검토 대기 |
| 2026-07-13 | — | Phase 5(콘텐츠) 제외 · Phase→빌드/테스트 단위(Build 1~4) 재구성 | 검토 대기 |
| 2026-07-13 | — | 열림 4건 해소 반영(범용 축·미바인딩0→1D · 임시 인스펙터 병행 · 실 anim set 테스트 · 임시 테스트코드) | 착수 승인 대기 |
