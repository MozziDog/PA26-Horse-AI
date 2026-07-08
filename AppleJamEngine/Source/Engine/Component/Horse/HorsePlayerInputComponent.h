#pragma once
#include "Component/ActorComponent.h"

struct FHorseInputState
{
	float Throttle = 0.0f;
	float Brake = 0.0f;
	float Steering = 0.0f;
};

class UPawnMovementComponent;

#include "Source/Engine/Component/Horse/HorsePlayerInputComponent.generated.h"

// 플레이어 입력(throttle/steering) 어댑터. 매 frame 입력을 world 방향벡터로 변환해
// 소유 액터의 UPawnMovementComponent(HorseMovement 등)에 AddInputVector 로 전달한다.
// 즉 플레이어도 BT task 와 동일한 입력 경로(AddInputVector)를 탄다 — 직접 조종으로 '연기'해본
// 입력 패턴을 그대로 BT 로 옮길 수 있다.
UCLASS()
class UHorsePlayerInputComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	UHorsePlayerInputComponent() = default;
	~UHorsePlayerInputComponent() override = default;

	void BeginPlay();
	void EndPlay();
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction);
	void PostEditChangeProperty(const FPropertyChangedEvent& Event);
	void PostDuplicate();

	UFUNCTION(Callable, Category = "Horse|Input")
	void SetThrottleInput(float InThrottle);
	UFUNCTION(Callable, Category = "Horse|Input")
	void SetBrakeInput(float InBrake);
	UFUNCTION(Callable, Category = "Horse|Input")
	void SetSteeringInput(float InSteering);

protected:
	// steering(±1) 이 목표 heading 을 forward 에서 얼마나 옆으로 편향시킬지. 1.0 이면 ±45°.
	UPROPERTY(Edit, Save, Category = "Horse|Input", DisplayName = "Steer Strength", Min = 0.0f, Max = 4.0f, Speed = 0.05f)
	float SteerStrength = 1.0f;

	FHorseInputState CurrentInput;
	TWeakObjectPtr<UPawnMovementComponent> Movement;
};
