#include "BossPattern_RendingClawArc.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float Pi = 3.1415926535f;

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		return Direction.IsNearlyZero() ? Fallback : Direction.Normalized();
	}

	FVector MakePerpendicularDirection(const FVector& Preferred, const FVector& Axis)
	{
		const FVector SafeAxis = SafeDirection(Axis, FVector::ForwardVector);
		const FVector Projected = Preferred - SafeAxis * Preferred.Dot(SafeAxis);
		if (!Projected.IsNearlyZero())
		{
			return Projected.Normalized();
		}

		const FVector UpProjected = FVector::UpVector - SafeAxis * FVector::UpVector.Dot(SafeAxis);
		if (!UpProjected.IsNearlyZero())
		{
			return UpProjected.Normalized();
		}

		const FVector RightProjected = FVector::RightVector - SafeAxis * FVector::RightVector.Dot(SafeAxis);
		return SafeDirection(RightProjected, FVector::RightVector);
	}
}

UBossPattern_RendingClawArc::UBossPattern_RendingClawArc()
{
	PatternName = "RendingClawArc";
	Weight = 1.0f;
	Cooldown = 2.5f;
	WindupDuration = 0.3f;
	RecoveryDuration = 0.35f;
}

bool UBossPattern_RendingClawArc::GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const
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

FString UBossPattern_RendingClawArc::GetRuntimeDebugText() const
{
	return "Planes=" + std::to_string(UsedPlaneCount) + " Projectiles=" + std::to_string(SpawnedProjectileCount);
}

void UBossPattern_RendingClawArc::OnPatternStart(const FBossPatternContext& Context)
{
	(void)Context;
	bClawArcsSpawned = false;
	SpawnedProjectileCount = 0;
	UsedPlaneCount = 0;
}

void UBossPattern_RendingClawArc::OnStepEnter(EBossPatternStep Step, const FBossPatternContext& Context)
{
	if (Step == EBossPatternStep::Task1)
	{
		SpawnClawArcs(Context);
	}
}

bool UBossPattern_RendingClawArc::ShouldAdvanceStep(const FBossPatternContext& Context) const
{
	(void)Context;

	switch (CurrentStep)
	{
	case EBossPatternStep::Windup:
		return StepElapsed >= WindupDuration;
	case EBossPatternStep::Task1:
		return bClawArcsSpawned;
	case EBossPatternStep::Recovery:
		return StepElapsed >= RecoveryDuration;
	default:
		return false;
	}
}

EBossPatternStep UBossPattern_RendingClawArc::GetNextStep(EBossPatternStep Step) const
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

void UBossPattern_RendingClawArc::SpawnClawArcs(const FBossPatternContext& Context)
{
	if (bClawArcsSpawned || !Context.BulletHell)
	{
		bClawArcsSpawned = true;
		return;
	}

	const FVector AimDirection = SafeDirection(Context.DirectionToTarget, Context.BossForward);
	const FVector PlaneNormal = MakePerpendicularDirection(Context.BossRight, AimDirection);
	const FVector ArcUp = SafeDirection(PlaneNormal.Cross(AimDirection), Context.BossUp);
	const FVector Center = Context.BossLocation;
	const int32 SafePlaneCount = (std::max)(1, PlaneCount);
	const int32 SafeProjectilesPerArc = (std::max)(1, ProjectilesPerArc);
	const float SafeSphereRadius = (std::max)(0.0f, SphereRadius);
	const float SafePlaneSpacing = (std::max)(0.0f, PlaneSpacing);
	const float ArcRadians = (std::max)(0.0f, (std::min)(360.0f, ArcCenterAngleDegrees)) * (Pi / 180.0f);
	const float FirstPlaneOffset = -0.5f * static_cast<float>(SafePlaneCount - 1) * SafePlaneSpacing;

	for (int32 PlaneIndex = 0; PlaneIndex < SafePlaneCount; ++PlaneIndex)
	{
		const float PlaneOffset = FirstPlaneOffset + static_cast<float>(PlaneIndex) * SafePlaneSpacing;
		const float AbsPlaneOffset = std::fabs(PlaneOffset);
		if (AbsPlaneOffset > SafeSphereRadius)
		{
			continue;
		}

		const float CircleRadiusSquared = SafeSphereRadius * SafeSphereRadius - PlaneOffset * PlaneOffset;
		const float CircleRadius = std::sqrt((std::max)(0.0f, CircleRadiusSquared));
		const FVector CircleCenter = Center + PlaneNormal * PlaneOffset;
		++UsedPlaneCount;

		for (int32 ProjectileIndex = 0; ProjectileIndex < SafeProjectilesPerArc; ++ProjectileIndex)
		{
			const float T = SafeProjectilesPerArc > 1
				? static_cast<float>(ProjectileIndex) / static_cast<float>(SafeProjectilesPerArc - 1)
				: 0.5f;
			const float Angle = (T - 0.5f) * ArcRadians;
			const FVector Position =
				CircleCenter
				+ AimDirection * (std::cos(Angle) * CircleRadius)
				+ ArcUp * (std::sin(Angle) * CircleRadius);

			Context.BulletHell->SpawnBullet(MakeBulletParams(Position, AimDirection));
			++SpawnedProjectileCount;
		}
	}

	bClawArcsSpawned = true;
}

FBulletSpawnParams UBossPattern_RendingClawArc::MakeBulletParams(const FVector& Position, const FVector& Direction) const
{
	FBulletSpawnParams Params;
	const FVector LaunchDirection = SafeDirection(Direction, FVector::ForwardVector);
	Params.Position = Position;
	Params.Velocity = LaunchDirection * ProjectileSpeed;
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
