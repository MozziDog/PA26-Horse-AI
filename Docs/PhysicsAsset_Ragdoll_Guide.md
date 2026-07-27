# PhysicsAsset / 래그돌 가이드

> 스켈레탈 메시에 래그돌을 붙이기 위해 현재 엔진에 **구현되어 있는 것**을 정리한 문서.
> Body / Constraint 각 옵션이 PhysX 의 무엇으로 내려가고 실제로 무슨 일이 일어나는지에 초점.
> 기준 커밋: `feature/suspension` (2026-07-27).
>
> 2026-07-27 디버깅 세션에서 고친 것 — **포즈 역전파 스케일 계약**(3장), **swing 한계 기즈모 축 스왑**(6장),
> 그리고 **constraint 프레임 Snap 기능** 추가(6장). 자세한 배경은 각 장 참고.

단위 규약: **1 unit = 1 m**. Scene gravity 는 `(0, 0, -9.81)`
([PhysXPhysicsScene.cpp:1330](AppleJamEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp#L1330)),
`PxTolerancesScale()` 도 기본값(length 1, speed 10)이라 미터계와 맞아떨어진다.
즉 문서에 나오는 모든 길이/반지름 값은 **미터**로 읽으면 된다.

---

## 1. 전체 구조

```
UPhysicsAsset (직렬화되는 데이터만)
  ├── TArray<FPhysicsAssetBodySetup>        본 1개 = 강체 1개
  │     └── TArray<FPhysicsAssetShapeSetup> 강체 1개 = 충돌 프리미티브 N개
  └── TArray<FPhysicsAssetConstraintSetup>  부모본 ↔ 자식본 D6 조인트

        │ FPhysicsAssetInstance::CreateBodiesAndConstraints(Options)
        ▼
FBodyCreationDesc / FConstraintCreationDesc   (엔진 중립 desc)
        │ 커맨드 큐 → 물리 스레드
        ▼
FPhysXBodyBuilder / FPhysXConstraintBuilder   (PxRigidDynamic / PxD6Joint)
        │ 스냅샷
        ▼
FPhysicsAssetInstance::PullPhysicsPose → USkeletalMeshComponent::ApplyPhysicsAssetPose
```

관련 파일:

| 역할 | 파일 |
|---|---|
| 에셋 데이터 구조 (모든 옵션의 원본) | [PhysicsAssetTypes.h](AppleJamEngine/Source/Engine/Physics/PhysicsAssetTypes.h) |
| 런타임 공통 타입 (`FConstraintLimitDesc` 등) | [PhysicsTypes.h](AppleJamEngine/Source/Engine/Physics/PhysicsTypes.h) |
| 에셋 오브젝트 / 조회 헬퍼 | [PhysicsAsset.h](AppleJamEngine/Source/Engine/Physics/PhysicsAsset.h) |
| 런타임 인스턴스 (생성/파괴/포즈 역전파) | [PhysicsAssetInstance.cpp](AppleJamEngine/Source/Engine/Physics/PhysicsAssetInstance.cpp) |
| PhysX 강체 생성 | [PhysXBodyBuilder.cpp](AppleJamEngine/Source/Engine/Physics/PhysXBodyBuilder.cpp) |
| PhysX D6 조인트 생성 | [PhysXConstraintBuilder.cpp](AppleJamEngine/Source/Engine/Physics/PhysXConstraintBuilder.cpp) |
| 자동 Body 생성 (Regenerate Bodies) | [PhysicsAssetAutoBodyGenerator.cpp](AppleJamEngine/Source/Engine/Physics/PhysicsAssetAutoBodyGenerator.cpp) |
| 에디터 UI | [PhysicsAssetEditorWidget.cpp](AppleJamEngine/Source/Editor/UI/Asset/Physics/PhysicsAssetEditorWidget.cpp) |
| 게임플레이 진입점 (전신/부분 래그돌) | [SkeletalMeshComponent.h](AppleJamEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h) |
| 검증 규칙 | [PhysicsAssetValidation.cpp](AppleJamEngine/Source/Engine/Physics/PhysicsAssetValidation.cpp) |

---

## 2. 좌표 / 프레임 규약 (먼저 읽을 것)

옵션 의미가 전부 여기에 걸려 있다. 합성 규칙은 생성·프리뷰·역전파 세 경로가 동일하다
([PhysicsAssetInstance.cpp:18](AppleJamEngine/Source/Engine/Physics/PhysicsAssetInstance.cpp#L18),
[PhysicsAssetPreviewUtils.cpp:17](AppleJamEngine/Source/Engine/Physics/PhysicsAssetPreviewUtils.cpp#L17)).

```
Bone(world)  = Component(world) ∘ BoneComponentSpace
Body(world)  = Bone(world)      ∘ BodySetup.BodyLocalFrame
Shape(world) = Body(world)      ∘ ShapeSetup.LocalTransform
ParentFrame(world) = ParentBody(world) ∘ Constraint.ParentLocalFrame
ChildFrame(world)  = ChildBody(world)  ∘ Constraint.ChildLocalFrame
```

핵심:

- **`BodyLocalFrame` 은 "본 → 강체" 오프셋.** 시뮬레이션 결과를 포즈로 되돌릴 때 이 값의 역변환을
  쓴다 (`ComputeBoneWorldTransformFromBody`). 여기에 값을 넣으면 본이 그만큼 반대로 밀린다는 뜻이 아니라,
  강체가 본 기준 그 위치에 붙고 본은 강체를 따라간다는 뜻이다. 왕복이 수학적으로 대칭이므로 안심하고 써도 된다.
- **Constraint 의 두 LocalFrame 은 "본 기준이 아니라 *강체(Body) 기준*"이다.** PxD6Joint 가 액터 로컬 프레임을
  받기 때문. 본 기준으로 착각하면 `BodyLocalFrame` 이 0 이 아닌 순간부터 조인트가 어긋난다.
- **스케일은 물리에 전달되지 않는다.** 위 합성식에서 결과 `Scale` 은 항상 1 로 강제된다.
  그래서 스켈레톤이 비단위 스케일을 가진 리그에서는 포즈 역전파에 별도 보정이 필요하다 → **3장**.
- **캡슐 축은 로컬 Z.** PhysX 캡슐은 X축 정렬이라 빌더가 `PxQuat(-π/2, Y)` 를 곱해 Z 로 돌린다
  ([PhysXBodyBuilder.cpp:240](AppleJamEngine/Source/Engine/Physics/PhysXBodyBuilder.cpp#L240)).
  에디터 디버그 드로우도 `DrawDebugCapsuleZAxis` 로 같은 규약.
- **D6 축 매핑**: Twist = 프레임 **X**축 회전, Swing1 = **Y**축 회전, Swing2 = **Z**축 회전.
  선형 X/Y/Z 는 프레임 축 그대로.
  회전축과 **변위 방향이 한 칸 어긋난다**는 점에 주의 — Y축 회전(Swing1)은 twist축 X 를 **Z 쪽으로**
  기울이고, Z축 회전(Swing2)은 X 를 **Y 쪽으로** 기울인다. 한계 시각화가 이걸 반대로 그려서
  오래 헷갈렸던 이력이 있다(6장).

---

## 3. 스케일 계약 (포즈 역전파)

말 리그(`MalbersHorse_*`)는 일부 본의 **레퍼런스 로컬 스케일이 0.01** 이다(FBX 100배 단위 변환 잔재,
본별로는 균일). 여기서 두 규약이 만나므로 명시적 보정이 필요하다.

| | 규약 |
|---|---|
| **스켈레톤** | `Global[i] = Local[i] * Global[parent]` (행 벡터). 즉 본의 로컬 translation 은 **부모의 누적 스케일이 곱해진 프레임**에 있다 ([SkinnedMeshComponent.cpp:1128](AppleJamEngine/Source/Engine/Component/Primitive/SkinnedMeshComponent.cpp#L1128)) |
| **물리 포즈 경로** | `PullPhysicsPose` 의 월드 트랜스폼은 의도적으로 **스케일 없음**(회전 + 이동만) |

두 공간을 오갈 때는 부모의 누적 레퍼런스 스케일(`FTransform(Bone.GetReferenceGlobalPose()).Scale`)로
로컬 translation 을 보정해야 한다:

- `PullPhysicsPose` 가 **바디 없는 본**을 재구성할 때는 오프셋을 **곱해서 키운다**
  (`ComposeSkeletonLocalOntoWorld` / `ComputeParentWorldFromSkeletonLocal`).
- `ApplyPhysicsAssetPose` 가 로컬 포즈를 되돌릴 때는 **다시 나눈다**. 재합성이 부모 스케일을
  또 곱하기 때문 (`RemoveParentReferenceScaleFromLocalTranslation`).

> **스케일을 행렬에 접지 말 것.** 컴포넌트 공간 행렬에 스케일을 실으면 로컬 행렬이
> `Scale(S[i])·Rot·Rot⁻¹·Scale(1/S[p])` 형태가 되어, 본별 스케일이 비균일한 순간 shear 가 섞인다.
> 그러면 `DecomposePoseMatrixPreservingScale` 이 뽑아낸 "회전"이 직교행렬이 아니게 되어 메시가
> 뒤틀린다. **회전은 건드리지 말고 translation 만 보정**하는 것이 유일하게 안전한 형태다.

이걸 틀리면 **바디가 없는 본 전부가 루트 쪽으로 수축**한다(이 리그는 약 100개 중 85개가 바디 없음).
증상은 "물리 바디는 멀쩡한데 메시만 뭉개짐" 이고, 아래 `Simulated Bodies (raw physics)` 뷰로 구분한다.

---

## 4. 작업 흐름 (스켈레탈 메시에 래그돌 붙이기)

1. **Mesh Editor 로 스켈레탈 메시를 연다 → Physics 탭.**
   PhysicsAsset 은 독립 에디터가 아니라 Mesh Editor 안에 임베드되어 있다
   (`FMeshEditorWidget::PhysicsAssetEditor`). 저장도 Mesh Editor 의 저장 버튼을 통한다
   (`FPhysicsAssetEditorWidget::RenderToolbar` 의 Save/Validate 버튼은 현재 주석 처리).
2. **PhysicsAsset 생성/할당.** 없으면 `CreateAndAssignPhysicsAssetForCurrentMesh()` 가
   `<메시이름>_PhysicsAsset` 이름으로 만들어 `USkeletalMesh::SetPhysicsAsset()` 로 붙인다.
   (레포에 이미 `MalbersHorse_noArmour_noLow_SkeletalMesh_PhysicsAsset.uasset` 이 생성돼 있음)
3. **Regenerate Bodies** 로 초안 생성 → 4장 참고.
4. 스켈레톤 트리에서 본을 골라 Body 를 추가/삭제하고, Shape 크기와 `BodyLocalFrame` 을
   뷰포트 기즈모로 다듬는다. (기즈모는 Shape 선택 시 Shape, 아니면 Body Local Frame 을 잡는다)
5. Constraint Graph 패널에서 부모-자식 연결을 확인하고 각 조인트의 Twist/Swing 한계를 넣는다.
6. **Simulate** 버튼으로 에디터 내 시뮬레이션. 마우스 좌클릭 드래그로 바디를 잡아끌 수 있다
   (`BeginEditorRagdollGrab`).
7. 저장 후 런타임에서 `USkeletalMeshComponent::EnableRagdollPhysics()` 호출 (7장).

**에셋 해석 우선순위** ([SkeletalMeshComponent.cpp:872](AppleJamEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L872)):

```
Component 의 Physics Asset Override  →  SkeletalMesh 의 PhysicsAsset  →  Skeleton 의 Default PhysicsAsset
```

Override 는 스켈레톤 호환성 검사(`CanUsePhysicsAsset`)를 통과해야 하고, 실패하면 자동으로 비워진다.

---

## 5. Body 옵션 (`FPhysicsAssetBodySetup`)

에디터: Details 패널 → Body 선택 (`RenderBodyDetails`,
[PhysicsAssetEditorWidget.cpp:2864](AppleJamEngine/Source/Editor/UI/Asset/Physics/PhysicsAssetEditorWidget.cpp#L2864)).
런타임 매핑은 `BuildBodyCreationDesc` → `FPhysXBodyBuilder::ApplyBodyProperties`.

| 옵션 | 기본값 | PhysX 매핑 | 실제 효과 / 주의 |
|---|---|---|---|
| `BoneName` | None | `FBodyCreationDesc::BoneName`, 스냅샷 조회 키 | 이 본의 월드 트랜스폼이 강체 초기 위치가 되고, 시뮬 후 이 본이 강체를 따라간다. 스켈레톤 트리에서 바인딩하는 것이 정석 (직접 타이핑 UI 는 비활성). 본 이름이 메시에 없으면 그 Body 는 **조용히 스킵**된다(로그만 남음). |
| `BodyLocalFrame` | Identity | 강체 초기 pose 계산 | 본 기준 강체 오프셋. 2장 참고. 회전을 주면 D6 조인트 프레임 기준도 같이 회전하므로 Constraint 를 나중에 다시 봐야 한다. |
| `Shapes` | 빈 배열 | `PxShape[]` (compound) | **비어 있으면 Body 생성 자체가 실패**한다 (`BuildBodyCreationDesc` 가 false 반환). 5장 참고. |
| `Mass` | 1.0 | `PxRigidBodyExt::setMassAndUpdateInertia(mass, &com)` | 실제 질량(kg). 관성 텐서는 셰이프 형상에서 자동 계산되고 질량만 지정 값으로 스케일된다. `<= 0` 이면 1.0 으로 대체. **인접 바디 간 질량비가 10:1 을 넘어가면 조인트가 눈에 띄게 늘어난다** — 말처럼 몸통/발굽 크기 차가 큰 리그에서 가장 흔한 불안정 원인. |
| `CenterOfMassLocalOffset` | (0,0,0) | `setMassAndUpdateInertia` 의 COM + `setCMassLocalPose` | 강체 로컬 기준 무게중심. 몸통을 아래로 내리면 넘어질 때 덜 뒤집힌다. |
| `LinearDamping` | 0.0 | `setLinearDamping` | 선속도 감쇠(1/s). 0 이면 공기저항 없음. 래그돌이 미끄러지듯 계속 흐르면 0.05~0.2 정도. |
| `AngularDamping` | 0.0 | `setAngularDamping` | 각속도 감쇠. **래그돌 떨림(jitter) 억제에 가장 효과가 큰 노브.** 사지처럼 가벼운 바디에 0.5~2.0 을 주면 팔랑거림이 크게 줄어든다. |
| `MaxAngularVelocity` | 100.0 | `setMaxAngularVelocity` | rad/s 상한. 기본 100 rad/s(≈5730°/s)는 사실상 무제한이라, 작은 바디가 충돌로 튀며 폭주하는 걸 막으려면 10~30 정도로 낮추는 것이 실용적. |
| `PositionSolverIterationCount` | 8 | `setSolverIterationCounts(pos, vel)` (최소 1 클램프) | 위치 솔버 반복. 조인트 한계 준수도/늘어남에 직결. 8 은 이미 넉넉한 편(PhysX 기본 4). 관절 체인이 길수록 이득이 크지만 비용도 선형 증가. |
| `VelocitySolverIterationCount` | 2 | 위와 동일 | 속도 솔버 반복. 반발/마찰 품질. 보통 1~4 면 충분. |
| `bEnableCCD` | false | `PxRigidBodyFlag::eENABLE_CCD` + 셰이프 필터의 CCD 비트 | 빠른 물체의 터널링 방지. **Scene 에 `PxSceneFlag::eENABLE_CCD` 가 켜져 있어야 동작**하고, 이건 프로젝트 설정 `Physics.bEnableCCD` 에 달려 있다 ([PhysXPhysicsScene.cpp:1337](AppleJamEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp#L1337)). 비싸므로 머리/발굽 등 필요한 바디만. |
| `bEnableGravity` | true | `PxActorFlag::eDISABLE_GRAVITY = !값` | 끄면 중력 무시. 시뮬 옵션의 `bNoGravity` / "Selected Simulation" 의 앵커 바디는 이 값을 **런타임에 덮어쓴다**. |
| `bLockLinearX/Y/Z` | false | `PxRigidDynamicLockFlag::eLOCK_LINEAR_*` | **월드 축 기준** 이동 자유도 고정. 로컬 축이 아니다. 2D 평면 래그돌 같은 특수 용도 외엔 거의 안 쓴다. |
| `bLockAngularX/Y/Z` | false | `PxRigidDynamicLockFlag::eLOCK_ANGULAR_*` | 월드 축 기준 회전 고정. 조인트 한계로 풀어야 할 문제를 이걸로 막으면 부모-자식이 서로 싸운다. |

런타임에 항상 강제되는 값(에셋에서 못 바꿈):

- `BodyType = Dynamic` — 단, 에디터 "Selected Simulation" 모드에서 선택 체인 밖 바디는 `Kinematic` + 중력 off 로 앵커가 된다.
- `SyncMode = Manual` — 컴포넌트가 명시적으로 포즈를 당겨오므로 일반 트랜스폼 미러링을 쓰지 않는다.
- `Domain = Ragdoll` — 디버그/스탯 분리 및 필터 정책 분기용.
- `bGenerateHitEvents = true`.

---

## 6. Shape 옵션 (`FPhysicsAssetShapeSetup`)

한 Body 는 여러 셰이프를 가질 수 있고(compound), 전부 같은 강체에 붙는다.
**모든 셰이프는 엔진 전역 기본 머티리얼(static 0.5 / dynamic 0.5 / restitution 0.3)을 공유한다**
([PhysXPhysicsScene.cpp:1362](AppleJamEngine/Source/Engine/Physics/PhysXPhysicsScene.cpp#L1362)).
바디별 마찰/반발 지정은 현재 지원되지 않는다.

| 옵션 | 기본값 | 의미 |
|---|---|---|
| `Type` | Box | `Box` / `Sphere` / `Capsule`. PhysicsAsset 에서는 Convex/TriangleMesh 를 쓸 수 없다 (`BuildShapeDescs` 의 switch 가 나머지를 `continue` 로 버린다). 래그돌은 사실상 캡슐+박스 조합. |
| `LocalTransform` | Identity | Body 프레임 기준 셰이프 배치. 회전으로 캡슐 축 방향을 맞춘다. |
| `BoxHalfExtent` | (0.8, 0.8, 0.8) | **반**치수(m). 에디터가 0.001 하한으로 클램프. |
| `SphereRadius` | 0.8 | 반지름(m). |
| `CapsuleRadius` | 0.4 | 반지름(m). |
| `CapsuleHalfHeight` | 1.2 | **반구를 포함한 전체 반높이.** 빌더가 `PxCapsuleGeometry(r, HalfHeight − r)` 로 변환하므로 `HalfHeight ≤ Radius` 면 원통부가 0 이 되어 사실상 구가 된다 ([PhysXBodyBuilder.cpp:237](AppleJamEngine/Source/Engine/Physics/PhysXBodyBuilder.cpp#L237)). UE 의 `CapsuleHalfHeight` 와 같은 정의. |

NaN/Inf 는 셰이프 생성 단계에서 거부되고, 유한한 값은 `1e-4` 하한으로 클램프된다.

### 충돌 필터 (에셋에 옵션이 없고 코드가 정한다)

`FillShapeFilterDataFromComponent`
([PhysicsAssetInstance.cpp:84](AppleJamEngine/Source/Engine/Physics/PhysicsAssetInstance.cpp#L84)) 가
소유 컴포넌트의 채널 설정을 그대로 복사한다. 시뮬레이션 옵션에 따라:

- `bUseIndependentRagdollCollision = true` (게임플레이 래그돌 기본값): 오너 액터 UUID 를 `IgnoreGroup`
  에 스탬프하고 `CollisionRole` 을 `FullRagdollBody`(부분이면 `PartialReactionBody`)로 표시해,
  **같은 액터의 캐릭터 캡슐/프리미티브와 싸우지 않게** 필터에서 걸러낸다.
- 래그돌끼리의 self-collision 은 필터가 아니라 **Constraint 의 `bDisableCollisionBetweenBodies`** 로 결정한다.
  즉 인접하지 않은 바디끼리는 기본적으로 서로 충돌한다.

---

## 7. Constraint 옵션 (`FPhysicsAssetConstraintSetup`)

모든 조인트는 **PxD6Joint** 하나로 만들어진다
([PhysXConstraintBuilder.cpp:30](AppleJamEngine/Source/Engine/Physics/PhysXConstraintBuilder.cpp#L30)).
`CreateFixedJoint` / `CreateSphericalJoint` 도 내부적으로 D6 를 락 조합으로 흉내낸 것.

| 옵션 | 기본값 | PhysX 매핑 | 효과 |
|---|---|---|---|
| `ParentBoneName` / `ChildBoneName` | None | 두 Body 핸들 조회 | **두 본 모두 Body 가 있어야** 조인트가 생성된다. 없으면 스킵 + 로그. 그래프 패널/본 트리로만 편집 가능(직접 타이핑 UI 없음). |
| `ParentLocalFrame` | Identity | `PxD6JointCreate` 의 actor0 프레임 | **부모 *강체* 기준** 조인트 프레임. |
| `ChildLocalFrame` | Identity | actor1 프레임 | **자식 *강체* 기준** 조인트 프레임. 두 프레임이 같은 월드 위치·자세로 겹쳐야 조인트가 튀지 않는다 → 아래 "두 프레임 정렬 확인법". |
| `Limits.LinearX/Y/Z` | **Locked** | `setMotion(eX/eY/eZ, ...)` | `Locked` = 두 프레임 원점 일치(볼조인트처럼 붙음). `Free` = 그 축으로 자유 슬라이드. **`Limited` 는 현재 사실상 `Free`** — 아래 "알려진 한계" 참고. 래그돌은 셋 다 Locked 로 두는 것이 정답. |
| `Limits.Twist` | Limited | `setMotion(eTWIST, ...)` | 프레임 **X축** 비틀림. |
| `Limits.Swing1` | Limited | `setMotion(eSWING1, ...)` | 프레임 **Y축** 스윙. |
| `Limits.Swing2` | Limited | `setMotion(eSWING2, ...)` | 프레임 **Z축** 스윙. |
| `TwistLimitMinDegrees` / `TwistLimitMaxDegrees` | −45 / +45 | `setTwistLimit(PxJointAngularLimitPair)` | **비대칭 가능**(min ≠ −max). 에디터가 min > max 면 자동으로 swap 한다. `Twist` 가 `Limited` 일 때만 의미 있음. |
| `Swing1LimitDegrees` | 30 | `setSwingLimit(PxJointLimitCone(y, z))` 의 y | **대칭 원뿔의 반각.** ±30° 라는 뜻. 음수 불가(에디터가 0 하한). |
| `Swing2LimitDegrees` | 30 | 위 cone 의 z | 위와 동일. Swing1/2 가 함께 타원 원뿔을 만든다. **Swing1/Swing2 중 하나만 `Limited` 여도 cone 값은 항상 둘 다 설정된다** — 나머지 축의 motion 이 `Free` 면 그 방향 한계는 무시된다. |
| `Limits.bEnableProjection` | true | `PxConstraintFlag::ePROJECTION` | 조인트가 벌어졌을 때 솔버가 강제로 붙이는 기능. **현재 톨러런스를 설정하지 않아 사실상 무동작** — 아래 참고. |
| `bDisableCollisionBetweenBodies` | true | `PxConstraintFlag::eCOLLISION_ENABLED = !값` | **연결된 두 바디끼리의 충돌만** 끈다. 켜 두는 것이 기본(부모-자식은 거의 항상 겹치므로). false 로 하면 상박/하박이 서로를 밀어내며 경련한다. |

### 두 프레임 정렬 확인법 (뷰포트)

Details 패널의 **Viewport Gizmo Target** 라디오(Parent Frame / Child Frame)로 잡는 기즈모는
`FPhysicsAssetConstraintFrameGizmoTarget`
([PhysicsAssetGizmoTarget.cpp:632](AppleJamEngine/Source/Engine/Gizmo/PhysicsAssetGizmoTarget.cpp#L632))이며,
**기즈모가 그려진 위치·자세가 곧 PxD6Joint 로 내려가는 조인트 프레임**이다
(런타임과 같은 합성식을 쓴다). 라디오는 둘 중 어느 쪽을 편집할지만 고른다.

- **위치 일치 확인**: 라디오를 토글할 필요 없이, 디버그 드로우가 두 프레임 원점에 점을 찍고 그 사이를
  선으로 잇는다 ([PhysicsAssetEditorWidget.cpp:4166](AppleJamEngine/Source/Editor/UI/Asset/Physics/PhysicsAssetEditorWidget.cpp#L4166)).
  **선이 점 하나로 보이면 정렬된 것**, 선이 보이면 그만큼 어긋난 것.
- **자세 일치 확인**: 라디오를 토글해 기즈모 축 방향을 비교한다. 프레임 X축이 Twist 축이므로,
  자세 선택이 "어느 해부학적 축이 비틀림/스윙이 되는가"를 결정한다.

**프레임 원점은 관절 피벗(자식 본의 원점)에 두는 것이 정석이다.** 그러면 무릎이 굽든 목이 돌든
자식 바디가 그 피벗을 중심으로 회전할 뿐이라 **어떤 포즈에서도 위치가 계속 일치**하고,
포즈에 따라 달라지는 것은 상대 자세뿐이라 각도 한계가 정확히 그 역할을 맡는다.
원점이 피벗을 벗어나면 포즈가 바뀔 때마다 두 프레임이 벌어지고, 래그돌 진입 순간 솔버가
바디를 끌어당겨 튄다. Regenerate Bodies 는 자식 본 원점을 조인트 지점으로 잡아 양쪽 프레임을
역산하므로 자동 생성 결과는 이 성질을 만족한다 — 기즈모로 수동 조정할 때는
**자세만 돌리고 위치는 본 원점에 유지**할 것.

### 각도 한계 감 잡기 (말 리그 기준 시작값)

| 부위 | Twist (X) | Swing1 (Y) | Swing2 (Z) |
|---|---|---|---|
| Spine / Chest | ±10° | 15° | 15° |
| Neck 각 세그먼트 | ±20° | 25° | 25° |
| Head | ±15° | 20° | 20° |
| 어깨/고관절 | ±20° | 45° | 45° |
| 무릎/비절 (1자유도 힌지) | ±5° | 60° | 3° |
| 발목/발굽 | ±5° | 25° | 5° |

무릎처럼 한 축만 굽는 관절은 **한계각을 작게 주는 것보다, 굽지 않아야 할 축을 `Locked` 로 두는 편이**
훨씬 안정적이다(솔버가 한계면 위에서 진동하지 않으므로).

---

## 8. Regenerate Bodies (자동 생성)

툴바 → `Regenerate Bodies` 팝업. `FPhysicsAssetAutoBodyGeneratorOptions` 를 채워
[PhysicsAssetAutoBodyGenerator.cpp](AppleJamEngine/Source/Engine/Physics/PhysicsAssetAutoBodyGenerator.cpp) 로 넘긴다.
**바인드 포즈(레퍼런스 포즈)의 스킨 웨이트**를 기준으로 각 본에 프리미티브를 피팅한다.

| 옵션 | 기본 | 의미 |
|---|---|---|
| Use PCA Analysis / Use Bone Axis | PCA | 상호 배타. PCA = 해당 본에 가중치가 실린 정점들의 주성분으로 캡슐 축·길이를 정함(품질 좋음). Bone Axis = 본→자식 방향을 축으로 단순 피팅. |
| Primitive | Capsule | 피팅된 바운드를 어떤 프리미티브로 만들지. Capsule / Box / Sphere. |
| Merge Small Bones | on | 작은 본에 개별 바디를 만들지 않고 정점을 부모 후보에 합침. 트위스트본·헬퍼본이 많은 리그에서 필수. |
| Min Bone Size | 0.025 (UI 기본) | 합쳐진 바운드 크기가 이 값 미만이면 스킵하고 부모에 병합. **스케일 의존** — 미터 단위 기준. |
| Min Weld Size | 1e-4 | 이보다 작은 피팅 결과는 위로 전파하지 않고 버린다. |
| Constraints | on | 생성된 바디들 사이에 부모-자식 조인트도 같이 생성. 부모는 "바디를 가진 가장 가까운 조상 본". |
| Disable Adjacent Pair Collision | on | 위에서 만든 각 조인트의 `bDisableCollisionBetweenBodies` 초기값. |
| Replace | on | **바디만** 지우고 재생성(`ClearBodySetups`). **Constraint 는 지워지지 않는다** ([PhysicsAssetAutoBodyGenerator.cpp:916](AppleJamEngine/Source/Engine/Physics/PhysicsAssetAutoBodyGenerator.cpp#L916)) — 아래 경고 참고. 끄면 없는 바디만 채운다(수동 조정 보존). |
| Skip Helper Bones | on | root / ik / socket / twist / control / dummy 류 이름 패턴 본을 제외. |
| Allow Bone-Axis Fallback | off | PCA 실패하거나 가중 정점이 부족할 때 스킵하는 대신 본 축으로 대충 만든다. 켜면 쓰레기 바디가 늘어날 수 있음. |
| Min Weight | 0.15 | 정점이 그 본에 이 이상의 스킨 웨이트를 가져야 피팅에 사용. (해당 본이 최대 가중치일 필요는 없음) |
| Min Vertices | 64 (UI 기본) | 이만큼의 가중 정점이 있어야 바디를 만든다. |

> 자동 생성은 어디까지나 초안이다. 생성 직후엔 조인트 한계가 전부 기본값(Twist ±45°, Swing 30/30)이라
> 그대로 시뮬하면 흐물거린다. 6장 표를 참고해 부위별로 손봐야 한다.

> **경고 — Regenerate 는 기존 constraint 를 고치지 못한다.** Replace 는 바디만 지우고,
> 생성 루프는 `HasConstraintBetweenBones` 로 **이미 있는 쌍을 건너뛴다**. 따라서 프레임이 잘못된
> 기존 constraint 는 그대로 살아남는다. 전체 초기화가 필요하면 constraint 를 하나씩 선택해
> 삭제한 뒤(Constraint Graph / 뷰포트에서 Delete) Regenerate 해야 한다.
> `UPhysicsAsset::ClearConstraintSetups()` 는 API 만 있고 호출하는 UI 가 없다.

---

## 9. 에디터 시뮬레이션 옵션

`FPhysicsAssetSimulationOptions` 중 에디터가 노출하는 것
([PhysicsAssetEditorWidget.cpp:3737](AppleJamEngine/Source/Editor/UI/Asset/Physics/PhysicsAssetEditorWidget.cpp#L3737)):

| UI | 옵션 | 효과 |
|---|---|---|
| Simulate / Stop / Pause / Reset | — | Stop 은 포즈를 되돌린다(`bResetPose`). Reset = Stop + Start. |
| No Gravity | `bNoGravity` | 모든 바디의 `bEnableGravity` 를 강제로 끈다. 낙하 없이 **초기 상호침투와 조인트 한계만** 확인할 때 유용. 토글하면 시뮬이 재시작된다. |
| Selected Simulation | `bSelectedOnly` + `SelectedBoneName` | 선택 바디와 그 자손 체인만 Dynamic, 나머지는 **Kinematic 앵커**. 예: 목만 매달아 흔들어 보기. 조인트는 양끝 중 하나라도 시뮬 대상이면 생성된다. |
| (항상) | `bForceQueryAndPhysicsCollision = true` | 에디터 프리뷰에선 컴포넌트 충돌 설정을 무시하고 전 채널 Block 으로 강제. |
| 뷰포트 좌드래그 | — | `BeginEditorRagdollGrab` — 레이에 가장 가까운 바디를 잡아 끈다. |

`Export JSON` 은 현재 에셋/에디터 상태를 `Saves/PhysicsAsset_Debug.json` 으로 덤프한다(디버깅용).

---

## 10. 런타임 사용법

전부 `USkeletalMeshComponent` 의 멤버
([SkeletalMeshComponent.h](AppleJamEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.h)).

```cpp
// 전신 래그돌
bool  EnableRagdollPhysics();
void  DisableRagdollPhysics();
bool  BeginRagdollRecovery();            // 기상 애니메이션으로 블렌드아웃

// 부분 래그돌 (히트 리액션)
bool  TriggerPartialRagdoll(const FPartialRagdollRequest&);
bool  TriggerPartialRagdollHitReaction(const FPartialRagdollHitReactionRequest&);
bool  EnablePartialRagdoll(const FName& RootBoneName);
void  DisablePartialRagdoll();

// 임펄스
bool  ApplyRagdollImpulse(const FRagdollImpulseRequest&);
bool  ApplyRagdollShockwave(const FRagdollShockwaveRequest&);

// 상태
bool  IsRagdollActive() const;
bool  IsPartialRagdollActive() const;
ERagdollMode GetRagdollMode() const;      // None / Partial / FullBody
float GetPhysicsAssetBlendWeight() const;
```

`EnableRagdollPhysics()` 가 내부적으로 쓰는 시뮬레이션 옵션
([SkeletalMeshComponent.cpp:1315](AppleJamEngine/Source/Engine/Component/Primitive/SkeletalMeshComponent.cpp#L1315)):
`bUseIndependentRagdollCollision = true`, `IndependentCollisionEnabled = QueryAndPhysics`,
`bIndependentGenerateOverlapEvents = false`. 부분 래그돌은 여기에 `bPartialSimulation`,
`PartialRootBoneName`, `bSuppressSameActorPrimitive*ForPartial = true` 가 추가된다.

### 블렌드 튜닝 프로퍼티 (에디터 디테일 패널 노출)

`Physics|Ragdoll` 카테고리: `RagdollBlendInTime`(0.15), `RagdollRecoveryBlendOutTime`(0.3),
`RagdollFirstValidPoseBlendInTime`(0.08), `RagdollCompletionHoldTime`(0.05), `RagdollFallbackHoldTime`(0.12),
기상 애니메이션(Front/Back Stand Up Animation).
`Physics|Partial Ragdoll`: 블렌드 인/아웃/홀드 타임.
`Physics|Ragdoll Launch`: 임펄스 크기 계열.

### 포즈 역전파가 실제로 하는 일

`PullPhysicsPose` ([PhysicsAssetInstance.cpp:732](AppleJamEngine/Source/Engine/Physics/PhysicsAssetInstance.cpp#L732)):

1. 현재 애니메이션 포즈로 전체 본 월드 트랜스폼을 채운다 (바디 없는 본이 무너지지 않게).
2. 바디가 있는 본만 물리 스냅샷에서 샘플링해 `BodyLocalFrame` 역변환으로 본 트랜스폼을 만든다.
3. 래그돌 루트보다 위쪽(바디 없는 조상)은 자식으로부터 역산해 채운다 — 메시가 공중에 뜨는 것을 방지.
4. **바디 없는 자손 본은 갱신된 부모 아래에서 기존 로컬 오프셋을 유지**하며 재합성한다.
   그래서 바디를 안 만든 꼬리·갈기 본들은 부모를 따라오되 물리적으로 흔들리지는 않는다.
5. 하나도 적용 못 했으면 false — 컴포넌트는 이를 "아직 준비 안 됨"으로 보고 래그돌을 해제하지 않는다
   (바디 생성이 커맨드 큐를 거치므로 첫 몇 틱은 스냅샷이 비어 있다).

---

## 11. 알려진 한계 / 함정

1. **`bEnableProjection` 은 현재 무동작.** `PxConstraintFlag::ePROJECTION` 은 켜지지만
   `setProjectionLinearTolerance` / `setProjectionAngularTolerance` 를 아무도 호출하지 않아
   PhysX 기본값(선형 `1e10`, 각 `π`)이 그대로 남는다. 즉 임계값에 영원히 도달하지 않는다.
   조인트가 늘어나는 문제는 지금으로선 솔버 반복 수·질량비·damping 으로 잡아야 한다.
   (고치려면 `FPhysXConstraintBuilder::CreateD6Joint` 에 톨러런스 설정을 추가하면 된다.)
2. **선형 `Limited` = 사실상 `Free`.** 빌더가 `setDistanceLimit`/`setLinearLimit` 을 호출하지 않아
   D6 기본 거리 한계(`PX_MAX_F32`)가 유지된다. 선형은 `Locked` 또는 `Free` 만 실질적 선택지.
3. **스프링/드라이브/소프트 리밋/브레이크 포스 미지원.** `PxD6JointDrive`, `PxSpring`,
   `setBreakForce` 어느 것도 노출되어 있지 않다. 즉 파워드 래그돌(애니메이션 추종형)은 불가.
4. **셰이프 머티리얼 지정 불가.** 전역 기본 머티리얼 하나를 공유(마찰 0.5, 반발 0.3).
   래그돌이 바닥에서 너무 미끄러지거나 튄다면 현재로선 damping 으로 우회.
5. **Convex / TriangleMesh 셰이프는 PhysicsAsset 경로에서 버려진다.** Box/Sphere/Capsule 만.
6. **CCD 는 프로젝트 설정 의존.** 바디에서 켜도 Scene 플래그가 꺼져 있으면 무의미.
7. **Body 순서가 래그돌 루트를 정한다.** `FPhysicsAssetInstance::Initialize` 는
   **BodySetups[0] 의 본**을 `RagdollRootBoneIndex` 로 삼는다
   ([PhysicsAssetInstance.cpp:360](AppleJamEngine/Source/Engine/Physics/PhysicsAssetInstance.cpp#L360)).
   골반/몸통이 첫 번째 바디가 되도록 유지할 것 — 아니면 위쪽 조상 본 재구성이 엉뚱한 곳에서 시작한다.
8. **본 이름이 안 맞으면 조용히 스킵된다.** 검증(Validate) UI 는 현재 주석 처리되어 있어
   실수가 로그로만 드러난다. FBX 재임포트로 본 이름이 바뀌면 래그돌이 절반만 도는 현상이 생긴다.
   `UPhysicsAsset::RepairInvalidLegacyConstraintNamesFromSkeleton()` 가 일부 케이스를 복구해 준다.
9. **스케일은 물리에 전달되지 않는다.** 컴포넌트 스케일을 1 이 아닌 값으로 쓰면 셰이프 크기가 따라가지 않는다.

## 12. 문제 → 확인 순서

| 증상 | 먼저 볼 것 |
|---|---|
| 시뮬 버튼이 비활성 | PhysicsAsset / 프리뷰 메시 / 프리뷰 월드 중 하나가 없음 (`bCanSimulate`) |
| 바디가 하나도 안 생김 | `BoneName` 오타, `Shapes` 비어 있음, 스켈레톤 바인딩 불일치 → 로그의 `Skipped PhysicsAsset body` |
| 조인트만 안 생김 | 양쪽 본 중 하나에 Body 없음 → `Skipped PhysicsAsset constraint: missing body handle` |
| **Simulate 누르자마자 바디가 한 점으로 수축** | **constraint 프레임이 Identity.** 에디터에서 손으로 만든 constraint(`AddDefaultConstraintForBones`)는 프레임을 채우지 않아 Parent/Child 프레임이 각 바디 원점이 되고, Linear XYZ 기본값이 `Locked` 라 D6 가 "두 바디 원점 일치"를 강제 → 체인 전체가 루트로 끌려간다. 아래 상세 참고 |
| 시작하자마자 폭발 | 초기 상호침투(No Gravity 로 확인), 인접 바디 질량비 과다, 두 constraint 프레임이 안 겹침 |
| 관절이 고무처럼 늘어남 | `PositionSolverIterationCount` ↑, 질량비 조정 (projection 은 위 1번 때문에 도움 안 됨) |
| 계속 미세하게 떨림 | `AngularDamping` ↑, `MaxAngularVelocity` ↓, 불필요한 축 `Locked` 처리 |
| 사지가 몸통을 뚫음 | 해당 쌍이 인접이 아니면 셰이프를 줄이거나 Body 를 합칠 것 (비인접 쌍은 기본적으로 충돌함) |
| 인접 부위가 서로 밀어냄 | 해당 Constraint 의 `bDisableCollisionBetweenBodies` 가 꺼져 있음 |
| 메시가 공중에 뜬 채 래그돌 | BodySetups[0] 이 루트 바디가 아님 (위 10-7) |
| 래그돌이 캐릭터 캡슐과 싸움 | 게임플레이 경로(`EnableRagdollPhysics`)를 쓰지 않고 직접 `CreateBodiesAndConstraints()` 를 호출한 경우 — `bUseIndependentRagdollCollision` 이 꺼져 있다 |

### 상세: 바디가 한 점으로 수축하는 경우

가장 흔한 초기 실패다. 원인은 **프레임이 Identity 인 constraint** 하나로 설명된다.

- `AddDefaultConstraintForBones` ([PhysicsAssetEditorWidget.cpp:3454](AppleJamEngine/Source/Editor/UI/Asset/Physics/PhysicsAssetEditorWidget.cpp#L3454))는
  본 이름만 채우고 두 LocalFrame 을 기본값(Identity)으로 남긴다. 프레임을 실제로 계산하는 코드는
  **자동 생성기 하나뿐**이다.
- 프레임이 Identity 면 `ParentFrame(world) = ParentBody(world)`, `ChildFrame(world) = ChildBody(world)`.
  여기에 `Limits.LinearX/Y/Z` 기본값 `Locked` 가 겹치면 D6 는 **두 바디의 원점을 일치시키라**는 구속이 된다.
  자식이 부모 원점으로 끌려가고 체인을 타고 전파되어 전부 루트 바디 원점으로 모인다.

**확인** (둘 중 하나면 확정)

1. constraint 선택 → Details 의 Parent/Child Local Frame **Location 이 둘 다 (0,0,0)**.
2. 뷰포트 constraint 디버그 선이 **두 바디 중심을 잇는 긴 선**으로 보인다(정상이면 점 하나).

**해결**

- **권장**: 기존 constraint 를 전부 삭제한 뒤 Regenerate Bodies(Constraints + Replace 켜고) 실행.
  Regenerate 만 다시 눌러서는 안 고쳐진다(7장 경고 참고).
- **수동**: Child Frame 기즈모를 자식 본 원점으로, Parent Frame 기즈모도 같은 월드 지점으로 옮긴다.
- **진단 확인용**: 아무 constraint 의 Linear X/Y/Z 를 `Free` 로 바꾸고 Simulate — 그 관절만 수축이
  멈추면 진단 확정. 그 상태로 두면 바디가 붙지 않고 흩어지므로 확인 후 `Locked` 로 되돌릴 것.

> 관련: Regenerate 로 constraint 를 만든 **뒤에 바디를 기즈모로 옮기면** 같은 문제가 부분적으로 생긴다.
> 프레임은 바디 기준 상대값이라 바디를 움직이는 순간 저장된 프레임이 어긋난다.
> 바디를 옮겼으면 그 바디에 물린 constraint 프레임을 다시 잡아줘야 한다.
