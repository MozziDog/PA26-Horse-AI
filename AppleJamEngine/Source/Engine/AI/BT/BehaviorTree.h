#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/FName.h"
#include <memory>

class AActor;

enum class EBTResult
{
	Running, Success, Fail
};

// 트리 실행 중 노드 사이로 전달되는 공유 컨텍스트
// NOTE: Owner 등을 std::function 캡쳐 내부에서 잡아두면 댕글링 포인터 생길 수 있어서 
// 매 프레임 필요한 정보를 주입해주는 방식을 채택
struct FBTContext
{
	AActor* Owner       = nullptr;
	float   DeltaTime   = 0.0f;
	uint64  FrameNumber = 0;		// 현재 프레임 번호. 시각화가 "이번 tick 에 평가된 노드"를 판별하는 데 사용
	// NOTE: 블랙보드 등 필요한 필드는 여기에 확장
};

// 노드별 디버그 스냅샷. 런타임 로직엔 영향 없음. 시각화(뷰어)가 read-only 로 읽는다.
struct FBTDebugInfo
{
	EBTResult LastResult         = EBTResult::Fail;
	uint64    LastEvaluatedFrame = 0;     // 마지막으로 Execute 된 tick
	float     ActiveDuration     = 0.0f;  // 연속 Running 유지 시간(전이 시 0)
};

class BehaviorNode
{
public:
	virtual ~BehaviorNode() = default;

	// 노드는 자식을 unique_ptr 로 소유하므로 복사 금지
	BehaviorNode()                               = default;
	BehaviorNode(const BehaviorNode&)            = delete;
	BehaviorNode& operator=(const BehaviorNode&) = delete;

	EBTResult Execute(FBTContext& Context)
	{
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
		return Result;
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
	virtual TArray<const BehaviorNode*> GetChildrenForDebug() const { return {}; }

protected:
	virtual EBTResult OnBehave(FBTContext& Context) = 0;

	FBTDebugInfo Debug;
	FName        DebugLabel;
};

class BehaviorTree
{
public:
	explicit BehaviorTree(std::unique_ptr<BehaviorNode> InRootNode) : Root(std::move(InRootNode)) { }
	EBTResult Behave(FBTContext& Context) { return Root->Execute(Context); }

	const BehaviorNode* GetRootForDebug() const { return Root.get(); }
private:
	std::unique_ptr<BehaviorNode> Root;
};

// ===== Leaf Nodes =====

class BehaviorTask : public BehaviorNode
{
public:
	BehaviorTask(FName InName, std::function<EBTResult(FBTContext&)> InFunc)
		: Func(std::move(InFunc)) { DebugLabel = InName; }

	const char* GetNodeTypeName() const override { return "Task"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		return Func(Context);
	}
private:
	std::function<EBTResult(FBTContext&)> Func;
};

class Conditional : public BehaviorNode
{
public:
	Conditional(FName InName, std::function<bool(FBTContext&)> InFunc)
		: Func(std::move(InFunc)) { DebugLabel = InName; }

	const char* GetNodeTypeName() const override { return "Condition"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		return Func(Context) ? EBTResult::Success : EBTResult::Fail;
	}
private:
	std::function<bool(FBTContext&)> Func;
};

// ===== Composite Nodes =====

class CompositeNode : public BehaviorNode
{
public:
	explicit CompositeNode(TArray<std::unique_ptr<BehaviorNode>> InChildren)
		: Children(std::move(InChildren)) { }

	TArray<const BehaviorNode*> GetChildrenForDebug() const override
	{
		TArray<const BehaviorNode*> Out;
		Out.reserve(Children.size());
		for (const std::unique_ptr<BehaviorNode>& Child : Children)
			Out.push_back(Child.get());
		return Out;
	}
protected:
	TArray<std::unique_ptr<BehaviorNode>> Children;
};

// NOTE: Sequence는 진행 중이던 자식을 기억하지 않음 (=> Reactive node)
class Sequence : public CompositeNode
{
public:
	explicit Sequence(TArray<std::unique_ptr<BehaviorNode>> InChildren)
		: CompositeNode(std::move(InChildren)) { DebugLabel = FName("Sequence"); }

	const char* GetNodeTypeName() const override { return "Sequence"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		for (const std::unique_ptr<BehaviorNode>& Child : Children)
		{
			EBTResult ChildResult = Child->Execute(Context);
			if (ChildResult != EBTResult::Success)
				return ChildResult;
		}
		return EBTResult::Success;
	}
};

// NOTE: Selector도 진행 중이던 자식을 기억하지 않음 (=> Reactive node)
class Selector : public CompositeNode
{
public:
	explicit Selector(TArray<std::unique_ptr<BehaviorNode>> InChildren)
		: CompositeNode(std::move(InChildren)) { DebugLabel = FName("Selector"); }

	const char* GetNodeTypeName() const override { return "Selector"; }
protected:
	EBTResult OnBehave(FBTContext& Context) override
	{
		for (const std::unique_ptr<BehaviorNode>& Child : Children)
		{
			EBTResult ChildResult = Child->Execute(Context);
			if (ChildResult != EBTResult::Fail)
				return ChildResult;
		}
		return EBTResult::Fail;
	}
};

// ===== Decorator Nodes =====
class DecoratorNode : public BehaviorNode
{
public:
	explicit DecoratorNode(std::unique_ptr<BehaviorNode> InChild) : Child(std::move(InChild)) { }

	TArray<const BehaviorNode*> GetChildrenForDebug() const override
	{
		return { Child.get() };
	}
protected:
	std::unique_ptr<BehaviorNode> Child;
};

// 자식이 Fail 이면 Success 로 바꾸고, 그 외(Success/Running)는 그대로 전달
class ForceSuccess : public DecoratorNode
{
public:
	explicit ForceSuccess(std::unique_ptr<BehaviorNode> InChild) 
		: DecoratorNode(std::move(InChild)) { DebugLabel = FName("ForceSuccess"); }

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

class Invert : public DecoratorNode
{
public:
	explicit Invert(std::unique_ptr<BehaviorNode> InChild) 
		: DecoratorNode(std::move(InChild)) { DebugLabel = FName("Invert"); }

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
