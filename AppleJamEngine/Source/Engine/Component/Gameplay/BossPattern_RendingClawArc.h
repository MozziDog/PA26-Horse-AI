#pragma once

#include "Component/Gameplay/BossPatternComponentBase.h"
#include "Component/Gameplay/BulletHellComponent.h"

#include "Source/Engine/Component/Gameplay/BossPattern_RendingClawArc.generated.h"

UCLASS()
class UBossPattern_RendingClawArc : public UBossPatternComponentBase
{
public:
	GENERATED_BODY()
	UBossPattern_RendingClawArc();
	~UBossPattern_RendingClawArc() override = default;

	bool GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const override;
	FString GetRuntimeDebugText() const override;

protected:
	void OnPatternStart(const FBossPatternContext& Context) override;
	void OnStepEnter(EBossPatternStep Step, const FBossPatternContext& Context) override;
	bool ShouldAdvanceStep(const FBossPatternContext& Context) const override;
	EBossPatternStep GetNextStep(EBossPatternStep CurrentStep) const override;

private:
	void SpawnClawArcs(const FBossPatternContext& Context);
	FBulletSpawnParams MakeBulletParams(const FVector& Position, const FVector& Direction) const;

private:
	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Sphere Radius", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float SphereRadius = 4.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Plane Count", Min=1, Max=64, Speed=1)
	int32 PlaneCount = 4;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Plane Spacing", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float PlaneSpacing = 0.8f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Arc Center Angle Degrees", Min=0.0f, Max=360.0f, Speed=1.0f)
	float ArcCenterAngleDegrees = 110.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Projectiles Per Arc", Min=1, Max=512, Speed=1)
	int32 ProjectilesPerArc = 10;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Projectile Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float ProjectileSpeed = 13.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Projectile Lifetime", Min=-1.0f, Max=300.0f, Speed=0.1f)
	float ProjectileLifetime = 5.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Projectile Radius", Min=0.01f, Max=100.0f, Speed=0.01f)
	float ProjectileRadius = 0.22f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Projectile Render Scale", Min=0.01f, Max=100.0f, Speed=0.01f)
	float ProjectileRenderScale = 0.18f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Projectile Mesh Path", AssetType="StaticMesh")
	FString ProjectileMeshPath = "Content/Data/BasicShape/Sphere.OBJ";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc", DisplayName="Projectile Material Path", AssetType="Material")
	FString ProjectileMaterialPath = "None";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Trail", DisplayName="Projectile Trail Enabled")
	bool bProjectileTrailEnabled = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Trail", DisplayName="Projectile Trail Material Path", AssetType="Material")
	FString ProjectileTrailMaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Trail", DisplayName="Projectile Trail Color", Type=Color4)
	FVector4 ProjectileTrailColor = FVector4(1.0f, 0.2f, 0.12f, 1.0f);

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Trail", DisplayName="Projectile Trail Width", Min=0.001f, Max=100.0f, Speed=0.01f)
	float ProjectileTrailWidth = 0.08f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Trail", DisplayName="Projectile Trail Lifetime", Min=0.001f, Max=60.0f, Speed=0.01f)
	float ProjectileTrailLifetime = 0.12f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Trail", DisplayName="Projectile Trail Max Samples", Min=2, Max=1024, Speed=1)
	int32 ProjectileTrailMaxSamples = 8;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Trail", DisplayName="Projectile Trail Sample Interval", Min=0.0f, Max=1.0f, Speed=0.001f)
	float ProjectileTrailSampleInterval = 0.02f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Trail", DisplayName="Projectile Trail Min Sample Distance", Min=0.0f, Max=100.0f, Speed=0.01f)
	float ProjectileTrailMinSampleDistance = 0.05f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Death Effect", DisplayName="Projectile Death Effect Enabled")
	bool bProjectileDeathEffectEnabled = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Death Effect", DisplayName="Projectile Death Effect Path", AssetType="UParticleSystem")
	FString ProjectileDeathEffectPath = "Content/Particle System/BossBulletDestroyVfx.uasset";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Death Effect", DisplayName="Projectile Death Effect Event Name")
	FName ProjectileDeathEffectEventName = FName("BulletDeath");

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Death Effect", DisplayName="Projectile Death Effect Inherit Velocity")
	bool bProjectileDeathEffectInheritVelocity = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Rending Claw Arc|Death Effect", DisplayName="Projectile Death Effect Velocity Scale", Min=-1000.0f, Max=1000.0f, Speed=0.01f)
	float ProjectileDeathEffectVelocityScale = 1.0f;

	bool bClawArcsSpawned = false;
	int32 SpawnedProjectileCount = 0;
	int32 UsedPlaneCount = 0;
};
