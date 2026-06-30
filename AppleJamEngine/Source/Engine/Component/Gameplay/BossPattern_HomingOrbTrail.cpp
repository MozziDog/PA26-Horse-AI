#include "BossPattern_HomingOrbTrail.h"

#include "GameFramework/AActor.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float TwoPi = 6.28318530718f;

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		return Direction.IsNearlyZero() ? Fallback : Direction.Normalized();
	}
}

UBossPattern_HomingOrbTrail::UBossPattern_HomingOrbTrail()
{
	PatternName = "HomingOrbTrail";
	Weight = 1.0f;
	Cooldown = 2.5f;
	WindupDuration = 0.25f;
	RecoveryDuration = 0.35f;
}

bool UBossPattern_HomingOrbTrail::GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const
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

FString UBossPattern_HomingOrbTrail::GetRuntimeDebugText() const
{
	int32 PendingCount = 0;
	for (const FHomingOrbPendingLaunch& Pending : PendingLaunches)
	{
		if (!Pending.bLaunched)
		{
			++PendingCount;
		}
	}

	return "Spawned=" + std::to_string(SpawnedCount)
		+ " PendingLaunches=" + std::to_string(PendingCount);
}

void UBossPattern_HomingOrbTrail::OnPatternStart(const FBossPatternContext& Context)
{
	PendingLaunches.clear();
	LockedTargetPosition = Context.TargetActor ? Context.TargetLocation : Context.BossLocation + Context.BossForward;
	SpawnedCount = 0;
	NextSpawnTime = 0.0f;
}

void UBossPattern_HomingOrbTrail::TickCurrentStep(float DeltaTime, const FBossPatternContext& Context)
{
	if (CurrentStep == EBossPatternStep::Task1)
	{
		// TODO(BossMovement): Move the boss forward during HomingOrbTrail.
		// Movement ownership is handled by another teammate; this pattern only spawns and launches projectiles for now.
		TickSpawning(Context);
		TickPendingLaunches(DeltaTime, Context);
	}
	else if (CurrentStep == EBossPatternStep::Task2)
	{
		TickPendingLaunches(DeltaTime, Context);
	}
}

bool UBossPattern_HomingOrbTrail::ShouldAdvanceStep(const FBossPatternContext& Context) const
{
	(void)Context;

	switch (CurrentStep)
	{
	case EBossPatternStep::Windup:
		return StepElapsed >= WindupDuration;
	case EBossPatternStep::Task1:
		return SpawnedCount >= (std::max)(1, ProjectileCount);
	case EBossPatternStep::Task2:
		return !HasPendingLaunches();
	case EBossPatternStep::Recovery:
		return StepElapsed >= RecoveryDuration;
	default:
		return false;
	}
}

EBossPatternStep UBossPattern_HomingOrbTrail::GetNextStep(EBossPatternStep Step) const
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

void UBossPattern_HomingOrbTrail::TickSpawning(const FBossPatternContext& Context)
{
	if (!Context.BulletHell)
	{
		SpawnedCount = (std::max)(1, ProjectileCount);
		return;
	}

	const int32 Count = (std::max)(1, ProjectileCount);
	const float Interval = GetSpawnInterval();
	while (SpawnedCount < Count && StepElapsed + 0.0001f >= NextSpawnTime)
	{
		SpawnOrb(Context);
		++SpawnedCount;

		if (Interval <= 0.0f)
		{
			NextSpawnTime = StepElapsed;
			continue;
		}

		NextSpawnTime += Interval;
	}
}

void UBossPattern_HomingOrbTrail::SpawnOrb(const FBossPatternContext& Context)
{
	const int32 Count = (std::max)(1, ProjectileCount);
	const float Angle = TwoPi * static_cast<float>(SpawnedCount) / static_cast<float>(Count);
	const FVector RingOffset =
		Context.BossRight * std::cos(Angle) * SpawnRadiusAroundBoss
		+ Context.BossUp * std::sin(Angle) * SpawnRadiusAroundBoss;
	const FVector Position =
		Context.BossLocation
		+ Context.BossForward * SpawnForwardOffset
		+ Context.BossUp * SpawnUpOffset
		+ RingOffset;
	const FVector TargetPosition = Context.TargetActor ? Context.TargetLocation : LockedTargetPosition;
	const FVector FallbackDirection = SafeDirection(TargetPosition - Position, Context.BossForward);

	const FBulletHandle Handle = Context.BulletHell->SpawnBullet(MakeStationaryBulletParams(Position));
	if (Handle.IsValid())
	{
		FHomingOrbPendingLaunch Pending;
		Pending.Handle = Handle;
		Pending.FallbackDirection = FallbackDirection;
		Pending.LastTargetPosition = TargetPosition;
		Pending.RemainingDelay = LaunchDelay;
		PendingLaunches.push_back(Pending);
	}
}

void UBossPattern_HomingOrbTrail::TickPendingLaunches(float DeltaTime, const FBossPatternContext& Context)
{
	if (!Context.BulletHell)
	{
		PendingLaunches.clear();
		return;
	}

	const FVector TargetPosition = Context.TargetActor ? Context.TargetLocation : LockedTargetPosition;
	for (FHomingOrbPendingLaunch& Pending : PendingLaunches)
	{
		if (Pending.bLaunched)
		{
			continue;
		}

		Pending.RemainingDelay -= DeltaTime;
		if (Context.TargetActor)
		{
			Pending.LastTargetPosition = TargetPosition;
		}

		if (Pending.RemainingDelay > 0.0f)
		{
			continue;
		}

		const FBulletInstance* Bullet = Context.BulletHell->FindBullet(Pending.Handle);
		const FVector LaunchDirection = Bullet
			? SafeDirection(Pending.LastTargetPosition - Bullet->Position, Pending.FallbackDirection)
			: Pending.FallbackDirection;

		FBulletLaunchParams LaunchParams;
		LaunchParams.Velocity = LaunchDirection * ProjectileSpeed;
		LaunchParams.bSetHoming = true;
		LaunchParams.bHoming = Context.TargetActor != nullptr;
		LaunchParams.HomingTargetPosition = Pending.LastTargetPosition;
		LaunchParams.HomingTargetActor = Context.TargetActor;
		LaunchParams.HomingStrength = HomingStrength;
		LaunchParams.HomingMaxTurnRateDegrees = HomingMaxTurnRateDegrees;
		LaunchParams.HomingConeHalfAngleDegrees = HomingConeHalfAngleDegrees;
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

bool UBossPattern_HomingOrbTrail::HasPendingLaunches() const
{
	for (const FHomingOrbPendingLaunch& Pending : PendingLaunches)
	{
		if (!Pending.bLaunched)
		{
			return true;
		}
	}
	return false;
}

FBulletSpawnParams UBossPattern_HomingOrbTrail::MakeStationaryBulletParams(const FVector& Position) const
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

float UBossPattern_HomingOrbTrail::GetSpawnInterval() const
{
	if (SpawnIntervalOverride > 0.0f)
	{
		return SpawnIntervalOverride;
	}

	const int32 Count = (std::max)(1, ProjectileCount);
	return Count > 0 ? SpawnDuration / static_cast<float>(Count) : 0.0f;
}
