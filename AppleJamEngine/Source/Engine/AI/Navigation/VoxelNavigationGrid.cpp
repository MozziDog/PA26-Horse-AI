#include "pch.h"

#include "AI/Navigation/VoxelNavigationGrid.h"

#include "Asset/AssetPackage.h"
#include "Core/Logging/Log.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Profiling/Stats/MemoryStats.h"
#include "Profiling/Stats/Stats.h"
#include "SimpleJSON/json.hpp"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
#include <tuple>
#include <Windows.h>

namespace
{
	constexpr float INF = FLT_MAX;
	constexpr float Epsilon = 1.e-4f;
	constexpr uint32 NavigationAssetFormatVersion = 1;
	constexpr uint32 NavigationAssetByteOrder = 0x01020304u;
	constexpr uint32 MaxNavigationAssetChunks = 1u << 20;
	constexpr uint32 MaxNavigationAssetEdgesPerChunk = 1u << 20;

	struct FNavigationChunkIndexEntry
	{
		FVoxelCoord Coord;
		uint64 Offset = 0;
		uint32 Size = 0;
	};

	constexpr uint8 FullNeighborMask = 0xffu; // = 0b11111111, 8방향으로 연결된 상태
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

	bool AreCrossingsAdjacent(
		const FCrossingCandidate& A,
		const FCrossingCandidate& B,
		const TArray<FVoxelNavigationNode>& Nodes)
	{
		const FVoxelCoord& A0 = Nodes[A.NodeA].Coord;
		const FVoxelCoord& A1 = Nodes[A.NodeB].Coord;
		const FVoxelCoord& B0 = Nodes[B.NodeA].Coord;
		const FVoxelCoord& B1 = Nodes[B.NodeB].Coord;
		return std::abs(A0.X - B0.X) <= 1 && std::abs(A0.Y - B0.Y) <= 1 
				&& std::abs(A1.X - B1.X) <= 1 && std::abs(A1.Y - B1.Y) <= 1;
	}

	bool IsCoordLess(const FVoxelCoord& A, const FVoxelCoord& B)
	{
		return std::tie(A.X, A.Y, A.Z) < std::tie(B.X, B.Y, B.Z);
	}

	bool WriteChunkIndexEntry(FArchive& Ar, const FNavigationChunkIndexEntry& Entry)
	{
		int32 X = Entry.Coord.X;
		int32 Y = Entry.Coord.Y;
		int32 Z = Entry.Coord.Z;
		uint64 Offset = Entry.Offset;
		uint32 Size = Entry.Size;
		Ar << X << Y << Z << Offset << Size;
		return Ar.IsValid();
	}

	bool ReadChunkIndexEntry(FArchive& Ar, FNavigationChunkIndexEntry& OutEntry)
	{
		Ar << OutEntry.Coord.X << OutEntry.Coord.Y << OutEntry.Coord.Z << OutEntry.Offset << OutEntry.Size;
		return Ar.IsValid();
	}

	bool WriteNavigationChunk(FArchive& Ar, const FBakedVoxelNavigationChunk& Chunk)
	{
		Ar.Serialize(const_cast<uint8*>(Chunk.Cells.data()), Chunk.Cells.size());
		uint32 IntraCount = static_cast<uint32>(Chunk.IntraEdges.size());
		Ar << IntraCount;
		for (const FBakedVoxelNavigationIntraEdge& Edge : Chunk.IntraEdges)
		{
			uint8 PortalA = Edge.PortalA;
			uint8 PortalB = Edge.PortalB;
			float Cost = Edge.Cost;
			Ar << PortalA << PortalB << Cost;
		}
		uint32 ExternalCount = static_cast<uint32>(Chunk.ExternalLinks.size());
		Ar << ExternalCount;
		for (const FBakedVoxelNavigationExternalLink& Link : Chunk.ExternalLinks)
		{
			uint8 LocalPortal = Link.LocalPortalId;
			uint8 Delta = Link.PackedNeighborChunkDelta;
			uint8 NeighborPortal = Link.NeighborPortalId;
			float Cost = Link.Cost;
			Ar << LocalPortal << Delta << NeighborPortal << Cost;
		}
		return Ar.IsValid();
	}

	bool ReadNavigationChunk(FArchive& Ar, FBakedVoxelNavigationChunk& OutChunk)
	{
		Ar.Serialize(OutChunk.Cells.data(), OutChunk.Cells.size());
		uint32 IntraCount = 0;
		Ar << IntraCount;
		if (!Ar.IsValid() || IntraCount > MaxNavigationAssetEdgesPerChunk) return false;
		OutChunk.IntraEdges.resize(IntraCount);
		for (FBakedVoxelNavigationIntraEdge& Edge : OutChunk.IntraEdges)
		{
			Ar << Edge.PortalA << Edge.PortalB << Edge.Cost;
			if (!Ar.IsValid()) return false;
		}
		uint32 ExternalCount = 0;
		Ar << ExternalCount;
		if (!Ar.IsValid() || ExternalCount > MaxNavigationAssetEdgesPerChunk) return false;
		OutChunk.ExternalLinks.resize(ExternalCount);
		for (FBakedVoxelNavigationExternalLink& Link : OutChunk.ExternalLinks)
		{
			Ar << Link.LocalPortalId << Link.PackedNeighborChunkDelta << Link.NeighborPortalId << Link.Cost;
			if (!Ar.IsValid()) return false;
		}
		return true;
	}
} // namespace

FVoxelNavigationGrid::~FVoxelNavigationGrid()
{
	MemoryStats::SubVoxelNavigationMemory(TrackedMemoryBytes);
}

