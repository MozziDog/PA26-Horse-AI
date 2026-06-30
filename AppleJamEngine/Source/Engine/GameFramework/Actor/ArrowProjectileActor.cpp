#include "pch.h"
#include "ArrowProjectileActor.h"
#include "Audio/AudioManager.h"

#include "Component/Gameplay/BulletHellDamageReceiverComponent.h"
#include "Component/Input/ActionComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Core/ScoreManager.h"
#include "Core/Types/CollisionTypes.h"
#include "Engine/Component/Particle/ParticleSystemComponent.h"
#include "Engine/Component/Primitive/StaticMeshComponent.h"
#include "Engine/Runtime/Engine.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/Camera/WaveOscillatorCameraShake.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/BossCharacter.h"
#include "GameFramework/ProjectilePoolSubSystem.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Particle/ParticleSystemManager.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetPreviewUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	constexpr float Pi = 3.1415926535f;
	constexpr const char* PlayerTagName = "Player";
	constexpr const char* BossTagName = "Boss";

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

	bool IsBossActor(const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}
		if (HasActorTag(Actor, BossTagName) || Cast<ABossCharacter>(Actor))
		{
			return true;
		}
		const std::string Name = Actor->GetName();
		return Name.find("Boss") != std::string::npos || Name.find("boss") != std::string::npos;
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
		const float MaxBounds[3] = { Expanded.X, Expanded.Y, Expanded.Z };

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
			return SegmentIntersectsExpandedLocalBox(LocalStart, LocalEnd, Shape.BoxHalfExtent, ProjectileRadius, OutAlpha);
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

	FRotator MakeArrowRotationFromVelocity(const FVector& Velocity)
	{
		if (Velocity.Length() <= 1.e-6f)
		{
			return FRotator(FVector(90.0f, 0.0f, 90.0f));
		}

		FVector Direction = Velocity;
		Direction.Normalize();

		constexpr float RadToDeg = 180.0f / Pi;
		const float HorizontalLength = std::sqrt(Direction.X * Direction.X + Direction.Y * Direction.Y);
		const float Pitch = std::atan2(-Direction.Z, HorizontalLength) * RadToDeg;
		const float Yaw = std::atan2(Direction.Y, Direction.X) * RadToDeg;

		const FRotator DirectionRotation(Pitch, Yaw, 0.0f);
		const FRotator MeshAxisOffset(FVector(90.0f, 0.0f, 90.0f));
		return (DirectionRotation.ToQuaternion() * MeshAxisOffset.ToQuaternion()).ToRotator();
	}

	constexpr const char* AimParticlePath = "Content/Particle System/Aim.uasset";
	constexpr const char* FireArrowParticlePath = "Content/Particle System/FireArrow.uasset";
}

void AArrowProjectileActor::BeginPlay()
{
	Super::BeginPlay();
}

void AArrowProjectileActor::InitArrowComponents()
{
	StaticMeshComponent = AddComponent<UStaticMeshComponent>();
	SetRootComponent(StaticMeshComponent);

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	UStaticMesh* MeshAsset = FMeshManager::LoadStaticMesh("Content/Data/Arrow/Arrow_StaticMesh.uasset", Device);
	StaticMeshComponent->SetStaticMesh(MeshAsset);
	EnsureArrowParticleComponent(AimParticleComponent, "ArrowAimParticle", AimParticlePath);
	EnsureArrowParticleComponent(FireArrowParticleComponent, "ArrowFireParticle", FireArrowParticlePath);
	SetAimParticleActive(false);
	SetFireArrowParticleActive(false);
}

