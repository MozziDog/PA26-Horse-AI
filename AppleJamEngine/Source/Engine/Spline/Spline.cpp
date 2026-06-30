#include "Spline.h"
#include <algorithm>

bool MapGraph::AddNode(SplineNode InNode)
{
	auto it = FindNode(InNode.GetID());
	if(it == Nodes.end())
		return false;

	Nodes.insert(it, InNode);
	return true;
}

bool MapGraph::RemoveNode(int InID)
{
	auto it = FindNode(InID);
	if (it == Nodes.end())
		return false;
	if (!it->GetConnectedSegments().empty())
		return false;

	Nodes.erase(it);
	return true;
}

TArray<SplineNode>::iterator MapGraph::FindNode(int InID)
{
	SplineNode ToFind = { InID, FVector::ZeroVector };
	auto it = std::lower_bound(Nodes.begin(), Nodes.end(), ToFind);
	if (it->GetID() == InID)
		return it;
	else
		return Nodes.end();
}

bool MapGraph::AddSegment(SplineSegment InSegment)
{
	auto it = FindSegment(InSegment.GetID());
	if (it != Segments.end())
		return false;

	auto startNode = FindNode(InSegment.GetStartNodeID());
	if (startNode == Nodes.end())
		return false;

	auto endNode = FindNode(InSegment.GetEndNodeID());
	if (endNode == Nodes.end())
		return false;

	startNode->AddConnectedSegment(InSegment.GetID());
	endNode->AddConnectedSegment(InSegment.GetID());
	Segments.insert(it, InSegment);
	return true;
}

bool MapGraph::RemoveSegment(int InID)
{
	auto segmentIt = FindSegment(InID);
	if (segmentIt == Segments.end())
		return false;

	auto startNodeIt = FindNode(segmentIt->GetStartNodeID());
	if (startNodeIt == Nodes.end())
		return false;

	auto endNodeIt = FindNode(segmentIt->GetEndNodeID());
	if (endNodeIt == Nodes.end())
		return false;

	startNodeIt->RemoveConnectedSegment(InID);
	endNodeIt->RemoveConnectedSegment(InID);
	Segments.erase(segmentIt);
	return true;
}

TArray<SplineSegment>::iterator MapGraph::FindSegment(int InID)
{
	SplineSegment ToFind = { InID, 0, 0 };
	auto it = std::lower_bound(Segments.begin(), Segments.end(), ToFind);
	if (it->GetID() == InID)
		return it;
	else
		return Segments.end();
}
