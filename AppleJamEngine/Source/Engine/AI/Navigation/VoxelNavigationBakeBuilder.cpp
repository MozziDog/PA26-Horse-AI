#include "pch.h"

#include "AI/Navigation/VoxelNavigationBakeBuilder.h"

#include "Core/Logging/Log.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Profiling/Stats/Stats.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>

namespace
{
	constexpr float INF = FLT_MAX;
	constexpr float Epsilon = 1.e-4f;
	constexpr uint8 FullNeighborMask = 0xffu; // 8방향으로 이웃과 모두 연결됨

	const int NeighborOffsets[8][2] =
	{
		{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
	};

	float ElapsedMilliseconds(const std::chrono::steady_clock::time_point& Start)
	{
		return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - Start).count();
	}

	bool ContainsIndex(const TArray<int>& Values, int Value)
	{
		return std::find(Values.begin(), Values.end(), Value) != Values.end();
	}

	int DirectionBit(int DeltaX, int DeltaY)
	{
		for (int Index = 0; Index < 8; ++Index)
		{
			if (NeighborOffsets[Index][0] == DeltaX && NeighborOffsets[Index][1] == DeltaY)
			{
				return Index;
			}
		}
		return -1;
	}

	struct FCrossingCandidate
	{
		int NodeA = -1;
		int NodeB = -1;
		int ChunkA = -1;
		int ChunkB = -1;
		int CellA = -1;
		int CellB = -1;
		int SubareaA = -1;
		int SubareaB = -1;
	};

	bool AreCrossingsAdjacent(const FCrossingCandidate& A, const FCrossingCandidate& B, const TArray<FVoxelNavigationNode>& Nodes)
	{
		const FVoxelCoord& A0 = Nodes[A.NodeA].Coord;
		const FVoxelCoord& A1 = Nodes[A.NodeB].Coord;
		const FVoxelCoord& B0 = Nodes[B.NodeA].Coord;
		const FVoxelCoord& B1 = Nodes[B.NodeB].Coord;
		return std::abs(A0.X - B0.X) <= 1 && std::abs(A0.Y - B0.Y) <= 1 &&
			std::abs(A1.X - B1.X) <= 1 && std::abs(A1.Y - B1.Y) <= 1;
	}
} // namespace

FVoxelNavigationBakeGrid::FVoxelNavigationBakeGrid()
{
	// A bake grid is temporary.  Its runtime-shaped output is reported in
	// FVoxelNavigationBuildStats, but never added to the live runtime counter.
	bRuntimeMemoryStatsEnabled = false;
}

int FVoxelNavigationBakeGrid::FlattenColumn(int X, int Y) const
{
	return Y * CellCountX + X;
}

bool FVoxelNavigationBakeGrid::IsValidColumn(int X, int Y) const
{
	return X >= 0 && X < CellCountX && Y >= 0 && Y < CellCountY;
}

bool FVoxelNavigationBakeBuilder::Build(
	UWorld* World,
	const FVector& BoundsCenter,
	const FVector& BoundsExtent,
	const FVoxelNavigationBuildSettings& Settings,
	const AActor* QueryOwner,
	FVoxelNavigationBakedData& OutData,
	FVoxelNavigationBuildStats& OutStats) const
{
	OutData = {};
	OutStats = {};
	FVoxelNavigationBakeGrid BakeGrid;
	if (!BakeGrid.Build(World, BoundsCenter, BoundsExtent, Settings, QueryOwner) || !BakeGrid.ExportBakedData(OutData))
	{
		OutStats = BakeGrid.GetBuildStats();
		return false;
	}
	OutStats = BakeGrid.GetBuildStats();
	return true;
}

bool FVoxelNavigationBakeGrid::ExportBakedData(FVoxelNavigationBakedData& OutData) const
{
	if (!bBuilt || BakedChunks.empty()) 
		return false;

	OutData.BoundsCenter = BoundsCenter;
	OutData.BoundsExtent = BoundsExtent;
	OutData.Settings = BuildSettings;
	OutData.Chunks = BakedChunks;
	return true;
}

