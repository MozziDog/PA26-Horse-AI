#include "BossPattern_IdleTrackTarget.h"

UBossPattern_IdleTrackTarget::UBossPattern_IdleTrackTarget()
{
	PatternName = "IdleTrackTarget";
	Weight = 1.0f;
	Cooldown = 0.0f;
	WindupDuration = 0.0f;
	RecoveryDuration = 0.0f;
	bBlockImmediateRepeat = false;
}

bool UBossPattern_IdleTrackTarget::ShouldAdvanceStep(const FBossPatternContext& Context) const
{
	(void)Context;

	switch (CurrentStep)
	{
	case EBossPatternStep::Windup:
		return true;
	case EBossPatternStep::Task1:
		return StepElapsed >= IdleDuration;
	case EBossPatternStep::Recovery:
		return true;
	default:
		return false;
	}
}

EBossPatternStep UBossPattern_IdleTrackTarget::GetNextStep(EBossPatternStep Step) const
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
