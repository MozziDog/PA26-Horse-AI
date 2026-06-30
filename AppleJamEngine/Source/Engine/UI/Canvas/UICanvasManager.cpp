#include "UI/Canvas/UICanvasManager.h"

#include "UI/Canvas/UICanvas.h"
#include "UI/Canvas/UIElement.h"
#include "UI/Canvas/UILabel.h"
#include "UI/Canvas/UIButton.h"
#include "UI/Canvas/UIImage.h"
#include "UI/Canvas/UICanvasActor.h"
#include "Object/Object.h"
#include "Input/InputSystem.h"
#include "Runtime/Engine.h"          // GEngine, RequestTransitionToScene (ChangeScene 액션)
#include "Lua/LuaScriptManager.h"    // CallLua 액션 / (b) 콜백 호출
#include "Core/ScoreManager.h"       // ShowScoreboard 액션 — 점수 내림차순 텍스트
#include "UI/UserWidget.h"           // ShowScoreboard 액션 — RmlUi 점수판 위젯
#include "UI/UIManager.h"            // ShowScoreboard 액션 — CreateWidget/RemoveFromViewport

void FUICanvasManager::RegisterCanvas(UUICanvas* Canvas)
{
	if (!Canvas)
	{
		return;
	}
	for (UUICanvas* Existing : Canvases)
	{
		if (Existing == Canvas)
		{
			return;
		}
	}
	Canvases.push_back(Canvas);
}

void FUICanvasManager::UnregisterCanvas(UUICanvas* Canvas)
{
	for (auto It = Canvases.begin(); It != Canvases.end(); ++It)
	{
		if (*It == Canvas)
		{
			Canvases.erase(It);
			return;
		}
	}
}

UUICanvas* FUICanvasManager::CreateCanvas()
{
	UUICanvas* Canvas = UObjectManager::Get().CreateObject<UUICanvas>();
	RegisterCanvas(Canvas);
	return Canvas;
}

void FUICanvasManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	// UUICanvas* → UObject* 암시적 업캐스트. 각 Canvas 의 자식 트리는
	// USceneComponent::AddReferencedObjects 가 별도로 보고한다.
	Collector.AddReferencedObjects(Canvases, "UICanvas");
}

void FUICanvasManager::LayoutAll()
{
	for (UUICanvas* Canvas : Canvases)
	{
		if (!Canvas)
		{
			continue;
		}
		// 루트는 origin=(0,0) 에서 시작하며, 부모 크기는 Canvas 자신의 레퍼런스 크기다.
		// Canvas 의 anchor/pivot/position 은 기본 0 이라 FinalPos 도 (0,0) 이 된다.
		const FVector2 CanvasSize = Canvas->GetRectTransform().Size;
		// 런타임 패스 → 외부(RmlUi) 동기화 허용(bSyncExternal=true).
		LayoutElement(Canvas, FVector2(0.0f, 0.0f), CanvasSize, GlobalScale, /*bSyncExternal=*/true);
	}
}

void FUICanvasManager::LayoutCanvas(UUICanvas* Canvas, float Scale, bool bSyncExternal)
{
	if (!Canvas)
	{
		return;
	}
	// LayoutAll 과 동일 규칙(origin=(0,0), parentSize=Canvas 레퍼런스 크기)으로 한 트리만 계산.
	// 에디터 호출은 bSyncExternal=false(기본) → RmlUi 마운트/동기화 스킵(R1).
	const FVector2 CanvasSize = Canvas->GetRectTransform().Size;
	LayoutElement(Canvas, FVector2(0.0f, 0.0f), CanvasSize, Scale, bSyncExternal);
}

void FUICanvasManager::LayoutElement(UUIElement* Element, const FVector2& ParentOrigin,
                                     const FVector2& ParentSize, float Scale, bool bSyncExternal)
{
	if (!Element)
	{
		return;
	}

	const FUIRectTransform& RT = Element->GetRectTransform();

	// 레퍼런스 좌표 공간(좌상단 원점, Y-down)에서 이 노드의 좌상단 위치(진단 C2).
	//   AnchorPx = ParentSize * anchor       (성분별)
	//   FinalPos = ParentOrigin + AnchorPx + position - (size * pivot)   (성분별)
	const FVector2 AnchorPx = ComponentMul(ParentSize, RT.Anchor);
	const FVector2 FinalPos = ParentOrigin + AnchorPx + RT.Position - ComponentMul(RT.Size, RT.Pivot);

	// 화면 사각형 = 레퍼런스 결과에 GlobalScale 적용(진단 D3). 드로우/히트테스트가 이 값을 쓴다.
	FUIRect Screen;
	Screen.Pos = FinalPos * Scale;
	Screen.Size = RT.Size * Scale;
	Element->SetScreenRect(Screen);

	// 화면 위치 종속 외부 리소스 동기화 훅(예: UUITextElement 의 RmlUi 텍스트). bSyncExternal 게이트(R1).
	Element->OnLayoutUpdated(Scale, bSyncExternal);

	// 자식은 이 노드의 레퍼런스 좌상단/크기를 부모 기준으로 받아 top-down 누적(진단 C3).
	for (USceneComponent* Child : Element->GetChildren())
	{
		if (UUIElement* ChildElement = Cast<UUIElement>(Child))
		{
			LayoutElement(ChildElement, FinalPos, RT.Size, Scale, bSyncExternal);
		}
	}
}

