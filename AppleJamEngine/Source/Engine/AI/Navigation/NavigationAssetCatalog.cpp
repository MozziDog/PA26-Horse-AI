#include "pch.h"

#include "AI/Navigation/NavigationAssetCatalog.h"

#include "Asset/AssetPackage.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <tuple>
#include <Windows.h>

namespace
{
	constexpr float Epsilon = 1.e-4f;
	constexpr uint32 FormatVersion = 1;
	constexpr uint32 ByteOrder = 0x01020304u;
	constexpr uint32 MaxChunks = 1u << 20;
	constexpr uint32 MaxEdgesPerChunk = 1u << 20;

	bool IsCoordLess(const FVoxelCoord& A, const FVoxelCoord& B)
	{
		return std::tie(A.X, A.Y, A.Z) < std::tie(B.X, B.Y, B.Z);
	}

	bool UnpackNeighborDelta(uint8 Packed, FVoxelCoord& OutDelta)
	{
		const int X = static_cast<int>(Packed & 0x3u) - 1;
		const int Y = static_cast<int>((Packed >> 2) & 0x3u) - 1;
		const int Z = static_cast<int>((Packed >> 4) & 0x3u) - 1;
		if (X < -1 || X > 1 || Y < -1 || Y > 1 || Z < -1 || Z > 1 || (X == 0 && Y == 0 && Z == 0)) return false;
		OutDelta = { X, Y, Z };
		return true;
	}

	bool PackNeighborDelta(const FVoxelCoord& Delta, uint8& OutPacked)
	{
		if (Delta.X < -1 || Delta.X > 1 || Delta.Y < -1 || Delta.Y > 1 || Delta.Z < -1 || Delta.Z > 1 ||
			(Delta.X == 0 && Delta.Y == 0 && Delta.Z == 0)) return false;
		OutPacked = static_cast<uint8>((Delta.X + 1) | ((Delta.Y + 1) << 2) | ((Delta.Z + 1) << 4));
		return true;
	}

	bool WriteIndexEntry(FArchive& Ar, const FVoxelCoord& Coord, uint64 Offset, uint32 Size)
	{
		int32 X = Coord.X, Y = Coord.Y, Z = Coord.Z;
		Ar << X << Y << Z << Offset << Size;
		return Ar.IsValid();
	}

	bool ReadIndexEntry(FArchive& Ar, FVoxelCoord& Coord, uint64& Offset, uint32& Size)
	{
		Ar << Coord.X << Coord.Y << Coord.Z << Offset << Size;
		return Ar.IsValid();
	}

	bool WriteChunk(FArchive& Ar, const FBakedVoxelNavigationChunk& Chunk)
	{
		Ar.Serialize(const_cast<uint8*>(Chunk.Cells.data()), Chunk.Cells.size());
		uint32 IntraCount = static_cast<uint32>(Chunk.IntraEdges.size());
		Ar << IntraCount;
		for (const FBakedVoxelNavigationIntraEdge& Edge : Chunk.IntraEdges)
		{
			uint8 A = Edge.PortalA, B = Edge.PortalB;
			float Cost = Edge.Cost;
			Ar << A << B << Cost;
		}
		uint32 ExternalCount = static_cast<uint32>(Chunk.ExternalLinks.size());
		Ar << ExternalCount;
		for (const FBakedVoxelNavigationExternalLink& Link : Chunk.ExternalLinks)
		{
			uint8 Local = Link.LocalPortalId, Delta = Link.PackedNeighborChunkDelta, Neighbor = Link.NeighborPortalId;
			float Cost = Link.Cost;
			Ar << Local << Delta << Neighbor << Cost;
		}
		return Ar.IsValid();
	}

	bool ReadChunk(FArchive& Ar, FBakedVoxelNavigationChunk& OutChunk)
	{
		Ar.Serialize(OutChunk.Cells.data(), OutChunk.Cells.size());
		uint32 IntraCount = 0;
		Ar << IntraCount;
		if (!Ar.IsValid() || IntraCount > MaxEdgesPerChunk) return false;
		OutChunk.IntraEdges.resize(IntraCount);
		for (FBakedVoxelNavigationIntraEdge& Edge : OutChunk.IntraEdges)
		{
			Ar << Edge.PortalA << Edge.PortalB << Edge.Cost;
			if (!Ar.IsValid()) return false;
		}
		uint32 ExternalCount = 0;
		Ar << ExternalCount;
		if (!Ar.IsValid() || ExternalCount > MaxEdgesPerChunk) return false;
		OutChunk.ExternalLinks.resize(ExternalCount);
		for (FBakedVoxelNavigationExternalLink& Link : OutChunk.ExternalLinks)
		{
			Ar << Link.LocalPortalId << Link.PackedNeighborChunkDelta << Link.NeighborPortalId << Link.Cost;
			if (!Ar.IsValid()) return false;
		}
		return true;
	}

