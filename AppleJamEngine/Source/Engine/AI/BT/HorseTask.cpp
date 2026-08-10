#include "pch.h"

#include "AI/BT/BTBehaviorRegistry.h"
#include "AI/Blackboard.h"
#include "Component/AI/BlackboardComponent.h"
#include "Game/Horse/HorseCallNavigationComponent.h"
#include "Game/Horse/HorseCharacter.h"
#include "Game/Horse/HorseConstants.h"
#include "Game/Horse/HorseLocomotionComponent.h" // EHorseGait
#include "Game/Horse/HorseUserGuidanceComponent.h"
#include "GameFramework/AActor.h"
#include "Runtime/EngineInitHooks.h"

// HorseBT.lua에서 참조하는 BT task들
// FEngineInitHooks 로 엔진 부팅 시 1회 자동 등록 → BeginPlay 시점에 이미 등록되어있음 보장
namespace
{
	void SetDesiredGait(FBTContext& Context, EHorseGait Gait)
	{
		if (Context.Blackboard)
		{
			Context.Blackboard->SetInt(HorseBBKeys::DesiredGait, static_cast<int>(Gait));
		}
	}

	void PublishControlPolicy(FBTContext& Context, bool bRoadAssist, bool bIgnoreContextAvoidance, bool bAutoJump, bool bStrafe)
	{
		if (!Context.Blackboard)
		{
			return;
		}
		Context.Blackboard->SetBool(HorseBBKeys::ControlEnableRoadAssist, bRoadAssist);
		Context.Blackboard->SetBool(HorseBBKeys::ControlIgnoreContextAvoidance, bIgnoreContextAvoidance);
		Context.Blackboard->SetBool(HorseBBKeys::ControlEnableAutoJump, bAutoJump);
		Context.Blackboard->SetBool(HorseBBKeys::ControlEnableStrafe, bStrafe);
	}

	void PublishHorseCallControlPolicy(FBTContext& Context)
	{
		PublishControlPolicy(Context, false, false, false, false);
	}

	void SetCallRequested(FBTContext& Context, bool bRequested)
	{
		if (Context.Blackboard)
		{
			Context.Blackboard->SetBool(HorseBBKeys::CallRequested, bRequested);
		}
	}

	AHorseCharacter* GetHorse(FBTContext& Context)
	{
		return Cast<AHorseCharacter>(Context.Owner);
	}

	UHorseCallNavigationComponent* GetNavigation(FBTContext& Context)
	{
		if (AHorseCharacter* Horse = GetHorse(Context))
		{
			return Horse->GetComponentByClass<UHorseCallNavigationComponent>();
		}
		return nullptr;
	}

	UHorseUserGuidanceComponent* GetUserGuidance(FBTContext& Context)
	{
		if (AHorseCharacter* Horse = GetHorse(Context))
		{
			return Horse->GetComponentByClass<UHorseUserGuidanceComponent>();
		}
		return nullptr;
	}

	EHorseCallNavigationStatus GetCallStatus(FBTContext& Context)
	{
		if (UHorseCallNavigationComponent* Navigation = GetNavigation(Context))
		{
			return Navigation->GetStatus();
		}

		int StatusValue = static_cast<int>(EHorseCallNavigationStatus::Idle);
		if (Context.Blackboard)
		{
			Context.Blackboard->TryGetInt(HorseBBKeys::CallStatus, StatusValue);
		}
		return static_cast<EHorseCallNavigationStatus>(StatusValue);
	}

	void ClearCallProfile(FBTContext& Context)
	{
		if (UHorseCallNavigationComponent* Navigation = GetNavigation(Context))
		{
			Navigation->ClearGuidance();
		}
		PublishHorseCallControlPolicy(Context);
	}

	void AbortCall(FBTContext& Context)
	{
		if (UHorseCallNavigationComponent* Navigation = GetNavigation(Context))
		{
			Navigation->CancelCall();
		}
		SetDesiredGait(Context, EHorseGait::Stop);
		ClearCallProfile(Context);
	}

	FBTBehaviorRegistry::FTaskDefinition MakeCallTask(
		FBTBehaviorRegistry::FTaskFn Tick,
		FBTBehaviorRegistry::FTaskLifecycleFn OnEnter)
	{
		FBTBehaviorRegistry::FTaskDefinition Definition;
		Definition.Tick = std::move(Tick);
		Definition.OnEnter = std::move(OnEnter);
		Definition.OnAbort = AbortCall;
		Definition.OnExit = ClearCallProfile;
		return Definition;
	}

