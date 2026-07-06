#include "BehaviorTreeExecutorComponent.h"

#include "Core/TickFunction.h"
#include "GameFramework/AActor.h"
#include "AI/BT/BehaviorTreeDebug.h"

#include <cmath>

namespace
{
	// TODO: Tree builder로 옮기기
	std::unique_ptr<BehaviorTask> MakeTask(const char* Name, std::function<EBTResult(FBTContext&)> Fn)
	{
		return std::make_unique<BehaviorTask>(FName(Name), std::move(Fn));
	}

	std::unique_ptr<Conditional> MakeCond(const char* Name, std::function<bool(FBTContext&)> Fn)
	{
		return std::make_unique<Conditional>(FName(Name), std::move(Fn));
	}

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
	void FlattenNode(const BehaviorNode* Node, int32 Depth, uint64 Frame, TArray<FBTNodeDebugEntry>& Out)
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

		for (const BehaviorNode* Child : Node->GetChildrenForDebug())
		{
			FlattenNode(Child, Depth + 1, Frame, Out);
		}
	}
#endif
}

UBehaviorTreeExecutorComponent::UBehaviorTreeExecutorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.SetTickGroup(TG_PostPhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PostPhysics);
}

void UBehaviorTreeExecutorComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	BuildTestTree();
}

void UBehaviorTreeExecutorComponent::BuildTestTree()
{
	// 12초 주기로 상태가 바뀌는 테스트용 트리(하드코딩):
	//   0~5s  : Hungry  → Graze (Chew, Running)
	//   5~8s  : (없음)  → Idle
	//   8~11s : Threat  → Flee  (Run, Running)  ← 우선순위가 더 높아 Graze 를 선점
	//   11~12s: (없음)  → Idle
	auto Phase = [this]() { return std::fmod(Elapsed, 12.0f); };

	// --- Flee 서브트리 (최우선) ---
	TArray<std::unique_ptr<BehaviorNode>> FleeChildren;
	FleeChildren.push_back(MakeCond("ThreatNear", [Phase](FBTContext&)
		{ const float P = Phase(); return P >= 8.0f && P < 11.0f; }));
	FleeChildren.push_back(MakeTask("Run", [](FBTContext& Ctx)
		{
			// [테스트] 오너 액터의 forward 방향으로 임의 속도(3m/s)로 전진.
			if (Ctx.Owner)
			{
				constexpr float Speed = 3.0f;
				Ctx.Owner->AddActorWorldOffset(Ctx.Owner->GetActorForward() * (Speed * Ctx.DeltaTime));
			}
			return EBTResult::Running;
		}));
	auto FleeSeq = std::make_unique<Sequence>(std::move(FleeChildren));
	FleeSeq->SetDebugLabel(FName("FleeSeq"));

	// --- Graze 서브트리 ---
	// ForceSuccess 는 자식 1개(unique_ptr)를 받는다.
	auto ChewDeco = std::make_unique<ForceSuccess>(
		MakeTask("Chew", [](FBTContext&) { return EBTResult::Running; }));
	ChewDeco->SetDebugLabel(FName("MaybeChew"));

	TArray<std::unique_ptr<BehaviorNode>> GrazeChildren;
	GrazeChildren.push_back(MakeCond("Hungry", [Phase](FBTContext&)
		{ return Phase() < 5.0f; }));
	GrazeChildren.push_back(std::move(ChewDeco));
	auto GrazeSeq = std::make_unique<Sequence>(std::move(GrazeChildren));
	GrazeSeq->SetDebugLabel(FName("GrazeSeq"));

	// --- Root Selector ---
	TArray<std::unique_ptr<BehaviorNode>> RootChildren;
	RootChildren.push_back(std::move(FleeSeq));
	RootChildren.push_back(std::move(GrazeSeq));
	RootChildren.push_back(MakeTask("Idle", [](FBTContext&) { return EBTResult::Running; }));
	auto Root = std::make_unique<Selector>(std::move(RootChildren));
	Root->SetDebugLabel(FName("Root"));

	Tree = std::make_unique<BehaviorTree>(std::move(Root));
}

void UBehaviorTreeExecutorComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (!Tree)
	{
		return;
	}

	Elapsed += DeltaTime;

	FBTContext Context;
	Context.Owner       = GetOwner();
	Context.DeltaTime   = DeltaTime;
	Context.FrameNumber = ++FrameCounter;   // 1 부터 시작(0 은 초기 미평가 상태로 예약)

	Tree->Behave(Context);
	PublishSnapshot(Context.FrameNumber);
}

void UBehaviorTreeExecutorComponent::PublishSnapshot(uint64 Frame) const
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