	bool HasCoord(const TArray<FVoxelCoord>& Coords, const FVoxelCoord& Target)
	{
		return std::any_of(Coords.begin(), Coords.end(), [&Target](const FVoxelCoord& Value) { return Value == Target; });
	}

	bool ValidateChunkPayloadAgainstCatalog(const FBakedVoxelNavigationChunk& Chunk, const TArray<FVoxelCoord>& Available)
	{
		for (const FBakedVoxelNavigationIntraEdge& Edge : Chunk.IntraEdges)
		{
			if (Edge.PortalA >= NavL1ChunkCellCount || Edge.PortalB >= NavL1ChunkCellCount || Edge.PortalA >= Edge.PortalB ||
				Chunk.Cells[Edge.PortalA] == 0 || Chunk.Cells[Edge.PortalB] == 0 || !std::isfinite(Edge.Cost) || Edge.Cost < 0.0f) return false;
		}
		for (const FBakedVoxelNavigationExternalLink& Link : Chunk.ExternalLinks)
		{
			FVoxelCoord Delta;
			if (Link.LocalPortalId >= NavL1ChunkCellCount || Link.NeighborPortalId >= NavL1ChunkCellCount ||
				Chunk.Cells[Link.LocalPortalId] == 0 || !std::isfinite(Link.Cost) || Link.Cost < 0.0f || !UnpackNeighborDelta(Link.PackedNeighborChunkDelta, Delta)) return false;
			const FVoxelCoord Neighbor{ Chunk.Coord.X + Delta.X, Chunk.Coord.Y + Delta.Y, Chunk.Coord.Z + Delta.Z };
			if (!HasCoord(Available, Neighbor)) return false;
		}
		return true;
	}

	bool ValidateNavigationAssetDataset(const TArray<FBakedVoxelNavigationChunk>& Chunks)
	{
		if (Chunks.empty() || Chunks.size() > MaxChunks) return false;
		TArray<FVoxelCoord> Coords;
		Coords.reserve(Chunks.size());
		for (const FBakedVoxelNavigationChunk& Chunk : Chunks) Coords.push_back(Chunk.Coord);
		std::sort(Coords.begin(), Coords.end(), IsCoordLess);
		if (std::adjacent_find(Coords.begin(), Coords.end(), [](const FVoxelCoord& A, const FVoxelCoord& B) { return A == B; }) != Coords.end()) return false;
		for (const FBakedVoxelNavigationChunk& Chunk : Chunks)
		{
			if (!ValidateChunkPayloadAgainstCatalog(Chunk, Coords)) return false;
			for (const FBakedVoxelNavigationExternalLink& Link : Chunk.ExternalLinks)
			{
				FVoxelCoord Delta;
				UnpackNeighborDelta(Link.PackedNeighborChunkDelta, Delta);
				const FVoxelCoord NeighborCoord{ Chunk.Coord.X + Delta.X, Chunk.Coord.Y + Delta.Y, Chunk.Coord.Z + Delta.Z };
				auto Neighbor = std::find_if(Chunks.begin(), Chunks.end(), [&NeighborCoord](const auto& Value) { return Value.Coord == NeighborCoord; });
				if (Neighbor == Chunks.end() || Link.NeighborPortalId >= NavL1ChunkCellCount || Neighbor->Cells[Link.NeighborPortalId] == 0) return false;
				uint8 ReverseDelta = 0;
				if (!PackNeighborDelta({ -Delta.X, -Delta.Y, -Delta.Z }, ReverseDelta)) return false;
				const bool bReciprocal = std::any_of(Neighbor->ExternalLinks.begin(), Neighbor->ExternalLinks.end(), [&Link, ReverseDelta](const auto& Candidate)
				{
					return Candidate.LocalPortalId == Link.NeighborPortalId && Candidate.NeighborPortalId == Link.LocalPortalId &&
						Candidate.PackedNeighborChunkDelta == ReverseDelta && std::abs(Candidate.Cost - Link.Cost) <= Epsilon;
				});
				if (!bReciprocal) return false;
			}
		}
		return true;
	}
}

