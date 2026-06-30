# Boss Pattern Selector Handoff Guide

이 문서는 완성된 Boss Pattern Selector를 실제로 조정하고, 필요하면 코드를 수정하기 위한 설명서다. 앞부분은 사람이 에디터에서 따라 하는 cookbook이고, 뒷부분은 AI Agent가 코드를 수정할 때 필요한 시스템 설명이다.

## Cookbook For Designers And Teammates

### 기본 배치 확인

1. 레벨에서 보스 actor를 선택한다.
2. Details 패널의 component 목록에서 `UBossPatternSelectorComponent`가 붙어 있는지 확인한다.
3. 같은 보스 actor에 `UBulletHellComponent`가 붙어 있는지 확인한다. 실제 공격 pattern은 이 component의 public API로 탄을 만든다.
4. 같은 보스 actor에 사용할 pattern component들이 붙어 있는지 확인한다.
   - `UBossPattern_AimedRingVolley`
   - `UBossPattern_HomingOrbTrail`
   - `UBossPattern_RendingClawArc`
   - `UBossPattern_SphericalPulseBarrage`
   - `UBossPattern_ThunderclapCascade`
   - 필요하면 `UBossPattern_IdleTrackTarget`
     - 아무것도 안하는 패턴
5. 체력 비율로 phase를 바꾸고 싶으면 보스 actor가 `APawn` 계통인지 확인한다. selector는 owner pawn의 `Pawn|Health` 값에서 `HealthRatio`를 읽는다.
6. 탄막 피격을 pawn 체력으로 전달해야 하는 actor에만 `UBulletHellDamageReceiverComponent`를 붙인다. 이 component는 체력을 소유하지 않고, owner가 `APawn`이면 `GetDamaged(damage)`만 호출한다.

### Pawn 체력 조정

1. 보스 또는 플레이어 pawn actor를 선택한다.
2. Details 패널에서 actor 본체 또는 pawn component 설정이 노출되는 항목을 본다.
3. `Pawn|Health` 카테고리의 `Max Health`를 조정한다.
4. 시작할 때 항상 최대 체력으로 초기화하려면 `Reset Health On Begin Play`를 true로 둔다.
5. 런타임 중 현재 체력을 직접 확인하거나 임시 조정해야 하면 같은 카테고리의 `Current Health`를 본다.
6. 체력 기반 phase가 필요 없는 pawn은 기본값 그대로 둬도 된다.

### 탄막 DamageReceiver 배치

1. 탄막과 충돌했을 때 피해를 받아야 하는 pawn actor를 선택한다.
2. component 목록에 `UBulletHellDamageReceiverComponent`를 추가한다.
3. 이 component에는 체력 수치가 없다. 체력 수치는 owner pawn의 `Pawn|Health`에서 조정한다.
4. 플레이어 공격이 탄막이 아닌 경우, 보스 체력 감소는 플레이어 공격 처리 코드에서 pawn의 `GetDamaged(damage)`를 직접 호출하거나 별도 공격 시스템에서 같은 API로 연결한다.

### 현재 상태를 화면에서 보기

1. PIE 또는 Game 실행 중 콘솔을 연다.
2. `stat bosspattern`을 입력한다.
3. 오버레이에서 상태를 확인한다.
   - 초록색 `READY`: 지금 선택될 수 있는 pattern
   - 파란색 `ACTIVE`: 현재 실행 중인 pattern
   - 빨간색 `BLOCK`: 선택될 수 없는 pattern
4. 빨간색 line의 괄호 안 reason을 먼저 본다.
   - `cooldown Ns`: 쿨타임이 남았다.
   - `disabled`: pattern component가 꺼져 있다.
   - `zero weight`: 공통 weight가 0이다.
   - `phase blocked`: 현재 phase가 `Allowed Phase Mask`에 없다.
   - `phase weight zero`: 현재 phase의 phase weight가 0이다.
   - `target too close` / `target too far`: 거리 조건에 막혔다.
   - `repeat blocked`: 최근 사용 pattern 반복 방지에 막혔다.
5. 오버레이를 끄려면 `stat none`을 입력한다.

### Pattern을 끄기

1. 보스 actor를 선택한다.
2. 끄고 싶은 pattern component를 component 목록에서 선택한다.
3. `Boss Pattern|Common` 카테고리의 `Enabled`를 false로 둔다.
4. `stat bosspattern`에서 해당 pattern이 빨간색 `BLOCK (... disabled)`로 나오는지 확인한다.

