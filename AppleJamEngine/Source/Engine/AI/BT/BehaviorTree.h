#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/FName.h"
#include <memory>

class AActor;
class FBlackboard;

enum class EBTResult
{
	Running, Success, Fail
};

// 트리 실행 중 노드 사이로 전달되는 공유 컨텍스트
// NOTE: Owner 등을 std::function 캡쳐 내부에서 잡아두면 댕글링 포인터 생길 수 있어서
// 매 프레임 필요한 정보를 주입해주는 방식을 채택
struct FBTContext
{
	AActor*      Owner       = nullptr;
	float        DeltaTime   = 0.0f;
	uint64       FrameNumber = 0;		// 현재 프레임 번호. 시각화가 "이번 tick 에 평가된 노드"를 판별하는 데 사용
	FBlackboard* Blackboard  = nullptr;	// executor 가 매 tick 주입. 노드는 컴포넌트가 아니라 '데이터'만 안다.
	// NOTE: task 는 이동 컴포넌트를 직접 만지지 않는다 — Blackboard 에 '판단(mode/gait 등)'만 쓰고,
	//       Locomotion 이 그걸 읽어 조향·gait 로 변환한다. (예: DesiredGait 키)

	// NOTE: 필요한 필드는 여기에 확장
};

// 노드별 디버그 스냅샷. 런타임 로직엔 영향 없음. 시각화(뷰어)가 read-only 로 읽는다.
struct FBTDebugInfo
{
	EBTResult LastResult         = EBTResult::Fail;
	uint64    LastEvaluatedFrame = 0;     // 마지막으로 Execute 된 tick
	float     ActiveDuration     = 0.0f;  // 연속 Running 유지 시간(전이 시 0)
};

class FBehaviorNode
{
public:
	virtual ~FBehaviorNode() = default;

	// 노드는 자식을 unique_ptr 로 소유하므로 복사 금지
	FBehaviorNode()                               = default;
	FBehaviorNode(const FBehaviorNode&)            = delete;
	FBehaviorNode& operator=(const FBehaviorNode&) = delete;

	EBTResult Execute(FBTContext& Context)
	{
		if (!bActive)
		{
			bActive = true;
			OnEnter(Context);
		}

		// 실제 동작 수행
		EBTResult Result = OnBehave(Context);

		// 디버그 정보 채우기
		if (Result == EBTResult::Running
			&& Debug.LastResult == EBTResult::Running
			&& Debug.LastEvaluatedFrame == Context.FrameNumber - 1)
			Debug.ActiveDuration += Context.DeltaTime;   // 연속 Running 유지
		else if (Result == EBTResult::Running)
			Debug.ActiveDuration = Context.DeltaTime;     // Running 진입
		else
			Debug.ActiveDuration = 0.0f;

		Debug.LastResult         = Result;
		Debug.LastEvaluatedFrame = Context.FrameNumber;

		if (Result != EBTResult::Running)
		{
			OnExit(Context);
			bActive = false;
		}
		return Result;
	}

	// Reactive parent node가 현재 Running child를 선점할 때 호출한다.
	// 일반 종료(OnExit)와 달리 기존 활성 subtree 전체에 cleanup을 전파해서 정리할 여지를 줌
	void Abort(FBTContext& Context)
	{
		if (!bActive)
		{
			return;
		}
		OnAbort(Context);
		bActive = false;
	}

	// 이번 프레임에 평가됐고 Running 이면 root→leaf 실행 체인의 일부(active path)
	bool IsOnActivePath(uint64 CurrentFrame) const
	{
		return Debug.LastEvaluatedFrame == CurrentFrame && Debug.LastResult == EBTResult::Running;
	}
	bool WasEvaluated(uint64 CurrentFrame) const { return Debug.LastEvaluatedFrame == CurrentFrame; }
	const FBTDebugInfo& GetDebugInfo() const { return Debug; }

	virtual const char* GetNodeTypeName() const = 0;
	FName GetDebugLabel() const { return DebugLabel; }
	void  SetDebugLabel(FName In) { DebugLabel = In; }
	virtual TArray<const FBehaviorNode*> GetChildrenForDebug() const { return {}; }

protected:
	virtual void OnEnter(FBTContext& Context) { (void)Context; }
	virtual void OnAbort(FBTContext& Context) { (void)Context; }
	virtual void OnExit(FBTContext& Context) { (void)Context; }
	virtual EBTResult OnBehave(FBTContext& Context) = 0;

