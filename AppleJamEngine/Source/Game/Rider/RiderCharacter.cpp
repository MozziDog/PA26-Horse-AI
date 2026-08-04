#include "pch.h"
#include "RiderCharacter.h"

#include "Component/Shape/CapsuleComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/ParentConstraintComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Game/Horse/HorseCharacter.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/World.h"

#include <algorithm>

ARiderCharacter::ARiderCharacter()
{
	// 상태 별로 다른 입력 처리 필요하므로 Rider가 직접 입력 바인딩 수행
	// ACharacter의 입력 초기화 사용 X
	bAutoInputWASD = false;
	bAutoInputMouseLook = false;
}

void ARiderCharacter::InitDefaultComponents(const FString& SkeletalMeshFileName)
{
	Super::InitDefaultComponents(SkeletalMeshFileName);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -0.9f));

	ParentConstraintComponent = AddComponent<UParentConstraintComponent>();

	// 3인칭 카메라 체인 — Capsule → SpringArm → Camera. lag 적용해 부드럽게 따라옴.
	SpringArm = AddComponent<USpringArmComponent>();
	SpringArm->AttachToComponent(CapsuleComponent);
	SpringArm->TargetArmLength = 10.0f;
	SpringArm->SocketOffset = FVector(0.0f, 0.0f, 3.0f);
	SpringArm->bEnableCameraLag = true;
	SpringArm->bEnableCameraRotationLag = true;

	// mouse look 이 capsule rotation 안 건드리고 카메라만 회전 — UE ThirdPerson 패턴.
	// ACharacter::Tick 이 APawn::ControlRotation 누적 → SpringArm 이 이걸 inherit.
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = false;

	Camera = AddComponent<UCameraComponent>();
	Camera->AttachToComponent(SpringArm);
}

void ARiderCharacter::PostDuplicate()
{
	Super::PostDuplicate();
	ParentConstraintComponent = GetComponentByClass<UParentConstraintComponent>();
}

void ARiderCharacter::OnPostLoad(FArchive& Ar)
{
	Super::OnPostLoad(Ar);
	ParentConstraintComponent = GetComponentByClass<UParentConstraintComponent>();
}

void ARiderCharacter::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent) return;

	InputComponent->AddAxisMapping("MoveForward", "W", 1.0f);
	InputComponent->AddAxisMapping("MoveForward", "S", -1.0f);
	InputComponent->AddAxisMapping("MoveRight", "D", 1.0f);
	InputComponent->AddAxisMapping("MoveRight", "A", -1.0f);
	InputComponent->AddGamepadAxisMapping("MoveForward", EInputAxisSourceType::GamepadLeftStickY, 1.0f);
	InputComponent->AddGamepadAxisMapping("MoveRight", EInputAxisSourceType::GamepadLeftStickX, 1.0f);
	InputComponent->BindAxis("MoveForward", [this](float Value) { MoveForward(Value); });
	InputComponent->BindAxis("MoveRight", [this](float Value) { MoveRight(Value); });

	InputComponent->AddActionMapping("Jump", 0x20);
	InputComponent->AddActionMapping("Jump", "GamepadFaceButtonBottom");
	InputComponent->BindAction("Jump", EInputEvent::Pressed, [this]() { JumpOrGiddyup(); });

	InputComponent->AddActionMapping("Mount", "E");
	InputComponent->BindAction("Mount", EInputEvent::Pressed, [this]() { if (MountedHorse) Unmount(); else Mount(); });

	InputComponent->AddActionMapping("Unmount", "GamepadFaceButtonRight");
	InputComponent->BindAction("Unmount", EInputEvent::Pressed, [this]() { Unmount(); });

	InputComponent->AddActionMapping("HorseSlowDown", "S");
	InputComponent->AddActionMapping("HorseStop", "X");
	InputComponent->AddActionMapping("HorseStop", "GamepadFaceButtonLeft");
	InputComponent->BindAction("HorseSlowDown", EInputEvent::Pressed, [this]() { SlowDownHorse(); });
	InputComponent->BindAction("HorseStop", EInputEvent::Pressed, [this]() { StopHorse(); });

	InputComponent->AddAxisMapping("HorseGaze", "LeftShift", 1.0f);
	InputComponent->AddGamepadAxisMapping("HorseGaze", EInputAxisSourceType::GamepadLeftTrigger, 1.0f);
	InputComponent->BindAxis("HorseGaze", [this](float Value) { SetHorseGaze(Value); });

	InputComponent->AddMouseAxisMapping("Turn", EInputAxisSourceType::MouseX, MouseSensitivity);
	InputComponent->AddMouseAxisMapping("LookUp", EInputAxisSourceType::MouseY, MouseSensitivity);
	InputComponent->AddGamepadAxisMapping("Turn", EInputAxisSourceType::GamepadRightStickX, GamepadLookSensitivity);
	InputComponent->AddGamepadAxisMapping("LookUp", EInputAxisSourceType::GamepadRightStickY, -GamepadLookSensitivity);
	InputComponent->BindAxis("Turn", [this](float Value) { Turn(Value); });
	InputComponent->BindAxis("LookUp", [this](float Value) { LookUp(Value); });
}

