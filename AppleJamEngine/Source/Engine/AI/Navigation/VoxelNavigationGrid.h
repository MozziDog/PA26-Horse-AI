#pragma once

#include "AI/Navigation/VoxelNavigationTypes.h"

class AActor;
class UWorld;

// Dense 3D sampling, sparse walkable-node graph. A column may contain any number
// of walkable nodes, so bridges and floors sharing XY remain distinct.
class FVoxelNavigationGrid
{
public:
	bool Build(
		UWorld* World,
		const FVector& BoundsCenter,
		const FVector& BoundsExtent,
		const FVoxelNavigationBuildSettings& Settings,
		const AActor* QueryOwner = nullptr);

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
	const TArray<FVoxelNavigationNode>& GetNodes() const { return Nodes; }

private:
	int32 FlattenColumn(int32 X, int32 Y) const;
	bool IsValidColumn(int32 X, int32 Y) const;
	int32 FindNearestNode(const FVector& Point, float MaxDistance = 0.0f) const;
	bool CanTraverse(UWorld* World, int32 FromNode, int32 ToNode, const AActor* QueryOwner) const;
	bool HasCardinalBridge(int32 FromNode, int32 ToNode, int32 BridgeX, int32 BridgeY) const;
	void AddDirectedEdge(int32 FromNode, int32 ToNode);

	FVector BoundsCenter = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	FVector BoundsMin = FVector::ZeroVector;
	FVoxelNavigationBuildSettings BuildSettings;
	FVoxelNavigationBuildStats BuildStats;
	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 SizeZ = 0;
	bool bBuilt = false;
	TArray<FVoxelNavigationNode> Nodes;
	TArray<TArray<int32>> ColumnNodes;
};
