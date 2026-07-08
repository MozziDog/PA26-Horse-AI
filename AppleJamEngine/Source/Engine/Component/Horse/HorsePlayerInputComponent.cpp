#include "pch.h"
#include "HorsePlayerInputComponent.h"

#include "Component/Movement/PawnMovementComponent.h"
#include "GameFramework/AActor.h"

#include <algorithm>

void UHorsePlayerInputComponent::BeginPlay()
{
	if (AActor* Owner = GetOwner())
	{
		Movement = Owner->GetComponentByClass<UPawnMovementComponent>();
	}
}

void UHorsePlayerInputComponent::EndPlay()
{
}

void UHorsePlayerInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	(void)DeltaTime;
	(void)TickType;
	(void)ThisTickFunction;

	AActor* Owner = GetOwner();
	UPawnMovementComponent* Move = Movement.Get();
	if (Owner && Move)
	{
		// 후진(throttle<0)은 후속 — 지금은 무입력으로 두어 MovementComponent 가 braking 감속하게 한다.
		const float Throttle = std::clamp(CurrentInput.Throttle, 0.0f, 1.0f);
		const float Steering = std::clamp(CurrentInput.Steering, -1.0f, 1.0f);

		if (Throttle > 0.0f)
		{
			FVector Forward = Owner->GetActorForward();
			FVector Right   = Owner->GetActorRight();
			Forward.Z = 0.0f;
			Right.Z   = 0.0f;

			// 목표 heading = forward 를 steering 만큼 옆으로 편향, 세기 = throttle.
			// MovementComponent 가 len=throttle 을 전진 세기로, 방향을 조향 목표로 해석한다.
			FVector Desired = Forward + Right * (Steering * SteerStrength);
			if (!Desired.IsNearlyZero())
			{
				Desired = Desired.Normalized() * Throttle;
				Move->AddInputVector(Desired, 1.0f);
			}
		}
	}

	CurrentInput = { 0.0f, 0.0f, 0.0f };   // consume input
}

void UHorsePlayerInputComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
	(void)Event;
}

void UHorsePlayerInputComponent::PostDuplicate()
{
}

void UHorsePlayerInputComponent::SetThrottleInput(float InThrottle)
{
	CurrentInput.Throttle = std::clamp(InThrottle, -1.0f, 1.0f);
}

void UHorsePlayerInputComponent::SetBrakeInput(float InBrake)
{
	CurrentInput.Brake = std::clamp(InBrake, 0.0f, 1.0f);
}

void UHorsePlayerInputComponent::SetSteeringInput(float InSteering)
{
	CurrentInput.Steering = std::clamp(InSteering, -1.0f, 1.0f);
}
