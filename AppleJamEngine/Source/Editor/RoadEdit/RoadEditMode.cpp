#include "RoadEditMode.h"

#include "Component/AI/RoadGraphComponent.h"
#include "Component/Debug/GizmoComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Core/Types/RayTypes.h"
#include "Core/Types/CollisionTypes.h"
#include "Core/Logging/Log.h"
#include "Debug/DrawDebugHelpers.h"

#include <imgui.h>

namespace
{
	constexpr float NodePickRadius = 0.6f;
	constexpr float ControlPointPickRadius = 0.4f;
	constexpr float EdgePickRadius = 0.4f;
	constexpr float NodeAddForwardDistance = 10.0f;

	FVector NormalizedOrZero(const FVector& V)
	{
		return V.IsNearlyZero() ? FVector::ZeroVector : V.Normalized();
	}

	FVector CameraForwardPoint(const FRay& Ray, float Distance)
	{
		return Ray.Origin + NormalizedOrZero(Ray.Direction) * Distance;
	}

	// 광선-평면 교차 (앞쪽만). 평행이면 false.
	bool IntersectPlane(const FRay& Ray, const FVector& PlanePoint, const FVector& PlaneNormal, FVector& OutPos)
	{
		const FVector Dir = NormalizedOrZero(Ray.Direction);
		const float Denom = Dir.Dot(PlaneNormal);
		const float AbsDenom = Denom < 0.0f ? -Denom : Denom;
		if (AbsDenom < 1.e-6f)
		{
			return false;
		}

		const float T = (PlanePoint - Ray.Origin).Dot(PlaneNormal) / Denom;
		if (T < 0.0f)
		{
			return false;
		}

		OutPos = Ray.Origin + Dir * T;
		return true;
	}

	int32 NextNodeID(const FRoadGraph& Graph)
	{
		int32 MaxID = -1;
		for (const FRoadNode& Node : Graph.Nodes)
		{
			if (Node.ID > MaxID) MaxID = Node.ID;
		}
		return MaxID + 1;
	}

	int32 NextEdgeID(const FRoadGraph& Graph)
	{
		int32 MaxID = -1;
		for (const FRoadEdge& Edge : Graph.Edges)
		{
			if (Edge.ID > MaxID) MaxID = Edge.ID;
		}
		return MaxID + 1;
	}

	// 정규화된 Ray에 대해 구와 교차하면 true, 광선 파라미터(카메라로부터 거리)를 OutT로 반환.
	bool RayHitsSphere(const FRay& Ray, const FVector& Center, float Radius, float& OutT)
	{
		const FVector ToCenter = Center - Ray.Origin;
		const float T = ToCenter.Dot(Ray.Direction);
		if (T < 0.0f)
		{
			return false;
		}

		const FVector Closest = Ray.Origin + Ray.Direction * T;
		if (FVector::DistSquared(Center, Closest) > Radius * Radius)
		{
			return false;
		}

		OutT = T;
		return true;
	}

	// Ray(앞쪽 반직선)와 선분 [A,B] 사이 최근접 거리가 Threshold 이내면 true.
	// 클램프는 단일 패스 근사(프로토타입 픽에 충분). OutT = 광선 파라미터.
	bool RayHitsEdge(const FRay& Ray, const FVector& A, const FVector& B, float Threshold, float& OutT)
	{
		const FVector U = Ray.Direction;
		const FVector V = B - A;
		const FVector W0 = Ray.Origin - A;
		const float a = U.Dot(U);
		const float b = U.Dot(V);
		const float c = V.Dot(V);
		const float d = U.Dot(W0);
		const float e = V.Dot(W0);
		const float Denom = a * c - b * b;

		float RayT = 0.0f;
		float SegS = 0.0f;
		if (Denom < 1.e-8f)
		{
			RayT = 0.0f;
			SegS = (c > 1.e-8f) ? (e / c) : 0.0f;
		}
		else
		{
			RayT = (b * e - c * d) / Denom;
			SegS = (a * e - b * d) / Denom;
		}

		if (RayT < 0.0f) RayT = 0.0f;
		if (SegS < 0.0f) SegS = 0.0f;
		else if (SegS > 1.0f) SegS = 1.0f;

		const FVector PointOnRay = Ray.Origin + U * RayT;
		const FVector PointOnEdge = A + V * SegS;
		if (FVector::DistSquared(PointOnRay, PointOnEdge) > Threshold * Threshold)
		{
			return false;
		}

		OutT = RayT;
		return true;
	}
}

