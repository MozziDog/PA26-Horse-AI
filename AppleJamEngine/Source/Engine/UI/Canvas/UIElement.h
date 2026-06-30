#pragma once

#include "Component/SceneComponent.h"
#include "UI/Canvas/UIRect.h"
#include "UI/Canvas/UIRectTransform.h"

#include "Source/Engine/UI/Canvas/UIElement.generated.h"

// 요소의 외곽 모양. Rectangle=저작한 CornerRadius 사용(0=직각). Circle=짧은 변의 절반을 반지름으로
// 하는 완전 둥근꼴(정사각형이면 원, 직사각형이면 스타디움). 둘 다 기존 둥근사각형 SDF 한 경로로 렌더.
UENUM()
enum class EUIElementShape : uint8
{
	Rectangle,
	Circle,
};

// 계층형 UI 트리의 기본 노드.
// USceneComponent 를 상속해 부모-자식 트리 / 사이클검사 / GC keepalive 를 재사용하되(진단 A2),
// 레이아웃은 3D RelativeTransform 이 아니라 FUIRectTransform 으로만 한다(진단 C5).
// 화면 사각형(ScreenRect)은 레이아웃 패스가 매 프레임 top-down 으로 채운다(진단 C3, 사이클 3).
UCLASS()
class UUIElement : public USceneComponent
{
public:
	GENERATED_BODY()
	UUIElement() { SetComponentTickEnabled(false); }

	FUIRectTransform& GetRectTransform() { return RectTransform; }
	const FUIRectTransform& GetRectTransform() const { return RectTransform; }

	void SetPosition(const FVector2& InPosition) { RectTransform.Position = InPosition; }
	void SetSize(const FVector2& InSize) { RectTransform.Size = InSize; }
	void SetAnchor(const FVector2& InAnchor) { RectTransform.Anchor = InAnchor; }
	void SetPivot(const FVector2& InPivot) { RectTransform.Pivot = InPivot; }

	FVector2 GetPosition() const { return RectTransform.Position; }
	FVector2 GetSize() const { return RectTransform.Size; }

	// 바인딩/조회용 사용자 식별자(예: "HealthBar"). 컴포넌트의 UObject 이름은 직렬화되지 않고
	// 클래스명 기반이라 고유하지 않으므로(SerializeSceneComponentTree 는 Name 미기록), 별도
	// Save 필드로 식별자를 보존한다. UI 에디터 Details 에서 편집한다(데이터 바인딩 사이클 1).
	void SetElementName(const FString& InName) { ElementName = InName; }
	const FString& GetElementName() const { return ElementName; }

