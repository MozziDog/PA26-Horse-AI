#include "pch.h"
#include "MountTriggerComponent.h"

#include "Component/PrimitiveComponent.h"
#include "Game/Horse/HorseCharacter.h"
#include "Game/Rider/RiderCharacter.h"

void UMountTriggerComponent::BeginPlay()
{
	Super::BeginPlay();

	BeginOverlapHandle = OnComponentBeginOverlap.AddWeakUObject(this, &UMountTriggerComponent::HandleBeginOverlap);
	EndOverlapHandle = OnComponentEndOverlap.AddWeakUObject(this, &UMountTriggerComponent::HandleEndOverlap);
}

void UMountTriggerComponent::EndPlay()
{
	if (BeginOverlapHandle.IsValid())
	{
		OnComponentBeginOverlap.Remove(BeginOverlapHandle);
		BeginOverlapHandle.Reset();
	}
	if (EndOverlapHandle.IsValid())
	{
		OnComponentEndOverlap.Remove(EndOverlapHandle);
		EndOverlapHandle.Reset();
	}

	Super::EndPlay();
}

AHorseCharacter* UMountTriggerComponent::GetMountTarget() const
{
	return Cast<AHorseCharacter>(GetOwner());
}

void UMountTriggerComponent::HandleBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/,
	bool /*bFromSweep*/,
	const FHitResult& /*SweepResult*/)
{
	AHorseCharacter* Horse = GetMountTarget();
	ARiderCharacter* Rider = Cast<ARiderCharacter>(OtherActor);
	if (!Horse || !Rider || Horse->IsRiderMounted())
	{
		return;
	}

	UE_LOG("[MountTriggerComponent] Set mount trigger by %s", UObject::GetName());
	Rider->SetMountTrigger(this);
}

void UMountTriggerComponent::HandleEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/,
	AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/)
{
	if (ARiderCharacter* Rider = Cast<ARiderCharacter>(OtherActor))
	{
		UE_LOG("[MountTriggerComponent] Unset mount trigger by %s", UObject::GetName().c_str());
		Rider->ClearMountTrigger(this);
	}
}
