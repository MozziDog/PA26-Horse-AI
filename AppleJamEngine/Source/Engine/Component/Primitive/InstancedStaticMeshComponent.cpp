#include "Component/Primitive/InstancedStaticMeshComponent.h"

#include "Collision/Ray/RayUtils.h"
#include "Collision/Octree/SpatialPartition.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Render/Proxy/DirtyFlag.h"
#include "Render/Proxy/InstancedStaticMeshSceneProxy.h"

#include <algorithm>
#include <cmath>
#include <utility>

FPrimitiveSceneProxy* UInstancedStaticMeshComponent::CreateSceneProxy()
{
	return new FInstancedStaticMeshSceneProxy(this);
}

int32 UInstancedStaticMeshComponent::AddInstance(const FTransform& InstanceTransform)
{
	InstanceTransforms.push_back(InstanceTransform);
	MarkInstancesDirty(true);
	return static_cast<int32>(InstanceTransforms.size() - 1);
}

bool UInstancedStaticMeshComponent::UpdateInstanceTransform(int32 InstanceIndex, const FTransform& InstanceTransform)
{
	if (!IsValidInstanceIndex(InstanceIndex))
	{
		return false;
	}

	InstanceTransforms[InstanceIndex] = InstanceTransform;
	MarkInstancesDirty();
	return true;
}

int32 UInstancedStaticMeshComponent::RemoveInstanceSwap(int32 InstanceIndex)
{
	if (!IsValidInstanceIndex(InstanceIndex))
	{
		return -1;
	}

	const int32 LastIndex = static_cast<int32>(InstanceTransforms.size() - 1);
	int32 MovedToIndex = -1;
	if (InstanceIndex != LastIndex)
	{
		InstanceTransforms[InstanceIndex] = InstanceTransforms[LastIndex];
		MovedToIndex = InstanceIndex;
	}

	InstanceTransforms.pop_back();
	MarkInstancesDirty(true);
	return MovedToIndex;
}

void UInstancedStaticMeshComponent::ClearInstances()
{
	if (InstanceTransforms.empty())
	{
		return;
	}

	InstanceTransforms.clear();
	MarkInstancesDirty(true);
}

void UInstancedStaticMeshComponent::SetInstances(TArray<FTransform>&& NewInstanceTransforms)
{
	InstanceTransforms = std::move(NewInstanceTransforms);
	MarkInstancesDirty(true);
}

void UInstancedStaticMeshComponent::ReserveInstances(int32 InstanceCount)
{
	if (InstanceCount > 0)
	{
		InstanceTransforms.reserve(static_cast<size_t>(InstanceCount));
	}
}

FTransform UInstancedStaticMeshComponent::GetInstanceTransform(int32 InstanceIndex) const
{
	FTransform Result;
	GetInstanceTransform(InstanceIndex, Result);
	return Result;
}

bool UInstancedStaticMeshComponent::GetInstanceTransform(int32 InstanceIndex, FTransform& OutTransform) const
{
	if (!IsValidInstanceIndex(InstanceIndex))
	{
		OutTransform = FTransform();
		return false;
	}

	OutTransform = InstanceTransforms[InstanceIndex];
	return true;
}

bool UInstancedStaticMeshComponent::LineTraceComponent(const FRay& /*Ray*/, FHitResult& /*OutHitResult*/)
{
	// Per-instance picking is explicitly out of scope for the MVP.
	return false;
}

void UInstancedStaticMeshComponent::UpdateWorldAABB() const
{
	if (InstanceTransforms.empty())
	{
		UPrimitiveComponent::UpdateWorldAABB();
		bHasValidWorldAABB = false;
		return;
	}

	UStaticMesh* StaticMesh = GetStaticMesh();
	if (!StaticMesh)
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	FStaticMesh* Asset = StaticMesh->GetStaticMeshAsset();
	if (!Asset || Asset->Vertices.empty())
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	if (!Asset->bBoundsValid)
	{
		Asset->CacheBounds();
	}

	if (!Asset->bBoundsValid)
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	const FMatrix ComponentWorldMatrix = GetWorldMatrix();
	FBoundingBox CombinedBounds;
	for (const FTransform& InstanceTransform : InstanceTransforms)
	{
		const FMatrix InstanceWorldMatrix = InstanceTransform.ToMatrix() * ComponentWorldMatrix;
		ExpandInstanceMeshBounds(CombinedBounds, InstanceWorldMatrix);
	}

	if (CombinedBounds.IsValid())
	{
		WorldAABBMinLocation = CombinedBounds.Min;
		WorldAABBMaxLocation = CombinedBounds.Max;
		bWorldAABBDirty = false;
		bHasValidWorldAABB = true;
	}
	else
	{
		UPrimitiveComponent::UpdateWorldAABB();
		bHasValidWorldAABB = false;
	}
}

void UInstancedStaticMeshComponent::MarkInstancesDirty(bool bUpdatePartitionImmediately)
{
	MarkWorldBoundsDirty();
	MarkTransformDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);

	if (bUpdatePartitionImmediately)
	{
		UpdatePartitionImmediately();
	}
}

void UInstancedStaticMeshComponent::UpdatePartitionImmediately()
{
	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		return;
	}

	UWorld* World = OwnerActor->GetWorld();
	if (!World)
	{
		return;
	}

	World->GetPartition().UpdatePrimitiveImmediate(this);
	World->MarkWorldPrimitivePickingBVHDirty();
}

bool UInstancedStaticMeshComponent::IsValidInstanceIndex(int32 InstanceIndex) const
{
	return InstanceIndex >= 0 && InstanceIndex < static_cast<int32>(InstanceTransforms.size());
}

void UInstancedStaticMeshComponent::ExpandInstanceMeshBounds(FBoundingBox& Bounds, const FMatrix& InstanceWorldMatrix) const
{
	UStaticMesh* StaticMesh = GetStaticMesh();
	if (!StaticMesh)
	{
		return;
	}

	FStaticMesh* Asset = StaticMesh->GetStaticMeshAsset();
	if (!Asset || !Asset->bBoundsValid)
	{
		return;
	}

	const FVector LocalMin = Asset->BoundsCenter - Asset->BoundsExtent;
	const FVector LocalMax = Asset->BoundsCenter + Asset->BoundsExtent;
	const FVector Corners[8] =
	{
		FVector(LocalMin.X, LocalMin.Y, LocalMin.Z),
		FVector(LocalMax.X, LocalMin.Y, LocalMin.Z),
		FVector(LocalMin.X, LocalMax.Y, LocalMin.Z),
		FVector(LocalMax.X, LocalMax.Y, LocalMin.Z),
		FVector(LocalMin.X, LocalMin.Y, LocalMax.Z),
		FVector(LocalMax.X, LocalMin.Y, LocalMax.Z),
		FVector(LocalMin.X, LocalMax.Y, LocalMax.Z),
		FVector(LocalMax.X, LocalMax.Y, LocalMax.Z),
	};

	for (const FVector& Corner : Corners)
	{
		Bounds.Expand(InstanceWorldMatrix.TransformPositionWithW(Corner));
	}
}