### Pattern 빈도 조정

1. 보스 actor에서 조정할 pattern component를 선택한다.
2. `Boss Pattern|Selection` 카테고리의 `Weight`를 조정한다.
   - 0이면 선택되지 않는다.
   - 값이 클수록 같은 phase 안에서 더 자주 선택된다.
3. phase별로 빈도를 다르게 하고 싶으면 같은 카테고리의 `Phase Weight 0`, `Phase Weight 1`, `Phase Weight 2`를 조정한다.
   - 최종 선택 가중치는 `Weight * Phase Weight N`이다.
   - 특정 phase에서만 막고 싶으면 해당 `Phase Weight N`을 0으로 둔다.
4. `stat bosspattern`의 `W` 값이 기대한 effective weight로 표시되는지 확인한다.

### Pattern이 너무 자주 나올 때

1. 보스 actor에서 해당 pattern component를 선택한다.
2. `Boss Pattern|Selection` 카테고리의 `Cooldown`을 늘린다.
3. 같은 pattern이 바로 반복되는 것이 싫으면 selector component를 선택한다.
4. `Boss Pattern|Selection` 카테고리의 `Repeat Block Count`를 1 이상으로 둔다.
   - 1은 방금 쓴 pattern 1개를 막는다.
   - 2 이상은 최근 pattern 여러 개를 막으므로 fallback이 더 자주 나올 수 있다.

### 특정 거리에서만 쓰게 하기

1. 보스 actor에서 해당 pattern component를 선택한다.
2. `Boss Pattern|Condition` 카테고리의 `Min Target Distance`와 `Max Target Distance`를 조정한다.
3. target이 너무 가까우면 `target too close`, 너무 멀면 `target too far`로 표시된다.
4. 모든 거리에서 허용하려면 기본값처럼 `Min Target Distance=0`, `Max Target Distance`를 충분히 크게 둔다.

### Phase별 pattern pool 만들기

1. 보스 actor의 selector component를 선택한다.
2. `Boss Pattern|Phase` 카테고리에서 `Phase 1 Health Ratio Threshold`, `Phase 2 Health Ratio Threshold`를 정한다.
   - 기본값은 0.66 이하부터 phase 1, 0.33 이하부터 phase 2다.
3. 실제 체력으로 테스트하려면 보스 pawn의 `Pawn|Health`에서 `Max Health`와 `Current Health`를 조정한다.
4. 빠르게 테스트하려면 selector의 `Boss Pattern|Debug` 카테고리에서 `Use Debug Boss Health Ratio`를 켜고 `Debug Boss Health Ratio`를 바꾼다.
5. 특정 pattern component를 선택한다.
6. hard block이 필요하면 `Boss Pattern|Condition`의 `Allowed Phase Mask`를 조정한다.
7. 선택 확률만 조정하려면 `Boss Pattern|Selection`의 `Phase Weight 0/1/2`를 조정한다.
8. `stat bosspattern`에서 `Phase`, `HealthRatio`, blocked reason이 기대와 맞는지 본다.

### 특정 pattern 강제 실행

1. 보스 actor의 selector component를 선택한다.
2. `Boss Pattern|Debug` 카테고리의 `Forced Pattern Name`에 pattern 이름을 입력한다.
   - 예: `AimedRingVolley`, `HomingOrbTrail`, `RendingClawArc`, `SphericalPulseBarrage`, `ThunderclapCascade`
3. 조건을 존중하고 강제 실행하려면 `Force Pattern Ignore Conditions`를 false로 둔다.
4. cooldown, distance, phase를 무시하고 테스트하려면 `Force Pattern Ignore Conditions`를 true로 둔다.
   - 단, disabled pattern은 여전히 실행하지 않는다.
5. `Force Pattern Request`를 토글한다.
6. 실행 중인 pattern을 중단하려면 `Cancel Pattern Request`를 토글한다.
7. 취소 후 fallback으로 보낼지 여부는 `Cancel Goes To Fallback`으로 정한다.

### Aimed Ring Volley 조정

