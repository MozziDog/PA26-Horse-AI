#include "pch.h"

#include "AI/Navigation/NavigationStreamingService.h"

#include "AI/Navigation/VoxelNavigationVolume.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <chrono>
#include <exception>

namespace
{
	bool ContainsCoord(const TArray<FVoxelCoord>& Coords, const FVoxelCoord& Coord)
	{
		return std::any_of(Coords.begin(), Coords.end(), [&Coord](const FVoxelCoord& Value) { return Value == Coord; });
	}

	bool SameCoordSet(TArray<FVoxelCoord> A, TArray<FVoxelCoord> B)
	{
		std::sort(A.begin(), A.end());
		std::sort(B.begin(), B.end());
		return A == B;
	}
}

FNavigationStreamingService::~FNavigationStreamingService()
{
	Reset();
}

FNavigationStreamingService::FRequestedSet* FNavigationStreamingService::FindRequestedSet(AVoxelNavigationVolume* Volume)
{
	auto It = std::find_if(RequestedSets.begin(), RequestedSets.end(), [Volume](const FRequestedSet& Set) { return Set.Volume.Get() == Volume; });
	return It == RequestedSets.end() ? nullptr : &*It;
}

const FNavigationStreamingService::FRequestedSet* FNavigationStreamingService::FindRequestedSet(const AVoxelNavigationVolume* Volume) const
{
	auto It = std::find_if(RequestedSets.begin(), RequestedSets.end(), [Volume](const FRequestedSet& Set) { return Set.Volume.Get() == Volume; });
	return It == RequestedSets.end() ? nullptr : &*It;
}

bool FNavigationStreamingService::IsCurrentRequest(const FPendingLoad& Pending, const AVoxelNavigationVolume& Volume) const
{
	const FRequestedSet* Set = FindRequestedSet(&Volume);
	return Set && Set->NavigationDataGeneration == Pending.NavigationDataGeneration && Set->Revision == Pending.RequestRevision;
}

void FNavigationStreamingService::UpdateDesiredChunks(
	AVoxelNavigationVolume* Volume,
	std::shared_ptr<const FNavigationAssetCatalog> AssetCatalog,
	const TArray<FVoxelCoord>& DesiredChunkCoords)
{
	if (!Volume || !AssetCatalog || !AssetCatalog->IsOpen())
	{
		return;
	}

	FRequestedSet* Set = FindRequestedSet(Volume); // 현재 volume내에서 '가지고 있어야 할' 청크들 수집
	if (!Set)
	{
		Set = &RequestedSets.emplace_back();
		Set->Volume = Volume;
		Set->NavigationDataGeneration = Volume->GetNavigationDataGeneration();
		Set->Revision = 1;
	}
	if (Set->NavigationDataGeneration != Volume->GetNavigationDataGeneration() || !SameCoordSet(Set->DesiredChunkCoords, DesiredChunkCoords))
	{
		Set->NavigationDataGeneration = Volume->GetNavigationDataGeneration();
		++Set->Revision;
		Set->DesiredChunkCoords = DesiredChunkCoords;
	}

	// 현재 로드되어있는 청크들과 대조하여 로드/언로드할 청크 산출
	TArray<FVoxelCoord> LoadedCoords;
	Volume->GatherLoadedNavigationChunks(LoadedCoords);
	TArray<FVoxelCoord> UnloadCoords;
	for (const FVoxelCoord& Loaded : LoadedCoords)
	{
		if (!ContainsCoord(Set->DesiredChunkCoords, Loaded)) 
			UnloadCoords.push_back(Loaded);
	}
	if (!UnloadCoords.empty())
	{
		Volume->UnloadStreamingNavigationChunks(UnloadCoords);
	}

	// 임의의 청크를 여러번 로드 요청하지 않도록 기존 load 요청에 있는지 검사
	TArray<FVoxelCoord> LoadCoords;
	for (const FVoxelCoord& Desired : Set->DesiredChunkCoords)
	{
		if (ContainsCoord(LoadedCoords, Desired)) 
			continue;

		const bool bAlreadyInFlight = std::any_of(PendingLoads.begin(), PendingLoads.end(), 
			[Volume, Set, &Desired](const FPendingLoad& Pending)
			{
				return Pending.Volume.Get() == Volume 
					&& Pending.NavigationDataGeneration == Set->NavigationDataGeneration
					&& Pending.RequestRevision == Set->Revision 
					&& ContainsCoord(Pending.ChunkCoords, Desired);
			}
		);
		if (!bAlreadyInFlight)
		{
			LoadCoords.push_back(Desired);
		}
	}
	if (LoadCoords.empty())
	{
		RefreshStats();
		return;
	}

	// 프레임 드랍을 막기 위해 백그라운드에서 로드 수행
	FPendingLoad Pending;
	Pending.Volume = Volume;
	Pending.NavigationDataGeneration = Set->NavigationDataGeneration;
	Pending.RequestRevision = Set->Revision;
	Pending.ChunkCoords = LoadCoords;
	for (const FVoxelCoord& Coord : LoadCoords)
	{
		Pending.RequestedBytes += AssetCatalog->GetChunkPayloadBytes(Coord);
	}
	Pending.Future = std::async(std::launch::async, [AssetCatalog = std::move(AssetCatalog), LoadCoords]()
	{
		const auto StartTime = std::chrono::steady_clock::now();
		FLoadResult Result;
		Result.bSuccess = AssetCatalog->ReadChunks(LoadCoords, Result.Chunks);
		Result.IoDeserializeTimeMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - StartTime).count();
		return Result;
	});
	PendingLoads.push_back(std::move(Pending));
	RefreshStats();
}