void FRoadEditMode::Enter(UWorld* World, UGizmoComponent* InGizmo)
{
	Gizmo = InGizmo;
	BoundWorld = World;
	RoadComponent = ResolveRoadComponent(World);

	if (!RoadComponent.Get())
	{
		UE_LOG("RoadEditMode: no URoadGraphComponent found in the world.");
	}
}

void FRoadEditMode::Exit()
{
	ClearSelection();
	bPendingNodeDeleteConfirm = false;
	bNeedOpenDeletePopup = false;
	PendingDeleteNodeIndex = -1;
	PendingDeleteEdgeCount = 0;
	RoadComponent.Reset();
	BoundWorld.Reset();
	Gizmo = nullptr;
}

bool FRoadEditMode::IsBoundToWorld(UWorld* World) const
{
	return BoundWorld.Get() == World;
}

void FRoadEditMode::Tick()
{
	if (!Gizmo)
	{
		return;
	}

	// gizmo가 붙는 선택(노드/제어점)만 매 프레임 갱신. 세그먼트는 gizmo가 없다.
	if (Selection == ERoadEditSelection::Node || Selection == ERoadEditSelection::ControlPoint)
	{
		if (!GizmoTarget.IsValid())
		{
			ClearSelection();
			return;
		}
		Gizmo->UpdateGizmoTransform();
	}
}

bool FRoadEditMode::PickElementAtRay(const FRay& Ray, ERoadEditSelection& OutKind, int32& OutNodeIndex, int32& OutEdgeIndex, int32& OutControlPointIndex) const
{
	OutKind = ERoadEditSelection::None;
	OutNodeIndex = -1;
	OutEdgeIndex = -1;
	OutControlPointIndex = -1;

	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return false;
	}

	const FRoadGraph& Graph = Comp->GetRoadGraph();

	// 1) 노드 우선.
	const int32 BestNode = PickNodeAtRay(Ray);
	if (BestNode != -1)
	{
		OutKind = ERoadEditSelection::Node;
		OutNodeIndex = BestNode;
		return true;
	}

	// 2) 제어점.
	int32 BestEdge = -1;
	int32 BestControlPoint = -1;
	float BestControlPointT = 0.0f;
	for (int32 EdgeIndex = 0; EdgeIndex < static_cast<int32>(Graph.Edges.size()); ++EdgeIndex)
	{
		const TArray<FVector>& ControlPoints = Graph.Edges[EdgeIndex].ControlPoints;
		for (int32 CpIndex = 0; CpIndex < static_cast<int32>(ControlPoints.size()); ++CpIndex)
		{
			float T = 0.0f;
			if (RayHitsSphere(Ray, ControlPoints[CpIndex], ControlPointPickRadius, T))
			{
				if (BestEdge == -1 || T < BestControlPointT)
				{
					BestEdge = EdgeIndex;
					BestControlPoint = CpIndex;
					BestControlPointT = T;
				}
			}
		}
	}
	if (BestEdge != -1)
	{
		OutKind = ERoadEditSelection::ControlPoint;
		OutEdgeIndex = BestEdge;
		OutControlPointIndex = BestControlPoint;
		return true;
	}

	// 3) 엣지
	int32 BestEdgeLine = -1;
	float BestEdgeT = 0.0f;
	TArray<FVector> PathPoints;
	for (int32 EdgeIndex = 0; EdgeIndex < static_cast<int32>(Graph.Edges.size()); ++EdgeIndex)
	{
		if (!Graph.GetEdgePathPoints(Graph.Edges[EdgeIndex], PathPoints) || PathPoints.size() < 2)
		{
			continue;
		}

		for (int32 i = 0; i + 1 < static_cast<int32>(PathPoints.size()); ++i)
		{
			float T = 0.0f;
			if (RayHitsEdge(Ray, PathPoints[i], PathPoints[i + 1], EdgePickRadius, T))
			{
				if (BestEdgeLine == -1 || T < BestEdgeT)
				{
					BestEdgeLine = EdgeIndex;
					BestEdgeT = T;
				}
			}
		}
	}
	if (BestEdgeLine != -1)
	{
		OutKind = ERoadEditSelection::Edge;
		OutEdgeIndex = BestEdgeLine;
		return true;
	}

	return false;
}