1. 보스 actor에서 `UBossPattern_AimedRingVolley` component를 선택한다.
2. `Boss Pattern|Aimed Ring Volley` 카테고리를 연다.
3. 원형 대기 탄 수를 바꾸려면 `Projectile Count`를 조정한다.
4. 원 크기를 바꾸려면 `Ring Radius`를 조정한다.
5. 대기 시간을 바꾸려면 `Launch Delay`를 조정한다.
6. 발사 속도를 바꾸려면 `Projectile Speed`를 조정한다.
7. 생성 위치를 boss 기준 앞/위로 옮기려면 `Spawn Forward Offset`, `Spawn Up Offset`을 조정한다.
8. 원의 회전 시작 각도를 바꾸려면 `Angle Offset Degrees`를 조정한다.
9. 시작 시점 target 방향을 고정하려면 `Lock Target Direction On Start`를 true로 둔다.

### Homing Orb Trail 조정

1. 보스 actor에서 `UBossPattern_HomingOrbTrail` component를 선택한다.
2. `Boss Pattern|Homing Orb Trail` 카테고리를 연다.
3. 전체 생성 시간을 바꾸려면 `Spawn Duration`을 조정한다.
4. 생성 개수를 바꾸려면 `Projectile Count`를 조정한다.
5. 명시적 생성 간격이 필요하면 `Spawn Interval Override`를 0보다 크게 둔다.
6. boss 주변 생성 반경은 `Spawn Radius Around Boss`로 조정한다.
7. 생성 후 발사까지의 대기 시간은 `Launch Delay`로 조정한다.
8. 호밍 세기는 `Homing Strength`, 회전 제한은 `Homing Max Turn Rate Degrees`, 추적 각도는 `Homing Cone Half Angle Degrees`로 조정한다.
9. 이 pattern은 boss 이동을 직접 하지 않는다. 이동은 별도 boss movement 작업에서 처리해야 한다.

### Spherical Pulse Barrage 조정

1. 보스 actor에서 `UBossPattern_SphericalPulseBarrage` component를 선택한다.
2. `Boss Pattern|Spherical Pulse Barrage` 카테고리를 연다.
3. pulse 횟수는 `Pulse Count`로 조정한다.
4. 전체 pulse 기간은 `Pulse Duration`으로 조정한다.
5. 명시적 pulse 간격이 필요하면 `Pulse Interval Override`를 0보다 크게 둔다.
6. pulse당 탄 수는 `Projectiles Per Pulse`로 조정한다.
7. 구면 생성 반경은 `Sphere Radius`로 조정한다.
8. 즉시 퍼지는 속도는 `Projectile Speed`로 조정한다.
9. 분포를 랜덤화하려면 `Use Random Sphere Points`를 켜고 `Random Seed Offset`으로 변형한다.

### Rending Claw Arc 조정

1. 보스 actor에서 `UBossPattern_RendingClawArc` component를 선택한다.
2. `Boss Pattern|Rending Claw Arc` 카테고리를 연다.
3. 보스 중심 구체 반경은 `Sphere Radius`로 조정한다.
4. 할퀴는 줄 수는 `Plane Count`, 줄 사이 간격은 `Plane Spacing`으로 조정한다.
5. 각 줄의 호 길이는 `Arc Center Angle Degrees`로 조정한다. 호의 중앙은 항상 target 방향을 향한다.
6. 줄마다 탄 수는 `Projectiles Per Arc`로 조정한다.
7. 모든 탄은 보스에서 target으로 향하는 같은 방향에 `Projectile Speed`를 곱한 속도로 발사된다.

### Thunderclap Cascade 조정

1. 보스 actor에서 `UBossPattern_ThunderclapCascade` component를 선택한다.
2. `Boss Pattern|Thunderclap Cascade` 카테고리를 연다.
3. 낙뢰 반복 횟수는 `Cycle Count`로 조정한다.
4. 낙뢰 간격은 `Cycle Interval`로 조정한다.
5. boss 앞 기준 위치는 `Strike Forward Distance`로 조정한다.
6. 매 cycle마다 x/y 위치를 흔들고 싶으면 `Strike Random XY Radius`를 0보다 크게 둔다.
7. impact z는 strike x/y 지점 위에서 아래로 WorldStatic raycast를 해서 구한다. cast 시작 높이는 `Ground Trace Start Height`, 아래 탐색 거리는 `Ground Trace Down Distance`로 조정한다.
8. 실제 지면보다 살짝 위/아래를 맞추고 싶으면 `Ground Height Offset`을 조정한다.
9. 낙뢰가 시작되는 높이는 `Strike Spawn Height`, 떨어지는 속도는 `Strike Fall Speed`로 조정한다.
10. 충격파 높이는 `Shockwave Height Offset`으로 조정한다.
11. 충격파 탄 수는 `Shockwave Projectile Count`, 퍼지는 속도는 `Shockwave Speed`로 조정한다.
12. pattern이 무한히 지속되는 것을 막는 안전 시간은 `Max Pattern Duration`으로 조정한다.

