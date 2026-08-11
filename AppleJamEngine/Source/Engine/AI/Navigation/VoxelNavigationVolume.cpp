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
	// Primitive components enqueue their body creation during BeginPlay.  Let one
	// complete physics frame consume those commands before querying the scene.
	InitialBuildDelayTicks = 1;
}

void AVoxelNavigationVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (InitialBuildDelayTicks >= 0)
	{
		if (InitialBuildDelayTicks == 0)
		{
			InitialBuildDelayTicks = -1;
			RebuildNavigation();
		}
		else
		{
			--InitialBuildDelayTicks;
		}
	}
	if (bDrawWalkableNodes || bDrawGraphEdges)
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

bool AVoxelNavigationVolume::RebuildNavigation()
{
	InitialBuildDelayTicks = -1;
	RebindComponents();
	UWorld* World = GetWorld();
	UBoxComponent* Box = VolumeBox.Get();
	if (!World || !Box)
	{
		return false;
	}
	const FRotator VolumeRotation = GetActorRotation();
	if (std::abs(VolumeRotation.Pitch) > 0.01f || std::abs(VolumeRotation.Yaw) > 0.01f || std::abs(VolumeRotation.Roll) > 0.01f)
	{
		UE_LOG("[VoxelNavigation] Volume=%s uses axis-aligned prototype bounds; actor rotation is ignored.", GetName().c_str());
	}

	FVoxelNavigationBuildSettings Settings;
	Settings.AgentRadius = AgentRadius;
	Settings.AgentHeight = AgentHeight;
	Settings.MaxWalkableSlopeDegrees = MaxWalkableSlopeDegrees;
	Settings.MaxNeighborHeightDelta = MaxNeighborHeightDelta;

	const bool bSuccess = Grid.Build(World, GetActorLocation(), Box->GetScaledBoxExtent(), Settings, this);

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

	UE_LOG("[VoxelNavigation] Build Volume=%s Success=%d Cells=%d RawNodes=%d Nodes=%d Eroded=%d RawEdges=%d L1=%d AbstractNodes=%d AbstractEdges=%d SlopeRejected=%d ClearanceRejected=%d TimeMs=%.3f PeakMemoryBytes=%llu",
		GetName().c_str(), bSuccess ? 1 : 0, DebugSampledCells, DebugRawWalkableNodes, DebugWalkableNodes,
		DebugErodedNodes, DebugDirectedEdges, DebugBuiltL1Chunks, DebugAbstractNodes, DebugAbstractEdges,
		DebugRejectedSlope, DebugRejectedClearance, DebugBuildTimeMs,
		static_cast<unsigned long long>(Stats.PeakMemoryBytes));
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
	if (!World || !Grid.IsBuilt() || MaxDebugNodes <= 0) return;

	TArray<FVector> Nodes;
	TArray<TPair<FVector, FVector>> Edges;
	Grid.GatherDebugGeometry(MaxDebugNodes, Nodes, Edges);
	for (const FVector& Node : Nodes)
	{
		if (bDrawWalkableNodes)
		{
			DrawDebugBox(World, Node + FVector::UpVector * 0.03f,
				FVector(NavVoxelCellSize * 0.18f, NavVoxelCellSize * 0.18f, 0.03f), FColor::Green());
		}
	}
	if (bDrawGraphEdges)
	{
		for (const TPair<FVector, FVector>& Edge : Edges)
		{
			DrawDebugLine(World, Edge.first + FVector::UpVector * 0.08f,
				Edge.second + FVector::UpVector * 0.08f, FColor(80, 160, 255));
		}
	}
}
