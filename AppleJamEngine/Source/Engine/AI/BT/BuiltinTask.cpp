#include "pch.h"

#include "AI/BT/BTBehaviorRegistry.h"
#include "Runtime/EngineInitHooks.h"

// 엔진 기본 제공 '범용' BT behavior(도메인 무관)
// FEngineInitHooks 로 엔진 부팅 시 1회 자동 등록 → 어떤 BTAgent 든 BeginPlay 이전에 사용 가능.
// (전용 behavior 는 ***Task.cpp 로 분리 — 예: HorseTask.cpp)
namespace
{
	void RegisterBuiltinTasks()
	{
		// Idle: 아무것도 하지 않고 계속 Running
		FBTBehaviorRegistry::RegisterTask(
			FName("Idle"), 
			[](FBTContext&) { return EBTResult::Running; }
		);
	}

	// 자기-등록 — 엔진 부팅 시 1회 자동 호출(RunAll).
	struct FBuiltinTasksAutoReg
	{
		FBuiltinTasksAutoReg() { FEngineInitHooks::Register(&RegisterBuiltinTasks); }
	};
	static FBuiltinTasksAutoReg gBuiltinTasksAutoReg;
}
