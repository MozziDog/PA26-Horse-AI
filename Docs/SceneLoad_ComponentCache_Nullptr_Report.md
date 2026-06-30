# Game 월드 Scene 로드 시 컴포넌트 캐시 nullptr 문제 분석

작성일: 2026-06-06

이 문서는 `EWorldType`이 `Game`으로 결정된 상태에서 씬을 로드했을 때, **액터의 BeginPlay는 정상 호출되지만 액터가 들고 있는 타입별 컴포넌트 캐시 포인터(`StaticMeshComponent`, `Mesh`, `CapsuleComponent`, `LightComponent` 등)가 nullptr로 남는 문제**의 원인과 해결책을 정리한 문서입니다.

## 최종 결론

컴포넌트 캐시 포인터를 다시 잡는 재연결 로직이 **`PostDuplicate()`에만 구현**되어 있는데, `PostDuplicate()`는 **복제(Duplicate) 경로에서만** 호출됩니다. 씬 로드 경로는 `PostDuplicate()`를 호출하지 않고 `PostLoadFromArchive()`만 호출하는데, 어떤 액터도 `OnPostLoad()`를 오버라이드하지 않아 사실상 no-op입니다.

결과적으로 씬에서 로드된 액터는 **컴포넌트 자체는 정상 복원되지만(컴포넌트 리스트/RootComponent는 유효)**, **액터 클래스의 캐시 멤버 포인터만 nullptr**로 남습니다.

> 참고: 설계 주석([`PawnStaticMesh.h`](../KraftonEngine/Source/Engine/GameFramework/Actor/PawnStaticMesh.h))에 *"직렬화/복제 시에는 컴포넌트가 씬에서 복원되며, PostDuplicate에서 캐시 포인터만 다시 잡는다"*고 명시되어 있습니다. 즉 "직렬화(=로드) 시에도 PostDuplicate가 캐시를 잡아야 한다"는 계약이 존재하나, 로드 경로가 이를 호출하지 않아 계약이 깨진 상태입니다.

---

## 1. 배경 — `Game` 월드는 BeginPlay가 돈다

`StringToWorldType("Game")` → `EWorldType::Game` ([`SceneSaveManager.cpp:247`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp)). 실제 게임 실행 경로에서는 [`GameEngine.cpp:206-215`](../KraftonEngine/Source/Engine/Runtime/GameEngine.cpp)가 `OverrideWorldType`으로 강제로 `Game`을 적용합니다.

`Game`(과 `PIE`) 월드만 `World->BeginPlay()`가 호출되고([`Engine.cpp:159`](../KraftonEngine/Source/Engine/Runtime/Engine.cpp), [`GameEngine.cpp:181`](../KraftonEngine/Source/Engine/Runtime/GameEngine.cpp)), 그 안에서 모든 액터의 `BeginPlay()`가 실행됩니다. 따라서 이 문제는 `Game`/`PIE` 월드에서만 표면화됩니다(에디터 월드는 BeginPlay 자체가 없음).

---

## 2. 현상

- 씬 로드 후 액터의 `BeginPlay()`는 정상 호출된다.
- 그런데 `GetStaticMeshComponent()` / `Mesh` / `GetCapsuleComponent()` 등 **액터 캐시 포인터가 nullptr**다.
- 반면 `GetComponents()`, `GetComponentByClass<T>()`, `GetRootComponent()`는 정상 동작한다(컴포넌트 자체는 살아있음).
- 같은 액터를 에디터에서 **복제(Ctrl+D)** 하면 캐시가 정상이다.

즉 **컴포넌트는 있는데 액터의 타입별 캐시 포인터만 비어 있는** 상태입니다.

---

## 3. 원인 분석

### 3.1 캐시 포인터는 직렬화 대상이 아니다

액터의 타입별 컴포넌트 포인터는 `UPROPERTY`가 아닌 **단순 캐시**입니다. 예:

