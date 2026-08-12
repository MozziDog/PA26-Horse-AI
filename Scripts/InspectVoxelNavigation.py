#!/usr/bin/env python3
"""Standalone validator/dumper for VoxelNavigation .uasset files.

This intentionally knows only the documented package prelude and navigation
payload layout.  It does not import or invoke any AppleJamEngine code.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


ASSET_MAGIC = 0x54455341
ASSET_TYPE_VOXEL_NAVIGATION = 17
NAVIGATION_FORMAT_VERSION = 1
BYTE_ORDER_MARKER = 0x01020304
CELL_COUNT = 100
MAX_CHUNKS = 1 << 20
MAX_EDGES_PER_CHUNK = 1 << 20


class FormatError(ValueError):
    pass


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, size: int) -> bytes:
        if size < 0 or self.pos + size > len(self.data):
            raise FormatError("truncated payload")
        value = self.data[self.pos:self.pos + size]
        self.pos += size
        return value

    def unpack(self, fmt: str):
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(size))

    def u32(self) -> int:
        return self.unpack("<I")[0]

    def string(self) -> str:
        size = self.u32()
        try:
            return self.take(size).decode("utf-8")
        except UnicodeDecodeError as error:
            raise FormatError(f"metadata string is not UTF-8: {error}") from error


@dataclass(frozen=True)
class IndexEntry:
    coord: tuple[int, int, int]
    offset: int
    size: int


def finite_non_negative(value: float, label: str) -> None:
    if not math.isfinite(value) or value < 0.0:
        raise FormatError(f"{label} must be finite and non-negative")


def read_asset(path: Path) -> dict:
    reader = Reader(path.read_bytes())
    magic, package_version, package_type = reader.unpack("<III")
    if magic != ASSET_MAGIC:
        raise FormatError("asset package magic mismatch")
    if package_version != 1 and not 2 <= package_version <= 7:
        raise FormatError(f"unsupported package version {package_version}")
    if package_type != ASSET_TYPE_VOXEL_NAVIGATION:
        raise FormatError(f"asset type is {package_type}, not VoxelNavigation")

    metadata = {
        "SourcePath": reader.string(),
        "SourceTimestamp": reader.unpack("<Q")[0],
        "SourceFileSize": reader.unpack("<Q")[0],
    }
    payload_start = reader.pos
    format_version, byte_order = reader.unpack("<II")
    if format_version != NAVIGATION_FORMAT_VERSION:
        raise FormatError(f"unsupported navigation format version {format_version}")
    if byte_order != BYTE_ORDER_MARKER:
        raise FormatError("byte-order marker mismatch")
    bounds_center = list(reader.unpack("<3f"))
    bounds_extent = list(reader.unpack("<3f"))
    settings_values = reader.unpack("<6f")
    if not all(math.isfinite(value) for value in bounds_center + bounds_extent):
        raise FormatError("bounds contain NaN or infinity")
    if any(value <= 0.0 for value in bounds_extent):
        raise FormatError("bounds extent must be positive")
    settings = dict(zip((
        "AgentRadius", "AgentHeight", "MaxWalkableSlopeDegrees",
        "MaxNeighborHeightDelta", "GroundProbeInset", "ClearanceOffset",
    ), settings_values, strict=True))
    if not all(math.isfinite(value) for value in settings.values()):
        raise FormatError("settings contain NaN or infinity")
    if settings["AgentRadius"] <= 0.0 or settings["AgentHeight"] <= 0.0:
        raise FormatError("agent dimensions must be positive")

    chunk_count = reader.u32()
    if not 0 < chunk_count <= MAX_CHUNKS:
        raise FormatError(f"invalid chunk count {chunk_count}")
    index = [IndexEntry(reader.unpack("<3i"), reader.unpack("<Q")[0], reader.u32()) for _ in range(chunk_count)]
    payload_data_start = reader.pos
    expected_offset = payload_data_start - payload_start
    previous_coord: tuple[int, int, int] | None = None
    for entry in index:
        if previous_coord is not None and entry.coord <= previous_coord:
            raise FormatError("chunk index coordinates are not strictly sorted")
        if entry.offset != expected_offset or entry.size == 0:
            raise FormatError("chunk index has a gap, overlap, or zero-size payload")
        expected_offset += entry.size
        if payload_start + expected_offset > len(reader.data):
            raise FormatError("chunk index exceeds file bounds")
        previous_coord = entry.coord
    if payload_start + expected_offset != len(reader.data):
        raise FormatError("trailing data or unindexed payload bytes")

    chunks = []
    by_coord = {}
    for entry in index:
        chunk_reader = Reader(reader.data[payload_start + entry.offset:payload_start + entry.offset + entry.size])
        cells = list(chunk_reader.take(CELL_COUNT))
        intra_count = chunk_reader.u32()
        if intra_count > MAX_EDGES_PER_CHUNK:
            raise FormatError("intra-edge count exceeds limit")
        intra_edges = []
        previous_edge = None
        for _ in range(intra_count):
            portal_a, portal_b, cost = chunk_reader.unpack("<BBf")
            if not (portal_a < portal_b < CELL_COUNT) or cells[portal_a] == 0 or cells[portal_b] == 0:
                raise FormatError("invalid intra-edge endpoint")
            finite_non_negative(cost, "intra-edge cost")
            edge_key = (portal_a, portal_b)
            if previous_edge == edge_key:
                raise FormatError("duplicate intra-edge")
            previous_edge = edge_key
            intra_edges.append([portal_a, portal_b, cost])
        external_count = chunk_reader.u32()
        if external_count > MAX_EDGES_PER_CHUNK:
            raise FormatError("external-link count exceeds limit")
        external_links = []
        for _ in range(external_count):
            local_portal, packed_delta, neighbor_portal, cost = chunk_reader.unpack("<BBBf")
            if local_portal >= CELL_COUNT or neighbor_portal >= CELL_COUNT or cells[local_portal] == 0:
                raise FormatError("invalid external-link endpoint")
            if packed_delta & 0xC0:
                raise FormatError("external-link uses reserved delta bits")
            xyz = (packed_delta & 3, (packed_delta >> 2) & 3, (packed_delta >> 4) & 3)
            if 3 in xyz or xyz == (1, 1, 1):
                raise FormatError("invalid external-link chunk delta")
            finite_non_negative(cost, "external-link cost")
            external_links.append([local_portal, packed_delta, neighbor_portal, cost])
        if chunk_reader.pos != entry.size:
            raise FormatError("chunk payload size does not match its index")
        chunk = {"Coord": list(entry.coord), "Cells": cells, "IntraEdges": intra_edges, "ExternalLinks": external_links}
        chunks.append(chunk)
        by_coord[entry.coord] = chunk

    for chunk in chunks:
        coord = tuple(chunk["Coord"])
        for local_portal, packed_delta, neighbor_portal, cost in chunk["ExternalLinks"]:
            delta = ((packed_delta & 3) - 1, ((packed_delta >> 2) & 3) - 1, ((packed_delta >> 4) & 3) - 1)
            neighbor_coord = tuple(coord[index] + delta[index] for index in range(3))
            neighbor = by_coord.get(neighbor_coord)
            if neighbor is None or neighbor["Cells"][neighbor_portal] == 0:
                raise FormatError("external-link references a missing portal or chunk")
            reverse_delta = ((-delta[0] + 1) | ((-delta[1] + 1) << 2) | ((-delta[2] + 1) << 4))
            if not any(link[0] == neighbor_portal and link[1] == reverse_delta and link[2] == local_portal and abs(link[3] - cost) <= 1.0e-4
                       for link in neighbor["ExternalLinks"]):
                raise FormatError("external-link has no exact reciprocal descriptor")

    return {
        "FormatVersion": format_version,
        "Transport": "VoxelNavigationBinary",
        "PackageVersion": package_version,
        "Metadata": metadata,
        "BoundsCenter": bounds_center,
        "BoundsExtent": bounds_extent,
        "Settings": settings,
        "Chunks": chunks,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate or dump a VoxelNavigation .uasset.")
    parser.add_argument("asset", type=Path)
    parser.add_argument("--validate", action="store_true", help="validate and print a concise result")
    parser.add_argument("--dump", type=Path, help="write canonical JSON")
    parser.add_argument("--chunk", help="print one chunk as X,Y,Z")
    args = parser.parse_args()
    try:
        document = read_asset(args.asset)
        if args.dump:
            args.dump.write_text(json.dumps(document, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
        if args.chunk:
            coord = tuple(int(value) for value in args.chunk.split(","))
            if len(coord) != 3:
                raise FormatError("--chunk requires X,Y,Z")
            chunk = next((item for item in document["Chunks"] if tuple(item["Coord"]) == coord), None)
            if chunk is None:
                raise FormatError(f"chunk {args.chunk} is not present")
            print(json.dumps(chunk, indent=2, ensure_ascii=False, allow_nan=False))
        if args.validate:
            print(f"valid: chunks={len(document['Chunks'])} format={document['FormatVersion']}")
        if not args.validate and not args.dump and not args.chunk:
            print(json.dumps(document, indent=2, ensure_ascii=False, allow_nan=False))
        return 0
    except (OSError, FormatError, ValueError) as error:
        print(f"invalid: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
