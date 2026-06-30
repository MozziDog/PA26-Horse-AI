#pragma once

#include "Component/Gameplay/BossPatternComponentBase.h"
#include "Component/Gameplay/BulletHellComponent.h"

#include "Source/Engine/Component/Gameplay/BossPattern_HomingOrbTrail.generated.h"

struct FHomingOrbPendingLaunch
{
	FBulletHandle Handle;
	FVector FallbackDirection = FVector::ForwardVector;
	FVector LastTargetPosition = FVector::ZeroVector;
	float RemainingDelay = 0.0f;
	bool bLaunched = false;
};

UCLASS()
class UBossPattern_HomingOrbTrail : public UBossPatternComponentBase
{
public:
	GENERATED_BODY()
	UBossPattern_HomingOrbTrail();
	~UBossPattern_HomingOrbTrail() override = default;

	bool GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const override;
	FString GetRuntimeDebugText() const override;

protected:
	void OnPatternStart(const FBossPatternContext& Context) override;
	void TickCurrentStep(float DeltaTime, const FBossPatternContext& Context) override;
	bool ShouldAdvanceStep(const FBossPatternContext& Context) const override;
	EBossPatternStep GetNextStep(EBossPatternStep CurrentStep) const override;

private:
	void TickSpawning(const FBossPatternContext& Context);
	void SpawnOrb(const FBossPatternContext& Context);
	void TickPendingLaunches(float DeltaTime, const FBossPatternContext& Context);
	bool HasPendingLaunches() const;
	FBulletSpawnParams MakeStationaryBulletParams(const FVector& Position) const;
	float GetSpawnInterval() const;

private:
	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Spawn Duration", Min=0.0f, Max=60.0f, Speed=0.01f)
	float SpawnDuration = 2.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Projectile Count", Min=1, Max=512, Speed=1)
	int32 ProjectileCount = 12;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Spawn Interval Override", Min=0.0f, Max=30.0f, Speed=0.01f)
	float SpawnIntervalOverride = 0.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Spawn Radius Around Boss", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float SpawnRadiusAroundBoss = 1.5f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Launch Delay", Min=0.0f, Max=30.0f, Speed=0.01f)
	float LaunchDelay = 0.6f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Projectile Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float ProjectileSpeed = 10.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Projectile Lifetime", Min=-1.0f, Max=300.0f, Speed=0.1f)
	float ProjectileLifetime = 6.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Projectile Radius", Min=0.01f, Max=100.0f, Speed=0.01f)
	float ProjectileRadius = 0.25f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Projectile Render Scale", Min=0.01f, Max=100.0f, Speed=0.01f)
	float ProjectileRenderScale = 0.2f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Projectile Mesh Path", AssetType="StaticMesh")
	FString ProjectileMeshPath = "Content/Data/BasicShape/Sphere.OBJ";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Projectile Material Path", AssetType="Material")
	FString ProjectileMaterialPath = "None";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Trail", DisplayName="Projectile Trail Enabled")
	bool bProjectileTrailEnabled = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Trail", DisplayName="Projectile Trail Material Path", AssetType="Material")
	FString ProjectileTrailMaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Trail", DisplayName="Projectile Trail Color", Type=Color4)
	FVector4 ProjectileTrailColor = FVector4(1.0f, 0.6f, 0.15f, 1.0f);

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Trail", DisplayName="Projectile Trail Width", Min=0.001f, Max=100.0f, Speed=0.01f)
	float ProjectileTrailWidth = 0.08f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Trail", DisplayName="Projectile Trail Lifetime", Min=0.001f, Max=60.0f, Speed=0.01f)
	float ProjectileTrailLifetime = 0.12f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Trail", DisplayName="Projectile Trail Max Samples", Min=2, Max=1024, Speed=1)
	int32 ProjectileTrailMaxSamples = 8;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Trail", DisplayName="Projectile Trail Sample Interval", Min=0.0f, Max=1.0f, Speed=0.001f)
	float ProjectileTrailSampleInterval = 0.02f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Trail", DisplayName="Projectile Trail Min Sample Distance", Min=0.0f, Max=100.0f, Speed=0.01f)
	float ProjectileTrailMinSampleDistance = 0.05f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Death Effect", DisplayName="Projectile Death Effect Enabled")
	bool bProjectileDeathEffectEnabled = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Death Effect", DisplayName="Projectile Death Effect Path", AssetType="UParticleSystem")
	FString ProjectileDeathEffectPath = "Content/Particle System/BossBulletDestroyVfx.uasset";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Death Effect", DisplayName="Projectile Death Effect Event Name")
	FName ProjectileDeathEffectEventName = FName("BulletDeath");

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Death Effect", DisplayName="Projectile Death Effect Inherit Velocity")
	bool bProjectileDeathEffectInheritVelocity = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail|Death Effect", DisplayName="Projectile Death Effect Velocity Scale", Min=-1000.0f, Max=1000.0f, Speed=0.01f)
	float ProjectileDeathEffectVelocityScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Homing Strength", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float HomingStrength = 8.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Homing Max Turn Rate Degrees", Min=0.0f, Max=720.0f, Speed=1.0f)
	float HomingMaxTurnRateDegrees = 180.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Homing Cone Half Angle Degrees", Min=0.0f, Max=180.0f, Speed=1.0f)
	float HomingConeHalfAngleDegrees = 180.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Spawn Forward Offset", Min=-1000.0f, Max=1000.0f, Speed=0.1f)
	float SpawnForwardOffset = 0.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Homing Orb Trail", DisplayName="Spawn Up Offset", Min=-1000.0f, Max=1000.0f, Speed=0.1f)
	float SpawnUpOffset = 1.0f;

	TArray<FHomingOrbPendingLaunch> PendingLaunches;
	FVector LockedTargetPosition = FVector::ZeroVector;
	int32 SpawnedCount = 0;
	float NextSpawnTime = 0.0f;
};
