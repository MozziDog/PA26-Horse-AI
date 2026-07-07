#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Gizmo/RoadGraphGizmoTarget.h"

class URoadGraphComponent;
class UWorld;
class UGizmoComponent;
struct FRay;
struct FRoadGraph;
struct FRoadEdge;

// EGizmoMode(Translate/Rotate/Scale)와 독립축인 전역 에디터 툴 모드.
enum class EEditorToolMode : uint8
{
	Default,
	RoadEdit,
};

enum class ERoadEditSelection : uint8
{
	None,
	Node,
	ControlPoint,
	Edge,
};

// 레벨 뷰포트에서 도로망(노드/제어점/세그먼트)을 편집하는 툴 상태.
// 도로 액터는 월드에 1개 전제 → 진입 시 유일 컴포넌트를 해석한다.
class FRoadEditMode
{
public:
	void Enter(UWorld* World, UGizmoComponent* InGizmo);
	void Exit();
	void Tick();
	void DrawHighlights() const;
	void RenderUI();

	URoadGraphComponent* GetRoadComponent() const { return RoadComponent.Get(); }
	bool HasRoadComponent() const { return RoadComponent.Get() != nullptr; }

	// 진입 시점의 월드와 동일한지. 씬 로드 등으로 월드가 바뀌면 false.
	bool IsBoundToWorld(UWorld* World) const;

	// 뷰포트 좌클릭: 노드 > 제어점 > 세그먼트 우선순위로 선택.
	void PickAtRay(const FRay& Ray);
	void ClearSelection();

	// 매 프레임 마우스 위치 기준 hover 대상 갱신(선택은 바꾸지 않음).
	void UpdateHover(const FRay& Ray);

	// 선택된 요소 삭제. 연결 세그먼트가 있는 노드는 확인 팝업을 띄운다.
	void RequestDeleteSelected();

	// 생성 단축키
	void AddNodeAtCursor(const FRay& Ray);          // N
	void ToggleConnect();                           // C: 선택 노드에서 연결 시작/취소
	void AddControlPointAtCursor(const FRay& Ray);  // V: 선택 세그먼트에 제어점 삽입

private:
	// World에서 RoadGraphComponent 가져오기. World 상에 1개만 있다고 가정.
	static URoadGraphComponent* ResolveRoadComponent(UWorld* World);
	static int32 CountReferencingEdges(const FRoadGraph& Graph, int32 NodeID);

	void SelectNode(int32 NodeIndex);
	void SelectControlPoint(int32 EdgeIndex, int32 ControlPointIndex);
	void SelectEdge(int32 EdgeIndex);
	void ApplyGizmoTarget();
	void PerformNodeDelete(int32 NodeIndex);

	int32 PickNodeAtRay(const FRay& Ray) const;
	bool PickElementAtRay(const FRay& Ray, ERoadEditSelection& OutKind, int32& OutNodeIndex, int32& OutEdgeIndex, int32& OutControlPointIndex) const;
	void CreateEdge(int32 FromNodeIndex, int32 ToNodeIndex);
	bool CursorToWorldForEdge(const FRay& Ray, const FRoadEdge& Edge, FVector& OutPos) const;
	bool RaycastGround(const FRay& Ray, FVector& OutPos) const;

	TWeakObjectPtr<URoadGraphComponent> RoadComponent;
	TWeakObjectPtr<UWorld> BoundWorld;
	UGizmoComponent* Gizmo = nullptr;

	FRoadGraphGizmoTarget GizmoTarget;
	ERoadEditSelection Selection = ERoadEditSelection::None;
	int32 SelectedNodeIndex = -1;
	int32 SelectedEdgeIndex = -1;
	int32 SelectedControlPointIndex = -1;

	// 세그먼트 연결 대기 상태 (C로 시작 → 다음 노드 클릭으로 완성).
	bool bConnecting = false;
	int32 ConnectFromNodeIndex = -1;

	// hover 대상 (마우스 오버 강조용).
	ERoadEditSelection HoverKind = ERoadEditSelection::None;
	int32 HoverNodeIndex = -1;
	int32 HoverEdgeIndex = -1;
	int32 HoverControlPointIndex = -1;

	// 노드 삭제 확인 팝업 상태.
	bool bPendingNodeDeleteConfirm = false;
	bool bNeedOpenDeletePopup = false;
	int32 PendingDeleteNodeIndex = -1;
	int32 PendingDeleteEdgeCount = 0;
};
