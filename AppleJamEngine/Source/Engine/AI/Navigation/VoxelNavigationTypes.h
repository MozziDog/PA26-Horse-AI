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

// Baked files must never contain a runtime portal-array index.  A portal is
// permanently identified by its chunk and representative 10x10 cell.
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
