# BulletHell Trail Renderer Plan

## Goals

- `UBulletHellComponent`가 스폰하는 탄에 탄종별 trail 효과를 적용한다.
- 탄종별로 trail 사용 여부, trail material, 폭, 지속 시간, sample 수를 다르게 설정할 수 있게 한다.
- 많은 탄이 있어도 draw call이 탄 개수에 비례하지 않도록 trail material 단위로 batch 처리한다.
- 기존 탄 본체 렌더링 경로인 `UInstancedStaticMeshComponent` 슬롯 구조와 충돌하지 않는 별도 trail 렌더러를 둔다.
- 초기 단계부터 debug stat과 runtime 검증 지표를 제공한다.

## No Goals

- 탄별로 독립적인 shader constant parameter를 지원하지 않는다.
- trail material 내부 parameter를 runtime에서 탄마다 수정하지 않는다.
- kill 이후 남는 trail fade-out은 MVP 범위에 포함하지 않는다.
- trail끼리의 per-segment 정렬, OIT, per-bullet draw call은 MVP 범위에 포함하지 않는다.
- collision, damage, homing 동작은 변경하지 않는다.

## Implementation Policy

- `FBulletArchetype`을 trail authoring의 기준으로 사용한다.
- `FBulletSpawnParams`로 들어온 archetype의 trail 설정은 spawn 시 `FBulletInstance`에 복사한다.
- trail이 꺼진 탄은 sample 저장, batch 등록, 렌더 제출을 모두 하지 않는다.
- trail batch key는 우선 `TrailMaterialPath`로 둔다.
- 탄별 색, 폭, alpha fade는 vertex data로 처리한다.
- material texture, blend state, depth state, shader parameter가 달라야 하는 경우에는 별도 material asset을 사용한다.
- trail renderer는 별도 `UPrimitiveComponent`와 scene proxy로 구현한다.
- bounds 변경 시 `MarkWorldBoundsDirty()`, render data 변경 시 `MarkRenderStateDirty()` 또는 proxy dynamic buffer 갱신 경로를 명확히 사용한다.

## Data Model

### `FBulletTrailSettings`

`FBulletArchetype`에 아래 설정을 추가한다.

```cpp
struct FBulletTrailSettings
{
	bool bEnableTrail = false;
	FString MaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";
	FVector4 Color = { 1.0f, 0.6f, 0.15f, 1.0f };
	float Width = 0.08f;
	float Lifetime = 0.12f;
	int32 MaxSamples = 8;
	float SampleInterval = 0.02f;
	float MinSampleDistance = 0.05f;
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
};
```

### `FBulletInstance`

`FBulletInstance`에는 runtime 복사본과 sample 상태만 둔다.

```cpp
struct FBulletTrailSample
{
	FVector Position = FVector::ZeroVector;
	float Age = 0.0f;
};
```

필드:

- `FBulletTrailSettings Trail`
- `TArray<FBulletTrailSample> TrailSamples`
- `float TrailSampleAccumulator`
- `int32 TrailBatchIndex`

## Render Ownership

### `UBulletHellComponent`

역할:

- 탄 runtime state와 trail sample state를 소유한다.
- spawn 시 trail 설정을 `FBulletInstance`로 복사한다.
- tick 중 위치 갱신 후 trail sample을 갱신한다.
- 자동 생성된 `UBulletTrailComponent`에 frame snapshot을 전달한다.
- debug stat을 수집한다.

### `UBulletTrailComponent`

역할:

- owner actor에 자동 생성되는 렌더 전용 primitive component다.
- `UBulletHellComponent`가 넘긴 trail snapshot을 보관한다.
- world bounds를 계산한다.
- `FBulletTrailSceneProxy`를 생성한다.

### `FBulletTrailSceneProxy`

역할:

- trail material batch별 dynamic vertex/index buffer를 관리한다.
- batch별 section을 만든다.
- section material은 batch의 `TrailMaterialPath`로 로드한 material을 사용한다.
- geometry는 camera-facing strip으로 생성한다.

## Batch Model

### Batch Key

MVP batch key:

```cpp
struct FBulletTrailBatchKey
{
	FString MaterialPath;
};
```

향후 확장 후보:

- `BlendMode`가 material path에서 분리될 경우 render state key 추가
- `TextureTileMode`가 vertex UV만으로 처리되지 않을 경우 key 추가

### Batch Data

```cpp
struct FBulletTrailBatch
{
	FString MaterialPath;
	TArray<int32> BulletIndices;
};
```

batch 재구성은 매 프레임 snapshot 생성 시 수행한다. 탄 수가 많아질 경우 material path별 map을 재사용하는 최적화를 별도 phase에서 추가한다.

## Geometry Rules

- sample chain은 오래된 sample에서 최신 sample 순서로 정렬한다.
- sample 1개만 있으면 렌더하지 않는다.
- sample point마다 좌/우 vertex 2개를 생성한다.
- segment 1개마다 index 6개를 생성한다.
- strip side vector는 camera position과 segment tangent를 사용해 계산한다.
- `Width`는 world unit 기준이다.
- `Color.a`는 sample age 기반 fade를 곱한다.
- `UV.x`는 trail 길이 누적값 또는 normalized chain alpha로 둔다.
- `UV.y`는 좌/우에 `0/1`을 부여한다.

