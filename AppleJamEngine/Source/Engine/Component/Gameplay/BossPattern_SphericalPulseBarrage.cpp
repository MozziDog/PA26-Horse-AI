#include "BossPattern_SphericalPulseBarrage.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float GoldenAngle = 2.39996322973f;
	constexpr uint32 LcgA = 1664525u;
	constexpr uint32 LcgC = 1013904223u;

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		return Direction.IsNearlyZero() ? Fallback : Direction.Normalized();
	}

	float HashUnitFloat(uint32 Seed)
	{
		uint32 Value = Seed * LcgA + LcgC;
		Value = Value * LcgA + LcgC;
		return static_cast<float>(Value & 0x00ffffffu) / static_cast<float>(0x01000000u);
	}
}

UBossPattern_SphericalPulseBarrage::UBossPattern_SphericalPulseBarrage()
{
	PatternName = "SphericalPulseBarrage";
	Weight = 1.0f;
	Cooldown = 3.0f;
	WindupDuration = 0.3f;
	RecoveryDuration = 0.4f;
}

bool UBossPattern_SphericalPulseBarrage::GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const
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

FString UBossPattern_SphericalPulseBarrage::GetRuntimeDebugText() const
{
	return "Pulses=" + std::to_string(SpawnedPulseCount);
}

void UBossPattern_SphericalPulseBarrage::OnPatternStart(const FBossPatternContext& Context)
{
	(void)Context;
	SpawnedPulseCount = 0;
	NextPulseTime = 0.0f;
}

void UBossPattern_SphericalPulseBarrage::TickCurrentStep(float DeltaTime, const FBossPatternContext& Context)
{
	(void)DeltaTime;

	if (CurrentStep == EBossPatternStep::Task1)
	{
		TickPulseSpawning(Context);
	}
}

bool UBossPattern_SphericalPulseBarrage::ShouldAdvanceStep(const FBossPatternContext& Context) const
{
	(void)Context;

	switch (CurrentStep)
	{
	case EBossPatternStep::Windup:
		return StepElapsed >= WindupDuration;
	case EBossPatternStep::Task1:
		return SpawnedPulseCount >= (std::max)(1, PulseCount);
	case EBossPatternStep::Recovery:
		return StepElapsed >= RecoveryDuration;
	default:
		return false;
	}
}

EBossPatternStep UBossPattern_SphericalPulseBarrage::GetNextStep(EBossPatternStep Step) const
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

void UBossPattern_SphericalPulseBarrage::TickPulseSpawning(const FBossPatternContext& Context)
{
	if (!Context.BulletHell)
	{
		SpawnedPulseCount = (std::max)(1, PulseCount);
		return;
	}

	const int32 Count = (std::max)(1, PulseCount);
	const float Interval = GetPulseInterval();
	while (SpawnedPulseCount < Count && StepElapsed + 0.0001f >= NextPulseTime)
	{
		SpawnPulse(Context);
		++SpawnedPulseCount;

		if (Interval <= 0.0f)
		{
			NextPulseTime = StepElapsed;
			continue;
		}

		NextPulseTime += Interval;
	}
}

void UBossPattern_SphericalPulseBarrage::SpawnPulse(const FBossPatternContext& Context)
{
	const int32 Count = (std::max)(1, ProjectilesPerPulse);
	const FVector Center = Context.BossLocation;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Direction = MakeSphereDirection(Index, Count, SpawnedPulseCount);
		const FVector Position = Center + Direction * SphereRadius;
		Context.BulletHell->SpawnBullet(MakeBulletParams(Position, Direction));
	}
}

FBulletSpawnParams UBossPattern_SphericalPulseBarrage::MakeBulletParams(const FVector& Position, const FVector& Direction) const
{
	FBulletSpawnParams Params;
	Params.Position = Position;
	Params.Velocity = SafeDirection(Direction, FVector::ForwardVector) * ProjectileSpeed;
	Params.Archetype.Radius = ProjectileRadius;
	Params.Archetype.Speed = ProjectileSpeed;
	Params.Archetype.Lifetime = ProjectileLifetime;
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

float UBossPattern_SphericalPulseBarrage::GetPulseInterval() const
{
	if (PulseIntervalOverride > 0.0f)
	{
		return PulseIntervalOverride;
	}

	const int32 Count = (std::max)(1, PulseCount);
	return Count > 0 ? PulseDuration / static_cast<float>(Count) : 0.0f;
}

FVector UBossPattern_SphericalPulseBarrage::MakeSphereDirection(int32 Index, int32 Count, int32 PulseIndex) const
{
	const float T = (static_cast<float>(Index) + 0.5f) / static_cast<float>((std::max)(1, Count));
	float Z = 1.0f - 2.0f * T;
	float Theta = GoldenAngle * static_cast<float>(Index + PulseIndex * Count);

	if (bUseRandomSpherePoints)
	{
		const uint32 BaseSeed =
			static_cast<uint32>(RandomSeedOffset)
			^ (static_cast<uint32>(PulseIndex) * 73856093u)
			^ (static_cast<uint32>(Index) * 19349663u);
		Z = HashUnitFloat(BaseSeed) * 2.0f - 1.0f;
		Theta += HashUnitFloat(BaseSeed ^ 0xa511e9b3u) * GoldenAngle;
	}

	const float RadiusXY = std::sqrt((std::max)(0.0f, 1.0f - Z * Z));
	return SafeDirection(
		FVector(std::cos(Theta) * RadiusXY, std::sin(Theta) * RadiusXY, Z),
		FVector::ForwardVector);
}
