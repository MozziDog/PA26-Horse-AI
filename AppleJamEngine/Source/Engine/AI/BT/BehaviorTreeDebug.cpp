#include "BehaviorTreeDebug.h"

#if STATS
uint32 FBehaviorTreeDebug::TreeCount = 0;
TArray<FBehaviorTreeDebugSnapshot> FBehaviorTreeDebug::Snapshots;

void FBehaviorTreeDebug::Reset()
{
	TreeCount = 0;
	Snapshots.clear();
}

void FBehaviorTreeDebug::AddTree(const FBehaviorTreeDebugSnapshot& Snapshot)
{
	++TreeCount;
	Snapshots.push_back(Snapshot);
}
#endif