UParticleSystemComponent* AArrowProjectileActor::EnsureArrowParticleComponent(
	TWeakObjectPtr<UParticleSystemComponent>& Component,
	const char* ComponentName,
	const char* TemplatePath)
{
	if (UParticleSystemComponent* Existing = Component.Get())
	{
		return Existing;
	}

	for (UActorComponent* ActorComponent : GetComponents())
	{
		UParticleSystemComponent* ParticleComponent = Cast<UParticleSystemComponent>(ActorComponent);
		if (ParticleComponent && ParticleComponent->GetFName() == FName(ComponentName))
		{
			Component = ParticleComponent;
			return ParticleComponent;
		}
	}

	UParticleSystemComponent* ParticleComponent = AddComponent<UParticleSystemComponent>();
	if (!ParticleComponent)
	{
		UE_LOG("[ArrowProjectileParticle] failed to create component=%s", ComponentName);
		return nullptr;
	}

	ParticleComponent->SetFName(FName(ComponentName));
	ParticleComponent->SetAutoActivate(false);
	ParticleComponent->SetResetOnActivate(true);
	if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
	{
		ParticleComponent->AttachToComponent(Mesh);
	}

	UParticleSystem* ParticleSystem = FParticleSystemManager::Get().Load(TemplatePath);
	if (ParticleSystem)
	{
		ParticleComponent->SetTemplate(ParticleSystem);
		UE_LOG("[ArrowProjectileParticle] loaded component=%s path=%s", ComponentName, TemplatePath);
	}
	else
	{
		UE_LOG("[ArrowProjectileParticle] failed to load template component=%s path=%s", ComponentName, TemplatePath);
	}

	Component = ParticleComponent;
	return ParticleComponent;
}

void AArrowProjectileActor::SetAimParticleActive(bool bActive)
{
	// Aim uses a vector-field particle. In this engine it renders reliably as a
	// world-spawned ParticleSystemActor/PSC, but not as a child PSC on the pooled
	// arrow mesh. HaruController owns the world-space Aim effect while the arrow is held.
	if (bActive)
	{
		return;
	}

	UParticleSystemComponent* AimParticle = EnsureArrowParticleComponent(AimParticleComponent, "ArrowAimParticle", AimParticlePath);
	if (!AimParticle)
	{
		return;
	}

	if (bActive)
	{
		constexpr int32 AimRefreshIntervalFrames = 6;
		const bool bShouldRefresh = !bAimParticleRequested
			|| !AimParticle->IsActive()
			|| AimParticleRefreshCounter >= AimRefreshIntervalFrames;
		if (bShouldRefresh)
		{
			AimParticle->Deactivate();
			AimParticle->Activate(true);
			const FVector L = GetActorLocation();
			UE_LOG("[ArrowProjectileParticle] Aim active actor=%s loc=(%.3f,%.3f,%.3f) refresh=%d",
				GetName().c_str(), L.X, L.Y, L.Z, AimParticleRefreshCounter);
			AimParticleRefreshCounter = 0;
		}
		else
		{
			++AimParticleRefreshCounter;
		}
		bAimParticleRequested = true;
	}
	else
	{
		if (bAimParticleRequested || AimParticle->IsActive())
		{
			UE_LOG("[ArrowProjectileParticle] Aim inactive actor=%s", GetName().c_str());
		}
		AimParticleRefreshCounter = 0;
		bAimParticleRequested = false;
		AimParticle->Deactivate();
	}
}

void AArrowProjectileActor::SetFireArrowParticleActive(bool bActive)
{
	UParticleSystemComponent* FireParticle = EnsureArrowParticleComponent(FireArrowParticleComponent, "ArrowFireParticle", FireArrowParticlePath);
	if (!FireParticle)
	{
		return;
	}

	if (bActive)
	{
		if (!bFireArrowParticleRequested || !FireParticle->IsActive())
		{
			FireParticle->Activate(true);
			const FVector L = GetActorLocation();
			UE_LOG("[ArrowProjectileParticle] FireArrow active actor=%s loc=(%.3f,%.3f,%.3f)",
				GetName().c_str(), L.X, L.Y, L.Z);
		}
		bFireArrowParticleRequested = true;
	}
	else
	{
		bFireArrowParticleRequested = false;
		FireParticle->Deactivate();
	}
}

void AArrowProjectileActor::OnPoolConstruct()
{
	if (bPoolConstructed) return;
	bPoolConstructed = true;
	InitArrowComponents();

	UE_LOG("[ArrowProjectileConstruct] HasActorBegunPlay=%d", (int)HasActorBegunPlay());
	if (HasActorBegunPlay())
	{
		if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
		{
			Mesh->BeginPlay();
		}
		if (UParticleSystemComponent* AimParticle = AimParticleComponent.Get())
		{
			AimParticle->BeginPlay();
		}
		if (UParticleSystemComponent* FireParticle = FireArrowParticleComponent.Get())
		{
			FireParticle->BeginPlay();
		}
	}
}

