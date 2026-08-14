#include "pch.h"

#include "AI/Navigation/VoxelNavigationGrid.h"

#include "Core/Logging/Log.h"
#include "Profiling/Stats/MemoryStats.h"
#include "Profiling/Stats/Stats.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>

namespace
{
	constexpr float INF = FLT_MAX;
	constexpr float Epsilon = 1.e-4f;

	constexpr int NeighborOffsets[8][2] =
	{
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
	};

	// A*에서 priority_queue의 원소. {index, score}와 동일
	struct FOpenNode
	{
		int Index = -1;
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
} // namespace

FVoxelNavigationGrid::~FVoxelNavigationGrid()
{
	if (bRuntimeMemoryStatsEnabled)
	{
		MemoryStats::SubVoxelNavigationMemory(RuntimeMemoryBytes);
	}
}

bool FVoxelNavigationGrid::PackNeighborChunkDelta(const FVoxelCoord& Delta, uint8& OutPacked)
{
	if (Delta.X < -1 || Delta.X > 1 || Delta.Y < -1 || Delta.Y > 1 || Delta.Z < -1 || Delta.Z > 1 ||
		(Delta.X == 0 && Delta.Y == 0 && Delta.Z == 0))
	{
		return false;
	}
	OutPacked = static_cast<uint8>((Delta.X + 1) | ((Delta.Y + 1) << 2) | ((Delta.Z + 1) << 4));
	return true;
}

bool FVoxelNavigationGrid::UnpackNeighborChunkDelta(uint8 Packed, FVoxelCoord& OutDelta)
{
	if ((Packed & 0xc0u) != 0)
	{
		return false;
	}
	const int X = Packed & 0x3;
	const int Y = (Packed >> 2) & 0x3;
	const int Z = (Packed >> 4) & 0x3;
	if (X == 3 || Y == 3 || Z == 3)
	{
		return false;
	}
	OutDelta = { X - 1, Y - 1, Z - 1 };
	return OutDelta.X != 0 || OutDelta.Y != 0 || OutDelta.Z != 0;
}

int FVoxelNavigationGrid::FindChunkIndexByCoord(const FVoxelCoord& Coord) const
{
	return IsValidL1ChunkCoord(Coord.X, Coord.Y, Coord.Z)
		? L1ChunkLookup[FlattenL1Chunk(Coord.X, Coord.Y, Coord.Z)] : -1;
}

bool FVoxelNavigationGrid::ValidateLoadedChunkPayload(const FBakedVoxelNavigationChunk& Chunk) const
{
	FBakedNavPortal PreviousA = InvalidBakedNavPortal;
	FBakedNavPortal PreviousB = InvalidBakedNavPortal;

	// 포탈 & 청크 내부 연결의 유효성 검사
	for (const FBakedVoxelNavigationIntraEdge& Edge : Chunk.IntraEdges)
	{
		if (Edge.PortalA >= NavL1ChunkCellCount || Edge.PortalB >= NavL1ChunkCellCount ||
			Chunk.Cells[Edge.PortalA] == 0 || Chunk.Cells[Edge.PortalB] == 0 ||
			Edge.PortalA >= Edge.PortalB || 
			!std::isfinite(Edge.Cost) || Edge.Cost < 0.0f ||
			(PreviousA == Edge.PortalA && PreviousB == Edge.PortalB))
		{
			return false;
		}
		PreviousA = Edge.PortalA;
		PreviousB = Edge.PortalB;
	}
	// 청크 간 연결의 유효성 검사
	for (const FBakedVoxelNavigationExternalLink& Link : Chunk.ExternalLinks)
	{
		FVoxelCoord Delta;
		if (Link.LocalPortalId >= NavL1ChunkCellCount || Link.NeighborPortalId >= NavL1ChunkCellCount ||
			Chunk.Cells[Link.LocalPortalId] == 0 || 
			!std::isfinite(Link.Cost) || Link.Cost < 0.0f ||
			!UnpackNeighborChunkDelta(Link.PackedNeighborChunkDelta, Delta))
		{
			return false;
		}
	}
	return true;
}

void FVoxelNavigationGrid::ClearRuntimeTopology()
{
	bBuilt = false;
	L1Chunks.clear();
	Portals.clear();
	BakedChunks.clear();
	FreePortalIndices.clear();
	L1ChunkLookup.assign(L1ChunkCountX * L1ChunkCountY * L1ChunkCountZ, -1);
}

bool FVoxelNavigationGrid::ApplyLoadedChunk(const FBakedVoxelNavigationChunk& Baked)
{
	if (!IsValidL1ChunkCoord(Baked.Coord.X, Baked.Coord.Y, Baked.Coord.Z) || !ValidateLoadedChunkPayload(Baked))
		return false;

	int ChunkIndex = FindChunkIndexByCoord(Baked.Coord);
	if (ChunkIndex >= 0)
	{
		// A reload replaces just this chunk and its incident seams.
		DisconnectChunkSeams(ChunkIndex);
		for (int PortalIndex : L1Chunks[ChunkIndex].PortalIndices)
			DeactivatePortal(PortalIndex);
		L1Chunks[ChunkIndex].PortalIndices.clear();
		BakedChunks[ChunkIndex] = Baked;
		L1Chunks[ChunkIndex].Cells = Baked.Cells;
	}
	else
	{
		ChunkIndex = static_cast<int>(L1Chunks.size());
		FVoxelNavigationL1Chunk RuntimeChunk;
		RuntimeChunk.Coord = Baked.Coord;
		RuntimeChunk.Cells = Baked.Cells;
		L1Chunks.push_back(std::move(RuntimeChunk));
		BakedChunks.push_back(Baked);
		L1ChunkLookup[FlattenL1Chunk(Baked.Coord.X, Baked.Coord.Y, Baked.Coord.Z)] = ChunkIndex;
	}

	BuildChunkTopology(ChunkIndex);
	ConnectChunkSeams(ChunkIndex);
	return true;
}

bool FVoxelNavigationGrid::ApplyRemovedChunk(const FVoxelCoord& Coord)
{
	const int ChunkIndex = FindChunkIndexByCoord(Coord);
	if (ChunkIndex < 0)
		return true;

	DisconnectChunkSeams(ChunkIndex);
	for (int PortalIndex : L1Chunks[ChunkIndex].PortalIndices)
		DeactivatePortal(PortalIndex);
	L1ChunkLookup[FlattenL1Chunk(Coord.X, Coord.Y, Coord.Z)] = -1;

	const int LastChunkIndex = static_cast<int>(L1Chunks.size()) - 1;
	if (ChunkIndex != LastChunkIndex)
	{
		L1Chunks[ChunkIndex] = std::move(L1Chunks.back());
		BakedChunks[ChunkIndex] = std::move(BakedChunks.back());
		const FVoxelCoord& MovedCoord = L1Chunks[ChunkIndex].Coord;
		L1ChunkLookup[FlattenL1Chunk(MovedCoord.X, MovedCoord.Y, MovedCoord.Z)] = ChunkIndex;
		for (int PortalIndex : L1Chunks[ChunkIndex].PortalIndices)
		{
			Portals[PortalIndex].ChunkIndex = ChunkIndex;
		}
	}
	L1Chunks.pop_back();
	BakedChunks.pop_back();
	return true;
}

void FVoxelNavigationGrid::BuildChunkTopology(int ChunkIndex)
{
	const FBakedVoxelNavigationChunk& Baked = BakedChunks[ChunkIndex];
	for (const FBakedVoxelNavigationIntraEdge& Edge : Baked.IntraEdges)
	{
		const int A = FindOrAddPortal(ChunkIndex, Edge.PortalA);
		const int B = FindOrAddPortal(ChunkIndex, Edge.PortalB);
		AddAbstractEdge(A, B, Edge.Cost);
		AddAbstractEdge(B, A, Edge.Cost);
	}
	for (const FBakedVoxelNavigationExternalLink& Link : Baked.ExternalLinks)
	{
		FindOrAddPortal(ChunkIndex, Link.LocalPortalId);
	}
}

bool FVoxelNavigationGrid::HasReciprocalExternalLink(
	const FBakedVoxelNavigationExternalLink& Link, int ToChunkIndex) const
{
	FVoxelCoord Delta;
	if (!UnpackNeighborChunkDelta(Link.PackedNeighborChunkDelta, Delta))
		return false;
	uint8 ReverseDelta = 0;
	if (!PackNeighborChunkDelta({ -Delta.X, -Delta.Y, -Delta.Z }, ReverseDelta))
		return false;
	const FBakedVoxelNavigationChunk& Neighbor = BakedChunks[ToChunkIndex];
	return std::any_of(Neighbor.ExternalLinks.begin(), Neighbor.ExternalLinks.end(), [&Link, ReverseDelta](const auto& Candidate)
	{
		return Candidate.LocalPortalId == Link.NeighborPortalId && Candidate.NeighborPortalId == Link.LocalPortalId &&
			Candidate.PackedNeighborChunkDelta == ReverseDelta && std::abs(Candidate.Cost - Link.Cost) <= Epsilon;
	});
}

void FVoxelNavigationGrid::ConnectExternalLinksForChunk(int ChunkIndex)
{
	const FBakedVoxelNavigationChunk& Baked = BakedChunks[ChunkIndex];
	for (const FBakedVoxelNavigationExternalLink& Link : Baked.ExternalLinks)
	{
		FVoxelCoord Delta;
		UnpackNeighborChunkDelta(Link.PackedNeighborChunkDelta, Delta);
		const FVoxelCoord NeighborCoord{ Baked.Coord.X + Delta.X, Baked.Coord.Y + Delta.Y, Baked.Coord.Z + Delta.Z };
		const int NeighborChunkIndex = FindChunkIndexByCoord(NeighborCoord);
		if (NeighborChunkIndex < 0 || BakedChunks[NeighborChunkIndex].Cells[Link.NeighborPortalId] == 0 ||
			!HasReciprocalExternalLink(Link, NeighborChunkIndex))
		{
			continue;
		}
		AddAbstractEdge(FindOrAddPortal(ChunkIndex, Link.LocalPortalId),
			FindOrAddPortal(NeighborChunkIndex, Link.NeighborPortalId), Link.Cost);
	}
}

void FVoxelNavigationGrid::ConnectChunkSeams(int ChunkIndex)
{
	ConnectExternalLinksForChunk(ChunkIndex);
	const FVoxelCoord Coord = BakedChunks[ChunkIndex].Coord;
	for (int Z = -1; Z <= 1; ++Z)
	{
		for (int Y = -1; Y <= 1; ++Y)
		{
			for (int X = -1; X <= 1; ++X)
			{
				if (X == 0 && Y == 0 && Z == 0) continue;
				const int Neighbor = FindChunkIndexByCoord({ Coord.X + X, Coord.Y + Y, Coord.Z + Z });
				if (Neighbor >= 0) ConnectExternalLinksForChunk(Neighbor);
			}
		}
	}
}

void FVoxelNavigationGrid::DisconnectChunkSeams(int ChunkIndex)
{
	if (ChunkIndex < 0 || ChunkIndex >= static_cast<int>(L1Chunks.size()))
		return;

	const FVoxelCoord Coord = L1Chunks[ChunkIndex].Coord;
	for (int Z = -1; Z <= 1; ++Z)
	{
		for (int Y = -1; Y <= 1; ++Y)
		{
			for (int X = -1; X <= 1; ++X)
			{
				if (X == 0 && Y == 0 && Z == 0) continue;
				const int NeighborChunkIndex = FindChunkIndexByCoord({ Coord.X + X, Coord.Y + Y, Coord.Z + Z });
				if (NeighborChunkIndex < 0) continue;

				for (int PortalIndex : L1Chunks[NeighborChunkIndex].PortalIndices)
				{
					TArray<FVoxelNavigationAbstractEdge>& Edges = Portals[PortalIndex].Edges;
					Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [this, ChunkIndex](const auto& Edge)
					{
						return Edge.ToPortal >= 0 && Edge.ToPortal < static_cast<int>(Portals.size()) &&
							Portals[Edge.ToPortal].bActive && Portals[Edge.ToPortal].ChunkIndex == ChunkIndex;
					}), Edges.end());
				}
			}
		}
	}
}


