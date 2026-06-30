#include "Component/Gameplay/BulletTrailComponent.h"

#include "Render/Proxy/BulletTrailSceneProxy.h"

#include <algorithm>
#include <utility>

FPrimitiveSceneProxy* UBulletTrailComponent::CreateSceneProxy()
{
	return new FBulletTrailSceneProxy(this);
}

bool UBulletTrailComponent::LineTraceComponent(const FRay& /*Ray*/, FHitResult& /*OutHitResult*/)
{
	return false;
}

void UBulletTrailComponent::SetTrailChains(TArray<FBulletTrailChain>&& NewChains)
{
	Chains = std::move(NewChains);
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UBulletTrailComponent::ClearTrailChains()
{
	if (Chains.empty())
	{
		return;
	}

	Chains.clear();
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UBulletTrailComponent::UpdateWorldAABB() const
{
	if (Chains.empty())
	{
		UPrimitiveComponent::UpdateWorldAABB();
		bHasValidWorldAABB = false;
		return;
	}

	FBoundingBox Bounds;
	for (const FBulletTrailChain& Chain : Chains)
	{
		for (const FBulletTrailPoint& Point : Chain.Points)
		{
			const float Extent = (std::max)(0.001f, Point.Width * 0.5f);
			const FVector Padding(Extent, Extent, Extent);
			Bounds.Expand(Point.Position - Padding);
			Bounds.Expand(Point.Position + Padding);
		}
	}

	if (Bounds.IsValid())
	{
		WorldAABBMinLocation = Bounds.Min;
		WorldAABBMaxLocation = Bounds.Max;
		bWorldAABBDirty = false;
		bHasValidWorldAABB = true;
	}
	else
	{
		UPrimitiveComponent::UpdateWorldAABB();
		bHasValidWorldAABB = false;
	}
}
