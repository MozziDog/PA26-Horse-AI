#pragma once

#include "Math/Vector.h"

// 경량 2D 사각형 — 신규 계층형 UI 레이아웃 결과를 담는다.
// 좌표계는 엔진 2D 규약(좌상단 원점, Y-down, 픽셀)을 따른다.
//   Pos  = 사각형의 좌상단 모서리(픽셀)
//   Size = 폭/높이(픽셀)
// Editor/Slate 의 FRect 를 런타임 UI 가 끌어오면 Engine→Editor 의존이 생기므로,
// 그 의존을 피하기 위한 Engine 측 전용 타입이다(진단 문서 A4 참조).
struct FUIRect
{
	FVector2 Pos{ 0.0f, 0.0f };
	FVector2 Size{ 0.0f, 0.0f };

	FUIRect() = default;
	FUIRect(const FVector2& InPos, const FVector2& InSize)
		: Pos(InPos)
		, Size(InSize)
	{
	}

	// 점이 사각형 내부에 있는지 — 좌상단 경계 포함, 우하단 경계 제외.
	// 런타임 드래그 에디터의 히트테스트 토대(진단 문서 E1).
	bool Contains(const FVector2& P) const
	{
		return P.X >= Pos.X && P.X < Pos.X + Size.X
			&& P.Y >= Pos.Y && P.Y < Pos.Y + Size.Y;
	}
};
