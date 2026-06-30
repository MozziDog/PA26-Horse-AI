#pragma once

#include "Component/Primitive/InstancedStaticMeshComponent.h"

#include "Source/Engine/Component/Debug/InstancedStaticMeshValidationComponent.generated.h"

UENUM()
enum class EISMValidationStressPreset : int32
{
	Custom,
	Count1K,
	Count10K,
	Count100K,
	Count1M
};

UENUM()
enum class EISMValidationDebugDrawMode : int32
{
	Off,
	All,
	Selected,
	BoundsOnly
};

// Development-only validation harness for UInstancedStaticMeshComponent.
// Keep gameplay semantics out of this class; bullet-style systems should own
// lifetime/collision/state and feed transforms into UInstancedStaticMeshComponent.
UCLASS()
class UInstancedStaticMeshValidationComponent : public UInstancedStaticMeshComponent
{
public:
	GENERATED_BODY()
	UInstancedStaticMeshValidationComponent();
	~UInstancedStaticMeshValidationComponent() override = default;

	void BeginPlay() override;
	void PostEditProperty(const char* PropertyName) override;

	UFUNCTION(Callable, Category="Instanced Static Mesh Validation")
	void RebuildValidationInstances();

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	void ApplyValidationAssets();
	void DrawValidationDebug() const;
	void DrawInstanceCross(const FVector& Center, const FColor& Color, float Extent) const;
	FTransform MakeGridInstanceTransform(int32 InstanceIndex) const;
	void MaybeUpdateAnimatedInstances(float DeltaTime);
	void MaybeRemoveInstance(float DeltaTime);
	bool RemoveInstanceStableForValidation(int32 InstanceIndex);
	void ApplyStressPreset();
	bool ShouldRebuildForProperty(const char* PropertyName) const;

	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Mesh Path", AssetType="StaticMesh")
	FString ValidationMeshPath = "Content/Data/BasicShape/Cube.OBJ";
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Material Path", AssetType="Material")
	FString ValidationMaterialPath = "None";
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Stress Preset", Enum=EISMValidationStressPreset)
	EISMValidationStressPreset StressPreset = EISMValidationStressPreset::Custom;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Grid Count", Min=0, Max=1000000, Speed=1)
	int32 GridCount = 25;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Spacing", Min=0.1, Max=1000.0, Speed=0.1)
	float Spacing = 2.0f;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Validation Tick Toggle")
	bool bValidationTickToggle = true;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Debug Draw Toggle")
	bool bDebugDrawToggle = true;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Debug Draw Mode", Enum=EISMValidationDebugDrawMode)
	EISMValidationDebugDrawMode DebugDrawMode = EISMValidationDebugDrawMode::All;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Update Toggle")
	bool bUpdateToggle = false;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Random Removal Toggle")
	bool bRandomRemovalToggle = false;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Use Swap Removal Toggle")
	bool bUseSwapRemovalToggle = false;
	UPROPERTY(Edit, Save, Category="Instanced Static Mesh Validation", DisplayName="Highlighted Index", Min=-1, Max=10000, Speed=1)
	int32 HighlightedIndex = 0;

	float ElapsedTime = 0.0f;
	float RemovalAccumulator = 0.0f;
	uint32 RemovalState = 0x12345678u;
};
