#pragma once

#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"

#include "Source/Game/Horse/ObstacleFanSensorComponent.generated.h"

// 1)
// 전방 부채꼴 box sweep 으로 장애물 회피용 clearance map 을 Blackboard 에 기록(HorseBBKeys::ObsClear)
// 부채꼴 각도는 HorseBBKeys::ObsFanAngles 상수값 사용
// 2)
// 전방으로 low/high raycast, 장애물 높이에 따라 점프 가능 여부(ObsJumpable) 산출
// sphere sweep한 값 재사용하지 않고 별개의 ray 2개 사용 (sphere 사용하면 반지름만큼 편향 발생
// 
// 소비: UHorseLocomotionComponent 의 context-steering
// NOTE: 현재는 WorldStatic 대상만. 방향은 Component forward 기준.
enum class EJumpProbeResult : uint8
{
	NoObstacle,
	SurfaceNotFacing,
	GroundTransition,
	InsufficientUpperSpace,
	PathBlocked,
	Jumpable,
};

UCLASS()
class UObstacleFanSensorComponent : public USceneComponent
{
public:
	GENERATED_BODY();
	UObstacleFanSensorComponent();

	void BeginPlay() override;

	// 런타임 디버거에서 최종 판정 단계를 확인하기 위한 값.
	EJumpProbeResult GetLastJumpProbeResult() const { return LastJumpProbeResult; }

	// Editor time preview
	void ContributeSelectedVisuals(FScene& Scene) const override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Probe Range", Min=0.0f, Max=50.0f, Speed=0.1f)
	float ProbeRange = 6.0f;      // m — 각 sweep 최대 이동거리. 미탐지 시 clearance 로 기록되는 값.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Body Half Width", Min=0.0f, Max=3.0f, Speed=0.02f)
	float BodyRadius = 0.5f;      // m — box 좌우/전후 half extent. 기존 sphere와 같은 전방 clearance 여유를 유지한다.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Body Half Height", Min=0.0f, Max=3.0f, Speed=0.02f)
	float BodyHalfHeight = 1.0f;  // m — component 기본 높이(지면+1.2m) 기준 box 하단이 약 0.2m가 된다.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Ground Bias Angle", Min=0.0f, Max=15.0f, Speed=0.1f)
	float GroundBiasAngle = 5.0f; // deg — box 정렬은 유지하고 sweep 경로만 지면 접선보다 위로 든다.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Jump Probe Up", Min=0.0f, Max=3.0f, Speed=0.02f)
	float JumpProbeUp = 1.0f;     // m — center 방향을 이만큼 올려 쏨. 그 위가 뚫려 있으면 장애물 윗변이 아래 → 점프 가능.

	UPROPERTY(Edit, Save, Category = "AI|Sensor", DisplayName = "Jump Probe Down", Min = 0.0f, Max = 3.0f, Speed = 0.02f)
	float JumpProbeDown = 1.0f;     // m — center 방향을 이만큼 내려 쏨. low ray 높이.

	// low ray가 경사면/도로 이음매를 맞은 경우를 실제 장애물과 구분하기 위한 임시 분류 기준.
	// -Normal·Forward가 이 값보다 작으면 진행 방향을 막는 면이 아니라 지면으로 본다.
	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump", DisplayName="Min Obstacle Facing", Min=0.0f, Max=1.0f, Speed=0.02f)
	float MinObstacleFacing = 0.25f;

	// low hit 전후의 지면 높이 차가 이 값 이하이고 동일 collision component가 이어지면
	// 주행 가능한 지면 전환(경사/이음매)으로 보고 점프 후보에서 제외한다.
	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump", DisplayName="Max Ground Transition Height", Min=0.0f, Max=2.0f, Speed=0.02f)
	float MaxGroundTransitionHeight = 0.35f;

	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump", DisplayName="Ground Transition Probe Offset", Min=0.05f, Max=2.0f, Speed=0.02f)
	float GroundTransitionProbeOffset = 0.35f;

	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump", DisplayName="Ground Transition Probe Up", Min=0.1f, Max=5.0f, Speed=0.05f)
	float GroundTransitionProbeUp = 1.0f;

	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump", DisplayName="Ground Transition Probe Down", Min=0.1f, Max=10.0f, Speed=0.05f)
	float GroundTransitionProbeDown = 2.0f;

	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump", DisplayName="Min Walkable Ground Normal Z", Min=0.0f, Max=1.0f, Speed=0.02f)
	float MinWalkableGroundNormalZ = 0.7f;

	// 순간 hit를 눈으로 확인할 수 있도록 jump probe hit만 잠시 잔상으로 남긴다.
	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump", DisplayName="Jump Debug Hit Duration", Min=0.0f, Max=2.0f, Speed=0.05f)
	float JumpDebugHitDuration = 0.25f;

	// 착지 가능성은 검사하지 않는다. 고정 발사각 궤적에서 장애물 앞/뒤 지점만 계산하고,
	// 그 사이를 넉넉한 body box 한 번으로 sweep한다.
	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump Path", DisplayName="Check Jump Trajectory")
	bool bCheckJumpTrajectory = true;

	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump Path", DisplayName="Jump Trajectory Angle", Min=5.0f, Max=85.0f, Speed=0.5f)
	float JumpTrajectoryAngle = 35.0f;

	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump Path", DisplayName="Jump Path Beyond Obstacle", Min=0.0f, Max=5.0f, Speed=0.05f)
	float JumpPathBeyondObstacle = 0.75f;

	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump Path", DisplayName="Jump Path Before Obstacle", Min=0.0f, Max=5.0f, Speed=0.05f)
	float JumpPathBeforeObstacle = 0.5f;

	UPROPERTY(Edit, Save, Category="AI|Sensor|Jump Path", DisplayName="Jump Path Box Padding", Min=0.0f, Max=2.0f, Speed=0.02f)
	float JumpPathBoxPadding = 0.15f;

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Draw Debug")
	bool  bDrawDebug = true;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
	TWeakObjectPtr<class UHorseMovementComponent> MovementComp;
	EJumpProbeResult LastJumpProbeResult = EJumpProbeResult::NoObstacle;
};
