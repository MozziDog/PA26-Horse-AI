#include "AnimNotify_BeamFire.h"

#include "Component/Gameplay/BeamAttackComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"

void UAnimNotify_BeamFire::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* /*Anim*/)
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

	// PlayerSprayProjectileComponent 의 Lua 자동 생성 패턴과 동일하게 처리.
	UBeamAttackComponent* Beam = Owner->GetComponentByClass<UBeamAttackComponent>();
	if (!Beam)
	{
		Beam = Owner->AddComponent<UBeamAttackComponent>();
		if (Beam)
		{
			Beam->SetFName(FName("BeamAttackComponent"));
			if (Owner->HasActorBegunPlay())
			{
				Beam->BeginPlay();
			}
		}
	}

	if (Beam)
	{
		Beam->FireBeam();
	}
}
