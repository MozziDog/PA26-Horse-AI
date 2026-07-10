# 말 장애물 회피 / 조향 안정화 — 진행 상황 핸드오프

> 다른 PC에서 이어서 작업하기 위한 맥락 요약. 2026-07-10 기준.

## 목표

"완벽하게 주행하는 로봇"이 아니라 **그럴싸한 말**을 구현. 단 Game-ready 여야 하므로
조작감 ↔ 현실성 사이 타협이 핵심. 미로처럼 좁고 급격히 꺾이는 지형도 통과 가능해야 함.

## 아키텍처 (기존)

4계층: `Sensors → BT → Locomotion(context-steering) → Movement`.
- **센서** `UObstacleFanSensorComponent` — 전방 부채꼴로 장애물까지 clearance 측정,
  Blackboard(`HorseBBKeys::ObsClear[]` 등)에 기록.
- **arbiter** `UHorseLocomotionComponent` — clearance(danger) + interest 로 heading 선택,
  `Movement->AddInputVector(heading, speed)` 로 전달. gait 상태머신도 소유.
- **Movement** `UHorseMovementComponent` — 비홀로노믹 조향. yaw 선회율을 `MaxTurnRate=120°/s`
  (+ `MinTurnRadius`)로 이미 제한. **즉 조향 각속도 상한은 여기서 걸림.**

부채꼴: `HorseBBKeys::ObsFanAngles = {-40,-20,0,20,40}` (deg, forward 기준), `ObsFanCount=5`.

## 이번 작업에서 바꾼 것 (순서대로)

주로 `HorseLocomotionComponent.{h,cpp}` 의 context-steering arbiter, 그리고 센서.
전부 UPROPERTY 로 노출해 에디터에서 토글/튜닝 가능(연구 단계라 롤백 쉽게).

1. **센서: raycast → sphere sweep** (`ObstacleFanSensorComponent.cpp`)
   - 각 slot 을 `BodyRadius`(기본 0.5m) sphere 로 sweep → 말 몸통 폭 반영. 얇은 장애물(차단봉)·
     틈새 누락도 해결. `Hit.Distance` 를 그대로 `ObsClear[i]` 에 기록(arbiter 무수정).
   - **JumpProbe 는 ray 유지**(높이 판정은 폭 무관). low/high 둘 다 ray 라 sweep 반경 편향 없음.

2. **binary veto → graded danger (2단계)**
   - `clear ≥ SafeDistance` → danger 0, `HardBlockDistance~SafeDistance` → 0→1 선형 램프,
     `clear ≤ HardBlockDistance` → danger 1 **+ 하드 제외**(안전 바닥).
   - `Score[i] = Interest - DangerWeight*danger`. 임계값 근처 톡톡 튐 제거.

3. **이웃 danger 확산** `DangerSpread`(기본 0.5)
   - `SpreadDanger[i] = max(Danger[i], spread*Danger[i±1])`. **목적은 danger field 의 공간적
     저역통과 = 판단 떨림 완화** (berth 아님). spread=0 으로 하면 떨림 심해짐 → 현재 필수.

4. **danger-gated commit (히스테리시스)** `CommitWeight`
   - `+ CommitWeight * DangerActivation * max(0, SlotDir·SteerDir)`. 직전 heading 유지 편향으로
     좌/우 argmax 핑퐁 억제. **danger 로 게이팅**(DangerActivation=max SpreadDanger): 열린 공간
     에선 0 → 관성이 이겨 forward 복귀. (게이팅 안 하면 상대 조향각 영구 유지 = 나선/직진불가 버그.)

5. **sub-slot 포물선 보간**
   - argmax slot 과 양옆 score 로 연속 목표 조향각 산출 → 5-slot 20° 스냅 떨림 제거.

6. **danger persistence — fast-attack / slow-release** `bDangerPersistence`, `DangerReleaseRate`(3.0/s)
   - `Danger[i] = max(관측, PrevDanger[i] - Rate*dt)`. 올릴 땐 즉시(안전), 내릴 땐 천천히.
     회전 중 장애물이 sweep 경계에서 **사라지는** 쪽 떨림을 흡수. attack 즉시라 회피 반응 지연 0.
   - **효과 큼**(눈에 띄게 감소). 단 장애물이 **나타나는** 쪽은 원리상 못 잡음(안전상 즉응 유지).

7. **조향각 slew (출력 저역통과)** `bSmoothSteering`, `SteerRateLimit`
   - 목표 조향각을 초당 `SteerRateLimit` 이하로만 따라감.
   - **결론: 이 문제엔 부적합/역효과.** 피드백 루프(센서가 말에 붙어 조향→헤딩→판독→조향)의
     limit cycle 이라, 출력단 지연을 넣으면 위상여유↓ → **떨림 폭이 오히려 커짐**(90에서 확인).
     → C/D 트랙 실험 전엔 **끄거나 ≥120 으로** 두고 베이스라인 잡을 것.

8. **forward-lane guard (A안)** `ForwardLaneGuard`(1.0)  ← **가장 최근, 아직 미해결**
   - 정면(0°=현재 heading) slot 의 score 에서 옆 slot spread 오염분을 걷어내 raw danger 만 보게 함:
     `EffDanger[center] = SpreadDanger - (SpreadDanger - Danger)*guard`.
   - 의도: 통로 탈출 시 남은 벽의 spread 가 정면을 눌러 열린 쪽으로 홱 꺾이는 lurch 제거.
   - **결과: 그대로임(효과 없음).** 아래 "미해결" 참고.