bool FVoxelNavigationGrid::InitializeRuntime(
	const FVector& InBoundsCenter,
	const FVector& InBoundsExtent,
	const FVoxelNavigationBuildSettings& Settings)
{
	if (InBoundsExtent.X <= Epsilon || InBoundsExtent.Y <= Epsilon || InBoundsExtent.Z <= Epsilon ||
		!std::isfinite(InBoundsCenter.X) || !std::isfinite(InBoundsCenter.Y) || !std::isfinite(InBoundsCenter.Z) ||
		!std::isfinite(InBoundsExtent.X) || !std::isfinite(InBoundsExtent.Y) || !std::isfinite(InBoundsExtent.Z) ||
		!std::isfinite(Settings.AgentRadius) || Settings.AgentRadius <= Epsilon ||
		!std::isfinite(Settings.AgentHeight) || Settings.AgentHeight <= Epsilon)
	{
		return false;
	}

	BoundsCenter = InBoundsCenter;
	BoundsExtent = InBoundsExtent;
	BoundsMin = BoundsCenter - BoundsExtent;
	BuildSettings = Settings;
	CellCountX = static_cast<int>(std::ceil(BoundsExtent.X * 2.0f / NavVoxelCellSize));
	CellCountY = static_cast<int>(std::ceil(BoundsExtent.Y * 2.0f / NavVoxelCellSize));
	CellCountZ = static_cast<int>(std::ceil(BoundsExtent.Z * 2.0f / NavVoxelCellSize));
	L1ChunkCountX = (CellCountX + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	L1ChunkCountY = (CellCountY + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	L1ChunkCountZ = (CellCountZ + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	bRuntimeInitialized = true;
	++NavigationDataGeneration;
	ClearRuntimeTopology();
	RefreshRuntimeMemory();

	return true;
}

bool FVoxelNavigationGrid::AddLoadedChunks(const TArray<FBakedVoxelNavigationChunk>& LoadedChunks)
{
	if (!bRuntimeInitialized || LoadedChunks.empty())
		return LoadedChunks.empty();
	for (const FBakedVoxelNavigationChunk& Loaded : LoadedChunks)
	{
		if (!IsValidL1ChunkCoord(Loaded.Coord.X, Loaded.Coord.Y, Loaded.Coord.Z) || !ValidateLoadedChunkPayload(Loaded))
			return false;
	}
	for (const FBakedVoxelNavigationChunk& Loaded : LoadedChunks)
	{
		if (!ApplyLoadedChunk(Loaded)) return false;
	}

	size_t NumWalkableNodes = 0;
	for (const FVoxelNavigationL1Chunk& Chunk : L1Chunks)
		for (uint8 Cell : Chunk.Cells) NumWalkableNodes += Cell != 0;
	bBuilt = NumWalkableNodes > 0;
	RefreshRuntimeMemory();
	return bBuilt;
}

bool FVoxelNavigationGrid::RemoveLoadedChunks(const TArray<FVoxelCoord>& ChunkCoords)
{
	if (!bRuntimeInitialized || ChunkCoords.empty()) return ChunkCoords.empty();
	for (const FVoxelCoord& Coord : ChunkCoords)
	{
		if (!ApplyRemovedChunk(Coord)) return false;
	}

	size_t NumWalkableNodes = 0;
	for (const FVoxelNavigationL1Chunk& Chunk : L1Chunks)
		for (uint8 Cell : Chunk.Cells) NumWalkableNodes += Cell != 0;
	bBuilt = NumWalkableNodes > 0;
	RefreshRuntimeMemory();
	return true;
}

void FVoxelNavigationGrid::ClearNavigationData()
{
	++NavigationDataGeneration;
	ClearRuntimeTopology();
	RefreshRuntimeMemory();
}

void FVoxelNavigationGrid::GatherLoadedChunkCoords(TArray<FVoxelCoord>& OutCoords) const
{
	OutCoords.clear();
	OutCoords.reserve(BakedChunks.size());
	for (const FBakedVoxelNavigationChunk& Chunk : BakedChunks)
	{
		OutCoords.push_back(Chunk.Coord);
	}
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

	if (!bBuilt)
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoData;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}

	const FCellRef StartCell = FindNearestCell(Start, MaxStartSnapDistance);
	const FCellRef GoalCell = FindNearestCell(Goal);
	if (!StartCell.IsValid())
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoStart;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}
	if (!GoalCell.IsValid())
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}

	TArray<FCellRef> ConcreteCells;	// 최종 계산된 경로 노드 리스트 (마지막에 FVector로 변환되어서 반환됨)
	auto FinalizeConcretePath = [this, &Result, Goal, GoalAcceptanceRadius, MaxPathLength](
		const TArray<FCellRef>& RawCells, bool bForcePartial)
	{
		TArray<FCellRef> SmoothedCells;
		Result.NumRawPoints = static_cast<int32>(RawCells.size());
		const auto SmoothingStart = std::chrono::steady_clock::now();
		{
			SCOPE_STAT_CAT("VoxelNav.SmoothPath", "Navigation");
			SmoothConcretePath(RawCells, SmoothedCells, &Result.NumVisibilityTests);
		}
		Result.SmoothingTimeMs = ElapsedMilliseconds(SmoothingStart);
		Result.NumSmoothedPoints = static_cast<int32>(SmoothedCells.size());
		if (SmoothedCells.empty())
		{
			Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
			return false;
		}

		Result.Points.clear();
		Result.Points.reserve(SmoothedCells.size());
		Result.RawPoints.clear();
		Result.RawPoints.reserve(RawCells.size());
		Result.PathLength = 0.0f;
		for (const FCellRef& Cell : RawCells)
		{
			Result.RawPoints.push_back(GetCellPosition(Cell));
		}
		for (const FCellRef& Cell : SmoothedCells)
		{
			Result.Points.push_back(GetCellPosition(Cell));
		}
		for (size_t Index = 1; Index < Result.Points.size(); ++Index)
		{
			Result.PathLength += FVector::Distance(Result.Points[Index - 1], Result.Points[Index]);
		}
		if (MaxPathLength > 0.0f && Result.PathLength > MaxPathLength + Epsilon)
		{
			Result.Points.clear();
			Result.PathLength = 0.0f;
			Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
			return false;
		}

		Result.bSuccess = true;
		Result.bPartial = bForcePartial || FVector::Distance(Result.Points.back(), Goal) > GoalAcceptanceRadius;
		return true;
	};
	if (StartCell.ChunkIndex == GoalCell.ChunkIndex)	// 만약 목적지가 같은 청크 안에 있다면
	{
		const float DirectCost = FindLocalPath(StartCell, GoalCell, &ConcreteCells);
		if (DirectCost < (std::numeric_limits<float>::max)() &&
			(MaxPathLength <= 0.0f || DirectCost <= MaxPathLength))
		{
			FinalizeConcretePath(ConcreteCells, false);
			Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
			return Result;
		}
		// NOTE: 같은 청크 안에 있어도 벽에 막히는 등 경로가 없을 수 있음
	}

	// 경로의 마지막 간선 미리 구해두기: '목적지를 포함한 청크' 내에서 포탈과 목적지간의 거리
	TArray<float> GoalPortalCosts(Portals.size(), INF);
	for (int PortalIndex : L1Chunks[GoalCell.ChunkIndex].PortalIndices)
	{
		const FCellRef PortalCell{ Portals[PortalIndex].ChunkIndex, Portals[PortalIndex].LocalCell };
		GoalPortalCosts[PortalIndex] = FindLocalPath(PortalCell, GoalCell);
	}

	// abstract graph 상에서 A* 수행
	TArray<float> Cost(Portals.size(), INF);
	TArray<int> Parent(Portals.size(), -1);	// 경로 복구용으로 부모 기록
											// -1: 미기록, -2: 없음으로 명시적 표시
	TArray<uint8> Visited(Portals.size(), 0);
	std::priority_queue<FOpenNode> PriorityQue;
	for (int PortalIndex : L1Chunks[StartCell.ChunkIndex].PortalIndices)
	{
		const FCellRef PortalCell{ Portals[PortalIndex].ChunkIndex, Portals[PortalIndex].LocalCell };
		const float StartCost = FindLocalPath(StartCell, PortalCell);
		if (StartCost >= INF || (MaxPathLength > 0.0f && StartCost > MaxPathLength)) 
			continue;

		Cost[PortalIndex] = StartCost;
		Parent[PortalIndex] = -2;
		PriorityQue.push({ PortalIndex, StartCost + FVector::Distance(GetCellPosition(PortalCell), Goal) });
	}
	// 경로 산출 불가능할 때 그나마 가장 가까운 위치로 가기 위해 BestGoal과 별개로 BestReachable 기록
	int BestGoalPortal = -1; 
	float BestGoalCost = INF;
	int BestReachablePortal = -1;
	float BestReachableDistance = FVector::Distance(GetCellPosition(StartCell), Goal);
	while (!PriorityQue.empty()) // A star
	{
		const FOpenNode NextNode = PriorityQue.top();
		PriorityQue.pop();
		if (NextNode.Score >= BestGoalCost) 
			break;

		const int CurNodeId = NextNode.Index;
		if (CurNodeId < 0 || Visited[CurNodeId] != 0) 
			continue;

		Visited[CurNodeId] = 1;
		++Result.NumExpandedNodes;
		const FVoxelNavigationPortal& CurrentPortal = Portals[CurNodeId];
		const FVector CurrentPortalLocation = GetCellPosition({ CurrentPortal.ChunkIndex, CurrentPortal.LocalCell });
		const float ReachableDistance = FVector::Distance(CurrentPortalLocation, Goal);
		if (ReachableDistance < BestReachableDistance)
		{
			BestReachableDistance = ReachableDistance;
			BestReachablePortal = CurNodeId;
		}

		const float GoalTail = GoalPortalCosts[CurNodeId];
		if (GoalTail < INF)
		{
			const float Total = Cost[CurNodeId] + GoalTail;
			if ((MaxPathLength <= 0.0f || Total <= MaxPathLength) && Total < BestGoalCost)
			{
				BestGoalCost = Total;
				BestGoalPortal = CurNodeId;
			}
		}

		for (const FVoxelNavigationAbstractEdge& Edge : Portals[CurNodeId].Edges)
		{
			if (Edge.ToPortal < 0 || Visited[Edge.ToPortal] != 0) 
				continue;

			const float NewCost = Cost[CurNodeId] + Edge.Cost;
			if ( (MaxPathLength > 0.0f && NewCost > MaxPathLength) 
				|| NewCost >= Cost[Edge.ToPortal] )
				continue;

			Cost[Edge.ToPortal] = NewCost;
			Parent[Edge.ToPortal] = CurNodeId;
			const FVoxelNavigationPortal& NextPortal = Portals[Edge.ToPortal];
			const FCellRef NextCell{ NextPortal.ChunkIndex, NextPortal.LocalCell };
			const float Heuristic = FVector::Distance(GetCellPosition(NextCell), Goal);
			PriorityQue.push({ Edge.ToPortal, NewCost + Heuristic });
		}
	}

	bool bAbstractPartial = false;
	if (BestGoalPortal < 0 && BestReachablePortal >= 0)
	{
		BestGoalPortal = BestReachablePortal;
		bAbstractPartial = true;
	}
	if (BestGoalPortal < 0) // Abstract graph 상에서는 Partial path조차 못찾은 경우
	{
		// 청크 안에서 그나마 goal과 가까운 쪽으로 가는 경로를 결과로 반환
		const float PartialCost = FindLocalPath(StartCell, StartCell, &ConcreteCells, true, &Goal);
		if (PartialCost >= INF || (MaxPathLength > 0.0f && PartialCost > MaxPathLength))
		{
			Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
			Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
			return Result;
		}
		FinalizeConcretePath(ConcreteCells, true);
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}

	// Parent를 사용하여 경로 복구 (백트래킹)
	TArray<int> ReversePortalPath;
	for (int Current = BestGoalPortal; Current >= 0; Current = Parent[Current])
	{
		ReversePortalPath.push_back(Current);
		if (Parent[Current] == -2) break;
	}
	if (ReversePortalPath.empty() || Parent[ReversePortalPath.back()] != -2)
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}
	TArray<int> PortalPath(ReversePortalPath.rbegin(), ReversePortalPath.rend());

	// 청크&포탈 기반의 Abstract path를 복셀 기반의 concrete path로 변환
	auto AppendSegment = [&ConcreteCells](const TArray<FCellRef>& Segment)
	{
		for (const FCellRef& Cell : Segment)
		{
			if (ConcreteCells.empty() || !(ConcreteCells.back() == Cell)) ConcreteCells.push_back(Cell);
		}
	};

	TArray<FCellRef> Segment;
	FCellRef FirstPortalCell{ Portals[PortalPath.front()].ChunkIndex,
		Portals[PortalPath.front()].LocalCell };
	if (FindLocalPath(StartCell, FirstPortalCell, &Segment) >= INF)
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}
	AppendSegment(Segment);

	for (size_t Index = 1; Index < PortalPath.size(); ++Index)
	{
		const FVoxelNavigationPortal& Previous = Portals[PortalPath[Index - 1]];
		const FVoxelNavigationPortal& Current = Portals[PortalPath[Index]];
		const FCellRef PreviousCell{ Previous.ChunkIndex, Previous.LocalCell };
		const FCellRef CurrentCell{ Current.ChunkIndex, Current.LocalCell };
		Segment.clear();
		if (Previous.ChunkIndex == Current.ChunkIndex)
		{
			if (FindLocalPath(PreviousCell, CurrentCell, &Segment) >= INF)
			{
				Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
				Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
				return Result;
			}
		}
		else
		{
			Segment.push_back(PreviousCell);
			Segment.push_back(CurrentCell);
		}
		AppendSegment(Segment);
	}

	const FVoxelNavigationPortal& LastPortal = Portals[PortalPath.back()];
	Segment.clear();
	const FCellRef LastPortalCell{ LastPortal.ChunkIndex, LastPortal.LocalCell };
	const float TailCost = bAbstractPartial
		? FindLocalPath(LastPortalCell, LastPortalCell, &Segment, true, &Goal)
		: FindLocalPath(LastPortalCell, GoalCell, &Segment);
	if (TailCost >= INF)
	{
		Result.Failure = FVoxelNavigationPathResult::EFailure::NoPath;
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}
	AppendSegment(Segment);

	FinalizeConcretePath(ConcreteCells, bAbstractPartial);
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

