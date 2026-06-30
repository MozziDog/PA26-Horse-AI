#pragma once

#include "Component/Gameplay/BulletTrailComponent.h"
#include "Particle/ParticleHelper.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

#include <memory>

class UMaterial;

class FBulletTrailSceneProxy : public FPrimitiveSceneProxy
{
public:
	struct FBatch
	{
		FString MaterialPath;
		UMaterial* Material = nullptr;
		TArray<FBulletTrailChain> Chains;
	};

	FBulletTrailSceneProxy(UBulletTrailComponent* InComponent);
	~FBulletTrailSceneProxy() override = default;

	void AddReferencedObjects(FReferenceCollector& Collector) override;
	void UpdateTransform() override;
	void UpdateMesh() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
		FDrawCommandBuffer& OutBuffer) const override;

private:
	UBulletTrailComponent* GetTrailComponent() const;
	void RebuildBatches();
	UMaterial* ResolveBatchMaterial(const FString& MaterialPath) const;

	mutable TArray<std::unique_ptr<FDynamicVertexBuffer>> BatchVBs;
	mutable TArray<std::unique_ptr<FDynamicIndexBuffer>> BatchIBs;
	TArray<FBatch> Batches;
	FVector CachedCameraPosition = FVector::ZeroVector;
	UMaterial* FallbackMaterial = nullptr;
};
