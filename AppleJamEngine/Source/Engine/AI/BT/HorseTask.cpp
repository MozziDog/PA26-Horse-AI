#include "pch.h"

#include "AI/BT/BTBehaviorRegistry.h"
#include "AI/Blackboard.h"
#include "GameFramework/AActor.h"
#include "Runtime/EngineInitHooks.h"

// 말 전용 BT behavior(task/condition) 정의. Lua BT(BT/HorseTest.lua)가 이 이름들을 참조.
// FEngineInitHooks 로 엔진 부팅 시 1회 자동 등록 → 등록 타이밍/순서 무관.
namespace
{
	void RegisterHorseTasks()
	{
		// NOTE: ThreatNear와 Hungry 등의 behavior 등은 테스트용 임시 구현. 추후 삭제/대체할 것.
		FBTBehaviorRegistry::RegisterCondition(FName("ThreatNear"), [](FBTContext& Ctx)
			{
				bool Value = false;
				return Ctx.Blackboard && Ctx.Blackboard->TryGetBool(FName("ThreatNear"), Value) && Value;
			});
		FBTBehaviorRegistry::RegisterCondition(FName("Hungry"), [](FBTContext& Ctx)
			{
				bool Value = false;
				return Ctx.Blackboard && Ctx.Blackboard->TryGetBool(FName("Hungry"), Value) && Value;
			});

		// 태스크
		FBTBehaviorRegistry::RegisterTask(FName("Run"), [](FBTContext& Ctx)
			{
				// TODO: 실제 이동 로직으로 대체(현재는 forward 로 3m/s 전진하는 placeholder).
				if (Ctx.Owner)
				{
					constexpr float Speed = 3.0f;
					Ctx.Owner->AddActorWorldOffset(Ctx.Owner->GetActorForward() * (Speed * Ctx.DeltaTime));
				}
				return EBTResult::Running;
			});
		FBTBehaviorRegistry::RegisterTask(FName("Chew"), [](FBTContext&) { return EBTResult::Running; });
	}

	// 자기-등록 — 엔진 부팅 시 1회 자동 호출(RunAll).
	struct FHorseTasksAutoReg
	{
		FHorseTasksAutoReg() { FEngineInitHooks::Register(&RegisterHorseTasks); }
	};
	static FHorseTasksAutoReg gHorseTasksAutoReg;
}
