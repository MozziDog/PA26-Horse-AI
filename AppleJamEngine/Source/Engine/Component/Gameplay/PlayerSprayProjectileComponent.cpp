#include "Component/Gameplay/PlayerSprayProjectileComponent.h"

#include "Audio/AudioManager.h"
#include "Component/Gameplay/BulletHellDamageReceiverComponent.h"
#include "Component/Gameplay/BulletTrailComponent.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Component/Primitive/InstancedStaticMeshComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Core/ScoreManager.h"
#include "Core/Types/RayTypes.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/BossCharacter.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/World.h"
#include "Math/Rotator.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetPreviewUtils.h"
#include "Particle/ParticleSystemManager.h"
#include "Render/Types/MinimalViewInfo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
	constexpr float Pi = 3.1415926535f;
	constexpr const char* PlayerTagName = "Player";
	constexpr const char* BossTagName = "Boss";
	constexpr const char* PlayerSprayDeathEffectComponentPrefix = "PlayerSprayDeathEffect";

	float ClampFloat(float Value, float MinValue, float MaxValue)
	{
		return (std::max)(MinValue, (std::min)(MaxValue, Value));
	}

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		return Direction.IsNearlyZero() ? Fallback : Direction.Normalized();
	}

	AActor* ResolveHitActor(const FHitResult& Hit)
	{
		if (Hit.HitActor)
		{
			return Hit.HitActor;
		}
		return Hit.HitComponent ? Hit.HitComponent->GetOwner() : nullptr;
	}

	bool HasActorTag(const AActor* Actor, const char* TagName)
	{
		return Actor && TagName && Actor->HasTag(FName(TagName));
	}

	FTransform ComposePhysicsAssetHitTransforms(const FTransform& ParentWorld, const FTransform& Local)
	{
		FTransform Result = Local;
		Result.Location = ParentWorld.Location + ParentWorld.Rotation.RotateVector(Local.Location);
		Result.Rotation = (ParentWorld.Rotation * Local.Rotation).GetNormalized();
		Result.Scale = FVector::OneVector;
		return Result;
	}

	FVector TransformPointToShapeLocal(const FVector& WorldPoint, const FTransform& ShapeWorld)
	{
		const FQuat InverseRotation = ShapeWorld.Rotation.GetNormalized().Inverse();
		return InverseRotation.RotateVector(WorldPoint - ShapeWorld.Location);
	}

	float SegmentPointDistanceSquared(const FVector& A, const FVector& B, const FVector& Point, float& OutAlpha)
	{
		const FVector AB = B - A;
		const float LenSq = AB.Dot(AB);
		if (LenSq <= 1.0e-8f)
		{
			OutAlpha = 0.0f;
			return FVector::DistSquared(A, Point);
		}

		OutAlpha = ClampFloat((Point - A).Dot(AB) / LenSq, 0.0f, 1.0f);
		const FVector Closest = A + AB * OutAlpha;
		return FVector::DistSquared(Closest, Point);
	}

	float SegmentSegmentDistanceSquared(
		const FVector& P1,
		const FVector& Q1,
		const FVector& P2,
		const FVector& Q2,
		float& OutAlpha1)
	{
		const FVector D1 = Q1 - P1;
		const FVector D2 = Q2 - P2;
		const FVector R = P1 - P2;
		const float A = D1.Dot(D1);
		const float E = D2.Dot(D2);
		const float F = D2.Dot(R);

		float S = 0.0f;
		float T = 0.0f;
		if (A <= 1.0e-8f && E <= 1.0e-8f)
		{
			OutAlpha1 = 0.0f;
			return FVector::DistSquared(P1, P2);
		}
		if (A <= 1.0e-8f)
		{
			T = ClampFloat(F / E, 0.0f, 1.0f);
		}
		else
		{
			const float C = D1.Dot(R);
			if (E <= 1.0e-8f)
			{
				S = ClampFloat(-C / A, 0.0f, 1.0f);
			}
			else
			{
				const float B = D1.Dot(D2);
				const float Denom = A * E - B * B;
				S = Denom != 0.0f ? ClampFloat((B * F - C * E) / Denom, 0.0f, 1.0f) : 0.0f;
				T = (B * S + F) / E;
				if (T < 0.0f)
				{
					T = 0.0f;
					S = ClampFloat(-C / A, 0.0f, 1.0f);
				}
				else if (T > 1.0f)
				{
					T = 1.0f;
					S = ClampFloat((B - C) / A, 0.0f, 1.0f);
				}
			}
		}

		OutAlpha1 = S;
		const FVector C1 = P1 + D1 * S;
		const FVector C2 = P2 + D2 * T;
		return FVector::DistSquared(C1, C2);
	}

	bool SegmentIntersectsExpandedLocalBox(
		const FVector& Start,
		const FVector& End,
		const FVector& HalfExtent,
		float Radius,
		float& OutAlpha)
	{
		const FVector D = End - Start;
		const FVector Expanded = HalfExtent + Radius;
		float TMin = 0.0f;
		float TMax = 1.0f;
		const float* Origin = &Start.X;
		const float* Direction = &D.X;
		const float MinBounds[3] = { -Expanded.X, -Expanded.Y, -Expanded.Z };
		const float MaxBounds[3] = {  Expanded.X,  Expanded.Y,  Expanded.Z };

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (std::fabs(Direction[Axis]) < 1.0e-6f)
			{
				if (Origin[Axis] < MinBounds[Axis] || Origin[Axis] > MaxBounds[Axis])
				{
					return false;
				}
				continue;
			}

			float T1 = (MinBounds[Axis] - Origin[Axis]) / Direction[Axis];
			float T2 = (MaxBounds[Axis] - Origin[Axis]) / Direction[Axis];
			if (T1 > T2)
			{
				std::swap(T1, T2);
			}
			TMin = (std::max)(TMin, T1);
			TMax = (std::min)(TMax, T2);
			if (TMin > TMax)
			{
				return false;
			}
		}

		OutAlpha = TMin;
		return true;
	}

	bool SegmentIntersectsPhysicsAssetShape(
		const FVector& SegmentStartWorld,
		const FVector& SegmentEndWorld,
		float ProjectileRadius,
		const FTransform& ShapeWorld,
		const FPhysicsAssetShapeSetup& Shape,
		float& OutAlpha)
	{
		const FVector LocalStart = TransformPointToShapeLocal(SegmentStartWorld, ShapeWorld);
		const FVector LocalEnd = TransformPointToShapeLocal(SegmentEndWorld, ShapeWorld);
		switch (Shape.Type)
		{
		case EPhysicsAssetShapeType::Box:
			return SegmentIntersectsExpandedLocalBox(
				LocalStart,
				LocalEnd,
				Shape.BoxHalfExtent,
				ProjectileRadius,
				OutAlpha);
		case EPhysicsAssetShapeType::Sphere:
		{
			const float Radius = (std::max)(0.001f, Shape.SphereRadius + ProjectileRadius);
			float Alpha = 0.0f;
			if (SegmentPointDistanceSquared(LocalStart, LocalEnd, FVector::ZeroVector, Alpha) <= Radius * Radius)
			{
				OutAlpha = Alpha;
				return true;
			}
			return false;
		}
		case EPhysicsAssetShapeType::Capsule:
		{
			const float CapsuleRadius = (std::max)(0.001f, Shape.CapsuleRadius + ProjectileRadius);
			const float HalfHeight = (std::max)(Shape.CapsuleHalfHeight, Shape.CapsuleRadius);
			const float CylinderHalf = (std::max)(0.0f, HalfHeight - Shape.CapsuleRadius);
			const FVector CapsuleA(0.0f, 0.0f, -CylinderHalf);
			const FVector CapsuleB(0.0f, 0.0f, CylinderHalf);
			float Alpha = 0.0f;
			if (SegmentSegmentDistanceSquared(LocalStart, LocalEnd, CapsuleA, CapsuleB, Alpha) <= CapsuleRadius * CapsuleRadius)
			{
				OutAlpha = Alpha;
				return true;
			}
			return false;
		}
		default:
			return false;
		}
	}

	void MakeBasis(const FVector& Forward, FVector& OutRight, FVector& OutUp)
	{
		const FVector F = SafeDirection(Forward, FVector::ForwardVector);
		const FVector Reference = std::fabs(F.Z) < 0.95f ? FVector::UpVector : FVector::RightVector;
		OutRight = SafeDirection(Reference.Cross(F), FVector::RightVector);
		OutUp = SafeDirection(F.Cross(OutRight), FVector::UpVector);
	}
}

