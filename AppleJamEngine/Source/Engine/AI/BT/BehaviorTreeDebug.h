#pragma once

#include "Core/Types/CoreTypes.h"
#include "Profiling/Stats/Stats.h"

// BehaviorTree 실행 상태의 시각화용 스냅샷.
// 매 프레임 BTExecutor 가 트리를 평탄화해서 넘김. (의존성 최소화 — BehaviorTree.h 에 의존 안 함)
enum class EBTNodeDebugResult : uint8
{
	Running,
	Success,
	Fail
};

struct FBTNodeDebugEntry
{
	int32              Depth               = 0;      // 트리 깊이(들여쓰기용)
	FString            TypeName;                      // "Sequence" / "Task" ...
	FString            Label;                         // 노드 표시 이름
	EBTNodeDebugResult LastResult          = EBTNodeDebugResult::Fail;
	float              ActiveDuration      = 0.0f;
	bool               bEvaluatedThisFrame = false;   // 이번 tick 에 평가됐나(회색 처리 기준)
	bool               bOnActivePath       = false;   // root→running leaf 체인 위인가
};

struct FBehaviorTreeDebugSnapshot
{
	FString                   OwnerName;
	uint64                    FrameNumber = 0;
	TArray<FBTNodeDebugEntry> Nodes;      // pre-order(DFS) 순서로 평탄화
};

#if STATS
struct FBehaviorTreeDebug
{
	static uint32 TreeCount;
	static TArray<FBehaviorTreeDebugSnapshot> Snapshots;

	static void Reset();
	static void AddTree(const FBehaviorTreeDebugSnapshot& Snapshot);
};

#define BT_DEBUG_RESET()          FBehaviorTreeDebug::Reset()
#define BT_DEBUG_ADD_TREE(Snap)   FBehaviorTreeDebug::AddTree((Snap))
#else
#define BT_DEBUG_RESET()          ((void)0)
#define BT_DEBUG_ADD_TREE(Snap)   ((void)0)
#endif
