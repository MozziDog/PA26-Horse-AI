#pragma once

#include "AI/BT/BehaviorTree.h"   // EBTResult, FBTContext
#include <functional>

// lua script에서는 BT의 구성만 담당, 각 Task들의 구현은 여기서 보관
// (hot - reload / 씬 전환 시 Lua 클로저에서 댕글링 포인터 발생 우려)
class FBTBehaviorRegistry
{
public:
	using FTaskFn      = std::function<EBTResult(FBTContext&)>;
	using FConditionFn = std::function<bool(FBTContext&)>;
	using FTaskLifecycleFn = std::function<void(FBTContext&)>;

	struct FTaskDefinition
	{
		FTaskFn          Tick;
		FTaskLifecycleFn OnEnter;
		FTaskLifecycleFn OnAbort;
		FTaskLifecycleFn OnExit;

		bool IsValid() const { return static_cast<bool>(Tick); }
	};

	// 같은 이름으로 다시 등록하면 덮어쓴다(재등록 idempotent).
	static void RegisterTask(FName Name, FTaskFn Fn);
	static void RegisterTask(FName Name, FTaskDefinition Definition);
	static void RegisterCondition(FName Name, FConditionFn Fn);

	// 없으면 nullptr.
	static const FTaskDefinition* FindTask(FName Name);
	static const FConditionFn* FindCondition(FName Name);

private:
	static TMap<FName, FTaskDefinition>& Tasks();
	static TMap<FName, FConditionFn>& Conditions();
};
