#include "Spline.h"
#include <algorithm>

bool MapGraph::AddNode(SplineNode InNode)
{
	auto it = std::lower_bound(Nodes.begin(), Nodes.end(), InNode);
	if(it != Nodes.end() && it->GetID() == InNode.GetID())
		return false;

	Nodes.insert(it, InNode);
	return true;
}

bool MapGraph::RemoveNode(int InID)
{
	auto it = std::lower_bound(
		Nodes.begin(),
		Nodes.end(),
		[InID](SplineNode node) { return node.GetID() < InID; }
	);
	if (it == Nodes.end() || it->GetID() != InID)
		return false;
	if (!it->GetConnectedSegments().empty())
		return false;

	Nodes.erase(it);
	return true;
}

bool MapGraph::AddSegment(SplineSegment InSegment)
{
	auto it = std::lower_bound(Segments.begin(), Segments.end(), InSegment);
	if (it != Segments.end() && it->GetID() == InSegment.GetID())
		return false;

	auto startNode = std::lower_bound(Nodes.begin(), Nodes.end(), InSegment.GetStartNodeID());
	if (startNode == Nodes.end() || startNode->GetID() != InSegment.GetStartNodeID())
		return false;

	auto endNode = std::lower_bound(Nodes.begin(), Nodes.end(), InSegment.GetEndNodeID());
	if (endNode == Nodes.end() || endNode->GetID() != InSegment.GetEndNodeID())
		return false;

	startNode->AddConnectedSegment(InSegment.GetID());
	endNode->AddConnectedSegment(InSegment.GetID());
	Segments.insert(it, InSegment);
}

bool MapGraph::RemoveSegment(int InID)
{
	auto segmentIt = std::lower_bound(
		Segments.begin(),
		Segments.end(),
		[InID](SplineSegment segment) { return segment.GetID() < InID; }
	);
	if (segmentIt == Segments.end() || segmentIt->GetID() != InID)
		return false;

	int startNodeID = segmentIt->GetStartNodeID();
	auto startNodeIt = find(Nodes.begin(), Nodes.end(), 
		[startNodeID](SplineNode node) { node.GetID() == startNodeID; }
	);
	if (startNodeIt == Nodes.end())
		return false;

	int endNodeID = segmentIt->GetEndNodeID();
	auto endNodeIt = find(Nodes.begin(), Nodes.end(),
		[endNodeID](SplineNode node) { node.GetID() == endNodeID; }
	);
	if (endNodeIt == Nodes.end())
		return false;

	startNodeIt->RemoveConnectedSegment(InID);
	endNodeIt->RemoveConnectedSegment(InID);
	Segments.erase(segmentIt);
}