void FNavigationStreamingService::Tick()
{
	for (auto It = PendingLoads.begin(); It != PendingLoads.end();)
	{
		if (!It->Future.valid() || It->Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		{
			++It;
			continue;
		}

		FLoadResult Result;
		try
		{
			Result = It->Future.get();
		}
		catch (const std::exception& Exception)
		{
			UE_LOG("[VoxelNavigation] Streaming chunk read failed: %s", Exception.what());
		}

		if (AVoxelNavigationVolume* Volume = It->Volume.Get())
		{
			Stats.LastIoDeserializeTimeMs = Result.IoDeserializeTimeMs;
			if (Volume->GetNavigationDataGeneration() != It->NavigationDataGeneration || !IsCurrentRequest(*It, *Volume))
			{
				// 로드되는 도중에 순간이동 등으로 ClearNavigationData()가 요청된 경우.
				// → 로드된 데이터 폐기
				++Stats.StaleDiscardCount;
			}
			else if(!Result.bSuccess || !Volume->PublishStreamingNavigationChunks(Result.Chunks))
			{
				UE_LOG("[VoxelNavigation] Streaming chunk publish failed for volume %s", Volume->GetName().c_str());
			}
		}
		It = PendingLoads.erase(It);
	}
	RefreshStats();
}

void FNavigationStreamingService::Reset()
{
	// std::future 소멸자에서 async work thread Join하므로 
	// PendingLoads.clear()로 소멸자 호출 유도 → 고아 스레드 방지
	PendingLoads.clear();
	RequestedSets.clear();
	Stats = {};
}

void FNavigationStreamingService::RefreshStats()
{
	Stats.RequestedChunkCount = 0;
	Stats.InFlightChunkCount = 0;
	Stats.InFlightBytes = 0;
	for (const FRequestedSet& Set : RequestedSets)
	{
		Stats.RequestedChunkCount += static_cast<int32>(Set.DesiredChunkCoords.size());
	}
	for (const FPendingLoad& Pending : PendingLoads)
	{
		Stats.InFlightChunkCount += static_cast<int32>(Pending.ChunkCoords.size());
		Stats.InFlightBytes += Pending.RequestedBytes;
	}
}
