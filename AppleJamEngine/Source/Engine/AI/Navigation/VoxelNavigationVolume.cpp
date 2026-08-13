#include "pch.h"

#include "AI/Navigation/VoxelNavigationVolume.h"

#include "Component/Shape/BoxComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Types/CollisionTypes.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/World.h"
#include "Serialization/Archive.h"

void AVoxelNavigationVolume::InitDefaultComponents(const FVector& Extent)
{
	VolumeBox = AddComponent<UBoxComponent>();
	SetRootComponent(VolumeBox.Get());
	if (VolumeBox)
	{
		VolumeBox->SetBoxExtent(Extent);
		VolumeBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		VolumeBox->SetGenerateOverlapEvents(false);
		VolumeBox->SetSimulatePhysics(false);
	}
}

void AVoxelNavigationVolume::BeginPlay()
{
	RebindComponents();
	Super::BeginPlay();
	if (!ReferenceDataPath.empty() && !InitializeStreamingNavigation())
	{
		UE_LOG("[VoxelNavigation] Failed to initialize streaming navigation asset: %s", ReferenceDataPath.c_str());
	}
}

void AVoxelNavigationVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDrawWalkableNodes || bDrawChunkBoundaries)
	{
		DrawNavigationDebug();
	}
}

void AVoxelNavigationVolume::PostDuplicate()
{
	Super::PostDuplicate();
	RebindComponents();
}

void AVoxelNavigationVolume::OnPostLoad(FArchive& Ar)
{
	Super::OnPostLoad(Ar);
	RebindComponents();
}

const FVoxelNavigationBuildSettings AVoxelNavigationVolume::GetNavigationBuildSettings() const
{
	FVoxelNavigationBuildSettings Settings;
	Settings.AgentRadius = AgentRadius;
	Settings.AgentHeight = AgentHeight;
	Settings.MaxWalkableSlopeDegrees = MaxWalkableSlopeDegrees;
	Settings.MaxNeighborHeightDelta = MaxNeighborHeightDelta;
	return Settings;
}

bool AVoxelNavigationVolume::GetNavigationBakeInput(
	FVector& OutCenter,
	FVector& OutExtent,
	FVoxelNavigationBuildSettings& OutSettings) const
{
	const UBoxComponent* Box = VolumeBox.Get();
	if (!Box) return false;
	const FRotator VolumeRotation = GetActorRotation();
	if (std::abs(VolumeRotation.Pitch) > 0.01f || std::abs(VolumeRotation.Yaw) > 0.01f || std::abs(VolumeRotation.Roll) > 0.01f)
	{
		return false;
	}
	OutCenter = GetActorLocation();
	OutExtent = Box->GetScaledBoxExtent();
	OutSettings = GetNavigationBuildSettings();
	return true;
}

void AVoxelNavigationVolume::ApplyNavigationBakeResult(const FString& AssetPath, const FVoxelNavigationBuildStats& Stats)
{
	ReferenceDataPath = AssetPath;
	DebugSampledCells = static_cast<int32>(Stats.NumSampledCells);
	DebugWalkableNodes = static_cast<int32>(Stats.NumWalkableNodes);
	DebugDirectedEdges = static_cast<int32>(Stats.NumDirectedEdges);
	DebugRawWalkableNodes = static_cast<int32>(Stats.NumRawWalkableNodes);
	DebugErodedNodes = static_cast<int32>(Stats.NumErodedNodes);
	DebugBuiltL1Chunks = static_cast<int32>(Stats.NumBuiltL1Chunks);
	DebugAbstractNodes = static_cast<int32>(Stats.NumAbstractNodes);
	DebugAbstractEdges = static_cast<int32>(Stats.NumAbstractEdges);
	DebugRejectedSlope = static_cast<int32>(Stats.NumRejectedSlope);
	DebugRejectedClearance = static_cast<int32>(Stats.NumRejectedClearance);
	DebugBuildTimeMs = Stats.BuildTimeMs;
	DebugPeakMemoryMB = static_cast<float>(Stats.PeakMemoryBytes) / (1024.0f * 1024.0f);
	DebugRuntimeMemoryMB = static_cast<float>(Stats.RuntimeMemoryBytes) / (1024.0f * 1024.0f);
	DebugPeakBakeScratchMemoryMB = static_cast<float>(Stats.PeakBakeScratchMemoryBytes) / (1024.0f * 1024.0f);

}

