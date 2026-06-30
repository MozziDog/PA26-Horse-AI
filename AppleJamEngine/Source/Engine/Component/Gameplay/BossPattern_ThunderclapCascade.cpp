#include "BossPattern_ThunderclapCascade.h"

#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace
{
	constexpr float TwoPi = 6.28318530718f;

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		return Direction.IsNearlyZero() ? Fallback : Direction.Normalized();
	}

	float RandomUnitFloat()
	{
		static thread_local std::mt19937 Generator(0x4c1a7d3bu);
		static thread_local std::uniform_real_distribution<float> Distribution(0.0f, 1.0f);
		return Distribution(Generator);
	}

	FVector RandomPointInXYDisk(float Radius)
	{
		if (Radius <= 0.0f)
		{
			return FVector::ZeroVector;
		}

		const float Distance = std::sqrt(RandomUnitFloat()) * Radius;
		const float Angle = RandomUnitFloat() * TwoPi;
		return FVector(std::cos(Angle) * Distance, std::sin(Angle) * Distance, 0.0f);
	}
}

UBossPattern_ThunderclapCascade::UBossPattern_ThunderclapCascade()
{
	PatternName = "ThunderclapCascade";
	Weight = 1.0f;
	Cooldown = 3.5f;
	WindupDuration = 0.35f;
	RecoveryDuration = 0.45f;
}

bool UBossPattern_ThunderclapCascade::GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const
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

FString UBossPattern_ThunderclapCascade::GetRuntimeDebugText() const
{
	int32 ActiveCount = 0;
	for (const FThunderclapCycleState& Cycle : ActiveCycles)
	{
		if (!Cycle.bFinished)
		{
			++ActiveCount;
		}
	}

	return "StartedCycles=" + std::to_string(StartedCycleCount)
		+ " ActiveCycles=" + std::to_string(ActiveCount);
}

void UBossPattern_ThunderclapCascade::OnPatternStart(const FBossPatternContext& Context)
{
	(void)Context;
	ActiveCycles.clear();
	StartedCycleCount = 0;
	NextCycleStartTime = 0.0f;
	bForceDurationReached = false;
}

void UBossPattern_ThunderclapCascade::TickCurrentStep(float DeltaTime, const FBossPatternContext& Context)
{
	if (CurrentStep == EBossPatternStep::Task1)
	{
		TickCycleStarting(Context);
		TickActiveCycles(DeltaTime, Context);
		if (PatternElapsed >= MaxPatternDuration)
		{
			bForceDurationReached = true;
			ForceFinishAllCycles(Context);
		}
	}
}

bool UBossPattern_ThunderclapCascade::ShouldAdvanceStep(const FBossPatternContext& Context) const
{
	(void)Context;

	switch (CurrentStep)
	{
	case EBossPatternStep::Windup:
		return StepElapsed >= WindupDuration;
	case EBossPatternStep::Task1:
		return bForceDurationReached
			|| (StartedCycleCount >= (std::max)(1, CycleCount) && !HasActiveCycles());
	case EBossPatternStep::Recovery:
		return StepElapsed >= RecoveryDuration;
	default:
		return false;
	}
}

EBossPatternStep UBossPattern_ThunderclapCascade::GetNextStep(EBossPatternStep Step) const
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

void UBossPattern_ThunderclapCascade::TickCycleStarting(const FBossPatternContext& Context)
{
	if (!Context.BulletHell)
	{
		StartedCycleCount = (std::max)(1, CycleCount);
		return;
	}

	const int32 Count = (std::max)(1, CycleCount);
	while (StartedCycleCount < Count && StepElapsed + 0.0001f >= NextCycleStartTime)
	{
		StartCycle(Context);
		++StartedCycleCount;

		if (CycleInterval <= 0.0f)
		{
			NextCycleStartTime = StepElapsed;
			continue;
		}

		NextCycleStartTime += CycleInterval;
	}
}

void UBossPattern_ThunderclapCascade::TickActiveCycles(float DeltaTime, const FBossPatternContext& Context)
{
	if (!Context.BulletHell)
	{
		ActiveCycles.clear();
		return;
	}

	for (FThunderclapCycleState& Cycle : ActiveCycles)
	{
		if (Cycle.bFinished)
		{
			continue;
		}

		Cycle.Age += DeltaTime;
		const FBulletInstance* Strike = Cycle.StrikeHandle.IsValid()
			? Context.BulletHell->FindBullet(Cycle.StrikeHandle)
			: nullptr;
		const bool bStrikeMissing = Cycle.bStrikeSpawned && !Strike;
		const bool bImpactReached = Strike && Strike->Position.Z <= Cycle.ImpactLocation.Z;
		const bool bStrikeTimedOut = Cycle.Age >= StrikeLifetime;

		if (!Cycle.bShockwaveSpawned && (bStrikeMissing || bImpactReached || bStrikeTimedOut))
		{
			if (Strike)
			{
				Context.BulletHell->KillBullet(Cycle.StrikeHandle);
			}
			SpawnShockwave(Cycle, Context);
		}

		if (Cycle.bShockwaveSpawned)
		{
			FinishCycle(Cycle, Context);
		}
	}
}