void AArrowProjectileActor::Activate(const FVector& Location, const FVector& Velocity)
{
	SetActorLocation(Location);
	if (Velocity.Length() <= 1.e-6f)
	{
		HoldAt(Location, FVector::ForwardVector);
		return;
	}

	Launch(Velocity);
}

void AArrowProjectileActor::HoldAt(const FVector& Location, const FVector& AimDirection)
{
	bHeld = true;
	bNeedsTick = false;
	CachedVelocity = FVector::ZeroVector;
	LifeTimeRemaining = DefaultLifeTime;

	SetActorLocation(Location);
	SetActorRotation(MakeArrowRotationFromVelocity(AimDirection));
	SetVisible(true);
	SetFireArrowParticleActive(false);
	SetAimParticleActive(true);

	if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
	{
		Mesh->SetLinearVelocity(FVector::ZeroVector);
		Mesh->SetAngularVelocity(FVector::ZeroVector);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetSimulatePhysics(false);
		Mesh->SetEnableGravity(false);
	}
}

void AArrowProjectileActor::Launch(const FVector& Velocity)
{
	bHeld = false;
	CachedVelocity = Velocity;
	LifeTimeRemaining = DefaultLifeTime;
	SetActorRotation(MakeArrowRotationFromVelocity(Velocity));
	SetVisible(true);
	SetAimParticleActive(false);
	SetFireArrowParticleActive(true);

	if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetEnableGravity(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetLinearVelocity(FVector::ZeroVector);
		Mesh->SetAngularVelocity(FVector::ZeroVector);

		UE_LOG("[ArrowProjectileActivate] kinematic=1 sim=%d gravity=0 coll=%d inVel=(%.3f,%.3f,%.3f)",
			(int)Mesh->GetSimulatePhysics(), (int)Mesh->IsCollisionEnabled(),
			Velocity.X, Velocity.Y, Velocity.Z);
	}
	else
	{
		UE_LOG("[ArrowProjectileActivate] StaticMeshComponent == NULL");
	}

	bNeedsTick = true;

	// 화살 발사 = 명중률 분모 1 증가 (held/charging 은 HoldAt 경유라 여기 오지 않음).
	FScoreManager::Get().AddShotFired();
}

void AArrowProjectileActor::Deactivate()
{
	bNeedsTick = false;
	bHeld = false;

	if (UStaticMeshComponent* Mesh = StaticMeshComponent.Get())
	{
		Mesh->SetLinearVelocity(FVector::ZeroVector);
		Mesh->SetAngularVelocity(FVector::ZeroVector);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetSimulatePhysics(false);
		Mesh->SetEnableGravity(false);
	}
	SetAimParticleActive(false);
	SetFireArrowParticleActive(false);

	SetVisible(false);
}

void AArrowProjectileActor::ResetState()
{
	bHeld = false;
	bAimParticleRequested = false;
	bFireArrowParticleRequested = false;
	AimParticleRefreshCounter = 0;
	CachedVelocity = FVector::ZeroVector;
	LifeTimeRemaining = 0.0f;
	SetAimParticleActive(false);
	SetFireArrowParticleActive(false);
}

void AArrowProjectileActor::Tick(float DeltaTime)
{
	AActor::Tick(DeltaTime);
	if (!bNeedsTick) return;

	const FVector PreviousLocation = GetActorLocation();
	CachedVelocity += FVector(0.0f, 0.0f, -GravityAcceleration) * DeltaTime;
	const FVector CurrentLocation = PreviousLocation + CachedVelocity * DeltaTime;

	FHitResult Hit;
	if (CheckKinematicCollision(PreviousLocation, CurrentLocation, Hit))
	{
		const FVector HitLocation = Hit.WorldHitLocation.IsNearlyZero() ? CurrentLocation : Hit.WorldHitLocation;
		SetActorLocation(HitLocation);
		ApplyDamageToHitTarget(Hit);
		EmitDeathEffect(HitLocation, CachedVelocity);
		UE_LOG("[ArrowProjectileHit] actor=%s loc=(%.3f,%.3f,%.3f) target=%s boss=%d",
			GetName().c_str(),
			HitLocation.X, HitLocation.Y, HitLocation.Z,
			ResolveHitActor(Hit) ? ResolveHitActor(Hit)->GetName().c_str() : "nil",
			(int)IsBossActor(ResolveHitActor(Hit)));
		ReleaseToPoolOrDeactivate();
		return;
	}

	SetActorLocation(CurrentLocation);
	SetActorRotation(MakeArrowRotationFromVelocity(CachedVelocity));

	LifeTimeRemaining -= DeltaTime;
	if (LifeTimeRemaining <= 0.0f)
	{
		ReleaseToPoolOrDeactivate();
	}
}

