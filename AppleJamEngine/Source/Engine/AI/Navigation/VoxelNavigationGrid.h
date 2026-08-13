#pragma once

#include "AI/Navigation/VoxelNavigationTypes.h"

class AActor;
class UWorld;

// 물리 쿼리에서 정확한 이동 가능성을 구축한 뒤, 침식 필터를 적용하여 가장자리 복셀들은 정리
// 같은 청크 내에서는 걸을 수 있는 지면이 1개라는 전제로 복셀을 2.5D height map으로 저장,
// 어차피 데이터 압축때문에 청크 도입했으니 탐색은 HPA* 알고리즘으로.
class FVoxelNavigationGrid
{
public:
	FVoxelNavigationGrid() = default;
	~FVoxelNavigationGrid();
	FVoxelNavigationGrid(const FVoxelNavigationGrid&) = delete;
	FVoxelNavigationGrid& operator=(const FVoxelNavigationGrid&) = delete;

	bool Build(
		UWorld* World,
		const FVector& BoundsCenter,
		const FVector& BoundsExtent,
		const FVoxelNavigationBuildSettings& Settings,
		const AActor* QueryOwner = nullptr);

	// Debug/reference transport used before the production binary page format is
	// introduced.  Loading recreates the same runtime HPA* graph from baked
	// chunk identities and edges, without invoking any physics query.
	bool SaveReferenceJson(const FString& Path) const;
	bool LoadReferenceJson(const FString& Path);
	// Production transport.  The payload is an explicitly field-serialized,
	// indexed little-endian stream; no C++ struct layout is persisted.
	bool SaveNavigationAsset(const FString& Path, const FString& SourceScenePath) const;
	bool LoadNavigationAsset(const FString& Path);

	// Streaming transport: asset I/O is safe to run off the game thread; only
	// Initialize/Publish/Clear mutate the runtime HPA* graph on the game thread.
	static bool ReadNavigationAssetInfo(const FString& Path, FVoxelNavigationAssetInfo& OutInfo);
	static bool ReadNavigationAssetChunks(const FString& Path, const TArray<FVoxelCoord>& RequestedCoords,
		TArray<FBakedVoxelNavigationChunk>& OutChunks);
	bool InitializeStreamingNavigation(const FVoxelNavigationAssetInfo& AssetInfo);
	bool PublishStreamingChunks(const TArray<FBakedVoxelNavigationChunk>& LoadedChunks);
	bool UnloadStreamingChunks(const TArray<FVoxelCoord>& ChunkCoords);
	void ClearNavigationData();
	bool IsStreamingNavigationInitialized() const { return bStreamingNavigationInitialized; }
	uint64 GetNavigationDataGeneration() const { return NavigationDataGeneration; }
	const TArray<FVoxelCoord>& GetAvailableStreamingChunkCoords() const { return AvailableStreamingChunkCoords; }
	void GatherLoadedChunkCoords(TArray<FVoxelCoord>& OutCoords) const;
	void GatherStreamingChunkCoordsInRadius(const FVector& Center, float Radius, TArray<FVoxelCoord>& OutCoords) const;

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
	const FVoxelNavigationBuildStats& GetBuildStats() const { return BuildStats; }
	void GatherDebugWalkableNodes(int MaxNodes, TArray<FVector>& OutNodes) const;
	// Each baked chunk contributes one XY outline at its ChunkCoord.Z slab: four
	// lines only, avoiding overlapping top/bottom box edges and terrain occlusion.
	void GatherDebugChunkBoundaryLines(int MaxChunks, TArray<TPair<FVector, FVector>>& OutLines) const;

private:
	struct FCellRef
	{
		int ChunkIndex = -1;
		int LocalCell = -1;

		bool IsValid() const { return ChunkIndex >= 0 && LocalCell >= 0; }
		bool operator==(const FCellRef& Other) const
		{
			return ChunkIndex == Other.ChunkIndex && LocalCell == Other.LocalCell;
		}
	};

