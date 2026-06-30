#include "BulletHellComponent.h"

#include "Audio/AudioManager.h"
#include "Component/Gameplay/BulletTrailComponent.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Component/Primitive/InstancedStaticMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Gameplay/BulletHellDamageReceiverComponent.h"
#include "Core/Logging/Log.h"
#include "Core/TickFunction.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Rotator.h"
#include "Particle/ParticleSystemManager.h"
#include "Platform/Paths.h"
#include "Profiling/Stats/BulletHellStats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>

namespace
{
	constexpr float Pi = 3.1415926535f;
	constexpr float TwoPi = 6.28318530718f;
	constexpr float GoldenAngle = 2.39996322973f;
	constexpr const char* BulletTrailFallbackMaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";
	constexpr const char* PlayerTagName = "Player";
	constexpr const char* BossTagName = "Boss";
	constexpr const char* BulletDeathEffectComponentPrefix = "BulletHellDeathEffect";

	uint32 AdvanceNonZero(uint32 Value)
	{
		++Value;
		return Value == 0 ? 1 : Value;
	}

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback)
	{
		return Direction.IsNearlyZero() ? Fallback : Direction.Normalized();
	}

	float ClampFloat(float Value, float MinValue, float MaxValue)
	{
		return (std::max)(MinValue, (std::min)(MaxValue, Value));
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

	bool IsSelfOrFriendlyHit(const AActor* OwnerActor, const AActor* TargetActor)
	{
		if (!OwnerActor || !TargetActor)
		{
			return false;
		}
		if (TargetActor == OwnerActor)
		{
			return true;
		}
		if (HasActorTag(OwnerActor, BossTagName) && HasActorTag(TargetActor, BossTagName))
		{
			return true;
		}
		if (HasActorTag(OwnerActor, PlayerTagName) && HasActorTag(TargetActor, PlayerTagName))
		{
			return true;
		}
		return false;
	}

	bool IsPlayerActor(const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}
		if (HasActorTag(Actor, PlayerTagName))
		{
			return true;
		}
		const std::string Name = Actor->GetName();
		return Name.find("Player") != std::string::npos
			|| Name.find("player") != std::string::npos
			|| Name.find("Haru") != std::string::npos
			|| Name.find("haru") != std::string::npos;
	}

	FVector VelocityToTarget(const FVector& Position, const FVector& Target, float Speed)
	{
		return SafeDirection(Target - Position, FVector::ForwardVector) * Speed;
	}

	FVector VelocityInDirection(const FVector& Direction, float Speed)
	{
		return SafeDirection(Direction, FVector::ForwardVector) * Speed;
	}

	void MakePlaneBasis(const FVector& Normal, FVector& OutU, FVector& OutV)
	{
		const FVector N = SafeDirection(Normal, FVector::UpVector);
		const FVector Reference = std::fabs(N.Z) < 0.9f ? FVector::UpVector : FVector::RightVector;
		OutU = SafeDirection(Reference.Cross(N), FVector::RightVector);
		OutV = SafeDirection(N.Cross(OutU), FVector::UpVector);
	}

	float RandomUnitFloat()
	{
		static thread_local std::mt19937 Generator(0x6c8e9cf5u);
		static thread_local std::uniform_real_distribution<float> Distribution(0.0f, 1.0f);
		return Distribution(Generator);
	}

	bool IsProperty(const char* PropertyName, const char* MemberName, const char* DisplayName)
	{
		return PropertyName
			&& (std::strcmp(PropertyName, MemberName) == 0 || std::strcmp(PropertyName, DisplayName) == 0);
	}

	FString NormalizeTrailMaterialPath(const FString& MaterialPath)
	{
		return (MaterialPath.empty() || MaterialPath == "None")
			? FString(BulletTrailFallbackMaterialPath)
			: MaterialPath;
	}

	bool DoesMaterialPathExist(const FString& MaterialPath)
	{
		std::filesystem::path Path(FPaths::ToWide(FPaths::MakeProjectRelative(MaterialPath)));
		if (std::filesystem::exists(Path))
		{
			return true;
		}

		if (Path.extension() == L".mat")
		{
			Path.replace_extension(L".uasset");
			return std::filesystem::exists(Path);
		}

		return false;
	}

	bool IsExplicitTrailMaterialMissing(const FString& MaterialPath)
	{
		if (MaterialPath.empty() || MaterialPath == "None")
		{
			return false;
		}

		return !DoesMaterialPathExist(MaterialPath);
	}
}

UBulletHellComponent::UBulletHellComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.SetTickGroup(TG_PostUpdateWork);
	PrimaryComponentTick.SetEndTickGroup(TG_PostUpdateWork);
}

void UBulletHellComponent::BeginPlay()
{
	UActorComponent::BeginPlay();

	if (bEnableRendering)
	{
		RebuildRendererFromBullets();
	}
}

void UBulletHellComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);

	if (IsProperty(PropertyName, "bEnableRendering", "Enable Rendering") ||
		IsProperty(PropertyName, "bAutoCreateRenderer", "Auto Create Renderer") ||
		IsProperty(PropertyName, "RenderScale", "Render Scale"))
	{
		if (bEnableRendering)
		{
			EnsureRenderComponent();
			ApplyRenderAssets();
			RebuildRendererFromBullets();
		}
		else
		{
			ClearRenderer();
			ClearTrailRenderer();
		}
	}
}

void UBulletHellComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	ResetPerFrameDebugStats();
	TickBullets(DeltaTime);
	SyncRenderInstancesBulk();
	SyncTrailSegments();
	RecordOverlayStats();
}

FBulletHandle UBulletHellComponent::SpawnBullet(
	const FVector& Position,
	const FVector& Velocity,
	float Radius,
	float Lifetime)
{
	FBulletSpawnParams Params;
	Params.Position = Position;
	Params.Velocity = Velocity;
	Params.Archetype.Radius = Radius;
	Params.Archetype.Speed = Velocity.Length();
	Params.Archetype.Lifetime = Lifetime;
	Params.Archetype.RenderScale = RenderScale;
	return SpawnBullet(Params);
}

FBulletHandle UBulletHellComponent::SpawnBullet(const FBulletSpawnParams& Params)
{
	const FBulletArchetype& Archetype = Params.Archetype;

	FBulletInstance Bullet;
	Bullet.Id = NextBulletId;
	Bullet.Generation = NextBulletGeneration;
	Bullet.MeshPath = Archetype.MeshPath;
	Bullet.MaterialPath = Archetype.MaterialPath;
	Bullet.Position = Params.Position;
	Bullet.PreviousPosition = Params.Position;
	Bullet.Velocity = Params.Velocity;
	Bullet.Radius = (std::max)(0.01f, Archetype.Radius);
	Bullet.Damage = (std::max)(0.0f, Archetype.Damage);
	Bullet.Age = 0.0f;
	Bullet.Lifetime = Archetype.Lifetime;
	Bullet.ArchetypeIndex = Params.ArchetypeIndex;
	Bullet.GroupId = Params.GroupId;
	Bullet.RenderSlotIndex = -1;
	Bullet.RenderScale = (std::max)(0.01f, Archetype.RenderScale);
	Bullet.Trail = Archetype.Trail;
	Bullet.DeathEffect = Archetype.DeathEffect;
	ResetTrailSamples(Bullet);
	Bullet.HomingTargetPosition = Params.HomingTargetPosition;
	Bullet.HomingTargetActor = Params.HomingTargetActor;
	Bullet.bHoming = Params.bHoming;
	Bullet.HomingStrength = (std::max)(0.0f, Params.HomingStrength);
	Bullet.HomingMaxTurnRateDegrees = (std::max)(0.0f, Params.HomingMaxTurnRateDegrees);
	Bullet.HomingConeHalfAngleDegrees = ClampFloat(Params.HomingConeHalfAngleDegrees, 0.0f, 180.0f);
	Bullet.RenderInstanceIndex = -1;
	Bullet.bAlive = true;

	NextBulletId = AdvanceNonZero(NextBulletId);
	NextBulletGeneration = AdvanceNonZero(NextBulletGeneration);

	const int32 NewIndex = static_cast<int32>(Bullets.size());
	BulletIndexById[Bullet.Id] = NewIndex;
	Bullets.push_back(Bullet);
	const int32 RenderSlotIndex = FindOrCreateRenderSlot(Archetype);
	Bullets.back().RenderSlotIndex = RenderSlotIndex;
	if (RenderSlotIndex >= 0)
	{
		FBulletRenderSlot& Slot = RenderSlots[RenderSlotIndex];
		if (UInstancedStaticMeshComponent* Renderer = EnsureRenderSlotComponent(RenderSlotIndex))
		{
			Bullets.back().RenderInstanceIndex = Renderer->AddInstance(MakeBulletRenderTransform(Bullets.back()));
			Slot.BulletIndices.push_back(NewIndex);
		}
	}

	DebugStats.ActiveBulletCount = static_cast<int32>(Bullets.size());
	++DebugStats.TotalSpawned;
	UpdateBehaviorDebugStats();
	UpdateRenderDebugStats();

	return FBulletHandle{ Bullet.Id, Bullet.Generation };
}