UPlayerSprayProjectileComponent::UPlayerSprayProjectileComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPlayerSprayProjectileComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	EnsureRenderComponent();
	EnsureTrailComponent();
}

void UPlayerSprayProjectileComponent::StartAttack()
{
	bAttackHeld = true;
	FireAccumulator = 0.0f;
	TryFireBurst();
	UE_LOG("[PlayerSpray] StartAttack owner=%s comp=%s fireRate=%.3f perBurst=%d aimDist=%.3f spawnOffset=(%.3f,%.3f) cone=%.3f radius=%.3f renderScale=%.3f mesh=%s material=%s",
		GetOwner() ? GetOwner()->GetName().c_str() : "nil",
		GetName().c_str(),
		FireRate,
		ProjectilesPerBurst,
		AimRayDistance,
		SpawnForwardOffset,
		SpawnUpOffset,
		ConeHalfAngleDegrees,
		ProjectileRadius,
		RenderScale,
		MeshPath.c_str(),
		MaterialPath.c_str());
}

void UPlayerSprayProjectileComponent::StopAttack()
{
	bAttackHeld = false;
	FireAccumulator = 0.0f;
	UE_LOG("[PlayerSpray] StopAttack activeProjectiles=%d", static_cast<int32>(Projectiles.size()));
}

void UPlayerSprayProjectileComponent::ClearProjectiles()
{
	Projectiles.clear();
	ClearRender();
	SyncTrail();
}

void UPlayerSprayProjectileComponent::AddUltimateGauge(float Amount)
{
	if (Amount <= 0.0f || UltimateGaugeMax <= 0.0f)
	{
		return;
	}

	const float PreviousGauge = UltimateGauge;
	UltimateGauge = ClampFloat(UltimateGauge + Amount, 0.0f, UltimateGaugeMax);
	UE_LOG("[PlayerSprayUltimate] gauge increased owner=%s amount=%.3f gauge=%.3f/%.3f ready=%d",
		GetOwner() ? GetOwner()->GetName().c_str() : "nil",
		UltimateGauge - PreviousGauge,
		UltimateGauge,
		UltimateGaugeMax,
		IsUltimateReady() ? 1 : 0);
}

void UPlayerSprayProjectileComponent::ResetUltimateGauge()
{
	UltimateGauge = 0.0f;
	UE_LOG("[PlayerSprayUltimate] gauge reset owner=%s gauge=%.3f/%.3f",
		GetOwner() ? GetOwner()->GetName().c_str() : "nil",
		UltimateGauge,
		UltimateGaugeMax);
}

void UPlayerSprayProjectileComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	ResetDeathEffectFrameCounters();
	TickAttack(DeltaTime);
	TickProjectiles(DeltaTime);
	SyncRender();
	SyncTrail();
}

void UPlayerSprayProjectileComponent::TickAttack(float DeltaTime)
{
	if (!bAttackHeld || DeltaTime <= 0.0f)
	{
		return;
	}

	const float Interval = FireRate > 0.0f ? 1.0f / FireRate : 0.0f;
	if (Interval <= 0.0f)
	{
		TryFireBurst();
		return;
	}

	FireAccumulator += DeltaTime;
	while (FireAccumulator >= Interval)
	{
		FireAccumulator -= Interval;
		TryFireBurst();
	}
}

