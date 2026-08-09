#include "pch.h"

#include "Game/Horse/HorseUserGuidanceComponent.h"

#include "AI/Blackboard.h"
#include "Component/AI/BlackboardComponent.h"
#include "Game/Horse/HorseConstants.h"
#include "GameFramework/AActor.h"

#include <algorithm>
#include <cmath>

UHorseUserGuidanceComponent::UHorseUserGuidanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bTickEnabled = false;
}

void UHorseUserGuidanceComponent::BeginPlay()
{
	Super::BeginPlay();
	RebindOwnerComponents();
	ClearGuidance();
}

void UHorseUserGuidanceComponent::SetGuidanceActive(bool bActive)
{
	if (bGuidanceActive == bActive)
	{
		return;
	}

	bGuidanceActive = bActive;
	if (bGuidanceActive)
	{
		OnSteeringInput(LastSteeringInput);
	}
	else
	{
		ClearGuidance();
	}
}

void UHorseUserGuidanceComponent::OnSteeringInput(float Value)
{
	LastSteeringInput = std::clamp(Value, -1.0f, 1.0f);
	if (!bGuidanceActive)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		ClearGuidance();
		return;
	}

	const float Strength = std::abs(LastSteeringInput);
	if (Strength <= 1.e-3f)
	{
		ClearGuidance();
		return;
	}

	FVector Forward = Owner->GetActorForward();
	FVector Right = Owner->GetActorRight();
	Forward.Z = 0.0f;
	Right.Z = 0.0f;
	FVector Direction = Forward + Right * LastSteeringInput;
	Direction.Z = 0.0f;
	if (Direction.IsNearlyZero())
	{
		ClearGuidance();
		return;
	}

	PublishGuidance(Direction.Normalized(), UserWeight * Strength);
}

void UHorseUserGuidanceComponent::ClearGuidance()
{
	PublishGuidance(FVector::ZeroVector, 0.0f);
}

void UHorseUserGuidanceComponent::RebindOwnerComponents()
{
	BlackboardComponent.Reset();
	if (AActor* Owner = GetOwner())
	{
		BlackboardComponent = Owner->GetComponentByClass<UBlackboardComponent>();
	}
}

void UHorseUserGuidanceComponent::PublishGuidance(const FVector& Direction, float Weight)
{
	if (!BlackboardComponent)
	{
		RebindOwnerComponents();
	}
	if (!BlackboardComponent)
	{
		return;
	}

	FVector PlanarDirection = Direction;
	PlanarDirection.Z = 0.0f;
	const float ClampedWeight = std::max(0.0f, Weight);
	const bool bHasGuidance = ClampedWeight > 1.e-3f && !PlanarDirection.IsNearlyZero();
	if (bHasGuidance)
	{
		PlanarDirection.Normalize();
	}

	FBlackboard& Blackboard = BlackboardComponent->GetBlackboard();
	Blackboard.SetVector(HorseBBKeys::GuidanceDirection, bHasGuidance ? PlanarDirection : FVector::ZeroVector);
	Blackboard.SetFloat(HorseBBKeys::GuidanceWeight, bHasGuidance ? ClampedWeight : 0.0f);
}
