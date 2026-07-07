#pragma once
#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"
#include "Physics/IPhysicsScene.h"

#include "Source/Engine/Component/AI/RaycastSensorComponent.generated.h"

// Raycast를 통해 보이는 지형/장애물 등을 탐지하는 컴포넌트
// NOTE: 현재는 WorldStatic을 대상으로한 레이캐스팅만 지원함
UCLASS()
class URaycastSensorComponent : public USceneComponent
{
public:
	GENERATED_BODY();
	URaycastSensorComponent() = default;

	void BeginPlay() override;

	// 선택된 액터의 컴포넌트에 대해 매 프레임 호출 — tick 없어도 에디터 상에서 프리뷰 표시
	void ContributeSelectedVisuals(FScene& Scene) const override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	UPROPERTY(Edit, Save, Category = "AI|Sensor")
	FName BlackboardKey;

	UPROPERTY(Edit, Save, Category = "AI|Sensor", DisplayName = "RayDir(Local Space)")
	FVector RayDir;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
};