void UBossPattern_ThunderclapCascade::StartCycle(const FBossPatternContext& Context)
{
	FThunderclapCycleState Cycle;
	const FVector CandidateLocation =
		Context.BossLocation
		+ SafeDirection(Context.BossForward, FVector::ForwardVector) * StrikeForwardDistance
		+ RandomPointInXYDisk(StrikeRandomXYRadius);
	Cycle.ImpactLocation = ResolveImpactLocation(Context, CandidateLocation);
	SpawnStrike(Cycle, Context);
	ActiveCycles.push_back(Cycle);
}

FVector UBossPattern_ThunderclapCascade::ResolveImpactLocation(const FBossPatternContext& Context, const FVector& CandidateLocation) const
{
	FVector ImpactLocation = CandidateLocation;

	float GroundZ = CandidateLocation.Z;
	if (SampleGroundHeight(Context, CandidateLocation, GroundZ))
	{
		ImpactLocation.Z = GroundZ;
	}

	ImpactLocation.Z += GroundHeightOffset;
	return ImpactLocation;
}

bool UBossPattern_ThunderclapCascade::SampleGroundHeight(const FBossPatternContext& Context, const FVector& CandidateLocation, float& OutGroundZ) const
{
	UWorld* World = Context.BossActor ? Context.BossActor->GetWorld() : GetWorld();
	if (!World)
	{
		return false;
	}

	const float StartHeight = (std::max)(0.0f, GroundTraceStartHeight);
	const float DownDistance = (std::max)(0.0f, GroundTraceDownDistance);
	const float MaxDistance = StartHeight + DownDistance;
	if (MaxDistance <= 0.0f)
	{
		return false;
	}

	const FVector TraceStart = CandidateLocation + FVector::UpVector * StartHeight;
	FHitResult Hit;
	if (!World->PhysicsRaycastByObjectTypes(
		TraceStart,
		FVector::DownVector,
		MaxDistance,
		Hit,
		ObjectTypeBit(ECollisionChannel::WorldStatic),
		Context.BossActor))
	{
		return false;
	}

	if (!Hit.bHit)
	{
		return false;
	}

	OutGroundZ = Hit.WorldHitLocation.Z;
	return true;
}

void UBossPattern_ThunderclapCascade::SpawnStrike(FThunderclapCycleState& Cycle, const FBossPatternContext& Context)
{
	if (!Context.BulletHell)
	{
		return;
	}

	const FVector StrikePosition = Cycle.ImpactLocation + FVector::UpVector * StrikeSpawnHeight;
	Cycle.StrikeHandle = Context.BulletHell->SpawnBullet(MakeStrikeParams(StrikePosition));
	Cycle.bStrikeSpawned = Cycle.StrikeHandle.IsValid();
}

void UBossPattern_ThunderclapCascade::SpawnShockwave(FThunderclapCycleState& Cycle, const FBossPatternContext& Context)
{
	if (!Context.BulletHell)
	{
		Cycle.bShockwaveSpawned = true;
		return;
	}

	const int32 Count = (std::max)(1, ShockwaveProjectileCount);
	const FVector Center = Cycle.ImpactLocation + FVector::UpVector * ShockwaveHeightOffset;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = TwoPi * static_cast<float>(Index) / static_cast<float>(Count);
		const FVector Direction = SafeDirection(FVector(std::cos(Angle), std::sin(Angle), 0.0f), FVector::ForwardVector);
		const FVector Position = Center + Direction * ShockwaveRadius;
		Context.BulletHell->SpawnBullet(MakeShockwaveParams(Position, Direction));
	}

	Cycle.bShockwaveSpawned = true;
}

void UBossPattern_ThunderclapCascade::FinishCycle(FThunderclapCycleState& Cycle, const FBossPatternContext& Context)
{
	(void)Context;
	Cycle.bFinished = true;
}

void UBossPattern_ThunderclapCascade::ForceFinishAllCycles(const FBossPatternContext& Context)
{
	if (Context.BulletHell)
	{
		for (FThunderclapCycleState& Cycle : ActiveCycles)
		{
			if (Cycle.StrikeHandle.IsValid() && Context.BulletHell->IsBulletAlive(Cycle.StrikeHandle))
			{
				Context.BulletHell->KillBullet(Cycle.StrikeHandle);
			}
			Cycle.bFinished = true;
		}
	}

	ActiveCycles.clear();
}

