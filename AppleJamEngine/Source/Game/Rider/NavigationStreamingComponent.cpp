#include "pch.h"

#include "Game/Rider/NavigationStreamingComponent.h"

#include "AI/Navigation/VoxelNavigationVolume.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>

namespace {
	struct FChunkCandidate
	{
		AVoxelNavigationVolume* Volume = nullptr;
		FVoxelCoord Coord;
		float DistanceSquared = 0.0f;

		const bool operator < (FChunkCandidate other)
		{
			return DistanceSquared < other.DistanceSquared;
		}
	};
}

UNavigationStreamingComponent::UNavigationStreamingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.SetTickGroup(TG_PostPhysics);
}

void UNavigationStreamingComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UNavigationStreamingComponent::EndPlay()
{
	StreamingService.Reset();
	Super::EndPlay();
}

void UNavigationStreamingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StreamingService.Tick();
	LoadedChunkCount = 0;

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World) 
		return;

	const FVector ActorLocation = Owner->GetActorLocation();

	// 현재 월드에 존재하는 모든 VoxelNavigationVolume을 순회하면서 청크 로드
	// NOTE: 만약 월드에 단 1개의 VoxelNavigationVolume만 존재한다고 보장되면 단일 Volume 순회로 간소화 가능
	TArray<AVoxelNavigationVolume*> Volumes;
	TArray<FChunkCandidate> Candidates;
	for (AActor* Actor : World->GetActors())
	{
		AVoxelNavigationVolume* Volume = Cast<AVoxelNavigationVolume>(Actor);
		if (!Volume || !Volume->IsStreamingNavigationInitialized()) 
			continue;

		Volumes.push_back(Volume);
		TArray<FVoxelCoord> CandidateCoords;
		Volume->GatherNavigationChunksInRadius(ActorLocation, LoadRadius, CandidateCoords);
		for (const FVoxelCoord& Coord : CandidateCoords)
		{
			Candidates.push_back({ Volume, Coord, GetChunkDistanceSquared(*Volume, Coord, ActorLocation) });
		}
	}

	// 액터와 가까운 순으로 정렬하고 자르기 → 액터와 가까운 MaxLoadedChunks 만큼만 유지
	std::sort(Candidates.begin(), Candidates.end()); 
	if (Candidates.size() > MaxLoadedChunks)
	{
		Candidates.resize(MaxLoadedChunks);
	}

	for (AVoxelNavigationVolume* Volume : Volumes)
	{
		TArray<FVoxelCoord> DesiredCoords;
		for (const FChunkCandidate& Candidate : Candidates)
		{
			if (Candidate.Volume == Volume)	// multi-navVolume 지원때문에 Candidate가 어떤 볼륨에 있는지 파악 필요
			{
				DesiredCoords.push_back(Candidate.Coord);
			}
		}
		UpdateStreamingForVolume(*Volume, DesiredCoords);
		TArray<FVoxelCoord> LoadedCoords;
		Volume->GatherLoadedNavigationChunks(LoadedCoords);
		LoadedChunkCount += LoadedCoords.size();
	}
	const FNavigationStreamingStats& Stats = StreamingService.GetStats();
	RequestedChunkCount = Stats.RequestedChunkCount;
	InFlightChunkCount = Stats.InFlightChunkCount;
	InFlightMemoryMB = static_cast<float>(Stats.InFlightBytes) / (1024.0f * 1024.0f);
	StaleLoadDiscardCount = static_cast<int>(Stats.StaleDiscardCount);
	LastIoDeserializeTimeMs = Stats.LastIoDeserializeTimeMs;
}

void UNavigationStreamingComponent::UpdateStreamingForVolume(AVoxelNavigationVolume& Volume, const TArray<FVoxelCoord>& DesiredCoords)
{
	StreamingService.UpdateDesiredChunks(&Volume, Volume.GetNavigationAssetCatalog(), DesiredCoords);
}

float UNavigationStreamingComponent::GetChunkDistanceSquared( const AVoxelNavigationVolume& Volume, 
																const FVoxelCoord& Coord, const FVector& ActorLocation) const
{
	const FVector Delta = Volume.GetNavigationChunkCenter(Coord) - ActorLocation;
	return Delta.LengthSquared();
}
