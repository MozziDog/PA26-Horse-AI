#pragma once

#include "AI/Navigation/VoxelNavigationTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include <future>

class AVoxelNavigationVolume;

// Owns background payload reads only.  Runtime graph mutation is intentionally
// deferred to Tick(), which is called by the Rider's game-thread component.
class FNavigationStreamingService
{
public:
	FNavigationStreamingService() = default;
	~FNavigationStreamingService();
	FNavigationStreamingService(const FNavigationStreamingService&) = delete;
	FNavigationStreamingService& operator=(const FNavigationStreamingService&) = delete;

	bool RequestLoad(AVoxelNavigationVolume* Volume, const FString& AssetPath, const TArray<FVoxelCoord>& ChunkCoords);
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