void FVoxelNavigationGrid::GatherDebugWalkableNodes(int MaxNodes, TArray<FVector>& OutNodes) const
{
	OutNodes.clear();
	if (MaxNodes <= 0) 
		return;

	for (int ChunkIndex = 0; ChunkIndex < L1Chunks.size() && OutNodes.size() < MaxNodes; ++ChunkIndex)
	{
		for (int LocalCell = 0; LocalCell < NavL1ChunkCellCount && OutNodes.size() < MaxNodes; ++LocalCell)
		{
			if (!IsCellWalkable(ChunkIndex, LocalCell)) 
				continue;

			OutNodes.push_back(GetCellPosition({ ChunkIndex, LocalCell }));
		}
	}
}


void FVoxelNavigationGrid::GatherDebugChunkBoundaryLines(
	int MaxChunks,
	TArray<TPair<FVector, FVector>>& OutLines) const
{
	OutLines.clear();
	if (MaxChunks <= 0) 
		return;

	const FVector BoundsMax = BoundsMin + FVector(
		CellCountX * NavVoxelCellSize,
		CellCountY * NavVoxelCellSize,
		CellCountZ * NavVoxelCellSize);
	for (int ChunkIndex = 0; ChunkIndex < static_cast<int>(L1Chunks.size()) && ChunkIndex < MaxChunks; ++ChunkIndex)
	{
		const FVoxelCoord& Coord = L1Chunks[ChunkIndex].Coord;
		const float MinX = BoundsMin.X + Coord.X * NavL1ChunkSize;
		const float MinY = BoundsMin.Y + Coord.Y * NavL1ChunkSize;
		const float MaxX = (std::min)(MinX + NavL1ChunkSize, BoundsMax.X);
		const float MaxY = (std::min)(MinY + NavL1ChunkSize, BoundsMax.Y);
		const float Z = BoundsMin.Z + Coord.Z * NavL1ChunkSize;

		const FVector P0(MinX, MinY, Z);
		const FVector P1(MaxX, MinY, Z);
		const FVector P2(MaxX, MaxY, Z);
		const FVector P3(MinX, MaxY, Z);
		OutLines.push_back({ P0, P1 });
		OutLines.push_back({ P1, P2 });
		OutLines.push_back({ P2, P3 });
		OutLines.push_back({ P3, P0 });
	}
}