bool FVoxelNavigationBakeGrid::BuildBakedChunksFromRuntimeGraph()
{
	BakedChunks.clear();
	BakedChunks.resize(L1Chunks.size());
	for (int ChunkIndex = 0; ChunkIndex < static_cast<int>(L1Chunks.size()); ++ChunkIndex)
	{
		BakedChunks[ChunkIndex].Coord = L1Chunks[ChunkIndex].Coord;
		BakedChunks[ChunkIndex].Cells = L1Chunks[ChunkIndex].Cells;
	}

	for (int PortalIndex = 0; PortalIndex < static_cast<int>(Portals.size()); ++PortalIndex)
	{
		const FVoxelNavigationPortal& From = Portals[PortalIndex];
		if (From.ChunkIndex < 0 || From.ChunkIndex >= static_cast<int>(BakedChunks.size())) 
			return false;

		for (const FVoxelNavigationAbstractEdge& Edge : From.Edges)
		{
			if (Edge.ToPortal < 0 || Edge.ToPortal >= static_cast<int>(Portals.size()) ||
				!std::isfinite(Edge.Cost) || Edge.Cost < 0.0f) 
				return false;

			const FVoxelNavigationPortal& To = Portals[Edge.ToPortal];
			if (From.ChunkIndex == To.ChunkIndex)
			{
				if (From.LocalCell >= To.LocalCell)
					continue;

				FBakedVoxelNavigationIntraEdge BakedEdge{ From.LocalCell, To.LocalCell, Edge.Cost };
				auto& Edges = BakedChunks[From.ChunkIndex].IntraEdges;
				const bool bExists = std::any_of(Edges.begin(), Edges.end(), [&BakedEdge](const auto& Existing)
				{
					return Existing.PortalA == BakedEdge.PortalA && Existing.PortalB == BakedEdge.PortalB;
				});
				if (!bExists) 
					Edges.push_back(BakedEdge);

				continue;
			}

			const FVoxelCoord& SourceCoord = L1Chunks[From.ChunkIndex].Coord;
			const FVoxelCoord& TargetCoord = L1Chunks[To.ChunkIndex].Coord;
			uint8 PackedDelta = 0;
			if (!PackNeighborChunkDelta({ TargetCoord.X - SourceCoord.X, TargetCoord.Y - SourceCoord.Y, TargetCoord.Z - SourceCoord.Z }, PackedDelta)) 
				return false;
			BakedChunks[From.ChunkIndex].ExternalLinks.push_back({ From.LocalCell, PackedDelta, To.LocalCell, Edge.Cost });
		}
	}

	for (FBakedVoxelNavigationChunk& Chunk : BakedChunks)
	{
		std::sort(Chunk.IntraEdges.begin(), Chunk.IntraEdges.end(), [](const auto& A, const auto& B)
		{
			return std::tie(A.PortalA, A.PortalB) < std::tie(B.PortalA, B.PortalB);
		});
		std::sort(Chunk.ExternalLinks.begin(), Chunk.ExternalLinks.end(), [](const auto& A, const auto& B)
		{
			return std::tie(A.LocalPortalId, A.PackedNeighborChunkDelta, A.NeighborPortalId)
					< std::tie(B.LocalPortalId, B.PackedNeighborChunkDelta, B.NeighborPortalId);
		});
	}
	return !BakedChunks.empty();
}

