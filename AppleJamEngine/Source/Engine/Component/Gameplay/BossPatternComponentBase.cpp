#include "BossPatternComponentBase.h"

#include "Animation/AnimInstance.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Character.h"
#include "Math/Rotator.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float BossPatternPi = 3.1415926535f;

	float ClampCooldown(float Value)
	{
		return (std::max)(0.0f, Value);
	}

	const char* LexToString(EBossPatternStep Step)
	{
		switch (Step)
		{
		case EBossPatternStep::Windup:
			return "Windup";
		case EBossPatternStep::Task1:
			return "Task1";
		case EBossPatternStep::Task2:
			return "Task2";
		case EBossPatternStep::Task3:
			return "Task3";
		case EBossPatternStep::Recovery:
			return "Recovery";
		case EBossPatternStep::Finished:
			return "Finished";
		default:
			return "None";
		}
	}

	bool IsRootMotionDrivingRotation(const AActor* Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(Actor);
		if (!Character)
		{
			return false;
		}

		if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (const UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				if (AnimInstance->HasPendingRootMotion())
				{
					return true;
				}
			}
		}

		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		return Movement && Movement->HasYawDrivenByRootMotion();
	}
}

UBossPatternComponentBase::UBossPatternComponentBase()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bTickEnabled = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	CooldownRemaining = InitialCooldown;
}

bool UBossPatternComponentBase::GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const
{
	if (!bEnabled)
	{
		if (OutRejectReason) *OutRejectReason = PatternName + ": disabled";
		return false;
	}

	if (Weight <= 0.0f)
	{
		if (OutRejectReason) *OutRejectReason = PatternName + ": zero weight";
		return false;
	}

	if (CooldownRemaining > 0.0f)
	{
		if (OutRejectReason) *OutRejectReason = PatternName + ": cooldown";
		return false;
	}

	if (Context.TargetActor)
	{
		if (Context.DistanceToTarget < MinTargetDistance)
		{
			if (OutRejectReason) *OutRejectReason = PatternName + ": target too close";
			return false;
		}

		if (Context.DistanceToTarget > MaxTargetDistance)
		{
			if (OutRejectReason) *OutRejectReason = PatternName + ": target too far";
			return false;
		}
	}

	if (Context.BossPhase >= 0 && Context.BossPhase < 31)
	{
		const int32 PhaseBit = 1 << Context.BossPhase;
		if ((AllowedPhaseMask & PhaseBit) == 0)
		{
			if (OutRejectReason) *OutRejectReason = PatternName + ": phase blocked";
			return false;
		}
	}

	return true;
}

void UBossPatternComponentBase::StartPattern(const FBossPatternContext& Context)
{
	bRunning = true;
	bFinished = false;
	PatternElapsed = 0.0f;
	StepElapsed = 0.0f;
	CurrentStep = EBossPatternStep::None;
	OnPatternStart(Context);
	EnterStep(EBossPatternStep::Windup, Context);
}

void UBossPatternComponentBase::TickPattern(float DeltaTime, const FBossPatternContext& Context)
{
	if (!bRunning || bFinished)
	{
		return;
	}

	PatternElapsed += DeltaTime;
	StepElapsed += DeltaTime;

	if (bFaceTargetDuringPattern && !IsRootMotionDrivingRotation(Context.BossActor))
	{
		FaceTarget(Context);
	}

	TickCurrentStep(DeltaTime, Context);
	if (bFinished)
	{
		return;
	}

	if (ShouldAdvanceStep(Context))
	{
		EnterStep(GetNextStep(CurrentStep), Context);
	}
}

void UBossPatternComponentBase::CancelPattern(const FBossPatternContext& Context)
{
	if (!bRunning && bFinished)
	{
		return;
	}

	FinishPattern(Context);
}

bool UBossPatternComponentBase::IsPatternFinished() const
{
	return bFinished;
}

float UBossPatternComponentBase::GetEffectiveWeight(const FBossPatternContext& Context) const
{
	float PhaseWeight = PhaseWeight0;
	if (Context.BossPhase == 1)
	{
		PhaseWeight = PhaseWeight1;
	}
	else if (Context.BossPhase >= 2)
	{
		PhaseWeight = PhaseWeight2;
	}

	return (std::max)(0.0f, Weight) * (std::max)(0.0f, PhaseWeight);
}

FString UBossPatternComponentBase::GetRuntimeDebugText() const
{
	return "";
}

void UBossPatternComponentBase::NotifySelected()
{
	++SelectionCount;
	CooldownRemaining = ClampCooldown(Cooldown);
}

void UBossPatternComponentBase::TickCooldown(float DeltaTime)
{
	if (CooldownRemaining <= 0.0f)
	{
		return;
	}

	CooldownRemaining = ClampCooldown(CooldownRemaining - DeltaTime);
}

void UBossPatternComponentBase::OnPatternStart(const FBossPatternContext& Context)
{
	(void)Context;
}

void UBossPatternComponentBase::OnPatternEnd(const FBossPatternContext& Context)
{
	(void)Context;
}

void UBossPatternComponentBase::OnStepEnter(EBossPatternStep Step, const FBossPatternContext& Context)
{
	(void)Step;
	(void)Context;
}

void UBossPatternComponentBase::TickCurrentStep(float DeltaTime, const FBossPatternContext& Context)
{
	(void)DeltaTime;
	(void)Context;
}

bool UBossPatternComponentBase::ShouldAdvanceStep(const FBossPatternContext& Context) const
{
	(void)Context;

	switch (CurrentStep)
	{
	case EBossPatternStep::Windup:
		return StepElapsed >= WindupDuration;
	case EBossPatternStep::Task1:
		return true;
	case EBossPatternStep::Recovery:
		return StepElapsed >= RecoveryDuration;
	default:
		return false;
	}
}

EBossPatternStep UBossPatternComponentBase::GetNextStep(EBossPatternStep Step) const
{
	switch (Step)
	{
	case EBossPatternStep::Windup:
		return EBossPatternStep::Task1;
	case EBossPatternStep::Task1:
		return EBossPatternStep::Recovery;
	case EBossPatternStep::Recovery:
		return EBossPatternStep::Finished;
	default:
		return EBossPatternStep::Finished;
	}
}

void UBossPatternComponentBase::FinishPattern(const FBossPatternContext& Context)
{
	if (CurrentStep != EBossPatternStep::Finished)
	{
		CurrentStep = EBossPatternStep::Finished;
		StepElapsed = 0.0f;
	}

	bRunning = false;
	bFinished = true;
	OnPatternEnd(Context);
}

void UBossPatternComponentBase::EnterStep(EBossPatternStep NewStep, const FBossPatternContext& Context)
{
	CurrentStep = NewStep;
	StepElapsed = 0.0f;
	OnStepEnter(NewStep, Context);

	if (NewStep == EBossPatternStep::Finished)
	{
		FinishPattern(Context);
	}
}

void UBossPatternComponentBase::FaceTarget(const FBossPatternContext& Context) const
{
	if (!Context.BossActor || Context.DirectionToTarget.IsNearlyZero())
	{
		return;
	}

	const FVector Direction = Context.DirectionToTarget.Normalized();
	const float YawDegrees = std::atan2(Direction.Y, Direction.X) * 180.0f / BossPatternPi;
	Context.BossActor->SetActorRotation(FRotator(0.0f, YawDegrees, 0.0f));
}