int FVoxelNavigationGrid::FlattenL1Chunk(int X, int Y, int Z) const
{
	return (Z * L1ChunkCountY + Y) * L1ChunkCountX + X;
}

bool FVoxelNavigationGrid::IsValidL1ChunkCoord(int X, int Y, int Z) const
{
	return X >= 0 && X < L1ChunkCountX && 
			Y >= 0 && Y < L1ChunkCountY && 
			Z >= 0 && Z < L1ChunkCountZ;
}

FCellRef FVoxelNavigationGrid::FindNearestCell(const FVector& Point, float MaxDistance) const
{
	FCellRef BestCell;
	float BestDistSquared = MaxDistance > 0.0f ? MaxDistance * MaxDistance : INF;
	TArray<uint8> ChunkVisited(L1Chunks.size(), 0);

	auto FindNearestCellInChunk = [&](int ChunkIndex)
	{
		if (ChunkIndex < 0 || ChunkIndex >= L1Chunks.size() ||
			ChunkVisited[ChunkIndex] != 0) 
			return;

		ChunkVisited[ChunkIndex] = 1;
		for (int LocalCell = 0; LocalCell < NavL1ChunkCellCount; ++LocalCell)
		{
			if (!IsCellWalkable(ChunkIndex, LocalCell)) 
				continue;

			const float DistanceSquared = FVector::DistSquared(GetCellPosition({ ChunkIndex, LocalCell }), Point);
			if (DistanceSquared < BestDistSquared)
			{
				BestDistSquared = DistanceSquared;
				BestCell = { ChunkIndex, LocalCell };
			}
		}
	};

	const int HomeX = static_cast<int>(std::floor((Point.X - BoundsMin.X) / NavL1ChunkSize));
	const int HomeY = static_cast<int>(std::floor((Point.Y - BoundsMin.Y) / NavL1ChunkSize));
	const int HomeZ = static_cast<int>(std::floor((Point.Z - BoundsMin.Z) / NavL1ChunkSize));
	for (int Z = HomeZ - 1; Z <= HomeZ + 1; ++Z)
	{
		for (int Y = HomeY - 1; Y <= HomeY + 1; ++Y)
		{
			for (int X = HomeX - 1; X <= HomeX + 1; ++X)
			{
				if (!IsValidL1ChunkCoord(X, Y, Z)) 
					continue;

				FindNearestCellInChunk(L1ChunkLookup[FlattenL1Chunk(X, Y, Z)]);
			}
		}
	}

	for (int ChunkIndex = 0; ChunkIndex < L1Chunks.size(); ++ChunkIndex)
	{
		if (ChunkVisited[ChunkIndex] != 0) 
			continue;

		const FVoxelCoord& Coord = L1Chunks[ChunkIndex].Coord;
		const FVector Min = BoundsMin + FVector(Coord.X, Coord.Y, Coord.Z) * NavL1ChunkSize;
		const FVector Max = Min + FVector::OneVector * NavL1ChunkSize;
		const float DX = Point.X < Min.X ? Min.X - Point.X : (Point.X > Max.X ? Point.X - Max.X : 0.0f);
		const float DY = Point.Y < Min.Y ? Min.Y - Point.Y : (Point.Y > Max.Y ? Point.Y - Max.Y : 0.0f);
		const float DZ = Point.Z < Min.Z ? Min.Z - Point.Z : (Point.Z > Max.Z ? Point.Z - Max.Z : 0.0f);
		if (DX * DX + DY * DY + DZ * DZ > BestDistSquared) 
			continue;

		FindNearestCellInChunk(ChunkIndex);
	}
	return BestCell;
}

