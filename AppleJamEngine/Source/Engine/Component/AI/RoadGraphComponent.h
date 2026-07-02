#pragma once

#include "Component/ActorComponent.h"
#include "AI/RoadNetwork/RoadGraph.h"

#include "Source/Engine/Component/AI/RoadGraphComponent.generated.h"

// NOTE: URoadGraphComponent는 아직 프로토타입이며 아직 다듬어야 함 (EnableEditorTick() 등등)
UCLASS()
class URoadGraphComponent : public UActorComponent
{
public:
	GENERATED_BODY()

	URoadGraphComponent();

	// 등록 시(에디터 배치 / 씬 로드) 소유 액터의 에디터 틱을 켬
	void CreateRenderState() override;
	void PostEditProperty(const char* PropertyName) override;

	// NOTE: AI 등이 쿼리할 때 FRoadGraph를 직접 가져오지 않고 컴포넌트에서 제공하는 wrapper를 사용할지 고민 필요
	const FRoadGraph& GetRoadGraph() const { return RoadGraph; }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	// 소유 액터가 에디터(비 PIE) 뷰포트에서도 틱하도록 표시한다.
	void EnableEditorTick();
	void DrawRoadNetwork() const;

	UPROPERTY(Edit, Save, Category="Road Graph", DisplayName="Road Graph", Type=Struct)
	FRoadGraph RoadGraph;

	UPROPERTY(Edit, Save, Category="Road Graph|Debug", DisplayName="Draw Debug")
	bool bDrawDebug = true;

	UPROPERTY(Edit, Save, Category="Road Graph|Debug", DisplayName="Node Radius", Min=0.0f, Max=0.0f, Speed=0.02f)
	float NodeDrawRadius = 0.5f;
};