	FBTDebugInfo Debug;
	FName        DebugLabel;
	bool         bActive = false;
};

class FBehaviorTree
{
public:
	explicit FBehaviorTree(std::unique_ptr<FBehaviorNode> InRootNode) : Root(std::move(InRootNode)) { }
	EBTResult Behave(FBTContext& Context) { return Root ? Root->Execute(Context) : EBTResult::Fail; }
	void Abort(FBTContext& Context) { if (Root) Root->Abort(Context); }

	const FBehaviorNode* GetRootForDebug() const { return Root.get(); }
private:
	std::unique_ptr<FBehaviorNode> Root;
};

// ===== Leaf Nodes =====

class FBehaviorTask : public FBehaviorNode
{
public:
	using FTaskFn = std::function<EBTResult(FBTContext&)>;
	using FLifecycleFn = std::function<void(FBTContext&)>;

	FBehaviorTask(FName InName, FTaskFn InFunc, FLifecycleFn InOnEnter = {}, FLifecycleFn InOnAbort = {}, FLifecycleFn InOnExit = {})
		: Func(std::move(InFunc)), EnterFunc(std::move(InOnEnter)), AbortFunc(std::move(InOnAbort)), ExitFunc(std::move(InOnExit)) { DebugLabel = InName; }

	const char* GetNodeTypeName() const override { return "Task"; }
protected:
	void OnEnter(FBTContext& Context) override { if (EnterFunc) EnterFunc(Context); }
	void OnAbort(FBTContext& Context) override { if (AbortFunc) AbortFunc(Context); }
	void OnExit(FBTContext& Context) override { if (ExitFunc) ExitFunc(Context); }

	EBTResult OnBehave(FBTContext& Context) override
	{
		return Func ? Func(Context) : EBTResult::Fail;
	}
private:
	FTaskFn      Func;
	FLifecycleFn EnterFunc;
	FLifecycleFn AbortFunc;
	FLifecycleFn ExitFunc;
};

class FConditional : public FBehaviorNode
{
public:
	FConditional(FName InName, std::function<bool(FBTContext&)> InFunc)
		: Func(std::move(InFunc)) { DebugLabel = InName; }

	const char* GetNodeTypeName() const override { return "Condition"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		return Func && Func(Context) ? EBTResult::Success : EBTResult::Fail;
	}
private:
	std::function<bool(FBTContext&)> Func;
};

// ===== Composite Nodes =====

class FCompositeNode : public FBehaviorNode
{
public:
	explicit FCompositeNode(TArray<std::unique_ptr<FBehaviorNode>> InChildren)
		: Children(std::move(InChildren)) { }

	TArray<const FBehaviorNode*> GetChildrenForDebug() const override
	{
		TArray<const FBehaviorNode*> Out;
		Out.reserve(Children.size());
		for (const std::unique_ptr<FBehaviorNode>& Child : Children)
			Out.push_back(Child.get());
		return Out;
	}
protected:
	void OnAbort(FBTContext& Context) override
	{
		for (const std::unique_ptr<FBehaviorNode>& Child : Children)
		{
			Child->Abort(Context);
		}
	}

	TArray<std::unique_ptr<FBehaviorNode>> Children;
};

// Reactive composites reevaluate children from the first child each tick.  Only
// the child that was Running on the previous tick needs an explicit abort when
// a higher-priority child takes over.
class FReactiveCompositeNode : public FCompositeNode
{
protected:
	using FCompositeNode::FCompositeNode;

	void TransitionActiveChild(FBTContext& Context, int NewActiveChild)
	{
		if (ActiveChildIndex >= 0 && ActiveChildIndex != NewActiveChild)
		{
			Children[ActiveChildIndex]->Abort(Context);
		}
		ActiveChildIndex = NewActiveChild;
	}

	void OnAbort(FBTContext& Context) override
	{
		FCompositeNode::OnAbort(Context);
		ActiveChildIndex = -1;
	}

private:
	int ActiveChildIndex = -1;
};

// NOTE: Sequence는 진행 중이던 자식을 기억하지 않음 (=> Reactive node)
class FSequence : public FReactiveCompositeNode
{
public:
	explicit FSequence(TArray<std::unique_ptr<FBehaviorNode>> InChildren)
		: FReactiveCompositeNode(std::move(InChildren)) { DebugLabel = FName("Sequence"); }

