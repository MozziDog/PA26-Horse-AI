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
UCLASS()
class UObstacleFanSensorComponent : public USceneComponent
{
public:
	GENERATED_BODY();
	UObstacleFanSensorComponent() = default;

	void BeginPlay() override;

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
	float JumpProbeDown = 1.0f;     // m — center 방향을 이만큼 올려 쏨. 그 위가 뚫려 있으면 장애물 윗변이 아래 → 점프 가능.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Draw Debug")
	bool  bDrawDebug = true;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
	TWeakObjectPtr<class UHorseMovementComponent> MovementComp;
};