bool AArrowProjectileActor::CheckKinematicCollision(
	const FVector& PreviousLocation,
	const FVector& CurrentLocation,
	FHitResult& OutHit) const
{
	OutHit = FHitResult();
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const uint32 Mask =
		ObjectTypeBit(ECollisionChannel::WorldStatic)
		| ObjectTypeBit(ECollisionChannel::WorldDynamic)
		| ObjectTypeBit(ECollisionChannel::Pawn)
		| ObjectTypeBit(ECollisionChannel::Trigger);

	FHitResult SweepHit;
	const bool bSweepHit = World->PhysicsSweepByObjectTypes(
		PreviousLocation,
		CurrentLocation,
		FQuat::Identity,
		FCollisionShape::MakeSphere(ProjectileRadius),
		SweepHit,
		Mask,
		this);
	if (bSweepHit)
	{
		AActor* TargetActor = ResolveHitActor(SweepHit);
		if (TargetActor == this || HasActorTag(TargetActor, PlayerTagName))
		{
			return false;
		}

		if (IsBossActor(TargetActor))
		{
			FHitResult PhysicsAssetHit;
			if (!CheckBossPhysicsAssetHit(TargetActor, PreviousLocation, CurrentLocation, ProjectileRadius, PhysicsAssetHit))
			{
				return false;
			}
			OutHit = PhysicsAssetHit;
			return true;
		}

		OutHit = SweepHit;
		return true;
	}

	FHitResult PhysicsAssetHit;
	if (FindBossPhysicsAssetHit(PreviousLocation, CurrentLocation, ProjectileRadius, PhysicsAssetHit))
	{
		OutHit = PhysicsAssetHit;
		return true;
	}

	return false;
}

