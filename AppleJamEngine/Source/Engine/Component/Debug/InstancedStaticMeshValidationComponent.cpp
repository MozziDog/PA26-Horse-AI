#include "Component/Debug/InstancedStaticMeshValidationComponent.h"

#include "Core/Logging/Log.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace
{
	float GetValidationAnimationPhase(const FTransform& Transform)
	{
		return Transform.Location.X * 0.173f + Transform.Location.Y * 0.227f;
	}
}

UInstancedStaticMeshValidationComponent::UInstancedStaticMeshValidationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UInstancedStaticMeshValidationComponent::BeginPlay()
{
	UInstancedStaticMeshComponent::BeginPlay();
	RebuildValidationInstances();
}

void UInstancedStaticMeshValidationComponent::PostEditProperty(const char* PropertyName)
{
	UInstancedStaticMeshComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "StressPreset") == 0 || std::strcmp(PropertyName, "Stress Preset") == 0)
	{
		ApplyStressPreset();
	}
	else if (std::strcmp(PropertyName, "GridCount") == 0 || std::strcmp(PropertyName, "Grid Count") == 0)
	{
		StressPreset = EISMValidationStressPreset::Custom;
	}

	if (ShouldRebuildForProperty(PropertyName))
	{
		RebuildValidationInstances();
	}
}

void UInstancedStaticMeshValidationComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	UInstancedStaticMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->bTickInEditor = true;
	}

	if (!bValidationTickToggle)
	{
		return;
	}

	if (GetInstanceCount() == 0 && GridCount > 0 && !bRandomRemovalToggle)
	{
		RebuildValidationInstances();
	}

	MaybeUpdateAnimatedInstances(DeltaTime);
	MaybeRemoveInstance(DeltaTime);
	if (bDebugDrawToggle)
	{
		DrawValidationDebug();
	}
}

void UInstancedStaticMeshValidationComponent::RebuildValidationInstances()
{
	ApplyValidationAssets();

	const int32 SafeCount = (std::max)(0, GridCount);
	TArray<FTransform> NewInstanceTransforms;
	NewInstanceTransforms.reserve(static_cast<size_t>(SafeCount));
	for (int32 Index = 0; Index < SafeCount; ++Index)
	{
		NewInstanceTransforms.push_back(MakeGridInstanceTransform(Index));
	}
	SetInstances(std::move(NewInstanceTransforms));

	UE_LOG(
		"ISM validation rebuild: requested=%d count=%d spacing=%.2f",
		SafeCount,
		GetInstanceCount(),
		Spacing);
}

void UInstancedStaticMeshValidationComponent::ApplyValidationAssets()
{
	if (!ValidationMeshPath.empty() && ValidationMeshPath != "None")
	{
		SetStaticMeshByPath(ValidationMeshPath);
	}

	if (!ValidationMaterialPath.empty() && ValidationMaterialPath != "None" && GetMaterialSlotCount() > 0)
	{
		SetMaterialByPath(0, ValidationMaterialPath);
	}
}

void UInstancedStaticMeshValidationComponent::DrawValidationDebug() const
{
	if (DebugDrawMode == EISMValidationDebugDrawMode::Off)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const bool bDrawAllMarkers = DebugDrawMode == EISMValidationDebugDrawMode::All;
	const bool bDrawSelectedMarker = DebugDrawMode == EISMValidationDebugDrawMode::Selected;
	if (bDrawAllMarkers || bDrawSelectedMarker)
	{
		const FMatrix ComponentWorldMatrix = GetWorldMatrix();
		const int32 Count = GetInstanceCount();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const bool bHighlighted = Index == HighlightedIndex;
			if (bDrawSelectedMarker && !bHighlighted)
			{
				continue;
			}

			FTransform InstanceTransform;
			if (!GetInstanceTransform(Index, InstanceTransform))
			{
				continue;
			}

			const FVector Center = (InstanceTransform.ToMatrix() * ComponentWorldMatrix).GetLocation();
			const FColor Color = bHighlighted ? FColor::Yellow() : FColor(0, 210, 255);
			DrawInstanceCross(Center, Color, bHighlighted ? 0.45f : 0.25f);
			DrawDebugSphere(World, Center, bHighlighted ? 0.18f : 0.10f, 8, Color, 0.0f);
		}
	}

	if (DebugDrawMode == EISMValidationDebugDrawMode::All ||
		DebugDrawMode == EISMValidationDebugDrawMode::Selected ||
		DebugDrawMode == EISMValidationDebugDrawMode::BoundsOnly)
	{
		const FBoundingBox Bounds = GetWorldBoundingBox();
		if (Bounds.IsValid())
		{
			DrawDebugBox(World, Bounds.GetCenter(), Bounds.GetExtent(), FColor(255, 160, 0), 0.0f);
		}
	}
}

