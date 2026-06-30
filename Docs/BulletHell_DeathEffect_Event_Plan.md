# BulletHell Death Effect Event Plan

## Goals

- 탄이 삭제될 때 즉시 사라지지 않고 위치 기반 소멸 이펙트를 남긴다.
- `FBulletArchetype`별로 서로 다른 파티클 시스템 에셋과 이벤트 이름을 지정할 수 있게 한다.
- 삭제되는 탄마다 `UParticleSystemComponent`를 새로 만들지 않고, 파티클 시스템 에셋 단위로 PSC를 재사용한다.
- 기존 파티클 시스템의 `Event Receiver Spawn` 경로를 재사용해 외부 이벤트만 주입한다.
- 대량 탄 삭제 상황에서도 프레임당 이벤트 예산과 debug stat으로 비용을 제한하고 관찰할 수 있게 한다.

## No Goals

- 탄별 독립 `UParticleSystemComponent` 스폰은 하지 않는다.
- 파티클 collision/event generation을 탄 소멸 판단에 사용하지 않는다.
- 파티클 시스템 에셋 내부 emitter/module 구조를 BulletHell 코드가 직접 수정하지 않는다.
- MVP에서는 이벤트별 색, 크기, material parameter override를 지원하지 않는다.
- GPU particle simulation, component pooling generalization, Niagara-style parameter binding은 포함하지 않는다.

## Implementation Policy

- `UBulletHellComponent`는 탄 삭제 원인과 위치/속도만 결정한다.
- `UParticleSystemComponent`는 외부 이벤트를 받아 기존 `DispatchEventsToReceivers()`로 전달한다.
- 실제 이펙트 입자 생성은 파티클 에셋 내부의 `UParticleModuleEventReceiverSpawn`이 수행한다.
- PSC lifetime은 `UBulletHellComponent`가 owner actor에 자동 생성한 runtime helper component로 관리한다.
- PSC cache key는 MVP에서 `ParticleSystemPath`로 둔다.
- 같은 파티클 시스템 에셋을 쓰는 탄들은 같은 PSC에 이벤트만 누적한다.
- 이벤트 예산 초과 시 새 PSC를 만들거나 이벤트를 지연하지 않고 현재 프레임 이벤트를 드롭한다.

## Data Model

### `FBulletDeathEffectSettings`

`FBulletArchetype`에 소멸 이펙트 설정을 추가한다.

```cpp
struct FBulletDeathEffectSettings
{
	bool bEnableDeathEffect = false;
	FString ParticleSystemPath = "None";
	FName EventName = FName("BulletDeath");
	EParticleEventType EventType = EParticleEventType::Death;
	bool bInheritBulletVelocity = true;
	float VelocityScale = 1.0f;
};
```

### `FBulletArchetype`

```cpp
struct FBulletArchetype
{
	FString MeshPath = "Content/Data/BasicShape/Sphere.OBJ";
	FString MaterialPath = "None";
	float Radius = 1.0f;
	float Speed = 1.0f;
	float Lifetime = 1.0f;
	float RenderScale = 1.0f;
	float Damage = 1.0f;
	FBulletTrailSettings Trail;
	FBulletDeathEffectSettings DeathEffect;
};
```

### `FBulletInstance`

Spawn 시 archetype 설정을 runtime instance로 복사한다.

```cpp
struct FBulletInstance
{
	// existing fields...
	FBulletDeathEffectSettings DeathEffect;
};
```

## Particle Event API

### `UParticleSystemComponent`

외부 gameplay 코드가 PSC에 이벤트를 넣을 수 있도록 public helper를 추가한다.

```cpp
UFUNCTION(Callable, Category="Particle|Event")
void EmitExternalEvent(const FParticleEventDataBase& Event);

UFUNCTION(Callable, Category="Particle|Event")
void EmitExternalDeathEvent(FName EventName, const FVector& Location, const FVector& Velocity);
```

구현 정책:

- `EmitExternalEvent()`는 `Event.Type`에 따라 `PendingEvents`의 해당 배열에 append한다.
- `EmitExternalDeathEvent()`는 `FParticleEventDeathData`를 만들어 `PendingEvents.Death`에 append한다.
- 외부 이벤트는 다음 PSC tick에서 기존 `DispatchEventsToReceivers()`로 전달된다.
- EventManager가 없어도 receiver dispatch는 PSC 내부에서 먼저 수행되므로 기본 소멸 이펙트는 동작해야 한다.
- manager broadcast가 필요 없는 BulletHell 소멸 이펙트는 `AParticleEventManager` 의존성을 새로 만들지 않는다.

