#pragma once

#include "UI/Canvas/UIElement.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/UI/Canvas/UITextElement.generated.h"

class UUserWidget;

// 텍스트(내용+폰트+정렬+색)를 가지는 중간 UI 베이스. Button/Image/Label 이 이를 상속한다.
// 순수 컨테이너(Canvas/Group)는 UUIElement 에 직접 남아 텍스트를 갖지 않는다(결정: 중간 클래스 한정)
//  → 빈 컨테이너의 유령 RmlUi 위젯/빈 텍스트 직렬화(진단 R2/R6)가 구조적으로 소멸한다.
// 텍스트는 RmlUi 문서를 마운트해 렌더한다(레이아웃이 정한 화면 위치를 매 프레임 따라감, dp_ratio=
// GlobalScale). RmlUi 의존은 이 클래스 .cpp 에만 격리한다(이 헤더엔 RmlUi include 없음).
UCLASS()
class UUITextElement : public UUIElement
{
public:
	GENERATED_BODY()
	UUITextElement() = default;

	void SetText(const FString& InText) { Text = InText; }
	const FString& GetText() const { return Text; }

	void SetFontSize(float InSize) { FontSize = InSize; }
	float GetFontSize() const { return FontSize; }

	void SetFontWeight(const FString& InWeight) { FontWeight = InWeight; }
	const FString& GetFontWeight() const { return FontWeight; }

	void SetTextAlign(const FString& InAlign) { TextAlign = InAlign; }
	const FString& GetTextAlign() const { return TextAlign; }

	void SetTextColor(const FVector4& InColor) { TextColor = InColor; }
	FVector4 GetTextColor() const { return TextColor; }

	// 레이아웃 갱신 직후 RmlUi 위젯을 생성(최초 1회, Text 비어있지 않을 때만 — R2)하고 위치/텍스트/
	// 폰트/정렬/색을 동기화한다. 에디터(bSyncExternal=false)에선 아무것도 하지 않는다(R1).
	void OnLayoutUpdated(float GlobalScale, bool bSyncExternal) override;
	void BeginDestroy() override;

protected:
	// 텍스트 5속성(UPROPERTY Save). 기본값은 Content/UI/SimpleUILabel.rml CSS 와 정확히 일치(R4):
	//   font-size:20dp · color:#ffffff · (body)font-weight:bold · text-align 미지정=left.
	// → 승격 후에도 기존 라벨 외형 회귀 0. 빈 Text 면 사이클 ②의 마운트 가드로 RmlUi 위젯 미생성.
	// 서브클래스(UILabel 등)가 직접 참조하므로 protected.
	UPROPERTY(Save, Category="Text", DisplayName="Text")
	FString Text;
	UPROPERTY(Save, Category="Text", DisplayName="Font Size")
	float FontSize = 20.0f;
	UPROPERTY(Save, Category="Text", DisplayName="Font Weight")
	FString FontWeight = "bold";
	UPROPERTY(Save, Category="Text", DisplayName="Text Align")
	FString TextAlign = "left";
	UPROPERTY(Save, Category="Text", DisplayName="Text Color", Type=Color4)
	FVector4 TextColor = { 1.0f, 1.0f, 1.0f, 1.0f };

private:
	// RmlUi 브리지 상태(직렬화 안 함). bMountAttempted 는 Text 가 비어있는 동안엔 latch 하지 않아
	// 나중에 텍스트가 채워질 때 마운트되도록 한다(R2). 자세한 로직은 .cpp 의 OnLayoutUpdated 참고.
	TWeakObjectPtr<UUserWidget> Widget;
	bool bMountAttempted = false;
};