bool FVoxelNavigationBakeGrid::Build(
	UWorld* World,
	const FVector& InBoundsCenter,
	const FVector& InBoundsExtent,
	const FVoxelNavigationBuildSettings& Settings,
	const AActor* QueryOwner)
{
	SCOPE_STAT_CAT("VoxelNav.BuildGrid", "Navigation");
	// 통계 초기화
	const auto StartTime = std::chrono::steady_clock::now();
	int TestedL1ChunkCount = 0;
	int EmptyL1ChunkCount = 0;

	// 데이터 초기화
	bBuilt = false;
	Nodes.clear();
	XYToNodesLookup.clear();
	L1Chunks.clear();
	L1ChunkLookup.clear();
	Portals.clear();
	FreePortalIndices.clear();
	BakedChunks.clear();
	bRuntimeInitialized = false;
	NodeToChunkLookup.clear();
	NodeToLocalCellIdxLookup.clear();
	ChunkCellToNodeLookup.clear();
	BuildStats = FVoxelNavigationBuildStats();
	BuildSettings = Settings;
	RefreshMemoryUsage();

	if (!World)
	{
		UE_LOG("[VoxelNavigationGrid] Build rejected: Cannot approach physics query");
		return false;
	}
	if (Settings.AgentRadius <= Epsilon || Settings.AgentHeight <= Settings.AgentRadius * 2.0f 
		|| InBoundsExtent.X <= Epsilon || InBoundsExtent.Y <= Epsilon || InBoundsExtent.Z <= Epsilon)
	{
		UE_LOG("[VoxelNavigationGrid] Build rejected: Invalid build settings");
		return false;
	}

	BoundsCenter = InBoundsCenter;
	BoundsExtent = InBoundsExtent;
	BoundsMin = BoundsCenter - BoundsExtent;
	CellCountX = (std::max)(1, static_cast<int>(std::ceil(BoundsExtent.X * 2.0f / NavVoxelCellSize)));
	CellCountY = (std::max)(1, static_cast<int>(std::ceil(BoundsExtent.Y * 2.0f / NavVoxelCellSize)));
	CellCountZ = (std::max)(1, static_cast<int>(std::ceil(BoundsExtent.Z * 2.0f / NavVoxelCellSize)));
	L1ChunkCountX = (CellCountX + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	L1ChunkCountY = (CellCountY + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	L1ChunkCountZ = (CellCountZ + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	XYToNodesLookup.resize(static_cast<size_t>(CellCountX) * static_cast<size_t>(CellCountY));
	L1ChunkLookup.assign(L1ChunkCountX * L1ChunkCountY * L1ChunkCountZ, -1);
	RefreshMemoryUsage();

	// NOTE: Navigation 빌드시에는 WorldStatic만 고려
	constexpr uint32 LayerMask = ObjectTypeBit(ECollisionChannel::WorldStatic);
	const float SlopeCos = std::cos(Settings.MaxWalkableSlopeDegrees * FMath::DegToRad);
	const float ProbeMargin = std::clamp(Settings.GroundProbeInset, 0.001f, NavVoxelCellSize * 0.25f);
	const FCollisionShape ClearanceShape = FCollisionShape::MakeCapsule(Settings.AgentRadius, Settings.AgentHeight * 0.5f);

	// L1 청크 단위로 1차 검사 (broad phase): 약간의 여유분 추가해서 보수적으로 검사 수행
	TArray<bool> L1TestResult(L1ChunkCountX * L1ChunkCountY * L1ChunkCountZ, false);
	const float OverlapPadding = (std::max)(ProbeMargin, 0.01f);
	const auto L1OverlapStartTime = std::chrono::steady_clock::now();
	for (int ChunkZ = 0; ChunkZ < L1ChunkCountZ; ++ChunkZ)
	{
		for (int ChunkY = 0; ChunkY < L1ChunkCountY; ++ChunkY)
		{
			for (int ChunkX = 0; ChunkX < L1ChunkCountX; ++ChunkX)
			{
				const int CellMinX = ChunkX * NavL1ChunkCellsPerAxis;
				const int CellMinY = ChunkY * NavL1ChunkCellsPerAxis;
				const int CellMinZ = ChunkZ * NavL1ChunkCellsPerAxis;
				const int CellMaxX = (std::min)(CellCountX, CellMinX + NavL1ChunkCellsPerAxis);
				const int CellMaxY = (std::min)(CellCountY, CellMinY + NavL1ChunkCellsPerAxis);
				const int CellMaxZ = (std::min)(CellCountZ, CellMinZ + NavL1ChunkCellsPerAxis);
				const FVector Min = BoundsMin + FVector(CellMinX * NavVoxelCellSize,
														CellMinY * NavVoxelCellSize,
														CellMinZ * NavVoxelCellSize);
				const FVector Max = BoundsMin + FVector(CellMaxX * NavVoxelCellSize,
														CellMaxY * NavVoxelCellSize,
														CellMaxZ * NavVoxelCellSize);
				const FVector Center = (Min + Max) * 0.5f;
				const FVector Extent = (Max - Min) * 0.5f + FVector(OverlapPadding, OverlapPadding, OverlapPadding);
				const int FlatChunk = FlattenL1Chunk(ChunkX, ChunkY, ChunkZ);
				TestedL1ChunkCount++;
				if (World->PhysicsOverlapAnyByObjectTypes(
					Center, FQuat::Identity, FCollisionShape::MakeBox(Extent), LayerMask, QueryOwner))
				{
					L1TestResult[FlatChunk] = true;
				}
				else
				{
					EmptyL1ChunkCount++;
					BuildStats.NumSkippedCellsByL1Overlap +=
						(CellMaxX - CellMinX) * (CellMaxY - CellMinY) * (CellMaxZ - CellMinZ);
				}
			}
		}
	}
	const float L1OverlapTimeMs = ElapsedMilliseconds(L1OverlapStartTime);
	UE_LOG("[VoxelNavigationGrid] L1Overlap finished in %f ms. Tested %d chunks, %d was empty.", 
			L1OverlapTimeMs, TestedL1ChunkCount, EmptyL1ChunkCount);
	UpdateBuildPeakMemory(L1TestResult.capacity() * sizeof(uint8));

	// 각 복셀에 대해 서있을 수 있는 경사인지 검사
	{
		SCOPE_STAT_CAT("VoxelNav.ClassifyStandable", "Navigation");
		for (int Y = 0; Y < CellCountY; ++Y)
		{
			for (int X = 0; X < CellCountX; ++X)
			{
				// (X,Y) column 내에서 밑에서 위로 훑으면서 서 있을 수 있는 복셀이 있나 체크
				TArray<float> AcceptedGroundHeights;
				for (int Z = 0; Z < CellCountZ; ++Z)
				{
					// 만약 해당 복셀이 '아무것도 없는 것으로 판정된' 청크에 속해있다면 not-walkable이 확실하니 스킵
					const int FlatChunk = FlattenL1Chunk(
						X / NavL1ChunkCellsPerAxis,
						Y / NavL1ChunkCellsPerAxis,
						Z / NavL1ChunkCellsPerAxis);
					if (L1TestResult[FlatChunk] == 0)
					{
						continue;
					}

					++BuildStats.NumSampledCells;
					const float CellCenterX = BoundsMin.X + ((X + 0.5f) * NavVoxelCellSize);
					const float CellCenterY = BoundsMin.Y + ((Y + 0.5f) * NavVoxelCellSize);
					const float SlabBottom = BoundsMin.Z + (Z * NavVoxelCellSize);
					const float SlabTop = SlabBottom + NavVoxelCellSize;
					const FVector RayStart(CellCenterX, CellCenterY, SlabTop - ProbeMargin);

					// 지면 존재 여부 검사
					FHitResult GroundHit;
					if (!World->PhysicsRaycastByObjectTypes(
						RayStart, FVector::DownVector, NavVoxelCellSize - ProbeMargin + Epsilon,
						GroundHit, LayerMask, QueryOwner))
					{
						++BuildStats.NumNoGroundCells;
						continue;
					}

					// 높이로 중복 검사: ProbeMargin 여유분 때문에 청크 경계에서 동일 지면이 중복 카운트되는 것 방지 
					bool bDuplicateHeight = false;
					for (float ExistingHeight : AcceptedGroundHeights)
					{
						if (std::abs(ExistingHeight - GroundHit.WorldHitLocation.Z) <= ProbeMargin * 2.0f)
						{
							bDuplicateHeight = true;
							break;
						}
					}
					if (bDuplicateHeight)
					{
						continue;
					}

					// 지면이 서있을만한 각도인지 검사
					FVector GroundNormal = GroundHit.WorldNormal;
					if (GroundNormal.IsNearlyZero()) GroundNormal = GroundHit.ImpactNormal;
					GroundNormal.Normalize();
					if (GroundNormal.Dot(FVector::UpVector) + Epsilon < SlopeCos)
					{
						++BuildStats.NumRejectedSlope;
						continue;
					}

					// 벽과 너무 딱붙은 곳 등 에이전트가 서있을 수 없는 곳 필터링
					const FVector StandingPoint = GroundHit.WorldHitLocation;
					const FVector CapsuleCenter = StandingPoint + FVector::UpVector *
						(Settings.AgentHeight * 0.5f + Settings.ClearanceOffset);
					if (World->PhysicsOverlapAnyByObjectTypes(
						CapsuleCenter, FQuat::Identity, ClearanceShape, LayerMask, QueryOwner))
					{
						++BuildStats.NumRejectedClearance;
						continue;
					}

					// 모든 검사 통과했으면 노드 추가 + 메모리 프로파일링 정보 업데이트
					FVoxelNavigationNode Node;
					Node.Coord = { X, Y, Z };
					Node.Position = StandingPoint;
					Node.GroundNormal = GroundNormal;
					const int NodeIndex = static_cast<int>(Nodes.size());
					Nodes.push_back(Node);
					TArray<int>& Column = XYToNodesLookup[FlattenColumn(X, Y)];
					Column.push_back(NodeIndex);
					AcceptedGroundHeights.push_back(StandingPoint.Z);
				}
				RefreshBakeScratchMemory();
				UpdateBuildPeakMemory(static_cast<size_t>(AcceptedGroundHeights.capacity()) * sizeof(float));
			}
		}
	}
	BuildStats.NumRawWalkableNodes = Nodes.size();

	// 하나의 복셀(X,Y)에서 이웃한 복셀로 이동가능한지 체크 (경사 및 단차 검사)
	const int CardinalOffsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };	// 십자 방향
	for (int NodeIndex = 0; NodeIndex < Nodes.size(); ++NodeIndex)
	{
		const FVoxelNavigationNode& Node = Nodes[NodeIndex];
		for (const auto& Offset : CardinalOffsets)
		{
			const int NX = Node.Coord.X + Offset[0];
			const int NY = Node.Coord.Y + Offset[1];
			if (!IsValidColumn(NX, NY)) 
				continue;

			for (int Candidate : XYToNodesLookup[FlattenColumn(NX, NY)])
			{
				const float HeightDifference = std::abs(Nodes[Candidate].Position.Z - Node.Position.Z);
				if (HeightDifference > Settings.MaxNeighborHeightDelta + Epsilon)
					continue;
				
				AddDirectedEdge(NodeIndex, Candidate);
			}
		}
	}
	const int DiagonalOffsets[4][2] = { { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } }; // 대각선 방향
	for (int NodeIndex = 0; NodeIndex < Nodes.size(); ++NodeIndex)
	{
		const FVoxelNavigationNode& Node = Nodes[NodeIndex];
		for (const auto& Offset : DiagonalOffsets)
		{
			const int NX = Node.Coord.X + Offset[0];
			const int NY = Node.Coord.Y + Offset[1];
			if (!IsValidColumn(NX, NY)) 
				continue;

			for (int Candidate : XYToNodesLookup[FlattenColumn(NX, NY)])
			{
				const float HeightDifference = std::abs(Nodes[Candidate].Position.Z - Node.Position.Z);
				if (HeightDifference > Settings.MaxNeighborHeightDelta + Epsilon)
					continue;

				if (!HasCardinalBridge(NodeIndex, Candidate, Node.Coord.X + Offset[0], Node.Coord.Y) 
					|| !HasCardinalBridge(NodeIndex, Candidate, Node.Coord.X, Node.Coord.Y + Offset[1]))
					continue;

				AddDirectedEdge(NodeIndex, Candidate);
			}
		}
	}
	CalculateBakeScratchMemoryBytes();

	// 8방향으로 연결된 노드만 남기고 필터링 (mark and sweep)
	TArray<uint8> RetainedNodes(Nodes.size(), 0);
	UpdateBuildPeakMemory(RetainedNodes.capacity() * sizeof(uint8));
	for (int NodeIndex = 0; NodeIndex < Nodes.size(); ++NodeIndex)
	{
		const FVoxelNavigationNode& Node = Nodes[NodeIndex];
		uint8 DirectionMask = 0;
		for (int Neighbor : Node.Neighbors)
		{
			if (Neighbor < 0
				|| Neighbor >= Nodes.size()
				|| !ContainsIndex(Nodes[Neighbor].Neighbors, NodeIndex))
			{
				continue;
			}

			const FVoxelCoord& OtherCoord = Nodes[Neighbor].Coord;
			const int Bit = DirectionBit(OtherCoord.X - Node.Coord.X, OtherCoord.Y - Node.Coord.Y);
			if (Bit >= 0) DirectionMask |= static_cast<uint8>(1u << Bit);
		}
		RetainedNodes[NodeIndex] = (DirectionMask == FullNeighborMask ? 1 : 0);
	}

	NodeToChunkLookup.assign(Nodes.size(), -1);
	NodeToLocalCellIdxLookup.assign(Nodes.size(), -1);
	for (int NodeIndex = 0; NodeIndex < Nodes.size(); ++NodeIndex)
	{
		if (RetainedNodes[NodeIndex] == 0) 
			continue;

		const FVoxelNavigationNode& Node = Nodes[NodeIndex];
		const int ChunkX = Node.Coord.X / NavL1ChunkCellsPerAxis;
		const int ChunkY = Node.Coord.Y / NavL1ChunkCellsPerAxis;
		const int ChunkZ = Node.Coord.Z / NavL1ChunkCellsPerAxis;
		const int FlatChunk = FlattenL1Chunk(ChunkX, ChunkY, ChunkZ);
		int ChunkIndex = L1ChunkLookup[FlatChunk];
		if (ChunkIndex < 0)
		{
			ChunkIndex = L1Chunks.size();
			FVoxelNavigationL1Chunk Chunk;
			Chunk.Coord = { ChunkX, ChunkY, ChunkZ };
			L1Chunks.push_back(Chunk);
			TStaticArray<int, NavL1ChunkCellCount> CellToNode;
			CellToNode.fill(-1);
			ChunkCellToNodeLookup.push_back(CellToNode);
			L1ChunkLookup[FlatChunk] = ChunkIndex;
		}

		const int LocalX = Node.Coord.X % NavL1ChunkCellsPerAxis;
		const int LocalY = Node.Coord.Y % NavL1ChunkCellsPerAxis;
		const int LocalCell = LocalY * NavL1ChunkCellsPerAxis + LocalX;
		FVoxelNavigationL1Chunk& Chunk = L1Chunks[ChunkIndex];
		const float ChunkBottom = BoundsMin.Z + (ChunkZ * NavL1ChunkSize);
		const float RelativeHeight = std::clamp(
			(Node.Position.Z - ChunkBottom) / NavL1ChunkSize, 0.0f, 1.0f);
		const uint8 CellValue = static_cast<uint8>(1 + static_cast<int>(std::round(RelativeHeight * 254.0f)));
		if (Chunk.Cells[LocalCell] > CellValue)
		{
			UE_LOG("[VoxelNavigationBakeBuilder] Warning: more than one nodes in chunk collided after erosion (%.1f, %.1f, %.1f)",
				Node.Position.X, Node.Position.Y, Node.Position.Z);
			// 이웃한 지면과의 연결성으로 필터링했음에도 불구하고
			// 청크 내에 XY를 공유하는 복수의 청크가 존재하는 경우
			// → 조금이라도 높이가 높은 쪽을 채택 (경사로가 지면과 만나는 지점 등 특수 케이스 고려)
			continue;
		}
		Chunk.Cells[LocalCell] = CellValue;
		NodeToChunkLookup[NodeIndex] = ChunkIndex;
		NodeToLocalCellIdxLookup[NodeIndex] = LocalCell;
		ChunkCellToNodeLookup[ChunkIndex][LocalCell] = NodeIndex;
	}

	// 기존 연결 그래프를 heightmap으로 압축했을 때 정보 손실 있는지 검사 (이웃한 노드간에 연결성이 담보되는지)
	if(false)// 테스트
	for (int NodeIndex = 0; NodeIndex < Nodes.size(); ++NodeIndex)
	{
		if (RetainedNodes[NodeIndex] == 0) 
			continue;

		const int ChunkIndex = NodeToChunkLookup[NodeIndex];
		const int LocalCell = NodeToLocalCellIdxLookup[NodeIndex];
		const int LocalX = LocalCell % NavL1ChunkCellsPerAxis;
		const int LocalY = LocalCell / NavL1ChunkCellsPerAxis;
		for (const auto& Offset : NeighborOffsets)
		{
			const int NX = LocalX + Offset[0];
			const int NY = LocalY + Offset[1];
			if (NX < 0 || NX >= NavL1ChunkCellsPerAxis ||
				NY < 0 || NY >= NavL1ChunkCellsPerAxis) 
				continue;

			const int NeighborCell = NY * NavL1ChunkCellsPerAxis + NX;
			const int NeighborNode = ChunkCellToNodeLookup[ChunkIndex][NeighborCell];
			if (NeighborNode < 0) 
				continue;

			if (!ContainsIndex(Nodes[NodeIndex].Neighbors, NeighborNode) ||
				!ContainsIndex(Nodes[NeighborNode].Neighbors, NodeIndex))
			{
				UE_LOG("[VoxelNavigationGrid] Build rejected: fixed8 occupancy would infer an invalid local edge.");
				Nodes.clear();
				XYToNodesLookup.clear();
				L1Chunks.clear();
				L1ChunkLookup.assign(L1ChunkLookup.size(), -1);
				Portals.clear();
				NodeToChunkLookup.clear();
				NodeToLocalCellIdxLookup.clear();
				ChunkCellToNodeLookup.clear();
				RefreshMemoryUsage();
				return false;
			}
		}
	}

	// 프로파일링 정보 업데이트
	BuildStats.NumWalkableNodes = 0;
	for (uint8 bRetained : RetainedNodes)
	{
		BuildStats.NumWalkableNodes += bRetained != 0 ? 1 : 0;
	}
	BuildStats.NumErodedNodes = BuildStats.NumRawWalkableNodes - BuildStats.NumWalkableNodes;
	BuildStats.NumBuiltL1Chunks = L1Chunks.size();
	RefreshMemoryUsage();
	UpdateBuildPeakMemory(RetainedNodes.capacity() * sizeof(uint8));

	// HPA* 계층 구성
	BuildAbstractGraph(RetainedNodes);
	if (!BuildBakedChunksFromRuntimeGraph())
	{
		UE_LOG("[VoxelNavigationGrid] Build rejected: baked chunk graph export failed.");
		bBuilt = false;
		return false;
	}

	// 프로파일링 정보 업데이트
	BuildStats.NumAbstractNodes =Portals.size();
	BuildStats.NumAbstractEdges = 0;
	for (const FVoxelNavigationPortal& Portal : Portals)
	{
		BuildStats.NumAbstractEdges += Portal.Edges.size();
	}
	RefreshMemoryUsage();

	// 계산 과정에 썼던 임시 배열들 정리
	TArray<FVoxelNavigationNode>().swap(Nodes);
	TArray<TArray<int>>().swap(XYToNodesLookup);
	TArray<int>().swap(NodeToChunkLookup);
	TArray<int>().swap(NodeToLocalCellIdxLookup);
	TArray<TStaticArray<int, NavL1ChunkCellCount>>().swap(ChunkCellToNodeLookup);
	RefreshMemoryUsage();

	BuildStats.BuildTimeMs = ElapsedMilliseconds(StartTime);
	bBuilt = BuildStats.NumWalkableNodes > 0;

	UE_LOG("[VoxelNavigationGrid] Build finished in %.3fMs NumWalkableNodes:%d NumErodedNodes:%d",
		BuildStats.BuildTimeMs, BuildStats.NumWalkableNodes, BuildStats.NumErodedNodes);
	return bBuilt;
}


void FVoxelNavigationBakeGrid::BuildAbstractGraph(const TArray<uint8>& RetainedNodes)
{
	Portals.clear();

	if (L1Chunks.empty()) 
		return;

	for (FVoxelNavigationL1Chunk& Chunk : L1Chunks)
	{
		Chunk.PortalIndices.clear();
	}

	// 청크 내에서 연속된 부분 합치기
	TArray<TStaticArray<int16, NavL1ChunkCellCount>> Subareas(L1Chunks.size());
	for (int ChunkIndex = 0; ChunkIndex < L1Chunks.size(); ++ChunkIndex)
	{
		Subareas[ChunkIndex].fill(-1);
		int16 NextComponent = 0;
		for (int Seed = 0; Seed < NavL1ChunkCellCount; ++Seed)
		{
			if (!IsCellWalkable(ChunkIndex, Seed) || Subareas[ChunkIndex][Seed] >= 0) 
				continue;

			TArray<int> Queue = { Seed };
			Subareas[ChunkIndex][Seed] = NextComponent;
			for (size_t QueueIndex = 0; QueueIndex < Queue.size(); ++QueueIndex)
			{
				const int Current = Queue[QueueIndex];
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
					if (!IsCellWalkable(ChunkIndex, Neighbor) ||
						Subareas[ChunkIndex][Neighbor] >= 0) 
						continue;

					Subareas[ChunkIndex][Neighbor] = NextComponent;
					Queue.push_back(Neighbor);
				}
			}
			++NextComponent;
		}
	}

	// 이웃한 노드가 서로 다른 청크에 있을 경우 후보로 추가
	// (이 단계에서는 중복된 subarea 쌍이 포함될 수 있음)
	TArray<FCrossingCandidate> Crossings;
	for (int NodeIndex = 0; NodeIndex < Nodes.size(); ++NodeIndex)
	{
		if (RetainedNodes[NodeIndex] == 0) 
			continue;

		for (int Neighbor : Nodes[NodeIndex].Neighbors)
		{
			if (Neighbor <= NodeIndex || RetainedNodes[Neighbor] == 0 ||
				!ContainsIndex(Nodes[Neighbor].Neighbors, NodeIndex)) 
				continue;

			int ChunkA = NodeToChunkLookup[NodeIndex];
			int ChunkB = NodeToChunkLookup[Neighbor];
			if (ChunkA < 0 || ChunkB < 0 || ChunkA == ChunkB) 
				continue;

			int NodeA = NodeIndex;
			int NodeB = Neighbor;
			int CellA = NodeToLocalCellIdxLookup[NodeIndex];
			int CellB = NodeToLocalCellIdxLookup[Neighbor];
			if (ChunkB < ChunkA)
			{
				std::swap(ChunkA, ChunkB);
				std::swap(NodeA, NodeB);
				std::swap(CellA, CellB);
			}
			Crossings.push_back({ NodeA, NodeB, ChunkA, ChunkB, CellA, CellB,
				Subareas[ChunkA][CellA],
				Subareas[ChunkB][CellB] });
		}
	}

	std::sort(Crossings.begin(), Crossings.end(), [](const FCrossingCandidate& A, const FCrossingCandidate& B)
	{
		return std::tie(A.ChunkA, A.SubareaA, A.ChunkB, A.SubareaB, A.CellA, A.CellB) <
			std::tie(B.ChunkA, B.SubareaA, B.ChunkB, B.SubareaB, B.CellA, B.CellB);
	});
	UpdateBuildPeakMemory(
		Subareas.capacity() * sizeof(TStaticArray<int16, NavL1ChunkCellCount>) +
		Crossings.capacity() * sizeof(FCrossingCandidate));

	// 정렬된 Crossing 리스트에서 같은 portal을 가리키는 {chunk, subarea} 쌍은 묶고 중복되지 않은 portal만 추가
	for (size_t GroupBegin = 0; GroupBegin < Crossings.size(); )
	{	
		size_t GroupEnd = GroupBegin + 1;
		while (GroupEnd < Crossings.size() &&
			Crossings[GroupEnd].ChunkA == Crossings[GroupBegin].ChunkA &&
			Crossings[GroupEnd].ChunkB == Crossings[GroupBegin].ChunkB &&
			Crossings[GroupEnd].SubareaA == Crossings[GroupBegin].SubareaA &&
			Crossings[GroupEnd].SubareaB == Crossings[GroupBegin].SubareaB)
		{
			++GroupEnd;
		}

		// 서로 같은 Subarea 쌍을 가리키더라도 서로 다른 portal일 수 있음
		// e.g.) 경계에 작은 장애물이 걸쳐져 있는 경우
		TArray<uint8> Visited(GroupEnd - GroupBegin, 0);
		for (size_t LocalSeed = 0; LocalSeed < Visited.size(); ++LocalSeed)
		{
			if (Visited[LocalSeed] != 0) 
				continue;

			TArray<size_t> Run = { LocalSeed };
			Visited[LocalSeed] = 1;
			for (size_t QueueIndex = 0; QueueIndex < Run.size(); ++QueueIndex)
			{
				for (size_t Candidate = 0; Candidate < Visited.size(); ++Candidate)
				{
					if (Visited[Candidate] != 0) 
						continue;

					if (AreCrossingsAdjacent(Crossings[GroupBegin + Run[QueueIndex]],
						Crossings[GroupBegin + Candidate], Nodes))
					{
						Visited[Candidate] = 1;
						Run.push_back(Candidate);
					}
				}
			}

			const FCrossingCandidate& Chosen = Crossings[GroupBegin + Run[Run.size() / 2]];
			UpdateBuildPeakMemory(
				Subareas.capacity() * sizeof(TStaticArray<int16, NavL1ChunkCellCount>) +
				Crossings.capacity() * sizeof(FCrossingCandidate) +
				Visited.capacity() * sizeof(uint8) + Run.capacity() * sizeof(size_t));
			const int PortalA = FindOrAddPortal(Chosen.ChunkA, Chosen.CellA);
			const int PortalB = FindOrAddPortal(Chosen.ChunkB, Chosen.CellB);
			const float CrossingCost = FVector::Distance(
				GetCellPosition({ Chosen.ChunkA, Chosen.CellA }),
				GetCellPosition({ Chosen.ChunkB, Chosen.CellB }));
			AddAbstractEdge(PortalA, PortalB, CrossingCost);
			AddAbstractEdge(PortalB, PortalA, CrossingCost);
		}
		GroupBegin = GroupEnd;
	}

	// 청크 내부 간선 추가
	for (const FVoxelNavigationL1Chunk& Chunk : L1Chunks)
	{
		for (size_t A = 0; A < Chunk.PortalIndices.size(); ++A)
		{
			for (size_t B = A + 1; B < Chunk.PortalIndices.size(); ++B)
			{
				const int PortalA = Chunk.PortalIndices[A];
				const int PortalB = Chunk.PortalIndices[B];
				const float Cost = FindLocalPath(
					{ Portals[PortalA].ChunkIndex, Portals[PortalA].LocalCell },
					{ Portals[PortalB].ChunkIndex, Portals[PortalB].LocalCell });
				if (Cost >= INF) 
					continue;

				AddAbstractEdge(PortalA, PortalB, Cost);
				AddAbstractEdge(PortalB, PortalA, Cost);
			}
		}
	}
}

bool FVoxelNavigationBakeGrid::HasCardinalBridge(int FromNode, int ToNode, int BridgeX, int BridgeY) const
{
	if (FromNode < 0 || ToNode < 0 || !IsValidColumn(BridgeX, BridgeY)) 
		return false;

	const TArray<int>& FromNeighbors = Nodes[FromNode].Neighbors;
	for (int BridgeNode : XYToNodesLookup[FlattenColumn(BridgeX, BridgeY)])
	{
		if (!ContainsIndex(FromNeighbors, BridgeNode)) 
			continue;

		if (ContainsIndex(Nodes[BridgeNode].Neighbors, ToNode)) 
			return true;
	}
	return false;
}

void FVoxelNavigationBakeGrid::AddDirectedEdge(int FromNode, int ToNode)
{
	if (FromNode < 0 || ToNode < 0 || FromNode == ToNode) 
		return;

	TArray<int>& Neighbors = Nodes[FromNode].Neighbors;
	if (!ContainsIndex(Neighbors, ToNode))
	{
		Neighbors.push_back(ToNode);
		++BuildStats.NumDirectedEdges;
	}
}

void FVoxelNavigationBakeGrid::RefreshMemoryUsage()
{
	RefreshRuntimeMemory();
	RefreshBakeScratchMemory();
}

void FVoxelNavigationBakeGrid::RefreshBakeScratchMemory()
{
	BakeScratchMemoryBytes = CalculateBakeScratchMemoryBytes();
	UpdateBuildPeakMemory();
}

size_t FVoxelNavigationBakeGrid::CalculateBakeScratchMemoryBytes() const
{
	size_t TotalBytes = 0;
	TotalBytes += Nodes.capacity() * sizeof(FVoxelNavigationNode);
	TotalBytes += XYToNodesLookup.capacity() * sizeof(TArray<int>);
	TotalBytes += NodeToChunkLookup.capacity() * sizeof(int);
	TotalBytes += NodeToLocalCellIdxLookup.capacity() * sizeof(int);
	TotalBytes += ChunkCellToNodeLookup.capacity() * sizeof(TStaticArray<int, NavL1ChunkCellCount>);
	for (const FVoxelNavigationNode& Node : Nodes)
	{
		TotalBytes += Node.Neighbors.capacity() * sizeof(int);
	}
	for (const TArray<int>& Column : XYToNodesLookup)
	{
		TotalBytes += Column.capacity() * sizeof(int);
	}
	return TotalBytes;
}

void FVoxelNavigationBakeGrid::UpdateBuildPeakMemory(uint64 AdditionalTemporaryBytes)
{
	BuildStats.RuntimeMemoryBytes = RuntimeMemoryBytes;
	BuildStats.PeakBakeScratchMemoryBytes = (std::max)(
		BuildStats.PeakBakeScratchMemoryBytes,
		static_cast<size_t>(BakeScratchMemoryBytes + AdditionalTemporaryBytes));
	BuildStats.PeakMemoryBytes = (std::max)(
		BuildStats.PeakMemoryBytes,
		static_cast<size_t>(RuntimeMemoryBytes + BakeScratchMemoryBytes + AdditionalTemporaryBytes));
}