void UPlayerSprayProjectileComponent::TryFireBurst()
{
	FVector CameraLocation;
	FVector CameraForward;
	AActor* BossActor = nullptr;
	if (!FindCameraBossTarget(CameraLocation, CameraForward, BossActor))
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	const FVector OwnerLocation = OwnerActor ? OwnerActor->GetActorLocation() : CameraLocation;
	const FVector SpawnOrigin = OwnerLocation
		+ SafeDirection(CameraForward, FVector::ForwardVector) * SpawnForwardOffset
		+ FVector::UpVector * SpawnUpOffset;
	const int32 Count = (std::max)(1, ProjectilesPerBurst);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		SpawnProjectile(SpawnOrigin, CameraForward, BossActor, Index, Count);
	}

	UE_LOG("[PlayerSpray] Burst aimTarget=%s count=%d active=%d",
		BossActor ? BossActor->GetName().c_str() : "nil",
		Count,
		static_cast<int32>(Projectiles.size()));
}

bool UPlayerSprayProjectileComponent::FindCameraBossTarget(
	FVector& OutCameraLocation,
	FVector& OutCameraForward,
	AActor*& OutBossActor)
{
	OutCameraLocation = FVector::ZeroVector;
	OutCameraForward = FVector::ForwardVector;
	OutBossActor = nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FMinimalViewInfo POV;
	if (!World->GetActivePOV(POV))
	{
		return false;
	}

	OutCameraLocation = POV.Location;
	OutCameraForward = SafeDirection(POV.Rotation.GetForwardVector(), FVector::ForwardVector);
	const FVector End = OutCameraLocation + OutCameraForward * AimRayDistance;

	FHitResult Hit;
	const uint32 Mask =
		ObjectTypeBit(ECollisionChannel::WorldStatic)
		| ObjectTypeBit(ECollisionChannel::WorldDynamic)
		| ObjectTypeBit(ECollisionChannel::Pawn);
	const AActor* IgnoreActor = GetOwner();
	bool bHit = World->PhysicsRaycastByObjectTypes(
		OutCameraLocation,
		OutCameraForward,
		AimRayDistance,
		Hit,
		Mask,
		IgnoreActor);
	bool bVisualFallbackHit = false;
	if (!bHit || !IsBossActor(Hit.HitActor))
	{
		AActor* VisualBossActor = nullptr;
		FHitResult VisualHit;
		if (RaycastBossVisualFallback(
			World,
			OutCameraLocation,
			OutCameraForward,
			AimRayDistance,
			VisualHit,
			VisualBossActor))
		{
			Hit = VisualHit;
			Hit.HitActor = VisualBossActor;
			bHit = true;
			bVisualFallbackHit = true;
		}
	}

	const FVector DebugEnd = bHit ? Hit.WorldHitLocation : End;
	if (bDrawAimRay)
	{
		DrawDebugLine(World, OutCameraLocation, End, FColor(0, 180, 255), AimRayDebugDuration);
		DrawDebugLine(World, OutCameraLocation, DebugEnd, bHit && IsBossActor(Hit.HitActor) ? FColor::Green() : FColor::Red(), AimRayDebugDuration);
	}

	if (!bHit || !IsBossActor(Hit.HitActor))
	{
		UE_LOG("[PlayerSpray] Aim ray miss boss hit=%d actor=%s visualFallback=%d povLoc=(%.3f,%.3f,%.3f) povRot=(%.3f,%.3f,%.3f) forward=(%.3f,%.3f,%.3f)",
			(int)bHit,
			Hit.HitActor ? Hit.HitActor->GetName().c_str() : "nil",
			(int)bVisualFallbackHit,
			OutCameraLocation.X,
			OutCameraLocation.Y,
			OutCameraLocation.Z,
			POV.Rotation.Pitch,
			POV.Rotation.Yaw,
			POV.Rotation.Roll,
			OutCameraForward.X,
			OutCameraForward.Y,
			OutCameraForward.Z);
		return true;
	}

	OutBossActor = Hit.HitActor;
	UE_LOG("[PlayerSpray] Aim ray hit boss actor=%s hitLoc=(%.3f,%.3f,%.3f) distance=%.3f visualFallback=%d povLoc=(%.3f,%.3f,%.3f) povRot=(%.3f,%.3f,%.3f) forward=(%.3f,%.3f,%.3f)",
		OutBossActor ? OutBossActor->GetName().c_str() : "nil",
		Hit.WorldHitLocation.X,
		Hit.WorldHitLocation.Y,
		Hit.WorldHitLocation.Z,
		Hit.Distance,
		(int)bVisualFallbackHit,
		OutCameraLocation.X,
		OutCameraLocation.Y,
		OutCameraLocation.Z,
		POV.Rotation.Pitch,
		POV.Rotation.Yaw,
		POV.Rotation.Roll,
		OutCameraForward.X,
		OutCameraForward.Y,
		OutCameraForward.Z);
	return true;
}

bool UPlayerSprayProjectileComponent::IsBossActor(const AActor* Candidate) const
{
	if (!Candidate)
	{
		return false;
	}

	if (HasActorTag(Candidate, BossTagName))
	{
		return true;
	}

	if (Cast<ABossCharacter>(Candidate))
	{
		return true;
	}

	const FString Name = Candidate->GetName();
	return Name.find("Boss") != FString::npos || Name.find("boss") != FString::npos;
}