### Selector가 멈춘 것처럼 보일 때

1. `stat bosspattern`을 켠다.
2. `Select 0/N`이면 모든 pattern이 막힌 상태다.
3. 빨간 line의 reason을 본다.
4. cooldown만 남아 있으면 기다리거나 `Cooldown`을 줄인다.
5. `phase blocked`이면 `Allowed Phase Mask` 또는 selector phase threshold/debug phase를 확인한다.
6. `phase weight zero`이면 현재 phase의 `Phase Weight N`을 확인한다.
7. `repeat blocked`가 많으면 selector의 `Repeat Block Count`를 낮춘다.
8. pattern component가 아예 없으면 selector는 fallback idle만 돈다.

### 보스 공격을 멈추게 하고 싶을 때

1. 보스가 가진 `UBossPatternSelectorComponent`를 선택한다.
2. `Boss Pattern|Selector` 카테고리의 `Enable Pattern Selection` 옵션을 찾아 체크 해제한다.
3. 혹은, 코드 상으로 `bEnablePatternSelection`에 접근해 값을 false로 만든다.
   * 참고: private field라서 public getter 추가 필요합니다.

### 보스 공격 빈도를 높이거나 낮추고 싶을 때

보스는 이전 패턴이 끝나면 바로 다음 패턴을 시작합니다. 그래서 공격 빈도를 조절하려면 이전 패턴의 선딜레이/후딜레이를 조절하는 방식을 사용해야 합니다.

1. 보스가 가진 `UBossPattern_`로 시작하는 컴포넌트들을 선택한다.
2. `Boss Pattern|Condition` 카테고리의 `Windup Duration` 또는 `Recovery Duration`을 설정하여 선딜레이 / 후딜레이를 조절한다.

## System Notes For AI Agents

### Runtime ownership

- `UBossPatternSelectorComponent` owns selection, active pattern lifetime, cooldown ticking, fallback idle, target resolution, health-ratio-to-phase calculation, force/cancel debug requests, recent-pattern repeat blocking, and boss-pattern stat snapshots.
- `UBossPatternComponentBase` owns common pattern gates and the coarse step state machine.
- Concrete `UBossPattern_*` classes own only their local timing, projectile spawn math, delayed launch queues, and pattern-specific tuning parameters.
- `UBulletHellComponent` owns projectile storage, lifetime, collision, homing updates, render binding, and public spawn/launch APIs. Boss patterns must not mutate bullet arrays or render instance indices directly.

### Tick order

Selector tick runs in this order:

1. Build `FBossPatternContext`.
2. Consume `CancelPatternRequest` and `ForcePatternRequest`.
3. Tick all pattern cooldowns.
4. Tick fallback idle timer.
5. Tick active pattern.
6. If no active pattern and fallback elapsed, select the next pattern.
7. Update `FBossPatternDebugState`.
8. Record `BossPatternStats` snapshot.
9. Draw/log debug if enabled.

This order means a forced pattern can start and tick in the same frame. Cooldown is assigned by `NotifySelected()` before `StartPattern()`.

### Selection gates

The base `GetCanUse()` rejects in this order:

1. `Enabled == false`
2. `Weight <= 0`
3. `CooldownRemaining > 0`
4. target distance below `MinTargetDistance`
5. target distance above `MaxTargetDistance`
6. `AllowedPhaseMask` does not contain `BossPhase`

Selector-level selection then applies:

1. `GetEffectiveWeight(Context) > 0`
2. recent-pattern repeat block
3. weighted random pick among usable patterns

`GetEffectiveWeight()` returns `Weight * PhaseWeightN`, where phase 0 uses `PhaseWeight0`, phase 1 uses `PhaseWeight1`, and phase 2 or higher uses `PhaseWeight2`.

### Phase and health