```cpp
// StaticMeshActor.h:26  — UPROPERTY 매크로 없음 → 직렬화/역직렬화 대상 아님
private:
    TWeakObjectPtr<UStaticMeshComponent> StaticMeshComponent = nullptr;
```

따라서 이 포인터는 JSON에서 복원되지 않고, 아래 세 경로 중 하나에서만 채워집니다.

| 경로 | 캐시를 채우는 방법 | 씬 로드 시 호출됨? |
|------|--------------------|--------------------|
| 코드 스폰 | `InitDefaultComponents()`가 `AddComponent` 결과를 대입 | ❌ (의도적으로 호출 안 함) |
| 복제(Duplicate) | `PostDuplicate()`가 컴포넌트 트리에서 다시 잡음 | ❌ (복제 전용 경로) |
| **씬 로드** | `PostLoadFromArchive()` → `OnPostLoad()` | ✅ 호출되나 **no-op** |

### 3.2 재연결 로직은 `PostDuplicate()`에만 있다

```cpp
// PawnStaticMesh.cpp:16
void APawnStaticMesh::PostDuplicate()
{
    StaticMeshComponent = Cast<UStaticMeshComponent>(GetRootComponent());
}
```

```cpp
// Character.cpp:240
void ACharacter::PostDuplicate()
{
    Super::PostDuplicate();
    CapsuleComponent = Cast<UCapsuleComponent>(GetRootComponent());  // 컴포넌트 트리 재발견
    ...
}
```

`PostDuplicate()`를 오버라이드하는 액터는 다음 9개입니다:
`TriggerVolumeBase`, `LuaCharacter`, `CapsuleActor`, `BoxActor`, `PawnStaticMesh`, `Character`, `WheeledVehiclePawn`(→`RebindVehicleComponents()`), `ParticleSystemActor`, `SphereActor`.

반면 **`OnPostLoad()` / `PostLoadFromArchive()`를 오버라이드하는 액터는 0개**입니다.

### 3.3 두 경로의 호출 분기

베이스 정의 ([`Object.h`](../KraftonEngine/Source/Engine/Object/Object.h)):

```cpp
void PostLoadFromArchive(FArchive& Ar) { OnPostLoad(Ar); }   // :119
virtual void OnPostLoad(FArchive& /*Ar*/) {}                 // :129  베이스 no-op
virtual void PostDuplicate() {}                              // :132  베이스 no-op
```

- **복제 경로** — `AActor::Duplicate()`가 컴포넌트별 `PostDuplicate()`와 액터 `PostDuplicate()`를 호출 ([`AActor.cpp:803, 810`](../KraftonEngine/Source/Engine/GameFramework/AActor.cpp)):
  ```cpp
  // 4) 서브클래스 멤버 포인터 복원
  Dup->PostDuplicate();
  ```
- **씬 로드 경로** — `DeserializeProperties()`가 객체별로 `PostLoadFromArchive()`만 호출 ([`SceneSaveManager.cpp:788`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp)):
  ```cpp
  FSceneJsonLoadArchive PostLoadArchive(PropsJSON, Context);
  Obj->PostLoadFromArchive(PostLoadArchive);   // → OnPostLoad → no-op
  ```

즉 베이스 `PostLoadFromArchive` → `OnPostLoad`는 빈 함수이고 `PostDuplicate`를 호출하지도 않으므로, **로드 경로에서는 캐시 재연결이 전혀 일어나지 않습니다.**

### 3.4 씬 로드 액터 루프에 재연결 단계가 없음

[`SceneSaveManager.cpp:586-633`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) — 액터를 생성하고 컴포넌트 트리를 복원하지만 `PostDuplicate()`(또는 동등한 재연결)를 호출하지 않습니다:

