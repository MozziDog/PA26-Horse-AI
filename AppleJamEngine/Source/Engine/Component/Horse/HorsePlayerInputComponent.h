#pragma once
#include "Component/ActorComponent.h"

struct FHorseInputState
{
	float Throttle = 0.0f;
	float Brake = 0.0f;
	float Steering = 0.0f;
};

class UBlackboardComponent;

#include "Source/Engine/Component/Horse/HorsePlayerInputComponent.generated.h"

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

	UFUNCTION(Pure, Category = "Horse")
	float GetForwardSpeed() const;

protected:
	FHorseInputState CurrentInput;
	TWeakObjectPtr<UBlackboardComponent> Blackboard;
};

