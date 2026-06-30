#include "Render/Proxy/InstancedStaticMeshSceneProxy.h"

#include "Component/Primitive/InstancedStaticMeshComponent.h"
#include "Materials/Material.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"

#include <algorithm>

namespace
{
	bool SectionMaterialLess(const FMeshSectionDraw& A, const FMeshSectionDraw& B)
	{
		const uintptr_t AMat = reinterpret_cast<uintptr_t>(A.Material);
		const uintptr_t BMat = reinterpret_cast<uintptr_t>(B.Material);
		if (AMat != BMat)
		{
			return AMat < BMat;
		}

		return A.FirstIndex < B.FirstIndex;
	}

	FInstancedStaticMeshSceneProxy::FInstanceVertex MakeInstanceVertex(const FTransform& Transform)
	{
		const FMatrix Matrix = Transform.ToMatrix();
		FInstancedStaticMeshSceneProxy::FInstanceVertex Vertex;
		Vertex.Row0 = FVector4(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2], Matrix.M[0][3]);
		Vertex.Row1 = FVector4(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2], Matrix.M[1][3]);
		Vertex.Row2 = FVector4(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2], Matrix.M[2][3]);
		Vertex.Row3 = FVector4(Matrix.M[3][0], Matrix.M[3][1], Matrix.M[3][2], Matrix.M[3][3]);
		Vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		return Vertex;
	}
}

FInstancedStaticMeshSceneProxy::FInstancedStaticMeshSceneProxy(UInstancedStaticMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::StaticMesh
		| EPrimitiveProxyFlags::InstancedStaticMesh
		| EPrimitiveProxyFlags::SkipOcclusion;
}

void FInstancedStaticMeshSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
}

void FInstancedStaticMeshSceneProxy::UpdateMesh()
{
	SnapshotInstances();
	RebuildSectionDraws();
}

bool FInstancedStaticMeshSceneProxy::PrepareDrawBuffer(
	ID3D11Device* Device,
	ID3D11DeviceContext* Context,
	FDrawCommandBuffer& OutBuffer) const
{
	OutBuffer = {};
	if (!Device || !Context || InstanceVertices.empty())
	{
		return false;
	}

	const uint32 RequiredInstanceCount = static_cast<uint32>(InstanceVertices.size());
	if (InstanceBuffer.GetStride() == 0 || InstanceBuffer.GetMaxCount() == 0)
	{
		InstanceBuffer.Create(Device, RequiredInstanceCount, sizeof(FInstanceVertex));
		bInstanceBufferDirty = true;
	}
	else
	{
		const bool bNeedsGrow = RequiredInstanceCount > InstanceBuffer.GetMaxCount();
		InstanceBuffer.EnsureCapacity(Device, RequiredInstanceCount);
		if (bNeedsGrow)
		{
			bInstanceBufferDirty = true;
		}
	}
	if (bInstanceBufferDirty)
	{
		if (!InstanceBuffer.Update(Context, InstanceVertices.data(), static_cast<uint32>(InstanceVertices.size())))
		{
			return false;
		}
		bInstanceBufferDirty = false;
	}

	RefreshSectionBufferOverrides();
	return true;
}

UInstancedStaticMeshComponent* FInstancedStaticMeshSceneProxy::GetInstancedStaticMeshComponent() const
{
	return static_cast<UInstancedStaticMeshComponent*>(GetOwner());
}

void FInstancedStaticMeshSceneProxy::RebuildSectionDraws()
{
	UInstancedStaticMeshComponent* ISMC = GetInstancedStaticMeshComponent();
	UStaticMesh* Mesh = ISMC ? ISMC->GetStaticMesh() : nullptr;
	if (!Mesh || !Mesh->GetStaticMeshAsset() || InstanceVertices.empty())
	{
		MeshBuffer = nullptr;
		SectionDraws.clear();
		return;
	}

	MeshBuffer = Mesh->GetLODMeshBuffer(0);
	if (!MeshBuffer || !MeshBuffer->IsValid())
	{
		SectionDraws.clear();
		return;
	}

	const auto& Slots = Mesh->GetStaticMaterials();
	const auto& Overrides = ISMC->GetOverrideMaterials();
	const auto& Sections = Mesh->GetLODSections(0);

	SectionDraws.clear();
	SectionDraws.reserve(Sections.size());

	for (const FStaticMeshSection& Section : Sections)
	{
		FMeshSectionDraw Draw;
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.NumTriangles * 3;
		Draw.VertexFactory = EVertexFactoryType::InstancedStaticMesh;
		// ISMC transparent sorting is proxy-level only. Per-instance sorting would require
		// breaking instancing or camera-sorting the instance buffer every frame.

		const int32 MaterialIndex = Section.MaterialIndex;
		if (MaterialIndex >= 0 && MaterialIndex < static_cast<int32>(Slots.size()))
		{
			if (MaterialIndex < static_cast<int32>(Overrides.size()) && IsValid(Overrides[MaterialIndex]))
			{
				Draw.Material = Overrides[MaterialIndex];
			}
			else if (IsValid(Slots[MaterialIndex].MaterialInterface))
			{
				Draw.Material = Slots[MaterialIndex].MaterialInterface;
			}
		}

		SectionDraws.push_back(Draw);
	}

	if (SectionDraws.size() > 1)
	{
		std::sort(SectionDraws.begin(), SectionDraws.end(), SectionMaterialLess);
	}
}

void FInstancedStaticMeshSceneProxy::SnapshotInstances()
{
	UInstancedStaticMeshComponent* ISMC = GetInstancedStaticMeshComponent();
	InstanceVertices.clear();
	if (!ISMC)
	{
		bInstanceBufferDirty = true;
		return;
	}

	const TArray<FTransform>& SourceTransforms = ISMC->GetInstanceTransforms();
	InstanceVertices.reserve(SourceTransforms.size());
	for (const FTransform& Transform : SourceTransforms)
	{
		InstanceVertices.push_back(MakeInstanceVertex(Transform));
	}

	bInstanceBufferDirty = true;
}

void FInstancedStaticMeshSceneProxy::RefreshSectionBufferOverrides() const
{
	if (!MeshBuffer || !MeshBuffer->IsValid() || !InstanceBuffer.GetBuffer() || InstanceVertices.empty())
	{
		return;
	}

	TArray<FMeshSectionDraw>& MutableSectionDraws = const_cast<TArray<FMeshSectionDraw>&>(SectionDraws);
	for (FMeshSectionDraw& Section : MutableSectionDraws)
	{
		Section.BufferOverride = {};
		Section.BufferOverride.VB = MeshBuffer->GetVertexBuffer().GetBuffer();
		Section.BufferOverride.VBStride = MeshBuffer->GetVertexBuffer().GetStride();
		Section.BufferOverride.IB = MeshBuffer->GetIndexBuffer().GetBuffer();
		Section.BufferOverride.InstanceCount = static_cast<uint32>(InstanceVertices.size());
		Section.BufferOverride.InstanceVB = InstanceBuffer.GetBuffer();
		Section.BufferOverride.InstanceVBStride = InstanceBuffer.GetStride();
	}
}