bool FVoxelNavigationGrid::Build(
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
	BakedChunks.clear();
	NodeToChunkLookup.clear();
	NodeToLocalCellIdxLookup.clear();
	ChunkCellToNodeLookup.clear();
	BuildStats = FVoxelNavigationBuildStats();
	BuildSettings = Settings;
	RefreshTrackedMemory();

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
	RefreshTrackedMemory();

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
					const FVector StandingPoint(CellCenterX, CellCenterY, GroundHit.WorldHitLocation.Z);
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
					const size_t PreviousNodeCapacity = Nodes.capacity();
					Nodes.push_back(Node);
					TArray<int>& Column = XYToNodesLookup[FlattenColumn(X, Y)];
					const size_t PreviousColumnCapacity = Column.capacity();
					Column.push_back(NodeIndex);
					AcceptedGroundHeights.push_back(StandingPoint.Z);
					AddTrackedMemory(
						static_cast<size_t>(Nodes.capacity() - PreviousNodeCapacity) * sizeof(FVoxelNavigationNode) +
						static_cast<size_t>(Column.capacity() - PreviousColumnCapacity) * sizeof(int));
					UpdateBuildPeakMemory(static_cast<size_t>(AcceptedGroundHeights.capacity()) * sizeof(float));
				}
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
				if (HeightDifference <= Settings.MaxNeighborHeightDelta + Epsilon
					 && CanTraverse(World, NodeIndex, Candidate, QueryOwner))
				{
					AddDirectedEdge(NodeIndex, Candidate);
				}
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
				if (std::abs(Nodes[Candidate].Position.Z - Node.Position.Z) >
					Settings.MaxNeighborHeightDelta + Epsilon)
					continue;
				if (!HasCardinalBridge(NodeIndex, Candidate, Node.Coord.X + Offset[0], Node.Coord.Y) 
					|| !HasCardinalBridge(NodeIndex, Candidate, Node.Coord.X, Node.Coord.Y + Offset[1]))
					continue;

				if (CanTraverse(World, NodeIndex, Candidate, QueryOwner)) 
					AddDirectedEdge(NodeIndex, Candidate);
			}
		}
	}

	// 8방향으로 연결된 노드만 남기고 필터링 (mark and sweep)
	TArray<uint8> RetainedNodes(Nodes.size(), 0);
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
	bool bXYCollision = false;
	int CollidedX = 0;
	int CollidedY = 0;
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
		if (Chunk.Cells[LocalCell] != 0)
		{
			// 이웃한 지면과의 연결성으로 필터링했음에도 불구하고
			// 청크 내에 XY를 공유하는 복수의 청크가 존재하여 height map으로 옮기기 실패
			// → 빌드 거부
			bXYCollision = true;
			CollidedX = LocalX;
			CollidedY = LocalY;
			break;
		}
		const float ChunkBottom = BoundsMin.Z + (ChunkZ * NavL1ChunkSize);
		const float RelativeHeight = std::clamp(
			(Node.Position.Z - ChunkBottom) / NavL1ChunkSize, 0.0f, 1.0f);
		Chunk.Cells[LocalCell] = static_cast<uint8>(1 + static_cast<int>(std::round(RelativeHeight * 254.0f)));
		NodeToChunkLookup[NodeIndex] = ChunkIndex;
		NodeToLocalCellIdxLookup[NodeIndex] = LocalCell;
		ChunkCellToNodeLookup[ChunkIndex][LocalCell] = NodeIndex;
	}

	if (bXYCollision)
	{
		UE_LOG("[VoxelNavigationGrid] Build rejected: more than one nodes in chunk share (%d, %d) after erosion.",
				CollidedX, CollidedY);
		Nodes.clear();
		XYToNodesLookup.clear();
		L1Chunks.clear();
		L1ChunkLookup.assign(L1ChunkLookup.size(), -1);
		Portals.clear();
		NodeToChunkLookup.clear();
		NodeToLocalCellIdxLookup.clear();
		ChunkCellToNodeLookup.clear();
		RefreshTrackedMemory();
		return false;
	}

	// 기존 연결 그래프를 heightmap으로 압축했을 때 정보 손실 있는지 검사 (이웃한 노드간에 연결성이 담보되는지)
	for (int NodeIndex = 0; NodeIndex < Nodes.size(); ++NodeIndex)
	{
		if (RetainedNodes[NodeIndex] == 0) continue;
		const int ChunkIndex = NodeToChunkLookup[NodeIndex];
		const int LocalCell = NodeToLocalCellIdxLookup[NodeIndex];
		const int LocalX = LocalCell % NavL1ChunkCellsPerAxis;
		const int LocalY = LocalCell / NavL1ChunkCellsPerAxis;
		for (const auto& Offset : NeighborOffsets)
		{
			const int NX = LocalX + Offset[0];
			const int NY = LocalY + Offset[1];
			if (NX < 0 || NX >= NavL1ChunkCellsPerAxis ||
				NY < 0 || NY >= NavL1ChunkCellsPerAxis) continue;
			const int NeighborCell = NY * NavL1ChunkCellsPerAxis + NX;
			const int NeighborNode = ChunkCellToNodeLookup[ChunkIndex][NeighborCell];
			if (NeighborNode < 0) continue;
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
				RefreshTrackedMemory();
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

	// HPA* 계층 구성
	BuildAbstractGraph(RetainedNodes);
	if (!BuildBakedChunksFromRuntimeGraph() || !BuildRuntimeGraphFromBakedChunks())
	{
		UE_LOG("[VoxelNavigationGrid] Build rejected: baked chunk graph validation failed.");
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
	RefreshTrackedMemory();

	// 계산 과정에 썼던 임시 배열들 정리
	TArray<FVoxelNavigationNode>().swap(Nodes);
	TArray<TArray<int>>().swap(XYToNodesLookup);
	TArray<int>().swap(NodeToChunkLookup);
	TArray<int>().swap(NodeToLocalCellIdxLookup);
	TArray<TStaticArray<int, NavL1ChunkCellCount>>().swap(ChunkCellToNodeLookup);
	RefreshTrackedMemory();

	BuildStats.BuildTimeMs = ElapsedMilliseconds(StartTime);
	bBuilt = BuildStats.NumWalkableNodes > 0;
	return bBuilt;
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
	for (int Index = 0; Index < static_cast<int>(L1Chunks.size()); ++Index)
	{
		if (L1Chunks[Index].Coord == Coord)
		{
			return Index;
		}
	}
	return -1;
}

bool FVoxelNavigationGrid::BuildBakedChunksFromRuntimeGraph()
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
		{
			return false;
		}
		for (const FVoxelNavigationAbstractEdge& Edge : From.Edges)
		{
			if (Edge.ToPortal < 0 || Edge.ToPortal >= static_cast<int>(Portals.size()) ||
				!std::isfinite(Edge.Cost) || Edge.Cost < 0.0f)
			{
				return false;
			}
			const FVoxelNavigationPortal& To = Portals[Edge.ToPortal];
			if (From.ChunkIndex == To.ChunkIndex)
			{
				if (From.LocalCell >= To.LocalCell)
				{
					continue;
				}
				FBakedVoxelNavigationIntraEdge BakedEdge{ From.LocalCell, To.LocalCell, Edge.Cost };
				auto& Edges = BakedChunks[From.ChunkIndex].IntraEdges;
				const bool bExists = std::any_of(Edges.begin(), Edges.end(), [&BakedEdge](const auto& Existing)
				{
					return Existing.PortalA == BakedEdge.PortalA && Existing.PortalB == BakedEdge.PortalB;
				});
				if (!bExists)
				{
					Edges.push_back(BakedEdge);
				}
				continue;
			}

			const FVoxelCoord& SourceCoord = L1Chunks[From.ChunkIndex].Coord;
			const FVoxelCoord& TargetCoord = L1Chunks[To.ChunkIndex].Coord;
			uint8 PackedDelta = 0;
			if (!PackNeighborChunkDelta({ TargetCoord.X - SourceCoord.X, TargetCoord.Y - SourceCoord.Y, TargetCoord.Z - SourceCoord.Z }, PackedDelta))
			{
				return false;
			}
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
			return std::tie(A.LocalPortalId, A.PackedNeighborChunkDelta, A.NeighborPortalId) <
				std::tie(B.LocalPortalId, B.PackedNeighborChunkDelta, B.NeighborPortalId);
		});
	}
	return ValidateBakedChunks();
}

