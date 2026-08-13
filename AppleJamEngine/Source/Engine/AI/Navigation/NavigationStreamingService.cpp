#include "pch.h"

#include "AI/Navigation/NavigationStreamingService.h"

#include "AI/Navigation/VoxelNavigationVolume.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <chrono>
#include <exception>

FNavigationStreamingService::~FNavigationStreamingService()
{
	Reset();
}

bool FNavigationStreamingService::RequestLoad(
	AVoxelNavigationVolume* Volume,
	std::shared_ptr<const FNavigationAssetCatalog> AssetCatalog,
	const TArray<FVoxelCoord>& ChunkCoords)
{
	if (!Volume || !AssetCatalog || !AssetCatalog->IsOpen() || ChunkCoords.empty())
	{
		return false;
	}

	if ( std::any_of(PendingLoads.begin(), PendingLoads.end(), 
		[Volume](const FPendingLoad& Pending) { return Pending.Volume.Get() == Volume; }) )
	{
		return false;
	}

	// 프레임 드랍을 막기 위해 백그라운드에서 로드 수행
	FPendingLoad Pending;
	Pending.Volume = Volume;
	Pending.NavigationDataGeneration = Volume->GetNavigationDataGeneration();
	Pending.Future = std::async(std::launch::async, [AssetCatalog = std::move(AssetCatalog), ChunkCoords]()
	{
		FLoadResult Result;
		Result.bSuccess = AssetCatalog->ReadChunks(ChunkCoords, Result.Chunks);
		return Result;
	});
	PendingLoads.push_back(std::move(Pending));
	return true;
}

bool FNavigationStreamingService::RequestUnload(AVoxelNavigationVolume* Volume, const TArray<FVoxelCoord>& ChunkCoords)
{
	return Volume && !ChunkCoords.empty() && Volume->UnloadStreamingNavigationChunks(ChunkCoords);
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
			if (Volume->GetNavigationDataGeneration() != It->NavigationDataGeneration)
			{
				// 로드되는 도중에 순간이동 등으로 ClearNavigationData()가 요청된 경우.
				// → 로드된 데이터 폐기
			}
			else if(!Result.bSuccess || !Volume->PublishStreamingNavigationChunks(Result.Chunks))
			{
				UE_LOG("[VoxelNavigation] Streaming chunk publish failed for volume %s", Volume->GetName().c_str());
			}
		}
		It = PendingLoads.erase(It);
	}
}

void FNavigationStreamingService::Reset()
{
	// std::future 소멸자에서 async work thread Join하므로 
	// PendingLoads.clear()로 소멸자 호출 유도 → 고아 스레드 방지
	PendingLoads.clear();
}