## Runtime Ownership

### `UBulletHellComponent`

역할:

- 탄 spawn 시 `Archetype.DeathEffect`를 `FBulletInstance`로 복사한다.
- 삭제 직전 탄의 `Position`, `Velocity`, `DeathEffect`를 snapshot으로 읽는다.
- `ParticleSystemPath`별 PSC cache를 lazy-create한다.
- 삭제 이벤트를 해당 PSC에 enqueue한다.
- 프레임당 이벤트 예산과 누락/드롭 stat을 기록한다.

Runtime cache 예시:

```cpp
struct FBulletDeathEffectRuntimeSlot
{
	FString ParticleSystemPath;
	TWeakObjectPtr<UParticleSystemComponent> Component;
	uint32 EventsSubmittedThisFrame = 0;
	uint32 EventsDroppedThisFrame = 0;
};
```

### `UParticleSystemComponent`

역할:

- 외부 이벤트를 `PendingEvents`에 누적한다.
- 기존 emitter tick 이후 내부 generated event와 외부 event를 함께 receiver로 전달한다.
- `Event Receiver Spawn` 모듈이 있는 emitter에서 이벤트 위치 기반 입자를 생성한다.

### Particle Asset

아키타입별 소멸 이펙트 파티클 시스템은 아래 조건을 만족해야 한다.

- 이펙트를 생성할 emitter에 `Event Receiver Spawn` 모듈을 둔다.
- `Use Event Name Filter`를 켜고 `Event Name`을 archetype 설정과 맞춘다.
- `Use Event Location`을 켠다.
- 탄 속도를 반영하려면 `Inherit Event Velocity`를 켠다.
- 기본 emitter의 rate spawn은 0에 가깝게 두고 event-driven spawn만 사용한다.

## Bullet Delete Hook

삭제 이벤트는 `UBulletHellComponent::RemoveBulletAtIndex()`에서 render slot과 bullet array를 변경하기 전에 발행한다.

필요한 이유:

- swap-remove 이후에는 삭제된 탄의 위치/속도/archetype 설정을 안정적으로 읽기 어렵다.
- collision kill, explicit kill, lifetime expire가 모두 이 함수로 모인다.
- 기존 damage/collision 판단은 건드리지 않고 visual side effect만 추가할 수 있다.

삭제 원인별 기본 이벤트 이름은 MVP에서 하나로 둔다.

```cpp
DeathEffect.EventName = FName("BulletDeath");
```

추후 필요하면 아래처럼 원인별 name을 분리한다.

- `BulletExpired`
- `BulletHit`
- `BulletErased`
- `PlayerBulletDeath`
- `BossBulletDeath`

## Budget And Stats

### Settings

`UBulletHellComponent`에 component-level 예산을 둔다.

```cpp
UPROPERTY(Edit, Save, Category="BulletHell|Death Effect")
int32 MaxDeathEffectEventsPerFrame = 256;

UPROPERTY(Edit, Save, Category="BulletHell|Death Effect")
int32 MaxDeathEffectComponents = 16;
```

정책:

- `MaxDeathEffectEventsPerFrame <= 0`이면 이벤트 발행을 하지 않는다.
- 한 프레임 전체 budget을 먼저 적용한다.
- PSC 종류 수가 `MaxDeathEffectComponents`를 넘으면 새 PSC 생성을 거부하고 이벤트를 드롭한다.
- 누락된 `ParticleSystemPath` 또는 load 실패는 이벤트 드롭으로 처리하고 stat에 기록한다.

### Debug Stats

`FBulletDebugStats` 또는 별도 stat snapshot에 아래 값을 추가한다.

- `DeathEffectComponentCount`
- `DeathEffectEventCount`
- `DeathEffectDroppedCount`
- `DeathEffectMissingAssetCount`
- `DeathEffectBudgetExceededCount`

Overlay가 필요하면 기존 BulletHell stat 라인에 compact하게 추가한다.

## Phase 1: PSC External Event API

### Goal

파티클 시스템이 외부 gameplay 이벤트를 받을 수 있는 최소 API를 추가한다.

### Tasks

- `UParticleSystemComponent::EmitExternalEvent()`를 추가한다.
- `UParticleSystemComponent::EmitExternalDeathEvent()` helper를 추가한다.
- `PendingEvents` append 경로를 event type별로 분기한다.
- receiver dispatch가 EventManager 없이도 동작한다는 현재 순서를 유지한다.

### Validation