FCellRef FVoxelNavigationGrid::FindLoadedCellByGlobalXY(int GlobalX, int GlobalY) const
{
	if (GlobalX < 0 || GlobalX >= CellCountX || GlobalY < 0 || GlobalY >= CellCountY)
	{
		return {};
	}

	const int ChunkX = GlobalX / NavL1ChunkCellsPerAxis;
	const int ChunkY = GlobalY / NavL1ChunkCellsPerAxis;
	const int LocalX = GlobalX % NavL1ChunkCellsPerAxis;
	const int LocalY = GlobalY % NavL1ChunkCellsPerAxis;
	const int LocalCell = LocalY * NavL1ChunkCellsPerAxis + LocalX;
	for (int ChunkZ = 0; ChunkZ < static_cast<int>(L1ChunkCountZ); ++ChunkZ)
	{
		const int ChunkIndex = FindChunkIndexByCoord({ ChunkX, ChunkY, ChunkZ });
		if (IsCellWalkable(ChunkIndex, LocalCell))
		{
			return { ChunkIndex, LocalCell };
		}
	}
	return {};
}

bool FVoxelNavigationGrid::GetGlobalCellXY(const FCellRef& Cell, int& OutGlobalX, int& OutGlobalY) const
{
	if (!IsCellWalkable(Cell.ChunkIndex, Cell.LocalCell))
	{
		return false;
	}

	const FVoxelCoord& ChunkCoord = L1Chunks[Cell.ChunkIndex].Coord;
	OutGlobalX = ChunkCoord.X * NavL1ChunkCellsPerAxis + Cell.LocalCell % NavL1ChunkCellsPerAxis;
	OutGlobalY = ChunkCoord.Y * NavL1ChunkCellsPerAxis + Cell.LocalCell / NavL1ChunkCellsPerAxis;
	return true;
}

