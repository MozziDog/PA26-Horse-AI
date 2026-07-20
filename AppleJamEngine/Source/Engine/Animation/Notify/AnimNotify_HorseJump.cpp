#include "AnimNotify_HorseJump.h"

#include "Component/Movement/HorseMovementComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"

void UAnimNotify_HorseJump::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* /*Anim*/)
{
	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	UHorseMovementComponent* HorseMovement = Owner->GetComponentByClass<UHorseMovementComponent>();
	if (HorseMovement)
	{
		UE_LOG("[HorseJumpDebug] Jump takeoff notify");
		HorseMovement->OnJumpNotify();
	}
}