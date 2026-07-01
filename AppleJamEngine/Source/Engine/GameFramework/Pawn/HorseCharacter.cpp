#include "pch.h"
#include "HorseCharacter.h"

#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Horse/HorsePlayerInputComponent.h"
#include "Component/AI/BTAgentComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/MeshManager.h"
#include "Runtime/Engine.h"

namespace
{
	float NormalizeCameraAngle(float Angle)
	{
		Angle = std::fmod(Angle, 360.0f);
		if (Angle > 180.0f)
		{
			Angle -= 360.0f;
		}
		else if (Angle <= -180.0f)
		{
			Angle += 360.0f;
		}
		return Angle;
	}

	float ClampCameraPitch(float Value, float MinPitch, float MaxPitch)
	{
		if (MinPitch > MaxPitch)
		{
			std::swap(MinPitch, MaxPitch);
		}
		return std::clamp(Value, MinPitch, MaxPitch);
	}

	float ClampCameraYawOffset(float Value, float MaxAbsOffset)
	{
		const float ClampedMax = std::clamp(MaxAbsOffset, 0.0f, 180.0f);
		return std::clamp(NormalizeCameraAngle(Value), -ClampedMax, ClampedMax);
	}

	float ExponentialInterpTo(float Current, float Target, float DeltaTime, float Speed)
	{
		if (DeltaTime <= 0.0f || Speed <= 0.0f)
		{
			return Current;
		}

		const float Alpha = 1.0f - std::exp(-Speed * DeltaTime);
		return Current + (Target - Current) * Alpha;
	}

	float ExponentialAngleInterpTo(float Current, float Target, float DeltaTime, float Speed)
	{
		if (DeltaTime <= 0.0f || Speed <= 0.0f)
		{
			return Current;
		}

		const float Delta = NormalizeCameraAngle(Target - Current);
		const float Alpha = 1.0f - std::exp(-Speed * DeltaTime);
		return NormalizeCameraAngle(Current + Delta * Alpha);
	}
} // namespace

void AHorseCharacter::InitDefaultComponents(const FString& SkeletalMeshFileName)
{
	// NOTE: Mesh가 Root여도 되는지 검증 필요
	MeshComponent = AddComponent<USkeletalMeshComponent>();
	SetRootComponent(MeshComponent);

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	if (!SkeletalMeshFileName.empty())
	{
		USkeletalMesh* Asset = FMeshManager::LoadSkeletalMesh(SkeletalMeshFileName, Device);
		MeshComponent->SetSkeletalMesh(Asset);
	}

	HorseMovementComponent = AddComponent<UHorsePlayerInputComponent>();

	BTAgentComponent = AddComponent<UBTAgentComponent>();

	SpringArmComponent = AddComponent<USpringArmComponent>();
	SpringArmComponent->AttachToComponent(MeshComponent);
	SpringArmComponent->TargetArmLength = 7.0f;
	SpringArmComponent->SocketOffset = FVector(0.0f, 0.0f, 2.5f);
	SpringArmComponent->bEnableCameraLag = true;
	SpringArmComponent->bEnableCameraRotationLag = true;
	SpringArmComponent->bUsePawnControlRotation = true;
	SpringArmComponent->bInheritPitch = true;
	SpringArmComponent->bInheritYaw = true;
	SpringArmComponent->bInheritRoll = false;

	CameraPitch = ClampCameraPitch(DefaultCameraPitch, MinCameraPitch, MaxCameraPitch);
	UpdateCameraControlRotation();
	
	CameraComponent = AddComponent<UCameraComponent>();
	CameraComponent->AttachToComponent(SpringArmComponent);
}

void AHorseCharacter::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		return;
	}

	if (HorseMovementComponent)
	{
		InputComponent->AddAxisMapping("HorseThrottle", "W", 1.0f);
		InputComponent->AddAxisMapping("HorseThrottle", "S", -1.0f);
		InputComponent->AddAxisMapping("HorseSteering", "D", 1.0f);
		InputComponent->AddAxisMapping("HorseSteering", "A", -1.0f);
		
		InputComponent->BindAxis("HorseThrottle", [this](float Value)
		{
			if (!HorseMovementComponent)
			{
				return;
			}

			LastThrottleInput = Value;
			HorseMovementComponent->SetThrottleInput(Value);
			HorseMovementComponent->SetBrakeInput(0.0f);
		});

		InputComponent->BindAxis("HorseSteering", [this](float Value)
		{
			LastSteeringInput = Value;
			if (HorseMovementComponent)
			{
				HorseMovementComponent->SetSteeringInput(Value);
			}
		});
	}

	if (bAutoInputCamera)
	{
		InputComponent->AddMouseAxisMapping("HorseCameraHorizontal", EInputAxisSourceType::MouseX, MouseSensitivity);
		InputComponent->AddMouseAxisMapping("HorseCameraVertical", EInputAxisSourceType::MouseY, MouseSensitivity);

		InputComponent->BindAxis("HorseCameraHorizontal", [this](float Value)
		{
			if (std::abs(Value) <= 0.0001f)
			{
				return;
			}

			CameraTimeSinceLookInput = 0.0f;
			bCameraLookInputThisFrame = true;
			CameraYawOffset = ClampCameraYawOffset(CameraYawOffset + Value, MaxCameraYawOffset);
			UpdateCameraControlRotation();
		});

		InputComponent->BindAxis("HorseCameraVertical", [this](float Value)
		{
			if (std::abs(Value) <= 0.0001f)
			{
				return;
			}

			CameraTimeSinceLookInput = 0.0f;
			bCameraLookInputThisFrame = true;
			const float Direction = bInvertMouseY ? -1.0f : 1.0f;
			CameraPitch = ClampCameraPitch(CameraPitch + Value * Direction, MinCameraPitch, MaxCameraPitch);
			UpdateCameraControlRotation();
		});
	}
}