int32 UBulletHellComponent::SpawnSphereSurfaceToTarget(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Center,
	float Radius,
	int32 Count,
	const FVector& Target,
	float Speed)
{
	if (Count <= 0)
	{
		return 0;
	}

	const float ClampedRadius = (std::max)(0.0f, Radius);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float T = (static_cast<float>(Index) + 0.5f) / static_cast<float>(Count);
		const float Z = 1.0f - 2.0f * T;
		const float RadiusXY = std::sqrt((std::max)(0.0f, 1.0f - Z * Z));
		const float Theta = GoldenAngle * static_cast<float>(Index);
		const FVector Direction(std::cos(Theta) * RadiusXY, std::sin(Theta) * RadiusXY, Z);
		const FVector Position = Center + Direction * ClampedRadius;

		FBulletSpawnParams Params = TemplateParams;
		Params.Position = Position;
		Params.Velocity = VelocityToTarget(Position, Target, Speed);
		Params.HomingTargetPosition = Target;
		SpawnBullet(Params);
	}

	return Count;
}

int32 UBulletHellComponent::SpawnSphereSurfaceInDirection(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Center,
	float Radius,
	int32 Count,
	const FVector& Direction,
	float Speed)
{
	if (Count <= 0)
	{
		return 0;
	}

	const FVector Velocity = VelocityInDirection(Direction, Speed);
	const float ClampedRadius = (std::max)(0.0f, Radius);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float T = (static_cast<float>(Index) + 0.5f) / static_cast<float>(Count);
		const float Z = 1.0f - 2.0f * T;
		const float RadiusXY = std::sqrt((std::max)(0.0f, 1.0f - Z * Z));
		const float Theta = GoldenAngle * static_cast<float>(Index);
		const FVector SurfaceDirection(std::cos(Theta) * RadiusXY, std::sin(Theta) * RadiusXY, Z);

		FBulletSpawnParams Params = TemplateParams;
		Params.Position = Center + SurfaceDirection * ClampedRadius;
		Params.Velocity = Velocity;
		SpawnBullet(Params);
	}

	return Count;
}

int32 UBulletHellComponent::SpawnSphereVolumeRandomToTarget(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Center,
	float Radius,
	int32 Count,
	const FVector& Target,
	float Speed)
{
	if (Count <= 0)
	{
		return 0;
	}

	const float ClampedRadius = (std::max)(0.0f, Radius);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Z = 1.0f - 2.0f * RandomUnitFloat();
		const float RadiusXY = std::sqrt((std::max)(0.0f, 1.0f - Z * Z));
		const float Theta = TwoPi * RandomUnitFloat();
		const float Distance = ClampedRadius * std::cbrt(RandomUnitFloat());
		const FVector Direction(std::cos(Theta) * RadiusXY, std::sin(Theta) * RadiusXY, Z);
		const FVector Position = Center + Direction * Distance;

		FBulletSpawnParams Params = TemplateParams;
		Params.Position = Position;
		Params.Velocity = VelocityToTarget(Position, Target, Speed);
		Params.HomingTargetPosition = Target;
		SpawnBullet(Params);
	}

	return Count;
}

int32 UBulletHellComponent::SpawnSphereVolumeRandomInDirection(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Center,
	float Radius,
	int32 Count,
	const FVector& Direction,
	float Speed)
{
	if (Count <= 0)
	{
		return 0;
	}

	const FVector Velocity = VelocityInDirection(Direction, Speed);
	const float ClampedRadius = (std::max)(0.0f, Radius);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Z = 1.0f - 2.0f * RandomUnitFloat();
		const float RadiusXY = std::sqrt((std::max)(0.0f, 1.0f - Z * Z));
		const float Theta = TwoPi * RandomUnitFloat();
		const float Distance = ClampedRadius * std::cbrt(RandomUnitFloat());
		const FVector SurfaceDirection(std::cos(Theta) * RadiusXY, std::sin(Theta) * RadiusXY, Z);

		FBulletSpawnParams Params = TemplateParams;
		Params.Position = Center + SurfaceDirection * Distance;
		Params.Velocity = Velocity;
		SpawnBullet(Params);
	}

	return Count;
}

int32 UBulletHellComponent::SpawnCircleToTarget(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Center,
	const FVector& Normal,
	float Radius,
	int32 Count,
	const FVector& Target,
	float Speed)
{
	if (Count <= 0)
	{
		return 0;
	}

	FVector U;
	FVector V;
	MakePlaneBasis(Normal, U, V);

	const float ClampedRadius = (std::max)(0.0f, Radius);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = TwoPi * static_cast<float>(Index) / static_cast<float>(Count);
		const FVector Position = Center + (U * std::cos(Angle) + V * std::sin(Angle)) * ClampedRadius;

		FBulletSpawnParams Params = TemplateParams;
		Params.Position = Position;
		Params.Velocity = VelocityToTarget(Position, Target, Speed);
		Params.HomingTargetPosition = Target;
		SpawnBullet(Params);
	}

	return Count;
}

int32 UBulletHellComponent::SpawnCircleInDirection(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Center,
	const FVector& Normal,
	float Radius,
	int32 Count,
	const FVector& Direction,
	float Speed)
{
	if (Count <= 0)
	{
		return 0;
	}

	FVector U;
	FVector V;
	MakePlaneBasis(Normal, U, V);

	const FVector Velocity = VelocityInDirection(Direction, Speed);
	const float ClampedRadius = (std::max)(0.0f, Radius);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = TwoPi * static_cast<float>(Index) / static_cast<float>(Count);

		FBulletSpawnParams Params = TemplateParams;
		Params.Position = Center + (U * std::cos(Angle) + V * std::sin(Angle)) * ClampedRadius;
		Params.Velocity = Velocity;
		SpawnBullet(Params);
	}

	return Count;
}

int32 UBulletHellComponent::SpawnBoxToTarget(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Center,
	int32 CountX,
	int32 CountY,
	int32 CountZ,
	float Spacing,
	const FQuat& Rotation,
	const FVector& Target,
	float Speed)
{
	if (CountX <= 0 || CountY <= 0 || CountZ <= 0)
	{
		return 0;
	}

	int32 SpawnedCount = 0;
	const FVector HalfExtents(
		static_cast<float>(CountX - 1) * 0.5f,
		static_cast<float>(CountY - 1) * 0.5f,
		static_cast<float>(CountZ - 1) * 0.5f);

	for (int32 Z = 0; Z < CountZ; ++Z)
	{
		for (int32 Y = 0; Y < CountY; ++Y)
		{
			for (int32 X = 0; X < CountX; ++X)
			{
				const FVector Local(
					(static_cast<float>(X) - HalfExtents.X) * Spacing,
					(static_cast<float>(Y) - HalfExtents.Y) * Spacing,
					(static_cast<float>(Z) - HalfExtents.Z) * Spacing);
				const FVector Position = Center + Rotation.RotateVector(Local);

				FBulletSpawnParams Params = TemplateParams;
				Params.Position = Position;
				Params.Velocity = VelocityToTarget(Position, Target, Speed);
				Params.HomingTargetPosition = Target;
				SpawnBullet(Params);
				++SpawnedCount;
			}
		}
	}

	return SpawnedCount;
}

int32 UBulletHellComponent::SpawnBoxInDirection(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Center,
	int32 CountX,
	int32 CountY,
	int32 CountZ,
	float Spacing,
	const FQuat& Rotation,
	const FVector& Direction,
	float Speed)
{
	if (CountX <= 0 || CountY <= 0 || CountZ <= 0)
	{
		return 0;
	}

	const FVector Velocity = VelocityInDirection(Direction, Speed);
	int32 SpawnedCount = 0;
	const FVector HalfExtents(
		static_cast<float>(CountX - 1) * 0.5f,
		static_cast<float>(CountY - 1) * 0.5f,
		static_cast<float>(CountZ - 1) * 0.5f);

	for (int32 Z = 0; Z < CountZ; ++Z)
	{
		for (int32 Y = 0; Y < CountY; ++Y)
		{
			for (int32 X = 0; X < CountX; ++X)
			{
				const FVector Local(
					(static_cast<float>(X) - HalfExtents.X) * Spacing,
					(static_cast<float>(Y) - HalfExtents.Y) * Spacing,
					(static_cast<float>(Z) - HalfExtents.Z) * Spacing);

				FBulletSpawnParams Params = TemplateParams;
				Params.Position = Center + Rotation.RotateVector(Local);
				Params.Velocity = Velocity;
				SpawnBullet(Params);
				++SpawnedCount;
			}
		}
	}

	return SpawnedCount;
}