void FRoadEditMode::PickAtRay(const FRay& Ray)
{
	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp || !Gizmo)
	{
		return;
	}

	// 연결 대기 중이면 노드 클릭으로 세그먼트를 완성한다.
	if (bConnecting)
	{
		const int32 HitNode = PickNodeAtRay(Ray);
		if (HitNode != -1 && HitNode != ConnectFromNodeIndex)
		{
			CreateEdge(ConnectFromNodeIndex, HitNode);
			bConnecting = false;
			ConnectFromNodeIndex = -1;
			return;
		}
		if (HitNode == ConnectFromNodeIndex)
		{
			// 같은 노드 클릭은 무시(연결 유지).
			return;
		}
		// 빈 공간/다른 요소 클릭 → 연결 취소 후 일반 선택으로 진행.
		bConnecting = false;
		ConnectFromNodeIndex = -1;
	}

	ERoadEditSelection Kind = ERoadEditSelection::None;
	int32 NodeIndex = -1;
	int32 EdgeIndex = -1;
	int32 ControlPointIndex = -1;
	if (!PickElementAtRay(Ray, Kind, NodeIndex, EdgeIndex, ControlPointIndex))
	{
		ClearSelection();
		return;
	}

	switch (Kind)
	{
	case ERoadEditSelection::Node:
		SelectNode(NodeIndex);
		break;
	case ERoadEditSelection::ControlPoint:
		SelectControlPoint(EdgeIndex, ControlPointIndex);
		break;
	case ERoadEditSelection::Edge:
		SelectEdge(EdgeIndex);
		break;
	default:
		ClearSelection();
		break;
	}
}

void FRoadEditMode::UpdateHover(const FRay& Ray)
{
	PickElementAtRay(Ray, HoverKind, HoverNodeIndex, HoverEdgeIndex, HoverControlPointIndex);
}

void FRoadEditMode::ClearSelection()
{
	Selection = ERoadEditSelection::None;
	SelectedNodeIndex = -1;
	SelectedEdgeIndex = -1;
	SelectedControlPointIndex = -1;
	bConnecting = false;
	ConnectFromNodeIndex = -1;
	GizmoTarget.Clear();

	if (Gizmo)
	{
		Gizmo->SetTarget(static_cast<IGizmoTransformTarget*>(nullptr));
	}
}

URoadGraphComponent* FRoadEditMode::ResolveRoadComponent(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (!Actor)
		{
			continue;
		}

		if (URoadGraphComponent* Comp = Actor->GetComponentByClass<URoadGraphComponent>())
		{
			return Comp;
		}
	}

	return nullptr;
}

void FRoadEditMode::SelectNode(int32 NodeIndex)
{
	Selection = ERoadEditSelection::Node;
	SelectedNodeIndex = NodeIndex;
	SelectedEdgeIndex = -1;
	SelectedControlPointIndex = -1;

	GizmoTarget.SetNode(RoadComponent.Get(), NodeIndex);
	ApplyGizmoTarget();
}

void FRoadEditMode::SelectControlPoint(int32 EdgeIndex, int32 ControlPointIndex)
{
	Selection = ERoadEditSelection::ControlPoint;
	SelectedNodeIndex = -1;
	SelectedEdgeIndex = EdgeIndex;
	SelectedControlPointIndex = ControlPointIndex;

	GizmoTarget.SetControlPoint(RoadComponent.Get(), EdgeIndex, ControlPointIndex);
	ApplyGizmoTarget();
}