bool ARiderCharacter::Mount()
{
	if (MountedHorse)
	{
		return true;
	}

	AHorseCharacter* Horse = FindMountTarget();
	if (!Horse || !ParentConstraintComponent || !Horse->GetRootComponent())
	{
		return false;
	}

	if (!ParentConstraintComponent->AttachTo(Horse->GetRootComponent(), FName::None, false))
	{
		return false;
	}

	ParentConstraintComponent->SetRelativeOffset(MountLocationOffset, MountRotationOffset);
	Horse->SetRiderMounted(true);
	MountedHorse = Horse;

	// 탑승 중에는 말의 카메라를 사용
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (World && PlayerController)
	{
		Horse->ResetCameraToDefault();
		PlayerController->SetViewTargetWithBlend(Horse, MountCameraTransitionTime, 
												EViewTargetBlendFunction::VTBlend_EaseInOut);
	}
	return true;
}

bool ARiderCharacter::Unmount()
{
	AHorseCharacter* Horse = MountedHorse.Get();
	if (!Horse || !ParentConstraintComponent)
	{
		return false;
	}

	Horse->SetRiderMounted(false);
	// TODO: 말이 하차 시 즉시 점추지 않고 자연스럽게 서서히 멈추도록 구현
	Horse->RequestStop();
	Horse->SetSteeringInput(0.0f);
	Horse->SetStrafeForwardInput(0.0f);
	Horse->SetGazeInput(0.0f);
	// 하차 전에 라이더의 '보는 방향'(=카메라 방향)을 말의 것과 동기화, 하차 직후 카메라 돌아가지 않게 함
	SetControlRotation(Horse->GetControlRotation());
	ParentConstraintComponent->Detach();
	MountedHorse.Reset();

	// 임시 구현) 무조건 말 오른쪽으로 내리도록 
	SetActorLocation(Horse->GetActorLocation() + Horse->GetActorRight() 
						+ FVector::UpVector * CapsuleComponent->GetScaledCapsuleHalfHeight());
	SetActorRotation(Horse->GetActorRotation());

	// 하차 시에 라이더의 카메라로 복귀
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (World && PlayerController)
	{
		PlayerController->SetViewTargetWithBlend(this, MountCameraTransitionTime, 
												EViewTargetBlendFunction::VTBlend_EaseInOut);
	}
	return true;
}

AHorseCharacter* ARiderCharacter::FindMountTarget() const
{
	// TODO: 이름으로 탑승 대상 찾는 임시 로직 제거
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	for (AActor* Actor : World->GetActors())
	{
		if (Actor && Actor->GetFName().ToString() == "HorseCharacter")
		{
			return Cast<AHorseCharacter>(Actor);
		}
	}
	return nullptr;
}

void ARiderCharacter::MoveForward(float Value)
{
	if (MountedHorse)
	{
		MountedHorse->SetStrafeForwardInput(Value);
		return;
	}
	if (Value == 0.0f) return;
	const FRotator YawOnly(0.0f, GetControlRotation().Yaw, 0.0f);
	AddMovementInput(YawOnly.GetForwardVector(), std::clamp(Value, -1.0f, 1.0f));
}

void ARiderCharacter::MoveRight(float Value)
{
	if (MountedHorse)
	{
		MountedHorse->SetSteeringInput(Value);
		return;
	}
	if (Value == 0.0f) return;
	const FRotator YawOnly(0.0f, GetControlRotation().Yaw, 0.0f);
	AddMovementInput(YawOnly.GetRightVector(), std::clamp(Value, -1.0f, 1.0f));
}

void ARiderCharacter::Turn(float Value)
{
	if (MountedHorse)
	{
		MountedHorse->AddCameraHorizontalInput(Value);
		return;
	}
	if (Value == 0.0f) return;
	FRotator Rotation = GetControlRotation();
	Rotation.Yaw += Value;
	SetControlRotation(Rotation);
}

void ARiderCharacter::LookUp(float Value)
{
	if (MountedHorse)
	{
		MountedHorse->AddCameraVerticalInput(Value);
		return;
	}
	if (Value == 0.0f) return;
	FRotator Rotation = GetControlRotation();
	Rotation.Pitch = std::clamp(Rotation.Pitch + Value, MinCameraPitch, MaxCameraPitch);
	SetControlRotation(Rotation);
}

void ARiderCharacter::JumpOrGiddyup()
{
	if (MountedHorse)
	{
		MountedHorse->RequestGiddyup();
		return;
	}
	Jump();
}

void ARiderCharacter::SlowDownHorse()
{
	if (MountedHorse)
	{
		MountedHorse->RequestSlowDown();
	}
}

void ARiderCharacter::StopHorse()
{
	if (MountedHorse)
	{
		MountedHorse->RequestStop();
	}
}

void ARiderCharacter::SetHorseGaze(float Value)
{
	if (MountedHorse)
	{
		MountedHorse->SetGazeInput(Value);
	}
}