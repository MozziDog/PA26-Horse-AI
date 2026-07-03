#pragma once

#include "Gizmo/GizmoTransformTarget.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Core/Types/CoreTypes.h"

class URoadGraphComponent;

enum class ERoadGizmoTargetKind : uint8
{
	Node,
	ControlPoint,
};

// FRoadGraph의 단일 노드 또는 제어점 좌표를 gizmo로 이동시키는 타겟.
// 회전/스케일은 도로 좌표에 의미가 없어 no-op.
class FRoadGraphGizmoTarget : public IGizmoTransformTarget
{
public:
	void SetNode(URoadGraphComponent* InComponent, int32 InNodeIndex);
	void SetControlPoint(URoadGraphComponent* InComponent, int32 InSegmentIndex, int32 InControlPointIndex);
	void Clear();

	ERoadGizmoTargetKind GetKind() const { return Kind; }
	int32 GetNodeIndex() const { return NodeIndex; }
	int32 GetSegmentIndex() const { return SegmentIndex; }
	int32 GetControlPointIndex() const { return ControlPointIndex; }

	bool IsValid() const override;
	UWorld* GetWorld() const override;

	FVector GetWorldLocation() const override;
	FRotator GetWorldRotation() const override;
	FQuat GetWorldQuat() const override;
	FVector GetWorldScale() const override;

	void SetWorldLocation(const FVector& NewLocation) override;
	void SetWorldRotation(const FRotator&) override {}
	void SetWorldRotation(const FQuat&) override {}
	void SetWorldScale(const FVector&) override {}

	void AddWorldOffset(const FVector& Delta) override;
	void AddWorldRotation(const FQuat&, bool) override {}
	void AddScaleDelta(const FVector&) override {}

private:
	FVector* ResolvePosition() const;

	TWeakObjectPtr<URoadGraphComponent> Component;
	ERoadGizmoTargetKind Kind = ERoadGizmoTargetKind::Node;
	int32 NodeIndex = -1;
	int32 SegmentIndex = -1;
	int32 ControlPointIndex = -1;
};