bool FVoxelNavigationGrid::HasLineOfSightSupercover(const FCellRef& Start, const FCellRef& Goal) const
{
	int StartX = 0;
	int StartY = 0;
	int GoalX = 0;
	int GoalY = 0;
	if (!GetGlobalCellXY(Start, StartX, StartY) || !GetGlobalCellXY(Goal, GoalX, GoalY))
	{
		return false;
	}

	auto IsGlobalCellWalkable = [this](int GlobalX, int GlobalY)
	{
		return FindLoadedCellByGlobalXY(GlobalX, GlobalY).IsValid();
	};
	if (!IsGlobalCellWalkable(StartX, StartY))
	{
		return false;
	}

	int CurrentX = StartX;
	int CurrentY = StartY;
	const int DeltaX = GoalX - StartX;
	const int DeltaY = GoalY - StartY;
	const int StepX = DeltaX > 0 ? 1 : (DeltaX < 0 ? -1 : 0);
	const int StepY = DeltaY > 0 ? 1 : (DeltaY < 0 ? -1 : 0);
	const double Infinite = (std::numeric_limits<double>::infinity)();
	const double TDeltaX = StepX != 0 ? 1.0 / std::abs(DeltaX) : Infinite;
	const double TDeltaY = StepY != 0 ? 1.0 / std::abs(DeltaY) : Infinite;
	double TMaxX = StepX != 0 ? 0.5 / std::abs(DeltaX) : Infinite;
	double TMaxY = StepY != 0 ? 0.5 / std::abs(DeltaY) : Infinite;

	while (CurrentX != GoalX || CurrentY != GoalY)
	{
		if (TMaxX + 1.e-9 < TMaxY)
		{
			CurrentX += StepX;
			TMaxX += TDeltaX;
			if (!IsGlobalCellWalkable(CurrentX, CurrentY)) return false;
		}
		else if (TMaxY + 1.e-9 < TMaxX)
		{
			CurrentY += StepY;
			TMaxY += TDeltaY;
			if (!IsGlobalCellWalkable(CurrentX, CurrentY)) return false;
		}
		else
		{
			// A line through a grid corner touches both side cells as well as the diagonal cell.
			if (!IsGlobalCellWalkable(CurrentX + StepX, CurrentY) ||
				!IsGlobalCellWalkable(CurrentX, CurrentY + StepY))
			{
				return false;
			}
			CurrentX += StepX;
			CurrentY += StepY;
			TMaxX += TDeltaX;
			TMaxY += TDeltaY;
			if (!IsGlobalCellWalkable(CurrentX, CurrentY)) return false;
		}
	}
	return true;
}