- `ResolveBossHealthRatio()` first checks selector debug override.
- Without override, it casts the selector owner to `APawn` and reads `APawn::GetHealthRatio()`.
- If the selector owner is not an `APawn`, health ratio is 1.0.
- `APawn` owns health state: `MaxHealth`, `CurrentHealth`, `bResetHealthOnBeginPlay`, `TotalDamageTaken`, and `HealthHitCount`.
- `APawn::BeginPlay()` resets health to `MaxHealth` when `bResetHealthOnBeginPlay` is true.
- `APawn::GetDamaged(float DamageAmount)` clamps negative damage to zero, reduces `CurrentHealth`, accumulates applied damage, increments hit count, and returns the applied amount.
- `UBulletHellDamageReceiverComponent` is a bullet-hit bridge only. It does not own health; it forwards valid bullet damage to `OwnerPawn->GetDamaged(damage)`.
- The old component-level health probe path was removed. Code should depend on `UBulletHellDamageReceiverComponent` for bullet-hit forwarding or `APawn` health directly for health/phase queries.
- `ComputeBossPhase()` uses selector thresholds. Default phase is 0, phase 1 starts at or below `Phase1HealthRatioThreshold`, and phase 2 starts at or below `Phase2HealthRatioThreshold`.
- `AllowedPhaseMask` is a hard block. `PhaseWeightN=0` is also a block in selection, but semantically represents weight tuning.

### Debug and stat flow

- `stat bosspattern` is implemented through `FOverlayStatSystem::BuildBossPatternLines()`.
- Runtime snapshots are accumulated in `FBossPatternStats` and reset once per `UEngine::WorldTick()`.
- Pattern-specific debug text comes from virtual `GetRuntimeDebugText()`.
- Existing useful details:
  - `AimedRingVolley`: pending launch count
  - `HomingOrbTrail`: spawned count and pending launch count
  - `RendingClawArc`: used plane count and spawned projectile count
  - `SphericalPulseBarrage`: spawned pulse count
  - `ThunderclapCascade`: started cycle count and active cycle count

### Adding a new pattern

1. Copy an existing `BossPattern_*.h/.cpp` pair under `KraftonEngine/Source/Engine/Component/Gameplay`.
2. Rename file names, class name, generated include, constructor, and default `PatternName`.
3. Register both files in `KraftonEngine.vcxproj` and `KraftonEngine.vcxproj.filters`.
4. Keep common gates in `UBossPatternComponentBase`; override `GetCanUse()` only for pattern-specific dependencies such as requiring `Context.BulletHell`.
5. Implement step behavior through `OnPatternStart()`, `OnStepEnter()`, `TickCurrentStep()`, `ShouldAdvanceStep()`, and `GetNextStep()`.
6. Call `FinishPattern(Context)` only through base flow or when the pattern must force-end.
7. Add a compact `GetRuntimeDebugText()` if the pattern has delayed launches, cycle state, or other hidden runtime state.
8. Build with `cmd /c "ReleaseBuild.bat < NUL"`.

### Invariants to preserve

- Only one top-level pattern is active in the selector.
- Parallel work is allowed only inside a pattern, such as `ThunderclapCascade` active cycles.
- Patterns must not store `FBossPatternContext` across frames.
- Patterns should tolerate missing target by using locked or fallback directions.
- Patterns that spawn projectiles should reject with a clear reason when `Context.BulletHell` is null.
- Do not add Behavior Tree, Lua bridge, data-only asset authoring, or navmesh requirements to this system unless the scope is explicitly reopened.

### Key files

- `KraftonEngine/Source/Engine/Component/Gameplay/BossPatternComponentBase.h`
- `KraftonEngine/Source/Engine/Component/Gameplay/BossPatternComponentBase.cpp`
- `KraftonEngine/Source/Engine/Component/Gameplay/BossPatternSelectorComponent.h`
- `KraftonEngine/Source/Engine/Component/Gameplay/BossPatternSelectorComponent.cpp`
- `KraftonEngine/Source/Engine/Component/Gameplay/BossPattern_AimedRingVolley.*`
- `KraftonEngine/Source/Engine/Component/Gameplay/BossPattern_HomingOrbTrail.*`
- `KraftonEngine/Source/Engine/Component/Gameplay/BossPattern_RendingClawArc.*`
- `KraftonEngine/Source/Engine/Component/Gameplay/BossPattern_SphericalPulseBarrage.*`
- `KraftonEngine/Source/Engine/Component/Gameplay/BossPattern_ThunderclapCascade.*`
- `KraftonEngine/Source/Engine/Profiling/Stats/BossPatternStats.*`
- `KraftonEngine/Source/Editor/Subsystem/OverlayStatSystem.*`