	// 이 노드와 하위 트리에서 ElementName 이 일치하는 첫 요소를 반환(없으면 nullptr). 빈 이름은 매칭 제외.
	// 바인딩 컴포넌트가 캔버스 루트에서 대상 엘리먼트(예: 체력바)를 찾을 때 사용.
	UUIElement* FindByName(const FString& InName)
	{
		if (!InName.empty() && ElementName == InName)
		{
			return this;
		}
		for (USceneComponent* Child : GetChildren())
		{
			if (UUIElement* UIChild = Cast<UUIElement>(Child))
			{
				if (UUIElement* Found = UIChild->FindByName(InName))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	// 레이아웃 패스가 채우는 화면 사각형(스크린 픽셀, GlobalScale 반영). 사이클 3에서 갱신.
	const FUIRect& GetScreenRect() const { return ScreenRect; }
	void SetScreenRect(const FUIRect& InRect) { ScreenRect = InRect; }

	// 레이아웃 패스가 ScreenRect 를 갱신한 직후 호출되는 훅(기본 no-op). 화면 위치에 종속된 외부
	// 리소스(예: UUITextElement 의 RmlUi 텍스트 위젯)를 동기화할 때 override 한다.
	// bSyncExternal: 런타임 LayoutAll=true / 에디터 LayoutCanvas=false — 에디터에서 RmlUi 가 게임
	// viewport 로 새는 것을 막는 게이트(진단 R1). false 면 외부 리소스 동기화를 수행하지 않아야 한다.
	virtual void OnLayoutUpdated(float GlobalScale, bool bSyncExternal) { (void)GlobalScale; (void)bSyncExternal; }

	// 이 노드가 사각형을 그릴지 여부. Canvas / Group 같은 순수 컨테이너는 false.
	bool IsVisibleRect() const { return bVisibleRect; }
	void SetVisibleRect(bool bVisible) { bVisibleRect = bVisible; }

	// 요소 전체 표시 여부(배경·텍스트·자식 서브트리 모두 포함) — UI 에디터의 show/off 토글이 제어한다.
	// bVisibleRect(배경 쿼드만 끔)와 구분: bVisible=false 면 이 요소와 그 아래가 통째로 렌더에서 빠진다.
	bool IsVisible() const { return bVisible; }
	void SetVisible(bool bInVisible) { bVisible = bInVisible; }

	// 자신과 모든 조상이 Visible 일 때만 true. 렌더/미러 패스는 숨김 시 조기 반환으로 서브트리를 끊으므로
	// 자기 플래그만으로 충분하지만, 항상 트리를 도는 텍스트(RmlUi) 동기화 경로는 실효 가시성이 필요하다.
	bool IsEffectivelyVisible() const
	{
		const UUIElement* Node = this;
		while (Node != nullptr)
		{
			if (!Node->bVisible)
			{
				return false;
			}
			Node = Cast<UUIElement>(Node->GetParent());
		}
		return true;
	}

	// 단색 배경색(RGBA). 드로우 패스가 ScreenRect 를 이 색의 쿼드로 그린다(사이클 5).
	void SetColor(const FVector4& InColor) { BackgroundColor = InColor; }
	FVector4 GetColor() const { return BackgroundColor; }

	// 모서리 둥글기(레퍼런스 px, 0=직각). 드로우 패스가 ScreenRect 의 쿼드를 둥근 사각형 SDF 로
	// 그릴 때 사용. 화면 px 로는 GlobalScale 을 곱하고, 변의 절반을 넘지 않도록 클램프한다.
	void SetCornerRadius(float InRadius) { CornerRadius = InRadius < 0.0f ? 0.0f : InRadius; }
	float GetCornerRadius() const { return CornerRadius; }

	// 요소 외곽 모양(Rectangle/Circle). UI 에디터 Details 에서 변경한다.
	void SetShape(EUIElementShape InShape) { Shape = InShape; }
	EUIElementShape GetShape() const { return Shape; }

	// 렌더가 실제로 사용할 모서리 반지름(레퍼런스 px). Circle 이면 짧은 변의 절반(드로우 패스/미러가
	// 변의 절반으로 클램프하므로 완전 둥근꼴=원·스타디움), Rectangle 이면 저작한 CornerRadius 그대로.
	// 런타임 SimpleUIPass·에디터 미러·선택 하이라이트가 공통으로 이 값을 쓰므로 모양이 한 곳에서 결정된다.
	float GetEffectiveCornerRadius() const
	{
		if (Shape == EUIElementShape::Circle)
		{
			const float ShorterSide = RectTransform.Size.X < RectTransform.Size.Y ? RectTransform.Size.X : RectTransform.Size.Y;
			return 0.5f * ShorterSide;
		}
		return CornerRadius;
	}

protected:
	// FUIRectTransform 은 USTRUCT 가 아니므로, USceneComponent 가 FTransform 의 하위 필드를
	// Member= 로 반사하는 방식(진단 C5 선례)을 그대로 차용해 4개의 Vec2 를 PF_Save 로 노출한다.
	// → FSceneSaveManager 가 .Scene 에 자동 영속(사이클 8). 토폴로지는 컴포넌트 트리 직렬화가 처리.
	UPROPERTY(Save, Category="RectTransform", DisplayName="Pivot", Member=RectTransform.Pivot, Type=Vec2);
	UPROPERTY(Save, Category="RectTransform", DisplayName="Anchor", Member=RectTransform.Anchor, Type=Vec2);
	UPROPERTY(Save, Category="RectTransform", DisplayName="Position", Member=RectTransform.Position, Type=Vec2);
	UPROPERTY(Save, Category="RectTransform", DisplayName="Size", Member=RectTransform.Size, Type=Vec2);
	FUIRectTransform RectTransform;

	FUIRect ScreenRect;               // 레이아웃 결과 캐시(직렬화 안 함)

	// 사용자 식별자(데이터 바인딩 대상 조회용). 기본 빈 문자열 — 명명된 요소만 바인딩 대상이 된다.
	// Edit: 레벨 아웃라이너 Details(FEditorPropertyWidget, 반사 기반)에서 요소 선택 시 텍스트로 편집.
	// UI 에디터(FUIEditorWidget::RenderDetailsPanel)는 별도 커스텀 컨트롤로 동일 필드를 편집한다.
	UPROPERTY(Edit, Save, Category="UI", DisplayName="Element Name")
	FString ElementName;

	UPROPERTY(Save, Category="UI", DisplayName="Visible Rect")
	bool bVisibleRect = true;

	// 요소 전체 표시 여부(기본 true). false 면 배경·텍스트·자식까지 렌더에서 제외(IsVisible/IsEffectivelyVisible).
	UPROPERTY(Edit, Save, Category="UI", DisplayName="Visible")
	bool bVisible = true;
	UPROPERTY(Save, Category="UI", DisplayName="Background Color", Type=Color4)
	FVector4 BackgroundColor = { 0.2f, 0.4f, 0.8f, 0.7f };

	// 모서리 둥글기(레퍼런스 px). 0 이면 기존과 동일한 직각 쿼드(드로우 패스가 SDF 분기 스킵).
	UPROPERTY(Save, Category="UI", DisplayName="Corner Radius")
	float CornerRadius = 0.0f;

	// 외곽 모양. Rectangle(기본)이면 CornerRadius 를, Circle 이면 짧은 변 절반을 반지름으로 렌더한다
	// (GetEffectiveCornerRadius). UI 에디터 Details 의 Shape 콤보 + 반사 기반 Details(enum 드롭다운) 편집.
	UPROPERTY(Edit, Save, Category="UI", DisplayName="Shape", Enum=EUIElementShape)
	EUIElementShape Shape = EUIElementShape::Rectangle;
};
