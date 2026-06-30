#pragma once

#include "UI/Canvas/UITextElement.h"

#include "Source/Engine/UI/Canvas/UILabel.generated.h"

// "배경 없는 텍스트" 팔레트 프리셋 — bVisibleRect=false 라 단색 쿼드는 안 그리고 텍스트만 보인다.
// 텍스트 데이터/렌더(RmlUi 마운트·동기화)는 모두 중간 클래스 UUITextElement 가 담당하므로, 이 클래스는
// "텍스트, 배경 없음" 기본값만 세팅하는 생성자 전용 프리셋이다(Button/Image 와 형제, 진단 핵심질문1 결론 (c)).
UCLASS()
class UUILabel : public UUITextElement
{
public:
	GENERATED_BODY()
	UUILabel()
	{
		SetVisibleRect(false);
		SetText("Text");   // 팔레트 스폰 직후 보이도록 기본 텍스트(빈 컨테이너와 달리 텍스트 프리셋).
	}
};
