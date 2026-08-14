#pragma once

#include "AI/Navigation/VoxelNavigationTypes.h"

// VoxelNavigationGrid에서 디스크 에셋 로드하는 부분 분리
class FNavigationAssetCatalog
{
public:
	bool Open(const FString& AssetPath);
	bool ReadChunks(const TArray<FVoxelCoord>& RequestedCoords, TArray<FBakedVoxelNavigationChunk>& OutChunks) const;
	bool ReadAllChunks(TArray<FBakedVoxelNavigationChunk>& OutChunks) const;
	bool ValidateCompleteAsset() const;

	const FVoxelNavigationAssetInfo& GetInfo() const { return Info; }
	bool IsOpen() const { return !AssetPath.empty(); }
	void GatherChunkCoordsInRadius(const FVector& Center, float Radius, TArray<FVoxelCoord>& OutCoords) const;
	FVector GetChunkCenter(const FVoxelCoord& Coord) const;
	uint64 GetChunkPayloadBytes(const FVoxelCoord& Coord) const;

	static bool WriteAsset(const FString& AssetPath, const FString& SourceScenePath, const FVoxelNavigationBakedData& Data);
	static bool WriteReferenceJson(const FString& Path, const FVoxelNavigationBakedData& Data);

private:
	struct FChunkIndexEntry
	{
		FVoxelCoord Coord;
		uint64 AbsoluteOffset = 0;
		uint32 Size = 0;
	};

	const FChunkIndexEntry* FindEntry(const FVoxelCoord& Coord) const;

	FString AssetPath;
	FVoxelNavigationAssetInfo Info;
	TArray<FChunkIndexEntry> Index;	// 청크 좌표 → 청크 내 바이트 위치/길이 lookup
};