void FVoxelNavigationGrid::SmoothConcretePath(
	const TArray<FCellRef>& RawCells, TArray<FCellRef>& OutCells, int32* OutVisibilityTests) const
{
	OutCells.clear();
	if (OutVisibilityTests)
	{
		*OutVisibilityTests = 0;
	}
	if (RawCells.empty())
	{
		return;
	}

	auto AppendUnique = [&OutCells](const FCellRef& Cell)
	{
		if (OutCells.empty() || !(OutCells.back() == Cell))
		{
			OutCells.push_back(Cell);
		}
	};
	AppendUnique(RawCells.front());
	const int LastIndex = static_cast<int>(RawCells.size()) - 1;
	for (int CurrentIndex = 0; CurrentIndex < LastIndex;)
	{
		// Test the farthest candidate first: the first visible cell is the greedy next waypoint.
		int NextIndex = CurrentIndex + 1;
		for (int CandidateIndex = LastIndex; CandidateIndex > CurrentIndex + 1; --CandidateIndex)
		{
			if (OutVisibilityTests)
			{
				++*OutVisibilityTests;
			}
			if (HasLineOfSightSupercover(RawCells[CurrentIndex], RawCells[CandidateIndex]))
			{
				NextIndex = CandidateIndex;
				break;
			}
		}
		AppendUnique(RawCells[NextIndex]);
		CurrentIndex = NextIndex;
	}
}

FVector FVoxelNavigationGrid::GetCellPosition(const FCellRef& Cell) const
{
	if (!Cell.IsValid() || Cell.ChunkIndex >= L1Chunks.size()) 
		return FVector::ZeroVector;

	const FVoxelNavigationL1Chunk& Chunk = L1Chunks[Cell.ChunkIndex];
	const uint8 HeightCode = Chunk.Cells[Cell.LocalCell];
	if (HeightCode == 0) 
		return FVector::ZeroVector;

	const int LocalX = Cell.LocalCell % NavL1ChunkCellsPerAxis;
	const int LocalY = Cell.LocalCell / NavL1ChunkCellsPerAxis;
	const int GlobalX = Chunk.Coord.X * NavL1ChunkCellsPerAxis + LocalX;
	const int GlobalY = Chunk.Coord.Y * NavL1ChunkCellsPerAxis + LocalY;
	const float ChunkBottom = BoundsMin.Z + (Chunk.Coord.Z * NavL1ChunkSize);
	return FVector(
		BoundsMin.X + (GlobalX + 0.5f) * NavVoxelCellSize,
		BoundsMin.Y + (GlobalY + 0.5f) * NavVoxelCellSize,
		ChunkBottom + (HeightCode - 1.0f) * (NavL1ChunkSize / 254.0f));
}

bool FVoxelNavigationGrid::IsCellWalkable(int ChunkIndex, int LocalCell) const
{
	return ChunkIndex >= 0 && ChunkIndex < L1Chunks.size() 
		&& LocalCell >= 0 && LocalCell < NavL1ChunkCellCount
		&& L1Chunks[ChunkIndex].Cells[LocalCell] != 0;
}

float FVoxelNavigationGrid::FindLocalPath(const FCellRef& Start, const FCellRef& Goal,
										TArray<FCellRef>* OutPath,
										bool bAllowPartial, const FVector* PartialTarget) const
{
	if (!Start.IsValid() || !Goal.IsValid() || Start.ChunkIndex != Goal.ChunkIndex ||
		!IsCellWalkable(Start.ChunkIndex, Start.LocalCell) || !IsCellWalkable(Goal.ChunkIndex, Goal.LocalCell))
	{
		return INF;
	}

	TStaticArray<float, NavL1ChunkCellCount> Cost;
	TStaticArray<int, NavL1ChunkCellCount> Parent;
	TStaticArray<uint8, NavL1ChunkCellCount> Visited = {0, };
	Cost.fill(INF);
	Parent.fill(-1);
	std::priority_queue<FOpenNode> PriorityQue;
	Cost[Start.LocalCell] = 0.0f;
	const FVector SearchTarget = PartialTarget ? *PartialTarget : GetCellPosition(Goal);
	PriorityQue.push({ Start.LocalCell, FVector::Distance(GetCellPosition(Start), SearchTarget) });
	int BestCell = Start.LocalCell;
	float BestTargetDistance = PartialTarget
		? FVector::Distance(GetCellPosition(Start), *PartialTarget)
		: FVector::Distance(GetCellPosition(Start), GetCellPosition(Goal));

	while (!PriorityQue.empty())
	{
		const int Current = PriorityQue.top().Index;
		PriorityQue.pop();
		if (Visited[Current] != 0) 
			continue;

		Visited[Current] = 1;
		const FCellRef CurrentRef{ Start.ChunkIndex, Current };
		const float TargetDistance = PartialTarget
			? FVector::Distance(GetCellPosition(CurrentRef), *PartialTarget)
			: FVector::Distance(GetCellPosition(CurrentRef), GetCellPosition(Goal));
		if (TargetDistance < BestTargetDistance)
		{
			BestTargetDistance = TargetDistance;
			BestCell = Current;
		}
		if (Current == Goal.LocalCell && !bAllowPartial)
		{
			BestCell = Current;
			break;
		}

		const int X = Current % NavL1ChunkCellsPerAxis;
		const int Y = Current / NavL1ChunkCellsPerAxis;
		for (const auto& Offset : NeighborOffsets)
		{
			const int NX = X + Offset[0];
			const int NY = Y + Offset[1];
			if (NX < 0 || NX >= NavL1ChunkCellsPerAxis ||
				NY < 0 || NY >= NavL1ChunkCellsPerAxis) 
				continue;

			const int Neighbor = NY * NavL1ChunkCellsPerAxis + NX;
			if (!IsCellWalkable(Start.ChunkIndex, Neighbor) || Visited[Neighbor] != 0) 
				continue;

			const FCellRef NeighborRef{ Start.ChunkIndex, Neighbor };
			const float NewCost = Cost[Current] +
				FVector::Distance(GetCellPosition(CurrentRef), GetCellPosition(NeighborRef));
			if (NewCost >= Cost[Neighbor]) 
				continue;

			Cost[Neighbor] = NewCost;
			Parent[Neighbor] = Current;
			PriorityQue.push({ Neighbor, NewCost + FVector::Distance(GetCellPosition(NeighborRef), SearchTarget) });
		}
	}

	if (BestCell != Goal.LocalCell && !bAllowPartial)
	{
		return INF;
	}

	if (OutPath)
	{
		TArray<FCellRef> ReversePath;
		for (int Current = BestCell; Current >= 0; Current = Parent[Current])
		{
			ReversePath.push_back({ Start.ChunkIndex, Current });
			if (Current == Start.LocalCell) 
				break;
		}
		if (ReversePath.empty() || ReversePath.back().LocalCell != Start.LocalCell)
		{
			return INF;
		}
		OutPath->assign(ReversePath.rbegin(), ReversePath.rend());
	}
	return Cost[BestCell];
}