bool UPlayerSprayProjectileComponent::RaycastBossVisualFallback(
	UWorld* World,
	const FVector& Start,
	const FVector& Direction,
	float MaxDistance,
	FHitResult& OutHit,
	AActor*& OutBossActor) const
{
	OutHit = FHitResult();
	OutBossActor = nullptr;
	if (!World || MaxDistance <= 0.0f)
	{
		return false;
	}

	float BestDistance = (std::numeric_limits<float>::max)();
	FHitResult BestHit;
	AActor* BestActor = nullptr;
	FRay Ray;
	Ray.Origin = Start;
	Ray.Direction = Direction;

	for (AActor* Candidate : World->GetActors())
	{
		if (!IsValid(Candidate) || Candidate == GetOwner() || !IsBossActor(Candidate))
		{
			continue;
		}

		for (UActorComponent* Component : Candidate->GetComponents())
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive || !Primitive->IsVisible())
			{
				continue;
			}

			FHitResult ComponentHit;
			if (Primitive->LineTraceComponent(Ray, ComponentHit))
			{
				const float Distance = (ComponentHit.WorldHitLocation - Start).Length();
				if (Distance >= 0.0f && Distance <= MaxDistance && Distance < BestDistance)
				{
					BestDistance = Distance;
					BestHit = ComponentHit;
					BestHit.HitActor = Candidate;
					BestHit.HitComponent = Primitive;
					BestHit.Distance = Distance;
					BestActor = Candidate;
				}
				continue;
			}

			float BoxDistance = 0.0f;
			if (RayIntersectsBox(Start, Direction, MaxDistance, Primitive->GetWorldBoundingBox(), BoxDistance)
				&& BoxDistance < BestDistance)
			{
				BestDistance = BoxDistance;
				BestHit = FHitResult();
				BestHit.bHit = true;
				BestHit.HitActor = Candidate;
				BestHit.HitComponent = Primitive;
				BestHit.Distance = BoxDistance;
				BestHit.WorldHitLocation = Start + Direction * BoxDistance;
				BestHit.WorldNormal = (BestHit.WorldHitLocation - Candidate->GetActorLocation()).Normalized();
				BestHit.ImpactNormal = BestHit.WorldNormal;
				BestActor = Candidate;
			}
		}
	}

	if (!BestActor)
	{
		return false;
	}

	OutHit = BestHit;
	OutBossActor = BestActor;
	return true;
}

bool UPlayerSprayProjectileComponent::RayIntersectsBox(
	const FVector& Start,
	const FVector& Direction,
	float MaxDistance,
	const FBoundingBox& Bounds,
	float& OutDistance) const
{
	OutDistance = 0.0f;
	if (!Bounds.IsValid() || MaxDistance <= 0.0f)
	{
		return false;
	}

	float TMin = 0.0f;
	float TMax = MaxDistance;

	const auto TestAxis = [&TMin, &TMax](float StartValue, float DirectionValue, float MinValue, float MaxValue) -> bool
	{
		if (std::fabs(DirectionValue) < 1.0e-6f)
		{
			return StartValue >= MinValue && StartValue <= MaxValue;
		}

		float T1 = (MinValue - StartValue) / DirectionValue;
		float T2 = (MaxValue - StartValue) / DirectionValue;
		if (T1 > T2)
		{
			std::swap(T1, T2);
		}
		TMin = (std::max)(TMin, T1);
		TMax = (std::min)(TMax, T2);
		return TMin <= TMax;
	};

	if (!TestAxis(Start.X, Direction.X, Bounds.Min.X, Bounds.Max.X)) return false;
	if (!TestAxis(Start.Y, Direction.Y, Bounds.Min.Y, Bounds.Max.Y)) return false;
	if (!TestAxis(Start.Z, Direction.Z, Bounds.Min.Z, Bounds.Max.Z)) return false;

	OutDistance = TMin >= 0.0f ? TMin : TMax;
	return OutDistance >= 0.0f && OutDistance <= MaxDistance;
}

void UPlayerSprayProjectileComponent::SpawnProjectile(
	const FVector& Origin,
	const FVector& CameraForward,
	AActor* TargetActor,
	int32 BurstIndex,
	int32 BurstCount)
{
	FPlayerSprayProjectile Projectile;
	Projectile.Position = Origin;
	Projectile.PreviousPosition = Origin;
	const FVector Direction = BuildSpreadDirection(CameraForward, BurstIndex, BurstCount);
	Projectile.Velocity = Direction * InitialSpeed;
	Projectile.HomingTarget = TargetActor;
	Projectile.Lifetime = (std::max)(0.01f, ProjectileLifetime);
	Projectile.Radius = (std::max)(0.001f, ProjectileRadius);
	Projectile.Damage = (std::max)(0.0f, ProjectileDamage);
	Projectile.DeathEffect.bEnableDeathEffect = bDeathEffectEnabled;
	Projectile.DeathEffect.ParticleSystemPath = DeathEffectPath;
	Projectile.DeathEffect.EventName = DeathEffectEventName;
	Projectile.DeathEffect.bInheritProjectileVelocity = bDeathEffectInheritVelocity;
	Projectile.DeathEffect.VelocityScale = DeathEffectVelocityScale;
	Projectile.TrailSamples.push_back(Origin);
	Projectiles.push_back(Projectile);

	// 발사체 1발 = 명중률 분모 1 증가.
	FScoreManager::Get().AddShotFired();
}

void UPlayerSprayProjectileComponent::TickProjectiles(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	for (int32 Index = 0; Index < static_cast<int32>(Projectiles.size());)
	{
		FPlayerSprayProjectile& Projectile = Projectiles[Index];
		Projectile.Age += DeltaTime;
		Projectile.ScatterAge += DeltaTime;

		if (!Projectile.bHoming && Projectile.ScatterAge >= ScatterDuration)
		{
			Projectile.bHoming = true;
			if (HomingSpeed > 0.0f)
			{
				Projectile.Velocity = SafeDirection(Projectile.Velocity, FVector::ForwardVector) * HomingSpeed;
			}
		}

		UpdateHoming(Projectile, DeltaTime);
		Projectile.PreviousPosition = Projectile.Position;
		Projectile.Position += Projectile.Velocity * DeltaTime;

		const bool bHit = CheckProjectileCollision(Projectile);
		const bool bExpired = Projectile.Age >= Projectile.Lifetime;
		if (bHit || bExpired)
		{
			RemoveProjectileAtIndex(Index, bHit);
			continue;
		}

		Projectile.TrailSampleAccumulator += DeltaTime;
		const float SampleInterval = TrailMaxSamples > 1 ? TrailLifetime / static_cast<float>(TrailMaxSamples - 1) : TrailLifetime;
		if (Projectile.TrailSamples.empty() || Projectile.TrailSampleAccumulator >= (std::max)(0.005f, SampleInterval))
		{
			Projectile.TrailSampleAccumulator = 0.0f;
			Projectile.TrailSamples.push_back(Projectile.Position);
			while (static_cast<int32>(Projectile.TrailSamples.size()) > (std::max)(2, TrailMaxSamples))
			{
				Projectile.TrailSamples.erase(Projectile.TrailSamples.begin());
			}
		}

		++Index;
	}
}

