#pragma once

#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"

#include "Source/Game/Horse/ObstacleFanSensorComponent.generated.h"

class IPhysicsScene;
struct FHitResult;

// 전방 부채꼴 box sweep 으로 장애물 회피용 clearance map 을 Blackboard 에 기록(HorseBBKeys::ObsClear)
// 부채꼴 각도는 HorseBBKeys::ObsSlotAngles 상수값 사용
// 
// 소비: UHorseLocomotionComponent 의 context-steering
// NOTE: 현재는 WorldStatic 대상만. 방향은 Component forward 기준.
UCLASS()
class UObstacleFanSensorComponent : public USceneComponent
{
public:
	GENERATED_BODY();
	UObstacleFanSensorComponent();

	void BeginPlay() override;

	// Editor time preview
	void ContributeSelectedVisuals(FScene& Scene) const override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	// 단일 슬롯에 대해 1차 검사 결과를 기반으로 2차/3차 검사(Traversable check) 수행
	bool BuildGroundAlignedFrame(const FVector& PlanarDir, const FVector& GroundUp,
		FVector& OutForward, FVector& OutRight, FVector& OutUp, FQuat& OutRotation) const;

	bool IsTraversableTerrain(IPhysicsScene* Physics, const FVector& Origin, 
						const FVector& PlanarDir, const FHitResult& SweepHit) const;
	// 2차 검사: 만약 1차 검사점의 normal이 걸을 수 없는 각도라면, 그것이 허용할만한 단차인지 체크
	bool IsClearTraversableStepAtContact(IPhysicsScene* Physics, const FVector& Origin,
						const FVector& PlanarDir, float ContactDistance, float WalkableNormalZ) const;
	// 3차 검사: 장애물은 없다고 판정되었을 때, 이제 지면이 걸을 수 있는 경사인지 체크.
	bool HasTraversableTerrainProfile(IPhysicsScene* Physics, const FVector& Origin,
						const FVector& PlanarDir, float WalkableNormalZ) const;
	bool SampleWalkableGround(IPhysicsScene* Physics, const FVector& RayStart, float RayLength,
						float WalkableNormalZ, FHitResult& OutGroundHit) const;

	// ── 장애물 탐지 관련 ───────────────────────────────────
	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Probe Range", Min=0.0f, Max=50.0f, Speed=0.1f)
	float ProbeRange = 6.0f;      // m — 각 sweep 최대 이동거리. 미탐지 시 clearance 로 기록되는 값.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Body Half Width", Min=0.0f, Max=3.0f, Speed=0.02f)
	float BodyHalfWidth = 0.2f;   // m — sweep box의 XY방향 extent. 너무 작으면 장애물 인식력이 떨어지고 
								  // 너무 크면 말이 벽에 옆으로 붙었을 때 반대방향 센서까지 걸려버림

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Body Half Height", Min=0.0f, Max=3.0f, Speed=0.02f)
	float BodyHalfHeight = 0.7f;  // m — component 기본 높이(지면+1.2m) 기준 box 하단이 약 0.2m가 된다.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Ground Bias Angle", Min=0.0f, Max=15.0f, Speed=0.1f)
	float GroundBiasAngle = 5.0f; // deg — box 정렬은 유지하고 sweep 경로만 지면 접선보다 위로 든다.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Max Ground Alignment Angle", Min=0.0f, Max=90.0f, Speed=0.5f)
	float MaxGroundAlignmentAngle = 30.0f;

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Terrain Sample Spacing", Min=0.1f, Max=2.0f, Speed=0.05f)
	float TerrainSampleSpacing = 0.5f; // m — sweep 충돌이 지형인지 확인할 수직 ray 간격.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Terrain Probe Up", Min=0.0f, Max=5.0f, Speed=0.05f)
	float TerrainProbeUp = 1.0f;       // m — 직전 지면 높이에서 ray 시작점을 올리는 거리.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Terrain Probe Down", Min=0.0f, Max=10.0f, Speed=0.05f)
	float TerrainProbeDown = 3.0f;     // m — 직전 지면 높이 아래까지 탐색하는 거리.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Max Terrain Step", Min=0.0f, Max=3.0f, Speed=0.02f)
	float MaxTerrainStep = 0.5f;       // m — 인접 표본 사이에 허용할 최대 단차.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Contact Sample Half Span", Min=0.01f, Max=1.0f, Speed=0.01f)
	float ContactSampleHalfSpan = 0.1f; // m - sweep contact point local terrain sample half span.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Terrain Clearance Sweep Radius", Min=0.01f, Max=1.0f, Speed=0.01f)
	float TerrainClearanceSweepRadius = 0.05f; // m - sphere radius for thin-wall validation.

	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Terrain Clearance Margin", Min=0.0f, Max=0.5f, Speed=0.01f)
	float TerrainClearanceMargin = 0.02f; // m - clearance above the higher local terrain sample.
	UPROPERTY(Edit, Save, Category="Sensor", DisplayName="Walkable Terrain Deg", Min=0.0f, Max=90.0f, Speed=0.5f)
	float WalkableTerrainDeg = 45.0f;  // deg — 각 표본에서 허용할 최대 지면 경사.
									   // NOTE: 순간적인 경사 튐을 고려해서 walkableSlope보다 널널하게 설정

	// ── 디버깅 시각화 옵션 ───────────────────────────────────
	UPROPERTY(Edit, Save, Category="Sensor|Debug", DisplayName="Draw Debug")
	bool  bDrawDebug = true;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
	TWeakObjectPtr<class UHorseMovementComponent> MovementComp;
};
