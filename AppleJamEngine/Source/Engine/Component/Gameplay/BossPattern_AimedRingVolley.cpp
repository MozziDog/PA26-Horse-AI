#include "BossPattern_AimedRingVolley.h"

#include "GameFramework/AActor.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float Pi = 3.1415926535f;
	constexpr float TwoPi = 6.28318530718f;

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		return Direction.IsNearlyZero() ? Fallback : Direction.Normalized();
	}

	void MakePlaneBasis(const FVector& Normal, FVector& OutU, FVector& OutV)
	{
		const FVector N = SafeDirection(Normal, FVector::ForwardVector);
		const FVector Reference = std::fabs(N.Z) < 0.9f ? FVector::UpVector : FVector::RightVector;
		OutU = SafeDirection(Reference.Cross(N), FVector::RightVector);
		OutV = SafeDirection(N.Cross(OutU), FVector::UpVector);
	}
}

UBossPattern_AimedRingVolley::UBossPattern_AimedRingVolley()
{
	PatternName = "AimedRingVolley";
	Weight = 1.0f;
	Cooldown = 2.0f;
	WindupDuration = 0.35f;
	RecoveryDuration = 0.35f;
}

bool UBossPattern_AimedRingVolley::GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const
{
	if (!UBossPatternComponentBase::GetCanUse(Context, OutRejectReason))
	{
		return false;
	}

	if (!Context.BulletHell)
	{
		if (OutRejectReason) *OutRejectReason = PatternName + ": missing BulletHell";
		return false;
	}

	return true;
}

FString UBossPattern_AimedRingVolley::GetRuntimeDebugText() const
{
	int32 PendingCount = 0;
	for (const FPendingBossProjectileLaunch& Pending : PendingLaunches)
	{
		if (!Pending.bLaunched)
		{
			++PendingCount;
		}
	}

	return "PendingLaunches=" + std::to_string(PendingCount);
}

void UBossPattern_AimedRingVolley::OnPatternStart(const FBossPatternContext& Context)
{
	PendingLaunches.clear();
	bRingSpawned = false;

	LockedAimDirection = SafeDirection(Context.DirectionToTarget, Context.BossForward);
	LockedTargetPosition = Context.TargetActor ? Context.TargetLocation : Context.BossLocation + LockedAimDirection;
	MakePlaneBasis(LockedAimDirection, LockedRingU, LockedRingV);
}

void UBossPattern_AimedRingVolley::OnStepEnter(EBossPatternStep Step, const FBossPatternContext& Context)
{
	if (Step == EBossPatternStep::Task1)
	{
		SpawnRingProjectiles(Context);
	}
}

void UBossPattern_AimedRingVolley::TickCurrentStep(float DeltaTime, const FBossPatternContext& Context)
{
	if (CurrentStep == EBossPatternStep::Task2)
	{
		TickPendingLaunches(DeltaTime, Context);
	}
}

bool UBossPattern_AimedRingVolley::ShouldAdvanceStep(const FBossPatternContext& Context) const
{
	switch (CurrentStep)
	{
	case EBossPatternStep::Windup:
		return StepElapsed >= WindupDuration;
	case EBossPatternStep::Task1:
		return bRingSpawned;
	case EBossPatternStep::Task2:
		return !HasPendingLaunches();
	case EBossPatternStep::Recovery:
		return StepElapsed >= RecoveryDuration;
	default:
		return false;
	}
}

EBossPatternStep UBossPattern_AimedRingVolley::GetNextStep(EBossPatternStep Step) const
{
	switch (Step)
	{
	case EBossPatternStep::Windup:
		return EBossPatternStep::Task1;
	case EBossPatternStep::Task1:
		return EBossPatternStep::Task2;
	case EBossPatternStep::Task2:
		return EBossPatternStep::Recovery;
	case EBossPatternStep::Recovery:
		return EBossPatternStep::Finished;
	default:
		return EBossPatternStep::Finished;
	}
}

