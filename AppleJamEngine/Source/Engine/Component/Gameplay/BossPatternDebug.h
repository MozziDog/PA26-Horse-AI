#pragma once

#include "Core/Types/CoreTypes.h"
#include "Profiling/Stats/Stats.h"

// BossPatternSelector 실행 상태의 시각화용 스냅샷. 집계 지표(stat)가 아니라
// "지금 어떤 패턴이 활성/차단인가"의 런타임 debug 상태다.
enum class EBossPatternDebugStatus : uint8
{
	Ready,
	Active,
	Blocked
};

struct FBossPatternDebugEntry
{
	FString PatternName;
	FString Reason;
	FString Detail;
	EBossPatternDebugStatus Status = EBossPatternDebugStatus::Blocked;
	float CooldownRemaining = 0.0f;
	float Weight = 0.0f;
	int32 SelectionCount = 0;
};

struct FBossPatternDebugSnapshot
{
	FString OwnerName;
	FString ActivePatternName;
	FString LastSelectedPatternName;
	FString LastRejectedReason;
	FString ActiveDetail;
	int32 CandidateCount = 0;
	int32 UsableCandidateCount = 0;
	int32 SelectionCount = 0;
	int32 FallbackCount = 0;
	int32 BossPhase = 0;
	float BossHealthRatio = 1.0f;
	bool bSelectionEnabled = false;
	TArray<FBossPatternDebugEntry> Patterns;
};

#if STATS
struct FBossPatternDebug
{
	static uint32 ComponentCount;
	static TArray<FBossPatternDebugSnapshot> Snapshots;

	static void Reset();
	static void AddComponent(const FBossPatternDebugSnapshot& Snapshot);
};

#define BOSSPATTERN_DEBUG_RESET() FBossPatternDebug::Reset()
#define BOSSPATTERN_DEBUG_ADD_COMPONENT(Snapshot) FBossPatternDebug::AddComponent((Snapshot))
#else
#define BOSSPATTERN_DEBUG_RESET() ((void)0)
#define BOSSPATTERN_DEBUG_ADD_COMPONENT(Snapshot) ((void)0)
#endif