	const char* GetNodeTypeName() const override { return "Sequence"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		int NewActiveChild = -1;
		for (int Index = 0; Index < static_cast<int>(Children.size()); ++Index)
		{
			EBTResult ChildResult = Children[Index]->Execute(Context);
			if (ChildResult != EBTResult::Success)
			{
				if (ChildResult == EBTResult::Running)
				{
					NewActiveChild = Index;
				}
				TransitionActiveChild(Context, NewActiveChild);
				return ChildResult;
			}
		}
		TransitionActiveChild(Context, -1);
		return EBTResult::Success;
	}

};

// 성공한 child의 위치를 보존하는 stateful sequence. Reactive selector가 이 subtree를
// 선점하면 현재 Running child만 Abort되고, 다음 진입은 첫 child부터 시작한다.
class FStatefulSequence : public FCompositeNode
{
public:
	explicit FStatefulSequence(TArray<std::unique_ptr<FBehaviorNode>> InChildren)
		: FCompositeNode(std::move(InChildren)) { DebugLabel = FName("StatefulSequence"); }

	const char* GetNodeTypeName() const override { return "StatefulSequence"; }

protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		while (CurrentChildIndex < static_cast<int>(Children.size()))
		{
			EBTResult ChildResult = Children[CurrentChildIndex]->Execute(Context);
			if (ChildResult == EBTResult::Running)
			{
				return EBTResult::Running;
			}
			if (ChildResult == EBTResult::Fail)
			{
				CurrentChildIndex = 0;
				return EBTResult::Fail;
			}
			++CurrentChildIndex;
		}

		CurrentChildIndex = 0;
		return EBTResult::Success;
	}

	void OnAbort(FBTContext& Context) override
	{
		FCompositeNode::OnAbort(Context);
		CurrentChildIndex = 0;
	}

	void OnExit(FBTContext& Context) override
	{
		(void)Context;
		CurrentChildIndex = 0;
	}

private:
	int CurrentChildIndex = 0;
};

// NOTE: Selector도 진행 중이던 자식을 기억하지 않음 (=> Reactive node)
class FSelector : public FReactiveCompositeNode
{
public:
	explicit FSelector(TArray<std::unique_ptr<FBehaviorNode>> InChildren)
		: FReactiveCompositeNode(std::move(InChildren)) { DebugLabel = FName("Selector"); }

	const char* GetNodeTypeName() const override { return "Selector"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		int NewActiveChild = -1;
		for (int Index = 0; Index < static_cast<int>(Children.size()); ++Index)
		{
			EBTResult ChildResult = Children[Index]->Execute(Context);
			if (ChildResult != EBTResult::Fail)
			{
				if (ChildResult == EBTResult::Running)
				{
					NewActiveChild = Index;
				}
				TransitionActiveChild(Context, NewActiveChild);
				return ChildResult;
			}
		}
		TransitionActiveChild(Context, -1);
		return EBTResult::Fail;
	}

};

// ===== Decorator Nodes =====
class FDecoratorNode : public FBehaviorNode
{
public:
	explicit FDecoratorNode(std::unique_ptr<FBehaviorNode> InChild) : Child(std::move(InChild)) { }

	TArray<const FBehaviorNode*> GetChildrenForDebug() const override
	{
		return { Child.get() };
	}
protected:
	void OnAbort(FBTContext& Context) override
	{
		Child->Abort(Context);
	}

	std::unique_ptr<FBehaviorNode> Child;
};

// 자식이 Fail 이면 Success 로 바꾸고, 그 외(Success/Running)는 그대로 전달
class FForceSuccess : public FDecoratorNode
{
public:
	explicit FForceSuccess(std::unique_ptr<FBehaviorNode> InChild) 
		: FDecoratorNode(std::move(InChild)) { DebugLabel = FName("ForceSuccess"); }

	const char* GetNodeTypeName() const override { return "ForceSuccess"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		EBTResult Result = Child->Execute(Context);
		if (Result == EBTResult::Fail)
			return EBTResult::Success;
		else
			return Result;
	}
};

class FInvert : public FDecoratorNode
{
public:
	explicit FInvert(std::unique_ptr<FBehaviorNode> InChild) 
		: FDecoratorNode(std::move(InChild)) { DebugLabel = FName("Invert"); }

	const char* GetNodeTypeName() const override { return "Invert"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		EBTResult Result = Child->Execute(Context);
		if (Result == EBTResult::Fail)
			return EBTResult::Success;
		else if (Result == EBTResult::Success)
			return EBTResult::Fail;
		else
			return Result;
	}
};