## Phase 1: Archetype Settings and One-Frame Streak

### Goal

탄종별 trail 설정과 material batch 구조를 먼저 세우고, `PreviousPosition -> Position` 선분만 렌더한다.

### Tasks

- `FBulletTrailSettings`를 추가한다.
- `FBulletArchetype`에 `Trail` 필드를 추가한다.
- `FBulletInstance`에 spawn 시 trail 설정을 복사한다.
- `UBulletTrailComponent`와 `FBulletTrailSceneProxy` skeleton을 추가한다.
- `UBulletHellComponent`가 trail renderer를 자동 생성하도록 한다.
- trail이 켜진 탄의 `PreviousPosition -> Position`을 2-point strip으로 제출한다.
- `TrailMaterialPath`별 batch와 section을 만든다.
- debug stat을 추가한다.

### Validation

- trail off 탄은 trail geometry를 만들지 않는다.
- trail on 탄은 탄 이동 방향으로 짧은 streak이 보인다.
- 서로 다른 trail material을 가진 탄종은 서로 다른 section/material로 렌더된다.
- 탄 본체 material 변경과 trail material 변경이 독립적으로 동작한다.
- `TrailBatchCount`, `TrailVertexCount`, `TrailIndexCount`가 화면 상태와 일치한다.

### No Goal

- sample history는 구현하지 않는다.
- kill 이후 trail 잔상은 구현하지 않는다.

## Phase 2: Sample History Trail

### Goal

탄별 짧은 sample history를 유지해 실제 궤적을 렌더한다.

### Tasks

- `FBulletTrailSample`과 탄별 sample array를 추가한다.
- `SampleInterval`과 `MinSampleDistance` 조건으로 sample을 추가한다.
- `Lifetime`과 `MaxSamples`로 sample을 제거한다.
- sample age 기반 alpha fade를 적용한다.
- sample chain 기반 strip geometry를 만든다.
- trail bounds가 sample 전체를 포함하도록 갱신한다.

### Validation

- 빠른 탄과 느린 탄 모두 설정된 sample budget 안에서 trail 길이가 안정적으로 유지된다.
- homing 탄의 곡선 궤적이 sample chain으로 보인다.
- 탄이 대량으로 존재해도 draw call은 material batch 수에 비례한다.
- trail sample 제거 후 bounds가 과하게 남지 않는다.

### No Goal

- 삭제된 탄의 trail을 독립적으로 계속 유지하지 않는다.

## Phase 3: Pattern and Debug Authoring

### Goal

보스 패턴과 debug component에서 탄종별 trail 설정을 쉽게 조정할 수 있게 한다.

### Tasks

- `UBulletHellDebugComponent`의 primary/secondary archetype 설정에 trail 옵션을 추가한다.
- 각 boss pattern component에 필요한 trail property를 추가한다.
- pattern별 `FBulletSpawnParams` 생성 시 trail 설정을 채운다.
- trail material asset path는 `UPROPERTY(Edit, Save, AssetType="Material")` 패턴을 따른다.

### Validation

- primary/secondary debug 탄이 서로 다른 trail on/off, width, material을 사용할 수 있다.
- `HomingOrbTrail`, `SphericalPulseBarrage`, `ThunderclapCascade` 등 패턴별 trail 설정이 독립적으로 적용된다.
- 저장된 scene에서 trail 설정이 reload 후 유지된다.

### No Goal

- pattern별 custom shader parameter editor는 만들지 않는다.

## Phase 4: Budget and Observability

### Goal

대량 탄막에서 trail cost를 관찰하고 제한할 수 있게 한다.

### Tasks

- 전체 trail sample budget을 추가한다.
- 전체 trail vertex/index budget을 추가한다.
- budget 초과 시 오래된 sample 또는 낮은 priority batch부터 줄인다.
- debug stat을 overlay에 추가한다.

Suggested stats:

- `TrailEnabledBulletCount`
- `TrailSampleCount`
- `TrailBatchCount`
- `TrailVertexCount`
- `TrailIndexCount`
- `TrailTruncatedCount`
- `TrailMaterialMissingCount`

### Validation

- budget 이하에서는 visual truncation이 발생하지 않는다.
- budget 초과 시 crash 없이 `TrailTruncatedCount`가 증가한다.
- material 누락 시 fallback 또는 skip 정책이 stat으로 드러난다.

### No Goal

- GPU 기반 trail simulation은 구현하지 않는다.

## Validation Commands

문서 기반 구현 후 기본 검증은 아래 순서로 수행한다.

```bat
git diff --check
cmd /c "ReleaseBuild.bat < NUL"
```

빌드가 link 단계에서 `KraftonEngine.exe` 잠금으로 실패하면 실행 중인 에디터/게임 프로세스를 닫고 재시도한다.
