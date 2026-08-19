#include "AnimNotify_HorseUnmountFinish.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Game/Rider/RiderCharacter.h"
#include "GameFramework/AActor.h"

void UAnimNotify_HorseUnmountFinish::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* /*Anim*/)
{
	ARiderCharacter* Rider = MeshComp ? Cast<ARiderCharacter>(MeshComp->GetOwner()) : nullptr;
	if (Rider)
	{
		Rider->OnUnmountFinishNotify();
	}
}
