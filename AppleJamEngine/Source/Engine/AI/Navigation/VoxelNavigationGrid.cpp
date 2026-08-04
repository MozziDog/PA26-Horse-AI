#include "pch.h"

#include "AI/Navigation/VoxelNavigationGrid.h"

#include "Core/Types/CollisionTypes.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Profiling/Stats/Stats.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <queue>

namespace
{
	constexpr float NavEpsilon = 1.e-4f;

	struct FOpenNode
	{
		int32 NodeIndex = -1;
		float Score = 0.0f;

		bool operator<(const FOpenNode& Other) const
		{
			return Score > Other.Score;
		}
	};

	float ElapsedMilliseconds(const std::chrono::steady_clock::time_point& Start)
	{
		return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - Start).count();
	}
}

bool FVoxelNavigationGrid::Build(
	UWorld* World,
	const FVector& InBoundsCenter,
	const FVector& InBoundsExtent,
	const FVoxelNavigationBuildSettings& Settings,
	const AActor* QueryOwner)
{
	SCOPE_STAT_CAT("VoxelNav.BuildGrid", "Navigation");
	const auto StartTime = std::chrono::steady_clock::now();

	bBuilt = false;
	Nodes.clear();
	ColumnNodes.clear();
	BuildStats = FVoxelNavigationBuildStats();
	BuildSettings = Settings;

	if (!World || Settings.CellSize <= NavEpsilon || Settings.AgentRadius <= NavEpsilon ||
		Settings.AgentHeight <= Settings.AgentRadius * 2.0f ||
		InBoundsExtent.X <= NavEpsilon || InBoundsExtent.Y <= NavEpsilon || InBoundsExtent.Z <= NavEpsilon)
	{
		return false;
	}

	BoundsCenter = InBoundsCenter;
	BoundsExtent = InBoundsExtent;
	BoundsMin = BoundsCenter - BoundsExtent;
	SizeX = (std::max)(1, static_cast<int32>(std::ceil(BoundsExtent.X * 2.0f / Settings.CellSize)));
	SizeY = (std::max)(1, static_cast<int32>(std::ceil(BoundsExtent.Y * 2.0f / Settings.CellSize)));
	SizeZ = (std::max)(1, static_cast<int32>(std::ceil(BoundsExtent.Z * 2.0f / Settings.CellSize)));
	ColumnNodes.resize(static_cast<size_t>(SizeX * SizeY));

	const uint32 StaticMask = ObjectTypeBit(ECollisionChannel::WorldStatic);
	const float SlopeCos = std::cos(Settings.MaxWalkableSlopeDegrees * 3.14159265358979323846f / 180.0f);
	const float ProbeInset = std::clamp(Settings.GroundProbeInset, 0.001f, Settings.CellSize * 0.25f);
	const FCollisionShape ClearanceShape = FCollisionShape::MakeCapsule(Settings.AgentRadius, Settings.AgentHeight * 0.5f);

	{
		SCOPE_STAT_CAT("VoxelNav.ClassifyStandable", "Navigation");
		for (int32 Y = 0; Y < SizeY; ++Y)
		{
			for (int32 X = 0; X < SizeX; ++X)
			{
				TArray<float> AcceptedGroundHeights;
				for (int32 Z = 0; Z < SizeZ; ++Z)
				{
					++BuildStats.NumSampledCells;
					const float CellCenterX = BoundsMin.X + (static_cast<float>(X) + 0.5f) * Settings.CellSize;
					const float CellCenterY = BoundsMin.Y + (static_cast<float>(Y) + 0.5f) * Settings.CellSize;
					const float SlabBottom = BoundsMin.Z + static_cast<float>(Z) * Settings.CellSize;
					const float SlabTop = SlabBottom + Settings.CellSize;
					const FVector RayStart(CellCenterX, CellCenterY, SlabTop - ProbeInset);

					FHitResult GroundHit;
					if (!World->PhysicsRaycastByObjectTypes(
						RayStart, FVector::DownVector, Settings.CellSize - ProbeInset + NavEpsilon,
						GroundHit, StaticMask, QueryOwner))
					{
						++BuildStats.NumNoGroundCells;
						continue;
					}

					bool bDuplicateHeight = false;
					for (float ExistingHeight : AcceptedGroundHeights)
					{
						if (std::abs(ExistingHeight - GroundHit.WorldHitLocation.Z) <= ProbeInset * 2.0f)
						{
							bDuplicateHeight = true;
							break;
						}
					}
					if (bDuplicateHeight)
					{
						continue;
					}

					FVector GroundNormal = GroundHit.WorldNormal;
					if (GroundNormal.IsNearlyZero())
					{
						GroundNormal = GroundHit.ImpactNormal;
					}
					GroundNormal.Normalize();
					if (GroundNormal.Dot(FVector::UpVector) + NavEpsilon < SlopeCos)
					{
						++BuildStats.NumRejectedSlope;
						continue;
					}

					const FVector StandingPoint(CellCenterX, CellCenterY, GroundHit.WorldHitLocation.Z);
					const FVector CapsuleCenter = StandingPoint + FVector::UpVector *
						(Settings.AgentHeight * 0.5f + Settings.ClearanceOffset);
					if (World->PhysicsOverlapAnyByObjectTypes(
						CapsuleCenter, FQuat::Identity, ClearanceShape, StaticMask, QueryOwner))
					{
						++BuildStats.NumRejectedClearance;
						continue;
					}

					FVoxelNavigationNode Node;
					Node.Coord = { X, Y, Z };
					Node.Position = StandingPoint;
					Node.GroundNormal = GroundNormal;
					const int32 NodeIndex = static_cast<int32>(Nodes.size());
					Nodes.push_back(Node);
					ColumnNodes[static_cast<size_t>(FlattenColumn(X, Y))].push_back(NodeIndex);
					AcceptedGroundHeights.push_back(StandingPoint.Z);
				}
			}
		}
	}

	// Cardinal edges first. Diagonals are added only when both adjacent cardinal
	// columns can support the same transition, preventing corner cutting.
	const int32 CardinalOffsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	for (int32 NodeIndex = 0; NodeIndex < static_cast<int32>(Nodes.size()); ++NodeIndex)
	{
		const FVoxelNavigationNode& Node = Nodes[static_cast<size_t>(NodeIndex)];
		for (const auto& Offset : CardinalOffsets)
		{
			const int32 NX = Node.Coord.X + Offset[0];
			const int32 NY = Node.Coord.Y + Offset[1];
			if (!IsValidColumn(NX, NY)) continue;

			for (int32 Candidate : ColumnNodes[static_cast<size_t>(FlattenColumn(NX, NY))])
			{
				if (std::abs(Nodes[static_cast<size_t>(Candidate)].Position.Z - Node.Position.Z) <= Settings.MaxNeighborHeightDelta + NavEpsilon &&
					CanTraverse(World, NodeIndex, Candidate, QueryOwner))
				{
					AddDirectedEdge(NodeIndex, Candidate);
				}
			}
		}
	}

	const int32 DiagonalOffsets[4][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
	for (int32 NodeIndex = 0; NodeIndex < static_cast<int32>(Nodes.size()); ++NodeIndex)
	{
		const FVoxelNavigationNode& Node = Nodes[static_cast<size_t>(NodeIndex)];
		for (const auto& Offset : DiagonalOffsets)
		{
			const int32 NX = Node.Coord.X + Offset[0];
			const int32 NY = Node.Coord.Y + Offset[1];
			if (!IsValidColumn(NX, NY)) continue;

			for (int32 Candidate : ColumnNodes[static_cast<size_t>(FlattenColumn(NX, NY))])
			{
				const float CandidateZ = Nodes[static_cast<size_t>(Candidate)].Position.Z;
				if (std::abs(CandidateZ - Node.Position.Z) > Settings.MaxNeighborHeightDelta + NavEpsilon)
				{
					continue;
				}
				if (!HasCardinalBridge(NodeIndex, Candidate, Node.Coord.X + Offset[0], Node.Coord.Y) ||
					!HasCardinalBridge(NodeIndex, Candidate, Node.Coord.X, Node.Coord.Y + Offset[1]))
				{
					continue;
				}
				if (CanTraverse(World, NodeIndex, Candidate, QueryOwner))
				{
					AddDirectedEdge(NodeIndex, Candidate);
				}
			}
		}
	}

	BuildStats.NumWalkableNodes = static_cast<int32>(Nodes.size());
	BuildStats.BuildTimeMs = ElapsedMilliseconds(StartTime);
	bBuilt = !Nodes.empty();
	return bBuilt;
}

FVoxelNavigationPathResult FVoxelNavigationGrid::FindPath(
	const FVector& Start,
	const FVector& Goal,
	float GoalAcceptanceRadius,
	float MaxStartSnapDistance,
	float MaxPathLength) const
{
	SCOPE_STAT_CAT("VoxelNav.FindPath", "Navigation");
	const auto StartTime = std::chrono::steady_clock::now();
	FVoxelNavigationPathResult Result;

	const int32 StartNode = FindNearestNode(Start, MaxStartSnapDistance);
	const int32 GoalNode = FindNearestNode(Goal);
	if (!bBuilt || StartNode < 0)
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoStart;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}
	if (GoalNode < 0)
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}

	const size_t NodeCount = Nodes.size();
	const float Infinity = (std::numeric_limits<float>::max)();
	TArray<float> Cost(NodeCount, Infinity);
	TArray<int32> Parent(NodeCount, -1);
	TArray<bool> Closed(NodeCount, false);
	std::priority_queue<FOpenNode> Open;

	Cost[static_cast<size_t>(StartNode)] = 0.0f;
	Open.push({ StartNode, FVector::Distance(Nodes[static_cast<size_t>(StartNode)].Position, Goal) });
	int32 BestReachable = StartNode;
	float BestGoalDistance = FVector::Distance(Nodes[static_cast<size_t>(StartNode)].Position, Goal);
	bool bReachedGoalNode = false;

	while (!Open.empty())
	{
		const int32 Current = Open.top().NodeIndex;
		Open.pop();
		if (Current < 0 || Closed[static_cast<size_t>(Current)]) continue;
		Closed[static_cast<size_t>(Current)] = true;
		++Result.NumExpandedNodes;

		const float DistanceToGoal = FVector::Distance(Nodes[static_cast<size_t>(Current)].Position, Goal);
		if (DistanceToGoal < BestGoalDistance)
		{
			BestGoalDistance = DistanceToGoal;
			BestReachable = Current;
		}
		if (Current == GoalNode)
		{
			BestReachable = Current;
			bReachedGoalNode = true;
			break;
		}

		for (int32 Neighbor : Nodes[static_cast<size_t>(Current)].Neighbors)
		{
			if (Neighbor < 0 || Closed[static_cast<size_t>(Neighbor)]) continue;
			const float EdgeCost = FVector::Distance(
				Nodes[static_cast<size_t>(Current)].Position,
				Nodes[static_cast<size_t>(Neighbor)].Position);
			const float NewCost = Cost[static_cast<size_t>(Current)] + EdgeCost;
			if ((MaxPathLength > 0.0f && NewCost > MaxPathLength) || NewCost >= Cost[static_cast<size_t>(Neighbor)])
			{
				continue;
			}

			Cost[static_cast<size_t>(Neighbor)] = NewCost;
			Parent[static_cast<size_t>(Neighbor)] = Current;
			const float Heuristic = FVector::Distance(Nodes[static_cast<size_t>(Neighbor)].Position, Goal);
			Open.push({ Neighbor, NewCost + Heuristic });
		}
	}

	if (BestReachable < 0)
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}

	TArray<FVector> ReversePoints;
	for (int32 Current = BestReachable; Current >= 0; Current = Parent[static_cast<size_t>(Current)])
	{
		ReversePoints.push_back(Nodes[static_cast<size_t>(Current)].Position);
		if (Current == StartNode) break;
	}
	if (ReversePoints.empty() || FVector::Distance(ReversePoints.back(), Nodes[static_cast<size_t>(StartNode)].Position) > NavEpsilon)
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}

	Result.Points.assign(ReversePoints.rbegin(), ReversePoints.rend());
	for (size_t Index = 1; Index < Result.Points.size(); ++Index)
	{
		Result.PathLength += FVector::Distance(Result.Points[Index - 1], Result.Points[Index]);
	}
	Result.bSuccess = true;
	Result.bPartial = !bReachedGoalNode || FVector::Distance(Result.Points.back(), Goal) > GoalAcceptanceRadius;
	Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
	return Result;
}

