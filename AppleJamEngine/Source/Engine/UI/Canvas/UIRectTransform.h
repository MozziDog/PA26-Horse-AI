#pragma once

#include "Math/Vector.h"

// 신규 계층형 UI 의 RectTransform — Element 노드가 값으로 보유한다(진단 C5).
// 모든 좌표는 신규 UI 규약(좌상단 원점, Y-down)을 따른다.
//   Pivot    : (0..1) 자기 사각형의 기준점. (0,0)=좌상단.
//   Anchor   : (0..1) 부모 사각형 내 단일 점. (0,0)=부모 좌상단. (Unreal 식 영역 앵커 없음)
//   Position : 픽셀. anchor 기준 오프셋(레퍼런스 해상도 기준값).
//   Size     : 픽셀. 폭/높이(레퍼런스 해상도 기준값).
// USceneComponent::RelativeTransform(3D)은 UI 에서 사용하지 않는다(진단 A2/C5).
struct FUIRectTransform
{
	FVector2 Pivot{ 0.0f, 0.0f };
	FVector2 Anchor{ 0.0f, 0.0f };
	FVector2 Position{ 0.0f, 0.0f };
	FVector2 Size{ 100.0f, 100.0f };
};