void UBossPattern_AimedRingVolley::SpawnRingProjectiles(const FBossPatternContext& Context)
{
	if (bRingSpawned || !Context.BulletHell)
	{
		bRingSpawned = true;
		return;
	}

	const FVector AimDirection = bLockTargetDirectionOnStart
		? LockedAimDirection
		: SafeDirection(Context.DirectionToTarget, LockedAimDirection);
	FVector RingU = LockedRingU;
	FVector RingV = LockedRingV;
	if (!bLockTargetDirectionOnStart)
	{
		MakePlaneBasis(AimDirection, RingU, RingV);
	}

	const FVector Center =
		Context.BossLocation
		+ Context.BossForward * SpawnForwardOffset
		+ Context.BossUp * SpawnUpOffset;
	const int32 Count = (std::max)(1, ProjectileCount);
	const float AngleOffsetRadians = AngleOffsetDegrees * (Pi / 180.0f);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = AngleOffsetRadians + TwoPi * static_cast<float>(Index) / static_cast<float>(Count);
		const FVector Offset = RingU * std::cos(Angle) * RingRadius + RingV * std::sin(Angle) * RingRadius;
		const FVector Position = Center + Offset;
		const FVector TargetPosition = Context.TargetActor ? Context.TargetLocation : LockedTargetPosition;
		const FVector LaunchDirection = SafeDirection(TargetPosition - Position, AimDirection);

		FBulletSpawnParams Params = MakeStationaryBulletParams(Position);
		const FBulletHandle Handle = Context.BulletHell->SpawnBullet(Params);
		if (Handle.IsValid())
		{
			FPendingBossProjectileLaunch Pending;
			Pending.Handle = Handle;
			Pending.LaunchDirection = LaunchDirection;
			Pending.TargetPosition = TargetPosition;
			Pending.RemainingDelay = LaunchDelay;
			PendingLaunches.push_back(Pending);
		}
	}

	bRingSpawned = true;
}

void UBossPattern_AimedRingVolley::TickPendingLaunches(float DeltaTime, const FBossPatternContext& Context)
{
	if (!Context.BulletHell)
	{
		PendingLaunches.clear();
		return;
	}

	for (FPendingBossProjectileLaunch& Pending : PendingLaunches)
	{
		if (Pending.bLaunched)
		{
			continue;
		}

		Pending.RemainingDelay -= DeltaTime;
		if (Pending.RemainingDelay > 0.0f)
		{
			continue;
		}

		FBulletLaunchParams LaunchParams;
		LaunchParams.Velocity = SafeDirection(Pending.LaunchDirection, LockedAimDirection) * ProjectileSpeed;
		LaunchParams.bSetHoming = true;
		LaunchParams.bHoming = false;
		LaunchParams.HomingTargetPosition = Pending.TargetPosition;
		LaunchParams.bResetAge = true;
		LaunchParams.bSetLifetime = true;
		LaunchParams.Lifetime = ProjectileLifetime;
		Pending.bLaunched = Context.BulletHell->LaunchBullet(Pending.Handle, LaunchParams);
		if (!Pending.bLaunched && !Context.BulletHell->IsBulletAlive(Pending.Handle))
		{
			Pending.bLaunched = true;
		}
	}
}

bool UBossPattern_AimedRingVolley::HasPendingLaunches() const
{
	for (const FPendingBossProjectileLaunch& Pending : PendingLaunches)
	{
		if (!Pending.bLaunched)
		{
			return true;
		}
	}
	return false;
}

FBulletSpawnParams UBossPattern_AimedRingVolley::MakeStationaryBulletParams(const FVector& Position) const
{
	FBulletSpawnParams Params;
	Params.Position = Position;
	Params.Velocity = FVector::ZeroVector;
	Params.Archetype.Radius = ProjectileRadius;
	Params.Archetype.Speed = 0.0f;
	Params.Archetype.Lifetime = ProjectileLifetime < 0.0f ? -1.0f : LaunchDelay + (std::max)(0.0f, ProjectileLifetime);
	Params.Archetype.RenderScale = ProjectileRenderScale;
	Params.Archetype.MeshPath = ProjectileMeshPath;
	Params.Archetype.MaterialPath = ProjectileMaterialPath;
	Params.Archetype.Trail.bEnableTrail = bProjectileTrailEnabled;
	Params.Archetype.Trail.MaterialPath = ProjectileTrailMaterialPath;
	Params.Archetype.Trail.Color = ProjectileTrailColor;
	Params.Archetype.Trail.Width = (std::max)(0.001f, ProjectileTrailWidth);
	Params.Archetype.Trail.Lifetime = (std::max)(0.001f, ProjectileTrailLifetime);
	Params.Archetype.Trail.MaxSamples = (std::max)(2, ProjectileTrailMaxSamples);
	Params.Archetype.Trail.SampleInterval = (std::max)(0.0f, ProjectileTrailSampleInterval);
	Params.Archetype.Trail.MinSampleDistance = (std::max)(0.0f, ProjectileTrailMinSampleDistance);
	Params.Archetype.DeathEffect.bEnableDeathEffect = bProjectileDeathEffectEnabled;
	Params.Archetype.DeathEffect.ParticleSystemPath = ProjectileDeathEffectPath;
	Params.Archetype.DeathEffect.EventName = ProjectileDeathEffectEventName;
	Params.Archetype.DeathEffect.bInheritBulletVelocity = bProjectileDeathEffectInheritVelocity;
	Params.Archetype.DeathEffect.VelocityScale = ProjectileDeathEffectVelocityScale;
	Params.bHoming = false;
	Params.HomingStrength = 0.0f;
	Params.HomingMaxTurnRateDegrees = 0.0f;
	Params.HomingConeHalfAngleDegrees = 180.0f;
	return Params;
}