bool FVoxelNavigationGrid::ValidateBakedChunks() const
{
	for (size_t Index = 0; Index < BakedChunks.size(); ++Index)
	{
		const FBakedVoxelNavigationChunk& Chunk = BakedChunks[Index];
		for (size_t OtherIndex = 0; OtherIndex < Index; ++OtherIndex)
		{
			if (BakedChunks[OtherIndex].Coord == Chunk.Coord) return false;
		}
		FBakedNavPortal PreviousA = InvalidBakedNavPortal;
		FBakedNavPortal PreviousB = InvalidBakedNavPortal;
		for (const FBakedVoxelNavigationIntraEdge& Edge : Chunk.IntraEdges)
		{
			if (Edge.PortalA >= NavL1ChunkCellCount || Edge.PortalB >= NavL1ChunkCellCount ||
				Chunk.Cells[Edge.PortalA] == 0 || Chunk.Cells[Edge.PortalB] == 0 ||
				Edge.PortalA >= Edge.PortalB || !std::isfinite(Edge.Cost) || Edge.Cost < 0.0f ||
				(PreviousA == Edge.PortalA && PreviousB == Edge.PortalB))
			{
				return false;
			}
			PreviousA = Edge.PortalA;
			PreviousB = Edge.PortalB;
		}
		for (const FBakedVoxelNavigationExternalLink& Link : Chunk.ExternalLinks)
		{
			FVoxelCoord Delta;
			if (Link.LocalPortalId >= NavL1ChunkCellCount || Link.NeighborPortalId >= NavL1ChunkCellCount ||
				Chunk.Cells[Link.LocalPortalId] == 0 || !std::isfinite(Link.Cost) || Link.Cost < 0.0f ||
				!UnpackNeighborChunkDelta(Link.PackedNeighborChunkDelta, Delta))
			{
				return false;
			}
			const FVoxelCoord NeighborCoord{ Chunk.Coord.X + Delta.X, Chunk.Coord.Y + Delta.Y, Chunk.Coord.Z + Delta.Z };
			auto NeighborIt = std::find_if(BakedChunks.begin(), BakedChunks.end(), [&NeighborCoord](const auto& Candidate)
			{
				return Candidate.Coord == NeighborCoord;
			});
			if (NeighborIt == BakedChunks.end()) return false;
			if (NeighborIt->Cells[Link.NeighborPortalId] == 0) return false;
			uint8 ReverseDelta = 0;
			if (!PackNeighborChunkDelta({ -Delta.X, -Delta.Y, -Delta.Z }, ReverseDelta)) return false;
			const bool bReciprocal = std::any_of(NeighborIt->ExternalLinks.begin(), NeighborIt->ExternalLinks.end(),
				[&Link, ReverseDelta](const auto& Candidate)
				{
					return Candidate.LocalPortalId == Link.NeighborPortalId &&
						Candidate.NeighborPortalId == Link.LocalPortalId &&
						Candidate.PackedNeighborChunkDelta == ReverseDelta && std::abs(Candidate.Cost - Link.Cost) <= Epsilon;
				});
			if (!bReciprocal) return false;
		}
	}
	return true;
}