int32 UBulletHellComponent::SpawnLineToTarget(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Start,
	const FVector& End,
	int32 Count,
	const FVector& Target,
	float Speed)
{
	if (Count <= 0)
	{
		return 0;
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Alpha = Count == 1 ? 0.0f : static_cast<float>(Index) / static_cast<float>(Count - 1);
		const FVector Position = FVector::Lerp(Start, End, Alpha);

		FBulletSpawnParams Params = TemplateParams;
		Params.Position = Position;
		Params.Velocity = VelocityToTarget(Position, Target, Speed);
		Params.HomingTargetPosition = Target;
		SpawnBullet(Params);
	}

	return Count;
}

int32 UBulletHellComponent::SpawnLineInDirection(
	const FBulletSpawnParams& TemplateParams,
	const FVector& Start,
	const FVector& End,
	int32 Count,
	const FVector& Direction,
	float Speed)
{
	if (Count <= 0)
	{
		return 0;
	}

	const FVector Velocity = VelocityInDirection(Direction, Speed);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Alpha = Count == 1 ? 0.0f : static_cast<float>(Index) / static_cast<float>(Count - 1);

		FBulletSpawnParams Params = TemplateParams;
		Params.Position = FVector::Lerp(Start, End, Alpha);
		Params.Velocity = Velocity;
		SpawnBullet(Params);
	}

	return Count;
}

bool UBulletHellComponent::KillBullet(const FBulletHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return false;
	}

	auto It = BulletIndexById.find(Handle.Id);
	if (It == BulletIndexById.end())
	{
		return false;
	}

	const int32 BulletIndex = It->second;
	if (BulletIndex < 0 || BulletIndex >= static_cast<int32>(Bullets.size()))
	{
		BulletIndexById.erase(It);
		return false;
	}

	const FBulletInstance& Bullet = Bullets[BulletIndex];
	if (!Bullet.bAlive || Bullet.Generation != Handle.Generation)
	{
		return false;
	}

	return RemoveBulletAtIndex(BulletIndex, false);
}

bool UBulletHellComponent::KillBulletById(int32 BulletId, int32 Generation)
{
	if (BulletId <= 0 || Generation <= 0)
	{
		return false;
	}

	return KillBullet(FBulletHandle{ static_cast<uint32>(BulletId), static_cast<uint32>(Generation) });
}

bool UBulletHellComponent::IsBulletAlive(const FBulletHandle& Handle) const
{
	return FindBullet(Handle) != nullptr;
}

const FBulletInstance* UBulletHellComponent::FindBullet(const FBulletHandle& Handle) const
{
	if (!Handle.IsValid())
	{
		return nullptr;
	}

	auto It = BulletIndexById.find(Handle.Id);
	if (It == BulletIndexById.end())
	{
		return nullptr;
	}

	const int32 BulletIndex = It->second;
	if (BulletIndex < 0 || BulletIndex >= static_cast<int32>(Bullets.size()))
	{
		return nullptr;
	}

	const FBulletInstance& Bullet = Bullets[BulletIndex];
	return Bullet.bAlive && Bullet.Generation == Handle.Generation ? &Bullet : nullptr;
}

bool UBulletHellComponent::LaunchBullet(const FBulletHandle& Handle, const FBulletLaunchParams& Params)
{
	if (!Handle.IsValid())
	{
		return false;
	}

	auto It = BulletIndexById.find(Handle.Id);
	if (It == BulletIndexById.end())
	{
		return false;
	}

	const int32 BulletIndex = It->second;
	if (BulletIndex < 0 || BulletIndex >= static_cast<int32>(Bullets.size()))
	{
		BulletIndexById.erase(It);
		return false;
	}

	FBulletInstance& Bullet = Bullets[BulletIndex];
	if (!Bullet.bAlive || Bullet.Generation != Handle.Generation)
	{
		return false;
	}

	Bullet.Velocity = Params.Velocity;
	if (Params.bSetHoming)
	{
		Bullet.bHoming = Params.bHoming;
		Bullet.HomingTargetPosition = Params.HomingTargetPosition;
		Bullet.HomingTargetActor = Params.HomingTargetActor;
		Bullet.HomingStrength = (std::max)(0.0f, Params.HomingStrength);
		Bullet.HomingMaxTurnRateDegrees = (std::max)(0.0f, Params.HomingMaxTurnRateDegrees);
		Bullet.HomingConeHalfAngleDegrees = ClampFloat(Params.HomingConeHalfAngleDegrees, 0.0f, 180.0f);
	}

	if (Params.bResetAge)
	{
		Bullet.Age = 0.0f;
		ResetTrailSamples(Bullet);
	}

	if (Params.bSetLifetime)
	{
		Bullet.Lifetime = Params.Lifetime;
	}

	++DebugStats.RuntimeModificationCount;
	UpdateBehaviorDebugStats();
	return true;
}

int32 UBulletHellComponent::ApplyRuntimeModifier(const FBulletRuntimeModifier& Modifier)
{
	int32 UpdatedCount = 0;

	for (FBulletInstance& Bullet : Bullets)
	{
		if (!Bullet.bAlive)
		{
			continue;
		}

		if (Modifier.ArchetypeIndex >= 0 && Bullet.ArchetypeIndex != Modifier.ArchetypeIndex)
		{
			continue;
		}

		if (Modifier.GroupId >= 0 && Bullet.GroupId != Modifier.GroupId)
		{
			continue;
		}

		if (Modifier.bOnlyHoming && !Bullet.bHoming)
		{
			continue;
		}

		if (Modifier.bSetSpeed)
		{
			FVector DirectionFallback = Bullet.Velocity;
			if (DirectionFallback.IsNearlyZero())
			{
				if (Bullet.bHoming)
				{
					const AActor* TargetActor = Bullet.HomingTargetActor.Get();
					const FVector TargetPosition = TargetActor ? TargetActor->GetActorLocation() : Bullet.HomingTargetPosition;
					DirectionFallback = TargetPosition - Bullet.Position;
				}
			}

			Bullet.Velocity = SafeDirection(DirectionFallback, FVector::ForwardVector) * (std::max)(0.0f, Modifier.Speed);
		}

		if (Modifier.bSetHomingConeHalfAngle)
		{
			Bullet.HomingConeHalfAngleDegrees = ClampFloat(Modifier.HomingConeHalfAngleDegrees, 0.0f, 180.0f);
		}

		if (Modifier.bSetHomingEnabled && Bullet.bHoming != Modifier.bHoming)
		{
			Bullet.bHoming = Modifier.bHoming;
			++DebugStats.RuntimeModificationCount;
		}

		++UpdatedCount;
	}

	return UpdatedCount;
}

int32 UBulletHellComponent::SetActiveHomingConeHalfAngle(float ConeHalfAngleDegrees, int32 ArchetypeIndex)
{
	FBulletRuntimeModifier Modifier;
	Modifier.ArchetypeIndex = ArchetypeIndex;
	Modifier.bOnlyHoming = true;
	Modifier.bSetHomingConeHalfAngle = true;
	Modifier.HomingConeHalfAngleDegrees = ConeHalfAngleDegrees;
	return ApplyRuntimeModifier(Modifier);
}

void UBulletHellComponent::ClearBullets()
{
	DebugStats.TotalKilled += static_cast<uint32>(Bullets.size());
	Bullets.clear();
	BulletIndexById.clear();
	ClearRenderer();
	ClearTrailRenderer();
	DebugStats.ActiveBulletCount = 0;
	DebugStats.DebugDrawSelectedCount = 0;
	DebugStats.DebugDrawTruncatedCount = 0;
	UpdateBehaviorDebugStats();
	UpdateRenderDebugStats();
}

int32 UBulletHellComponent::GetBulletCount() const
{
	return static_cast<int32>(Bullets.size());
}

void UBulletHellComponent::RecordDebugDrawStats(int32 SelectedCount, int32 TruncatedCount)
{
	DebugStats.DebugDrawSelectedCount = (std::max)(0, SelectedCount);
	DebugStats.DebugDrawTruncatedCount = (std::max)(0, TruncatedCount);
}

