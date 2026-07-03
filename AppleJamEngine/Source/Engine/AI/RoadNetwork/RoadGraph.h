#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"
#include "Math/Vector.h"

#include "Source/Engine/AI/RoadNetwork/RoadGraph.generated.h"

// NOTE: RoadGraph에서 사용하는 모든 좌표는 World 좌표
USTRUCT()
struct FRoadNode
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Road Node", DisplayName="Node ID")
	int32 ID = 0;

	UPROPERTY(Edit, Save, Category="Road Node", DisplayName="Position (World)", Speed=0.1f)
	FVector Position = FVector::ZeroVector;
};

USTRUCT()
struct FRoadSegment
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Road Segment", DisplayName="Segment ID")
	int32 ID = 0;

	UPROPERTY(Edit, Save, Category="Road Segment", DisplayName="Start Node ID")
	int32 StartNodeID = 0;

	UPROPERTY(Edit, Save, Category="Road Segment", DisplayName="End Node ID")
	int32 EndNodeID = 0;

	UPROPERTY(Edit, Save, Category="Road Segment", DisplayName="Width", Speed=0.05f)
	float Width = 1.0f;

	// 시작/끝 노드 사이를 지나는 중간 정점(월드 공간). 비어 있으면 두 노드를 직선으로 잇는다.
	UPROPERTY(Edit, Save, Category="Road Segment", DisplayName="Control Points (World)", Type=Array)
	TArray<FVector> ControlPoints;
};

// 특정 점에서 가장 가까운 노드 쿼리 결과.
struct FRoadNodeQueryResult
{
	bool	bValid = false;                   // 그래프에 노드가 하나도 없으면 false.
	int32	NodeID = -1;                      // 가장 가까운 노드의 ID.
	int32	NodeIndex = -1;                   // Nodes 배열에서의 인덱스.
	FVector Position = FVector::ZeroVector;   // 노드 위치(월드).
	float	DistanceSq = 0.0f;                // 질의점과의 거리 제곱.
};

// 특정 점에서 세그먼트 위 가장 가까운 점 쿼리 결과.
struct FRoadSegmentQueryResult
{
	bool	bValid = false;                   // 유효한 세그먼트가 없으면 false.
	int32	SegmentID = -1;                   // 가장 가까운 세그먼트의 ID.
	int32	SegmentIndex = -1;                // Segments 배열에서의 인덱스.
	int32	SpanIndex = 0;                    // 제어점 사이 구간 인덱스(0이면 StartNode ~ ControlPoints[0] 구간)
	float	SpanAlpha = 0.0f;                 // 해당 구간에서의 0..1 파라미터.
	FVector Position = FVector::ZeroVector;   // Segment 곡선(꺾은선) 상에서의 좌표.
	FVector Direction = FVector::ZeroVector;  // 최근접 지점의 진행 방향(단위 벡터). 끝점이라면 0.
	float	DistanceSq = 0.0f;                // 쿼리 입력위치와의 거리 제곱.
};

// 컴포넌트가 소유하는 도로망 전체. Nodes/Segments 는 Property로 노출/직렬화
USTRUCT()
struct FRoadGraph
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="Road Graph", DisplayName="Nodes", Type=Array, Struct=FRoadNode)
	TArray<FRoadNode> Nodes;

	UPROPERTY(Edit, Save, Category="Road Graph", DisplayName="Segments", Type=Array, Struct=FRoadSegment)
	TArray<FRoadSegment> Segments;

	// ID 로 노드를 찾는다. 프로토타입이라 선형 탐색이며, 없으면 nullptr 을 반환한다.
	const FRoadNode* FindNode(int32 NodeID) const;
	FRoadNode* FindNode(int32 NodeID);

	// Segment가 지나는 점들 가져오기: Start 노드 -> ControlPoints... -> End 노드
	// 시작/끝 노드를 찾지 못하는 등 Segment가 유효하지 않으면 false 반환
	bool GetSegmentPathPoints(const FRoadSegment& Segment, TArray<FVector>& OutPoints) const;

	// --- AI 쿼리용 (현재는 완전 탐색; 추후 공간 가속 구조로 최적화 필요) ---
	FRoadNodeQueryResult FindNearestNode(const FVector& Point) const;
	FRoadSegmentQueryResult FindClosestPointOnSegments(const FVector& Point) const;

	// 특정 세그먼트에 대한 최근접점. SpanIndex는 제어점 삽입 위치로도 쓰인다. SegmentIndex는 설정하지 않음.
	FRoadSegmentQueryResult FindClosestPointOnSegment(const FRoadSegment& Segment, const FVector& Point) const;
};
