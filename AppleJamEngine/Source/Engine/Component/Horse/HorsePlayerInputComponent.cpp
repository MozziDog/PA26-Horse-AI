#include "pch.h"
#include "HorsePlayerInputComponent.h"

#include "GameFramework/AActor.h"
#include "Component/AI/BlackboardComponent.h"

void UHorsePlayerInputComponent::BeginPlay()
{
	// Super::BeginPlay();

	Blackboard = GetOwner()->GetComponentByClass<UBlackboardComponent>();
}

void UHorsePlayerInputComponent::EndPlay()
{
	// TODO: Implement EndPlay()

	// Super::EndPlay();
}

void UHorsePlayerInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	// Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// 입력을 블랙보드에 기록
	if (Blackboard.IsValid())
	{
		FBlackboard& BB = Blackboard->GetBlackboard();
		BB.SetFloat(FName("InputThrottle"), CurrentInput.Throttle);
		BB.SetFloat(FName("InputBrake"),    CurrentInput.Brake);
		BB.SetFloat(FName("InputSteering"), CurrentInput.Steering);
		CurrentInput = { 0.0f, 0.0f, 0.0f };	// consume input
	}
}

void UHorsePlayerInputComponent::PostEditChangeProperty(const FPropertyChangedEvent& Event)
{
	// Super::PostEditChangeProperty(Event);

	// TODO: Add property change handling here
}

void UHorsePlayerInputComponent::PostDuplicate()
{
	// Super::PostDuplicate();
	// TODO: Add post duplicate handling here
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

float UHorsePlayerInputComponent::GetForwardSpeed() const
{
	// TODO: Implement GetForwardSpeed()
	return 0.0f;
}
