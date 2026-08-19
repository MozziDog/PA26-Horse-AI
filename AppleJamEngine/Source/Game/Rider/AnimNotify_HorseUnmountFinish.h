#pragma once

#include "Animation/Notify/AnimNotify.h"

#include "Source/Game/Rider/AnimNotify_HorseUnmountFinish.generated.h"

// NOTE: 정확히 끝 시각의 instant notify는 dispatch되지 않으니 Unmount 동작 끝보다 한 프레임 앞에 배치할 것
UCLASS()
class UAnimNotify_HorseUnmountFinish : public UAnimNotify
{
public:
	GENERATED_BODY()
	UAnimNotify_HorseUnmountFinish() = default;
	~UAnimNotify_HorseUnmountFinish() override = default;

	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim) override;
};