int FVoxelNavigationGrid::FindOrAddPortal(int ChunkIndex, int LocalCell)
{
	FVoxelNavigationL1Chunk& Chunk = L1Chunks[ChunkIndex];
	for (int PortalIndex : Chunk.PortalIndices)
	{
		if (Portals[PortalIndex].LocalCell == LocalCell) 
			return PortalIndex;
	}
	const int PortalIndex = FreePortalIndices.empty() ? static_cast<int>(Portals.size()) : FreePortalIndices.back();
	if (!FreePortalIndices.empty())
		FreePortalIndices.pop_back();
	FVoxelNavigationPortal Portal;
	Portal.ChunkIndex = ChunkIndex;
	Portal.LocalCell = static_cast<uint8>(LocalCell);
	Portal.bActive = true;
	if (PortalIndex == static_cast<int>(Portals.size()))
	{
		Portals.push_back(std::move(Portal));
	}
	else
	{
		Portals[PortalIndex] = std::move(Portal);
	}
	Chunk.PortalIndices.push_back(PortalIndex);
	return PortalIndex;
}

void FVoxelNavigationGrid::AddAbstractEdge(int FromPortal, int ToPortal, float Cost)
{
	if (FromPortal < 0 || ToPortal < 0 || FromPortal == ToPortal ||
		FromPortal >= static_cast<int>(Portals.size()) || ToPortal >= static_cast<int>(Portals.size()) ||
		!Portals[FromPortal].bActive || !Portals[ToPortal].bActive)
		return;

	TArray<FVoxelNavigationAbstractEdge>& Edges = Portals[FromPortal].Edges;
	for (FVoxelNavigationAbstractEdge& Edge : Edges)
	{
		if (Edge.ToPortal != ToPortal) 
			continue;

		Edge.Cost = (std::min)(Edge.Cost, Cost);
		return;
	}
	Edges.push_back({ ToPortal, Cost });
}

void FVoxelNavigationGrid::DeactivatePortal(int PortalIndex)
{
	if (PortalIndex < 0 || PortalIndex >= static_cast<int>(Portals.size()) || !Portals[PortalIndex].bActive)
		return;
	Portals[PortalIndex].Edges.clear();
	Portals[PortalIndex].bActive = false;
	Portals[PortalIndex].ChunkIndex = -1;
	FreePortalIndices.push_back(PortalIndex);
}

void FVoxelNavigationGrid::RefreshRuntimeMemory()
{
	const uint64 NewRuntimeMemoryBytes = CalculateRuntimeMemoryBytes();
	if (bRuntimeMemoryStatsEnabled && NewRuntimeMemoryBytes > RuntimeMemoryBytes)
	{
		MemoryStats::AddVoxelNavigationMemory(NewRuntimeMemoryBytes - RuntimeMemoryBytes);
	}
	else if (bRuntimeMemoryStatsEnabled && NewRuntimeMemoryBytes < RuntimeMemoryBytes)
	{
		MemoryStats::SubVoxelNavigationMemory(RuntimeMemoryBytes - NewRuntimeMemoryBytes);
	}
	RuntimeMemoryBytes = NewRuntimeMemoryBytes;
}

size_t FVoxelNavigationGrid::CalculateRuntimeMemoryBytes() const
{
	size_t TotalBytes = 0;
	TotalBytes += L1Chunks.capacity() * sizeof(FVoxelNavigationL1Chunk);
	TotalBytes += L1ChunkLookup.capacity() * sizeof(int);
	TotalBytes += Portals.capacity() * sizeof(FVoxelNavigationPortal);
	TotalBytes += FreePortalIndices.capacity() * sizeof(int);
	TotalBytes += BakedChunks.capacity() * sizeof(FBakedVoxelNavigationChunk);
	for (const FVoxelNavigationL1Chunk& Chunk : L1Chunks)
	{
		TotalBytes += Chunk.PortalIndices.capacity() * sizeof(int);
	}
	for (const FVoxelNavigationPortal& Portal : Portals)
	{
		TotalBytes += Portal.Edges.capacity() * sizeof(FVoxelNavigationAbstractEdge);
	}
	for (const FBakedVoxelNavigationChunk& Chunk : BakedChunks)
	{
		TotalBytes += Chunk.IntraEdges.capacity() * sizeof(FBakedVoxelNavigationIntraEdge);
		TotalBytes += Chunk.ExternalLinks.capacity() * sizeof(FBakedVoxelNavigationExternalLink);
	}
	return TotalBytes;
}
