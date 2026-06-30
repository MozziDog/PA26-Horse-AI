#pragma once

#include "Component/ActorComponent.h"
#include "BulletHellComponent.h"

#include "Source/Engine/Component/Gameplay/BulletHellDebugComponent.generated.h"

class UWorld;

UENUM()
enum class EBulletHellDebugDrawMode : int32
{
	Off,
	All,
	Highlighted
};

UENUM()
enum class EBulletHellDebugVisualMode : int32
{
	Bounds,
	Velocity,
	BoundsAndVelocity,
	Collision,
	All
};

UENUM()
enum class EBulletHellDebugSpawnPattern : int32
{
	Line,
	Ring,
	Radial
};

UENUM()
enum class EBulletHellDebugArchetypeMode : int32
{
	Primary,
	Secondary,
	Alternating
};

UCLASS()
class UBulletHellDebugComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UBulletHellDebugComponent();
	~UBulletHellDebugComponent() override = default;

	void BeginPlay() override;
	void PostEditProperty(const char* PropertyName) override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	UFUNCTION(Callable, Category="Bullet Hell|Debug")
	int32 SpawnDebugPreset();

	UFUNCTION(Callable, Category="Bullet Hell|Debug")
	int32 SpawnDebugStressPreset();

	UFUNCTION(Callable, Category="Bullet Hell|Debug")
	bool KillRandomDebugBullet();

	UFUNCTION(Callable, Category="Bullet Hell|Debug")
	void LogFirstBulletDebugInfo() const;

	UFUNCTION(Callable, Category="Bullet Hell|Sample")
	int32 SpawnSampleBossPattern();

private:
	UBulletHellComponent* ResolveBulletHellComponent() const;
	int32 ApplyDebugRuntimeModifier();
	FBulletArchetype BuildDebugArchetype(int32 ArchetypeIndex) const;
	int32 ResolveDebugArchetypeIndex(int32 SpawnIndex) const;
	FBulletSpawnParams BuildSpawnParams(
		const FVector& Position,
		const FVector& Direction,
		int32 ArchetypeIndex,
		const FBulletArchetype& Archetype) const;
	void DrawBulletDebug();
	void DrawBulletCross(const FVector& Center, const FColor& Color, float Extent) const;
	void DrawDebugHomingTarget(UWorld* World) const;
	bool ShouldDrawDebugBounds() const;
	bool ShouldDrawDebugVelocity() const;
	bool ShouldDrawDebugCollision() const;
	FVector ResolveDebugSpawnOrigin() const;
	FVector ResolveDebugSpawnForward() const;
	FVector ResolveDebugSpawnRight() const;
	FVector ResolveDebugHomingTargetPosition() const;

