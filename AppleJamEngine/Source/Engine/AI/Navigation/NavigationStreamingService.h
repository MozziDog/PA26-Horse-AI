#pragma once

#include "AI/Navigation/NavigationAssetCatalog.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include <future>
#include <memory>

class AVoxelNavigationVolume;

// 파일 로드가 비동기라서 주기적으로 로드가 되었는지 Tick 호출 필요. 
// 현재는 NavigationStreamingComponent에 의해 Tick 수행됨 
// NOTE: NavigationStreamingComponent에서 Tick 호출되는 건 
//      청크 스트리밍 서비스 소비자가 RiderCharacter밖에 없어서 그런 것.
//		다수의 NavAgent가 존재하는 상황이라면 생명주기 수정 필요함.
class FNavigationStreamingService
{
public:
	FNavigationStreamingService() = default;
	~FNavigationStreamingService();
	FNavigationStreamingService(const FNavigationStreamingService&) = delete;
	FNavigationStreamingService& operator=(const FNavigationStreamingService&) = delete;

	bool RequestLoad(AVoxelNavigationVolume* Volume, std::shared_ptr<const FNavigationAssetCatalog> AssetCatalog,
		const TArray<FVoxelCoord>& ChunkCoords);
	bool RequestUnload(AVoxelNavigationVolume* Volume, const TArray<FVoxelCoord>& ChunkCoords);
	void Tick();
	void Reset();

private:
	struct FLoadResult
	{
		bool bSuccess = false;
		TArray<FBakedVoxelNavigationChunk> Chunks;
	};
	struct FPendingLoad
	{
		TWeakObjectPtr<AVoxelNavigationVolume> Volume;
		uint64 NavigationDataGeneration = 0;	// 순간 이동 등의 상황에서 로드된 데이터가 버려져야 할 수 있음
												// NavGrid의 것과 비교해서 값이 다르면 폐기할 데이터로 판정
		std::future<FLoadResult> Future;
	};

	TArray<FPendingLoad> PendingLoads;
};