bool FNavigationAssetCatalog::Open(const FString& InAssetPath)
{
	AssetPath.clear();
	Info = {};
	Index.clear();

	FWindowsBinReader Ar(FPaths::MakeProjectRelative(InAssetPath));
	if (!Ar.IsValid()) 
		return false;

	FAssetPackageHeader Header;
	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadPackagePrelude(Ar, EAssetPackageType::VoxelNavigation, Header, Metadata)) 
		return false;

	Ar.SetTaggedPropertySerializationEnabled(false);
	const uint64 PayloadStart = Ar.Tell();
	const uint64 FileSize = Ar.Size();
	uint32 Version = 0, StoredByteOrder = 0, ChunkCount = 0;
	Ar << Version << StoredByteOrder;
	Ar.Serialize(Info.BoundsCenter.Data, sizeof(Info.BoundsCenter.Data));
	Ar.Serialize(Info.BoundsExtent.Data, sizeof(Info.BoundsExtent.Data));
	Ar << Info.Settings.AgentRadius << Info.Settings.AgentHeight << Info.Settings.MaxWalkableSlopeDegrees
		<< Info.Settings.MaxNeighborHeightDelta << Info.Settings.GroundProbeInset << Info.Settings.ClearanceOffset << ChunkCount;
	Info.SourceScenePath = Metadata.SourcePath;

	// 데이터 유효성 검사
	if (!Ar.IsValid() || Version != FormatVersion || StoredByteOrder != ByteOrder || ChunkCount == 0 || ChunkCount > MaxChunks ||
		!std::isfinite(Info.BoundsCenter.X) || !std::isfinite(Info.BoundsCenter.Y) || !std::isfinite(Info.BoundsCenter.Z) ||
		!std::isfinite(Info.BoundsExtent.X) || !std::isfinite(Info.BoundsExtent.Y) || !std::isfinite(Info.BoundsExtent.Z) ||
		Info.BoundsExtent.X <= Epsilon || Info.BoundsExtent.Y <= Epsilon || Info.BoundsExtent.Z <= Epsilon ||
		!std::isfinite(Info.Settings.AgentRadius) || Info.Settings.AgentRadius <= Epsilon ||
		!std::isfinite(Info.Settings.AgentHeight) || Info.Settings.AgentHeight <= Epsilon ||
		!std::isfinite(Info.Settings.MaxWalkableSlopeDegrees) || !std::isfinite(Info.Settings.MaxNeighborHeightDelta) ||
		!std::isfinite(Info.Settings.GroundProbeInset) || !std::isfinite(Info.Settings.ClearanceOffset)) 
		return false;
	Index.resize(ChunkCount);
	Info.AvailableChunkCoords.reserve(ChunkCount);
	for (FChunkIndexEntry& Entry : Index)
	{
		uint64 RelativeOffset = 0;
		if (!ReadIndexEntry(Ar, Entry.Coord, RelativeOffset, Entry.Size)) 
			return false;

		Entry.AbsoluteOffset = PayloadStart + RelativeOffset;
		Info.AvailableChunkCoords.push_back(Entry.Coord);
	}
	uint64 ExpectedOffset = Ar.Tell();
	for (size_t i = 0; i < Index.size(); ++i)
	{
		const FChunkIndexEntry& Entry = Index[i];
		// 파일 읽을 범위 유효성 검사
		if ((i > 0 && !(Index[i - 1].Coord < Entry.Coord)) ||
			Entry.AbsoluteOffset != ExpectedOffset || Entry.AbsoluteOffset > FileSize ||
			Entry.Size == 0 || Entry.Size > FileSize - Entry.AbsoluteOffset)
		{
			return false;
		}

		ExpectedOffset += Entry.Size;
	}
	if (ExpectedOffset != FileSize) 
		return false;

	AssetPath = InAssetPath;
	return true;
}

const FNavigationAssetCatalog::FChunkIndexEntry* FNavigationAssetCatalog::FindEntry(const FVoxelCoord& Coord) const
{
	auto It = std::lower_bound(Index.begin(), Index.end(), Coord, 
		[](const FChunkIndexEntry& Entry, const FVoxelCoord& Value) { return IsCoordLess(Entry.Coord, Value); });
	return It != Index.end() && 
		It->Coord == Coord ? &*It : nullptr;
}

