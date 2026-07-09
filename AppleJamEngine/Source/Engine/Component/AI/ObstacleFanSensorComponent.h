#pragma once

#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"

#include "Source/Engine/Component/AI/ObstacleFanSensorComponent.generated.h"

// 전방 부채꼴 raycast 로 장애물 회피용 clearance map 을 Blackboard 에 기록한다(HorseBBKeys::ObsClear).
// 부채꼴 각도는 HorseBBKeys::ObsFanAngles 를 그대로 사용해 arbiter 와 일치시킨다.
// + center 방향을 한 단계 위로 올려 쏘는 probe 로 장애물 높낮이를 판정 → 점프 가능 여부(ObsJumpable) 산출.
// 소비: UHorseLocomotionComponent 의 context-steering arbiter.
// NOTE: 현재는 WorldStatic 대상 raycast 만. 방향은 actor forward 기준.
UCLASS()
class UObstacleFanSensorComponent : public USceneComponent
{
public:
	GENERATED_BODY();
	UObstacleFanSensorComponent() = default;

	void BeginPlay() override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Probe Range", Min=0.0f, Max=50.0f, Speed=0.1f)
	float ProbeRange = 6.0f;      // m — 각 레이 최대 길이. 미탐지 시 clearance 로 기록되는 값.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Jump Probe Up", Min=0.0f, Max=3.0f, Speed=0.02f)
	float JumpProbeUp = 1.0f;     // m — center 방향을 이만큼 올려 쏨. 그 위가 뚫려 있으면 장애물 윗변이 아래 → 점프 가능.

	UPROPERTY(Edit, Save, Category="AI|Sensor", DisplayName="Draw Debug")
	bool  bDrawDebug = true;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
};