- 테스트 PSC에 직접 death event를 넣으면 `Event Receiver Spawn` emitter가 입자를 생성한다.
- EventManager가 없는 preview/tool context에서도 receiver spawn은 동작한다.
- 외부 이벤트로 생성된 파티클이 같은 프레임에 다시 receiver를 재귀 호출하지 않는다.

## Phase 2: Archetype Death Effect Settings

### Goal

탄종별로 서로 다른 소멸 이펙트 에셋과 이벤트 이름을 설정할 수 있게 한다.

### Tasks

- `FBulletDeathEffectSettings`를 추가한다.
- `FBulletArchetype`에 `DeathEffect` 필드를 추가한다.
- `FBulletInstance`에 runtime copy를 추가한다.
- `SpawnBullet()`에서 archetype 설정을 instance로 복사한다.
- boss pattern과 debug spawn template이 필요한 경우 `DeathEffect` 설정을 채울 수 있게 property를 추가한다.

### Validation

- death effect가 꺼진 탄은 삭제 시 이벤트를 만들지 않는다.
- 서로 다른 archetype이 서로 다른 `ParticleSystemPath`와 `EventName`을 유지한다.
- 저장된 scene reload 후 설정이 유지된다.

## Phase 3: BulletHell PSC Cache And Delete Hook

### Goal

탄 삭제 시 archetype에 맞는 PSC를 재사용해 소멸 이벤트를 발행한다.

### Tasks

- `UBulletHellComponent`에 `ParticleSystemPath`별 runtime PSC cache를 추가한다.
- `FindOrCreateDeathEffectComponent()` helper를 추가한다.
- `EmitBulletDeathEffect(const FBulletInstance& Bullet, bool bExpired)` helper를 추가한다.
- `RemoveBulletAtIndex()`에서 swap-remove 전에 helper를 호출한다.
- runtime-created PSC는 owner actor의 component로 등록하고 `SetTemplate()` 또는 path load 경로를 사용한다.
- 생성된 PSC는 `Activate(false)` 상태로 유지하고 event-driven spawn만 수행한다.

### Validation

- 같은 소멸 이펙트 에셋을 쓰는 탄이 여러 개 삭제되어도 PSC는 하나만 생긴다.
- 서로 다른 에셋을 쓰는 탄은 에셋 수만큼 PSC가 생긴다.
- collision kill, explicit kill, lifetime expire에서 모두 이벤트가 발행된다.
- 삭제 이벤트 때문에 bullet render slot swap-remove mapping이 깨지지 않는다.

## Phase 4: Budget, Stats, And Authoring Cleanup

### Goal

대량 삭제 상황에서 성능과 관찰 가능성을 확보하고, 패턴별 authoring을 정리한다.

### Tasks

- `MaxDeathEffectEventsPerFrame`을 적용한다.
- `MaxDeathEffectComponents`를 적용한다.
- 드롭/누락/예산 초과 stat을 추가한다.
- `UBulletHellDebugComponent` primary/secondary archetype에 death effect authoring을 추가한다.
- boss pattern component의 projectile visual 설정 근처에 death effect path/name을 추가한다.
- 기본 파티클 에셋 1개를 event-driven 소멸 테스트용으로 지정한다.

### Validation

- 한 프레임 대량 삭제 시 budget 초과분이 드롭되고 stat이 증가한다.
- budget 이하에서는 모든 삭제 이벤트가 시각적으로 나타난다.
- 소멸 이펙트 에셋이 잘못된 경우 crash 없이 missing stat이 증가한다.
- pattern별 탄 소멸 이펙트가 서로 다르게 보인다.

## Validation Commands

문서 기반 구현 후 기본 검증은 아래 순서로 수행한다.

```bat
git diff --check
cmd /c "ReleaseBuild.bat < NUL"
```

빌드가 link 단계에서 `KraftonEngine.exe` 잠금으로 실패하면 실행 중인 에디터/게임 프로세스를 닫고 재시도한다.

## Key Files

- `KraftonEngine/Source/Engine/Component/Gameplay/BulletHellComponent.h`
- `KraftonEngine/Source/Engine/Component/Gameplay/BulletHellComponent.cpp`
- `KraftonEngine/Source/Engine/Component/Particle/ParticleSystemComponent.h`
- `KraftonEngine/Source/Engine/Component/Particle/ParticleSystemComponent.cpp`
- `KraftonEngine/Source/Engine/Particle/ParticleEvents.h`
- `KraftonEngine/Source/Engine/Particle/Modules/ParticleModuleEventReceiverSpawn.*`
- `KraftonEngine/Source/Editor/UI/Asset/Particle/ParticleEditorWidget.cpp`
