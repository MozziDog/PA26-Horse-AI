#pragma once

#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"
#include "Component/AI/RoadGraphComponent.h"

#include "Source/Engine/Component/AI/RoadSensorComponent.generated.h"

UCLASS()
class URoadSensorComponent : public USceneComponent
{
public:
	GENERATED_BODY();
	URoadSensorComponent() = default;

	void BeginPlay() override;

	// 선택된 액터의 컴포넌트에 대해 매 프레임 호출 — tick 없어도 에디터 상에서 프리뷰 표시
	void ContributeSelectedVisuals(FScene& Scene) const override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	UPROPERTY(Edit, Save, Category = "AI|Sensor")
	FName BlackboardKey;
	
	TWeakObjectPtr<UWorld> World;	// Debug draw용
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
	TWeakObjectPtr<URoadGraphComponent> RoadGraphComp;
};