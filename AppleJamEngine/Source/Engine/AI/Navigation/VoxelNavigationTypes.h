#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"

struct FVoxelNavigationCoord
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;
};

struct FVoxelNavigationBuildSettings
{
	float CellSize = 0.5f;
	float AgentRadius = 0.6f;
	float AgentHeight = 2.0f;
	float MaxWalkableSlopeDegrees = 30.0f;
	float MaxNeighborHeightDelta = 0.4f;
	float GroundProbeInset = 0.02f;
	float ClearanceOffset = 0.03f;
};

struct FVoxelNavigationBuildStats
{
	int32 NumSampledCells = 0;
	int32 NumNoGroundCells = 0;
	int32 NumRejectedSlope = 0;
	int32 NumRejectedClearance = 0;
	int32 NumWalkableNodes = 0;
	int32 NumDirectedEdges = 0;
	float BuildTimeMs = 0.0f;
};

struct FVoxelNavigationNode
{
	FVoxelNavigationCoord Coord;
	FVector Position = FVector::ZeroVector;
	FVector GroundNormal = FVector::UpVector;
	TArray<int32> Neighbors;
};

struct FVoxelNavigationPathResult
{
	enum class EFailure : uint8
	{
		None,
		NoStart,
		NoPath,
	};

	bool bSuccess = false;
	bool bPartial = false;
	EFailure Failure = EFailure::None;
	float PathLength = 0.0f;
	float SearchTimeMs = 0.0f;
	int32 NumExpandedNodes = 0;
	TArray<FVector> Points;
};
