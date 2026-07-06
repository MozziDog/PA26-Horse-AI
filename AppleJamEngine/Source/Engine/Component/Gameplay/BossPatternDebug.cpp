#include "BossPatternDebug.h"

#if STATS
uint32 FBossPatternDebug::ComponentCount = 0;
TArray<FBossPatternDebugSnapshot> FBossPatternDebug::Snapshots;

void FBossPatternDebug::Reset()
{
	ComponentCount = 0;
	Snapshots.clear();
}

void FBossPatternDebug::AddComponent(const FBossPatternDebugSnapshot& Snapshot)
{
	++ComponentCount;
	Snapshots.push_back(Snapshot);
}
#endif
