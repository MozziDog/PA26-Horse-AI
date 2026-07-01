#pragma once
#include "Component/ActorComponent.h"

// NOTE: UBTAgentComponent라는 이름은 Placeholder.
//       Player input을 그대로 Move로 반영하는 임시 로직만 있는 상태.
//       BT 동작은 처음부터 구현 필요

#include "Source/Engine/Component/AI/BTAgentComponent.generated.h"

UCLASS()
class UBTAgentComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void Move(float DeltaTime);

	void SetThrottle(float InThrottle) { Throttle = InThrottle; }
	void SetBrake(float InBrake) { Brake = InBrake; }
	void SetSteering(float InSteering) { Steering = InSteering; }

protected:
	UPROPERTY(Edit, Save, Category = "AI|Movement", DisplayName = "Move Speed", Min = 0.0f, Max = 100.0f, Speed = 0.1f)
	float MoveSpeed = 10.0f;
	UPROPERTY(Edit, Save, Category = "AI|Movement", DisplayName = "Rotate Speed", Min = 0.0f, Max = 360.0f, Speed = 0.1f)
	float RotateSpeed = 30.0f;

	// TODO: AI 판단에 필요한 파라미터들 Blackboard로 옮기기
	float Throttle = 0.0f;
	float Brake = 0.0f;
	float Steering = 0.0f;
};