	int FlattenColumn(int X, int Y) const;
	bool IsValidColumn(int X, int Y) const;
	int FlattenL1Chunk(int X, int Y, int Z) const;
	bool IsValidL1Chunk(int X, int Y, int Z) const;
	FCellRef FindNearestCell(const FVector& Point, float MaxDistance = 0.0f) const;
	FVector GetCellPosition(const FCellRef& Cell) const;
	bool IsCellWalkable(int ChunkIndex, int LocalCell) const;	// 해당 위치에 이미 다른 복셀이 있는지 여부 체크
	float FindLocalPath(
		const FCellRef& Start,
		const FCellRef& Goal,
		TArray<FCellRef>* OutPath = nullptr,
		bool bAllowPartial = false,
		const FVector* PartialTarget = nullptr) const;
	void BuildAbstractGraph(const TArray<uint8>& RetainedNodes);
	bool BuildBakedChunksFromRuntimeGraph();
	bool BuildRuntimeGraphFromBakedChunks(bool bAllowMissingExternalTargets = false); // TODO: 베이크 후 abstract graph 검증 역할 분리하도록 리팩토링
	bool ValidateBakedChunks(bool bAllowMissingExternalTargets = false) const;
	int FindChunkIndexByCoord(const FVoxelCoord& Coord) const;
	static bool PackNeighborChunkDelta(const FVoxelCoord& Delta, uint8& OutPacked);
	static bool UnpackNeighborChunkDelta(uint8 Packed, FVoxelCoord& OutDelta);
	int FindOrAddPortal(int ChunkIndex, int LocalCell);
	void AddAbstractEdge(int FromPortal, int ToPortal, float Cost);
	bool CanTraverse(UWorld* World, int FromNode, int ToNode, const AActor* QueryOwner) const;
	bool HasCardinalBridge(int FromNode, int ToNode, int BridgeX, int BridgeY) const;
	void AddDirectedEdge(int FromNode, int ToNode);
	// ── 메모리 프로파일링 관련 ──
	void RefreshTrackedMemory();
	size_t CalculateTrackedMemoryBytes() const;
	void AddTrackedMemory(size_t Size);
	void UpdateBuildPeakMemory(uint64 TemporaryMemoryBytes = 0);	// Bake 도중에 메모리 부족으로 터지지 않게 피크 사용량 체크

private:
	FVector BoundsCenter = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	FVector BoundsMin = FVector::ZeroVector;
	FVoxelNavigationBuildSettings BuildSettings;
	FVoxelNavigationBuildStats BuildStats;
	int CellCountX = 0;
	int CellCountY = 0;
	int CellCountZ = 0;
	size_t L1ChunkCountX = 0;
	size_t L1ChunkCountY = 0;
	size_t L1ChunkCountZ = 0;
	bool bBuilt = false;
	bool bStreamingNavigationInitialized = false;
	uint64 NavigationDataGeneration = 0;	// 순간 이동 등 특수한 상황에서 디스크에서 로드 중인 청크가 버려져야할 수 있음
											// 그러한 상황을 구분하기 위해 데이터 Clear 시마다 이 값을 1씩 증가시킴
	TArray<FVoxelCoord> AvailableStreamingChunkCoords;

	// ── 복셀 빌드 중에만 사용하는 임시 데이터들 ──
	TArray<FVoxelNavigationNode> Nodes;		// 복셀 1개를 1개의 노드로 취급
	TArray<FVoxelNavigationL1Chunk> L1Chunks;
	TArray<FVoxelNavigationPortal> Portals;	// 포탈: 청크간 연결
	TArray<FBakedVoxelNavigationChunk> BakedChunks;
	TArray<TArray<int>> XYToNodesLookup;	// flatten(XY) → NodeId(s) 매핑, 이웃한 노드들 빠른 탐색용
	TArray<int> L1ChunkLookup;				// flatten(XYZ) → L1ChunkId 매핑
	TArray<int> NodeToChunkLookup;			// NodeId → L1ChunkId 매핑
	TArray<int> NodeToLocalCellIdxLookup;	// NodeId → L1Chunk내에서의 cellIndex(0~99) 매핑	
	TArray<TStaticArray<int, NavL1ChunkCellCount>> ChunkCellToNodeLookup; // [L1ChunkId][cellIndex] → NodeId 매핑
	
	uint64 TrackedMemoryBytes = 0;
};