bool FVoxelNavigationGrid::Contains(const FVector& Point) const
{
	const FVector Max = BoundsCenter + BoundsExtent;
	return Point.X >= BoundsMin.X && Point.X <= Max.X &&
		Point.Y >= BoundsMin.Y && Point.Y <= Max.Y &&
		Point.Z >= BoundsMin.Z && Point.Z <= Max.Z;
}

int32 FVoxelNavigationGrid::FlattenColumn(int32 X, int32 Y) const
{
	return Y * SizeX + X;
}

bool FVoxelNavigationGrid::IsValidColumn(int32 X, int32 Y) const
{
	return X >= 0 && X < SizeX && Y >= 0 && Y < SizeY;
}

int32 FVoxelNavigationGrid::FindNearestNode(const FVector& Point, float MaxDistance) const
{
	int32 BestNode = -1;
	float BestDistanceSquared = (std::numeric_limits<float>::max)();
	for (int32 Index = 0; Index < static_cast<int32>(Nodes.size()); ++Index)
	{
		const float DistanceSquared = FVector::DistSquared(Nodes[static_cast<size_t>(Index)].Position, Point);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestNode = Index;
		}
	}
	if (MaxDistance > 0.0f && BestDistanceSquared > MaxDistance * MaxDistance)
	{
		return -1;
	}
	return BestNode;
}

