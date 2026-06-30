#include "BulletHellStats.h"

#if STATS
uint32 FBulletHellStats::ComponentCount = 0;
uint32 FBulletHellStats::ActiveBulletCount = 0;
uint32 FBulletHellStats::TotalSpawned = 0;
uint32 FBulletHellStats::TotalKilled = 0;
uint32 FBulletHellStats::TotalExpired = 0;
uint32 FBulletHellStats::CollisionQueryCount = 0;
uint32 FBulletHellStats::CollisionHitCount = 0;
uint32 FBulletHellStats::CollisionKilledCount = 0;
uint32 FBulletHellStats::EraseKilledCount = 0;
uint32 FBulletHellStats::RuntimeModificationCount = 0;
uint32 FBulletHellStats::ActiveNonHomingCount = 0;
uint32 FBulletHellStats::ActiveHomingCount = 0;
uint32 FBulletHellStats::ActivePrimaryArchetypeCount = 0;
uint32 FBulletHellStats::ActiveSecondaryArchetypeCount = 0;
uint32 FBulletHellStats::DebugDrawSelectedCount = 0;
uint32 FBulletHellStats::DebugDrawTruncatedCount = 0;
uint32 FBulletHellStats::RenderInstanceCount = 0;
uint32 FBulletHellStats::RendererSlotCount = 0;
uint32 FBulletHellStats::RendererSlot0InstanceCount = 0;
uint32 FBulletHellStats::RendererSlot1InstanceCount = 0;
uint32 FBulletHellStats::RenderMismatchCount = 0;
uint32 FBulletHellStats::TrailEnabledBulletCount = 0;
uint32 FBulletHellStats::TrailSampleCount = 0;
uint32 FBulletHellStats::TrailBatchCount = 0;
uint32 FBulletHellStats::TrailVertexCount = 0;
uint32 FBulletHellStats::TrailIndexCount = 0;
uint32 FBulletHellStats::TrailTruncatedCount = 0;
uint32 FBulletHellStats::TrailMaterialMissingCount = 0;
uint32 FBulletHellStats::DeathEffectComponentCount = 0;
uint32 FBulletHellStats::DeathEffectEventCount = 0;
uint32 FBulletHellStats::DeathEffectDroppedCount = 0;
uint32 FBulletHellStats::DeathEffectMissingAssetCount = 0;
uint32 FBulletHellStats::DeathEffectBudgetExceededCount = 0;

void FBulletHellStats::Reset()
{
	ComponentCount = 0;
	ActiveBulletCount = 0;
	TotalSpawned = 0;
	TotalKilled = 0;
	TotalExpired = 0;
	CollisionQueryCount = 0;
	CollisionHitCount = 0;
	CollisionKilledCount = 0;
	EraseKilledCount = 0;
	RuntimeModificationCount = 0;
	ActiveNonHomingCount = 0;
	ActiveHomingCount = 0;
	ActivePrimaryArchetypeCount = 0;
	ActiveSecondaryArchetypeCount = 0;
	DebugDrawSelectedCount = 0;
	DebugDrawTruncatedCount = 0;
	RenderInstanceCount = 0;
	RendererSlotCount = 0;
	RendererSlot0InstanceCount = 0;
	RendererSlot1InstanceCount = 0;
	RenderMismatchCount = 0;
	TrailEnabledBulletCount = 0;
	TrailSampleCount = 0;
	TrailBatchCount = 0;
	TrailVertexCount = 0;
	TrailIndexCount = 0;
	TrailTruncatedCount = 0;
	TrailMaterialMissingCount = 0;
	DeathEffectComponentCount = 0;
	DeathEffectEventCount = 0;
	DeathEffectDroppedCount = 0;
	DeathEffectMissingAssetCount = 0;
	DeathEffectBudgetExceededCount = 0;
}

void FBulletHellStats::AddComponent(const FBulletHellStatsSnapshot& Snapshot)
{
	++ComponentCount;
	ActiveBulletCount += Snapshot.ActiveBulletCount;
	TotalSpawned += Snapshot.TotalSpawned;
	TotalKilled += Snapshot.TotalKilled;
	TotalExpired += Snapshot.TotalExpired;
	CollisionQueryCount += Snapshot.CollisionQueryCount;
	CollisionHitCount += Snapshot.CollisionHitCount;
	CollisionKilledCount += Snapshot.CollisionKilledCount;
	EraseKilledCount += Snapshot.EraseKilledCount;
	RuntimeModificationCount += Snapshot.RuntimeModificationCount;
	ActiveNonHomingCount += Snapshot.ActiveNonHomingCount;
	ActiveHomingCount += Snapshot.ActiveHomingCount;
	ActivePrimaryArchetypeCount += Snapshot.ActivePrimaryArchetypeCount;
	ActiveSecondaryArchetypeCount += Snapshot.ActiveSecondaryArchetypeCount;
	DebugDrawSelectedCount += Snapshot.DebugDrawSelectedCount;
	DebugDrawTruncatedCount += Snapshot.DebugDrawTruncatedCount;
	RenderInstanceCount += Snapshot.RenderInstanceCount;
	RendererSlotCount += Snapshot.RendererSlotCount;
	RendererSlot0InstanceCount += Snapshot.RendererSlot0InstanceCount;
	RendererSlot1InstanceCount += Snapshot.RendererSlot1InstanceCount;
	RenderMismatchCount += Snapshot.RenderMismatchCount;
	TrailEnabledBulletCount += Snapshot.TrailEnabledBulletCount;
	TrailSampleCount += Snapshot.TrailSampleCount;
	TrailBatchCount += Snapshot.TrailBatchCount;
	TrailVertexCount += Snapshot.TrailVertexCount;
	TrailIndexCount += Snapshot.TrailIndexCount;
	TrailTruncatedCount += Snapshot.TrailTruncatedCount;
	TrailMaterialMissingCount += Snapshot.TrailMaterialMissingCount;
	DeathEffectComponentCount += Snapshot.DeathEffectComponentCount;
	DeathEffectEventCount += Snapshot.DeathEffectEventCount;
	DeathEffectDroppedCount += Snapshot.DeathEffectDroppedCount;
	DeathEffectMissingAssetCount += Snapshot.DeathEffectMissingAssetCount;
	DeathEffectBudgetExceededCount += Snapshot.DeathEffectBudgetExceededCount;
}
#endif
