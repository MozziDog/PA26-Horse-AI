#include "pch.h"
#include "BTAgentComponent.h"

#include "Core/TickFunction.h"
#include "GameFramework/AActor.h"
#include "AI/BT/BehaviorTreeDebug.h"
#include "AI/BT/BTLuaBuilder.h"
#include "Component/AI/BlackboardComponent.h"
#include "Lua/LuaScriptManager.h"

namespace
{
#if STATS
	EBTNodeDebugResult ToDebugResult(EBTResult Result)
	{
		switch (Result)
		{
		case EBTResult::Running: return EBTNodeDebugResult::Running;
		case EBTResult::Success: return EBTNodeDebugResult::Success;
		default:                 return EBTNodeDebugResult::Fail;
		}
	}

	// 디버깅 시각화용 트리 순회(DFS), 스냅샷 생성
	void FlattenNode(const FBehaviorNode* Node, int32 Depth, uint64 Frame, TArray<FBTNodeDebugEntry>& Out)
	{
		if (!Node)
		{
			return;
		}

		const FBTDebugInfo& Info = Node->GetDebugInfo();

		FBTNodeDebugEntry Entry;
		Entry.Depth               = Depth;
		Entry.TypeName            = Node->GetNodeTypeName();
		Entry.Label               = Node->GetDebugLabel().ToString();
		Entry.LastResult          = ToDebugResult(Info.LastResult);
		Entry.ActiveDuration      = Info.ActiveDuration;
		Entry.bEvaluatedThisFrame = Node->WasEvaluated(Frame);
		Entry.bOnActivePath       = Node->IsOnActivePath(Frame);
		Out.push_back(Entry);

		for (const FBehaviorNode* Child : Node->GetChildrenForDebug())
		{
			FlattenNode(Child, Depth + 1, Frame, Out);
		}
	}
#endif
}

UBTAgentComponent::UBTAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.SetTickGroup(TG_PostPhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PostPhysics);
}

UBTAgentComponent::~UBTAgentComponent()
{
	// 일반 소멸(월드 teardown) — Lua state 가 살아있는 동안 sol ref 해제. 셧다운 경로면 이미 해제돼 no-op.
	ReleaseLuaForShutdown();
}

void UBTAgentComponent::BeginPlay()
{
	UActorComponent::BeginPlay();

	if (AActor* OwnerActor = GetOwner())
	{
		BlackboardComp = OwnerActor->GetComponentByClass<UBlackboardComponent>();
	}

	BuildBehaviorTree();

	// hot-reload/셧다운 시 트리 재빌드·해제를 받기 위해 등록.
	FLuaScriptManager::RegisterBTAgent(this);
}

void UBTAgentComponent::ReleaseLuaForShutdown()
{
	FLuaScriptManager::UnregisterBTAgent(this);
	Tree.reset();   // FLuaTask 의 sol::protected_function 해제
}

void UBTAgentComponent::BuildBehaviorTree()
{
	if (BehaviorTreeScript.empty())
	{
		UE_LOG("[BT] BehaviorTreeScript 미지정 — 트리 미구성");
		Tree = nullptr;
		return;
	}

	// 구조는 Lua, 리프 로직은 FBTBehaviorRegistry 에 이름으로 등록된 C++ behavior 를 참조.
	Tree = FBTLuaBuilder::BuildTree(BehaviorTreeScript);
	if (!Tree)
	{
		UE_LOG("[BT] Lua 트리 빌드 실패: %s", BehaviorTreeScript.c_str());
	}
}

void UBTAgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (!Tree)
	{
		return;
	}

	FBTContext Context;
	Context.Owner       = GetOwner();
	Context.DeltaTime   = DeltaTime;
	Context.FrameNumber = ++FrameCounter;   // 1 부터 시작 (0 은 초기 미평가 상태)
	Context.Blackboard  = BlackboardComp ? &BlackboardComp->GetBlackboard() : nullptr;

	Tree->Behave(Context);
	PublishSnapshot(Context.FrameNumber);
}

void UBTAgentComponent::PublishSnapshot(uint64 Frame) const
{
#if STATS
	FBehaviorTreeDebugSnapshot Snapshot;
	const AActor* OwnerActor = GetOwner();
	Snapshot.OwnerName   = OwnerActor ? OwnerActor->GetName() : "None";
	Snapshot.FrameNumber = Frame;

	if (Tree)
	{
		FlattenNode(Tree->GetRootForDebug(), 0, Frame, Snapshot.Nodes);
	}

	BT_DEBUG_ADD_TREE(Snapshot);
#else
	(void)Frame;
#endif
}