void FUICanvasManager::HitTestRecursive(UUIElement* Element, const FVector2& MousePos, UUIElement*& OutTop)
{
	if (!Element)
	{
		return;
	}
	// [show/off] 숨긴 요소는 자신과 하위 트리 전체를 히트테스트에서 제외 — visible 일 때만 클릭 가능.
	// 렌더 게이트(SimpleUIPass::CollectVisible / 미러 DrawElement)와 동일 규칙: 안 보이는 요소는 클릭·
	// 마우스 소비 대상이 아니다. 조기 반환으로 자식 재귀도 중단 → 숨긴 부모의 자식도 비클릭이 된다.
	if (!Element->IsVisible())
	{
		return;
	}
	// 가시 사각형이고 마우스를 포함하면 후보. pre-order 라 나중에 그린(=위에 있는) 것이 덮어쓴다.
	if (Element->IsVisibleRect() && Element->GetScreenRect().Contains(MousePos))
	{
		OutTop = Element;
	}
	for (USceneComponent* Child : Element->GetChildren())
	{
		if (UUIElement* ChildElement = Cast<UUIElement>(Child))
		{
			HitTestRecursive(ChildElement, MousePos, OutTop);
		}
	}
}

UUIElement* FUICanvasManager::HitTest(const FVector2& MousePos) const
{
	UUIElement* Top = nullptr;
	for (UUICanvas* Canvas : Canvases)
	{
		HitTestRecursive(Canvas, MousePos, Top);
	}
	return Top;
}

UUIElement* FUICanvasManager::HitTestCanvas(UUICanvas* Canvas, const FVector2& MousePos) const
{
	UUIElement* Top = nullptr;
	HitTestRecursive(Canvas, MousePos, Top);
	return Top;
}

void FUICanvasManager::TickEditor()
{
	InputSystem& Input = InputSystem::Get();

	// 에디터 모드 토글 (F9). 토글 시 현재 잡고 있던 대상 해제.
	if (Input.GetKeyDown(VK_F9))
	{
		bEditorMode = !bEditorMode;
		GrabbedElement = nullptr;
	}

	if (!bEditorMode)
	{
		GrabbedElement = nullptr;
		return;
	}

	const POINT MP = Input.GetMouseClientPos();   // 클라이언트 px, 좌상단 원점(진단 A5/E1)
	const FVector2 MousePos(static_cast<float>(MP.x), static_cast<float>(MP.y));

	if (Input.GetKeyDown(VK_LBUTTON))
	{
		// 누른 순간 — 마우스 아래 최상위 가시 Element 를 잡는다.
		GrabbedElement = HitTest(MousePos);
	}
	else if (Input.GetKey(VK_LBUTTON))
	{
		// 드래그 중 — anchor/pivot 고정, position 만 갱신(진단 E2).
		// 마우스 델타는 스크린 px 이므로 GlobalScale 로 나눠 레퍼런스 px 로 환산해 더한다.
		if (UUIElement* Element = GrabbedElement.Get())
		{
			const float Scale = (GlobalScale > 0.0f) ? GlobalScale : 1.0f;
			const FVector2 Delta(static_cast<float>(Input.MouseDeltaX()) / Scale,
			                     static_cast<float>(Input.MouseDeltaY()) / Scale);
			Element->SetPosition(Element->GetPosition() + Delta);
		}
	}
	else
	{
		GrabbedElement = nullptr;
	}
}

// [점수판] Board 버튼이 토글하는 RmlUi 점수판 위젯. 한 번 만들어 재사용(토글 시 viewport add/remove).
static TWeakObjectPtr<UUserWidget> GScoreboardWidget;

