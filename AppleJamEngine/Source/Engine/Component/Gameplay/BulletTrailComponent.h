#pragma once

#include "Component/PrimitiveComponent.h"
#include "Math/Vector.h"

#include "Source/Engine/Component/Gameplay/BulletTrailComponent.generated.h"

class FPrimitiveSceneProxy;

struct FBulletTrailPoint
{
	FVector4 Color = FVector4(1.0f, 0.6f, 0.15f, 1.0f);
	FVector Position = FVector::ZeroVector;
	float Width = 0.08f;
};

struct FBulletTrailChain
{
	FString MaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";
	TArray<FBulletTrailPoint> Points;
};

UCLASS()
class UBulletTrailComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UBulletTrailComponent() = default;
	~UBulletTrailComponent() override = default;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void UpdateWorldAABB() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;

	void SetTrailChains(TArray<FBulletTrailChain>&& NewChains);
	void ClearTrailChains();
	const TArray<FBulletTrailChain>& GetTrailChains() const { return Chains; }

private:
	TArray<FBulletTrailChain> Chains;
};
