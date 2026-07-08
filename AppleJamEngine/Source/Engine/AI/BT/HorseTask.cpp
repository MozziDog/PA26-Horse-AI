#include "pch.h"

#include "AI/BT/BTBehaviorRegistry.h"
#include "AI/Blackboard.h"
#include "Component/Movement/HorseLocomotionComponent.h"   // EHorseGait
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

		// 태스크 — BT 는 이동을 직접 만지지 않고 Blackboard 의 "DesiredGait"(원하는 보법)만 쓴다.
		// Locomotion이 이를 읽어 쿨타임·envelope를 고려해 실제 gait에 반영하고 Movement로 라우팅한다.
		auto SetDesiredGait = [](FBTContext& Ctx, EHorseGait Gait)
			{
				if (Ctx.Blackboard)
				{
					Ctx.Blackboard->SetInt(FName("DesiredGait"), static_cast<int>(Gait));
				}
			};

		// Run: 전력 질주 요청(Flee 등). Chew/Idle: 정지 요청.
		FBTBehaviorRegistry::RegisterTask(FName("Run"), [SetDesiredGait](FBTContext& Ctx)
			{
				SetDesiredGait(Ctx, EHorseGait::Gallop);
				return EBTResult::Running;
			});
		FBTBehaviorRegistry::RegisterTask(FName("Chew"), [SetDesiredGait](FBTContext& Ctx) // 테스트용 임시 Task
			{
				SetDesiredGait(Ctx, EHorseGait::Stop);
				return EBTResult::Running;
			});
		FBTBehaviorRegistry::RegisterTask(FName("Idle"), [SetDesiredGait](FBTContext& Ctx)
			{
				SetDesiredGait(Ctx, EHorseGait::Stop);
				return EBTResult::Running;
			});
	}

	// 자기-등록 — 엔진 부팅 시 1회 자동 호출(RunAll).
	struct FHorseTasksAutoReg
	{
		FHorseTasksAutoReg() { FEngineInitHooks::Register(&RegisterHorseTasks); }
	};
	static FHorseTasksAutoReg gHorseTasksAutoReg;
}
