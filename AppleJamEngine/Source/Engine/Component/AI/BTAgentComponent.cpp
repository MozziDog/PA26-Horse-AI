#include "pch.h"
#include "BTAgentComponent.h"

#include "GameFramework/AActor.h"

void UBTAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Move(DeltaTime);
}

// TODO: 움직임 처리 함수는 다른 클래스로 이동
void UBTAgentComponent::Move(float DeltaTime)
{
	AActor* Owner = GetOwner();
	
	FVector Forward = Owner->GetActorForward();
	Owner->SetActorLocation(Owner->GetActorLocation() + Forward * Throttle * MoveSpeed * DeltaTime);
	
	FRotator Rotation = Owner->GetActorRotation();
	Rotation.Yaw += Steering * RotateSpeed * DeltaTime;
	Owner->SetActorRotation(Rotation.GetClamped());
}
