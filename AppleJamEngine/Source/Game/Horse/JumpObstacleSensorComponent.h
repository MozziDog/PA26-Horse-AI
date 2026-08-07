#pragma once

#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"

#include "Source/Game/Horse/JumpObstacleSensorComponent.generated.h"

enum class EJumpProbeResult : uint8
{
	NoObstacle,
	SurfaceNotFacing,
	GroundTransition,
	InsufficientUpperSpace,
	PathBlocked,
	Jumpable,
};

// 전방 low/high raycast와 장애물 전후 지면 검사를 통해 점프 가능 여부를 판정한다.
// 결과는 HorseBBKeys::ObsFwdDist와 HorseBBKeys::ObsJumpable에 기록한다.
// NOTE: 현재는 WorldStatic 대상만. 방향은 Component forward 기준.
UCLASS()
class UJumpObstacleSensorComponent : public USceneComponent
{
public:
	GENERATED_BODY();
	UJumpObstacleSensorComponent();

	void BeginPlay() override;
	void Deactivate() override;

	// 런타임 디버거에서 최종 판정 단계를 확인하기 위한 값.
	EJumpProbeResult GetLastJumpProbeResult() const { return LastJumpProbeResult; }

	// Editor time preview
	void ContributeSelectedVisuals(FScene& Scene) const override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void ResetBlackboardResult() const;

	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Jump Probe Range", Min=0.0f, Max=50.0f, Speed=0.1f)
	float JumpProbeRange = 10.0f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Jump Probe Up", Min=0.0f, Max=3.0f, Speed=0.02f)
	float JumpProbeUp = 1.0f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Jump Probe Down", Min=0.0f, Max=3.0f, Speed=0.02f)
	float JumpProbeDown = 1.0f;

	// low ray가 진행 방향을 실제로 막는 면인지 분류하는 기준.
	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Min Obstacle Facing", Min=0.0f, Max=1.0f, Speed=0.02f)
	float MinObstacleFacing = 0.25f;

	// low hit 전후의 지면 높이 차가 이 값 이하이고 동일 collision component가 이어지면
	// 주행 가능한 지면 전환(경사/이음매)으로 보고 점프 후보에서 제외한다.
	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Max Ground Transition Height", Min=0.0f, Max=2.0f, Speed=0.02f)
	float MaxGroundTransitionHeight = 0.35f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Ground Transition Probe Offset", Min=0.05f, Max=2.0f, Speed=0.02f)
	float GroundTransitionProbeOffset = 0.35f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Ground Transition Probe Up", Min=0.1f, Max=5.0f, Speed=0.05f)
	float GroundTransitionProbeUp = 1.0f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Ground Transition Probe Down", Min=0.1f, Max=10.0f, Speed=0.05f)
	float GroundTransitionProbeDown = 2.0f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump", DisplayName="Max Walkable Ground Deg", Min=0.0f, Max=90.0f, Speed=0.5f)
	float MaxWalkableGroundDeg = 45.0f;

	// 착지 가능성은 검사하지 않는다. 고정 발사각 궤적에서 장애물 앞/뒤 지점만 계산하고,
	// 그 사이를 rough box sweep 1회로 검사한다.
	UPROPERTY(Edit, Save, Category="Sensor|Jump Path", DisplayName="Check Jump Trajectory")
	bool bCheckJumpTrajectory = true;

	UPROPERTY(Edit, Save, Category="Sensor|Jump Path", DisplayName="Jump Trajectory Angle", Min=5.0f, Max=85.0f, Speed=0.5f)
	float JumpTrajectoryAngle = 35.0f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump Path", DisplayName="Jump Path Beyond Obstacle", Min=0.0f, Max=5.0f, Speed=0.05f)
	float JumpPathBeyondObstacle = 0.75f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump Path", DisplayName="Jump Path Before Obstacle", Min=0.0f, Max=5.0f, Speed=0.05f)
	float JumpPathBeforeObstacle = 0.5f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump Path", DisplayName="Jump Path Half Width", Min=0.0f, Max=3.0f, Speed=0.02f)
	float JumpPathHalfWidth = 0.5f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump Path", DisplayName="Jump Path Half Height", Min=0.0f, Max=3.0f, Speed=0.02f)
	float JumpPathHalfHeight = 1.0f;

	UPROPERTY(Edit, Save, Category="Sensor|Jump Path", DisplayName="Jump Path Box Padding", Min=0.0f, Max=2.0f, Speed=0.02f)
	float JumpPathBoxPadding = 0.15f;

	UPROPERTY(Edit, Save, Category="Sensor|Debug", DisplayName="Draw Jump Debug")
	bool bDrawJumpDebug = true;

	UPROPERTY(Edit, Save, Category="Sensor|Debug", DisplayName="Draw Jump Debug Duration", Min=0.0f, Max=2.0f, Speed=0.05f)
	float JumpDebugHitDuration = 0.25f;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
	TWeakObjectPtr<class UHorseMovementComponent> MovementComp;
	EJumpProbeResult LastJumpProbeResult = EJumpProbeResult::NoObstacle;
};
