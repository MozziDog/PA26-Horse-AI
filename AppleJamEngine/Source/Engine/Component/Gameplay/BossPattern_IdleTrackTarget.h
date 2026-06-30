#pragma once

#include "Component/Gameplay/BossPatternComponentBase.h"

#include "Source/Engine/Component/Gameplay/BossPattern_IdleTrackTarget.generated.h"

UCLASS()
class UBossPattern_IdleTrackTarget : public UBossPatternComponentBase
{
public:
	GENERATED_BODY()
	UBossPattern_IdleTrackTarget();
	~UBossPattern_IdleTrackTarget() override = default;

protected:
	bool ShouldAdvanceStep(const FBossPatternContext& Context) const override;
	EBossPatternStep GetNextStep(EBossPatternStep CurrentStep) const override;

private:
	UPROPERTY(Edit, Save, Category="Boss Pattern|Idle", DisplayName="Idle Duration", Min=0.0f, Max=30.0f, Speed=0.01f)
	float IdleDuration = 0.5f;
};
