#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Singleton.h"
#include "Object/GarbageCollection.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Math/Vector.h"

class UUICanvas;
class UUIElement;

// 신규 계층형 UI 의 레지스트리 + GC 루트.
// RmlUi 의 UUIManager 와 독립인 별도 경량 시스템(진단 F1: 병존). 레이아웃(사이클3) /
// 드로우(사이클5) / 히트테스트(사이클7) 가 이 매니저를 통해 활성 Canvas 목록에 접근한다.
// UObject 가 아니므로 F 접두사(UUIManager 와 혼동 방지).
class FUICanvasManager : public TSingleton<FUICanvasManager>, public FGCObject
{
	friend class TSingleton<FUICanvasManager>;

public:
	void RegisterCanvas(UUICanvas* Canvas);
	void UnregisterCanvas(UUICanvas* Canvas);
	const TArray<UUICanvas*>& GetCanvases() const { return Canvases; }

	// [씬 전환 정리] 등록된 모든 Canvas 를 비운다. 전역 싱글톤이라 월드 경계를 넘어 잔여 캔버스가
	// 남으면 다음 씬에 누수되므로, 스탠드얼론 씬 전환에서 호출해 방어적으로 정리한다(정상 경로인
	// 액터 EndPlay→UnregisterCanvas 의 보강). 캔버스 UObject 는 GC 소유 — 여기선 등록 목록만 비운다.
	void ClearCanvases() { Canvases.clear(); }

	// 액터 없이 런타임에서 직접 Canvas 를 만들 때(테스트 / HUD 부트스트랩). 매니저가 keepalive 한다.
	UUICanvas* CreateCanvas();

	// 레이아웃 패스 — 등록된 모든 Canvas 를 top-down 전체 재계산해 노드별 화면 사각형을
	// 갱신한다(진단 C1/C3/C4). UEngine::TickFrameBody 의 WorldTick 과 Render 사이에서 호출된다.
	void LayoutAll();

	// 등록 없이 단일 캔버스 트리만 레이아웃한다(에디터 전용 seam, 진단 §C). 전역 레지스트리/
	// GlobalScale 에 영향을 주지 않으므로 런타임 LayoutAll/SimpleUIPass 와 격리된다.
	// bSyncExternal=false(기본) → 에디터 전용 seam: OnLayoutUpdated 의 외부(RmlUi) 동기화를 건너뛴다(R1).
	void LayoutCanvas(UUICanvas* Canvas, float Scale, bool bSyncExternal = false);

	// 화면/레퍼런스 해상도 스케일. 레이아웃 픽셀 결과에 곱한다(진단 D3). 사이클 4에서 설정.
	float GetGlobalScale() const { return GlobalScale; }
	void SetGlobalScale(float InScale) { GlobalScale = InScale; }

	// --- 런타임 드래그 에디터(진단 E, 사이클 7) ---
	// 매 프레임 토글키/마우스 드래그를 처리한다. LayoutAll 직전(드래그가 position 을 바꾸면
	// 같은 프레임에 재레이아웃되도록)에 호출된다. 입력은 raw InputSystem 에서 직접 읽는다.
	void TickEditor();
	bool IsEditorMode() const { return bEditorMode; }
	void SetEditorMode(bool bOn) { bEditorMode = bOn; if (!bOn) { GrabbedElement = nullptr; } }

	// [버튼 액션] 런타임 클릭 디스패치 — 마우스 down/up 으로 버튼 클릭을 판정해 액션을 실행한다.
	// GameViewportClient::ProcessInput 가 매 프레임 호출(게임/PIE). 에디터 드래그 모드/편집 월드(등록
	// 캔버스 없음)에선 no-op. 커서가 버튼 위면 ConsumedMouseThisFrame()=true → 게임 마우스 입력 억제.
	void TickRuntimeInput();
	bool ConsumedMouseThisFrame() const { return bConsumedMouseThisFrame; }

	// 마우스(클라이언트 px, 좌상단 원점) 아래의 최상위 가시 Element 를 반환(진단 E1).
	UUIElement* HitTest(const FVector2& MousePos) const;

	// 등록 없이 단일 캔버스 트리에만 히트테스트한다(에디터 전용 seam). MousePos 는 캔버스
	// 원점(0,0) 기준 화면 px(=뷰포트 마우스 - 캔버스 화면 원점). 전역 레지스트리 미사용.
	UUIElement* HitTestCanvas(UUICanvas* Canvas, const FVector2& MousePos) const;

	// FGCObject — 등록된 Canvas(및 그 자식 트리)를 GC sweep 으로부터 보호한다.
	// 각 노드의 자식은 USceneComponent::AddReferencedObjects 가 재귀로 보고한다(진단 A1).
	void AddReferencedObjects(FReferenceCollector& Collector) override;
	const char* GetReferencerName() const override { return "FUICanvasManager"; }

private:
	FUICanvasManager() = default;
	~FUICanvasManager() = default;

	// 한 노드의 화면 사각형을 계산하고 자식으로 재귀(top-down). 좌표는 레퍼런스 공간에서
	// 누적하고 마지막에 Scale 을 곱해 화면 픽셀로 변환한다(진단 C2/C3/D3).
	static void LayoutElement(UUIElement* Element, const FVector2& ParentOrigin,
	                          const FVector2& ParentSize, float Scale, bool bSyncExternal);

	// 트리를 pre-order(그린 순서)로 순회하며 마우스를 포함하는 가시 Element 중 마지막(=최상위)을
	// OutTop 에 남긴다(진단 E1: 최후-그림/최심 우선).
	static void HitTestRecursive(UUIElement* Element, const FVector2& MousePos, UUIElement*& OutTop);

	TArray<UUICanvas*> Canvases;
	float GlobalScale = 1.0f;

	// 드래그 에디터 상태.
	bool bEditorMode = false;
	TWeakObjectPtr<UUIElement> GrabbedElement;

	// [버튼 액션] 런타임 클릭 판정 상태. PressedElement = LBUTTON down 시 잡은 요소(같은 요소에서 up
	// 해야 클릭 성립). bConsumedMouseThisFrame = 이번 프레임 커서가 버튼 위였는지(게임 입력 억제 신호).
	TWeakObjectPtr<UUIElement> PressedElement;
	bool bConsumedMouseThisFrame = false;
};
