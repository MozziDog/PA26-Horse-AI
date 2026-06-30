#pragma once
#include "Math/Vector.h"

class SplineNode
{
public:
	SplineNode(int ID, FVector InPosition)
		: Position(InPosition) { }

	int GetID() { return ID; }

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

private:
	int ID;
	FVector Position;
	TArray<int> ConnectedSegments;
};

class SplineSegment
{
public:
	SplineSegment(int InStartNodeID, int InEndNodeID) 
		: startNodeID(InStartNodeID), endNodeID(InEndNodeID) {}

	int GetID() { return ID; }
	int GetStartNodeID() { return startNodeID; }
	int GetEndNodeID() { return endNodeID; }

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
	
	bool AddSegment(SplineSegment InSegment);
	bool RemoveSegment(int InID);
private:
	TArray<SplineNode> Nodes;
	TArray<SplineSegment> Segments;
};