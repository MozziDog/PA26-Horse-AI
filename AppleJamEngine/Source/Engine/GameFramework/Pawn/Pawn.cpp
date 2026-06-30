#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/SceneComponent.h"
#include "Core/Types/PropertyTypes.h"
#include "Serialization/Archive.h"

#include <algorithm>

void APawn::BeginPlay()
{
	if (bResetHealthOnBeginPlay)
	{
		ResetHealth();
	}

	// InputComponent는 다른 컴포넌트의 BeginPlay보다 먼저 준비한다.
	// LuaBlueprintComponent의 Event InputAction/InputAxis 자동 바인딩은 자기 BeginPlay에서
	// Pawn::GetInputComponent()를 사용하므로, Super::BeginPlay() 전에 생성/Setup되어 있어야 한다.
	if (!InputComponent)
	{
		InputComponent = GetComponentByClass<UInputComponent>();
		if (!InputComponent)
		{
			InputComponent = AddComponent<UInputComponent>();
		}
	}
	if (InputComponent)
	{
		InputComponent->ClearBindings();
	}
	SetupInputComponent();

	Super::BeginPlay();
}

void APawn::ResetHealth()
{
	MaxHealth = (std::max)(0.0f, MaxHealth);
	CurrentHealth = MaxHealth;
	TotalDamageTaken = 0.0f;
	HealthHitCount = 0;
}

float APawn::GetDamaged(float DamageAmount)
{
	const float ClampedDamage = (std::max)(0.0f, DamageAmount);
	if (ClampedDamage <= 0.0f || CurrentHealth <= 0.0f)
	{
		return 0.0f;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = (std::max)(0.0f, CurrentHealth - ClampedDamage);
	const float AppliedDamage = PreviousHealth - CurrentHealth;
	TotalDamageTaken += AppliedDamage;
	++HealthHitCount;
	return AppliedDamage;
}

float APawn::GetHealthRatio() const
{
	if (MaxHealth <= 0.0f)
	{
		return 1.0f;
	}
	return (std::max)(0.0f, (std::min)(1.0f, CurrentHealth / MaxHealth));
}

void APawn::ProcessPlayerInput(const FInputSystemSnapshot& Snapshot, float DeltaTime)
{
	if (InputComponent)
	{
		InputComponent->ProcessInput(Snapshot, DeltaTime);
	}
}

void APawn::PossessedBy(APlayerController* PC)
{
	Controller = PC;

	// 자기 첫 카메라 컴포넌트를 ActiveCamera로 — PIE 시작 시 시점이 Pawn 기준이 되도록.
	// 카메라 컴포넌트가 없으면 no-op (CameraManager의 기존 흐름이 다른 카메라를 선택).
	// E.2/2: PC->GetPlayerCameraManager() 경로 사용.
	if (UCameraComponent* MyCamera = GetComponentByClass<UCameraComponent>())
	{
		if (PC)
		{
			if (APlayerCameraManager* Mgr = PC->GetPlayerCameraManager())
			{
				Mgr->SetActiveCamera(MyCamera);
				Mgr->Possess(MyCamera);
			}
		}
	}
}

void APawn::UnPossessed()
{
	Controller = nullptr;

}

// 직렬화 오버라이드 제거: bAutoPossessPlayer / bUseControllerRotation* 4개는 모두
// UPROPERTY(Save) 라 AActor 가 상속시킨 반사 직렬화(ShouldReflectProperties==true)로 자동 처리된다.
// (기존 수동 Ar<< 는 반사와 중복이었음.)

void APawn::ApplyControllerRotationToRoot()
{
	if (!bUseControllerRotationPitch && !bUseControllerRotationYaw && !bUseControllerRotationRoll) return;

	USceneComponent* Root = GetRootComponent();
	if (!Root) return;

	FRotator R = Root->GetRelativeRotation();
	if (bUseControllerRotationYaw)   R.Yaw   = ControlRotation.Yaw;
	if (bUseControllerRotationPitch) R.Pitch = ControlRotation.Pitch;
	if (bUseControllerRotationRoll)  R.Roll  = ControlRotation.Roll;
	Root->SetRelativeRotation(R);
}
