#pragma once

#include "Component/Primitive/StaticMeshComponent.h"
#include "Math/Transform.h"

class FPrimitiveSceneProxy;

#include "Source/Engine/Component/Primitive/InstancedStaticMeshComponent.generated.h"

UCLASS()
class UInstancedStaticMeshComponent : public UStaticMeshComponent
{
public:
	GENERATED_BODY()
	UInstancedStaticMeshComponent() = default;
	~UInstancedStaticMeshComponent() override = default;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void UpdateWorldAABB() const override;
	bool LineTraceComponent(const FRay& Ray, FHitResult& OutHitResult) override;

	UFUNCTION(Callable, Category="Instancing")
	int32 AddInstance(const FTransform& InstanceTransform);
	UFUNCTION(Callable, Category="Instancing")
	bool UpdateInstanceTransform(int32 InstanceIndex, const FTransform& InstanceTransform);
	UFUNCTION(Callable, Category="Instancing")
	int32 RemoveInstanceSwap(int32 InstanceIndex);
	UFUNCTION(Callable, Category="Instancing")
	void ClearInstances();
	UFUNCTION(Pure, Category="Instancing")
	int32 GetInstanceCount() const { return static_cast<int32>(InstanceTransforms.size()); }
	UFUNCTION(Pure, Category="Instancing")
	FTransform GetInstanceTransform(int32 InstanceIndex) const;

	bool GetInstanceTransform(int32 InstanceIndex, FTransform& OutTransform) const;
	const TArray<FTransform>& GetInstanceTransforms() const { return InstanceTransforms; }
	void SetInstances(TArray<FTransform>&& NewInstanceTransforms);
	void ReserveInstances(int32 InstanceCount);

protected:
	void MarkInstancesDirty(bool bUpdatePartitionImmediately = false);
	bool IsValidInstanceIndex(int32 InstanceIndex) const;

private:
	void UpdatePartitionImmediately();
	void ExpandInstanceMeshBounds(FBoundingBox& Bounds, const FMatrix& InstanceWorldMatrix) const;

	TArray<FTransform> InstanceTransforms;
};
