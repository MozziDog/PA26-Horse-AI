#include "Render/Proxy/BulletTrailSceneProxy.h"

#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Render/Types/FrameContext.h"

#include <algorithm>
#include <vector>

namespace
{
	constexpr const char* BulletTrailFallbackMaterialPath = "Content/Material/Particle/ParticleBeamTrail.uasset";

	FVector SafeNormal(const FVector& Value, const FVector& Fallback)
	{
		return Value.IsNearlyZero() ? Fallback : Value.Normalized();
	}

	FVector ComputePointSide(const FVector& Position, const FVector& Tangent, const FVector& CameraPosition)
	{
		const FVector SegmentDirection = SafeNormal(Tangent, FVector::ForwardVector);
		FVector ViewDirection = CameraPosition - Position;
		if (ViewDirection.IsNearlyZero())
		{
			ViewDirection = FVector::UpVector;
		}

		FVector Side = SegmentDirection.Cross(ViewDirection);
		if (Side.IsNearlyZero())
		{
			Side = SegmentDirection.Cross(FVector::UpVector);
		}
		if (Side.IsNearlyZero())
		{
			Side = SegmentDirection.Cross(FVector::RightVector);
		}
		return SafeNormal(Side, FVector::RightVector);
	}

	void AppendChainGeometry(
		const FBulletTrailChain& Chain,
		const FVector& CameraPosition,
		std::vector<FParticleBeamTrailVertex>& OutVertices,
		std::vector<uint32>& OutIndices)
	{
		const uint32 PointCount = static_cast<uint32>(Chain.Points.size());
		if (PointCount < 2)
		{
			return;
		}

		std::vector<float> Distances(PointCount, 0.0f);
		for (uint32 PointIndex = 1; PointIndex < PointCount; ++PointIndex)
		{
			const FVector Delta = Chain.Points[PointIndex].Position - Chain.Points[PointIndex - 1].Position;
			Distances[PointIndex] = Distances[PointIndex - 1] + Delta.Length();
		}
		const float TotalDistance = Distances.back();
		if (TotalDistance <= 0.0001f)
		{
			return;
		}

		const uint32 BaseVertex = static_cast<uint32>(OutVertices.size());

		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			const FBulletTrailPoint& Point = Chain.Points[PointIndex];
			FVector Tangent = FVector::ForwardVector;
			if (PointIndex == 0)
			{
				Tangent = Chain.Points[1].Position - Point.Position;
			}
			else if (PointIndex + 1 == PointCount)
			{
				Tangent = Point.Position - Chain.Points[PointIndex - 1].Position;
			}
			else
			{
				Tangent = Chain.Points[PointIndex + 1].Position - Chain.Points[PointIndex - 1].Position;
			}

			const FVector Side = ComputePointSide(Point.Position, Tangent, CameraPosition)
				* ((std::max)(0.001f, Point.Width * 0.5f));
			const float U = Distances[PointIndex] / TotalDistance;

			FParticleBeamTrailVertex Left;
			Left.Position = Point.Position - Side;
			Left.Color = Point.Color;
			Left.UV = FVector2(U, 0.0f);

			FParticleBeamTrailVertex Right;
			Right.Position = Point.Position + Side;
			Right.Color = Point.Color;
			Right.UV = FVector2(U, 1.0f);

			OutVertices.push_back(Left);
			OutVertices.push_back(Right);
		}

		for (uint32 SegmentIndex = 0; SegmentIndex + 1 < PointCount; ++SegmentIndex)
		{
			if ((Chain.Points[SegmentIndex + 1].Position - Chain.Points[SegmentIndex].Position).IsNearlyZero())
			{
				continue;
			}

			const uint32 V0 = BaseVertex + SegmentIndex * 2;
			const uint32 V1 = V0 + 1;
			const uint32 V2 = V0 + 2;
			const uint32 V3 = V0 + 3;
			OutIndices.push_back(V0);
			OutIndices.push_back(V1);
			OutIndices.push_back(V2);
			OutIndices.push_back(V2);
			OutIndices.push_back(V1);
			OutIndices.push_back(V3);
		}
	}

	FVector ComputeSortPosition(const FBulletTrailSceneProxy::FBatch& Batch)
	{
		if (Batch.Chains.empty())
		{
			return FVector::ZeroVector;
		}

		FVector Sum = FVector::ZeroVector;
		uint32 PointCount = 0;
		for (const FBulletTrailChain& Chain : Batch.Chains)
		{
			for (const FBulletTrailPoint& Point : Chain.Points)
			{
				Sum += Point.Position;
				++PointCount;
			}
		}
		return PointCount > 0 ? Sum * (1.0f / static_cast<float>(PointCount)) : FVector::ZeroVector;
	}
}

FBulletTrailSceneProxy::FBulletTrailSceneProxy(UBulletTrailComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate
		| EPrimitiveProxyFlags::Particle
		| EPrimitiveProxyFlags::SkipOcclusion;
	ProxyFlags &= ~EPrimitiveProxyFlags::SupportsOutline;
	bCastShadow = false;
}

void FBulletTrailSceneProxy::AddReferencedObjects(FReferenceCollector& Collector)
{
	FPrimitiveSceneProxy::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(FallbackMaterial, "FBulletTrailSceneProxy::FallbackMaterial");
	for (FBatch& Batch : Batches)
	{
		Collector.AddReferencedObject(Batch.Material, "FBulletTrailSceneProxy::Batch.Material");
	}
}

void FBulletTrailSceneProxy::UpdateTransform()
{
	UPrimitiveComponent* Comp = GetOwner();
	if (Comp)
	{
		CachedWorldPos = Comp->GetWorldLocation();
		CachedBounds = Comp->GetWorldBoundingBox();
	}
	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(FMatrix::Identity);
	MarkPerObjectCBDirty();
}

