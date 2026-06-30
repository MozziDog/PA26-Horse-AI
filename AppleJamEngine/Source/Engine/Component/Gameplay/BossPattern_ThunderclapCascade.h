#pragma once

#include "Component/Gameplay/BossPatternComponentBase.h"
#include "Component/Gameplay/BulletHellComponent.h"

#include "Source/Engine/Component/Gameplay/BossPattern_ThunderclapCascade.generated.h"

struct FThunderclapCycleState
{
	FVector ImpactLocation = FVector::ZeroVector;
	FBulletHandle StrikeHandle;
	float Age = 0.0f;
	bool bStrikeSpawned = false;
	bool bShockwaveSpawned = false;
	bool bFinished = false;
};

UCLASS()
class UBossPattern_ThunderclapCascade : public UBossPatternComponentBase
{
public:
	GENERATED_BODY()
	UBossPattern_ThunderclapCascade();
	~UBossPattern_ThunderclapCascade() override = default;

	bool GetCanUse(const FBossPatternContext& Context, FString* OutRejectReason) const override;
	FString GetRuntimeDebugText() const override;

protected:
	void OnPatternStart(const FBossPatternContext& Context) override;
	void TickCurrentStep(float DeltaTime, const FBossPatternContext& Context) override;
	bool ShouldAdvanceStep(const FBossPatternContext& Context) const override;
	EBossPatternStep GetNextStep(EBossPatternStep CurrentStep) const override;

private:
	void TickCycleStarting(const FBossPatternContext& Context);
	void TickActiveCycles(float DeltaTime, const FBossPatternContext& Context);
	void StartCycle(const FBossPatternContext& Context);
	FVector ResolveImpactLocation(const FBossPatternContext& Context, const FVector& CandidateLocation) const;
	bool SampleGroundHeight(const FBossPatternContext& Context, const FVector& CandidateLocation, float& OutGroundZ) const;
	void SpawnStrike(FThunderclapCycleState& Cycle, const FBossPatternContext& Context);
	void SpawnShockwave(FThunderclapCycleState& Cycle, const FBossPatternContext& Context);
	void FinishCycle(FThunderclapCycleState& Cycle, const FBossPatternContext& Context);
	void ForceFinishAllCycles(const FBossPatternContext& Context);
	bool HasActiveCycles() const;
	FBulletSpawnParams MakeStrikeParams(const FVector& Position) const;
	FBulletSpawnParams MakeShockwaveParams(const FVector& Position, const FVector& Direction) const;

private:
	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Cycle Count", Min=1, Max=128, Speed=1)
	int32 CycleCount = 4;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Cycle Interval", Min=0.0f, Max=30.0f, Speed=0.01f)
	float CycleInterval = 0.4f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Forward Distance", Min=-1000.0f, Max=1000.0f, Speed=0.1f)
	float StrikeForwardDistance = 6.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Random XY Radius", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float StrikeRandomXYRadius = 0.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Spawn Height", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float StrikeSpawnHeight = 8.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Fall Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float StrikeFallSpeed = 18.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Projectile Radius", Min=0.01f, Max=100.0f, Speed=0.01f)
	float StrikeProjectileRadius = 0.35f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Lifetime", Min=0.0f, Max=300.0f, Speed=0.1f)
	float StrikeLifetime = 1.5f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Render Scale", Min=0.01f, Max=100.0f, Speed=0.01f)
	float StrikeRenderScale = 0.3f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Mesh Path", AssetType="StaticMesh")
	FString StrikeMeshPath = "Content/Data/BasicShape/Sphere.OBJ";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Strike Material Path", AssetType="Material")
	FString StrikeMaterialPath = "None";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Trail", DisplayName="Strike Trail Enabled")
	bool bStrikeTrailEnabled = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Trail", DisplayName="Strike Trail Material Path", AssetType="Material")
	FString StrikeTrailMaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Trail", DisplayName="Strike Trail Color", Type=Color4)
	FVector4 StrikeTrailColor = FVector4(1.0f, 0.6f, 0.15f, 1.0f);

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Trail", DisplayName="Strike Trail Width", Min=0.001f, Max=100.0f, Speed=0.01f)
	float StrikeTrailWidth = 0.1f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Trail", DisplayName="Strike Trail Lifetime", Min=0.001f, Max=60.0f, Speed=0.01f)
	float StrikeTrailLifetime = 0.18f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Trail", DisplayName="Strike Trail Max Samples", Min=2, Max=1024, Speed=1)
	int32 StrikeTrailMaxSamples = 8;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Trail", DisplayName="Strike Trail Sample Interval", Min=0.0f, Max=1.0f, Speed=0.001f)
	float StrikeTrailSampleInterval = 0.02f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Trail", DisplayName="Strike Trail Min Sample Distance", Min=0.0f, Max=100.0f, Speed=0.01f)
	float StrikeTrailMinSampleDistance = 0.05f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Death Effect", DisplayName="Strike Death Effect Enabled")
	bool bStrikeDeathEffectEnabled = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Death Effect", DisplayName="Strike Death Effect Path", AssetType="UParticleSystem")
	FString StrikeDeathEffectPath = "Content/Particle System/BossBulletDestroyVfx.uasset";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Death Effect", DisplayName="Strike Death Effect Event Name")
	FName StrikeDeathEffectEventName = FName("BulletDeath");

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Death Effect", DisplayName="Strike Death Effect Inherit Velocity")
	bool bStrikeDeathEffectInheritVelocity = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Strike Death Effect", DisplayName="Strike Death Effect Velocity Scale", Min=-1000.0f, Max=1000.0f, Speed=0.01f)
	float StrikeDeathEffectVelocityScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Ground Height Offset", Min=-1000.0f, Max=1000.0f, Speed=0.1f)
	float GroundHeightOffset = 0.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Ground Trace Start Height", Min=0.0f, Max=10000.0f, Speed=0.1f)
	float GroundTraceStartHeight = 100.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Ground Trace Down Distance", Min=0.0f, Max=10000.0f, Speed=0.1f)
	float GroundTraceDownDistance = 500.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Height Offset", Min=-1000.0f, Max=1000.0f, Speed=0.1f)
	float ShockwaveHeightOffset = 0.25f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Projectile Count", Min=1, Max=1024, Speed=1)
	int32 ShockwaveProjectileCount = 24;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Radius", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float ShockwaveRadius = 0.4f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float ShockwaveSpeed = 10.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Lifetime", Min=-1.0f, Max=300.0f, Speed=0.1f)
	float ShockwaveLifetime = 4.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Projectile Radius", Min=0.01f, Max=100.0f, Speed=0.01f)
	float ShockwaveProjectileRadius = 0.22f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Render Scale", Min=0.01f, Max=100.0f, Speed=0.01f)
	float ShockwaveRenderScale = 0.18f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Mesh Path", AssetType="StaticMesh")
	FString ShockwaveMeshPath = "Content/Data/BasicShape/Sphere.OBJ";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Shockwave Material Path", AssetType="Material")
	FString ShockwaveMaterialPath = "None";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Trail", DisplayName="Shockwave Trail Enabled")
	bool bShockwaveTrailEnabled = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Trail", DisplayName="Shockwave Trail Material Path", AssetType="Material")
	FString ShockwaveTrailMaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Trail", DisplayName="Shockwave Trail Color", Type=Color4)
	FVector4 ShockwaveTrailColor = FVector4(0.3f, 0.8f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Trail", DisplayName="Shockwave Trail Width", Min=0.001f, Max=100.0f, Speed=0.01f)
	float ShockwaveTrailWidth = 0.08f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Trail", DisplayName="Shockwave Trail Lifetime", Min=0.001f, Max=60.0f, Speed=0.01f)
	float ShockwaveTrailLifetime = 0.12f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Trail", DisplayName="Shockwave Trail Max Samples", Min=2, Max=1024, Speed=1)
	int32 ShockwaveTrailMaxSamples = 8;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Trail", DisplayName="Shockwave Trail Sample Interval", Min=0.0f, Max=1.0f, Speed=0.001f)
	float ShockwaveTrailSampleInterval = 0.02f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Trail", DisplayName="Shockwave Trail Min Sample Distance", Min=0.0f, Max=100.0f, Speed=0.01f)
	float ShockwaveTrailMinSampleDistance = 0.05f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Death Effect", DisplayName="Shockwave Death Effect Enabled")
	bool bShockwaveDeathEffectEnabled = false;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Death Effect", DisplayName="Shockwave Death Effect Path", AssetType="UParticleSystem")
	FString ShockwaveDeathEffectPath = "Content/Particle System/BossBulletDestroyVfx.uasset";

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Death Effect", DisplayName="Shockwave Death Effect Event Name")
	FName ShockwaveDeathEffectEventName = FName("BulletDeath");

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Death Effect", DisplayName="Shockwave Death Effect Inherit Velocity")
	bool bShockwaveDeathEffectInheritVelocity = true;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade|Shockwave Death Effect", DisplayName="Shockwave Death Effect Velocity Scale", Min=-1000.0f, Max=1000.0f, Speed=0.01f)
	float ShockwaveDeathEffectVelocityScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Boss Pattern|Thunderclap Cascade", DisplayName="Max Pattern Duration", Min=0.1f, Max=300.0f, Speed=0.1f)
	float MaxPatternDuration = 8.0f;

	TArray<FThunderclapCycleState> ActiveCycles;
	int32 StartedCycleCount = 0;
	float NextCycleStartTime = 0.0f;
	bool bForceDurationReached = false;
};