void UPlayerSprayProjectileComponent::UpdateHoming(FPlayerSprayProjectile& Projectile, float DeltaTime)
{
	if (!Projectile.bHoming || DeltaTime <= 0.0f)
	{
		return;
	}

	UpdateHomingTargetPoint(Projectile, DeltaTime);
	if (!Projectile.bHasHomingTargetPoint)
	{
		return;
	}

	const FVector DesiredDirection = SafeDirection(Projectile.HomingTargetPoint - Projectile.Position, Projectile.Velocity);
	const float CurrentSpeed = Projectile.Velocity.Length();
	if (CurrentSpeed <= 0.0f)
	{
		return;
	}

	const FVector CurrentDirection = SafeDirection(Projectile.Velocity, DesiredDirection);
	const float Dot = ClampFloat(CurrentDirection.Dot(DesiredDirection), -1.0f, 1.0f);
	const float AngleRadians = std::acos(Dot);
	if (AngleRadians <= 0.0001f)
	{
		Projectile.Velocity = DesiredDirection * CurrentSpeed;
		return;
	}

	const float MaxTurnRadians = (std::max)(0.0f, HomingMaxTurnRateDegrees) * (Pi / 180.0f) * DeltaTime;
	const float TurnAlpha = MaxTurnRadians > 0.0f ? ClampFloat(MaxTurnRadians / AngleRadians, 0.0f, 1.0f) : 1.0f;
	const float StrengthAlpha = ClampFloat((std::max)(0.0f, HomingStrength) * DeltaTime, 0.0f, 1.0f);
	const float Alpha = (std::min)(TurnAlpha, StrengthAlpha);
	const FVector NewDirection = SafeDirection(CurrentDirection * (1.0f - Alpha) + DesiredDirection * Alpha, DesiredDirection);
	Projectile.Velocity = NewDirection * (HomingSpeed > 0.0f ? HomingSpeed : CurrentSpeed);
}

bool UPlayerSprayProjectileComponent::UpdateHomingTargetPoint(FPlayerSprayProjectile& Projectile, float DeltaTime)
{
	const float CurrentSpeed = Projectile.Velocity.Length();
	if (CurrentSpeed <= 0.0f)
	{
		Projectile.bHasHomingTargetPoint = false;
		Projectile.HomingTargetMemoryTime = 0.0f;
		return false;
	}

	const FVector SensorDirection = SafeDirection(Projectile.Velocity, FVector::ForwardVector);
	const float LookAheadDistance = (std::max)(Projectile.Radius * 2.0f, CurrentSpeed * (std::max)(0.0f, HomingLookAheadTime));
	const FVector SensorEnd = Projectile.Position + SensorDirection * LookAheadDistance;
	const float SensorRadius = Projectile.Radius + (std::max)(0.0f, HomingSensorRadius);

	FHitResult SensorHit;
	if (FindBossPhysicsAssetHit(Projectile.Position, SensorEnd, SensorRadius, SensorHit))
	{
		Projectile.HomingTarget = ResolveHitActor(SensorHit);
		Projectile.HomingTargetPoint = SensorHit.WorldHitLocation;
		Projectile.HomingTargetMemoryTime = (std::max)(0.0f, HomingTargetMemoryDuration);
		Projectile.bHasHomingTargetPoint = true;
		return true;
	}

	if (Projectile.HomingTargetMemoryTime > 0.0f)
	{
		Projectile.HomingTargetMemoryTime = (std::max)(0.0f, Projectile.HomingTargetMemoryTime - DeltaTime);
		if (Projectile.HomingTargetMemoryTime > 0.0f)
		{
			return Projectile.bHasHomingTargetPoint;
		}
	}

	Projectile.HomingTarget.Reset();
	Projectile.bHasHomingTargetPoint = false;
	return false;
}

bool UPlayerSprayProjectileComponent::CheckProjectileCollision(const FPlayerSprayProjectile& Projectile)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FHitResult Hit;
	const uint32 Mask =
		ObjectTypeBit(ECollisionChannel::WorldStatic)
		| ObjectTypeBit(ECollisionChannel::WorldDynamic)
		| ObjectTypeBit(ECollisionChannel::Pawn)
		| ObjectTypeBit(ECollisionChannel::Trigger);
	const bool bHit = World->PhysicsSweepByObjectTypes(
		Projectile.PreviousPosition,
		Projectile.Position,
		FQuat::Identity,
		FCollisionShape::MakeSphere(Projectile.Radius),
		Hit,
		Mask,
		GetOwner());
	if (bHit)
	{
		AActor* TargetActor = ResolveHitActor(Hit);
		if (TargetActor == GetOwner() || HasActorTag(TargetActor, PlayerTagName))
		{
			return false;
		}
		if (IsBossActor(TargetActor))
		{
			FHitResult PhysicsAssetHit;
			if (!CheckBossPhysicsAssetHit(Projectile, TargetActor, PhysicsAssetHit))
			{
				return false;
			}
			ApplyDamageToHitTarget(Projectile, PhysicsAssetHit);
			return true;
		}
		ApplyDamageToHitTarget(Projectile, Hit);
		return true;
	}

	AActor* HomingTarget = Projectile.HomingTarget.Get();
	if (IsBossActor(HomingTarget))
	{
		FHitResult PhysicsAssetHit;
		if (CheckBossPhysicsAssetHit(Projectile, HomingTarget, PhysicsAssetHit))
		{
			ApplyDamageToHitTarget(Projectile, PhysicsAssetHit);
			return true;
		}
	}

	FHitResult PhysicsAssetHit;
	if (FindBossPhysicsAssetHit(Projectile.PreviousPosition, Projectile.Position, Projectile.Radius, PhysicsAssetHit))
	{
		ApplyDamageToHitTarget(Projectile, PhysicsAssetHit);
		return true;
	}

	return false;
}

