#include "pch.h"

#include "AI/BT/BTBehaviorRegistry.h"
#include "AI/Blackboard.h"
#include "Component/Movement/PawnMovementComponent.h"
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
		// Run: 현재 forward 방향으로 전진 입력만 제공. 실제 가감속·지면 처리는 MovementComponent 담당.
		// 입력 방향이 현재 forward 와 같아 조향은 중립 → 직진. Movement 없으면 아무것도 안 함(no-op).
		FBTBehaviorRegistry::RegisterTask(FName("Run"), [](FBTContext& Ctx)
			{
				if (Ctx.Movement && Ctx.Owner)
				{
					Ctx.Movement->AddInputVector(Ctx.Owner->GetActorForward(), 1.0f);
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