bool AArrowProjectileActor::CheckBossPhysicsAssetHit(
	AActor* BossActor,
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	float SweepRadius,
	FHitResult& OutHit) const
{
	OutHit = FHitResult();
	if (!BossActor || BossActor == this)
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

bool AArrowProjectileActor::FindBossPhysicsAssetHit(
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
		if (!IsValid(Candidate) || Candidate == this || HasActorTag(Candidate, PlayerTagName) || !IsBossActor(Candidate))
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

void AArrowProjectileActor::ApplyDamageToHitTarget(const FHitResult& Hit) const
{
	if (ProjectileDamage <= 0.0f)
	{
		return;
	}

	AActor* TargetActor = ResolveHitActor(Hit);
	if (!TargetActor || TargetActor == this || HasActorTag(TargetActor, PlayerTagName))
	{
		return;
	}

	if (UBulletHellDamageReceiverComponent* DamageReceiver = TargetActor->GetComponentByClass<UBulletHellDamageReceiverComponent>())
	{
		const float AppliedDamage = DamageReceiver->ApplyDamageFromSource(ProjectileDamage, EBossDamageSource::Arrow);
		if (AppliedDamage > 0.0f && IsBossActor(TargetActor))
		{
			FAudioManager::Get().PlayAudio("Hit", 1.0f);
			FScoreManager::Get().AddBossHit();  // 보스 유효타격 = 명중률 분자 1 증가
			FAudioManager::Get().PlayAudio("Explosion", 1.0f);
			PlayBossHitStop(TargetActor);
			PlayBossHitCameraShake();
		}
		UE_LOG("[ArrowProjectileDamage] target=%s boss=%d damage=%.3f applied=%.3f",
			TargetActor->GetName().c_str(),
			(int)IsBossActor(TargetActor),
			ProjectileDamage,
			AppliedDamage);
	}
}

void AArrowProjectileActor::PlayBossHitStop(AActor* TargetActor) const
{
	if (!IsBossActor(TargetActor) || BossHitStopDuration <= 0.0f)
	{
		return;
	}

	// Keep HitStop on a live actor; pooled projectiles stop ticking immediately after hit.
	AActor* ActionOwner = TargetActor;
	UActionComponent* Action = ActionOwner->GetComponentByClass<UActionComponent>();
	if (!Action)
	{
		Action = ActionOwner->AddComponent<UActionComponent>();
	}
	if (!Action)
	{
		return;
	}

	Action->HitStop(BossHitStopDuration, BossHitStopTimeDilation);
	if (BossSlomoDuration > 0.0f)
	{
		Action->Slomo(BossSlomoDuration, BossSlomoTimeDilation);
	}
	UE_LOG("[ArrowProjectileHitStop] actor=%s actionOwner=%s target=%s hitStopDuration=%.3f hitStopDilation=%.3f slomoDuration=%.3f slomoDilation=%.3f",
		GetName().c_str(),
		ActionOwner ? ActionOwner->GetName().c_str() : "nil",
		TargetActor ? TargetActor->GetName().c_str() : "nil",
		BossHitStopDuration,
		BossHitStopTimeDilation,
		BossSlomoDuration,
		BossSlomoTimeDilation);
}

void AArrowProjectileActor::PlayBossHitCameraShake() const
{
	if (BossCameraShakeScale <= 0.0f || BossCameraShakeDuration <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CameraManager = PlayerController ? PlayerController->GetPlayerCameraManager() : nullptr;
	if (!CameraManager)
	{
		return;
	}

	UWaveOscillatorCameraShake* Shake = CameraManager->StartCameraShake<UWaveOscillatorCameraShake>(BossCameraShakeScale);
	if (!Shake)
	{
		return;
	}

	Shake->Duration = BossCameraShakeDuration;
	Shake->BlendInTime = 0.0f;
	Shake->BlendOutTime = BossCameraShakeBlendOut;
	Shake->LocAmplitude = BossCameraShakeLocationAmplitude;
	Shake->RotAmplitude = BossCameraShakeRotationAmplitude;
	Shake->FOVAmplitude = BossCameraShakeFOVAmplitude;
	UE_LOG("[ArrowProjectileCameraShake] actor=%s manager=%p scale=%.3f duration=%.3f locAmp=(%.3f,%.3f,%.3f) rotAmp=(%.3f,%.3f,%.3f) fovAmp=%.3f",
		GetName().c_str(),
		CameraManager,
		BossCameraShakeScale,
		BossCameraShakeDuration,
		BossCameraShakeLocationAmplitude.X,
		BossCameraShakeLocationAmplitude.Y,
		BossCameraShakeLocationAmplitude.Z,
		BossCameraShakeRotationAmplitude.Pitch,
		BossCameraShakeRotationAmplitude.Yaw,
		BossCameraShakeRotationAmplitude.Roll,
		BossCameraShakeFOVAmplitude);
}

void AArrowProjectileActor::EmitDeathEffect(const FVector& Location, const FVector& Velocity) const
{
	if (DeathEffectPath.empty() || DeathEffectPath == "None")
	{
		return;
	}

	UParticleSystemComponent* Component = FGameplayStatics::SpawnEmitterAtLocation(
		GetWorld(),
		DeathEffectPath,
		Location,
		FRotator(),
		false);
	if (!Component)
	{
		UE_LOG("[ArrowProjectileDeathEffect] failed path=%s", DeathEffectPath.c_str());
		return;
	}

	Component->Activate(false);
	const FVector EventVelocity = bDeathEffectInheritVelocity
		? Velocity * DeathEffectVelocityScale
		: FVector::ZeroVector;
	Component->EmitExternalDeathEvent(DeathEffectEventName, Location, EventVelocity);
	UE_LOG("[ArrowProjectileDeathEffect] emitted path=%s event=%s loc=(%.3f,%.3f,%.3f)",
		DeathEffectPath.c_str(),
		DeathEffectEventName.ToString().c_str(),
		Location.X, Location.Y, Location.Z);
}

void AArrowProjectileActor::ReleaseToPoolOrDeactivate()
{
	if (UWorld* W = GetWorld())
	{
		if (FProjectilePoolSubsystem* Pool = W->GetProjectilePool())
		{
			Pool->Release(this);
			return;
		}
	}
	Deactivate();
}
