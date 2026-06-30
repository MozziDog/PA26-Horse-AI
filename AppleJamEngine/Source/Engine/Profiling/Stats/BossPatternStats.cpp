#include "BossPatternStats.h"

#if STATS
uint32 FBossPatternStats::ComponentCount = 0;
TArray<FBossPatternStatsSnapshot> FBossPatternStats::Snapshots;

void FBossPatternStats::Reset()
{
	ComponentCount = 0;
	Snapshots.clear();
}

void FBossPatternStats::AddComponent(const FBossPatternStatsSnapshot& Snapshot)
{
	++ComponentCount;
	Snapshots.push_back(Snapshot);
}
#endif