private:
	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Auto Spawn Debug Preset")
	bool bAutoSpawnDebugPreset = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Draw Bullet Debug")
	bool bDrawBulletDebug = true;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Draw Mode", Enum=EBulletHellDebugDrawMode)
	EBulletHellDebugDrawMode DebugDrawMode = EBulletHellDebugDrawMode::All;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Visual Mode", Enum=EBulletHellDebugVisualMode)
	EBulletHellDebugVisualMode DebugVisualMode = EBulletHellDebugVisualMode::BoundsAndVelocity;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Highlighted Bullet Id", Min=0, Max=1000000, Speed=1)
	int32 HighlightedBulletId = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Draw Max Count", Min=0, Max=1000000, Speed=1)
	int32 DebugDrawMaxCount = 512;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Velocity Draw Scale", Min=1.0f, Max=100000.0f, Speed=1.0f)
	float DebugVelocityDrawScale = 100.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Draw Debug Homing Target")
	bool bDrawDebugHomingTarget = true;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Spawn Count", Min=0, Max=1000000, Speed=1)
	int32 DebugSpawnCount = 64;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Spawn Speed", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float DebugSpawnSpeed = 6.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Spawn Lifetime", Min=0.0f, Max=600.0f, Speed=0.1f)
	float DebugSpawnLifetime = 4.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Spawn Radius", Min=0.01f, Max=1000.0f, Speed=0.1f)
	float DebugSpawnRadius = 0.08f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Spawn Pattern", Enum=EBulletHellDebugSpawnPattern)
	EBulletHellDebugSpawnPattern DebugSpawnPattern = EBulletHellDebugSpawnPattern::Radial;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Archetype Mode", Enum=EBulletHellDebugArchetypeMode)
	EBulletHellDebugArchetypeMode DebugArchetypeMode = EBulletHellDebugArchetypeMode::Primary;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Group Id", Min=0, Max=1000000, Speed=1)
	int32 DebugGroupId = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Spawn Homing")
	bool bDebugSpawnHoming = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Homing Target Forward Offset", Min=-100000.0f, Max=100000.0f, Speed=1.0f)
	float DebugHomingTargetForwardOffset = 9.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Homing Target Right Offset", Min=-100000.0f, Max=100000.0f, Speed=1.0f)
	float DebugHomingTargetRightOffset = 4.5f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Homing Target Up Offset", Min=-100000.0f, Max=100000.0f, Speed=1.0f)
	float DebugHomingTargetUpOffset = 0.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Homing Strength", Min=0.0f, Max=100.0f, Speed=0.1f)
	float DebugHomingStrength = 6.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Homing Max Turn Rate", Min=0.0f, Max=3600.0f, Speed=1.0f)
	float DebugHomingMaxTurnRateDegrees = 360.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Homing Cone Half Angle", Min=0.0f, Max=180.0f, Speed=1.0f)
	float DebugHomingConeHalfAngleDegrees = 90.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Stress Spawn Count", Min=0, Max=1000000, Speed=1)
	int32 DebugStressSpawnCount = 5000;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug", DisplayName="Debug Stress Clear Before Spawn")
	bool bDebugStressClearBeforeSpawn = true;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Primary Mesh Path", AssetType="StaticMesh")
	FString PrimaryMeshPath = "Content/Data/BasicShape/Sphere.OBJ";

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Primary Material Path", AssetType="Material")
	FString PrimaryMaterialPath = "None";

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Primary Render Scale", Min=0.01f, Max=1000.0f, Speed=0.1f)
	float PrimaryRenderScale = 0.1f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Primary Damage", Min=0.0f, Max=1000000.0f, Speed=1.0f)
	float PrimaryDamage = 1.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Trail", DisplayName="Primary Trail Enabled")
	bool bPrimaryTrailEnabled = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Trail", DisplayName="Primary Trail Material Path", AssetType="Material")
	FString PrimaryTrailMaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Trail", DisplayName="Primary Trail Color", Type=Color4)
	FVector4 PrimaryTrailColor = FVector4(1.0f, 0.6f, 0.15f, 1.0f);

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Trail", DisplayName="Primary Trail Width", Min=0.001f, Max=100.0f, Speed=0.01f)
	float PrimaryTrailWidth = 0.08f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Trail", DisplayName="Primary Trail Lifetime", Min=0.001f, Max=60.0f, Speed=0.01f)
	float PrimaryTrailLifetime = 0.12f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Trail", DisplayName="Primary Trail Max Samples", Min=2, Max=1024, Speed=1)
	int32 PrimaryTrailMaxSamples = 8;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Trail", DisplayName="Primary Trail Sample Interval", Min=0.0f, Max=1.0f, Speed=0.001f)
	float PrimaryTrailSampleInterval = 0.02f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Trail", DisplayName="Primary Trail Min Sample Distance", Min=0.0f, Max=100.0f, Speed=0.01f)
	float PrimaryTrailMinSampleDistance = 0.05f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Death Effect", DisplayName="Primary Death Effect Enabled")
	bool bPrimaryDeathEffectEnabled = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Death Effect", DisplayName="Primary Death Effect Path", AssetType="UParticleSystem")
	FString PrimaryDeathEffectPath = "Content/Particle System/BossBulletDestroyVfx.uasset";

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Death Effect", DisplayName="Primary Death Effect Event Name")
	FName PrimaryDeathEffectEventName = FName("BulletDeath");

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Death Effect", DisplayName="Primary Death Effect Inherit Velocity")
	bool bPrimaryDeathEffectInheritVelocity = true;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Primary Death Effect", DisplayName="Primary Death Effect Velocity Scale", Min=-1000.0f, Max=1000.0f, Speed=0.01f)
	float PrimaryDeathEffectVelocityScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Secondary Mesh Path", AssetType="StaticMesh")
	FString SecondaryMeshPath = "Content/Data/BasicShape/Cube.OBJ";

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Secondary Material Path", AssetType="Material")
	FString SecondaryMaterialPath = "None";

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Secondary Radius", Min=0.01f, Max=1000.0f, Speed=0.1f)
	float SecondaryRadius = 0.08f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Secondary Speed", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float SecondarySpeed = 4.5f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Secondary Lifetime", Min=0.0f, Max=600.0f, Speed=0.1f)
	float SecondaryLifetime = 4.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Secondary Render Scale", Min=0.01f, Max=1000.0f, Speed=0.1f)
	float SecondaryRenderScale = 0.1f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Secondary Damage", Min=0.0f, Max=1000000.0f, Speed=1.0f)
	float SecondaryDamage = 2.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype", DisplayName="Secondary Homing")
	bool bSecondaryHoming = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Trail", DisplayName="Secondary Trail Enabled")
	bool bSecondaryTrailEnabled = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Trail", DisplayName="Secondary Trail Material Path", AssetType="Material")
	FString SecondaryTrailMaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Trail", DisplayName="Secondary Trail Color", Type=Color4)
	FVector4 SecondaryTrailColor = FVector4(0.3f, 0.8f, 1.0f, 1.0f);

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Trail", DisplayName="Secondary Trail Width", Min=0.001f, Max=100.0f, Speed=0.01f)
	float SecondaryTrailWidth = 0.08f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Trail", DisplayName="Secondary Trail Lifetime", Min=0.001f, Max=60.0f, Speed=0.01f)
	float SecondaryTrailLifetime = 0.12f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Trail", DisplayName="Secondary Trail Max Samples", Min=2, Max=1024, Speed=1)
	int32 SecondaryTrailMaxSamples = 8;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Trail", DisplayName="Secondary Trail Sample Interval", Min=0.0f, Max=1.0f, Speed=0.001f)
	float SecondaryTrailSampleInterval = 0.02f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Trail", DisplayName="Secondary Trail Min Sample Distance", Min=0.0f, Max=100.0f, Speed=0.01f)
	float SecondaryTrailMinSampleDistance = 0.05f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Death Effect", DisplayName="Secondary Death Effect Enabled")
	bool bSecondaryDeathEffectEnabled = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Death Effect", DisplayName="Secondary Death Effect Path", AssetType="UParticleSystem")
	FString SecondaryDeathEffectPath = "Content/Particle System/BossBulletDestroyVfx.uasset";

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Death Effect", DisplayName="Secondary Death Effect Event Name")
	FName SecondaryDeathEffectEventName = FName("BulletDeath");

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Death Effect", DisplayName="Secondary Death Effect Inherit Velocity")
	bool bSecondaryDeathEffectInheritVelocity = true;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Archetype|Secondary Death Effect", DisplayName="Secondary Death Effect Velocity Scale", Min=-1000.0f, Max=1000.0f, Speed=0.01f)
	float SecondaryDeathEffectVelocityScale = 1.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Commands", DisplayName="Debug Spawn Request", Min=0, Max=1000000, Speed=1)
	int32 DebugSpawnRequest = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Commands", DisplayName="Debug Stress Spawn Request", Min=0, Max=1000000, Speed=1)
	int32 DebugStressSpawnRequest = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Commands", DisplayName="Debug Random Kill Request", Min=0, Max=1000000, Speed=1)
	int32 DebugRandomKillRequest = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Commands", DisplayName="Debug Clear Request", Min=0, Max=1000000, Speed=1)
	int32 DebugClearRequest = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Commands", DisplayName="Debug Log First Bullet Request", Min=0, Max=1000000, Speed=1)
	int32 DebugLogFirstBulletRequest = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Commands", DisplayName="Debug Sample Boss Pattern Request", Min=0, Max=1000000, Speed=1)
	int32 DebugSampleBossPatternRequest = 0;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Archetype Index", Min=-1, Max=1000000, Speed=1)
	int32 DebugRuntimeModifierArchetypeIndex = -1;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Group Id", Min=-1, Max=1000000, Speed=1)
	int32 DebugRuntimeModifierGroupId = -1;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Only Homing")
	bool bDebugRuntimeModifierOnlyHoming = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Set Speed")
	bool bDebugRuntimeModifierSetSpeed = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Speed", Min=0.0f, Max=100000.0f, Speed=1.0f)
	float DebugRuntimeModifierSpeed = 9.0f;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Set Homing Cone")
	bool bDebugRuntimeModifierSetHomingCone = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Set Homing Enabled")
	bool bDebugRuntimeModifierSetHomingEnabled = false;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Homing Enabled")
	bool bDebugRuntimeModifierHomingEnabled = true;

	UPROPERTY(Edit, Save, Category="Bullet Hell|Debug Runtime Modifier", DisplayName="Runtime Modifier Apply Request", Min=0, Max=1000000, Speed=1)
	int32 DebugRuntimeModifierApplyRequest = 0;

	uint32 DebugKillRandomState = 0x9e3779b9u;
	int32 LastDebugSpawnRequest = 0;
	int32 LastDebugStressSpawnRequest = 0;
	int32 LastDebugRandomKillRequest = 0;
	int32 LastDebugClearRequest = 0;
	int32 LastDebugLogFirstBulletRequest = 0;
	int32 LastDebugSampleBossPatternRequest = 0;
	int32 LastDebugRuntimeModifierApplyRequest = 0;
};