```cpp
for (auto& ActorJSON : root[SceneKeys::Actors].ArrayRange()) {
    UObject* ActorObj = FObjectFactory::Get().Create(ActorClass, World);
    AActor* Actor = static_cast<AActor*>(ActorObj);
    World->AddActor(Actor);
    ...
    // RootComponent 트리 복원 — 컴포넌트 자체는 여기서 정상 복원됨
    USceneComponent* Root = DeserializeSceneComponentTree(RootJSON, Actor, LoadContextState);
    if (Root) Actor->SetRootComponent(Root);          // :605
    ...
    // 비씬 컴포넌트도 RegisterComponent 로 정상 등록
    // ── 그러나 Actor->PostDuplicate() 같은 캐시 재연결 호출은 없음 ──
}
```

### 3.5 정리: 호출 흐름 비교

```text
[코드 스폰]   Create → InitDefaultComponents()       → (캐시 대입) → AddActor → BeginPlay
[복제]        Duplicate → 컴포넌트 트리 복제 → PostDuplicate() → (캐시 재연결) → AddActor
[씬 로드]     Create → DeserializeSceneComponentTree → PostLoadFromArchive()(no-op)
                                                       ▲ 캐시 재연결 단계 없음 → nullptr 유지
              → World::BeginPlay() → Actor::BeginPlay() 에서 캐시 사용 시 nullptr
```

---

## 4. 주의: `InitDefaultComponents()` 호출은 잘못된 해결책

로드 시 `InitDefaultComponents()`를 호출하면:

- JSON에서 복원된 컴포넌트에 더해 **기본 컴포넌트가 중복 생성**된다(예: `AddComponent<UStaticMeshComponent>()` 재실행).
- 기본 메시/에셋 로드가 **직렬화된 상태를 덮어쓴다**(예: `SetStaticMesh(기본 Cylinder)`).

따라서 로드 경로가 `InitDefaultComponents()`를 부르지 않는 것은 **의도된 설계**이며, 빠진 것은 컴포넌트 생성이 아니라 **캐시 재연결 단계**입니다.

---

## 5. 해결책

### 방법 1 — 최소 수정 (로드 루프에 재연결 한 줄 추가)

이미 모든 액터를 순회하는 render-state 재구성 패스([`SceneSaveManager.cpp:647-668`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp))에서, 컴포넌트/속성 복원이 끝난 시점에 `PostDuplicate()`를 호출합니다. 이 시점엔 RootComponent와 모든 컴포넌트가 복원 완료되어 타이밍이 안전합니다.

```cpp
for (AActor* Actor : World->GetActors())
{
    if (!IsValid(Actor)) continue;

    Actor->PostDuplicate();   // ★ load == duplicate 계약: 캐시 포인터 재연결

    for (UActorComponent* Component : Actor->GetComponents())
    {
        // ... 기존 render-state 재구성 ...
    }
}
```

- 장점: 변경 범위 최소, 기존 9개 `PostDuplicate` 오버라이드를 그대로 재사용.
- 단점: `PostDuplicate`라는 이름이 의미상 로드와 어긋남. (현재 구현들은 순수 재연결뿐이라 부작용 없음)

### 방법 2 — 구조적 수정 (재연결을 공용 훅으로 분리)

재연결 로직을 별도 메서드(예: `RebindComponentCache()`)로 추출하고, `PostDuplicate()`와 `OnPostLoad()` **양쪽에서** 호출합니다. 그러면 이미 객체별로 호출되는 `PostLoadFromArchive`([`SceneSaveManager.cpp:788`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp))를 통해 로드 시 자동으로 재연결되어, **두 경로가 영구적으로 일관**됩니다.

```cpp
// 예: 각 액터
protected:
    void RebindComponentCache() { StaticMeshComponent = Cast<UStaticMeshComponent>(GetRootComponent()); }
public:
    void PostDuplicate() override { RebindComponentCache(); }
    void OnPostLoad(FArchive&) override { RebindComponentCache(); }
```

- 장점: 의미가 명확하고 로드/복제 경로가 구조적으로 일치.
- 단점: 9개 액터에 훅 추가 필요(변경 범위 큼).

---

## 6. 영향 범위 / 점검 대상

