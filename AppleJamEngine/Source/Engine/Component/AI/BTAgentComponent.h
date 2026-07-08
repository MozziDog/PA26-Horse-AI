#pragma once

#include "Component/ActorComponent.h"
#include "AI/BT/BehaviorTree.h"

#include <memory>

#include "Source/Engine/Component/AI/BTAgentComponent.generated.h"

class UBlackboardComponent;
class UPawnMovementComponent;

// NOTE: "debug bt" 명령어를 통해 실시간 BT 판단 상태 콘솔 오버레이로 확인가능
UCLASS()
class UBTAgentComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UBTAgentComponent();
	~UBTAgentComponent() override;

	void BeginPlay() override;

	// BT 구성용 Lua 스크립트(Content/Script 기준 상대경로)를 주입한다. BeginPlay 전에 설정해야 빌드에 반영됨.
	void SetBehaviorTreeScript(const FString& InScriptPath) { BehaviorTreeScript = InScriptPath; }
	const FString& GetBehaviorTreeScript() const { return BehaviorTreeScript; }

	// FLuaScriptManager 가 .lua 변경 감지 시 호출 — 트리(및 Lua task 의 sol ref) 를 새로 빌드.
	void RebuildBehaviorTree() { BuildBehaviorTree(); }

	// Lua state shutdown 전에 트리를 drop 해 sol reference 를 (state 가 살아있는 동안) 해제.
	void ReleaseLuaForShutdown();

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void BuildBehaviorTree();     // BehaviorTreeScript 를 Lua 빌더로 구성. 실패/미지정 시 Tree = nullptr.
	void PublishSnapshot(uint64 Frame) const;

	// 어떤 트리를 실행할지는 이 스크립트가 결정. 에디터에서 직접 지정하거나 소유 액터가 코드로 주입.
	UPROPERTY(Edit, Save, Category = "AI|BehaviorTree", DisplayName = "Behavior Tree Script")
	FString BehaviorTreeScript;

	std::unique_ptr<FBehaviorTree> Tree;
	UBlackboardComponent* BlackboardComp = nullptr;
	// 입력 구동 이동 컴포넌트 캐시(BeginPlay 1회). 없으면 nullptr. 매 tick FBTContext 로 주입.
	UPawnMovementComponent* MovementComp = nullptr;
	uint64 FrameCounter = 0;
};