void AHorseCharacter::RebindComponents()
{
	MeshComponent = GetComponentByClass<USkeletalMeshComponent>();
	HorseMovementComponent = GetComponentByClass<UHorsePlayerInputComponent>();
	SpringArmComponent = GetComponentByClass<USpringArmComponent>();
	CameraComponent = GetComponentByClass<UCameraComponent>();
}

void AHorseCharacter::BeginPlay()
{
	RebindComponents();
	CameraPitch = ClampCameraPitch(DefaultCameraPitch, MinCameraPitch, MaxCameraPitch);
	CameraYawOffset = 0.0f;
	CameraTimeSinceLookInput = CameraReturnDelay;
	bCameraLookInputThisFrame = false;
	LastThrottleInput = 0.0f;
	LastSteeringInput = 0.0f;
	UpdateCameraControlRotation();

	Super::BeginPlay();
}

void AHorseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateCameraReturn(DeltaTime);
	UpdateCameraControlRotation();
}

void AHorseCharacter::PostDuplicate()
{
	Super::PostDuplicate();
	RebindComponents();
}

void AHorseCharacter::OnPostLoad(FArchive& Ar)
{
	Super::OnPostLoad(Ar);
	RebindComponents();
}

void AHorseCharacter::UpdateCameraControlRotation()
{
	if (!bAutoInputCamera)
	{
		return;
	}

	CameraPitch = ClampCameraPitch(CameraPitch, MinCameraPitch, MaxCameraPitch);
	CameraYawOffset = ClampCameraYawOffset(CameraYawOffset, MaxCameraYawOffset);

	const float BaseYaw = GetCameraBaseYaw();

	FRotator CameraControl = GetControlRotation();
	CameraControl.Pitch = CameraPitch;
	CameraControl.Yaw = NormalizeCameraAngle(BaseYaw + CameraYawOffset);
	CameraControl.Roll = 0.0f;
	SetControlRotation(CameraControl);
}

void AHorseCharacter::UpdateCameraReturn(float DeltaTime)
{
	if (!bAutoInputCamera || !bAutoReturnCamera || DeltaTime <= 0.0f)
	{
		bCameraLookInputThisFrame = false;
		return;
	}

	if (bCameraLookInputThisFrame)
	{
		CameraTimeSinceLookInput = 0.0f;
		bCameraLookInputThisFrame = false;
		return;
	}

	CameraTimeSinceLookInput += DeltaTime;

	const bool bInputActive =
		std::abs(LastThrottleInput) > 0.01f ||
		std::abs(LastSteeringInput) > 0.01f;
	const bool bMoving =
		HorseMovementComponent && std::abs(HorseMovementComponent->GetForwardSpeed()) > CameraMovingReturnSpeedThreshold;

	const bool bCameraReturnRequested = bInputActive || bMoving;
	if (!bCameraReturnRequested || CameraTimeSinceLookInput < CameraReturnDelay)
	{
		return;
	}

	const float SpeedScale = CameraMovingReturnMultiplier;
	CameraYawOffset = ExponentialAngleInterpTo(CameraYawOffset, 0.0f, DeltaTime, CameraYawReturnSpeed * SpeedScale);
	CameraPitch = ExponentialInterpTo(CameraPitch, DefaultCameraPitch, DeltaTime, CameraPitchReturnSpeed * SpeedScale);

	if (std::abs(CameraYawOffset) < 0.01f)
	{
		CameraYawOffset = 0.0f;
	}
	if (std::abs(CameraPitch - DefaultCameraPitch) < 0.01f)
	{
		CameraPitch = DefaultCameraPitch;
	}
}

float AHorseCharacter::GetCameraBaseYaw() const
{
	if (MeshComponent)
	{
		return MeshComponent->GetWorldRotation().Yaw;
	}
	if (const USceneComponent* Root = GetRootComponent())
	{
		return Root->GetWorldRotation().Yaw;
	}
	return GetActorRotation().Yaw;
}
