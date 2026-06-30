#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

class UInstancedStaticMeshComponent;

class FInstancedStaticMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	struct FInstanceVertex
	{
		FVector4 Row0;
		FVector4 Row1;
		FVector4 Row2;
		FVector4 Row3;
		FVector4 Color;
	};

	FInstancedStaticMeshSceneProxy(UInstancedStaticMeshComponent* InComponent);
	~FInstancedStaticMeshSceneProxy() override = default;

	void UpdateMaterial() override;
	void UpdateMesh() override;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
		FDrawCommandBuffer& OutBuffer) const override;

private:
	UInstancedStaticMeshComponent* GetInstancedStaticMeshComponent() const;
	void RebuildSectionDraws();
	void SnapshotInstances();
	void RefreshSectionBufferOverrides() const;

	TArray<FInstanceVertex> InstanceVertices;
	mutable FDynamicVertexBuffer InstanceBuffer;
	mutable bool bInstanceBufferDirty = true;
};