캐시 포인터를 쓰는 모든 액터가 영향을 받습니다. 우선 점검 대상:

| 액터 | 캐시 멤버 | `PostDuplicate` 오버라이드 | 비고 |
|------|-----------|:--:|------|
| `APawnStaticMesh` | `StaticMeshComponent` | O | 로드 시 nullptr |
| `ACharacter` / `ALuaCharacter` | `CapsuleComponent`, `SpringArm` 등 | O | 로드 시 nullptr |
| `AWheeledVehiclePawn` | `Mesh` 등 (`RebindVehicleComponents`) | O | 로드 시 nullptr |
| `ATriggerVolumeBase` | `TriggerBox` | O | 로드 시 nullptr |
| `ABoxActor` / `ACapsuleActor` / `ASphereActor` | 각 shape 컴포넌트 | O | 로드 시 nullptr |
| `AParticleSystemActor` | `ParticleSystemComponent` | O | 로드 시 nullptr |
| `AStaticMeshActor` | `StaticMeshComponent` | **X** | **로드/복제 모두 nullptr** (아래 참고) |
| 라이트 액터들(`APointLightActor` 등) | `LightComponent` | X | `InitDefaultComponents`만 보유 → 로드/복제 시 캐시 재연결 없음 |

### 부수 발견

`AStaticMeshActor`는 `PostDuplicate`를 **오버라이드하지 않습니다**. 따라서 이 액터의 `StaticMeshComponent`는 **로드뿐 아니라 복제 시에도** nullptr입니다. `BeginPlay`가 `GetComponentByClass<USphereComponent>()`만 사용하기 때문에 크래시는 없지만, `GetStaticMeshComponent()`를 호출하는 외부 코드는 null을 받습니다.

방법 1/2 적용 시 `AStaticMeshActor`(및 라이트 액터)에도 재연결 로직을 추가해야 완전히 해결됩니다:

```cpp
// AStaticMeshActor 에 추가 필요
void PostDuplicate() override { StaticMeshComponent = Cast<UStaticMeshComponent>(GetRootComponent()); }
```

---

## 7. 관련 파일

- [`KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) — 씬 로드 액터 루프(586-633), `PostLoadFromArchive` 호출(788)
- [`KraftonEngine/Source/Engine/Object/Object.h`](../KraftonEngine/Source/Engine/Object/Object.h) — `PostLoadFromArchive`/`OnPostLoad`/`PostDuplicate` 베이스 정의(119, 129, 132)
- [`KraftonEngine/Source/Engine/GameFramework/AActor.cpp`](../KraftonEngine/Source/Engine/GameFramework/AActor.cpp) — `Duplicate`에서 `PostDuplicate` 호출(803, 810)
- [`KraftonEngine/Source/Engine/GameFramework/Actor/StaticMeshActor.h`](../KraftonEngine/Source/Engine/GameFramework/Actor/StaticMeshActor.h) — 캐시 포인터 선언(26, 비-UPROPERTY)
- [`KraftonEngine/Source/Engine/GameFramework/Actor/PawnStaticMesh.cpp`](../KraftonEngine/Source/Engine/GameFramework/Actor/PawnStaticMesh.cpp) / [`.h`](../KraftonEngine/Source/Engine/GameFramework/Actor/PawnStaticMesh.h) — `PostDuplicate` 재연결 + 설계 주석
- [`KraftonEngine/Source/Engine/GameFramework/Pawn/Character.cpp`](../KraftonEngine/Source/Engine/GameFramework/Pawn/Character.cpp) — `PostDuplicate` 재연결(240-244)
- [`KraftonEngine/Source/Engine/Runtime/Engine.cpp`](../KraftonEngine/Source/Engine/Runtime/Engine.cpp) / [`GameEngine.cpp`](../KraftonEngine/Source/Engine/Runtime/GameEngine.cpp) — `Game`/`PIE`에서만 `World::BeginPlay` 호출