void FRoadEditMode::SelectEdge(int32 EdgeIndex)
{
	Selection = ERoadEditSelection::Edge;
	SelectedNodeIndex = -1;
	SelectedEdgeIndex = EdgeIndex;
	SelectedControlPointIndex = -1;

	// 세그먼트는 gizmo 없음.
	GizmoTarget.Clear();
	if (Gizmo)
	{
		Gizmo->SetTarget(static_cast<IGizmoTransformTarget*>(nullptr));
	}
}

void FRoadEditMode::ApplyGizmoTarget()
{
	if (!Gizmo)
	{
		return;
	}

	// 노드 이동이 다중 선택 액터를 건드리지 않도록 gizmo의 액터 목록을 비운다.
	Gizmo->SetSelectedActors(nullptr);
	Gizmo->UpdateGizmoMode(EGizmoMode::Translate);
	Gizmo->SetTarget(&GizmoTarget);
}

int32 FRoadEditMode::PickNodeAtRay(const FRay& Ray) const
{
	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return -1;
	}

	const FRoadGraph& Graph = Comp->GetRoadGraph();
	int32 Best = -1;
	float BestT = 0.0f;
	for (int32 Index = 0; Index < static_cast<int32>(Graph.Nodes.size()); ++Index)
	{
		float T = 0.0f;
		if (RayHitsSphere(Ray, Graph.Nodes[Index].Position, NodePickRadius, T))
		{
			if (Best == -1 || T < BestT)
			{
				Best = Index;
				BestT = T;
			}
		}
	}
	return Best;
}

bool FRoadEditMode::RaycastGround(const FRay& Ray, FVector& OutPos) const
{
	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return false;
	}

	UWorld* World = Comp->GetWorld();
	if (!World)
	{
		return false;
	}

	FHitResult Hit;
	AActor* HitActor = nullptr;
	if (!World->RaycastPrimitives(Ray, Hit, HitActor))
	{
		return false;
	}

	OutPos = Ray.Origin + NormalizedOrZero(Ray.Direction) * Hit.Distance;
	return true;
}

bool FRoadEditMode::CursorToWorldForEdge(const FRay& Ray, const FRoadEdge& Edge, FVector& OutPos) const
{
	if (RaycastGround(Ray, OutPos))
	{
		return true;
	}

	// 지형 미스: 세그먼트 start→end 방향과 Up으로 만든 Gram-Schmidt 평면에 투영.
	if (URoadGraphComponent* Comp = RoadComponent.Get())
	{
		const FRoadGraph& Graph = Comp->GetRoadGraph();
		const FRoadNode* A = Graph.FindNode(Edge.StartNodeID);
		const FRoadNode* B = Graph.FindNode(Edge.EndNodeID);
		if (A && B)
		{
			const FVector D = B->Position - A->Position;
			if (!D.IsNearlyZero())
			{
				const FVector Dn = D.Normalized();
				const FVector Up(0.0f, 0.0f, 1.0f);
				FVector N = Up - Dn * Up.Dot(Dn);
				if (!N.IsNearlyZero())
				{
					N = N.Normalized();
					if (IntersectPlane(Ray, A->Position, N, OutPos))
					{
						return true;
					}
				}
			}
		}
	}

	OutPos = CameraForwardPoint(Ray, NodeAddForwardDistance);
	return true;
}

void FRoadEditMode::CreateEdge(int32 FromNodeIndex, int32 ToNodeIndex)
{
	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return;
	}

	FRoadGraph& Graph = Comp->GetRoadGraphMutable();
	const int32 NodeCount = static_cast<int32>(Graph.Nodes.size());
	if (FromNodeIndex < 0 || FromNodeIndex >= NodeCount) return;
	if (ToNodeIndex < 0 || ToNodeIndex >= NodeCount) return;
	if (FromNodeIndex == ToNodeIndex) return; // self-loop 금지

	FRoadEdge Edge;
	Edge.ID = NextEdgeID(Graph);
	Edge.StartNodeID = Graph.Nodes[FromNodeIndex].ID;
	Edge.EndNodeID = Graph.Nodes[ToNodeIndex].ID;
	Edge.Width = 1.0f;
	Graph.Edges.push_back(Edge);
	Comp->PostEditProperty("RoadGraph");

	SelectEdge(static_cast<int32>(Graph.Edges.size()) - 1);
}

