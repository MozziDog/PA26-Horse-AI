#include "RoadGraphGizmoTarget.h"

#include "Component/AI/RoadGraphComponent.h"

void FRoadGraphGizmoTarget::SetNode(URoadGraphComponent* InComponent, int32 InNodeIndex)
{
	Component = InComponent;
	Kind = ERoadGizmoTargetKind::Node;
	NodeIndex = InNodeIndex;
	EdgeIndex = -1;
	ControlPointIndex = -1;
}

void FRoadGraphGizmoTarget::SetControlPoint(URoadGraphComponent* InComponent, int32 InEdgeIndex, int32 InControlPointIndex)
{
	Component = InComponent;
	Kind = ERoadGizmoTargetKind::ControlPoint;
	NodeIndex = -1;
	EdgeIndex = InEdgeIndex;
	ControlPointIndex = InControlPointIndex;
}

void FRoadGraphGizmoTarget::Clear()
{
	Component.Reset();
	NodeIndex = -1;
	EdgeIndex = -1;
	ControlPointIndex = -1;
}

FVector* FRoadGraphGizmoTarget::ResolvePosition() const
{
	URoadGraphComponent* Comp = Component.Get();
	if (!Comp)
	{
		return nullptr;
	}

	FRoadGraph& Graph = Comp->GetRoadGraphMutable();

	if (Kind == ERoadGizmoTargetKind::Node)
	{
		if (NodeIndex >= 0 && NodeIndex < static_cast<int32>(Graph.Nodes.size()))
		{
			return &Graph.Nodes[NodeIndex].Position;
		}
		return nullptr;
	}

	if (EdgeIndex >= 0 && EdgeIndex < static_cast<int32>(Graph.Edges.size()))
	{
		TArray<FVector>& ControlPoints = Graph.Edges[EdgeIndex].ControlPoints;
		if (ControlPointIndex >= 0 && ControlPointIndex < static_cast<int32>(ControlPoints.size()))
		{
			return &ControlPoints[ControlPointIndex];
		}
	}
	return nullptr;
}

bool FRoadGraphGizmoTarget::IsValid() const
{
	return ResolvePosition() != nullptr;
}

UWorld* FRoadGraphGizmoTarget::GetWorld() const
{
	URoadGraphComponent* Comp = Component.Get();
	return Comp ? Comp->GetWorld() : nullptr;
}

FVector FRoadGraphGizmoTarget::GetWorldLocation() const
{
	const FVector* Position = ResolvePosition();
	return Position ? *Position : FVector::ZeroVector;
}

FRotator FRoadGraphGizmoTarget::GetWorldRotation() const
{
	return FRotator::ZeroRotator;
}

FQuat FRoadGraphGizmoTarget::GetWorldQuat() const
{
	return FQuat::Identity;
}

FVector FRoadGraphGizmoTarget::GetWorldScale() const
{
	return FVector(1.0f, 1.0f, 1.0f);
}

void FRoadGraphGizmoTarget::SetWorldLocation(const FVector& NewLocation)
{
	if (FVector* Position = ResolvePosition())
	{
		*Position = NewLocation;
		if (URoadGraphComponent* Comp = Component.Get())
		{
			Comp->PostEditProperty("RoadGraph");
		}
	}
}

void FRoadGraphGizmoTarget::AddWorldOffset(const FVector& Delta)
{
	if (FVector* Position = ResolvePosition())
	{
		*Position = *Position + Delta;
		if (URoadGraphComponent* Comp = Component.Get())
		{
			Comp->PostEditProperty("RoadGraph");
		}
	}
}
