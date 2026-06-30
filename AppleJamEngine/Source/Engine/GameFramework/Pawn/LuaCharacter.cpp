#include "GameFramework/Pawn/LuaCharacter.h"

#include "Component/Camera/CameraComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "GameFramework/World.h"
#include "UI/Canvas/UICanvasActor.h"
#include "UI/Canvas/UICanvas.h"
#include "UI/Canvas/UIElement.h"
#include "Runtime/Engine.h"
#include "Viewport/GameViewportClient.h"

namespace
{
	// 요소와 모든 하위 요소의 bVisible 를 일괄 설정(서브트리 통째로 표시/숨김).
	void SetUISubtreeVisible(UUIElement* Element, bool bVisible)
	{
		if (!Element)
		{
			return;
		}
		Element->SetVisible(bVisible);
		for (USceneComponent* Child : Element->GetChildren())
		{
			if (UUIElement* UIChild = Cast<UUIElement>(Child))
			{
				SetUISubtreeVisible(UIChild, bVisible);
			}
		}
	}

	// 모든 캔버스에서 ElementName 이 일치하는 요소를 찾아 그 서브트리를 표시. 하나라도 찾으면 true.
	bool ShowNamedElementSubtree(UWorld* World, const FString& Name)
	{
		if (!World)
		{
			return false;
		}
		bool bFound = false;
		for (AActor* Actor : World->GetActors())
		{
			AUICanvasActor* CanvasActor = Cast<AUICanvasActor>(Actor);
			if (!CanvasActor)
			{
				continue;
			}
			UUICanvas* Canvas = CanvasActor->GetCanvas();
			if (!Canvas)
			{
				continue;
			}
			if (UUIElement* El = Canvas->FindByName(Name))
			{
				SetUISubtreeVisible(El, true);
				bFound = true;
			}
		}
		return bFound;
	}
}

void ALuaCharacter::InitDefaultComponents(const FString& SkeletalMeshFileName, const FString& ScriptFile)
{
	Super::InitDefaultComponents(SkeletalMeshFileName);

	// 3인칭 카메라 체인 — Capsule → SpringArm → Camera. lag 적용해 부드럽게 따라옴.
	SpringArm = AddComponent<USpringArmComponent>();
	SpringArm->AttachToComponent(CapsuleComponent);
	SpringArm->TargetArmLength       = 10.0f;
	SpringArm->SocketOffset          = FVector(0.0f, 0.0f, 3.0f);
	SpringArm->bEnableCameraLag      = true;
	SpringArm->bEnableCameraRotationLag = true;

	// mouse look 이 capsule rotation 안 건드리고 카메라만 회전 — UE ThirdPerson 패턴.
	// ACharacter::Tick 이 APawn::ControlRotation 누적 → SpringArm 이 이걸 inherit.
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bInheritPitch           = true;
	SpringArm->bInheritYaw             = true;
	SpringArm->bInheritRoll            = false;

	Camera = AddComponent<UCameraComponent>();
	Camera->AttachToComponent(SpringArm);

	LuaScriptComponent = AddComponent<ULuaScriptComponent>();
	if (!ScriptFile.empty())
	{
		LuaScriptComponent->SetScriptFile(ScriptFile);
	}
}

void ALuaCharacter::PostDuplicate()
{
	Super::PostDuplicate();
	LuaScriptComponent = GetComponentByClass<ULuaScriptComponent>();
	SpringArm          = GetComponentByClass<USpringArmComponent>();
	Camera             = GetComponentByClass<UCameraComponent>();
}

void ALuaCharacter::OnPostLoad(FArchive& Ar)
{
	Super::OnPostLoad(Ar);
	LuaScriptComponent = GetComponentByClass<ULuaScriptComponent>();
	SpringArm          = GetComponentByClass<USpringArmComponent>();
	Camera             = GetComponentByClass<UCameraComponent>();
}

void ALuaCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 빙의된 플레이어가 죽으면(HP=0 첫 프레임) 사망 UI 표시 + 마우스 자유(보스 클리어와 대칭). 1회만.
	if (!bDeathUIShown && IsPossessed() && GetCurrentHealth() <= 0.0f)
	{
		bDeathUIShown = true;
		OnPlayerDeath();
	}
}

void ALuaCharacter::OnPlayerDeath()
{
	UWorld* World = GetWorld();

	// 사망 UI 표시 — DeathUI 패널 + 자식 CharacterDeathReturnTitle. 자식이면 서브트리로 함께 켜지고,
	// 형제여도 안전하게 명시적으로 한 번 더 켠다. (체력 HUD 등 다른 요소는 건드리지 않음.)
	ShowNamedElementSubtree(World, FString("DeathUI"));
	ShowNamedElementSubtree(World, FString("CharacterDeathReturnTitle"));

	// 마우스를 카메라(마우스룩)에서 분리하고 커서를 풀어 UI 클릭 가능하게 (보스 클리어와 동일).
	if (GEngine)
	{
		if (UGameViewportClient* Viewport = GEngine->GetGameViewportClient())
		{
			Viewport->SetInputMode(EGameInputMode::UIOnly);
		}
	}
}
