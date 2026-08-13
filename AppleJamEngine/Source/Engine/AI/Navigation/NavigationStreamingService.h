#pragma once

#include "AI/Navigation/NavigationAssetCatalog.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include <future>
#include <memory>

class AVoxelNavigationVolume;

struct FNavigationStreamingStats
{
	int32 RequestedChunkCount = 0;
	int32 InFlightChunkCount = 0;
	uint64 InFlightBytes = 0;
	uint64 StaleDiscardCount = 0;
	float LastIoDeserializeTimeMs = 0.0f;
};

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

	void UpdateDesiredChunks(AVoxelNavigationVolume* Volume, std::shared_ptr<const FNavigationAssetCatalog> AssetCatalog,
		const TArray<FVoxelCoord>& DesiredChunkCoords);
	void Tick();
	void Reset();
	const FNavigationStreamingStats& GetStats() const { return Stats; }

private:
	struct FLoadResult
	{
		bool bSuccess = false;
		TArray<FBakedVoxelNavigationChunk> Chunks;
		float IoDeserializeTimeMs = 0.0f;
	};
	struct FPendingLoad
	{
		TWeakObjectPtr<AVoxelNavigationVolume> Volume;
		uint64 NavigationDataGeneration = 0;	// 순간 이동 등의 상황에서 로드된 데이터가 버려져야 할 수 있음
												// NavGrid의 것과 비교해서 값이 다르면 폐기할 데이터로 판정
		uint64 RequestRevision = 0;
		TArray<FVoxelCoord> ChunkCoords;
		uint64 RequestedBytes = 0;
		std::future<FLoadResult> Future;
	};
	struct FRequestedSet
	{
		TWeakObjectPtr<AVoxelNavigationVolume> Volume;
		uint64 NavigationDataGeneration = 0;
		uint64 Revision = 0;
		TArray<FVoxelCoord> DesiredChunkCoords;
	};

	FRequestedSet* FindRequestedSet(AVoxelNavigationVolume* Volume);
	const FRequestedSet* FindRequestedSet(const AVoxelNavigationVolume* Volume) const;
	bool IsCurrentRequest(const FPendingLoad& Pending, const AVoxelNavigationVolume& Volume) const;
	void RefreshStats();

	TArray<FPendingLoad> PendingLoads;
	TArray<FRequestedSet> RequestedSets;
	FNavigationStreamingStats Stats;
};