bool FNavigationAssetCatalog::ReadChunks(const TArray<FVoxelCoord>& RequestedCoords, TArray<FBakedVoxelNavigationChunk>& OutChunks) const
{
	OutChunks.clear();

	if (RequestedCoords.empty()) 
		return true;
	if (!IsOpen()) 
		return false;

	FWindowsBinReader Ar(FPaths::MakeProjectRelative(AssetPath));
	if (!Ar.IsValid()) 
		return false;

	OutChunks.reserve(RequestedCoords.size());
	for (const FVoxelCoord& Coord : RequestedCoords)
	{
		const FChunkIndexEntry* Entry = FindEntry(Coord);
		if (!Entry || !Ar.Seek(Entry->AbsoluteOffset)) 
			return false;

		FBakedVoxelNavigationChunk Chunk;
		Chunk.Coord = Coord;
		if (!ReadChunk(Ar, Chunk) || Ar.Tell() != Entry->AbsoluteOffset + Entry->Size ||
			!ValidateChunkPayloadAgainstCatalog(Chunk, Info.AvailableChunkCoords))
		{
			return false; // 청크 읽기 실패
		}
		OutChunks.push_back(std::move(Chunk));
	}
	return true;
}

bool FNavigationAssetCatalog::ReadAllChunks(TArray<FBakedVoxelNavigationChunk>& OutChunks) const
{
	return ReadChunks(Info.AvailableChunkCoords, OutChunks);
}

bool FNavigationAssetCatalog::ValidateCompleteAsset() const
{
	TArray<FBakedVoxelNavigationChunk> Chunks;
	return ReadAllChunks(Chunks) && ValidateNavigationAssetDataset(Chunks);
}

void FNavigationAssetCatalog::GatherChunkCoordsInRadius(const FVector& Center, float Radius, TArray<FVoxelCoord>& OutCoords) const
{
	OutCoords.clear();
	if (!IsOpen() || Radius <= 0.0f) 
		return;

	const float RadiusSquared = Radius * Radius;
	for (const FVoxelCoord& Coord : Info.AvailableChunkCoords)
	{
		const FVector Delta = GetChunkCenter(Coord) - Center;
		if (Delta.Dot(Delta) <= RadiusSquared) 
			OutCoords.push_back(Coord);
	}
}

FVector FNavigationAssetCatalog::GetChunkCenter(const FVoxelCoord& Coord) const
{
	const FVector BoundsMin = Info.BoundsCenter - Info.BoundsExtent;
	return BoundsMin + FVector((Coord.X + 0.5f) * NavL1ChunkSize, 
							(Coord.Y + 0.5f) * NavL1ChunkSize, 
							(Coord.Z + 0.5f) * NavL1ChunkSize);
}

