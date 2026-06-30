#pragma once

#include "GameFramework/Actor/ProjectileActor.h"
#include "Math/Rotator.h"
#include "Object/Ptr/WeakObjectPtr.h"

class UStaticMeshComponent;
class UParticleSystemComponent;
struct FHitResult;

#include "Source/Engine/GameFramework/Actor/ArrowProjectileActor.generated.h"

UCLASS()
class AArrowProjectileActor : public AProjectileActor
{
public:
	GENERATED_BODY()
	AArrowProjectileActor() = default;

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	AActor* AsActor() override { return this; }
	void OnPoolConstruct() override;
	void Activate(const FVector& Location, const FVector& Velocity) override;
	void Deactivate() override;
	void ResetState() override;

	void HoldAt(const FVector& Location, const FVector& AimDirection);
	void Launch(const FVector& Velocity);

private:
	void InitArrowComponents();
	UParticleSystemComponent* EnsureArrowParticleComponent(
		TWeakObjectPtr<UParticleSystemComponent>& Component,
		const char* ComponentName,
		const char* TemplatePath);
	void SetAimParticleActive(bool bActive);
	void SetFireArrowParticleActive(bool bActive);
	bool CheckKinematicCollision(const FVector& PreviousLocation, const FVector& CurrentLocation, FHitResult& OutHit) const;
	bool CheckBossPhysicsAssetHit(AActor* BossActor, const FVector& SegmentStart, const FVector& SegmentEnd, float SweepRadius, FHitResult& OutHit) const;
	bool FindBossPhysicsAssetHit(const FVector& SegmentStart, const FVector& SegmentEnd, float SweepRadius, FHitResult& OutHit) const;
	void ApplyDamageToHitTarget(const FHitResult& Hit) const;
	void PlayBossHitStop(AActor* TargetActor) const;
	void PlayBossHitCameraShake() const;
	void EmitDeathEffect(const FVector& Location, const FVector& Velocity) const;
	void ReleaseToPoolOrDeactivate();

private:
	TWeakObjectPtr<UStaticMeshComponent> StaticMeshComponent = nullptr;
	TWeakObjectPtr<UParticleSystemComponent> AimParticleComponent = nullptr;
	TWeakObjectPtr<UParticleSystemComponent> FireArrowParticleComponent = nullptr;

	bool    bPoolConstructed = false;
	bool    bHeld = false;
	bool    bAimParticleRequested = false;
	bool    bFireArrowParticleRequested = false;
	int32   AimParticleRefreshCounter = 0;
	FVector CachedVelocity = FVector::ZeroVector;
	float   LifeTimeRemaining = 0.0f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile", DisplayName = "Life Time")
	float DefaultLifeTime = 5.0f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile", DisplayName = "Projectile Radius", Min = 0.001f, Max = 100.0f, Speed = 0.01f)
	float ProjectileRadius = 0.08f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile", DisplayName = "Projectile Damage", Min = 0.0f, Max = 1000000.0f, Speed = 1.0f)
	float ProjectileDamage = 10.0f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile", DisplayName = "Gravity Acceleration", Min = 0.0f, Max = 1000.0f, Speed = 0.1f)
	float GravityAcceleration = 9.8f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Hit Stop", DisplayName = "Boss Hit Stop Duration", Min = 0.0f, Max = 1.0f, Speed = 0.01f)
	float BossHitStopDuration = 0.1f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Hit Stop", DisplayName = "Boss Hit Stop Time Dilation", Min = 0.0f, Max = 1.0f, Speed = 0.01f)
	float BossHitStopTimeDilation = 0.02f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Hit Stop", DisplayName = "Boss Slomo Duration", Min = 0.0f, Max = 2.0f, Speed = 0.01f)
	float BossSlomoDuration = 0.6f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Hit Stop", DisplayName = "Boss Slomo Time Dilation", Min = 0.0f, Max = 1.0f, Speed = 0.01f)
	float BossSlomoTimeDilation = 0.1f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Camera Shake", DisplayName = "Boss Camera Shake Scale", Min = 0.0f, Max = 10.0f, Speed = 0.01f)
	float BossCameraShakeScale = 1.0f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Camera Shake", DisplayName = "Boss Camera Shake Duration", Min = 0.0f, Max = 2.0f, Speed = 0.01f)
	float BossCameraShakeDuration = 0.18f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Camera Shake", DisplayName = "Boss Camera Shake Blend Out", Min = 0.0f, Max = 1.0f, Speed = 0.01f)
	float BossCameraShakeBlendOut = 0.08f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Camera Shake", DisplayName = "Boss Camera Shake Location Amplitude")
	FVector BossCameraShakeLocationAmplitude = FVector(0.04f, 0.04f, 0.025f);

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Camera Shake", DisplayName = "Boss Camera Shake Rotation Amplitude")
	FRotator BossCameraShakeRotationAmplitude = FRotator(0.8f, 1.0f, 1.2f);

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Camera Shake", DisplayName = "Boss Camera Shake FOV Amplitude", Min = 0.0f, Max = 1.0f, Speed = 0.001f)
	float BossCameraShakeFOVAmplitude = 0.01f;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Death Effect", DisplayName = "Death Effect Path", AssetType = "UParticleSystem")
	FString DeathEffectPath = "Content/Particle System/BossBulletDestroyVfx.uasset";

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Death Effect", DisplayName = "Death Effect Event Name")
	FName DeathEffectEventName = FName("BulletDeath");

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Death Effect", DisplayName = "Inherit Projectile Velocity")
	bool bDeathEffectInheritVelocity = true;

	UPROPERTY(Edit, Save, Category = "Arrow Projectile|Death Effect", DisplayName = "Death Effect Velocity Scale", Min = 0.0f, Max = 100.0f, Speed = 0.1f)
	float DeathEffectVelocityScale = 1.0f;
};
