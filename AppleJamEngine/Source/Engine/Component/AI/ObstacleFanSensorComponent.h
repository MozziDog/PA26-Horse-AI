#pragma once

#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"

#include "Source/Engine/Component/AI/ObstacleFanSensorComponent.generated.h"

// 1)
// 전방 부채꼴 sphere sweep 으로 장애물 회피용 clearance map 을 Blackboard 에 기록(HorseBBKeys::ObsClear)
// 부채꼴 각도는 HorseBBKeys::ObsFanAngles 상수값 사용
// 2)
// 전방으로 low/high raycast, 장애물 높이에 따라 점프 가능 여부(ObsJumpable) 산출
// sphere sweep한 값 재사용하지 않고 별개의 ray 2개 사용 (sphere 사용하면 반지름만큼 편향 발생
// 
// 소비: UHorseLocomotionComponent 의 context-steering
// NOTE: 현재는 WorldStatic 대상만. 방향은 actor forward 기준.
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

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Body Radius", Min=0.0f, Max=3.0f, Speed=0.02f)
	float BodyRadius = 0.5f;      // m — sweep sphere 반경. 말 몸통 반폭(+여유). clearance 는 이 반경만큼 벽 앞에서 멈춘다.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Jump Probe Up", Min=0.0f, Max=3.0f, Speed=0.02f)
	float JumpProbeUp = 1.0f;     // m — center 방향을 이만큼 올려 쏨. 그 위가 뚫려 있으면 장애물 윗변이 아래 → 점프 가능.

	UPROPERTY(Edit, Save, Category = "AI|Sensor", DisplayName = "Jump Probe Down", Min = 0.0f, Max = 3.0f, Speed = 0.02f)
	float JumpProbeDown = 1.0f;     // m — center 방향을 이만큼 올려 쏨. 그 위가 뚫려 있으면 장애물 윗변이 아래 → 점프 가능.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Draw Debug")
	bool  bDrawDebug = true;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
};
