#pragma once

#include "UI/Canvas/UIElement.h"

#include "Source/Engine/UI/Canvas/UICanvas.generated.h"

// 계층형 UI 트리의 루트. origin=(0,0) 에서 레이아웃이 시작된다(진단 C3).
// Size 는 레퍼런스 해상도(기본 1920x1080; 사이클 4에서 RefRes/GlobalScale 와 연동).
// Canvas 자신은 사각형을 그리지 않는다.
UCLASS()
class UUICanvas : public UUIElement
{
public:
	GENERATED_BODY()
	UUICanvas()
	{
		SetVisibleRect(false);
		RectTransform.Size = FVector2(1920.0f, 1080.0f);
	}
};
