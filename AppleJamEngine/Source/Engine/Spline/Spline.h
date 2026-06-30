#pragma once
#include "Math/Vector.h"

class SplineNode
{
public:
	SplineNode(int ID, FVector InPosition)
		: Position(InPosition) { }

	int GetID() const { return ID; }

	FVector GetPosition() { return Position; }
	void SetPosition(FVector InPosition) { Position = InPosition; }

	void AddConnectedSegment(int ID) 
	{ 
		ConnectedSegments.push_back(ID); 
	}
	void RemoveConnectedSegment(int ID) 
	{ 
		ConnectedSegments.erase(std::find(ConnectedSegments.begin(), ConnectedSegments.end(), ID)); 
	}

	const TArray<int>& GetConnectedSegments() { return ConnectedSegments; }

	bool operator<(const SplineNode& other) const { return ID < other.GetID(); }

private:
	int ID;
	FVector Position;
	TArray<int> ConnectedSegments;
};

class SplineSegment
{
public:
	SplineSegment(int InSegmentID, int InStartNodeID, int InEndNodeID) 
		: ID(InSegmentID), startNodeID(InStartNodeID), endNodeID(InEndNodeID) {}

	int GetID() const { return ID; }
	int GetStartNodeID() { return startNodeID; }
	int GetEndNodeID() { return endNodeID; }

	bool operator<(const SplineSegment& other) const { return ID < other.GetID(); }

private:
	int ID;
	int startNodeID;
	int endNodeID;
	float width;
	// TODO: Add control points to make curvy spline
};

class MapGraph
{
public:
	bool AddNode(SplineNode InNode);
	bool RemoveNode(int InID);
	TArray<SplineNode>::iterator FindNode(int InID);
	
	bool AddSegment(SplineSegment InSegment);
	bool RemoveSegment(int InID);
	TArray<SplineSegment>::iterator FindSegment(int InID);
private:
	TArray<SplineNode> Nodes;
	TArray<SplineSegment> Segments;
};

// 비교 연산자 특수화
namespace std {
	template <>
	struct less<SplineNode> {
		bool operator()(const SplineNode& lhs, const SplineNode& rhs) const {
			return lhs.GetID() < rhs.GetID();
		}
	};

	template <>
	struct less<SplineSegment> {
		bool operator()(const SplineSegment& lhs, const SplineSegment& rhs) const {
			return lhs.GetID() < rhs.GetID();
		}
	};
}