#pragma once

#include "AI/Navigation/VoxelNavigationTypes.h"

class FVoxelNavigationBakeGrid;

struct FCellRef // NavigationGrid 전역에서 unique한 복셀 ID
{
	int ChunkIndex = -1;
	int LocalCell = -1;

	bool IsValid() const { return ChunkIndex >= 0 && LocalCell >= 0; }
	bool operator==(const FCellRef& Other) const
	{
		return ChunkIndex == Other.ChunkIndex && LocalCell == Other.LocalCell;
	}
};

// 물리 쿼리에서 정확한 이동 가능성을 구축한 뒤, 침식 필터를 적용하여 가장자리 복셀들은 정리
// 같은 청크 내에서는 걸을 수 있는 지면이 1개라는 전제로 복셀을 2.5D height map으로 저장,
// 어차피 데이터 압축때문에 청크 도입했으니 탐색은 HPA* 알고리즘으로.
// AddLoadedChunks()로 로드된 청크에 대해서만 길찾기를 수행함.
// 청크 로딩은 외부에서 수행하고 VoxelNavigationGrid에는 결과만 전달
class FVoxelNavigationGrid
{
public:
	FVoxelNavigationGrid() = default;
	~FVoxelNavigationGrid();
	FVoxelNavigationGrid(const FVoxelNavigationGrid&) = delete;
	FVoxelNavigationGrid& operator=(const FVoxelNavigationGrid&) = delete;

	bool InitializeRuntime(const FVector& InBoundsCenter, const FVector& InBoundsExtent,
		const FVoxelNavigationBuildSettings& Settings);
	bool AddLoadedChunks(const TArray<FBakedVoxelNavigationChunk>& LoadedChunks);
	bool RemoveLoadedChunks(const TArray<FVoxelCoord>& ChunkCoords);
	void ClearNavigationData();
	bool IsRuntimeInitialized() const { return bRuntimeInitialized; }
	uint64 GetNavigationDataGeneration() const { return NavigationDataGeneration; }
	uint64 GetRuntimeMemoryBytes() const { return RuntimeMemoryBytes; }
	void GatherLoadedChunkCoords(TArray<FVoxelCoord>& OutCoords) const;

	FVoxelNavigationPathResult FindPath(
		const FVector& Start,
		const FVector& Goal,
		float GoalAcceptanceRadius,
		float MaxStartSnapDistance,
		float MaxPathLength = 0.0f) const;

	bool Contains(const FVector& Point) const;
	bool IsBuilt() const { return bBuilt; }
	const FVector& GetBoundsCenter() const { return BoundsCenter; }
	const FVector& GetBoundsExtent() const { return BoundsExtent; }
	const FVoxelNavigationBuildSettings& GetSettings() const { return BuildSettings; }

	// ─── Debug draw 관련 ───
	void GatherDebugWalkableNodes(int MaxNodes, TArray<FVector>& OutNodes) const;
	// 청크의 12개 모서리를 모두 그리면 헷갈리므로 청크 밑면의 4개 선만 표시함
	void GatherDebugChunkBoundaryLines(int MaxChunks, TArray<TPair<FVector, FVector>>& OutLines) const;

protected: // VoxelNavigationBakeGrid에서도 접근
	int FlattenL1Chunk(int X, int Y, int Z) const;
	bool IsValidL1Chunk(int X, int Y, int Z) const;

	// ─── 길찾기 관련 ───
	FCellRef FindNearestCell(const FVector& Point, float MaxDistance = 0.0f) const;
	FVector GetCellPosition(const FCellRef& Cell) const;
	bool IsCellWalkable(int ChunkIndex, int LocalCell) const;	// 해당 위치에 이미 다른 복셀이 있는지 여부 체크
	float FindLocalPath(
		const FCellRef& Start,
		const FCellRef& Goal,
		TArray<FCellRef>* OutPath = nullptr,
		bool bAllowPartial = false,
		const FVector* PartialTarget = nullptr) const;

	// ─── 청크 로드/언로드 & 후처리 관련 ───
	bool ValidateLoadedChunkPayload(const FBakedVoxelNavigationChunk& Chunk) const;
	bool RebuildActiveRuntimeGraph();							// HPA* abstract graph 재구축
	int FindChunkIndexByCoord(const FVoxelCoord& Coord) const;	// 현재 로드된 청크들 중에서 탐색
	static bool PackNeighborChunkDelta(const FVoxelCoord& Delta, uint8& OutPacked);
	static bool UnpackNeighborChunkDelta(uint8 Packed, FVoxelCoord& OutDelta);
	int FindOrAddPortal(int ChunkIndex, int LocalCell);
	void AddAbstractEdge(int FromPortal, int ToPortal, float Cost);

	// ─── 런타임 메모리 사용량 측정 관련 ───
	// NOTE: 빌드 타임 메모리 사용량 관련해서는 FVoxelNavigationBakeGrid 참조
	void RefreshRuntimeMemory();
	size_t CalculateRuntimeMemoryBytes() const;

protected:
	FVector BoundsCenter = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	FVector BoundsMin = FVector::ZeroVector;
	FVoxelNavigationBuildSettings BuildSettings;
	int CellCountX = 0;
	int CellCountY = 0;
	int CellCountZ = 0;
	size_t L1ChunkCountX = 0;
	size_t L1ChunkCountY = 0;
	size_t L1ChunkCountZ = 0;
	bool bBuilt = false;
	bool bRuntimeInitialized = false;
	uint64 NavigationDataGeneration = 0;	// 순간 이동 등 특수한 상황에서 디스크에서 로드 중인 청크가 버려져야할 수 있음
											// 그러한 상황을 구분하기 위해 데이터 Clear 시마다 이 값을 1씩 증가시킴

	TArray<FVoxelNavigationL1Chunk> L1Chunks;
	TArray<FVoxelNavigationPortal> Portals;	// 포탈: 청크간 연결
	TArray<FBakedVoxelNavigationChunk> BakedChunks;
	TArray<int> L1ChunkLookup;				// flatten(XYZ) → L1ChunkId 매핑
	
	uint64 RuntimeMemoryBytes = 0;
	bool bRuntimeMemoryStatsEnabled = true;
};