void UBulletHellComponent::TickBullets(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		DebugStats.ActiveBulletCount = static_cast<int32>(Bullets.size());
		UpdateBehaviorDebugStats();
		return;
	}

	for (int32 Index = 0; Index < static_cast<int32>(Bullets.size());)
	{
		FBulletInstance& Bullet = Bullets[Index];
		UpdateHomingBehavior(Bullet, DeltaTime);
		Bullet.PreviousPosition = Bullet.Position;
		Bullet.Position += Bullet.Velocity * DeltaTime;
		Bullet.Age += DeltaTime;

		const EBulletCollisionKillReason CollisionKillReason = CheckBulletCollision(Bullet);
		if (CollisionKillReason != EBulletCollisionKillReason::None)
		{
			if (CollisionKillReason == EBulletCollisionKillReason::Erase)
			{
				++DebugStats.EraseKilledCount;
			}
			else
			{
				++DebugStats.CollisionKilledCount;
			}
			RemoveBulletAtIndex(Index, false);
			continue;
		}

		if (Bullet.Lifetime >= 0.0f && Bullet.Age >= Bullet.Lifetime)
		{
			RemoveBulletAtIndex(Index, true);
			continue;
		}

		UpdateTrailSamples(Bullet, DeltaTime);
		++Index;
	}

	DebugStats.ActiveBulletCount = static_cast<int32>(Bullets.size());
	UpdateBehaviorDebugStats();
}

void UBulletHellComponent::UpdateHomingBehavior(FBulletInstance& Bullet, float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	if (!Bullet.bHoming)
	{
		return;
	}

	const AActor* TargetActor = Bullet.HomingTargetActor.Get();
	const FVector TargetPosition = TargetActor ? TargetActor->GetActorLocation() : Bullet.HomingTargetPosition;
	const FVector DesiredDirection = SafeDirection(TargetPosition - Bullet.Position, Bullet.Velocity);
	const float CurrentSpeed = Bullet.Velocity.Length();
	if (CurrentSpeed <= 0.0f || DesiredDirection.IsNearlyZero())
	{
		return;
	}

	const FVector CurrentDirection = SafeDirection(Bullet.Velocity, DesiredDirection);
	const float Dot = ClampFloat(CurrentDirection.Dot(DesiredDirection), -1.0f, 1.0f);
	const float ConeHalfAngleDegrees = ClampFloat(Bullet.HomingConeHalfAngleDegrees, 0.0f, 180.0f);
	if (ConeHalfAngleDegrees < 180.0f)
	{
		const float ConeCos = std::cos(ConeHalfAngleDegrees * (3.1415926535f / 180.0f));
		if (Dot < ConeCos)
		{
			Bullet.bHoming = false;
			++DebugStats.RuntimeModificationCount;
			return;
		}
	}

	const float AngleRadians = std::acos(Dot);
	if (AngleRadians <= 0.0001f)
	{
		Bullet.Velocity = DesiredDirection * CurrentSpeed;
		return;
	}

	const float MaxTurnRadians = (std::max)(0.0f, Bullet.HomingMaxTurnRateDegrees) * (3.1415926535f / 180.0f) * DeltaTime;
	const float TurnAlpha = MaxTurnRadians > 0.0f ? ClampFloat(MaxTurnRadians / AngleRadians, 0.0f, 1.0f) : 1.0f;
	const float StrengthAlpha = ClampFloat((std::max)(0.0f, Bullet.HomingStrength) * DeltaTime, 0.0f, 1.0f);
	const float Alpha = (std::min)(TurnAlpha, StrengthAlpha);
	const FVector NewDirection = SafeDirection(
		CurrentDirection * (1.0f - Alpha) + DesiredDirection * Alpha,
		DesiredDirection);
	Bullet.Velocity = NewDirection * CurrentSpeed;
}

void UBulletHellComponent::ResetTrailSamples(FBulletInstance& Bullet)
{
	Bullet.TrailSamples.clear();
	Bullet.TrailSampleAccumulator = 0.0f;
	if (!Bullet.Trail.bEnableTrail)
	{
		return;
	}

	FBulletTrailSample Sample;
	Sample.Position = Bullet.Position;
	Sample.Age = 0.0f;
	Bullet.TrailSamples.push_back(Sample);
}

void UBulletHellComponent::UpdateTrailSamples(FBulletInstance& Bullet, float DeltaTime)
{
	if (!Bullet.Trail.bEnableTrail)
	{
		Bullet.TrailSamples.clear();
		Bullet.TrailSampleAccumulator = 0.0f;
		return;
	}

	const int32 MaxSamples = (std::max)(2, Bullet.Trail.MaxSamples);
	const float TrailLifetime = (std::max)(0.001f, Bullet.Trail.Lifetime);
	const float SampleInterval = (std::max)(0.0f, Bullet.Trail.SampleInterval);
	const float MinSampleDistance = (std::max)(0.0f, Bullet.Trail.MinSampleDistance);

	for (FBulletTrailSample& Sample : Bullet.TrailSamples)
	{
		Sample.Age += DeltaTime;
	}

	while (Bullet.TrailSamples.size() > 1 && Bullet.TrailSamples.front().Age > TrailLifetime)
	{
		Bullet.TrailSamples.erase(Bullet.TrailSamples.begin());
	}

	if (Bullet.TrailSamples.empty())
	{
		FBulletTrailSample Sample;
		Sample.Position = Bullet.Position;
		Sample.Age = 0.0f;
		Bullet.TrailSamples.push_back(Sample);
		Bullet.TrailSampleAccumulator = 0.0f;
		return;
	}

	Bullet.TrailSampleAccumulator += DeltaTime;
	const FVector LastStoredSamplePosition = Bullet.TrailSamples.back().Position;
	const float DistanceSquared = FVector::DistSquared(Bullet.Position, LastStoredSamplePosition);
	const bool bPassedDistance = DistanceSquared >= MinSampleDistance * MinSampleDistance;
	const bool bPassedInterval = SampleInterval <= 0.0f || Bullet.TrailSampleAccumulator >= SampleInterval;
	const bool bNeedSecondPoint = Bullet.TrailSamples.size() < 2 && bPassedDistance;
	if ((bPassedDistance && bPassedInterval) || bNeedSecondPoint)
	{
		FBulletTrailSample Sample;
		Sample.Position = Bullet.Position;
		Sample.Age = 0.0f;
		Bullet.TrailSamples.push_back(Sample);
		Bullet.TrailSampleAccumulator = 0.0f;
	}

	while (Bullet.TrailSamples.size() > static_cast<size_t>(MaxSamples))
	{
		Bullet.TrailSamples.erase(Bullet.TrailSamples.begin());
	}
}

void UBulletHellComponent::UpdateBehaviorDebugStats()
{
	DebugStats.ActiveBulletCount = static_cast<int32>(Bullets.size());
	DebugStats.ActiveNonHomingCount = 0;
	DebugStats.ActiveHomingCount = 0;
	DebugStats.ActivePrimaryArchetypeCount = 0;
	DebugStats.ActiveSecondaryArchetypeCount = 0;

	for (const FBulletInstance& Bullet : Bullets)
	{
		if (Bullet.ArchetypeIndex == 1)
		{
			++DebugStats.ActiveSecondaryArchetypeCount;
		}
		else
		{
			++DebugStats.ActivePrimaryArchetypeCount;
		}

		if (Bullet.bHoming)
		{
			++DebugStats.ActiveHomingCount;
		}
		else
		{
			++DebugStats.ActiveNonHomingCount;
		}
	}
}

UBulletHellComponent::EBulletCollisionKillReason UBulletHellComponent::CheckBulletCollision(const FBulletInstance& Bullet)
{
	if (!bEnableCollision)
	{
		return EBulletCollisionKillReason::None;
	}

	FHitResult Hit;
	const uint32 EraseObjectTypeMask = BuildEraseObjectTypeMask();
	if (bEnableEraseVolumes && EraseObjectTypeMask != 0 && SweepBulletByObjectTypes(Bullet, EraseObjectTypeMask, Hit))
	{
		return EBulletCollisionKillReason::Erase;
	}

	const uint32 CollisionObjectTypeMask = BuildCollisionObjectTypeMask();
	if (CollisionObjectTypeMask != 0 && SweepBulletByObjectTypes(Bullet, CollisionObjectTypeMask, Hit))
	{
		if (IsSelfOrFriendlyHit(GetOwner(), ResolveHitActor(Hit)))
		{
			return EBulletCollisionKillReason::None;
		}
		ApplyDamageToHitTarget(Bullet, Hit);
		return EBulletCollisionKillReason::Collision;
	}

	if (bKillOnBlockingCollision && SweepBulletByChannel(Bullet, CollisionTraceChannel, Hit))
	{
		if (IsSelfOrFriendlyHit(GetOwner(), ResolveHitActor(Hit)))
		{
			return EBulletCollisionKillReason::None;
		}
		ApplyDamageToHitTarget(Bullet, Hit);
		return EBulletCollisionKillReason::Collision;
	}

	return EBulletCollisionKillReason::None;
}