bool FVoxelNavigationGrid::BuildRuntimeGraphFromBakedChunks()
{
	if (!ValidateBakedChunks()) return false;
	L1Chunks.clear();
	Portals.clear();
	L1ChunkLookup.assign(L1ChunkCountX * L1ChunkCountY * L1ChunkCountZ, -1);
	for (const FBakedVoxelNavigationChunk& Baked : BakedChunks)
	{
		FVoxelNavigationL1Chunk Chunk;
		Chunk.Coord = Baked.Coord;
		Chunk.Cells = Baked.Cells;
		const int RuntimeChunkIndex = static_cast<int>(L1Chunks.size());
		L1Chunks.push_back(Chunk);
		if (IsValidL1Chunk(Baked.Coord.X, Baked.Coord.Y, Baked.Coord.Z))
		{
			L1ChunkLookup[FlattenL1Chunk(Baked.Coord.X, Baked.Coord.Y, Baked.Coord.Z)] = RuntimeChunkIndex;
		}
	}
	for (int ChunkIndex = 0; ChunkIndex < static_cast<int>(BakedChunks.size()); ++ChunkIndex)
	{
		for (const FBakedVoxelNavigationIntraEdge& Edge : BakedChunks[ChunkIndex].IntraEdges)
		{
			FindOrAddPortal(ChunkIndex, Edge.PortalA);
			FindOrAddPortal(ChunkIndex, Edge.PortalB);
		}
		for (const FBakedVoxelNavigationExternalLink& Link : BakedChunks[ChunkIndex].ExternalLinks)
		{
			FindOrAddPortal(ChunkIndex, Link.LocalPortalId);
		}
	}
	for (int ChunkIndex = 0; ChunkIndex < static_cast<int>(BakedChunks.size()); ++ChunkIndex)
	{
		for (const FBakedVoxelNavigationIntraEdge& Edge : BakedChunks[ChunkIndex].IntraEdges)
		{
			const int A = FindOrAddPortal(ChunkIndex, Edge.PortalA);
			const int B = FindOrAddPortal(ChunkIndex, Edge.PortalB);
			AddAbstractEdge(A, B, Edge.Cost);
			AddAbstractEdge(B, A, Edge.Cost);
		}
		for (const FBakedVoxelNavigationExternalLink& Link : BakedChunks[ChunkIndex].ExternalLinks)
		{
			FVoxelCoord Delta;
			UnpackNeighborChunkDelta(Link.PackedNeighborChunkDelta, Delta);
			const FVoxelCoord NeighborCoord{
				BakedChunks[ChunkIndex].Coord.X + Delta.X, BakedChunks[ChunkIndex].Coord.Y + Delta.Y, BakedChunks[ChunkIndex].Coord.Z + Delta.Z };
			const int NeighborChunkIndex = FindChunkIndexByCoord(NeighborCoord);
			if (NeighborChunkIndex < 0) return false;
			const int From = FindOrAddPortal(ChunkIndex, Link.LocalPortalId);
			const int To = FindOrAddPortal(NeighborChunkIndex, Link.NeighborPortalId);
			AddAbstractEdge(From, To, Link.Cost);
		}
	}
	return true;
}

bool FVoxelNavigationGrid::SaveReferenceJson(const FString& Path) const
{
	if (!bBuilt || !ValidateBakedChunks()) return false;
	using namespace json;
	auto ToJsonCoord = [](const FVoxelCoord& Value)
	{
		return Array(Value.X, Value.Y, Value.Z);
	};
	auto ToJsonVector = [](const FVector& Value)
	{
		return Array(Value.X, Value.Y, Value.Z);
	};

	JSON Root = Object();
	Root["FormatVersion"] = 1;
	Root["Transport"] = "VoxelNavigationReferenceJson";
	Root["BoundsCenter"] = ToJsonVector(BoundsCenter);
	Root["BoundsExtent"] = ToJsonVector(BoundsExtent);
	JSON Settings = Object();
	Settings["AgentRadius"] = BuildSettings.AgentRadius;
	Settings["AgentHeight"] = BuildSettings.AgentHeight;
	Settings["MaxWalkableSlopeDegrees"] = BuildSettings.MaxWalkableSlopeDegrees;
	Settings["MaxNeighborHeightDelta"] = BuildSettings.MaxNeighborHeightDelta;
	Settings["GroundProbeInset"] = BuildSettings.GroundProbeInset;
	Settings["ClearanceOffset"] = BuildSettings.ClearanceOffset;
	Root["Settings"] = Settings;
	JSON Chunks = Array();
	for (const FBakedVoxelNavigationChunk& Chunk : BakedChunks)
	{
		JSON Item = Object();
		Item["Coord"] = ToJsonCoord(Chunk.Coord);
		JSON Cells = Array();
		for (uint8 Cell : Chunk.Cells) Cells.append(static_cast<int>(Cell));
		Item["Cells"] = Cells;
		JSON IntraEdges = Array();
		for (const FBakedVoxelNavigationIntraEdge& Edge : Chunk.IntraEdges)
		{
			IntraEdges.append(Array(Edge.PortalA, Edge.PortalB, Edge.Cost));
		}
		Item["IntraEdges"] = IntraEdges;
		JSON ExternalLinks = Array();
		for (const FBakedVoxelNavigationExternalLink& Link : Chunk.ExternalLinks)
		{
			ExternalLinks.append(Array(Link.LocalPortalId, Link.PackedNeighborChunkDelta, Link.NeighborPortalId, Link.Cost));
		}
		Item["ExternalLinks"] = ExternalLinks;
		Chunks.append(Item);
	}
	Root["Chunks"] = Chunks;

	const std::filesystem::path OutputPath(FPaths::ToWide(Path));
	std::error_code Error;
	std::filesystem::create_directories(OutputPath.parent_path(), Error);
	std::ofstream File(OutputPath);
	if (!File.is_open()) return false;
	File << Root.dump(2);
	return File.good();
}

