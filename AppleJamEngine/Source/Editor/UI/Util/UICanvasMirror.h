#pragma once

// 계층형 UI 캔버스 트리를 ImGui DrawList 로 "미러" 렌더하는 에디터 전용 헬퍼.
// UI 에셋 에디터(FUIEditorWidget)와 레벨 뷰포트(FEditorViewportClient) 양쪽이 공유한다.
// RmlUi 를 쓰지 않으므로(bSyncExternal=false 경로) 게임 뷰포트로의 텍스트 누수가 없다(진단 R3).
// 런타임 실 렌더(FSimpleUIPass + RmlUi)와 픽셀 동일하진 않지만, 단색/이미지/둥근모서리/텍스트를
// 모두 표현해 에디터에서 캔버스를 그대로 보고 편집할 수 있게 한다.

#include "UI/Canvas/UIElement.h"
#include "UI/Canvas/UIImage.h"
#include "UI/Canvas/UITextElement.h"
#include "UI/Canvas/UIRect.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Object/Object.h"        // Cast<>

#include <imgui.h>
#include <d3d11.h>

namespace FUICanvasMirror
{
	// 가시 요소(+하위 트리)를 ImGui DrawList 로 그린다. Origin=캔버스 좌상단(스크린 px),
	// Scale=레퍼런스 px → 스크린 px 배율. 호출 전 캔버스를 LayoutCanvas(bSyncExternal=false)로
	// 레이아웃해 ScreenRect 가 갱신돼 있어야 한다.
	inline void DrawElement(UUIElement* Element, ImDrawList* DL, const ImVec2& Origin, float Scale)
	{
		if (!Element)
		{
			return;
		}
		// [show/off] 숨긴 요소는 자신과 하위 트리 전체를 미러에서 제외(조기 반환으로 자식 재귀도 중단).
		if (!Element->IsVisible())
		{
			return;
		}
		if (Element->IsVisibleRect())
		{
			const FUIRect& R = Element->GetScreenRect();
			const FVector4 C = Element->GetColor();
			const ImVec2   Min(Origin.x + R.Pos.X, Origin.y + R.Pos.Y);
			const ImVec2   Max(Min.x + R.Size.X, Min.y + R.Size.Y);
			// 모서리 둥글기 — 런타임 SimpleUIPass 와 동일하게 레퍼런스 px*Scale 로 환산(ImGui 가 변의
			// 절반까지 내부 클램프하므로 런타임 클램프와 결과 일치). 0 이면 직각.
			const float Rounding = Element->GetEffectiveCornerRadius() * Scale;
			const ImU32 Col = ImGui::GetColorU32(ImVec4(C.R, C.G, C.B, C.A));

			// 이미지 요소면 단색 대신 실제 텍스처를 미러(런타임 SimpleUIPass 의 텍스처×Color 변조와
			// 동일). SRV 는 에디터 썸네일 매니저가 경로별 로드/캐시. 경로 없거나 로드 실패면 단색 fallback.
			ID3D11ShaderResourceView* TexSRV = nullptr;
			if (UUIImage* ImgElem = Cast<UUIImage>(Element))
			{
				if (!ImgElem->GetTexturePath().empty())
				{
					TexSRV = FEditorTextureManager::Get().GetOrLoadThumbnail(ImgElem->GetTexturePath());
				}
			}

			if (TexSRV)
			{
				// 텍스처 × BackgroundColor(틴트). 둥근 모서리는 AddImageRounded 로 유지.
				if (Rounding > 0.0f)
				{
					DL->AddImageRounded((ImTextureID)TexSRV, Min, Max, ImVec2(0, 0), ImVec2(1, 1), Col, Rounding);
				}
				else
				{
					DL->AddImage((ImTextureID)TexSRV, Min, Max, ImVec2(0, 0), ImVec2(1, 1), Col);
				}
			}
			else
			{
				DL->AddRectFilled(Min, Max, Col, Rounding);
			}
			DL->AddRect(Min, Max, IM_COL32(0, 0, 0, 60), Rounding);
		}
		// [R5] 텍스트 미러 — bVisibleRect 무관하게 Text 가 있으면 글자를 그린다(배경 없는 Text 프리셋도
		// 에디터에 보이도록). 런타임 텍스트는 RmlUi(에디터에선 R1 게이트로 비활성), 에디터는 이 ImGui 미러.
		// FontSize*Scale·TextColor 는 반영하되, font-weight/align 은 ImGui 기본 폰트라 미반영(런타임만 적용).
		if (UUITextElement* TextElem = Cast<UUITextElement>(Element))
		{
			const FString& Txt = TextElem->GetText();
			if (!Txt.empty())
			{
				const FUIRect& R = Element->GetScreenRect();
				const FVector4 TC = TextElem->GetTextColor();
				float FontPx = TextElem->GetFontSize() * Scale;
				if (FontPx < 1.0f) FontPx = 1.0f;
				const ImVec2 TextPos(Origin.x + R.Pos.X, Origin.y + R.Pos.Y);
				DL->AddText(ImGui::GetFont(), FontPx, TextPos,
				            ImGui::GetColorU32(ImVec4(TC.R, TC.G, TC.B, TC.A)), Txt.c_str());
			}
		}
		for (USceneComponent* Child : Element->GetChildren())
		{
			if (UUIElement* ChildElement = Cast<UUIElement>(Child))
			{
				DrawElement(ChildElement, DL, Origin, Scale);
			}
		}
	}
}
