#include "UI/Canvas/UITextElement.h"

#include "UI/UIManager.h"
#include "UI/UserWidget.h"

#include <string>

// 텍스트 RmlUi 마운트/동기화(사이클 ②에서 UILabel.cpp 에서 이전). RmlUi 의존은 이 .cpp 에만 격리.
namespace
{
	// RmlUi 텍스트 템플릿(루트 상대 경로). LoadDocument 가 존재 여부를 확인한다.
	constexpr const char* TextDocPath = "Content/UI/SimpleUILabel.rml";

	FString ToDp(float Value)
	{
		return FString(std::to_string(Value)) + "dp";
	}

	// FVector4(0..1 RGBA) → RmlUi color 문자열 "rgba(r,g,b,a)"(각 0..255 정수). 디테일 ColorEdit4 와 정합.
	FString ToRmlColor(const FVector4& C)
	{
		auto To255 = [](float V) -> int
		{
			int I = static_cast<int>(V * 255.0f + 0.5f);
			if (I < 0) I = 0;
			if (I > 255) I = 255;
			return I;
		};
		const std::string S = "rgba(" + std::to_string(To255(C.R)) + "," + std::to_string(To255(C.G)) + ","
			+ std::to_string(To255(C.B)) + "," + std::to_string(To255(C.A)) + ")";
		return FString(S);
	}
}

void UUITextElement::OnLayoutUpdated(float GlobalScale, bool bSyncExternal)
{
	// [R1] 외부(RmlUi) 동기화는 런타임 LayoutAll(bSyncExternal=true)에서만. 에디터 LayoutCanvas
	// (false)는 RmlUi 가 게임 viewport 로 새는 것을 막기 위해 바로 빠진다(에디터 텍스트는 사이클 ③의
	// ImGui AddText 미러로 그림).
	if (!bSyncExternal)
	{
		return;
	}

	// [show/off] 자신 또는 조상이 숨김이면 텍스트도 숨긴다. 렌더/미러는 서브트리를 끊지만 레이아웃 경로는
	// 항상 트리를 돌므로(여기서 OnLayoutUpdated 가 불림) 실효 가시성으로 판정해 위젯을 빈 라벨로 비운다.
	if (!IsEffectivelyVisible())
	{
		if (UUserWidget* Existing = Widget.Get())
		{
			Existing->SetText("label", FString());
		}
		return;
	}

	// [R2] 비빈-텍스트 가드: Text 가 비어있으면 마운트하지 않는다. 빈 동안 bMountAttempted 를 latch 하지
	// 않으므로 나중에 텍스트가 채워지면 그때 마운트된다. 이미 위젯이 있으면 빈 텍스트로 비워 숨긴다.
	if (Text.empty())
	{
		if (UUserWidget* Existing = Widget.Get())
		{
			Existing->SetText("label", FString());
		}
		return;
	}

	// 최초 1회 RmlUi 위젯 생성 + viewport 등록. 높은 ZOrder 로 RmlUi 레이어 내 위쪽에 둔다.
	if (!Widget.Get() && !bMountAttempted)
	{
		bMountAttempted = true;
		if (UUserWidget* NewWidget = UUIManager::Get().CreateWidget(nullptr, TextDocPath))
		{
			NewWidget->AddToViewport(1000);
			Widget = NewWidget;
		}
	}

	UUserWidget* W = Widget.Get();
	if (!W)
	{
		return;
	}

	// ScreenRect(스크린 px) → 레퍼런스 dp 로 환산. dp_ratio = GlobalScale 라 dp 좌표가 ScreenRect 와 1:1.
	const float Scale = (GlobalScale > 0.0f) ? GlobalScale : 1.0f;
	const FUIRect& R = GetScreenRect();

	W->SetText("label", Text);
	W->SetProperty("label", "left", ToDp(R.Pos.X / Scale));
	W->SetProperty("label", "top", ToDp(R.Pos.Y / Scale));

	// 텍스트 5속성 푸시. 기본값이 SimpleUILabel.rml CSS 와 일치(20dp/bold/left/white)하므로 속성을
	// 따로 안 건드린 기존 라벨도 외형 회귀 0(R4). 디테일 패널(사이클 ③)이 멤버를 바꾸면 다음 프레임 반영.
	W->SetProperty("label", "font-size", ToDp(FontSize));
	W->SetProperty("label", "font-weight", FontWeight);
	W->SetProperty("label", "text-align", TextAlign);
	W->SetProperty("label", "color", ToRmlColor(TextColor));
}

void UUITextElement::BeginDestroy()
{
	if (UUserWidget* W = Widget.Get())
	{
		UUIManager::Get().RemoveFromViewport(W);
	}
	Super::BeginDestroy();
}
