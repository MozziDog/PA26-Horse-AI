#include "pch.h"
#include "BTBehaviorRegistry.h"

// 함수 로컬 static — 정적 초기화 순서 문제(SIOF) 회피.
TMap<FName, FBTBehaviorRegistry::FTaskFn>& FBTBehaviorRegistry::Tasks()
{
	static TMap<FName, FTaskFn> Instance;
	return Instance;
}

TMap<FName, FBTBehaviorRegistry::FConditionFn>& FBTBehaviorRegistry::Conditions()
{
	static TMap<FName, FConditionFn> Instance;
	return Instance;
}

void FBTBehaviorRegistry::RegisterTask(FName Name, FTaskFn Fn)
{
	Tasks()[Name] = std::move(Fn);
}

void FBTBehaviorRegistry::RegisterCondition(FName Name, FConditionFn Fn)
{
	Conditions()[Name] = std::move(Fn);
}

const FBTBehaviorRegistry::FTaskFn* FBTBehaviorRegistry::FindTask(FName Name)
{
	auto It = Tasks().find(Name);
	return It != Tasks().end() ? &It->second : nullptr;
}

const FBTBehaviorRegistry::FConditionFn* FBTBehaviorRegistry::FindCondition(FName Name)
{
	auto It = Conditions().find(Name);
	return It != Conditions().end() ? &It->second : nullptr;
}
