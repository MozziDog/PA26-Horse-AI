#pragma once

#include "Component/ActorComponent.h"
#include "AI/BT/BehaviorTree.h"

#include <memory>

#include "Source/Engine/Component/AI/BehaviorTreeExecutorComponent.generated.h"

class UBlackboardComponent;

// [테스트용] 하드코딩된 BehaviorTree 를 매 tick 실행하고, 실행 상태를 시각화 stat 으로 발행한다.
// 아무 액터에 붙이면 동작한다. "debug bt" 콘솔 명령으로 오버레이 확인.
UCLASS()
class UBehaviorTreeExecutorComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UBehaviorTreeExecutorComponent();
	~UBehaviorTreeExecutorComponent() override = default;

	void BeginPlay() override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void BuildTestTree();         // 하드코딩 테스트 트리 구성
	void PublishSnapshot(uint64 Frame) const;

	std::unique_ptr<FBehaviorTree> Tree;
	UBlackboardComponent* BlackboardComp = nullptr;   // BeginPlay 에서 형제 컴포넌트를 캐싱(없으면 nullptr)
	uint64 FrameCounter = 0;
	float  Elapsed      = 0.0f;   // 테스트 조건을 시간 기반으로 흔들기 위한 누적 시간
};