bool UBulletHellComponent::SweepBulletByChannel(
	const FBulletInstance& Bullet,
	ECollisionChannel TraceChannel,
	FHitResult& OutHit)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	++DebugStats.CollisionQueryCount;
	const FCollisionShape SweepShape = FCollisionShape::MakeSphere((std::max)(0.01f, Bullet.Radius));
	const bool bHit = World->PhysicsSweep(
		Bullet.PreviousPosition,
		Bullet.Position,
		FQuat::Identity,
		SweepShape,
		OutHit,
		TraceChannel,
		GetOwner());

	if (bHit)
	{
		++DebugStats.CollisionHitCount;
	}

	return bHit;
}

bool UBulletHellComponent::SweepBulletByObjectTypes(
	const FBulletInstance& Bullet,
	uint32 ObjectTypeMask,
	FHitResult& OutHit)
{
	if (ObjectTypeMask == 0)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	++DebugStats.CollisionQueryCount;
	const FCollisionShape SweepShape = FCollisionShape::MakeSphere((std::max)(0.01f, Bullet.Radius));
	const bool bHit = World->PhysicsSweepByObjectTypes(
		Bullet.PreviousPosition,
		Bullet.Position,
		FQuat::Identity,
		SweepShape,
		OutHit,
		ObjectTypeMask,
		GetOwner());

	if (bHit)
	{
		++DebugStats.CollisionHitCount;
	}

	return bHit;
}

uint32 UBulletHellComponent::BuildCollisionObjectTypeMask() const
{
	uint32 Mask = 0;
	if (bKillOnWorldStatic)
	{
		Mask |= ObjectTypeBit(ECollisionChannel::WorldStatic);
	}
	if (bKillOnWorldDynamic)
	{
		Mask |= ObjectTypeBit(ECollisionChannel::WorldDynamic);
	}
	if (bKillOnPawn)
	{
		Mask |= ObjectTypeBit(ECollisionChannel::Pawn);
	}
	return Mask;
}

uint32 UBulletHellComponent::BuildEraseObjectTypeMask() const
{
	uint32 Mask = 0;
	if (bEraseOnTrigger)
	{
		Mask |= ObjectTypeBit(ECollisionChannel::Trigger);
	}
	if (bEraseOnProjectile)
	{
		Mask |= ObjectTypeBit(ECollisionChannel::Projectile);
	}
	return Mask;
}

void UBulletHellComponent::ApplyDamageToHitTarget(const FBulletInstance& Bullet, const FHitResult& Hit) const
{
	if (Bullet.Damage <= 0.0f)
	{
		return;
	}

	AActor* TargetActor = ResolveHitActor(Hit);

	if (!TargetActor || IsSelfOrFriendlyHit(GetOwner(), TargetActor))
	{
		return;
	}

	if (UBulletHellDamageReceiverComponent* DamageReceiver = TargetActor->GetComponentByClass<UBulletHellDamageReceiverComponent>())
	{
		const float AppliedDamage = DamageReceiver->ApplyDamage(Bullet.Damage);
		if (AppliedDamage > 0.0f && IsPlayerActor(TargetActor))
		{
			FAudioManager::Get().PlayAudio("Hit", 1.0f);
		}
	}
}

bool UBulletHellComponent::RemoveBulletAtIndex(int32 BulletIndex, bool bExpired)
{
	if (BulletIndex < 0 || BulletIndex >= static_cast<int32>(Bullets.size()))
	{
		return false;
	}

	const int32 LastIndex = static_cast<int32>(Bullets.size()) - 1;
	const uint32 RemovedId = Bullets[BulletIndex].Id;
	const int32 RemovedRenderSlotIndex = Bullets[BulletIndex].RenderSlotIndex;
	const int32 RemovedRenderIndex = Bullets[BulletIndex].RenderInstanceIndex;

	EmitBulletDeathEffect(Bullets[BulletIndex], bExpired);

	if (RemovedRenderSlotIndex >= 0 && RemovedRenderSlotIndex < static_cast<int32>(RenderSlots.size()))
	{
		FBulletRenderSlot& Slot = RenderSlots[RemovedRenderSlotIndex];
		if (RemovedRenderIndex >= 0 && RemovedRenderIndex < static_cast<int32>(Slot.BulletIndices.size()))
		{
			const int32 LastRenderIndex = static_cast<int32>(Slot.BulletIndices.size()) - 1;
			if (RemovedRenderIndex != LastRenderIndex)
			{
				const int32 MovedBulletIndex = Slot.BulletIndices[LastRenderIndex];
				Slot.BulletIndices[RemovedRenderIndex] = MovedBulletIndex;
				if (MovedBulletIndex >= 0 && MovedBulletIndex < static_cast<int32>(Bullets.size()))
				{
					Bullets[MovedBulletIndex].RenderInstanceIndex = RemovedRenderIndex;
				}
			}
			Slot.BulletIndices.pop_back();
		}

		if (UInstancedStaticMeshComponent* Renderer = GetRenderSlotComponent(RemovedRenderSlotIndex))
		{
			Renderer->RemoveInstanceSwap(RemovedRenderIndex);
		}
	}
	BulletIndexById.erase(RemovedId);

	if (BulletIndex != LastIndex)
	{
		Bullets[BulletIndex] = Bullets[LastIndex];
		if (Bullets[BulletIndex].RenderSlotIndex >= 0 &&
			Bullets[BulletIndex].RenderSlotIndex < static_cast<int32>(RenderSlots.size()) &&
			Bullets[BulletIndex].RenderInstanceIndex >= 0 &&
			Bullets[BulletIndex].RenderInstanceIndex < static_cast<int32>(RenderSlots[Bullets[BulletIndex].RenderSlotIndex].BulletIndices.size()))
		{
			RenderSlots[Bullets[BulletIndex].RenderSlotIndex].BulletIndices[Bullets[BulletIndex].RenderInstanceIndex] = BulletIndex;
		}
		BulletIndexById[Bullets[BulletIndex].Id] = BulletIndex;
	}

	Bullets.pop_back();

	if (bExpired)
	{
		++DebugStats.TotalExpired;
	}
	else
	{
		++DebugStats.TotalKilled;
	}
	DebugStats.ActiveBulletCount = static_cast<int32>(Bullets.size());
	UpdateBehaviorDebugStats();
	UpdateRenderDebugStats();
	return true;
}

UParticleSystemComponent* UBulletHellComponent::FindOrCreateDeathEffectComponent(const FString& ParticleSystemPath)
{
	if (ParticleSystemPath.empty() || ParticleSystemPath == "None")
	{
		++DebugStats.DeathEffectMissingAssetCount;
		return nullptr;
	}

	for (FBulletDeathEffectRuntimeSlot& Slot : DeathEffectSlots)
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
		++DebugStats.DeathEffectMissingAssetCount;
		return nullptr;
	}

	const int32 SlotIndex = static_cast<int32>(DeathEffectSlots.size());
	char NameBuffer[96];
	std::snprintf(NameBuffer, sizeof(NameBuffer), "%s%d", BulletDeathEffectComponentPrefix, SlotIndex);
	const FName ExpectedName(NameBuffer);

	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		UParticleSystemComponent* ExistingComponent = Cast<UParticleSystemComponent>(Component);
		if (ExistingComponent && ExistingComponent->GetOwner() == OwnerActor && ExistingComponent->GetFName() == ExpectedName)
		{
			ExistingComponent->SetAutoActivate(false);
			ExistingComponent->SetTemplate(Template);
			ExistingComponent->Activate(false);

			FBulletDeathEffectRuntimeSlot NewSlot;
			NewSlot.ParticleSystemPath = ParticleSystemPath;
			NewSlot.Component = ExistingComponent;
			DeathEffectSlots.push_back(NewSlot);
			DebugStats.DeathEffectComponentCount = CountValidDeathEffectComponents();
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

	FBulletDeathEffectRuntimeSlot NewSlot;
	NewSlot.ParticleSystemPath = ParticleSystemPath;
	NewSlot.Component = Component;
	DeathEffectSlots.push_back(NewSlot);
	DebugStats.DeathEffectComponentCount = CountValidDeathEffectComponents();
	return Component;
}

UParticleSystemComponent* UBulletHellComponent::GetDeathEffectComponent(const FBulletDeathEffectRuntimeSlot& Slot) const
{
	UParticleSystemComponent* Component = Slot.Component.Get();
	return Component && Component->GetOwner() == GetOwner() ? Component : nullptr;
}