void FRoadEditMode::AddNodeAtCursor(const FRay& Ray)
{
	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return;
	}

	bConnecting = false;
	ConnectFromNodeIndex = -1;

	FVector Position;
	if (!RaycastGround(Ray, Position))
	{
		Position = CameraForwardPoint(Ray, NodeAddForwardDistance);
	}

	FRoadGraph& Graph = Comp->GetRoadGraphMutable();
	FRoadNode Node;
	Node.ID = NextNodeID(Graph);
	Node.Position = Position;
	Graph.Nodes.push_back(Node);
	Comp->PostEditProperty("RoadGraph");

	SelectNode(static_cast<int32>(Graph.Nodes.size()) - 1);
}

void FRoadEditMode::ToggleConnect()
{
	if (bConnecting)
	{
		bConnecting = false;
		ConnectFromNodeIndex = -1;
		return;
	}

	if (Selection == ERoadEditSelection::Node && SelectedNodeIndex >= 0)
	{
		bConnecting = true;
		ConnectFromNodeIndex = SelectedNodeIndex;
	}
}

void FRoadEditMode::AddControlPointAtCursor(const FRay& Ray)
{
	if (Selection != ERoadEditSelection::Edge)
	{
		return; // V는 세그먼트 선택 상태에서만.
	}

	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return;
	}

	FRoadGraph& Graph = Comp->GetRoadGraphMutable();
	if (SelectedEdgeIndex < 0 || SelectedEdgeIndex >= static_cast<int32>(Graph.Edges.size()))
	{
		return;
	}

	FRoadEdge& Edge = Graph.Edges[SelectedEdgeIndex];

	FVector CursorPos;
	if (!CursorToWorldForEdge(Ray, Edge, CursorPos))
	{
		return;
	}

	// SpanIndex가 그대로 ControlPoints 삽입 위치(중간 insert). 삽입점은 커서 위치.
	const FRoadEdgeQueryResult Query = Graph.FindClosestPointOnEdge(Edge, CursorPos);
	int32 InsertIndex = Query.bValid ? Query.SpanIndex : static_cast<int32>(Edge.ControlPoints.size());
	InsertIndex = InsertIndex < 0 ? 0 : InsertIndex;
	if (InsertIndex > static_cast<int32>(Edge.ControlPoints.size()))
	{
		InsertIndex = static_cast<int32>(Edge.ControlPoints.size());
	}

	Edge.ControlPoints.insert(Edge.ControlPoints.begin() + InsertIndex, CursorPos);
	Comp->PostEditProperty("RoadGraph");

	SelectControlPoint(SelectedEdgeIndex, InsertIndex);
}

int32 FRoadEditMode::CountReferencingEdges(const FRoadGraph& Graph, int32 NodeID)
{
	int32 Count = 0;
	for (const FRoadEdge& Edge : Graph.Edges)
	{
		if (Edge.StartNodeID == NodeID || Edge.EndNodeID == NodeID)
		{
			++Count;
		}
	}
	return Count;
}

void FRoadEditMode::PerformNodeDelete(int32 NodeIndex)
{
	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return;
	}

	FRoadGraph& Graph = Comp->GetRoadGraphMutable();
	if (NodeIndex < 0 || NodeIndex >= static_cast<int32>(Graph.Nodes.size()))
	{
		return;
	}

	const int32 NodeID = Graph.Nodes[NodeIndex].ID;

	// 이 노드를 참조하는 세그먼트를 먼저 제거(뒤에서부터 지워 인덱스 안정).
	for (int32 i = static_cast<int32>(Graph.Edges.size()) - 1; i >= 0; --i)
	{
		if (Graph.Edges[i].StartNodeID == NodeID || Graph.Edges[i].EndNodeID == NodeID)
		{
			Graph.Edges.erase(Graph.Edges.begin() + i);
		}
	}

	Graph.Nodes.erase(Graph.Nodes.begin() + NodeIndex);
	Comp->PostEditProperty("RoadGraph");
}