bool AVoxelNavigationVolume::LoadNavigationAsset(const FString& InputPath)
{
	auto LoadedCatalog = std::make_shared<FNavigationAssetCatalog>();
	TArray<FBakedVoxelNavigationChunk> LoadedChunks;
	const bool bSuccess = LoadedCatalog->Open(InputPath) && LoadedCatalog->ValidateCompleteAsset() &&
		LoadedCatalog->ReadAllChunks(LoadedChunks) &&
		Grid.InitializeRuntime(LoadedCatalog->GetInfo().BoundsCenter, LoadedCatalog->GetInfo().BoundsExtent, LoadedCatalog->GetInfo().Settings) &&
		Grid.AddLoadedChunks(LoadedChunks);
	if (bSuccess)
	{
		ReferenceDataPath = InputPath;
		NavigationAssetCatalog = std::move(LoadedCatalog);
	}
	return bSuccess;
}

bool AVoxelNavigationVolume::InitializeStreamingNavigation()
{
	if (ReferenceDataPath.empty()) return false;
	if (IsStreamingNavigationInitialized()) return true;
	auto LoadedCatalog = std::make_shared<FNavigationAssetCatalog>();
	if (!LoadedCatalog->Open(ReferenceDataPath) || !LoadedCatalog->ValidateCompleteAsset()) return false;
	const FVoxelNavigationAssetInfo& AssetInfo = LoadedCatalog->GetInfo();
	if (!Grid.InitializeRuntime(AssetInfo.BoundsCenter, AssetInfo.BoundsExtent, AssetInfo.Settings)) return false;
	NavigationAssetCatalog = std::move(LoadedCatalog);
	return true;
}

bool AVoxelNavigationVolume::PublishStreamingNavigationChunks(const TArray<FBakedVoxelNavigationChunk>& LoadedChunks)
{
	return Grid.AddLoadedChunks(LoadedChunks);
}

bool AVoxelNavigationVolume::UnloadStreamingNavigationChunks(const TArray<FVoxelCoord>& ChunkCoords)
{
	return Grid.RemoveLoadedChunks(ChunkCoords);
}

void AVoxelNavigationVolume::ClearNavigationData()
{
	Grid.ClearNavigationData();
}

FVector AVoxelNavigationVolume::GetNavigationChunkCenter(const FVoxelCoord& Coord) const
{
	return NavigationAssetCatalog ? NavigationAssetCatalog->GetChunkCenter(Coord) : FVector::ZeroVector;
}

bool AVoxelNavigationVolume::Contains(const FVector& Point) const
{
	if (Grid.IsBuilt()) return Grid.Contains(Point);
	const UBoxComponent* Box = VolumeBox.Get();
	if (!Box) return false;
	const FVector Extent = Box->GetScaledBoxExtent();
	const FVector Min = GetActorLocation() - Extent;
	const FVector Max = GetActorLocation() + Extent;
	return Point.X >= Min.X && Point.X <= Max.X &&
		Point.Y >= Min.Y && Point.Y <= Max.Y &&
		Point.Z >= Min.Z && Point.Z <= Max.Z;
}

FVoxelNavigationPathResult AVoxelNavigationVolume::FindPath(const FVector& Start, const FVector& Goal) const
{
	return Grid.FindPath(Start, Goal, GoalAcceptanceRadius, MaxStartSnapDistance, MaxPathLength);
}

void AVoxelNavigationVolume::RebindComponents()
{
	VolumeBox = Cast<UBoxComponent>(GetRootComponent());
}

void AVoxelNavigationVolume::DrawNavigationDebug() const
{
	UWorld* World = GetWorld();
	if (!World || !Grid.IsBuilt())
		return;

	if (bDrawWalkableNodes && MaxDebugNodes > 0)
	{
		TArray<FVector> Nodes;
		Grid.GatherDebugWalkableNodes(MaxDebugNodes, Nodes);
		for (const FVector& Node : Nodes)
		{
			DrawDebugBox(World, Node + FVector::UpVector * 0.03f,
				FVector(NavVoxelCellSize * 0.18f, NavVoxelCellSize * 0.18f, 0.03f), FColor::Green());
		}
	}

	if (bDrawChunkBoundaries && MaxDebugChunks > 0)
	{
		TArray<TPair<FVector, FVector>> BoundaryLines;
		Grid.GatherDebugChunkBoundaryLines(MaxDebugChunks, BoundaryLines);
		for (const TPair<FVector, FVector>& Line : BoundaryLines)
		{
			DrawDebugLine(World, Line.first, Line.second, FColor(240, 0, 255)); // 보라색
		}
	}
}