bool FVoxelNavigationGrid::CanTraverse(UWorld* World, int32 FromNode, int32 ToNode, const AActor* QueryOwner) const
{
	if (!World || FromNode < 0 || ToNode < 0) return false;
	const FVector Lift = FVector::UpVector * (BuildSettings.AgentHeight * 0.5f + BuildSettings.ClearanceOffset);
	const FVector Start = Nodes[static_cast<size_t>(FromNode)].Position + Lift;
	const FVector End = Nodes[static_cast<size_t>(ToNode)].Position + Lift;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(BuildSettings.AgentRadius, BuildSettings.AgentHeight * 0.5f);
	FHitResult Hit;
	return !World->PhysicsSweepByObjectTypes(
		Start, End, FQuat::Identity, Shape, Hit,
		ObjectTypeBit(ECollisionChannel::WorldStatic), QueryOwner);
}

bool FVoxelNavigationGrid::HasCardinalBridge(int32 FromNode, int32 ToNode, int32 BridgeX, int32 BridgeY) const
{
	if (FromNode < 0 || ToNode < 0 || !IsValidColumn(BridgeX, BridgeY)) return false;
	const TArray<int32>& FromNeighbors = Nodes[static_cast<size_t>(FromNode)].Neighbors;
	for (int32 BridgeNode : ColumnNodes[static_cast<size_t>(FlattenColumn(BridgeX, BridgeY))])
	{
		if (std::find(FromNeighbors.begin(), FromNeighbors.end(), BridgeNode) == FromNeighbors.end())
		{
			continue;
		}
		const TArray<int32>& BridgeNeighbors = Nodes[static_cast<size_t>(BridgeNode)].Neighbors;
		if (std::find(BridgeNeighbors.begin(), BridgeNeighbors.end(), ToNode) != BridgeNeighbors.end())
		{
			return true;
		}
	}
	return false;
}

void FVoxelNavigationGrid::AddDirectedEdge(int32 FromNode, int32 ToNode)
{
	if (FromNode < 0 || ToNode < 0 || FromNode == ToNode) return;
	TArray<int32>& Neighbors = Nodes[static_cast<size_t>(FromNode)].Neighbors;
	if (std::find(Neighbors.begin(), Neighbors.end(), ToNode) == Neighbors.end())
	{
		Neighbors.push_back(ToNode);
		++BuildStats.NumDirectedEdges;
	}
}