void UInstancedStaticMeshValidationComponent::DrawInstanceCross(const FVector& Center, const FColor& Color, float Extent) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DrawDebugLine(World, Center - FVector(Extent, 0.0f, 0.0f), Center + FVector(Extent, 0.0f, 0.0f), Color, 0.0f);
	DrawDebugLine(World, Center - FVector(0.0f, Extent, 0.0f), Center + FVector(0.0f, Extent, 0.0f), Color, 0.0f);
	DrawDebugLine(World, Center - FVector(0.0f, 0.0f, Extent), Center + FVector(0.0f, 0.0f, Extent), Color, 0.0f);
}

FTransform UInstancedStaticMeshValidationComponent::MakeGridInstanceTransform(int32 InstanceIndex) const
{
	const int32 SafeCount = (std::max)(1, GridCount);
	const int32 GridSide = static_cast<int32>(std::ceil(std::sqrt(static_cast<float>(SafeCount))));
	const int32 X = InstanceIndex % GridSide;
	const int32 Y = InstanceIndex / GridSide;
	const float Half = static_cast<float>(GridSide - 1) * 0.5f;

	return FTransform(
		FVector((static_cast<float>(X) - Half) * Spacing, (static_cast<float>(Y) - Half) * Spacing, 0.0f),
		FRotator(0.0f, 0.0f, 0.0f),
		FVector(1.0f, 1.0f, 1.0f));
}

void UInstancedStaticMeshValidationComponent::MaybeUpdateAnimatedInstances(float DeltaTime)
{
	if (!bUpdateToggle)
	{
		return;
	}

	ElapsedTime += DeltaTime;
	const int32 Count = GetInstanceCount();
	TArray<FTransform> NewInstanceTransforms;
	NewInstanceTransforms.reserve(static_cast<size_t>(Count));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FTransform Transform;
		if (!GetInstanceTransform(Index, Transform))
		{
			continue;
		}

		Transform.Location.Z = std::sin(ElapsedTime * 2.0f + GetValidationAnimationPhase(Transform)) * 0.5f;
		NewInstanceTransforms.push_back(Transform);
	}
	SetInstances(std::move(NewInstanceTransforms));
}

void UInstancedStaticMeshValidationComponent::MaybeRemoveInstance(float DeltaTime)
{
	if (!bRandomRemovalToggle || GetInstanceCount() <= 0)
	{
		RemovalAccumulator = 0.0f;
		return;
	}

	RemovalAccumulator += DeltaTime;
	if (RemovalAccumulator < 0.25f)
	{
		return;
	}
	RemovalAccumulator = 0.0f;

	RemovalState = RemovalState * 1664525u + 1013904223u;
	const int32 RemoveIndex = static_cast<int32>(RemovalState % static_cast<uint32>(GetInstanceCount()));
	if (bUseSwapRemovalToggle)
	{
		const int32 MovedToIndex = RemoveInstanceSwap(RemoveIndex);
		UE_LOG(
			"ISM validation remove swap: removed=%d movedTo=%d count=%d",
			RemoveIndex,
			MovedToIndex,
			GetInstanceCount());
	}
	else
	{
		const bool bRemoved = RemoveInstanceStableForValidation(RemoveIndex);
		UE_LOG(
			"ISM validation remove stable: removed=%d success=%d count=%d",
			RemoveIndex,
			bRemoved ? 1 : 0,
			GetInstanceCount());
	}
}

bool UInstancedStaticMeshValidationComponent::RemoveInstanceStableForValidation(int32 InstanceIndex)
{
	const int32 Count = GetInstanceCount();
	if (InstanceIndex < 0 || InstanceIndex >= Count)
	{
		return false;
	}

	TArray<FTransform> NewInstanceTransforms;
	NewInstanceTransforms.reserve(static_cast<size_t>((std::max)(0, Count - 1)));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Index == InstanceIndex)
		{
			continue;
		}

		FTransform Transform;
		if (GetInstanceTransform(Index, Transform))
		{
			NewInstanceTransforms.push_back(Transform);
		}
	}

	SetInstances(std::move(NewInstanceTransforms));
	return true;
}

void UInstancedStaticMeshValidationComponent::ApplyStressPreset()
{
	switch (StressPreset)
	{
	case EISMValidationStressPreset::Count1K:
		GridCount = 1000;
		break;
	case EISMValidationStressPreset::Count10K:
		GridCount = 10000;
		break;
	case EISMValidationStressPreset::Count100K:
		GridCount = 100000;
		break;
	case EISMValidationStressPreset::Count1M:
		GridCount = 1000000;
		break;
	case EISMValidationStressPreset::Custom:
	default:
		break;
	}
}

bool UInstancedStaticMeshValidationComponent::ShouldRebuildForProperty(const char* PropertyName) const
{
	return std::strcmp(PropertyName, "ValidationMeshPath") == 0
		|| std::strcmp(PropertyName, "Mesh Path") == 0
		|| std::strcmp(PropertyName, "ValidationMaterialPath") == 0
		|| std::strcmp(PropertyName, "Material Path") == 0
		|| std::strcmp(PropertyName, "StressPreset") == 0
		|| std::strcmp(PropertyName, "Stress Preset") == 0
		|| std::strcmp(PropertyName, "GridCount") == 0
		|| std::strcmp(PropertyName, "Grid Count") == 0
		|| std::strcmp(PropertyName, "Spacing") == 0;
}
