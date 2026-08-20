#pragma once

#include "AI/Navigation/VoxelNavigationGrid.h"

class AActor;
class UWorld;

// FVoxelNavigationBakeBuilder는 베이크 데이터를 생성만 하고 
// 이를 런타임용 FVoxelNavigationGrid에 직접 전달하거나 에셋 파일로 내보내거나 하지 않음
// (에셋 파일로 내보내는 것은 NavigationAssetCatalog 역할)
class FVoxelNavigationBakeBuilder
{
public:
	bool Build(
		UWorld* World,
		const FVector& BoundsCenter,
		const FVector& BoundsExtent,
		const FVoxelNavigationBuildSettings& Settings,
		const AActor* QueryOwner,
		FVoxelNavigationBakedData& OutData,
		FVoxelNavigationBuildStats& OutStats) const;
};

// 베이크 전용 복셀 그리드. FVoxelNavigationBakeBuilder에서만 사용
class FVoxelNavigationBakeGrid final : public FVoxelNavigationGrid
{
public:
	FVoxelNavigationBakeGrid();

	bool Build(
		UWorld* World,
		const FVector& BoundsCenter,
		const FVector& BoundsExtent,
		const FVoxelNavigationBuildSettings& Settings,
		const AActor* QueryOwner);
	bool ExportBakedData(FVoxelNavigationBakedData& OutData) const;
	const FVoxelNavigationBuildStats& GetBuildStats() const { return BuildStats; }

private:
	int FlattenColumn(int X, int Y) const;
	bool IsValidColumn(int X, int Y) const;
	void BuildAbstractGraph(const TArray<uint8>& RetainedNodes);
	bool BuildBakedChunksFromRuntimeGraph();
	bool HasCardinalBridge(int FromNode, int ToNode, int BridgeX, int BridgeY) const;
	void AddDirectedEdge(int FromNode, int ToNode);
	// ───빌드 중 메모리 사용량 추적───
	void ResetBakeScratchMemory();
	// 메모리 추적 때문에 일괄 순회하지 않도록 원소 하나 추가할 때마다 capacity 추적, ElementSize 곱해서 총 사용량에 증감
	void TrackBakeScratchMemoryCapacity(size_t PreviousCapacity, size_t CurrentCapacity, size_t ElementSize);
	void UpdateBuildPeakMemory(uint64 AdditionalTemporaryBytes = 0);

	FVoxelNavigationBuildStats BuildStats;
	uint64 BakeScratchMemoryBytes = 0;

	// ───베이크 시에만 사용하는 임시 데이터들───
	TArray<FVoxelNavigationNode> Nodes;
	TArray<TArray<int>> XYToNodesLookup;
	TArray<int> NodeToChunkLookup;
	TArray<int> NodeToLocalCellIdxLookup;
	TArray<TStaticArray<int, NavL1ChunkCellCount>> ChunkCellToNodeLookup;
};