bool FVoxelNavigationGrid::LoadReferenceJson(const FString& Path)
{
	using namespace json;
	std::ifstream File(FPaths::ToWide(Path));
	if (!File.is_open()) return false;
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	JSON Root = JSON::Load(Buffer.str());
	if (Root.IsNull() || !Root.hasKey("FormatVersion") || Root["FormatVersion"].ToInt() != 1 ||
		!Root.hasKey("BoundsCenter") || !Root.hasKey("BoundsExtent") || !Root.hasKey("Settings") || !Root.hasKey("Chunks"))
	{
		return false;
	}
	auto ReadVector = [](JSON Value, FVector& OutValue)
	{
		if (Value.length() != 3) return false;
		OutValue = FVector(static_cast<float>(Value[0].ToFloat()), static_cast<float>(Value[1].ToFloat()), static_cast<float>(Value[2].ToFloat()));
		return std::isfinite(OutValue.X) && std::isfinite(OutValue.Y) && std::isfinite(OutValue.Z);
	};
	auto ReadCoord = [](JSON Value, FVoxelCoord& OutValue)
	{
		if (Value.length() != 3) return false;
		OutValue = { Value[0].ToInt(), Value[1].ToInt(), Value[2].ToInt() };
		return true;
	};
	FVector LoadedCenter;
	FVector LoadedExtent;
	if (!ReadVector(Root["BoundsCenter"], LoadedCenter) || !ReadVector(Root["BoundsExtent"], LoadedExtent) ||
		LoadedExtent.X <= Epsilon || LoadedExtent.Y <= Epsilon || LoadedExtent.Z <= Epsilon)
	{
		return false;
	}
	JSON SettingsJson = Root["Settings"];
	const char* RequiredSettings[] = { "AgentRadius", "AgentHeight", "MaxWalkableSlopeDegrees", "MaxNeighborHeightDelta", "GroundProbeInset", "ClearanceOffset" };
	for (const char* Key : RequiredSettings)
	{
		if (!SettingsJson.hasKey(Key)) return false;
	}
	FVoxelNavigationBuildSettings LoadedSettings;
	LoadedSettings.AgentRadius = static_cast<float>(SettingsJson["AgentRadius"].ToFloat());
	LoadedSettings.AgentHeight = static_cast<float>(SettingsJson["AgentHeight"].ToFloat());
	LoadedSettings.MaxWalkableSlopeDegrees = static_cast<float>(SettingsJson["MaxWalkableSlopeDegrees"].ToFloat());
	LoadedSettings.MaxNeighborHeightDelta = static_cast<float>(SettingsJson["MaxNeighborHeightDelta"].ToFloat());
	LoadedSettings.GroundProbeInset = static_cast<float>(SettingsJson["GroundProbeInset"].ToFloat());
	LoadedSettings.ClearanceOffset = static_cast<float>(SettingsJson["ClearanceOffset"].ToFloat());
	if (!std::isfinite(LoadedSettings.AgentRadius) || LoadedSettings.AgentRadius <= Epsilon ||
		!std::isfinite(LoadedSettings.AgentHeight) || LoadedSettings.AgentHeight <= Epsilon)
	{
		return false;
	}

	TArray<FBakedVoxelNavigationChunk> LoadedChunks;
	JSON ChunksJson = Root["Chunks"];
	for (int ChunkJsonIndex = 0; ChunkJsonIndex < ChunksJson.length(); ++ChunkJsonIndex)
	{
		JSON Item = ChunksJson[ChunkJsonIndex];
		if (!Item.hasKey("Coord") || !Item.hasKey("Cells") || !Item.hasKey("IntraEdges") || !Item.hasKey("ExternalLinks")) return false;
		FBakedVoxelNavigationChunk Chunk;
		JSON Cells = Item["Cells"];
		if (!ReadCoord(Item["Coord"], Chunk.Coord) || Cells.length() != NavL1ChunkCellCount) return false;
		for (int CellIndex = 0; CellIndex < NavL1ChunkCellCount; ++CellIndex)
		{
			const int Value = Cells[CellIndex].ToInt();
			if (Value < 0 || Value > 255) return false;
			Chunk.Cells[CellIndex] = static_cast<uint8>(Value);
		}
		JSON IntraEdges = Item["IntraEdges"];
		for (int EdgeIndex = 0; EdgeIndex < IntraEdges.length(); ++EdgeIndex)
		{
			JSON Edge = IntraEdges[EdgeIndex];
			if (Edge.length() != 3) return false;
			Chunk.IntraEdges.push_back({ static_cast<uint8>(Edge[0].ToInt()), static_cast<uint8>(Edge[1].ToInt()), static_cast<float>(Edge[2].ToFloat()) });
		}
		JSON ExternalLinks = Item["ExternalLinks"];
		for (int LinkIndex = 0; LinkIndex < ExternalLinks.length(); ++LinkIndex)
		{
			JSON Link = ExternalLinks[LinkIndex];
			if (Link.length() != 4) return false;
			Chunk.ExternalLinks.push_back({ static_cast<uint8>(Link[0].ToInt()), static_cast<uint8>(Link[1].ToInt()), static_cast<uint8>(Link[2].ToInt()), static_cast<float>(Link[3].ToFloat()) });
		}
		std::sort(Chunk.IntraEdges.begin(), Chunk.IntraEdges.end(), [](const auto& A, const auto& B)
		{
			return std::tie(A.PortalA, A.PortalB) < std::tie(B.PortalA, B.PortalB);
		});
		LoadedChunks.push_back(std::move(Chunk));
	}
	if (LoadedChunks.empty()) return false;
	std::sort(LoadedChunks.begin(), LoadedChunks.end(), [](const auto& A, const auto& B)
	{
		return std::tie(A.Coord.X, A.Coord.Y, A.Coord.Z) < std::tie(B.Coord.X, B.Coord.Y, B.Coord.Z);
	});

	BoundsCenter = LoadedCenter;
	BoundsExtent = LoadedExtent;
	BoundsMin = BoundsCenter - BoundsExtent;
	BuildSettings = LoadedSettings;
	CellCountX = static_cast<int>(std::ceil(BoundsExtent.X * 2.0f / NavVoxelCellSize));
	CellCountY = static_cast<int>(std::ceil(BoundsExtent.Y * 2.0f / NavVoxelCellSize));
	CellCountZ = static_cast<int>(std::ceil(BoundsExtent.Z * 2.0f / NavVoxelCellSize));
	L1ChunkCountX = (CellCountX + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	L1ChunkCountY = (CellCountY + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	L1ChunkCountZ = (CellCountZ + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	BakedChunks = std::move(LoadedChunks);
	if (!BuildRuntimeGraphFromBakedChunks())
	{
		bBuilt = false;
		return false;
	}
	BuildStats = FVoxelNavigationBuildStats();
	BuildStats.NumBuiltL1Chunks = L1Chunks.size();
	for (const FVoxelNavigationL1Chunk& Chunk : L1Chunks)
	{
		for (uint8 Cell : Chunk.Cells) BuildStats.NumWalkableNodes += Cell != 0;
	}
	BuildStats.NumAbstractNodes = Portals.size();
	for (const FVoxelNavigationPortal& Portal : Portals) BuildStats.NumAbstractEdges += Portal.Edges.size();
	bBuilt = BuildStats.NumWalkableNodes > 0;
	RefreshTrackedMemory();
	return bBuilt;
}

bool FVoxelNavigationGrid::SaveNavigationAsset(const FString& Path, const FString& SourceScenePath) const
{
	if (!bBuilt || !ValidateBakedChunks() || BakedChunks.empty() || BakedChunks.size() > MaxNavigationAssetChunks)
	{
		return false;
	}

	TArray<FBakedVoxelNavigationChunk> Chunks = BakedChunks;
	std::sort(Chunks.begin(), Chunks.end(), [](const auto& A, const auto& B) { return IsCoordLess(A.Coord, B.Coord); });
	for (FBakedVoxelNavigationChunk& Chunk : Chunks)
	{
		std::sort(Chunk.IntraEdges.begin(), Chunk.IntraEdges.end(), [](const auto& A, const auto& B)
		{
			return std::tie(A.PortalA, A.PortalB, A.Cost) < std::tie(B.PortalA, B.PortalB, B.Cost);
		});
		std::sort(Chunk.ExternalLinks.begin(), Chunk.ExternalLinks.end(), [](const auto& A, const auto& B)
		{
			return std::tie(A.LocalPortalId, A.PackedNeighborChunkDelta, A.NeighborPortalId, A.Cost) <
				std::tie(B.LocalPortalId, B.PackedNeighborChunkDelta, B.NeighborPortalId, B.Cost);
		});
	}

	const std::filesystem::path OutputPath(FPaths::ToWide(Path));
	std::error_code Error;
	std::filesystem::create_directories(OutputPath.parent_path(), Error);
	if (Error) return false;
	const FString StagingPath = Path + ".tmp";
	const std::filesystem::path StagingOutputPath(FPaths::ToWide(StagingPath));

	const bool bWritten = [&]()
	{
	FWindowsBinWriter Ar(FPaths::MakeProjectRelative(StagingPath));
	if (!Ar.IsValid()) return false;
	FAssetImportMetadata Metadata;
	Metadata.SourcePath = SourceScenePath;
	if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::VoxelNavigation, Metadata)) return false;
	Ar.SetTaggedPropertySerializationEnabled(false);
	const uint64 PayloadStart = Ar.Tell();

	uint32 FormatVersion = NavigationAssetFormatVersion;
	uint32 ByteOrder = NavigationAssetByteOrder;
	uint32 ChunkCount = static_cast<uint32>(Chunks.size());
	float AgentRadius = BuildSettings.AgentRadius;
	float AgentHeight = BuildSettings.AgentHeight;
	float MaxWalkableSlopeDegrees = BuildSettings.MaxWalkableSlopeDegrees;
	float MaxNeighborHeightDelta = BuildSettings.MaxNeighborHeightDelta;
	float GroundProbeInset = BuildSettings.GroundProbeInset;
	float ClearanceOffset = BuildSettings.ClearanceOffset;
	Ar << FormatVersion << ByteOrder;
	Ar.Serialize(const_cast<float*>(BoundsCenter.Data), sizeof(BoundsCenter.Data));
	Ar.Serialize(const_cast<float*>(BoundsExtent.Data), sizeof(BoundsExtent.Data));
	Ar << AgentRadius << AgentHeight << MaxWalkableSlopeDegrees << MaxNeighborHeightDelta << GroundProbeInset << ClearanceOffset;
	Ar << ChunkCount;
	if (!Ar.IsValid()) return false;

	const uint64 IndexStart = Ar.Tell();
	FNavigationChunkIndexEntry EmptyEntry;
	for (uint32 Index = 0; Index < ChunkCount; ++Index)
	{
		if (!WriteChunkIndexEntry(Ar, EmptyEntry)) return false;
	}

	TArray<FNavigationChunkIndexEntry> Index;
	Index.reserve(Chunks.size());
	for (const FBakedVoxelNavigationChunk& Chunk : Chunks)
	{
		const uint64 ChunkStart = Ar.Tell();
		if (ChunkStart < PayloadStart || !WriteNavigationChunk(Ar, Chunk)) return false;
		const uint64 ChunkEnd = Ar.Tell();
		if (ChunkEnd < ChunkStart || ChunkEnd - ChunkStart > (std::numeric_limits<uint32>::max)()) return false;
		Index.push_back({ Chunk.Coord, ChunkStart - PayloadStart, static_cast<uint32>(ChunkEnd - ChunkStart) });
	}
	const uint64 EndPosition = Ar.Tell();
	if (!Ar.Seek(IndexStart)) return false;
	for (const FNavigationChunkIndexEntry& Entry : Index)
	{
		if (!WriteChunkIndexEntry(Ar, Entry)) return false;
	}
	if (!Ar.Seek(EndPosition)) return false;
	return Ar.IsValid();
	}();

	if (!bWritten)
	{
		std::filesystem::remove(StagingOutputPath, Error);
		return false;
	}

	const std::wstring StagingPathWide = FPaths::ToWide(FPaths::MakeProjectRelative(StagingPath));
	const std::wstring OutputPathWide = FPaths::ToWide(FPaths::MakeProjectRelative(Path));
	if (!::MoveFileExW(StagingPathWide.c_str(), OutputPathWide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		std::filesystem::remove(StagingOutputPath, Error);
		return false;
	}
	return true;
}

bool FVoxelNavigationGrid::LoadNavigationAsset(const FString& Path)
{
	FWindowsBinReader Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;
	FAssetPackageHeader PackageHeader;
	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::VoxelNavigation, PackageHeader, Metadata)) return false;
	Ar.SetTaggedPropertySerializationEnabled(false);
	const uint64 PayloadStart = Ar.Tell();
	const uint64 FileSize = Ar.Size();

	uint32 FormatVersion = 0;
	uint32 ByteOrder = 0;
	FVector LoadedCenter;
	FVector LoadedExtent;
	FVoxelNavigationBuildSettings LoadedSettings;
	uint32 ChunkCount = 0;
	Ar << FormatVersion << ByteOrder;
	Ar.Serialize(LoadedCenter.Data, sizeof(LoadedCenter.Data));
	Ar.Serialize(LoadedExtent.Data, sizeof(LoadedExtent.Data));
	Ar << LoadedSettings.AgentRadius << LoadedSettings.AgentHeight << LoadedSettings.MaxWalkableSlopeDegrees
		<< LoadedSettings.MaxNeighborHeightDelta << LoadedSettings.GroundProbeInset << LoadedSettings.ClearanceOffset;
	Ar << ChunkCount;
	if (!Ar.IsValid() || FormatVersion != NavigationAssetFormatVersion || ByteOrder != NavigationAssetByteOrder ||
		ChunkCount == 0 || ChunkCount > MaxNavigationAssetChunks ||
		!std::isfinite(LoadedCenter.X) || !std::isfinite(LoadedCenter.Y) || !std::isfinite(LoadedCenter.Z) ||
		!std::isfinite(LoadedExtent.X) || !std::isfinite(LoadedExtent.Y) || !std::isfinite(LoadedExtent.Z) ||
		LoadedExtent.X <= Epsilon || LoadedExtent.Y <= Epsilon || LoadedExtent.Z <= Epsilon ||
		!std::isfinite(LoadedSettings.AgentRadius) || LoadedSettings.AgentRadius <= Epsilon ||
		!std::isfinite(LoadedSettings.AgentHeight) || LoadedSettings.AgentHeight <= Epsilon ||
		!std::isfinite(LoadedSettings.MaxWalkableSlopeDegrees) || !std::isfinite(LoadedSettings.MaxNeighborHeightDelta) ||
		!std::isfinite(LoadedSettings.GroundProbeInset) || !std::isfinite(LoadedSettings.ClearanceOffset))
	{
		return false;
	}

	TArray<FNavigationChunkIndexEntry> Index(ChunkCount);
	for (FNavigationChunkIndexEntry& Entry : Index)
	{
		if (!ReadChunkIndexEntry(Ar, Entry)) return false;
	}
	const uint64 PayloadDataStart = Ar.Tell();
	if (PayloadDataStart < PayloadStart || PayloadDataStart > FileSize) return false;

	uint64 ExpectedOffset = PayloadDataStart - PayloadStart;
	for (size_t IndexEntry = 0; IndexEntry < Index.size(); ++IndexEntry)
	{
		const FNavigationChunkIndexEntry& Entry = Index[IndexEntry];
		if ((IndexEntry > 0 && !IsCoordLess(Index[IndexEntry - 1].Coord, Entry.Coord)) ||
			Entry.Offset != ExpectedOffset || Entry.Size == 0 || Entry.Offset > FileSize - PayloadStart ||
			Entry.Size > FileSize - PayloadStart - Entry.Offset)
		{
			return false;
		}
		ExpectedOffset += Entry.Size;
	}
	if (PayloadStart > FileSize || ExpectedOffset != FileSize - PayloadStart) return false;

	TArray<FBakedVoxelNavigationChunk> LoadedChunks;
	LoadedChunks.reserve(ChunkCount);
	for (const FNavigationChunkIndexEntry& Entry : Index)
	{
		const uint64 ChunkStart = PayloadStart + Entry.Offset;
		if (!Ar.Seek(ChunkStart)) return false;
		FBakedVoxelNavigationChunk Chunk;
		Chunk.Coord = Entry.Coord;
		if (!ReadNavigationChunk(Ar, Chunk) || Ar.Tell() != ChunkStart + Entry.Size) return false;
		LoadedChunks.push_back(std::move(Chunk));
	}

	BoundsCenter = LoadedCenter;
	BoundsExtent = LoadedExtent;
	BoundsMin = BoundsCenter - BoundsExtent;
	BuildSettings = LoadedSettings;
	CellCountX = static_cast<int>(std::ceil(BoundsExtent.X * 2.0f / NavVoxelCellSize));
	CellCountY = static_cast<int>(std::ceil(BoundsExtent.Y * 2.0f / NavVoxelCellSize));
	CellCountZ = static_cast<int>(std::ceil(BoundsExtent.Z * 2.0f / NavVoxelCellSize));
	L1ChunkCountX = (CellCountX + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	L1ChunkCountY = (CellCountY + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	L1ChunkCountZ = (CellCountZ + NavL1ChunkCellsPerAxis - 1) / NavL1ChunkCellsPerAxis;
	for (const FBakedVoxelNavigationChunk& Chunk : LoadedChunks)
	{
		if (!IsValidL1Chunk(Chunk.Coord.X, Chunk.Coord.Y, Chunk.Coord.Z)) return false;
	}
	BakedChunks = std::move(LoadedChunks);
	if (!BuildRuntimeGraphFromBakedChunks())
	{
		bBuilt = false;
		return false;
	}
	BuildStats = FVoxelNavigationBuildStats();
	BuildStats.NumBuiltL1Chunks = L1Chunks.size();
	for (const FVoxelNavigationL1Chunk& Chunk : L1Chunks)
	{
		for (uint8 Cell : Chunk.Cells) BuildStats.NumWalkableNodes += Cell != 0;
	}
	BuildStats.NumAbstractNodes = Portals.size();
	for (const FVoxelNavigationPortal& Portal : Portals) BuildStats.NumAbstractEdges += Portal.Edges.size();
	bBuilt = BuildStats.NumWalkableNodes > 0;
	RefreshTrackedMemory();
	return bBuilt;
}

bool FVoxelNavigationGrid::HasLoadedNavigationAt(const FVector& Point) const
{
	if (!bBuilt || !Contains(Point)) return false;
	const int ChunkX = static_cast<int>(std::floor((Point.X - BoundsMin.X) / NavL1ChunkSize));
	const int ChunkY = static_cast<int>(std::floor((Point.Y - BoundsMin.Y) / NavL1ChunkSize));
	const int ChunkZ = static_cast<int>(std::floor((Point.Z - BoundsMin.Z) / NavL1ChunkSize));
	return FindChunkIndexByCoord({ ChunkX, ChunkY, ChunkZ }) >= 0;
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

	if (!bBuilt || !IsNavigationReadyFor(Start, Goal))
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
	if (StartCell.ChunkIndex == GoalCell.ChunkIndex)	// 만약 목적지가 같은 청크 안에 있다면
	{
		const float DirectCost = FindLocalPath(StartCell, GoalCell, &ConcreteCells);
		if (DirectCost < (std::numeric_limits<float>::max)() &&
			(MaxPathLength <= 0.0f || DirectCost <= MaxPathLength))
		{
			for (const FCellRef& Cell : ConcreteCells) Result.Points.push_back(GetCellPosition(Cell));
			Result.PathLength = DirectCost;
			Result.bSuccess = true;
			Result.bPartial = FVector::Distance(Result.Points.back(), Goal) > GoalAcceptanceRadius;
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
	while (!PriorityQue.empty())
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
		for (const FCellRef& Cell : ConcreteCells)
		{
			Result.Points.push_back(GetCellPosition(Cell));
		}
		Result.PathLength = PartialCost;
		Result.bSuccess = true;
		Result.bPartial = true;
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

	for (const FCellRef& Cell : ConcreteCells)
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
		Result.SearchTimeMs = ElapsedMilliseconds(StartTime);
		return Result;
	}

	Result.bSuccess = true;
	Result.bPartial = bAbstractPartial || FVector::Distance(Result.Points.back(), Goal) > GoalAcceptanceRadius;
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
	if (MaxNodes <= 0) return;

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
	if (MaxChunks <= 0) return;

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

int FVoxelNavigationGrid::FlattenColumn(int X, int Y) const
{
	return Y * CellCountX + X;
}

bool FVoxelNavigationGrid::IsValidColumn(int X, int Y) const
{
	return X >= 0 && X < CellCountX && Y >= 0 && Y < CellCountY;
}

int FVoxelNavigationGrid::FlattenL1Chunk(int X, int Y, int Z) const
{
	return (Z * L1ChunkCountY + Y) * L1ChunkCountX + X;
}

bool FVoxelNavigationGrid::IsValidL1Chunk(int X, int Y, int Z) const
{
	return X >= 0 && X < L1ChunkCountX && Y >= 0 && Y < L1ChunkCountY && Z >= 0 && Z < L1ChunkCountZ;
}

FVoxelNavigationGrid::FCellRef FVoxelNavigationGrid::FindNearestCell(const FVector& Point, float MaxDistance) const
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
				if (!IsValidL1Chunk(X, Y, Z)) 
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

void FVoxelNavigationGrid::BuildAbstractGraph(const TArray<uint8>& RetainedNodes)
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
			if (!IsCellWalkable(ChunkIndex, Seed) || Subareas[ChunkIndex][Seed] >= 0) continue;
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

int FVoxelNavigationGrid::FindOrAddPortal(int ChunkIndex, int LocalCell)
{
	FVoxelNavigationL1Chunk& Chunk = L1Chunks[ChunkIndex];
	for (int PortalIndex : Chunk.PortalIndices)
	{
		if (Portals[PortalIndex].LocalCell == LocalCell) 
			return PortalIndex;
	}
	const int PortalIndex = static_cast<int>(Portals.size());
	FVoxelNavigationPortal Portal;
	Portal.ChunkIndex = ChunkIndex;
	Portal.LocalCell = static_cast<uint8>(LocalCell);
	Portals.push_back(Portal);
	Chunk.PortalIndices.push_back(PortalIndex);
	return PortalIndex;
}

void FVoxelNavigationGrid::AddAbstractEdge(int FromPortal, int ToPortal, float Cost)
{
	if (FromPortal < 0 || ToPortal < 0 || FromPortal == ToPortal) 
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

bool FVoxelNavigationGrid::CanTraverse(UWorld* World, int FromNode, int ToNode, const AActor* QueryOwner) const
{
	if (!World || FromNode < 0 || ToNode < 0) 
		return false;

	const FVector Lift = FVector::UpVector * (BuildSettings.AgentHeight * 0.5f + BuildSettings.ClearanceOffset);
	const FVector Start = Nodes[FromNode].Position + Lift;
	const FVector End = Nodes[ToNode].Position + Lift;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(BuildSettings.AgentRadius, BuildSettings.AgentHeight * 0.5f);
	FHitResult Hit;
	return !World->PhysicsSweepByObjectTypes(
		Start, End, FQuat::Identity, Shape, Hit,
		ObjectTypeBit(ECollisionChannel::WorldStatic), QueryOwner);
}

bool FVoxelNavigationGrid::HasCardinalBridge(int FromNode, int ToNode, int BridgeX, int BridgeY) const
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

void FVoxelNavigationGrid::AddDirectedEdge(int FromNode, int ToNode)
{
	if (FromNode < 0 || ToNode < 0 || FromNode == ToNode) 
		return;

	TArray<int>& Neighbors = Nodes[FromNode].Neighbors;
	if (!ContainsIndex(Neighbors, ToNode))
	{
		const size_t PreviousCapacity = Neighbors.capacity();
		Neighbors.push_back(ToNode);
		AddTrackedMemory((Neighbors.capacity() - PreviousCapacity) * sizeof(int));
		++BuildStats.NumDirectedEdges;
	}
}

void FVoxelNavigationGrid::RefreshTrackedMemory()
{
	const uint64 NewTrackedMemoryBytes = CalculateTrackedMemoryBytes();
	MemoryStats::SetVoxelNavigationMemory(NewTrackedMemoryBytes);
	TrackedMemoryBytes = NewTrackedMemoryBytes;
	UpdateBuildPeakMemory();
}

size_t FVoxelNavigationGrid::CalculateTrackedMemoryBytes() const
{
	size_t TotalBytes = 0;
	TotalBytes += Nodes.capacity() * sizeof(FVoxelNavigationNode);
	TotalBytes += XYToNodesLookup.capacity() * sizeof(TArray<int>);
	TotalBytes += L1Chunks.capacity() * sizeof(FVoxelNavigationL1Chunk);
	TotalBytes += L1ChunkLookup.capacity() * sizeof(int);
	TotalBytes += Portals.capacity() * sizeof(FVoxelNavigationPortal);
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
	for (const FVoxelNavigationL1Chunk& Chunk : L1Chunks)
	{
		TotalBytes += Chunk.PortalIndices.capacity() * sizeof(int);
	}
	for (const FVoxelNavigationPortal& Portal : Portals)
	{
		TotalBytes += Portal.Edges.capacity() * sizeof(FVoxelNavigationAbstractEdge);
	}
	return TotalBytes;
}

void FVoxelNavigationGrid::AddTrackedMemory(size_t Size)
{
	if (Size == 0) return;
	TrackedMemoryBytes += Size;
	MemoryStats::AddVoxelNavigationMemory(Size);
	UpdateBuildPeakMemory();
}

void FVoxelNavigationGrid::UpdateBuildPeakMemory(uint64 TemporaryMemoryBytes)
{
	BuildStats.PeakMemoryBytes = (std::max)(BuildStats.PeakMemoryBytes, TrackedMemoryBytes + TemporaryMemoryBytes);
}