void FBulletTrailSceneProxy::UpdateMesh()
{
	RebuildBatches();
}

void FBulletTrailSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	CachedCameraPosition = Frame.CameraPosition;
}

bool FBulletTrailSceneProxy::PrepareDrawBuffer(
	ID3D11Device* Device,
	ID3D11DeviceContext* Context,
	FDrawCommandBuffer& OutBuffer) const
{
	OutBuffer = {};
	if (!Device || !Context || Batches.empty())
	{
		return false;
	}

	if (BatchVBs.size() < Batches.size())
	{
		BatchVBs.resize(Batches.size());
	}
	if (BatchIBs.size() < Batches.size())
	{
		BatchIBs.resize(Batches.size());
	}

	TArray<FMeshSectionDraw>& MutableSections = const_cast<TArray<FMeshSectionDraw>&>(SectionDraws);
	MutableSections.clear();

	for (int32 BatchIndex = 0; BatchIndex < static_cast<int32>(Batches.size()); ++BatchIndex)
	{
		const FBatch& Batch = Batches[BatchIndex];
		if (Batch.Chains.empty())
		{
			continue;
		}

		std::vector<FParticleBeamTrailVertex> Vertices;
		std::vector<uint32> Indices;
		for (const FBulletTrailChain& Chain : Batch.Chains)
		{
			Vertices.reserve(Vertices.size() + Chain.Points.size() * 2);
			if (Chain.Points.size() > 1)
			{
				Indices.reserve(Indices.size() + (Chain.Points.size() - 1) * 6);
			}
			AppendChainGeometry(Chain, CachedCameraPosition, Vertices, Indices);
		}

		if (Vertices.empty() || Indices.empty())
		{
			continue;
		}

		std::unique_ptr<FDynamicVertexBuffer>& VB = BatchVBs[BatchIndex];
		std::unique_ptr<FDynamicIndexBuffer>& IB = BatchIBs[BatchIndex];
		if (!VB)
		{
			VB = std::make_unique<FDynamicVertexBuffer>();
		}
		if (!IB)
		{
			IB = std::make_unique<FDynamicIndexBuffer>();
		}

		const uint32 VertexCount = static_cast<uint32>(Vertices.size());
		const uint32 IndexCount = static_cast<uint32>(Indices.size());
		if (VB->GetStride() != sizeof(FParticleBeamTrailVertex) || !VB->GetBuffer())
		{
			VB->Create(Device, VertexCount, sizeof(FParticleBeamTrailVertex));
		}
		else
		{
			VB->EnsureCapacity(Device, VertexCount);
		}
		if (!VB->Update(Context, Vertices.data(), VertexCount))
		{
			continue;
		}

		IB->EnsureCapacity(Device, IndexCount);
		if (!IB->Update(Context, Indices.data(), IndexCount))
		{
			continue;
		}

		FMeshSectionDraw Section;
		Section.FirstIndex = 0;
		Section.IndexCount = IndexCount;
		Section.Material = Batch.Material ? Batch.Material : FallbackMaterial;
		Section.VertexFactory = EVertexFactoryType::ParticleRibbon;
		Section.bHasSortPos = true;
		Section.SortWorldPos = ComputeSortPosition(Batch);
		Section.BufferOverride.VB = VB->GetBuffer();
		Section.BufferOverride.VBStride = VB->GetStride();
		Section.BufferOverride.IB = IB->GetBuffer();
		MutableSections.push_back(Section);
	}

	return !MutableSections.empty();
}

UBulletTrailComponent* FBulletTrailSceneProxy::GetTrailComponent() const
{
	return static_cast<UBulletTrailComponent*>(GetOwner());
}

void FBulletTrailSceneProxy::RebuildBatches()
{
	Batches.clear();
	SectionDraws.clear();

	if (!FallbackMaterial)
	{
		FallbackMaterial = FMaterialManager::Get().GetOrCreateMaterial(BulletTrailFallbackMaterialPath);
	}
	if (FallbackMaterial)
	{
		SectionDraws.push_back({ FallbackMaterial, 0, 0 });
	}

	const UBulletTrailComponent* Component = GetTrailComponent();
	if (!Component)
	{
		return;
	}

	const TArray<FBulletTrailChain>& Chains = Component->GetTrailChains();
	for (const FBulletTrailChain& Chain : Chains)
	{
		if (Chain.Points.size() < 2)
		{
			continue;
		}

		const FString MaterialPath = (Chain.MaterialPath.empty() || Chain.MaterialPath == "None")
			? FString(BulletTrailFallbackMaterialPath)
			: Chain.MaterialPath;

		FBatch* TargetBatch = nullptr;
		for (FBatch& Batch : Batches)
		{
			if (Batch.MaterialPath == MaterialPath)
			{
				TargetBatch = &Batch;
				break;
			}
		}

		if (!TargetBatch)
		{
			FBatch NewBatch;
			NewBatch.MaterialPath = MaterialPath;
			NewBatch.Material = ResolveBatchMaterial(MaterialPath);
			Batches.push_back(NewBatch);
			TargetBatch = &Batches.back();
		}

		TargetBatch->Chains.push_back(Chain);
	}
}

UMaterial* FBulletTrailSceneProxy::ResolveBatchMaterial(const FString& MaterialPath) const
{
	if (MaterialPath.empty() || MaterialPath == "None")
	{
		return FallbackMaterial;
	}

	if (UMaterial* Material = FMaterialManager::Get().GetOrCreateMaterial(MaterialPath))
	{
		return Material;
	}
	return FallbackMaterial;
}