void FRoadEditMode::RequestDeleteSelected()
{
	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return;
	}

	FRoadGraph& Graph = Comp->GetRoadGraphMutable();

	switch (Selection)
	{
	case ERoadEditSelection::Node:
	{
		if (SelectedNodeIndex < 0 || SelectedNodeIndex >= static_cast<int32>(Graph.Nodes.size()))
		{
			ClearSelection();
			return;
		}

		const int32 NodeID = Graph.Nodes[SelectedNodeIndex].ID;
		const int32 RefCount = CountReferencingEdges(Graph, NodeID);
		if (RefCount > 0)
		{
			// 연결 세그먼트가 있으면 확인 팝업(RenderUI에서 처리).
			PendingDeleteNodeIndex = SelectedNodeIndex;
			PendingDeleteEdgeCount = RefCount;
			bPendingNodeDeleteConfirm = true;
			bNeedOpenDeletePopup = true;
		}
		else
		{
			PerformNodeDelete(SelectedNodeIndex);
			ClearSelection();
		}
		break;
	}
	case ERoadEditSelection::ControlPoint:
	{
		if (SelectedEdgeIndex >= 0 && SelectedEdgeIndex < static_cast<int32>(Graph.Edges.size()))
		{
			TArray<FVector>& ControlPoints = Graph.Edges[SelectedEdgeIndex].ControlPoints;
			if (SelectedControlPointIndex >= 0 && SelectedControlPointIndex < static_cast<int32>(ControlPoints.size()))
			{
				ControlPoints.erase(ControlPoints.begin() + SelectedControlPointIndex);
				Comp->PostEditProperty("RoadGraph");
			}
		}
		ClearSelection();
		break;
	}
	case ERoadEditSelection::Edge:
	{
		if (SelectedEdgeIndex >= 0 && SelectedEdgeIndex < static_cast<int32>(Graph.Edges.size()))
		{
			Graph.Edges.erase(Graph.Edges.begin() + SelectedEdgeIndex);
			Comp->PostEditProperty("RoadGraph");
		}
		ClearSelection();
		break;
	}
	default:
		break;
	}
}

