#include "pch.h"
#include "GameFramework/ProjectilePoolSubSystem.h"
#include "GameFramework/World.h"

void FProjectilePoolSubsystem::Initialize(UWorld* InWorld)
{
	World = InWorld;
	// FGCObject 생성자가 이미 GC 외부 루트로 자기 자신을 등록했다.
	// → InactivePool / ActiveLookup 이 들고 있는 액터는 unreachable 로 잡히지 않는다.
}

void FProjectilePoolSubsystem::Release(AActor* Projectile)
{
	if (!IsValid(Projectile)) return;

	auto It = ActiveLookup.find(Projectile);
	if (It == ActiveLookup.end()) return;         // 이중 반환 / 외부 객체 방어
	IPoolableProjectile* Pooled = It->second;
	ActiveLookup.erase(It);

	Pooled->Deactivate();                         // 렌더/충돌/틱 OFF
	Pooled->ResetState();                         // 속도·타이머·파티클 리셋

	// ★ DestroyActor 호출 금지 — 비활성 상태로 풀에 보관만.
	InactivePool[Projectile->GetClass()].push_back(Pooled);
}

void FProjectilePoolSubsystem::Shutdown()
{
	// 월드 종료 정리 단계에서만 '실제 파괴'.
	// (FGCObject 루팅이 풀어지면 GC 가 알아서 회수하지만, 여기서 명시적으로 끊어 결정적 정리)
	if (World)
	{
		for (auto& Pair : InactivePool)
			for (IPoolableProjectile* P : Pair.second)
				if (P && IsValid(P->AsActor()))
					World->DestroyActor(P->AsActor());

		for (auto& Pair : ActiveLookup)
			if (IsValid(Pair.first))
				World->DestroyActor(Pair.first);
	}

	InactivePool.clear();
	ActiveLookup.clear();
	World = nullptr;
	// 별도 RemoveExternalRoot 불필요 — FGCObject 소멸자가 처리.
}

void FProjectilePoolSubsystem::AddReferencedObjects(FReferenceCollector& Collector)
{
	// ★ GC 핵심: 보관(비활성) + 대여(활성) 중인 모든 액터를 도달 가능으로 보고.
	//   AddReferencedObject 가 내부에서 IsValid 검사하므로 null/pending-kill 안전.
	for (auto& Pair : InactivePool)
		for (IPoolableProjectile* P : Pair.second)
			if (P)
				Collector.AddReferencedObject(P->AsActor(), "FProjectilePoolSubsystem.InactivePool");

	for (auto& Pair : ActiveLookup)
		Collector.AddReferencedObject(Pair.first, "FProjectilePoolSubsystem.ActiveLookup");
}