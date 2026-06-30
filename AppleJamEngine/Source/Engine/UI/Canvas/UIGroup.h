#pragma once

#include "UI/Canvas/UIElement.h"

#include "Source/Engine/UI/Canvas/UIGroup.generated.h"

// 순수 그룹 노드 — 자식을 묶어 함께 배치/이동하는 컨테이너. 자체 사각형은 그리지 않는다.
UCLASS()
class UUIGroup : public UUIElement
{
public:
	GENERATED_BODY()
	UUIGroup() { SetVisibleRect(false); }
};