bool FNavigationAssetCatalog::WriteAsset(const FString& AssetPath, const FString& SourceScenePath, const FVoxelNavigationBakedData& Data)
{
	if (!ValidateNavigationAssetDataset(Data.Chunks)) 
		return false;

	TArray<FBakedVoxelNavigationChunk> Chunks = Data.Chunks;
	std::sort(Chunks.begin(), Chunks.end(), 
		[](const auto& A, const auto& B) { return A.Coord < B.Coord; });

	for (FBakedVoxelNavigationChunk& Chunk : Chunks)
	{
		std::sort(Chunk.IntraEdges.begin(), Chunk.IntraEdges.end());
		std::sort(Chunk.ExternalLinks.begin(), Chunk.ExternalLinks.end());
	}

	const std::filesystem::path OutputPath(FPaths::ToWide(AssetPath));
	std::error_code Error;
	std::filesystem::create_directories(OutputPath.parent_path(), Error);
	if (Error)
	{
		return false;
	}
	
	// 일단 임시 파일에 쓰고 나중에 옮기기(or 덮어쓰기)
	const FString StagingPath = AssetPath + ".tmp";
	const bool bWritten = [&]()
	{
		FWindowsBinWriter Ar(FPaths::MakeProjectRelative(StagingPath));
		if (!Ar.IsValid()) 
			return false;

		FAssetImportMetadata Metadata;
		Metadata.SourcePath = SourceScenePath;
		if (!FAssetPackage::WritePackagePrelude(Ar, EAssetPackageType::VoxelNavigation, Metadata)) 
			return false;

		Ar.SetTaggedPropertySerializationEnabled(false);
		const uint64 PayloadStart = Ar.Tell();
		uint32 Version = FormatVersion, StoredByteOrder = ByteOrder, ChunkCount = static_cast<uint32>(Chunks.size());
		FVoxelNavigationBuildSettings Settings = Data.Settings;
		Ar << Version << StoredByteOrder;
		Ar.Serialize(const_cast<float*>(Data.BoundsCenter.Data), sizeof(Data.BoundsCenter.Data));
		Ar.Serialize(const_cast<float*>(Data.BoundsExtent.Data), sizeof(Data.BoundsExtent.Data));
		Ar << Settings.AgentRadius << Settings.AgentHeight << Settings.MaxWalkableSlopeDegrees << Settings.MaxNeighborHeightDelta << Settings.GroundProbeInset << Settings.ClearanceOffset << ChunkCount;
		if (!Ar.IsValid()) 
			return false;

		const uint64 IndexStart = Ar.Tell();
		for (uint32 i = 0; i < ChunkCount; ++i)
		{
			if (!WriteIndexEntry(Ar, {}, 0, 0))
				return false;
		}

		TArray<FChunkIndexEntry> WrittenIndex;
		for (const FBakedVoxelNavigationChunk& Chunk : Chunks)
		{
			const uint64 Start = Ar.Tell();
			if (!WriteChunk(Ar, Chunk)) 
				return false;

			const uint64 End = Ar.Tell();
			if (End < Start || End - Start > (std::numeric_limits<uint32>::max)()) 
				return false;

			WrittenIndex.push_back({ Chunk.Coord, Start - PayloadStart, static_cast<uint32>(End - Start) });
		}
		const uint64 End = Ar.Tell();
		if (!Ar.Seek(IndexStart)) 
			return false;

		for (const FChunkIndexEntry& Entry : WrittenIndex)
		{
			if (!WriteIndexEntry(Ar, Entry.Coord, Entry.AbsoluteOffset, Entry.Size)) 
				return false;
		}
		return Ar.Seek(End) && Ar.IsValid();
	}();

	if (!bWritten) 
		return false;

	const std::wstring From = FPaths::ToWide(FPaths::MakeProjectRelative(StagingPath));
	const std::wstring To = FPaths::ToWide(FPaths::MakeProjectRelative(AssetPath));
	return ::MoveFileExW(From.c_str(), To.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool FNavigationAssetCatalog::WriteReferenceJson(const FString& Path, const FVoxelNavigationBakedData& Data)
{
	if (!ValidateNavigationAssetDataset(Data.Chunks)) 
		return false;

	using namespace json;
	auto ToJsonCoord = [](const FVoxelCoord& Value) { return Array(Value.X, Value.Y, Value.Z); };
	auto ToJsonVector = [](const FVector& Value) { return Array(Value.X, Value.Y, Value.Z); };
	JSON Root = Object();
	Root["FormatVersion"] = 1;
	Root["Transport"] = "VoxelNavigationReferenceJson";
	Root["BoundsCenter"] = ToJsonVector(Data.BoundsCenter);
	Root["BoundsExtent"] = ToJsonVector(Data.BoundsExtent);
	JSON Settings = Object();
	Settings["AgentRadius"] = Data.Settings.AgentRadius;
	Settings["AgentHeight"] = Data.Settings.AgentHeight;
	Settings["MaxWalkableSlopeDegrees"] = Data.Settings.MaxWalkableSlopeDegrees;
	Settings["MaxNeighborHeightDelta"] = Data.Settings.MaxNeighborHeightDelta;
	Settings["GroundProbeInset"] = Data.Settings.GroundProbeInset;
	Settings["ClearanceOffset"] = Data.Settings.ClearanceOffset;
	Root["Settings"] = Settings;
	JSON Chunks = Array();
	for (const FBakedVoxelNavigationChunk& Chunk : Data.Chunks)
	{
		JSON Item = Object(), Cells = Array(), IntraEdges = Array(), ExternalLinks = Array();
		Item["Coord"] = ToJsonCoord(Chunk.Coord);
		for (uint8 Cell : Chunk.Cells) 
			Cells.append(static_cast<int>(Cell));
		for (const auto& Edge : Chunk.IntraEdges) 
			IntraEdges.append(Array(Edge.PortalA, Edge.PortalB, Edge.Cost));
		for (const auto& Link : Chunk.ExternalLinks) 
			ExternalLinks.append(Array(Link.LocalPortalId, Link.PackedNeighborChunkDelta, Link.NeighborPortalId, Link.Cost));
		Item["Cells"] = Cells; Item["IntraEdges"] = IntraEdges; Item["ExternalLinks"] = ExternalLinks;
		Chunks.append(Item);
	}
	Root["Chunks"] = Chunks;

	const std::filesystem::path OutputPath(FPaths::ToWide(Path));
	std::error_code Error;
	std::filesystem::create_directories(OutputPath.parent_path(), Error);

	std::ofstream File(OutputPath);
	if (!File.is_open()) 
		return false;
	File << Root.dump(2);
	return File.good();
}