	void RegisterHorseTasks()
	{
		FBTBehaviorRegistry::RegisterCondition(FName("Mounted"), [](FBTContext& Context)
			{
				if (const AHorseCharacter* Horse = Cast<AHorseCharacter>(Context.Owner))
				{
					return Horse->IsRiderMounted();
				}
				return false;
			});
		FBTBehaviorRegistry::RegisterCondition(FName("CallRequested"), [](FBTContext& Context)
			{
				bool bRequested = false;
				return Context.Blackboard && Context.Blackboard->TryGetBool(HorseBBKeys::CallRequested, bRequested) && bRequested;
			});

		FBTBehaviorRegistry::FTaskDefinition RiderControl;
		RiderControl.OnEnter = [](FBTContext& Context)
			{
				if (UHorseUserGuidanceComponent* Guidance = GetUserGuidance(Context))
				{
					Guidance->SetGuidanceActive(true);
				}
			};
		RiderControl.Tick = [](FBTContext& Context)
			{
				const AHorseCharacter* Horse = Cast<AHorseCharacter>(Context.Owner);
				if (!Context.Blackboard || !Horse || !Horse->IsRiderMounted())
				{
					return EBTResult::Fail;
				}
				PublishControlPolicy(Context, true, false, true, true);
				SetDesiredGait(Context, EHorseGait::None);
				return EBTResult::Running;
			};
		RiderControl.OnAbort = [](FBTContext& Context)
			{
				if (UHorseUserGuidanceComponent* Guidance = GetUserGuidance(Context))
				{
					Guidance->SetGuidanceActive(false);
				}
				PublishHorseCallControlPolicy(Context);
			};
		RiderControl.OnExit = RiderControl.OnAbort;
		FBTBehaviorRegistry::RegisterTask(FName("RiderControlGuidance"), std::move(RiderControl));

		FBTBehaviorRegistry::RegisterTask(FName("PlanCallPath"), MakeCallTask(
			[](FBTContext& Context)
			{
				PublishControlPolicy(Context, false, false, false, false);
				SetDesiredGait(Context, EHorseGait::Stop);
				UHorseCallNavigationComponent* Navigation = GetNavigation(Context);
				if (!Navigation)
				{
					return EBTResult::Fail;
				}
				const EHorseCallNavigationStatus Status = GetCallStatus(Context);
				if (Navigation->IsPlanReady())
				{
					return EBTResult::Success;
				}
				if (IsTerminalCallNavigationStatus(Status))
				{
					SetCallRequested(Context, false);
					return EBTResult::Fail;
				}
				return EBTResult::Running;
			},
			[](FBTContext& Context)
			{
				if (UHorseCallNavigationComponent* Navigation = GetNavigation(Context))
				{
					Navigation->ClearGuidance();
					Navigation->BeginPlan();
				}
			}));

		FBTBehaviorRegistry::RegisterTask(FName("AlignToPathStart"), MakeCallTask(
			[](FBTContext& Context)
			{
				PublishHorseCallControlPolicy(Context);
				SetDesiredGait(Context, EHorseGait::Stop);
				const EHorseCallNavigationStatus Status = GetCallStatus(Context);
				if (Status == EHorseCallNavigationStatus::Following)
				{
					return EBTResult::Success;
				}
				if (IsTerminalCallNavigationStatus(Status))
				{
					SetCallRequested(Context, false);
					return IsCompletedCallNavigationStatus(Status) ? EBTResult::Success : EBTResult::Fail;
				}
				return Status == EHorseCallNavigationStatus::Aligning ? EBTResult::Running : EBTResult::Fail;
			},
			[](FBTContext& Context)
			{
				if (UHorseCallNavigationComponent* Navigation = GetNavigation(Context))
				{
					Navigation->BeginAlignToPathStart();
				}
			}));

		FBTBehaviorRegistry::RegisterTask(FName("FollowCallPath"), MakeCallTask(
			[](FBTContext& Context)
			{
				PublishHorseCallControlPolicy(Context);
				UHorseCallNavigationComponent* Navigation = GetNavigation(Context);
				if (!Navigation)
				{
					return EBTResult::Fail;
				}
				const EHorseCallNavigationStatus Status = GetCallStatus(Context);
				if (Status == EHorseCallNavigationStatus::Following)
				{
					SetDesiredGait(Context, Navigation->GetRecommendedGait());
					return EBTResult::Running;
				}
				if (IsTerminalCallNavigationStatus(Status))
				{
					SetDesiredGait(Context, EHorseGait::Stop);
					SetCallRequested(Context, false);
					return IsCompletedCallNavigationStatus(Status) ? EBTResult::Success : EBTResult::Fail;
				}
				return EBTResult::Fail;
			},
			[](FBTContext& Context)
			{
				if (UHorseCallNavigationComponent* Navigation = GetNavigation(Context))
				{
					Navigation->BeginFollowPath();
				}
			}));

		FBTBehaviorRegistry::FTaskDefinition Idle;
		Idle.OnEnter = [](FBTContext& Context)
		{
			if (UHorseUserGuidanceComponent* Guidance = GetUserGuidance(Context))
			{
				Guidance->SetGuidanceActive(false);
			}
			if (UHorseCallNavigationComponent* Navigation = GetNavigation(Context))
			{
				Navigation->ClearGuidance();
			}
		};
		Idle.Tick = [](FBTContext& Context)
		{
			PublishHorseCallControlPolicy(Context);
			SetDesiredGait(Context, EHorseGait::Stop);
			return EBTResult::Running;
		};
		FBTBehaviorRegistry::RegisterTask(FName("Idle"), std::move(Idle));
	}

	struct FHorseTasksAutoReg
	{
		FHorseTasksAutoReg() { FEngineInitHooks::Register(&RegisterHorseTasks); }
	};
	static FHorseTasksAutoReg gHorseTasksAutoReg;
}
