#pragma once

#include "Animation/Notify/AnimNotify.h"
#include "Core/Types/CoreTypes.h"

// 모션의 특정 시점에 점프를 트리거하는 instant notify.

#include "Source/Game/Horse/AnimNotify_HorseJump.generated.h"

UCLASS()
class UAnimNotify_HorseJump : public UAnimNotify
{
public:
	GENERATED_BODY()
	UAnimNotify_HorseJump() = default;
	~UAnimNotify_HorseJump() override = default;

	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim) override;
};