void UBulletHellComponent::EmitBulletDeathEffect(const FBulletInstance& Bullet, bool bExpired)
{
	(void)bExpired;

	const FBulletDeathEffectSettings& DeathEffect = Bullet.DeathEffect;
	if (!DeathEffect.bEnableDeathEffect)
	{
		return;
	}

	if (DeathEffect.EventType != EParticleEventType::Death)
	{
		++DebugStats.DeathEffectDroppedCount;
		return;
	}

	if (DeathEffect.ParticleSystemPath.empty() ||
		DeathEffect.ParticleSystemPath == "None")
	{
		++DebugStats.DeathEffectDroppedCount;
		++DebugStats.DeathEffectMissingAssetCount;
		return;
	}

	if (MaxDeathEffectEventsPerFrame <= 0 ||
		DebugStats.DeathEffectEventCount >= MaxDeathEffectEventsPerFrame)
	{
		++DebugStats.DeathEffectDroppedCount;
		++DebugStats.DeathEffectBudgetExceededCount;
		return;
	}

	UParticleSystemComponent* Component = FindOrCreateDeathEffectComponent(DeathEffect.ParticleSystemPath);
	if (!Component)
	{
		++DebugStats.DeathEffectDroppedCount;
		return;
	}

	const FVector EventVelocity = DeathEffect.bInheritBulletVelocity
		? Bullet.Velocity * DeathEffect.VelocityScale
		: FVector::ZeroVector;
	Component->EmitExternalDeathEvent(DeathEffect.EventName, Bullet.Position, EventVelocity);
	++DebugStats.DeathEffectEventCount;

	for (FBulletDeathEffectRuntimeSlot& Slot : DeathEffectSlots)
	{
		if (Slot.ParticleSystemPath == DeathEffect.ParticleSystemPath)
		{
			++Slot.EventsSubmittedThisFrame;
			break;
		}
	}
}

bool UBulletHellComponent::CanAutoCreateRuntimeHelperComponent() const
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

int32 UBulletHellComponent::CountValidDeathEffectComponents() const
{
	int32 Count = 0;
	for (const FBulletDeathEffectRuntimeSlot& Slot : DeathEffectSlots)
	{
		if (GetDeathEffectComponent(Slot))
		{
			++Count;
		}
	}
	return Count;
}

UInstancedStaticMeshComponent* UBulletHellComponent::EnsureRenderComponent()
{
	FBulletArchetype DefaultArchetype;
	DefaultArchetype.RenderScale = RenderScale;
	const int32 SlotIndex = FindOrCreateRenderSlot(DefaultArchetype);
	return SlotIndex >= 0 ? EnsureRenderSlotComponent(SlotIndex) : nullptr;
}

UInstancedStaticMeshComponent* UBulletHellComponent::GetRenderComponent() const
{
	return GetRenderSlotComponent(0);
}

UBulletTrailComponent* UBulletHellComponent::EnsureTrailComponent()
{
	if (!bEnableRendering)
	{
		return nullptr;
	}

	UBulletTrailComponent* Renderer = TrailComponent.Get();
	if (Renderer && Renderer->GetOwner() == GetOwner())
	{
		return Renderer;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	const FName ExpectedName("BulletHellTrailRenderer");
	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		UBulletTrailComponent* ExistingRenderer = Cast<UBulletTrailComponent>(Component);
		if (ExistingRenderer && ExistingRenderer->GetOwner() == OwnerActor && ExistingRenderer->GetFName() == ExpectedName)
		{
			TrailComponent = ExistingRenderer;
			return ExistingRenderer;
		}
	}

	if (!CanAutoCreateRenderComponent())
	{
		return nullptr;
	}

	Renderer = OwnerActor->AddComponent<UBulletTrailComponent>();
	if (!Renderer)
	{
		return nullptr;
	}

	Renderer->SetFName(ExpectedName);
	if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
	{
		Renderer->AttachToComponent(RootComponent);
	}

	TrailComponent = Renderer;
	return Renderer;
}

UBulletTrailComponent* UBulletHellComponent::GetTrailComponent() const
{
	if (!bEnableRendering)
	{
		return nullptr;
	}

	UBulletTrailComponent* Renderer = TrailComponent.Get();
	return Renderer && Renderer->GetOwner() == GetOwner() ? Renderer : nullptr;
}

int32 UBulletHellComponent::FindOrCreateRenderSlot(const FBulletArchetype& Archetype)
{
	for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(RenderSlots.size()); ++SlotIndex)
	{
		const FBulletRenderSlot& Slot = RenderSlots[SlotIndex];
		if (Slot.MeshPath == Archetype.MeshPath && Slot.MaterialPath == Archetype.MaterialPath)
		{
			return SlotIndex;
		}
	}

	FBulletRenderSlot NewSlot;
	NewSlot.MeshPath = Archetype.MeshPath;
	NewSlot.MaterialPath = Archetype.MaterialPath;
	RenderSlots.push_back(NewSlot);
	return static_cast<int32>(RenderSlots.size()) - 1;
}

UInstancedStaticMeshComponent* UBulletHellComponent::FindExistingRenderSlotComponent(int32 SlotIndex) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	char NameBuffer[64];
	std::snprintf(NameBuffer, sizeof(NameBuffer), "BulletHellRenderer%d", SlotIndex);
	const FName ExpectedName(NameBuffer);
	for (UActorComponent* Component : OwnerActor->GetComponents())
	{
		UInstancedStaticMeshComponent* Renderer = Cast<UInstancedStaticMeshComponent>(Component);
		if (Renderer && Renderer->GetOwner() == OwnerActor && Renderer->GetFName() == ExpectedName)
		{
			return Renderer;
		}
	}

	return nullptr;
}

bool UBulletHellComponent::CanAutoCreateRenderComponent() const
{
	if (!bAutoCreateRenderer)
	{
		return false;
	}

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

UInstancedStaticMeshComponent* UBulletHellComponent::EnsureRenderSlotComponent(int32 SlotIndex)
{
	if (!bEnableRendering)
	{
		return nullptr;
	}

	if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(RenderSlots.size()))
	{
		return nullptr;
	}

	FBulletRenderSlot& Slot = RenderSlots[SlotIndex];
	UInstancedStaticMeshComponent* Renderer = Slot.Renderer.Get();
	if (Renderer && Renderer->GetOwner() == GetOwner())
	{
		return Renderer;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		UpdateRenderDebugStats();
		return nullptr;
	}

	Renderer = FindExistingRenderSlotComponent(SlotIndex);
	if (Renderer)
	{
		Slot.Renderer = Renderer;
		if (SlotIndex == 0)
		{
			RenderComponent = Renderer;
		}
		ApplyRenderSlotAssets(SlotIndex);
		UpdateRenderDebugStats();
		return Renderer;
	}

	if (!CanAutoCreateRenderComponent())
	{
		UpdateRenderDebugStats();
		return nullptr;
	}

	Renderer = OwnerActor->AddComponent<UInstancedStaticMeshComponent>();
	if (!Renderer)
	{
		UpdateRenderDebugStats();
		return nullptr;
	}

	char NameBuffer[64];
	std::snprintf(NameBuffer, sizeof(NameBuffer), "BulletHellRenderer%d", SlotIndex);
	Renderer->SetFName(FName(NameBuffer));
	if (USceneComponent* RootComponent = OwnerActor->GetRootComponent())
	{
		Renderer->AttachToComponent(RootComponent);
	}

	Slot.Renderer = Renderer;
	if (SlotIndex == 0)
	{
		RenderComponent = Renderer;
	}
	ApplyRenderSlotAssets(SlotIndex);
	UpdateRenderDebugStats();
	return Renderer;
}

UInstancedStaticMeshComponent* UBulletHellComponent::GetRenderSlotComponent(int32 SlotIndex) const
{
	if (!bEnableRendering)
	{
		return nullptr;
	}

	if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(RenderSlots.size()))
	{
		return nullptr;
	}

	UInstancedStaticMeshComponent* Renderer = RenderSlots[SlotIndex].Renderer.Get();
	return Renderer && Renderer->GetOwner() == GetOwner() ? Renderer : nullptr;
}

void UBulletHellComponent::ApplyRenderAssets()
{
	for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(RenderSlots.size()); ++SlotIndex)
	{
		ApplyRenderSlotAssets(SlotIndex);
	}
}

void UBulletHellComponent::ApplyRenderSlotAssets(int32 SlotIndex)
{
	UInstancedStaticMeshComponent* Renderer = GetRenderSlotComponent(SlotIndex);
	if (!Renderer)
	{
		return;
	}

	const FBulletRenderSlot& Slot = RenderSlots[SlotIndex];
	if (!Slot.MeshPath.empty() && Slot.MeshPath != "None")
	{
		Renderer->SetStaticMeshByPath(Slot.MeshPath);
	}

	if (!Slot.MaterialPath.empty() && Slot.MaterialPath != "None")
	{
		Renderer->SetMaterialByPath(0, Slot.MaterialPath);
	}
}