## 문제 분해 (현재 관점)

두 개를 **분리**해서 본다:

- **문제 1 — 통로 출구 과민 조향(lurch).** 좌우 벽 clearance 가 0.1 만 달라도, 먼저 사라지는
  벽 쪽으로 조향각이 **크게** 꺾임. 정면이 뚫려 있는데도 열린 옆쪽으로 가려는 성향.
  → A안(forward-lane guard)으로 시도했으나 **실패**.
- **문제 2 — 프레임 단위 조향 떨림.** limit cycle 성격. → **C/D 트랙**으로 접근 예정
  (아직 시작 안 함). 출력 slew(7번)는 여기에 역효과라 제외.

## 미해결 / 다음 스텝 (중요)

**A안이 안 먹힌 유력한 원인 = sub-slot 보간(5번)의 편향.**
argmax 가 정면(center)이어도, **한쪽 이웃이 막혀 있으면 포물선 보간 offset 이 열린 쪽으로 기울어**
최종 heading 이 그쪽으로 쏠린다. 예: center 가 best, 왼쪽(-20) 벽으로 매우 낮은 score, 오른쪽(+20)
열림 → `Offset = 0.5*(sL - sR)/Denom` 가 양수(오른쪽)로 나와 heading 이 오른쪽으로 기욺.
guard 는 **선택(argmax)** 만 고쳤고 **보간 lean** 은 그대로라 증상이 안 사라진 것으로 추정.

→ **다음 액션 후보:**
1. **보간 가드**: 정면 주변 보간 시, 막힌(고danger/하드제외) 이웃이 heading 을 끌어당기지 않도록
   offset 을 클램프하거나, 비슷하게 열린 slot 끼리만 보간. (문제 1 직격으로 예상)
2. 그래도 "정면 뚫리면 무조건 직진"은 부자연스러움(벽에 딱 붙어 달리는 말은 이상함) — 어느 정도
   열린 쪽으로 여유를 두되 **과민하지 않게** 하는 게 목표. guard/보간의 강도를 튜닝 포인트로.
3. 문제 2(프레임 떨림): **C안** — 루프 게인↓(DangerWeight), 결정 프레임을 순간 yaw 에서 분리
   (smoothed heading/velocity 기준 평가), yaw-rate 감쇠(진짜 D항, slew 와 다름). **D안** — 슬롯
   해상도↑(±40 5개 → 더 촘촘히)로 field 매끄럽게.

참고로 **B안(corridor centering)은 별도 도입 불필요** — 현재도 대칭 danger 로 통로 중앙 주행이
자연히 됨. 지금 겪는 "한쪽 열린 분기"가 곧 이 상황 자체라, centering 은 이 문제의 해가 아님.

## 현재 튜닝 기본값 (arbiter, `HorseLocomotionComponent.h`)

| 파라미터 | 값 | 비고 |
|---|---|---|
| SafeDistance | 2.0 m | danger 램프 상단 |
| HardBlockDistance | 0.8 m | 하드 제외 바닥 |
| DangerWeight | 3.0 | interest 합보다 커야 회피 |
| DangerSpread | 0.5 | 공간 LP(떨림완화). 0 이면 떨림↑ |
| ForwardLaneGuard | 1.0 | 정면 spread 제거율. **효과 없음(위 참고)** |
| CommitWeight | 0.75~2.0 | 테스트 중. 높여도 lurch 못 막음 |
| bDangerPersistence / DangerReleaseRate | true / 3.0 | 효과 큼 |
| bSmoothSteering / SteerRateLimit | true / 90 | **역효과. 끄거나 ≥120 권장** |
| (Movement) MaxTurnRate | 120 °/s | 조향 각속도 상한 |

센서(`ObstacleFanSensorComponent.h`): `ProbeRange=6`, `BodyRadius=0.5`, `JumpProbeUp=1.0`.

> 새 UPROPERTY 는 `Serialize()` 에 append 로 추가됨. **기존 저장 씬은 한 번 에디터에서 재저장** 필요.

## 빌드 / 검증

- 증분 빌드: `msbuild AppleJamEngine.sln /p:Configuration=Release /p:Platform=x64 /m`
  (vswhere 로 MSBuild 찾음. 전체 패키징은 `ReleaseBuild.bat`.)
- 지금까지 전부 빌드 통과. 거동은 에디터에서 `HorseTest.Scene` 로 플레이 확인.

## 주요 파일

- `AppleJamEngine/Source/Engine/Component/AI/ObstacleFanSensorComponent.{h,cpp}` — 센서(sweep)
- `AppleJamEngine/Source/Engine/Component/Movement/HorseLocomotionComponent.{h,cpp}` — arbiter(핵심)
- `AppleJamEngine/Source/Engine/Component/Movement/HorseMovementComponent.{h,cpp}` — yaw 조향/선회율
- `AppleJamEngine/Source/Engine/AI/HorseBlackboardKeys.h` — slot 각도/키 계약
- `AppleJamEngine/Source/Engine/GameFramework/Pawn/HorseCharacter.cpp` — 입력(조향은 ±축 → UserMoveDir)
