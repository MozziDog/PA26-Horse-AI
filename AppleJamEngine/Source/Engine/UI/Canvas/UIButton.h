#pragma once

#include "UI/Canvas/UITextElement.h"
#include "Core/Types/CoreTypes.h"

#include "Source/Engine/UI/Canvas/UIButton.generated.h"

// [버튼 액션] 클릭 시 실행할 동작의 종류.
UENUM()
enum class EUIButtonAction : uint8
{
	None,           // 동작 없음(기본)
	ChangeScene,    // Target = .Scene 경로/이름           → GEngine->RequestTransitionToScene
	ShowElement,    // Target = 대상 ElementName           → SetVisible(true)  (요소 전체 표시 = bVisible)
	HideElement,    // Target = 대상 ElementName           → SetVisible(false) (요소 전체 숨김 = bVisible)
	ToggleElement,  // Target = 대상 ElementName           → bVisible 가시성 토글
	SetImage,       // Target = 대상 ElementName(UUIImage), Param = 이미지 경로 → SetTexturePath
	CallLua,        // Target = Lua 전역 함수명            → 함수 호출
	QuitGame,       // 프로그램 종료(PostQuitMessage). Target/Param 미사용.
	ShowScoreboard, // Scores.json 내림차순 점수판(RmlUi 위젯) 토글. Target/Param 미사용.
};

// [버튼 액션] 클릭 동작 1건. UI 에디터 Details 의 "On Click Actions" 배열에서 행 단위로 편집한다
// (AUICanvasActor 의 FHudBinding 직렬화 패턴 동형 — USTRUCT + Type=Array,Struct= 로 .uasset 영속).
USTRUCT()
struct FUIButtonAction
{
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category="UI|Action", DisplayName="Action", Enum=EUIButtonAction)
	EUIButtonAction Action = EUIButtonAction::None;

	// 액션별 대상: 씬 경로 / 대상 ElementName / Lua 함수명.
	UPROPERTY(Edit, Save, Category="UI|Action", DisplayName="Target")
	FString Target;

	// 보조 파라미터(현재 SetImage 의 이미지 경로). 다른 액션은 미사용.
	UPROPERTY(Edit, Save, Category="UI|Action", DisplayName="Param")
	FString Param;
};

// 단색 버튼 요소(팔레트 3종). 가시 rect 유지(베이스 기본 bVisibleRect=true) → 단색 쿼드를 그린다.
// 텍스트(내용·폰트·정렬·색)는 중간 클래스 UUITextElement 에서 상속한다.
// 클릭하면 OnClickActions 의 액션들을 순서대로 실행한다(다중 액션). 런타임 디스패치는 FUICanvasManager.
UCLASS()
class UUIButton : public UUITextElement
{
public:
	GENERATED_BODY()
	UUIButton()
	{
		SetSize(FVector2(200.0f, 80.0f));
		SetColor(FVector4(0.25f, 0.55f, 0.32f, 1.0f));
	}

	// 클릭 액션 배열(다중). 런타임 디스패처가 클릭 시 순서대로 실행한다.
	const TArray<FUIButtonAction>& GetOnClickActions() const { return OnClickActions; }
	TArray<FUIButtonAction>& GetOnClickActionsMutable() { return OnClickActions; }

private:
	UPROPERTY(Edit, Save, Category="UI|Action", DisplayName="On Click Actions", Type=Array, Struct=FUIButtonAction)
	TArray<FUIButtonAction> OnClickActions;
};