void UBulletHellComponent::RebuildRendererFromBullets()
{
	if (!bEnableRendering)
	{
		ClearRenderer();
		return;
	}

	for (FBulletRenderSlot& Slot : RenderSlots)
	{
		Slot.BulletIndices.clear();
		if (UInstancedStaticMeshComponent* Renderer = Slot.Renderer.Get())
		{
			Renderer->ClearInstances();
		}
	}

	for (int32 Index = 0; Index < static_cast<int32>(Bullets.size()); ++Index)
	{
		FBulletInstance& Bullet = Bullets[Index];
		FBulletArchetype Archetype;
		Archetype.MeshPath = Bullet.MeshPath;
		Archetype.MaterialPath = Bullet.MaterialPath;
		Archetype.Radius = Bullet.Radius;
		Archetype.Lifetime = Bullet.Lifetime;
		Archetype.RenderScale = Bullet.RenderScale;
		const int32 SlotIndex = FindOrCreateRenderSlot(Archetype);
		Bullet.RenderSlotIndex = SlotIndex;
		Bullet.RenderScale = (std::max)(0.01f, Archetype.RenderScale);
		Bullet.RenderInstanceIndex = -1;
		if (SlotIndex < 0)
		{
			continue;
		}

		FBulletRenderSlot& Slot = RenderSlots[SlotIndex];
		if (UInstancedStaticMeshComponent* Renderer = EnsureRenderSlotComponent(SlotIndex))
		{
			Bullet.RenderInstanceIndex = Renderer->AddInstance(MakeBulletRenderTransform(Bullet));
			Slot.BulletIndices.push_back(Index);
		}
	}

	UpdateRenderDebugStats();
}

void UBulletHellComponent::SyncRenderInstancesBulk()
{
	if (!bEnableRendering)
	{
		ClearRenderer();
		return;
	}

	for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(RenderSlots.size()); ++SlotIndex)
	{
		FBulletRenderSlot& Slot = RenderSlots[SlotIndex];
		UInstancedStaticMeshComponent* Renderer = EnsureRenderSlotComponent(SlotIndex);
		if (!Renderer)
		{
			continue;
		}

		TArray<FTransform> Transforms;
		Transforms.reserve(Slot.BulletIndices.size());
		for (int32 RenderIndex = 0; RenderIndex < static_cast<int32>(Slot.BulletIndices.size()); ++RenderIndex)
		{
			const int32 BulletIndex = Slot.BulletIndices[RenderIndex];
			if (BulletIndex < 0 || BulletIndex >= static_cast<int32>(Bullets.size()))
			{
				continue;
			}

			Bullets[BulletIndex].RenderSlotIndex = SlotIndex;
			Bullets[BulletIndex].RenderInstanceIndex = RenderIndex;
			Transforms.push_back(MakeBulletRenderTransform(Bullets[BulletIndex]));
		}

		Renderer->SetInstances(std::move(Transforms));
	}

	UpdateRenderDebugStats();
}

void UBulletHellComponent::SyncTrailSegments()
{
	DebugStats.TrailEnabledBulletCount = 0;
	DebugStats.TrailSampleCount = 0;
	DebugStats.TrailBatchCount = 0;
	DebugStats.TrailVertexCount = 0;
	DebugStats.TrailIndexCount = 0;
	DebugStats.TrailTruncatedCount = 0;
	DebugStats.TrailMaterialMissingCount = 0;

	if (!bEnableRendering)
	{
		ClearTrailRenderer();
		return;
	}

	TArray<FBulletTrailChain> Chains;
	TArray<FString> MaterialPaths;
	Chains.reserve(Bullets.size());

	const int32 SampleBudget = (std::max)(0, MaxTrailSampleBudget);
	const int32 VertexBudget = (std::max)(0, MaxTrailVertexBudget);
	const int32 IndexBudget = (std::max)(0, MaxTrailIndexBudget);
	int32 UsedTrailSamples = 0;
	int32 UsedTrailVertices = 0;
	int32 UsedTrailIndices = 0;

	for (const FBulletInstance& Bullet : Bullets)
	{
		if (!Bullet.Trail.bEnableTrail)
		{
			continue;
		}

		++DebugStats.TrailEnabledBulletCount;
		DebugStats.TrailSampleCount += static_cast<int32>(Bullet.TrailSamples.size());
		if (Bullet.TrailSamples.empty())
		{
			continue;
		}

		FBulletTrailChain Chain;
		Chain.MaterialPath = NormalizeTrailMaterialPath(Bullet.Trail.MaterialPath);
		Chain.Points.reserve(Bullet.TrailSamples.size() + 1);
		const float TrailLifetime = (std::max)(0.001f, Bullet.Trail.Lifetime);
		auto AppendTrailPoint = [&](const FVector& Position, float Age)
		{
			const float AgeAlpha = ClampFloat(Age / TrailLifetime, 0.0f, 1.0f);
			FBulletTrailPoint Point;
			Point.Position = Position;
			Point.Width = (std::max)(0.001f, Bullet.Trail.Width);
			Point.Color = Bullet.Trail.Color;
			Point.Color.W *= 1.0f - AgeAlpha;
			Chain.Points.push_back(Point);
		};

		for (const FBulletTrailSample& Sample : Bullet.TrailSamples)
		{
			AppendTrailPoint(Sample.Position, Sample.Age);
		}

		if (Chain.Points.empty() || !(Bullet.Position - Chain.Points.back().Position).IsNearlyZero())
		{
			AppendTrailPoint(Bullet.Position, 0.0f);
		}

		if (Chain.Points.size() < 2)
		{
			continue;
		}

		const int32 RequestedPointCount = static_cast<int32>(Chain.Points.size());
		int32 AllowedPointCount = RequestedPointCount;
		if (SampleBudget > 0)
		{
			AllowedPointCount = (std::min)(AllowedPointCount, SampleBudget - UsedTrailSamples);
		}
		if (VertexBudget > 0)
		{
			AllowedPointCount = (std::min)(AllowedPointCount, (VertexBudget - UsedTrailVertices) / 2);
		}
		if (IndexBudget > 0)
		{
			AllowedPointCount = (std::min)(AllowedPointCount, ((IndexBudget - UsedTrailIndices) / 6) + 1);
		}

		if (AllowedPointCount < 2)
		{
			DebugStats.TrailTruncatedCount += RequestedPointCount;
			continue;
		}

		if (AllowedPointCount < RequestedPointCount)
		{
			Chain.Points.erase(Chain.Points.begin(), Chain.Points.end() - AllowedPointCount);
			DebugStats.TrailTruncatedCount += RequestedPointCount - AllowedPointCount;
		}

		const int32 ChainPointCount = static_cast<int32>(Chain.Points.size());
		const int32 ChainVertexCount = ChainPointCount * 2;
		const int32 ChainIndexCount = (ChainPointCount - 1) * 6;
		UsedTrailSamples += ChainPointCount;
		UsedTrailVertices += ChainVertexCount;
		UsedTrailIndices += ChainIndexCount;

		if (IsExplicitTrailMaterialMissing(Bullet.Trail.MaterialPath))
		{
			++DebugStats.TrailMaterialMissingCount;
		}

		Chains.push_back(Chain);

		bool bKnownMaterial = false;
		for (const FString& MaterialPath : MaterialPaths)
		{
			if (MaterialPath == Chain.MaterialPath)
			{
				bKnownMaterial = true;
				break;
			}
		}
		if (!bKnownMaterial)
		{
			MaterialPaths.push_back(Chain.MaterialPath);
		}
	}

	DebugStats.TrailBatchCount = static_cast<int32>(MaterialPaths.size());
	DebugStats.TrailVertexCount = UsedTrailVertices;
	DebugStats.TrailIndexCount = UsedTrailIndices;

	if (Chains.empty())
	{
		if (UBulletTrailComponent* Renderer = GetTrailComponent())
		{
			Renderer->ClearTrailChains();
		}
		return;
	}

	if (UBulletTrailComponent* Renderer = EnsureTrailComponent())
	{
		Renderer->SetTrailChains(std::move(Chains));
	}
}

void UBulletHellComponent::ClearRenderer()
{
	for (FBulletRenderSlot& Slot : RenderSlots)
	{
		Slot.BulletIndices.clear();
		if (UInstancedStaticMeshComponent* Renderer = Slot.Renderer.Get())
		{
			Renderer->ClearInstances();
		}
	}

	for (FBulletInstance& Bullet : Bullets)
	{
		Bullet.RenderSlotIndex = -1;
		Bullet.RenderInstanceIndex = -1;
	}
	UpdateRenderDebugStats();
}

void UBulletHellComponent::ClearTrailRenderer()
{
	UBulletTrailComponent* Renderer = TrailComponent.Get();
	if (Renderer && Renderer->GetOwner() == GetOwner())
	{
		Renderer->ClearTrailChains();
	}

	DebugStats.TrailEnabledBulletCount = 0;
	DebugStats.TrailSampleCount = 0;
	DebugStats.TrailBatchCount = 0;
	DebugStats.TrailVertexCount = 0;
	DebugStats.TrailIndexCount = 0;
	DebugStats.TrailTruncatedCount = 0;
	DebugStats.TrailMaterialMissingCount = 0;
}