// [버튼 액션] 클릭된 버튼의 OnClickActions 를 순서대로 실행. 대상 요소는 버튼의 소유 액터
// (AUICanvasActor)의 캔버스 루트에서 ElementName 으로 찾는다(FindByName).
static void ExecuteButtonAction(UUIButton* Btn)
{
	if (!Btn)
	{
		return;
	}
	UUICanvas* Root = nullptr;
	if (AUICanvasActor* Owner = Cast<AUICanvasActor>(Btn->GetOwner()))
	{
		Root = Owner->GetCanvas();
	}
	for (const FUIButtonAction& A : Btn->GetOnClickActions())
	{
		switch (A.Action)
		{
		case EUIButtonAction::ChangeScene:
			if (GEngine) { GEngine->RequestTransitionToScene(A.Target); }
			break;
		// 요소 전체 표시 토글은 bVisible(IsVisible/SetVisible) 기준 — 렌더 게이트(SimpleUIPass/미러)·
		// 히트테스트·UI 에디터 show/off 토글과 동일 플래그. bVisibleRect(배경 쿼드만)와 구분(예전엔 이 쪽을
		// 토글했으나, bVisible 로 숨긴 요소를 못 켜는 불일치가 있어 통일).
		case EUIButtonAction::ShowElement:
			if (UUIElement* E = Root ? Root->FindByName(A.Target) : nullptr) { E->SetVisible(true); }
			break;
		case EUIButtonAction::HideElement:
			if (UUIElement* E = Root ? Root->FindByName(A.Target) : nullptr) { E->SetVisible(false); }
			break;
		case EUIButtonAction::ToggleElement:
			if (UUIElement* E = Root ? Root->FindByName(A.Target) : nullptr) { E->SetVisible(!E->IsVisible()); }
			break;
		case EUIButtonAction::SetImage:
			if (UUIImage* Img = Cast<UUIImage>(Root ? Root->FindByName(A.Target) : nullptr)) { Img->SetTexturePath(A.Param); }
			break;
		case EUIButtonAction::CallLua:
			FLuaScriptManager::RunScriptFile(A.Target);
			break;
		case EUIButtonAction::QuitGame:
			PostQuitMessage(0);  // WM_QUIT — FEngineLoop 가 PumpMessages 에서 잡아 정상 shutdown
			break;
		case EUIButtonAction::ShowScoreboard:
		{
			// Scores.json 내림차순 점수판(RmlUi 위젯)을 토글. 표시할 때마다 최신 점수로 채운다.
			UUserWidget* W = GScoreboardWidget.Get();
			if (W && W->IsInViewport())
			{
				UUIManager::Get().RemoveFromViewport(W);   // 토글 오프
			}
			else
			{
				if (!W)
				{
					W = UUIManager::Get().CreateWidget(nullptr, FString("Content/UI/Scoreboard.rml"));
					GScoreboardWidget = W;
				}
				if (W)
				{
					W->AddToViewport(2000);
					W->SetText(FString("scorelist"), FScoreManager::Get().BuildScoreboardText());
				}
			}
			break;
		}
		case EUIButtonAction::None:
		default:
			break;
		}
	}
}

void FUICanvasManager::TickRuntimeInput()
{
	bConsumedMouseThisFrame = false;
	// 에디터 드래그 모드(F9) 중이거나 등록 캔버스가 없으면(편집 월드) 디스패치하지 않는다.
	if (bEditorMode || Canvases.empty())
	{
		PressedElement = nullptr;
		return;
	}

	InputSystem& Input = InputSystem::Get();
	const POINT MP = Input.GetMouseClientPos();
	const FVector2 MousePos(static_cast<float>(MP.x), static_cast<float>(MP.y));

	UUIElement* Hovered = HitTest(MousePos);
	if (Cast<UUIButton>(Hovered))
	{
		bConsumedMouseThisFrame = true;   // 버튼 위 → 게임 마우스 입력 억제(ProcessInput 가 참조)
	}

	if (Input.GetKeyDown(VK_LBUTTON))
	{
		PressedElement = Hovered;         // 누른 순간의 요소 기록
	}
	else if (Input.GetKeyUp(VK_LBUTTON))
	{
		// down 한 버튼과 같은 버튼 위에서 up 해야 클릭 성립(드래그/오발 방지).
		if (UUIButton* Btn = Cast<UUIButton>(Hovered))
		{
			if (Btn == PressedElement.Get())
			{
				ExecuteButtonAction(Btn);                                        // (a) 직렬화 액션
				FLuaScriptManager::InvokeUIButtonCallback(Btn->GetElementName());  // (b) Lua 콜백
			}
		}
		PressedElement = nullptr;
	}
}
