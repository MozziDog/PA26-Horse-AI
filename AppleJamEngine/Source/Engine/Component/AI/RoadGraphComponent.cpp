#include "RoadGraphComponent.h"

#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

URoadGraphComponent::URoadGraphComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void URoadGraphComponent::CreateRenderState()
{
	UActorComponent::CreateRenderState();
	EnableEditorTick();
}

void URoadGraphComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
	EnableEditorTick();
}

void URoadGraphComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)DeltaTime;
	(void)TickType;
	(void)ThisTickFunction;

	// 형제 디버그 컴포넌트들과 동일하게 매 틱 재확인한다.
	EnableEditorTick();

	if (bDrawDebug)
	{
		DrawRoadNetwork();
	}
}

void URoadGraphComponent::EnableEditorTick()
{
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->bTickInEditor = true;
	}
}

void URoadGraphComponent::DrawRoadNetwork() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 노드: 초록 구
	for (const FRoadNode& Node : RoadGraph.Nodes)
	{
		DrawDebugSphere(World, Node.Position, NodeDrawRadius, 12, FColor::Green(), 0.0f);
	}

	// 세그먼트: Start 노드 -> ControlPoints... -> End 노드 를 순서대로 잇는 꺾은 선(노랑).
	const FColor ControlPointColor(0, 200, 255);
	for (const FRoadSegment& Segment : RoadGraph.Segments)
	{
		const FRoadNode* StartNode = RoadGraph.FindNode(Segment.StartNodeID);
		const FRoadNode* EndNode = RoadGraph.FindNode(Segment.EndNodeID);
		if (!StartNode || !EndNode)
		{
			continue;
		}

		FVector PrevPoint = StartNode->Position;
		for (const FVector& ControlPoint : Segment.ControlPoints)
		{
			DrawDebugLine(World, PrevPoint, ControlPoint, FColor::Yellow(), 0.0f);
			DrawDebugPoint(World, ControlPoint, 0.15f, ControlPointColor, 0.0f);
			PrevPoint = ControlPoint;
		}
		DrawDebugLine(World, PrevPoint, EndNode->Position, FColor::Yellow(), 0.0f);
	}
}
