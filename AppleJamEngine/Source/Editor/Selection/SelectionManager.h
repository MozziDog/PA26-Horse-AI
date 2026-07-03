#pragma once

#include "Core/Types/CoreTypes.h"
#include "Object/GarbageCollection.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include <algorithm>

class AActor;
class USceneComponent;
class UGizmoComponent;
class UWorld;

class FSelectionManager : public FGCObject
{
public:
	void Init();
	void Shutdown();

	void Select(AActor* Actor);
	void SelectRange(AActor* ClickedActor, const TArray<AActor*>& ActorList);
	void ToggleSelect(AActor* Actor);
	void Deselect(AActor* Actor);
	void ClearSelection();
	int32 DeleteSelectedActors();
	// 선택 의존 없이 특정 액터를 즉시 파괴. 편집 모드의 AUICanvasActor 처럼 RootComponent 가 없어
	// Select() 가 거부하는(=뷰포트 raycast picking 불가) 액터를 Outliner 우클릭 Remove 로 지우기 위함.
	// 선택 중이던 액터면 dangling 기즈모/선택 방지를 위해 먼저 Deselect. 반환: 실제로 파괴했는지.
	bool DeleteActor(AActor* Actor);
	void Tick();

	void SelectComponent(USceneComponent* Component);
	USceneComponent* GetSelectedComponent() const;

	bool IsSelected(AActor* Actor) const;

	AActor* GetPrimarySelection() const;

	TArray<AActor*> GetSelectedActors() const;
	bool IsEmpty() const { return GetSelectedActors().empty() && GetSelectedComponent() == nullptr; }

	UGizmoComponent* GetGizmo() const;

	void SetGizmoEnabled(bool bEnabled);

	// 서브오브젝트 편집 툴(예: RoadEdit)이 gizmo 타겟을 직접 소유하는 동안,
	// 선택된 액터로의 매-틱 재동기화를 중단한다.
	void SetSubObjectOverride(bool bOverride);
	bool IsSubObjectOverride() const { return bSubObjectOverride; }

	void SetWorld(UWorld* InWorld);
	const char* GetReferencerName() const override { return "FSelectionManager"; }
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	void PruneInvalidSelection();
	void SyncGizmo();
	void SetActorProxiesSelected(AActor* Actor, bool bSelected);
	void RefreshSelectedActorCache();

	TArray<TWeakObjectPtr<AActor>> SelectedActors;
	TArray<AActor*> SelectedActorCache;
	TWeakObjectPtr<USceneComponent> SelectedComponent;
	UGizmoComponent* Gizmo = nullptr;
	TWeakObjectPtr<UWorld> World;
	bool bGizmoEnabled = true;
	bool bSubObjectOverride = false;
};