void FRoadEditMode::DrawHighlights() const
{
	URoadGraphComponent* Comp = RoadComponent.Get();
	if (!Comp)
	{
		return;
	}

	UWorld* World = Comp->GetWorld();
	if (!World)
	{
		return;
	}

	const FRoadGraph& Graph = Comp->GetRoadGraph();
	const FColor HighlightColor(255, 255, 255);

	// 연결 대기 중인 출발 노드는 청록으로 강조.
	if (bConnecting && ConnectFromNodeIndex >= 0 && ConnectFromNodeIndex < static_cast<int32>(Graph.Nodes.size()))
	{
		DrawDebugSphere(World, Graph.Nodes[ConnectFromNodeIndex].Position, 0.85f, 16, FColor(0, 255, 255), 0.0f);
	}

	// hover 강조(옅은 회색). 선택된 것과 동일 요소면 생략.
	const bool bHoverIsSelected =
		HoverKind == Selection &&
		HoverNodeIndex == SelectedNodeIndex &&
		HoverEdgeIndex == SelectedEdgeIndex &&
		HoverControlPointIndex == SelectedControlPointIndex;
	if (!bHoverIsSelected)
	{
		const FColor HoverColor(150, 150, 150);
		switch (HoverKind)
		{
		case ERoadEditSelection::Node:
			if (HoverNodeIndex >= 0 && HoverNodeIndex < static_cast<int32>(Graph.Nodes.size()))
			{
				DrawDebugSphere(World, Graph.Nodes[HoverNodeIndex].Position, 0.75f, 12, HoverColor, 0.0f);
			}
			break;
		case ERoadEditSelection::ControlPoint:
			if (HoverEdgeIndex >= 0 && HoverEdgeIndex < static_cast<int32>(Graph.Edges.size()))
			{
				const TArray<FVector>& HoverCPs = Graph.Edges[HoverEdgeIndex].ControlPoints;
				if (HoverControlPointIndex >= 0 && HoverControlPointIndex < static_cast<int32>(HoverCPs.size()))
				{
					DrawDebugSphere(World, HoverCPs[HoverControlPointIndex], 0.5f, 12, HoverColor, 0.0f);
				}
			}
			break;
		case ERoadEditSelection::Edge:
			if (HoverEdgeIndex >= 0 && HoverEdgeIndex < static_cast<int32>(Graph.Edges.size()))
			{
				TArray<FVector> HoverPath;
				if (Graph.GetEdgePathPoints(Graph.Edges[HoverEdgeIndex], HoverPath))
				{
					for (int32 i = 0; i + 1 < static_cast<int32>(HoverPath.size()); ++i)
					{
						DrawDebugLine(World, HoverPath[i], HoverPath[i + 1], HoverColor, 0.0f);
					}
				}
			}
			break;
		default:
			break;
		}
	}

	switch (Selection)
	{
	case ERoadEditSelection::Node:
		if (SelectedNodeIndex >= 0 && SelectedNodeIndex < static_cast<int32>(Graph.Nodes.size()))
		{
			DrawDebugSphere(World, Graph.Nodes[SelectedNodeIndex].Position, 0.7f, 16, HighlightColor, 0.0f);
		}
		break;
	case ERoadEditSelection::ControlPoint:
		if (SelectedEdgeIndex >= 0 && SelectedEdgeIndex < static_cast<int32>(Graph.Edges.size()))
		{
			const TArray<FVector>& ControlPoints = Graph.Edges[SelectedEdgeIndex].ControlPoints;
			if (SelectedControlPointIndex >= 0 && SelectedControlPointIndex < static_cast<int32>(ControlPoints.size()))
			{
				DrawDebugSphere(World, ControlPoints[SelectedControlPointIndex], 0.45f, 16, HighlightColor, 0.0f);
			}
		}
		break;
	case ERoadEditSelection::Edge:
		if (SelectedEdgeIndex >= 0 && SelectedEdgeIndex < static_cast<int32>(Graph.Edges.size()))
		{
			TArray<FVector> PathPoints;
			if (Graph.GetEdgePathPoints(Graph.Edges[SelectedEdgeIndex], PathPoints))
			{
				const FColor EdgeColor(255, 140, 0);
				for (int32 i = 0; i + 1 < static_cast<int32>(PathPoints.size()); ++i)
				{
					DrawDebugLine(World, PathPoints[i], PathPoints[i + 1], EdgeColor, 0.0f);
				}
			}
		}
		break;
	default:
		break;
	}
}

void FRoadEditMode::RenderUI()
{
	if (!bPendingNodeDeleteConfirm)
	{
		return;
	}

	const char* PopupId = "Delete Node###RoadEditDeleteNodeConfirm";

	// OpenPopup/BeginPopupModal은 현재 window를 요구한다. 이 함수는 최상위(현재 window 없음)에서
	// 호출되므로 보이지 않는 최소 host window를 열어 컨텍스트를 제공한다.
	const ImVec2 ScreenCenter = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(ScreenCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(1.0f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags HostFlags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	ImGui::Begin("##RoadEditModalHost", nullptr, HostFlags);

	if (bNeedOpenDeletePopup)
	{
		ImGui::OpenPopup(PopupId);
		bNeedOpenDeletePopup = false;
	}

	ImGui::SetNextWindowPos(ScreenCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal(PopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("This node is connected to %d Edge(s).", PendingDeleteEdgeCount);
		ImGui::Text("They will be deleted together. Continue?");
		ImGui::Separator();

		if (ImGui::Button("Delete", ImVec2(120, 0)))
		{
			PerformNodeDelete(PendingDeleteNodeIndex);
			bPendingNodeDeleteConfirm = false;
			PendingDeleteNodeIndex = -1;
			PendingDeleteEdgeCount = 0;
			ClearSelection();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)))
		{
			bPendingNodeDeleteConfirm = false;
			PendingDeleteNodeIndex = -1;
			PendingDeleteEdgeCount = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	else
	{
		// Esc 등으로 팝업이 닫혔으면 취소로 처리.
		bPendingNodeDeleteConfirm = false;
		PendingDeleteNodeIndex = -1;
		PendingDeleteEdgeCount = 0;
	}

	ImGui::End();
	ImGui::PopStyleVar();
}