bool UPlayerSprayProjectileComponent::CheckBossPhysicsAssetHit(
	const FPlayerSprayProjectile& Projectile,
	AActor* BossActor,
	FHitResult& OutHit) const
{
	return CheckBossPhysicsAssetHit(
		BossActor,
		Projectile.PreviousPosition,
		Projectile.Position,
		Projectile.Radius,
		OutHit);
}

bool UPlayerSprayProjectileComponent::CheckBossPhysicsAssetHit(
	AActor* BossActor,
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	float SweepRadius,
	FHitResult& OutHit) const
{
	OutHit = FHitResult();
	if (!BossActor || BossActor == GetOwner())
	{
		return false;
	}

	USkeletalMeshComponent* MeshComponent = BossActor->GetComponentByClass<USkeletalMeshComponent>();
	UPhysicsAsset* PhysicsAsset = MeshComponent ? MeshComponent->GetEffectivePhysicsAsset() : nullptr;
	if (!MeshComponent || !PhysicsAsset)
	{
		return false;
	}

	FPhysicsAssetPreviewPoseCache PoseCache;
	if (!PoseCache.Initialize(MeshComponent, PhysicsAsset))
	{
		return false;
	}

	const TArray<FPhysicsAssetBodySetup>& Bodies = PhysicsAsset->GetBodySetups();
	float BestAlpha = (std::numeric_limits<float>::max)();
	for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(Bodies.size()); ++BodyIndex)
	{
		FTransform BodyWorld;
		if (!PoseCache.ComputeBodyWorldTransform(BodyIndex, BodyWorld))
		{
			continue;
		}

		const FPhysicsAssetBodySetup& Body = Bodies[BodyIndex];
		for (const FPhysicsAssetShapeSetup& Shape : Body.Shapes)
		{
			const FTransform ShapeWorld = ComposePhysicsAssetHitTransforms(BodyWorld, Shape.LocalTransform);
			float Alpha = 0.0f;
			if (SegmentIntersectsPhysicsAssetShape(
				SegmentStart,
				SegmentEnd,
				SweepRadius,
				ShapeWorld,
				Shape,
				Alpha))
			{
				BestAlpha = (std::min)(BestAlpha, Alpha);
			}
		}
	}

	if (BestAlpha == (std::numeric_limits<float>::max)())
	{
		return false;
	}

	OutHit.bHit = true;
	OutHit.HitActor = BossActor;
	OutHit.HitComponent = MeshComponent;
	OutHit.Distance = (SegmentEnd - SegmentStart).Length() * BestAlpha;
	OutHit.WorldHitLocation = SegmentStart + (SegmentEnd - SegmentStart) * BestAlpha;
	OutHit.WorldNormal = SafeDirection(OutHit.WorldHitLocation - BossActor->GetActorLocation(), FVector::UpVector);
	OutHit.ImpactNormal = OutHit.WorldNormal;
	return true;
}

bool UPlayerSprayProjectileComponent::FindBossPhysicsAssetHit(
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	float SweepRadius,
	FHitResult& OutHit) const
{
	OutHit = FHitResult();
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	float BestDistance = (std::numeric_limits<float>::max)();
	FHitResult BestHit;
	for (AActor* Candidate : World->GetActors())
	{
		if (!IsValid(Candidate) || Candidate == GetOwner() || HasActorTag(Candidate, PlayerTagName) || !IsBossActor(Candidate))
		{
			continue;
		}

		FHitResult CandidateHit;
		if (CheckBossPhysicsAssetHit(Candidate, SegmentStart, SegmentEnd, SweepRadius, CandidateHit)
			&& CandidateHit.Distance < BestDistance)
		{
			BestDistance = CandidateHit.Distance;
			BestHit = CandidateHit;
		}
	}

	if (!BestHit.bHit)
	{
		return false;
	}

	OutHit = BestHit;
	return true;
}

void UPlayerSprayProjectileComponent::ApplyDamageToHitTarget(
	const FPlayerSprayProjectile& Projectile,
	const FHitResult& Hit)
{
	if (Projectile.Damage <= 0.0f)
	{
		return;
	}

	AActor* TargetActor = ResolveHitActor(Hit);
	if (!TargetActor || TargetActor == GetOwner() || HasActorTag(TargetActor, PlayerTagName))
	{
		return;
	}

	if (UBulletHellDamageReceiverComponent* DamageReceiver = TargetActor->GetComponentByClass<UBulletHellDamageReceiverComponent>())
	{
		const float AppliedDamage = DamageReceiver->ApplyDamageFromSource(Projectile.Damage, EBossDamageSource::Spray);
		if (AppliedDamage > 0.0f && IsBossActor(TargetActor))
		{
			FAudioManager::Get().PlayAudio("Hit", 1.0f);
			AddUltimateGauge(UltimateGaugePerBossHit);
			FScoreManager::Get().AddBossHit();  // 보스 유효타격 = 명중률 분자 1 증가
		}
	}
}

void UPlayerSprayProjectileComponent::RemoveProjectileAtIndex(int32 ProjectileIndex, bool bEmitDeathEffect)
{
	if (ProjectileIndex < 0 || ProjectileIndex >= static_cast<int32>(Projectiles.size()))
	{
		return;
	}

	if (bEmitDeathEffect)
	{
		EmitProjectileDeathEffect(Projectiles[ProjectileIndex]);
	}

	const int32 LastIndex = static_cast<int32>(Projectiles.size()) - 1;
	if (ProjectileIndex != LastIndex)
	{
		Projectiles[ProjectileIndex] = Projectiles[LastIndex];
	}
	Projectiles.pop_back();
}

