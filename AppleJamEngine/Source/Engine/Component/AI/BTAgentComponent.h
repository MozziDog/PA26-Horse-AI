#pragma once

#include "Component/ActorComponent.h"
#include "AI/BT/BehaviorTree.h"

#include <memory>

#include "Source/Engine/Component/AI/BTAgentComponent.generated.h"

class UBlackboardComponent;

// NOTE: "debug bt" 명령어를 통해 실시간 BT 판단 상태 콘솔 오버레이로 확인가능
UCLASS()
class UBTAgentComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UBTAgentComponent();
	~UBTAgentComponent() override = default;

	void BeginPlay() override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void BuildTestTree();         // 테스트용 BT (일단 하드 코딩 방식)
	void PublishSnapshot(uint64 Frame) const;

	std::unique_ptr<FBehaviorTree> Tree;
	UBlackboardComponent* BlackboardComp = nullptr;
	uint64 FrameCounter = 0;
	float  Elapsed      = 0.0f;   // 테스트용 BT를 위한 누적 시간
};
