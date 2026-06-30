#pragma once
#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "GameFramework/IPoolableProjectile.h"   

class UParticleSystemComponent;
class UStaticMeshComponent;
#include "Source/Engine/GameFramework/Actor/ProjectileActor.generated.h"


UCLASS()
class AProjectileActor : public AActor, public IPoolableProjectile
{
public:
	GENERATED_BODY()
	AProjectileActor() = default;
	
	void InitDefaultComponents();
	void PostDuplicate() override;
	void BeginPlay() override;
	void Tick(float DeltaTime) override;   // 직진 이동 + 수명 만료 시 자가 Release

	// ── IPoolableProjectile ─────────────────────────────────────
	AActor* AsActor() override { return this; }
	void OnPoolConstruct() override;
	void Activate(const FVector& Location, const FVector& Velocity) override;
	void Deactivate() override;
	void ResetState() override;

	UFUNCTION(Pure, Category = "Actor|Components")
	UStaticMeshComponent* GetStaticComponent() const { return StaticMeshComponent; }
	
	UFUNCTION(Pure, Category = "Actor|Components")
	UParticleSystemComponent* GetParticleSystemComponent() const { return ParticleSystemComponent; }


private:
	TWeakObjectPtr<UStaticMeshComponent> StaticMeshComponent = nullptr;
	TWeakObjectPtr<UParticleSystemComponent> ParticleSystemComponent = nullptr;


	// ── 풀링/비행 상태 ──────────────────────────────────────────
	bool    bPoolConstructed = false;   // OnPoolConstruct 1회 가드
	FVector CachedVelocity = FVector::ZeroVector;
	float   LifeTimeRemaining = 0.0f;

	UPROPERTY(Edit, Save, Category = "Projectile", DisplayName = "Life Time")
	float DefaultLifeTime = 3.0f;
};