FTransform UBulletHellComponent::MakeBulletRenderTransform(const FBulletInstance& Bullet) const
{
	FVector RenderPosition = Bullet.Position;
	FVector RenderVelocity = Bullet.Velocity;
	if (const UInstancedStaticMeshComponent* Renderer = GetRenderSlotComponent(Bullet.RenderSlotIndex))
	{
		const FMatrix RendererWorldInverse = Renderer->GetWorldInverseMatrix();
		RenderPosition = RendererWorldInverse.TransformPositionWithW(Bullet.Position);
		RenderVelocity = RendererWorldInverse.TransformVector(Bullet.Velocity);
	}

	FRotator Rotation = FRotator::ZeroRotator;
	if (RenderOrientationMode == EBulletHellRenderOrientationMode::VelocityYaw && !RenderVelocity.IsNearlyZero())
	{
		const float YawDegrees = std::atan2(RenderVelocity.Y, RenderVelocity.X) * (180.0f / 3.1415926535f);
		Rotation = FRotator(0.0f, YawDegrees, 0.0f);
	}

	const float Scale = (std::max)(0.01f, Bullet.RenderScale);
	return FTransform(
		RenderPosition,
		Rotation,
		FVector(Scale, Scale, Scale));
}

void UBulletHellComponent::UpdateRenderDebugStats()
{
	DebugStats.ActiveBulletCount = static_cast<int32>(Bullets.size());
	DebugStats.RendererSlotCount = 0;
	DebugStats.RenderInstanceCount = 0;
	DebugStats.RendererSlot0InstanceCount = 0;
	DebugStats.RendererSlot1InstanceCount = 0;
	if (!bEnableRendering)
	{
		DebugStats.RenderMismatchCount = 0;
		return;
	}

	int32 MismatchCount = 0;
	for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(RenderSlots.size()); ++SlotIndex)
	{
		const FBulletRenderSlot& Slot = RenderSlots[SlotIndex];
		UInstancedStaticMeshComponent* Renderer = GetRenderSlotComponent(SlotIndex);
		if (!Renderer)
		{
			if (!Slot.BulletIndices.empty())
			{
				++MismatchCount;
			}
			continue;
		}

		++DebugStats.RendererSlotCount;
		const int32 SlotInstanceCount = Renderer->GetInstanceCount();
		DebugStats.RenderInstanceCount += SlotInstanceCount;
		if (SlotIndex == 0)
		{
			DebugStats.RendererSlot0InstanceCount = SlotInstanceCount;
		}
		else if (SlotIndex == 1)
		{
			DebugStats.RendererSlot1InstanceCount = SlotInstanceCount;
		}
		if (SlotInstanceCount != static_cast<int32>(Slot.BulletIndices.size()))
		{
			++MismatchCount;
		}

		for (int32 RenderIndex = 0; RenderIndex < static_cast<int32>(Slot.BulletIndices.size()); ++RenderIndex)
		{
			const int32 BulletIndex = Slot.BulletIndices[RenderIndex];
			if (BulletIndex < 0 || BulletIndex >= static_cast<int32>(Bullets.size()))
			{
				++MismatchCount;
				continue;
			}

			const FBulletInstance& Bullet = Bullets[BulletIndex];
			if (Bullet.RenderSlotIndex != SlotIndex || Bullet.RenderInstanceIndex != RenderIndex)
			{
				++MismatchCount;
			}
		}
	}

	for (const FBulletInstance& Bullet : Bullets)
	{
		if (bEnableRendering && (Bullet.RenderSlotIndex < 0 || Bullet.RenderInstanceIndex < 0))
		{
			++MismatchCount;
		}
	}

	DebugStats.RenderMismatchCount = MismatchCount;
}

void UBulletHellComponent::ResetPerFrameDebugStats()
{
	DebugStats.ActiveBulletCount = static_cast<int32>(Bullets.size());
	DebugStats.DeathEffectComponentCount = CountValidDeathEffectComponents();
	DebugStats.DeathEffectEventCount = 0;
	DebugStats.DeathEffectDroppedCount = 0;
	DebugStats.DeathEffectMissingAssetCount = 0;
	DebugStats.DeathEffectBudgetExceededCount = 0;
	for (FBulletDeathEffectRuntimeSlot& Slot : DeathEffectSlots)
	{
		Slot.EventsSubmittedThisFrame = 0;
		Slot.EventsDroppedThisFrame = 0;
	}
}

void UBulletHellComponent::RecordOverlayStats() const
{
	FBulletHellStatsSnapshot Snapshot;
	Snapshot.ActiveBulletCount = static_cast<uint32>((std::max)(0, DebugStats.ActiveBulletCount));
	Snapshot.TotalSpawned = DebugStats.TotalSpawned;
	Snapshot.TotalKilled = DebugStats.TotalKilled;
	Snapshot.TotalExpired = DebugStats.TotalExpired;
	Snapshot.CollisionQueryCount = DebugStats.CollisionQueryCount;
	Snapshot.CollisionHitCount = DebugStats.CollisionHitCount;
	Snapshot.CollisionKilledCount = DebugStats.CollisionKilledCount;
	Snapshot.EraseKilledCount = DebugStats.EraseKilledCount;
	Snapshot.RuntimeModificationCount = DebugStats.RuntimeModificationCount;
	Snapshot.ActiveNonHomingCount = static_cast<uint32>((std::max)(0, DebugStats.ActiveNonHomingCount));
	Snapshot.ActiveHomingCount = static_cast<uint32>((std::max)(0, DebugStats.ActiveHomingCount));
	Snapshot.ActivePrimaryArchetypeCount = static_cast<uint32>((std::max)(0, DebugStats.ActivePrimaryArchetypeCount));
	Snapshot.ActiveSecondaryArchetypeCount = static_cast<uint32>((std::max)(0, DebugStats.ActiveSecondaryArchetypeCount));
	Snapshot.DebugDrawSelectedCount = static_cast<uint32>((std::max)(0, DebugStats.DebugDrawSelectedCount));
	Snapshot.DebugDrawTruncatedCount = static_cast<uint32>((std::max)(0, DebugStats.DebugDrawTruncatedCount));
	Snapshot.RenderInstanceCount = static_cast<uint32>((std::max)(0, DebugStats.RenderInstanceCount));
	Snapshot.RendererSlotCount = static_cast<uint32>((std::max)(0, DebugStats.RendererSlotCount));
	Snapshot.RendererSlot0InstanceCount = static_cast<uint32>((std::max)(0, DebugStats.RendererSlot0InstanceCount));
	Snapshot.RendererSlot1InstanceCount = static_cast<uint32>((std::max)(0, DebugStats.RendererSlot1InstanceCount));
	Snapshot.RenderMismatchCount = static_cast<uint32>((std::max)(0, DebugStats.RenderMismatchCount));
	Snapshot.TrailEnabledBulletCount = static_cast<uint32>((std::max)(0, DebugStats.TrailEnabledBulletCount));
	Snapshot.TrailSampleCount = static_cast<uint32>((std::max)(0, DebugStats.TrailSampleCount));
	Snapshot.TrailBatchCount = static_cast<uint32>((std::max)(0, DebugStats.TrailBatchCount));
	Snapshot.TrailVertexCount = static_cast<uint32>((std::max)(0, DebugStats.TrailVertexCount));
	Snapshot.TrailIndexCount = static_cast<uint32>((std::max)(0, DebugStats.TrailIndexCount));
	Snapshot.TrailTruncatedCount = static_cast<uint32>((std::max)(0, DebugStats.TrailTruncatedCount));
	Snapshot.TrailMaterialMissingCount = static_cast<uint32>((std::max)(0, DebugStats.TrailMaterialMissingCount));
	Snapshot.DeathEffectComponentCount = static_cast<uint32>((std::max)(0, DebugStats.DeathEffectComponentCount));
	Snapshot.DeathEffectEventCount = static_cast<uint32>((std::max)(0, DebugStats.DeathEffectEventCount));
	Snapshot.DeathEffectDroppedCount = static_cast<uint32>((std::max)(0, DebugStats.DeathEffectDroppedCount));
	Snapshot.DeathEffectMissingAssetCount = static_cast<uint32>((std::max)(0, DebugStats.DeathEffectMissingAssetCount));
	Snapshot.DeathEffectBudgetExceededCount = static_cast<uint32>((std::max)(0, DebugStats.DeathEffectBudgetExceededCount));
	BULLETHELL_STATS_ADD_COMPONENT(Snapshot);
}
