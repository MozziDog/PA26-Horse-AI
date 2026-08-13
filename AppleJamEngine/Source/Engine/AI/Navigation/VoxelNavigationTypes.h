#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"

inline constexpr float NavVoxelCellSize = 0.5f;
inline constexpr float NavL1ChunkSize = 5.0f;
inline constexpr int NavL1ChunkCellsPerAxis = 10;
inline constexpr int NavL1ChunkCellCount = NavL1ChunkCellsPerAxis * NavL1ChunkCellsPerAxis;

static_assert(NavVoxelCellSize * NavL1ChunkCellsPerAxis == NavL1ChunkSize);

struct FVoxelCoord
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;

	bool operator==(const FVoxelCoord& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}
};

// portal은 10x10 청크 기준 flattened coord를 사용하여 저장
// (런타임에 사용하는 array index는 immutable하지 않아서 flattened coord 사용)
// 청크 사이즈 바뀌면 자료형 변경해야 할 수 있음.
using FBakedNavPortal = uint8;
inline constexpr FBakedNavPortal InvalidBakedNavPortal = 0xff;
static_assert(NavL1ChunkCellCount < InvalidBakedNavPortal);

struct FVoxelNavigationPortalKey
{
	FVoxelCoord ChunkCoord;
	FBakedNavPortal LocalPortalCell = InvalidBakedNavPortal;

	bool IsValid() const { return LocalPortalCell != InvalidBakedNavPortal; }
};

struct FBakedVoxelNavigationIntraEdge
{
	FBakedNavPortal PortalA = InvalidBakedNavPortal;
	FBakedNavPortal PortalB = InvalidBakedNavPortal;
	float Cost = 0.0f;
};

struct FBakedVoxelNavigationExternalLink
{
	FBakedNavPortal LocalPortalId = InvalidBakedNavPortal;
	uint8 PackedNeighborChunkDelta = 0;
	FBakedNavPortal NeighborPortalId = InvalidBakedNavPortal;
	float Cost = 0.0f;
};

struct FBakedVoxelNavigationChunk
{
	FVoxelCoord Coord;
	TStaticArray<uint8, NavL1ChunkCellCount> Cells = {};
	TArray<FBakedVoxelNavigationIntraEdge> IntraEdges;
	TArray<FBakedVoxelNavigationExternalLink> ExternalLinks;
};

struct FVoxelNavigationBuildSettings
{
	float AgentRadius = 0.6f;
	float AgentHeight = 2.0f;
	float MaxWalkableSlopeDegrees = 30.0f;
	float MaxNeighborHeightDelta = 0.4f;
	float GroundProbeInset = 0.02f;
	float ClearanceOffset = 0.03f;
};

// Navigation asset의 메타데이터 
// 어느 씬의 어느 NavVolume에서 베이크했는지 등등
// 특정 청크를 로드하려고 했는데 그것이 어느 NavVolume에 속해있는지 파악할 때 등의 상황에 필요
// NOTE: it never transfers runtime portal indices or mutable graph state.
struct FVoxelNavigationAssetInfo
{
	FString SourceScenePath;
	FVector BoundsCenter = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	FVoxelNavigationBuildSettings Settings;
	TArray<FVoxelCoord> AvailableChunkCoords;
};

struct FVoxelNavigationBuildStats
{
	size_t NumSampledCells = 0;
	size_t NumNoGroundCells = 0;
	size_t NumRejectedSlope = 0;
	size_t NumRejectedClearance = 0;
	size_t NumWalkableNodes = 0;
	size_t NumDirectedEdges = 0;
	size_t NumRawWalkableNodes = 0;
	size_t NumErodedNodes = 0;
	size_t NumBuiltL1Chunks = 0;
	size_t NumAbstractNodes = 0;
	size_t NumAbstractEdges = 0;
	size_t NumSkippedCellsByL1Overlap = 0;
	float BuildTimeMs = 0.0f;
	size_t PeakMemoryBytes = 0;
};

// 복셀 데이터 계산중에 사용되는 노드 데이터. 최종 결과에는 포함되지 않음
struct FVoxelNavigationNode
{
	FVoxelCoord Coord;
	FVector Position = FVector::ZeroVector;
	FVector GroundNormal = FVector::UpVector;
	TArray<int32> Neighbors;
};

struct FVoxelNavigationL1Chunk
{
	FVoxelCoord Coord;
	// 0은 복셀 없음. [1..255]는 255단계로 양자화된 복셀의 높이 → [ChunkBottom, ChunkTop]
	TStaticArray<uint8, NavL1ChunkCellCount> Cells = {};
	TArray<int32> PortalIndices;
};

struct FVoxelNavigationAbstractEdge
{
	int32 ToPortal = -1;
	float Cost = 0.0f;
};

struct FVoxelNavigationPortal
{
	int32 ChunkIndex = -1;
	uint8 LocalCell = 0;
	TArray<FVoxelNavigationAbstractEdge> Edges;
};

struct FVoxelNavigationPathResult
{
	enum class EFailure : uint8
	{
		None,
		NoStart,
		NoPath,
		NoData,
	};

	bool bSuccess = false;
	bool bPartial = false;
	EFailure Failure = EFailure::None;
	float PathLength = 0.0f;
	float SearchTimeMs = 0.0f;
	int32 NumExpandedNodes = 0;
	TArray<FVector> Points;
};