bool UBossPattern_ThunderclapCascade::HasActiveCycles() const
{
	for (const FThunderclapCycleState& Cycle : ActiveCycles)
	{
		if (!Cycle.bFinished)
		{
			return true;
		}
	}
	return false;
}

FBulletSpawnParams UBossPattern_ThunderclapCascade::MakeStrikeParams(const FVector& Position) const
{
	FBulletSpawnParams Params;
	Params.Position = Position;
	Params.Velocity = FVector::DownVector * StrikeFallSpeed;
	Params.Archetype.Radius = StrikeProjectileRadius;
	Params.Archetype.Speed = StrikeFallSpeed;
	Params.Archetype.Lifetime = StrikeLifetime;
	Params.Archetype.RenderScale = StrikeRenderScale;
	Params.Archetype.MeshPath = StrikeMeshPath;
	Params.Archetype.MaterialPath = StrikeMaterialPath;
	Params.Archetype.Trail.bEnableTrail = bStrikeTrailEnabled;
	Params.Archetype.Trail.MaterialPath = StrikeTrailMaterialPath;
	Params.Archetype.Trail.Color = StrikeTrailColor;
	Params.Archetype.Trail.Width = (std::max)(0.001f, StrikeTrailWidth);
	Params.Archetype.Trail.Lifetime = (std::max)(0.001f, StrikeTrailLifetime);
	Params.Archetype.Trail.MaxSamples = (std::max)(2, StrikeTrailMaxSamples);
	Params.Archetype.Trail.SampleInterval = (std::max)(0.0f, StrikeTrailSampleInterval);
	Params.Archetype.Trail.MinSampleDistance = (std::max)(0.0f, StrikeTrailMinSampleDistance);
	Params.Archetype.DeathEffect.bEnableDeathEffect = bStrikeDeathEffectEnabled;
	Params.Archetype.DeathEffect.ParticleSystemPath = StrikeDeathEffectPath;
	Params.Archetype.DeathEffect.EventName = StrikeDeathEffectEventName;
	Params.Archetype.DeathEffect.bInheritBulletVelocity = bStrikeDeathEffectInheritVelocity;
	Params.Archetype.DeathEffect.VelocityScale = StrikeDeathEffectVelocityScale;
	Params.bHoming = false;
	return Params;
}

FBulletSpawnParams UBossPattern_ThunderclapCascade::MakeShockwaveParams(const FVector& Position, const FVector& Direction) const
{
	FBulletSpawnParams Params;
	Params.Position = Position;
	Params.Velocity = SafeDirection(Direction, FVector::ForwardVector) * ShockwaveSpeed;
	Params.Archetype.Radius = ShockwaveProjectileRadius;
	Params.Archetype.Speed = ShockwaveSpeed;
	Params.Archetype.Lifetime = ShockwaveLifetime;
	Params.Archetype.RenderScale = ShockwaveRenderScale;
	Params.Archetype.MeshPath = ShockwaveMeshPath;
	Params.Archetype.MaterialPath = ShockwaveMaterialPath;
	Params.Archetype.Trail.bEnableTrail = bShockwaveTrailEnabled;
	Params.Archetype.Trail.MaterialPath = ShockwaveTrailMaterialPath;
	Params.Archetype.Trail.Color = ShockwaveTrailColor;
	Params.Archetype.Trail.Width = (std::max)(0.001f, ShockwaveTrailWidth);
	Params.Archetype.Trail.Lifetime = (std::max)(0.001f, ShockwaveTrailLifetime);
	Params.Archetype.Trail.MaxSamples = (std::max)(2, ShockwaveTrailMaxSamples);
	Params.Archetype.Trail.SampleInterval = (std::max)(0.0f, ShockwaveTrailSampleInterval);
	Params.Archetype.Trail.MinSampleDistance = (std::max)(0.0f, ShockwaveTrailMinSampleDistance);
	Params.Archetype.DeathEffect.bEnableDeathEffect = bShockwaveDeathEffectEnabled;
	Params.Archetype.DeathEffect.ParticleSystemPath = ShockwaveDeathEffectPath;
	Params.Archetype.DeathEffect.EventName = ShockwaveDeathEffectEventName;
	Params.Archetype.DeathEffect.bInheritBulletVelocity = bShockwaveDeathEffectInheritVelocity;
	Params.Archetype.DeathEffect.VelocityScale = ShockwaveDeathEffectVelocityScale;
	Params.bHoming = false;
	return Params;
}
