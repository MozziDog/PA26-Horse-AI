#pragma once

#include "Animation/Notify/AnimNotify.h"
#include "Core/Types/CoreTypes.h"

// 모션의 특정 시점(예: 모션 끝)에 빔 시전을 트리거하는 instant notify.
//   - 몽타주/시퀀스의 마지막 프레임에 배치하면 "모션이 끝날 때 빔 생성" 이 된다.
//   - 발화 시 owner actor 의 UBeamAttackComponent 를 찾아(없으면 추가) FireBeam() 호출.
//   - 대안 트리거(motion-end 폴링)는 Lua World.StartPlayerBeamAttack 가 같은 FireBeam() 을 부른다.

#include "Source/Engine/Animation/Notify/AnimNotify_BeamFire.generated.h"

UCLASS()
class UAnimNotify_BeamFire : public UAnimNotify
{
public:
	GENERATED_BODY()
	UAnimNotify_BeamFire() = default;
	~UAnimNotify_BeamFire() override = default;

	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Anim) override;
};
