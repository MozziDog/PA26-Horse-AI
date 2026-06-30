#pragma once

#include "Core/Types/CoreTypes.h"
#include "Profiling/Stats/Stats.h"

enum class EBossPatternStatStatus : uint8
{
	Ready,
	Active,
	Blocked
};

struct FBossPatternStatEntry
{
	FString PatternName;
	FString Reason;
	FString Detail;
	EBossPatternStatStatus Status = EBossPatternStatStatus::Blocked;
	float CooldownRemaining = 0.0f;
	float Weight = 0.0f;
	int32 SelectionCount = 0;
};

struct FBossPatternStatsSnapshot
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
	TArray<FBossPatternStatEntry> Patterns;
};

#if STATS
struct FBossPatternStats
{
	static uint32 ComponentCount;
	static TArray<FBossPatternStatsSnapshot> Snapshots;

	static void Reset();
	static void AddComponent(const FBossPatternStatsSnapshot& Snapshot);
};

#define BOSSPATTERN_STATS_RESET() FBossPatternStats::Reset()
#define BOSSPATTERN_STATS_ADD_COMPONENT(Snapshot) FBossPatternStats::AddComponent((Snapshot))
#else
#define BOSSPATTERN_STATS_RESET() ((void)0)
#define BOSSPATTERN_STATS_ADD_COMPONENT(Snapshot) ((void)0)
#endif