UParticleSystemComponent* UPlayerSprayProjectileComponent::FindOrCreateDeathEffectComponent(const FString& ParticleSystemPath)
{
	if (ParticleSystemPath.empty() || ParticleSystemPath == "None")
	{
		++DeathEffectDroppedThisFrame;
		return nullptr;
	}

	for (FPlayerSprayDeathEffectRuntimeSlot& Slot : DeathEffectSlots)
	{
		if (Slot.ParticleSystemPath != ParticleSystemPath)
		{
			continue;
		}

		if (UParticleSystemComponent* ExistingComponent = GetDeathEffectComponent(Slot))
		{
			return ExistingComponent;
		}
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !CanAutoCreateRuntimeHelperComponent())
	{
		return nullptr;
	}

	if (MaxDeathEffectComponents <= 0 ||
		CountValidDeathEffectComponents() >= MaxDeathEffectComponents)
	{
		return nullptr;
	}

	UParticleSystem* Template = FParticleSystemManager::Get().Load(ParticleSystemPath);
	if (!Template)
	{
		++DeathEffectDroppedThisFrame;
		return nullptr;
	}

	const int32 SlotIndex = static_cast<int32>(DeathEffectSlots.size());
	char NameBuffer[96];
	std::snprintf(NameBuffer, sizeof(NameBuffer), "%s%d", PlayerSprayDeathEffectComponentPrefix, SlotIndex);
	const FName ExpectedName(NameBuffer);

	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		UParticleSystemComponent* ExistingComponent = Cast<UParticleSystemComponent>(Component);
		if (ExistingComponent && ExistingComponent->GetOwner() == OwnerActor && ExistingComponent->GetFName() == ExpectedName)
		{
			ExistingComponent->SetAutoActivate(false);
			ExistingComponent->SetTemplate(Template);
			ExistingComponent->Activate(false);

			FPlayerSprayDeathEffectRuntimeSlot NewSlot;
			NewSlot.ParticleSystemPath = ParticleSystemPath;
			NewSlot.Component = ExistingComponent;
			DeathEffectSlots.push_back(NewSlot);
			return ExistingComponent;
		}
	}

	UParticleSystemComponent* Component = OwnerActor->AddComponent<UParticleSystemComponent>();
	if (!Component)
	{
		return nullptr;
	}

	Component->SetFName(ExpectedName);
	if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
	{
		Component->AttachToComponent(RootComponent);
	}
	Component->SetAutoActivate(false);
	Component->SetTemplate(Template);
	Component->Activate(false);

	FPlayerSprayDeathEffectRuntimeSlot NewSlot;
	NewSlot.ParticleSystemPath = ParticleSystemPath;
	NewSlot.Component = Component;
	DeathEffectSlots.push_back(NewSlot);
	return Component;
}

UParticleSystemComponent* UPlayerSprayProjectileComponent::GetDeathEffectComponent(const FPlayerSprayDeathEffectRuntimeSlot& Slot) const
{
	UParticleSystemComponent* Component = Slot.Component.Get();
	return Component && Component->GetOwner() == GetOwner() ? Component : nullptr;
}

void UPlayerSprayProjectileComponent::EmitProjectileDeathEffect(const FPlayerSprayProjectile& Projectile)
{
	const FPlayerSprayDeathEffectSettings& DeathEffect = Projectile.DeathEffect;
	if (!DeathEffect.bEnableDeathEffect)
	{
		return;
	}

	if (DeathEffect.ParticleSystemPath.empty() ||
		DeathEffect.ParticleSystemPath == "None")
	{
		++DeathEffectDroppedThisFrame;
		return;
	}

	if (MaxDeathEffectEventsPerFrame <= 0 ||
		DeathEffectEventsThisFrame >= MaxDeathEffectEventsPerFrame)
	{
		++DeathEffectDroppedThisFrame;
		return;
	}

	UParticleSystemComponent* Component = FindOrCreateDeathEffectComponent(DeathEffect.ParticleSystemPath);
	if (!Component)
	{
		++DeathEffectDroppedThisFrame;
		return;
	}

	const FVector EventVelocity = DeathEffect.bInheritProjectileVelocity
		? Projectile.Velocity * DeathEffect.VelocityScale
		: FVector::ZeroVector;
	Component->EmitExternalDeathEvent(DeathEffect.EventName, Projectile.Position, EventVelocity);
	++DeathEffectEventsThisFrame;

	for (FPlayerSprayDeathEffectRuntimeSlot& Slot : DeathEffectSlots)
	{
		if (Slot.ParticleSystemPath == DeathEffect.ParticleSystemPath)
		{
			++Slot.EventsSubmittedThisFrame;
			break;
		}
	}
}

bool UPlayerSprayProjectileComponent::CanAutoCreateRuntimeHelperComponent() const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	if (OwnerActor->HasActorBegunPlay())
	{
		return true;
	}

	UWorld* World = GetWorld();
	return World && World->HasBegunPlay();
}

int32 UPlayerSprayProjectileComponent::CountValidDeathEffectComponents() const
{
	int32 Count = 0;
	for (const FPlayerSprayDeathEffectRuntimeSlot& Slot : DeathEffectSlots)
	{
		if (GetDeathEffectComponent(Slot))
		{
			++Count;
		}
	}
	return Count;
}

void UPlayerSprayProjectileComponent::ResetDeathEffectFrameCounters()
{
	DeathEffectEventsThisFrame = 0;
	DeathEffectDroppedThisFrame = 0;
	for (FPlayerSprayDeathEffectRuntimeSlot& Slot : DeathEffectSlots)
	{
		Slot.EventsSubmittedThisFrame = 0;
		Slot.EventsDroppedThisFrame = 0;
	}
}

