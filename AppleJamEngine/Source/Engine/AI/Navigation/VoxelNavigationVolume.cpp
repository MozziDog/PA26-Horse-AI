#include "pch.h"

#include "AI/Navigation/VoxelNavigationVolume.h"

#include "Component/Shape/BoxComponent.h"
#include "Core/Logging/Log.h"
#include "Core/Types/CollisionTypes.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/World.h"
#include "Platform/Paths.h"
#include "Serialization/Archive.h"

#include <filesystem>

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
	if (!ReferenceDataPath.empty() && !LoadNavigationAsset(ReferenceDataPath))
	{
		UE_LOG("[VoxelNavigation] Failed to load baked navigation asset: %s", ReferenceDataPath.c_str());
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

bool AVoxelNavigationVolume::BakeNavigationAsset(const FString& OutputPath, const FString& SourceScenePath)
{
	RebindComponents();
	UWorld* World = GetWorld();
	UBoxComponent* Box = VolumeBox.Get();
	if (!World || !Box) return false;
	const FRotator VolumeRotation = GetActorRotation();
	if (std::abs(VolumeRotation.Pitch) > 0.01f || std::abs(VolumeRotation.Yaw) > 0.01f || std::abs(VolumeRotation.Roll) > 0.01f)
	{
		UE_LOG("[VoxelNavigation] Bake rejected: Volume=%s must not be rotated.", GetName().c_str());
		return false;
	}

	const std::filesystem::path AssetPath(FPaths::ToWide(OutputPath));
	const FString ReferencePath = FPaths::ToUtf8((AssetPath.parent_path() /
		std::filesystem::path(AssetPath.stem().wstring() + L".reference.json")).wstring());
	const bool bSuccess = Grid.Build(World, GetActorLocation(), Box->GetScaledBoxExtent(), this->GetNavigationBuildSettings(), this) &&
		Grid.SaveReferenceJson(ReferencePath) &&
		Grid.SaveNavigationAsset(OutputPath, SourceScenePath);
	if (bSuccess)
	{
		ReferenceDataPath = OutputPath;
	}

	// 디버그 / 통계 정보를 detail 패널 + 로그로 출력
	// NOTE: reflection이 size_t(=uint64) 프로퍼티를 지원하지 않아 detail 패널 표기값은 '잘린 값'일 수 있음
	const FVoxelNavigationBuildStats& Stats = Grid.GetBuildStats();
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

	UE_LOG("[VoxelNavigation] Bake Volume=%s Success=%d Output=%s Cells=%d RawNodes=%d Nodes=%d Eroded=%d RawEdges=%d L1=%d AbstractNodes=%d AbstractEdges=%d SlopeRejected=%d ClearanceRejected=%d TimeMs=%.3f PeakMemoryBytes=%llu",
		GetName().c_str(), bSuccess ? 1 : 0, OutputPath.c_str(), DebugSampledCells, DebugRawWalkableNodes, DebugWalkableNodes,
		DebugErodedNodes, DebugDirectedEdges, DebugBuiltL1Chunks, DebugAbstractNodes, DebugAbstractEdges,
		DebugRejectedSlope, DebugRejectedClearance, DebugBuildTimeMs,
		static_cast<unsigned long long>(Stats.PeakMemoryBytes));
	return bSuccess;
}

bool AVoxelNavigationVolume::LoadNavigationAsset(const FString& InputPath)
{
	const bool bSuccess = Grid.LoadNavigationAsset(InputPath);
	if (bSuccess)
	{
		ReferenceDataPath = InputPath;
	}
	return bSuccess;
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
	if (!World || !Grid.IsBuilt()) return;

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
