#pragma once
#include "Editor/UI/Asset/AssetEditorWidget.h"

class AUICanvasActor;
class UUICanvas;
class UUIElement;
class UUIAsset;
class UClass;

// UI 에셋(UUIAsset) 전용 에디터 창. 4분할(팔레트/계층트리/캔버스 뷰포트/디테일) 구성.
// 진단서 UIEditor_4Panel_Layout_Diagnosis.md 기준. ImGui 즉시모드, free-floating 창.
class FUIEditorWidget : public FAssetEditorWidget
{
public:
	FUIEditorWidget() = default;

	bool CanEdit(UObject* Object) const override;
	void Open(UObject* Object) override;
	void Close() override;
	void Render(float DeltaTime) override;
	void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	// .uasset JSON 블롭(UUIAsset::CanvasData) → 라이브 UUICanvas 트리 복원/파괴(사이클 ⓪).
	// 에디터 전용 소유자 액터(월드 미사용). 전역 FUICanvasManager 에는 등록하지 않는다
	// (런타임 SimpleUIPass/LayoutAll 오염 방지) — 레이아웃/히트테스트는 per-canvas seam 사용.
	void BuildLiveTree(UUIAsset* Asset);
	void DestroyLiveTree();
	void SaveToAsset();   // 편집 라이브 트리 → SerializeUITree → UUIAsset::CanvasData → 파일 저장(사이클 ⑥).

	// 4분할 패널 내용(사이클 ①은 타이틀만, 후속 사이클이 알맹이를 채운다).
	void RenderPalettePanel();    // 좌상 — Canvas/Button/Image 팔레트(사이클 ③)
	void RenderHierarchyPanel();  // 좌하 — 계층 트리(사이클 ②)
	void RenderViewportPanel();   // 중앙 — 캔버스 뷰포트(사이클 ②), 선택/히트테스트(사이클 ④)
	void RenderDetailsPanel();    // 우   — W/H·Offset·Pivot·Color 디테일(사이클 ⑤)

	// 팔레트 → 요소 생성 + 부착(선택 노드, 없으면 캔버스 루트). 진단 §D: AddComponentToActor 모델.
	void SpawnElement(UClass* ElementClass);

	// 선택 요소(+서브트리) 삭제. AUICanvasActor::RemoveComponent 로 부모 detach + OwnedComponents 해제 +
	// RouteComponentDestroyed(자식 재귀) + DestroyObject 일괄. 루트 캔버스는 삭제 불가(트리 루트 계약).
	void DeleteSelected();

	AUICanvasActor* OwnerActor   = nullptr;  // 복원 트리의 소유자(에디터 수명). GC keepalive 대상.
	UUICanvas*      Canvas       = nullptr;  // 복원된 루트 캔버스(편집/레이아웃/드로우 대상).
	UUIElement*     Selected     = nullptr;  // 현재 선택 요소(트리/뷰포트). 생성 부모 + 디테일 대상.
	float           ViewportZoom = 1.0f;     // 뷰포트 줌(높이맞춤 기본 스케일에 곱). 휠로 조절.
};
