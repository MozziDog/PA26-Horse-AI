#pragma once

#include "Component/ActorComponent.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Game/Horse/HorseUserGuidanceComponent.generated.h"

class UBlackboardComponent;

// 탑승(Mount)상태에서 유저의 조향 입력을 Blackboard에 전달하는 Guidance producer
UCLASS()
class UHorseUserGuidanceComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UHorseUserGuidanceComponent();

	void BeginPlay() override;

	UFUNCTION(Callable, Category="Horse|Guidance")
	void SetGuidanceActive(bool bActive);
	UFUNCTION(Callable, Category="Horse|Guidance")
	void OnSteeringInput(float Value);
	UFUNCTION(Callable, Category="Horse|Guidance")
	void ClearGuidance();
	UFUNCTION(Pure, Category="Horse|Guidance")
	bool IsGuidanceActive() const { return bGuidanceActive; }

private:
	void RebindOwnerComponents();
	void PublishGuidance(const FVector& Direction, float Weight);

	TWeakObjectPtr<UBlackboardComponent> BlackboardComponent = nullptr;

	UPROPERTY(Edit, Save, Category="Horse|Guidance", DisplayName="User Guidance Weight", Min=0.0f, Max=20.0f, Speed=0.05f)
	float UserWeight = 2.0f;

	bool bGuidanceActive = false;
	float LastSteeringInput = 0.0f;
};