void UPlayerSprayProjectileComponent::SyncRender()
{
	UInstancedStaticMeshComponent* Renderer = EnsureRenderComponent();
	if (!Renderer)
	{
		return;
	}

	TArray<FTransform> Transforms;
	Transforms.reserve(Projectiles.size());
	const FMatrix RendererWorldInverse = Renderer->GetWorldInverseMatrix();
	for (const FPlayerSprayProjectile& Projectile : Projectiles)
	{
		const FTransform WorldTransform = MakeProjectileTransform(Projectile);
		Transforms.push_back(FTransform(WorldTransform.ToMatrix() * RendererWorldInverse));
	}
	Renderer->SetInstances(std::move(Transforms));
}

void UPlayerSprayProjectileComponent::SyncTrail()
{
	UBulletTrailComponent* TrailRenderer = EnsureTrailComponent();
	if (!TrailRenderer)
	{
		return;
	}

	TArray<FBulletTrailChain> Chains;
	Chains.reserve(Projectiles.size());
	for (const FPlayerSprayProjectile& Projectile : Projectiles)
	{
		if (Projectile.TrailSamples.size() < 2)
		{
			continue;
		}

		FBulletTrailChain Chain;
		Chain.MaterialPath = TrailMaterialPath;
		for (const FVector& Sample : Projectile.TrailSamples)
		{
			FBulletTrailPoint Point;
			Point.Position = Sample;
			Point.Color = TrailColor;
			Point.Width = TrailWidth;
			Chain.Points.push_back(Point);
		}
		Chains.push_back(std::move(Chain));
	}

	if (Chains.empty())
	{
		TrailRenderer->ClearTrailChains();
	}
	else
	{
		TrailRenderer->SetTrailChains(std::move(Chains));
	}
}

void UPlayerSprayProjectileComponent::ClearRender()
{
	if (UInstancedStaticMeshComponent* Renderer = RenderComponent.Get())
	{
		Renderer->ClearInstances();
	}
	if (UBulletTrailComponent* TrailRenderer = TrailComponent.Get())
	{
		TrailRenderer->ClearTrailChains();
	}
}

UInstancedStaticMeshComponent* UPlayerSprayProjectileComponent::EnsureRenderComponent()
{
	UInstancedStaticMeshComponent* Renderer = RenderComponent.Get();
	if (Renderer && Renderer->GetOwner() == GetOwner())
	{
		return Renderer;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	const FName ExpectedName("PlayerSprayProjectileRenderer");
	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		Renderer = Cast<UInstancedStaticMeshComponent>(Component);
		if (Renderer && Renderer->GetFName() == ExpectedName)
		{
			RenderComponent = Renderer;
			return Renderer;
		}
	}

	Renderer = OwnerActor->AddComponent<UInstancedStaticMeshComponent>();
	if (!Renderer)
	{
		return nullptr;
	}

	Renderer->SetFName(ExpectedName);
	if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
	{
		Renderer->AttachToComponent(RootComponent);
	}
	if (!MeshPath.empty() && MeshPath != "None")
	{
		Renderer->SetStaticMeshByPath(MeshPath);
	}
	if (!MaterialPath.empty() && MaterialPath != "None")
	{
		Renderer->SetMaterialByPath(0, MaterialPath);
	}
	RenderComponent = Renderer;
	return Renderer;
}

UBulletTrailComponent* UPlayerSprayProjectileComponent::EnsureTrailComponent()
{
	UBulletTrailComponent* TrailRenderer = TrailComponent.Get();
	if (TrailRenderer && TrailRenderer->GetOwner() == GetOwner())
	{
		return TrailRenderer;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	const FName ExpectedName("PlayerSprayProjectileTrail");
	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		TrailRenderer = Cast<UBulletTrailComponent>(Component);
		if (TrailRenderer && TrailRenderer->GetFName() == ExpectedName)
		{
			TrailComponent = TrailRenderer;
			return TrailRenderer;
		}
	}

	TrailRenderer = OwnerActor->AddComponent<UBulletTrailComponent>();
	if (!TrailRenderer)
	{
		return nullptr;
	}

	TrailRenderer->SetFName(ExpectedName);
	if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
	{
		TrailRenderer->AttachToComponent(RootComponent);
	}
	TrailComponent = TrailRenderer;
	return TrailRenderer;
}

FTransform UPlayerSprayProjectileComponent::MakeProjectileTransform(const FPlayerSprayProjectile& Projectile) const
{
	const FVector Direction = SafeDirection(Projectile.Velocity, FVector::ForwardVector);
	const float YawDegrees = std::atan2(Direction.Y, Direction.X) * (180.0f / Pi);
	const float FlatLength = std::sqrt(Direction.X * Direction.X + Direction.Y * Direction.Y);
	const float PitchDegrees = std::atan2(Direction.Z, FlatLength) * (180.0f / Pi);
	const float Scale = (std::max)(0.001f, RenderScale);
	return FTransform(
		Projectile.Position,
		FRotator(PitchDegrees, YawDegrees, 0.0f),
		FVector(Scale, Scale, Scale));
}

FVector UPlayerSprayProjectileComponent::BuildSpreadDirection(
	const FVector& CameraForward,
	int32 BurstIndex,
	int32 BurstCount) const
{
	const FVector Forward = SafeDirection(CameraForward, FVector::ForwardVector);
	FVector Right;
	FVector Up;
	MakeBasis(Forward, Right, Up);

	const int32 Count = (std::max)(1, BurstCount);
	const float T = Count > 1
		? static_cast<float>(BurstIndex) / static_cast<float>(Count - 1)
		: 0.5f;
	const float Angle = (static_cast<float>(BurstIndex) * 2.39996322973f);
	const float Radius = std::sqrt(ClampFloat(T, 0.0f, 1.0f));
	const float ConeRadians = ConeHalfAngleDegrees * (Pi / 180.0f);
	const FVector Offset =
		Right * std::cos(Angle) * std::sin(ConeRadians) * Radius
		+ Up * std::sin(Angle) * std::sin(ConeRadians) * Radius;
	return SafeDirection(Forward * std::cos(ConeRadians * Radius) + Offset, Forward);
}
