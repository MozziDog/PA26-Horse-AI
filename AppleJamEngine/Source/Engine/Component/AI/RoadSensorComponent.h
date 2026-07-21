#pragma once

#include "Component/SceneComponent.h"
#include "Component/AI/BlackboardComponent.h"
#include "Component/AI/RoadGraphComponent.h"
#include "AI/HorseBlackboardKeys.h"

#include "Source/Engine/Component/AI/RoadSensorComponent.generated.h"

// NOTE: RoadSensorComponent는 actor 진행방향 앞에 배치되어 사용됨을 전제로 함
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
	// ─── 블랙보드용 Key ───
	UPROPERTY(Edit, Save, Category = "AI|Sensor")
	FName RoadDirBlackboardKey = HorseBBKeys::RoadDir; // (FVector)검출된 목표를 향한 방향 (World space, Actor pivot 기준)

	UPROPERTY(Edit, Save, Category = "AI|Sensor")
	FName DistBlackboardKey = HorseBBKeys::RoadDist; // (float)도로까지의 거리. 멀면 도로추종 중단
	
	// ─── 런타임 캐시 ───
	TWeakObjectPtr<UWorld> World;	// Debug draw용
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp;
	TWeakObjectPtr<URoadGraphComponent> RoadGraphComp;
};