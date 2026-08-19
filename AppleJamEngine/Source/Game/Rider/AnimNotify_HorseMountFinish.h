#pragma once

#include "Animation/Notify/AnimNotify.h"

#include "Source/Game/Rider/AnimNotify_HorseMountFinish.generated.h"

// NOTE: 정확히 끝 시각의 instant notify는 dispatch되지 않으니 Mount 동작 끝보다 한 프레임 앞에 배치할 것
UCLASS()
class UAnimNotify_HorseMountFinish : public UAnimNotify
{
public:
	GENERATED_BODY()
	UAnimNotify_HorseMountFinish() = default;
	~UAnimNotify_HorseMountFinish() override = default;

	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim) override;
};
