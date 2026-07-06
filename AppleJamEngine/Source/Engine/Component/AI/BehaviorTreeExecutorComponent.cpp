#include "BehaviorTreeExecutorComponent.h"

#include "Core/TickFunction.h"
#include "GameFramework/AActor.h"
#include "AI/BT/BehaviorTreeDebug.h"
#include "Component/AI/BlackboardComponent.h"

#include <cmath>

namespace
{
	// TODO: Tree builder로 옮기기
	std::unique_ptr<FBehaviorTask> MakeTask(const char* Name, std::function<EBTResult(FBTContext&)> Fn)
	{
		return std::make_unique<FBehaviorTask>(FName(Name), std::move(Fn));
	}

	std::unique_ptr<FConditional> MakeCond(const char* Name, std::function<bool(FBTContext&)> Fn)
	{
		return std::make_unique<FConditional>(FName(Name), std::move(Fn));
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

	// 블랙보드는 형제 컴포넌트에서 찾아 캐싱한다(생산자=센서 / 소비자=BT 를 서로 모르게 디커플링).
	// 없으면 nullptr → 노드는 Ctx.Blackboard 널 체크로 대응.
	if (AActor* OwnerActor = GetOwner())
	{
		BlackboardComp = OwnerActor->GetComponentByClass<UBlackboardComponent>();
	}

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
	TArray<std::unique_ptr<FBehaviorNode>> FleeChildren;
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
	auto FleeSeq = std::make_unique<FSequence>(std::move(FleeChildren));
	FleeSeq->SetDebugLabel(FName("FleeSeq"));

	// --- Graze 서브트리 ---
	// ForceSuccess 는 자식 1개(unique_ptr)를 받는다.
	auto ChewDeco = std::make_unique<FForceSuccess>(
		MakeTask("Chew", [](FBTContext&) { return EBTResult::Running; }));
	ChewDeco->SetDebugLabel(FName("MaybeChew"));

	TArray<std::unique_ptr<FBehaviorNode>> GrazeChildren;
	GrazeChildren.push_back(MakeCond("Hungry", [Phase](FBTContext&)
		{ return Phase() < 5.0f; }));
	GrazeChildren.push_back(std::move(ChewDeco));
	auto GrazeSeq = std::make_unique<FSequence>(std::move(GrazeChildren));
	GrazeSeq->SetDebugLabel(FName("GrazeSeq"));

	// --- Root Selector ---
	TArray<std::unique_ptr<FBehaviorNode>> RootChildren;
	RootChildren.push_back(std::move(FleeSeq));
	RootChildren.push_back(std::move(GrazeSeq));
	RootChildren.push_back(MakeTask("Idle", [](FBTContext&) { return EBTResult::Running; }));
	auto Root = std::make_unique<FSelector>(std::move(RootChildren));
	Root->SetDebugLabel(FName("Root"));

	Tree = std::make_unique<FBehaviorTree>(std::move(Root));
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

	FBlackboard* Blackboard = BlackboardComp ? &BlackboardComp->GetBlackboard() : nullptr;

	// [테스트] 센서 단계 스탠드인: 블랙보드가 있으면 phase 파생값을 기록 → Details 패널 Entries 에서 라이브 확인.
	// (조건은 아직 시간기반이라 블랙보드가 없어도 트리는 정상 동작)
	if (Blackboard)
	{
		const float P = std::fmod(Elapsed, 12.0f);
		Blackboard->SetFloat(FName("Phase"), P);
		Blackboard->SetBool(FName("ThreatNear"), P >= 8.0f && P < 11.0f);
		Blackboard->SetBool(FName("Hungry"), P < 5.0f);
	}

	FBTContext Context;
	Context.Owner       = GetOwner();
	Context.DeltaTime   = DeltaTime;
	Context.FrameNumber = ++FrameCounter;   // 1 부터 시작(0 은 초기 미평가 상태로 예약)
	Context.Blackboard  = Blackboard;

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
