#include "pch.h"
#include "HorseCharacter.h"

#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Movement/HorseMovementComponent.h"
#include "Component/Movement/HorseLocomotionComponent.h"
#include "Component/AI/BTAgentComponent.h"
#include "Component/AI/ObstacleFanSensorComponent.h"
#include "Component/AI/BlackboardComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "AI/Blackboard.h"
#include "AI/HorseBlackboardKeys.h"
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
	// 몸통 collider 를 root 로: Movement 의 sweep(관통/비비기 차단)·에디터 시각화·향후 물리의 단일 바디.
	// (직립 capsule 이 아닌 quadruped 전제라 torso box 형상 사용.) Pawn 채널, 지금은 query 전용.
	CollisionComponent = AddComponent<UBoxComponent>();
	SetRootComponent(CollisionComponent);
	CollisionComponent->SetBoxExtent(FVector(0.8f, 0.3f, 0.3f));   // half-extents(전방X/측방Y/상하Z)
	CollisionComponent->SetCollisionObjectType(ECollisionChannel::Pawn);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	// 이동 담당
	MovementComponent = AddComponent<UHorseMovementComponent>();

	// SkeletalMesh 는 box 자식으로, 발바닥이 지면에 닿도록 StandHeight 만큼 아래로 offset.
	MeshComponent = AddComponent<USkeletalMeshComponent>();
	MeshComponent->AttachToComponent(CollisionComponent);
	MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -MovementComponent->GetStandHeight()));

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	if (!SkeletalMeshFileName.empty())
	{
		USkeletalMesh* Asset = FMeshManager::LoadSkeletalMesh(SkeletalMeshFileName, Device);
		MeshComponent->SetSkeletalMesh(Asset);
	}

	// ── AI 관련 ──
	// 플레이어/BT 입력을 받아 매 tick MovementComponent 로 라우팅.
	LocomotionComponent = AddComponent<UHorseLocomotionComponent>(); 
	BlackboardComponent = AddComponent<UBlackboardComponent>();
	ObstacleFanSensorComponent = AddComponent<UObstacleFanSensorComponent>();
	if(ObstacleFanSensorComponent)
	{
		ObstacleFanSensorComponent->AttachToComponent(CollisionComponent);
		ObstacleFanSensorComponent->SetRelativeLocation(FVector(1.0f, 0.0f, -0.6f));
	}
	BTAgentComponent = AddComponent<UBTAgentComponent>();
	if (BTAgentComponent)
	{
		BTAgentComponent->SetBehaviorTreeScript("BT/HorseBT.lua");
	}

	SpringArmComponent = AddComponent<USpringArmComponent>();
	SpringArmComponent->AttachToComponent(CollisionComponent);   // root(box) 기준으로 카메라 추종
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

	// 조향(아날로그): 좌우 축 → BB UserMoveDir 로 기록. Locomotion 이 직접 조종당하지 않고 arbiter 가
	// 이를 하나의 interest 로 소비(간접 반영 원칙). 게임패드 좌스틱 X 도 함께.
	InputComponent->AddAxisMapping("HorseSteering", "D", 1.0f);
	InputComponent->AddAxisMapping("HorseSteering", "A", -1.0f);
	InputComponent->AddGamepadAxisMapping("HorseSteering", EInputAxisSourceType::GamepadLeftStickX, 1.0f);
	InputComponent->BindAxis("HorseSteering", [this](float Value)
	{
		LastSteeringInput = Value;
		if (!BlackboardComponent)
		{
			return;
		}
		// 스칼라(±1, 좌우) → forward 에서 옆으로 최대 45° 편향된 world 목표 방향, 크기 = 입력 강도.
		// 매 프레임 현재 forward 기준이라 말이 회전해도 "우측으로 계속 밀기" 가 유지된다. 무입력(0)이면
		// 영벡터를 써 arbiter 가 유저 영향을 무시(도로/관성 자율 주행)한다.
		FVector Move(0.0f, 0.0f, 0.0f);
		const float Strength = std::clamp(std::abs(Value), 0.0f, 1.0f);
		if (Strength > 1.e-3f)
		{
			FVector Forward = GetActorForward();
			FVector Right   = GetActorRight();
			Forward.Z = 0.0f;
			Right.Z   = 0.0f;
			const FVector Dir = Forward + Right * Value;
			if (!Dir.IsNearlyZero())
			{
				Move = Dir.Normalized() * Strength;
			}
		}
		BlackboardComponent->GetBlackboard().SetVector(HorseBBKeys::UserMoveDir, Move);
	});

	// 보법(gait) 변속: W=한 단계 가속(쿨타임 있음), S=한 단계 감속, X=정지.
	// gait는 기어 변속 개념, 무입력 시 현재 gait 유지 => 순항
	InputComponent->AddActionMapping("HorseGiddyup", "W");
	InputComponent->AddActionMapping("HorseSlowDown", "S");
	InputComponent->AddActionMapping("HorseStop", "X");
	InputComponent->AddActionMapping("HorseGiddyup", "GamepadFaceButtonBottom");	// Xbox 패드 기준 A 버튼
	InputComponent->AddActionMapping("HorseSlowDown", "GamepadFaceButtonRight");	// B 버튼
	InputComponent->AddActionMapping("HorseStop", "GamepadFaceButtonLeft");			// X 버튼
	InputComponent->BindAction("HorseGiddyup", EInputEvent::Pressed, [this]()
	{
		if (LocomotionComponent) LocomotionComponent->RequestGiddyup();
	});
	InputComponent->BindAction("HorseSlowDown", EInputEvent::Pressed, [this]()
	{
		if (LocomotionComponent) LocomotionComponent->RequestSlowDown();
	});
	InputComponent->BindAction("HorseStop", EInputEvent::Pressed, [this]()
	{
		if (LocomotionComponent) LocomotionComponent->RequestStop();
	});

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
	CollisionComponent = GetComponentByClass<UBoxComponent>();
	MeshComponent = GetComponentByClass<USkeletalMeshComponent>();
	MovementComponent = GetComponentByClass<UHorseMovementComponent>();
	LocomotionComponent = GetComponentByClass<UHorseLocomotionComponent>();
	BTAgentComponent = GetComponentByClass<UBTAgentComponent>();
	BlackboardComponent = GetComponentByClass<UBlackboardComponent>();
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
	LastSteeringInput = 0.0f;
	UpdateCameraControlRotation();

	Super::BeginPlay();
}

void AHorseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 이동 입력은 LocomotionComponent 가 자기 tick 에서 Movement 로 라우팅한다(여기서 하지 않음).

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

	const bool bInputActive = std::abs(LastSteeringInput) > 0.01f;
	const bool bMoving =
		MovementComponent && std::abs(MovementComponent->GetForwardSpeed()) > CameraMovingReturnSpeedThreshold;

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
