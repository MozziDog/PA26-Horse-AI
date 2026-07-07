#include "RoadGraph.h"

namespace
{
	// 선분 [A,B] 위에서 P 에 가장 가까운 점을 구하고, 0..1 파라미터를 OutAlpha 로 돌려준다.
	// 길이가 0 인 선분은 A 를 반환한다(Alpha = 0).
	FVector ClosestPointOnLineSegment(const FVector& A, const FVector& B, const FVector& P, float& OutAlpha)
	{
		const FVector AB = B - A;
		const float LengthSq = AB.Dot(AB);
		if (LengthSq <= 1.e-8f)
		{
			OutAlpha = 0.0f;
			return A;
		}

		float Alpha = (P - A).Dot(AB) / LengthSq;
		if (Alpha < 0.0f) Alpha = 0.0f;
		else if (Alpha > 1.0f) Alpha = 1.0f;

		OutAlpha = Alpha;
		return A + AB * Alpha;
	}
}

const FRoadNode* FRoadGraph::FindNode(int32 NodeID) const
{
	for (const FRoadNode& Node : Nodes)
	{
		if (Node.ID == NodeID)
		{
			return &Node;
		}
	}
	return nullptr;
}

FRoadNode* FRoadGraph::FindNode(int32 NodeID)
{
	for (FRoadNode& Node : Nodes)
	{
		if (Node.ID == NodeID)
		{
			return &Node;
		}
	}
	return nullptr;
}

bool FRoadGraph::GetEdgePathPoints(const FRoadEdge& Edge, TArray<FVector>& OutPoints) const
{
	OutPoints.clear();

	const FRoadNode* StartNode = FindNode(Edge.StartNodeID);
	const FRoadNode* EndNode = FindNode(Edge.EndNodeID);
	if (!StartNode || !EndNode)
	{
		return false;
	}

	OutPoints.reserve(Edge.ControlPoints.size() + 2);
	OutPoints.push_back(StartNode->Position);
	for (const FVector& ControlPoint : Edge.ControlPoints)
	{
		OutPoints.push_back(ControlPoint);
	}
	OutPoints.push_back(EndNode->Position);
	return true;
}

FRoadNodeQueryResult FRoadGraph::FindNearestNode(const FVector& Point) const
{
	FRoadNodeQueryResult Result;

	for (int32 Index = 0; Index < static_cast<int32>(Nodes.size()); ++Index)
	{
		const FRoadNode& Node = Nodes[Index];
		const float DistanceSq = FVector::DistSquared(Point, Node.Position);
		if (!Result.bValid || DistanceSq < Result.DistanceSq)
		{
			Result.bValid = true;
			Result.NodeID = Node.ID;
			Result.NodeIndex = Index;
			Result.Position = Node.Position;
			Result.DistanceSq = DistanceSq;
		}
	}

	return Result;
}

FRoadEdgeQueryResult FRoadGraph::FindClosestPointOnEdge(const FRoadEdge& Edge, const FVector& Point) const
{
	FRoadEdgeQueryResult Result;

	TArray<FVector> PathPoints;
	if (!GetEdgePathPoints(Edge, PathPoints) || PathPoints.size() < 2)
	{
		return Result;
	}

	for (int32 SpanIndex = 0; SpanIndex + 1 < static_cast<int32>(PathPoints.size()); ++SpanIndex)
	{
		const FVector& A = PathPoints[SpanIndex];
		const FVector& B = PathPoints[SpanIndex + 1];
		float Alpha = 0.0f;
		const FVector Closest = ClosestPointOnLineSegment(A, B, Point, Alpha);
		const float DistanceSq = FVector::DistSquared(Point, Closest);
		if (!Result.bValid || DistanceSq < Result.DistanceSq)
		{
			const FVector Span = B - A;
			Result.bValid = true;
			Result.EdgeID = Edge.ID;
			Result.SpanIndex = SpanIndex;
			Result.SpanAlpha = Alpha;
			Result.Position = Closest;
			Result.Direction = Span.IsNearlyZero() ? FVector::ZeroVector : Span.Normalized();
			Result.DistanceSq = DistanceSq;
		}
	}

	return Result;
}

FRoadEdgeQueryResult FRoadGraph::FindClosestPoint(const FVector& Point) const
{
	FRoadEdgeQueryResult Result;

	TArray<FVector> PathPoints;
	for (int32 EdgeIndex = 0; EdgeIndex < static_cast<int32>(Edges.size()); ++EdgeIndex)
	{
		const FRoadEdge& Edge = Edges[EdgeIndex];
		if (!GetEdgePathPoints(Edge, PathPoints) || PathPoints.size() < 2)
		{
			continue;
		}

		// 제어점 사이 각 구간 [P[i], P[i+1]] 에 대해 최근접 점을 구하고 전체 최소를 유지한다.
		for (int32 SpanIndex = 0; SpanIndex + 1 < static_cast<int32>(PathPoints.size()); ++SpanIndex)
		{
			const FVector& A = PathPoints[SpanIndex];
			const FVector& B = PathPoints[SpanIndex + 1];
			float Alpha = 0.0f;
			const FVector Closest = ClosestPointOnLineSegment(A, B, Point, Alpha);
			const float DistanceSq = FVector::DistSquared(Point, Closest);
			if (!Result.bValid || DistanceSq < Result.DistanceSq)
			{
				const FVector Span = B - A;
				Result.bValid = true;
				Result.EdgeID = Edge.ID;
				Result.EdgeIndex = EdgeIndex;
				Result.SpanIndex = SpanIndex;
				Result.SpanAlpha = Alpha;
				Result.Position = Closest;
				Result.Direction = Span.IsNearlyZero() ? FVector::ZeroVector : Span.Normalized();
				Result.DistanceSq = DistanceSq;
			}
		}
	}

	return Result;
}
