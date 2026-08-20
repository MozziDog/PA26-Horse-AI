#include "pch.h"
#include "HorseCharacter.h"

#include "Animation/Graph/AnimGraphManager.h"
#include "Animation/Graph/AnimGraphInstance.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/AI/BTAgentComponent.h"
#include "Component/AI/RoadSensorComponent.h"
#include "Component/AI/BlackboardComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/SceneComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Quat.h"
#include "AI/Blackboard.h"
#include "HorseMovementComponent.h"
#include "HorseLocomotionComponent.h"
#include "HorseCallNavigationComponent.h"
#include "HorseUserGuidanceComponent.h"
#include "ObstacleFanSensorComponent.h"
#include "JumpObstacleSensorComponent.h"
#include "CliffFanSensorComponent.h"
#include "Game/Horse/HorseConstants.h"
#include "Game/Rider/MountTriggerComponent.h"
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

	void WriteControlProfile(UBlackboardComponent* BlackboardComponent, bool bRoadAssist, bool bIgnoreContextAvoidance, bool bAutoJump, bool bStrafe)
	{
		if (!BlackboardComponent)
		{
			return;
		}

		FBlackboard& Blackboard = BlackboardComponent->GetBlackboard();
		Blackboard.SetBool(HorseBBKeys::ControlEnableRoadAssist, bRoadAssist);
		Blackboard.SetBool(HorseBBKeys::ControlIgnoreContextAvoidance, bIgnoreContextAvoidance);
		Blackboard.SetBool(HorseBBKeys::ControlEnableAutoJump, bAutoJump);
		Blackboard.SetBool(HorseBBKeys::ControlEnableStrafe, bStrafe);
	}
} // namespace

void AHorseCharacter::InitDefaultComponents(const FString& SkeletalMeshFileName)
{
	// Root: Empty SceneComponent
	// 캡슐 컴포넌트가 local Z 방향으로 고정인 점, 스켈레톤 root가 골반 위치인 점 등을 고려, 
	// 어느 쪽도 Root로 삼기 애매해서 별도의 empty root 사용
	RootSceneComponent = AddComponent<USceneComponent>();
	SetRootComponent(RootSceneComponent);

	// 이동 담당
	MovementComponent = AddComponent<UHorseMovementComponent>();
	const float StandHeight = MovementComponent->StandHeight;

	// 몸통 콜라이더
	CollisionComponent = AddComponent<UCapsuleComponent>();
	CollisionComponent->AttachToComponent(RootSceneComponent);
	CollisionComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -StandHeight + 1.2f));
	// 앞쪽으로 눕힘. 쿼터니언-오일러각 변환 문제로 (0, 90, 0) 대신 (90, 0, 90) 사용
	CollisionComponent->SetRelativeRotation(FQuat::FromRotator(FRotator(0.0, 90.0, 90.0)));
	CollisionComponent->SetCapsuleSize(0.4f, 0.8f);
	CollisionComponent->SetCollisionObjectType(ECollisionChannel::Pawn);
	CollisionComponent->SetKinematic(true);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Leg box — 몸통 캡슐 아래쪽 판정용
	// NOTE: 겹친 상태의 sweep으로 MTD(최소 탈출 벡터)를 계산하여 측면 충돌만 처리하는데, 
	// 충돌 판정이 장애물 윗면을 품고 있으면 MTD가 위방향 → 측면 충돌만 필터링하는 로직에 의해 투과됨
	// 따라서 충돌판정을 Z방향으로 가능한 얇게 유지하여 상하방향 충돌로 오인 완화
	// (완전한 방지는 안됨. 구현 상의 사각지대로 유지)
	StepBlockComponent = AddComponent<UBoxComponent>();
	StepBlockComponent->AttachToComponent(RootSceneComponent);
	StepBlockComponent->SetRelativeLocation(FVector(-0.05f, 0.0f, 0.55f));
	StepBlockComponent->SetBoxExtent(FVector(0.7f, 0.2f, 0.1f));
	StepBlockComponent->SetCollisionObjectType(ECollisionChannel::Pawn);
	StepBlockComponent->SetKinematic(true);
	StepBlockComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	// 왼쪽/오른쪽/뒤쪽으로 각각 탑승을 위한 MountTrigger 추가
	auto AddMountTrigger = [this](EMountDirection Direction, const FVector& Location)
	{
		UMountTriggerComponent* Trigger = AddComponent<UMountTriggerComponent>();
		if (!Trigger)
		{
			return;
		}

		Trigger->AttachToComponent(RootSceneComponent);
		Trigger->SetMountDirection(Direction);
		Trigger->SetRelativeLocation(Location);
		Trigger->SetBoxExtent(FVector(0.65f, 0.50f, 1.00f));
		Trigger->SetCollisionObjectType(ECollisionChannel::Trigger);
		Trigger->SetCollisionResponseToAllChannels(ECollisionResponse::Overlap);
		Trigger->SetGenerateOverlapEvents(true);
		Trigger->SetSimulatePhysics(false);
		Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	};
	AddMountTrigger(EMountDirection::Left,  FVector(0.0f, -1.10f, 1.00f));
	AddMountTrigger(EMountDirection::Right, FVector(0.0f,  1.10f, 1.00f));
	AddMountTrigger(EMountDirection::Back,  FVector(-1.35f, 0.0f, 1.00f));

	// SkeletalMesh. 발바닥이 지면에 닿도록 StandHeight 만큼 아래로 offset 부여
	MeshComponent = AddComponent<USkeletalMeshComponent>();
	MeshComponent->AttachToComponent(RootSceneComponent);
	MeshComponent->SetRelativeLocation(FVector(-0.5f, 0.0f, -StandHeight));
	MeshComponent->SetRelativeScale(FVector(1.08f, 1.08f, 1.08f));

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	if (!SkeletalMeshFileName.empty())
	{
		USkeletalMesh* Asset = FMeshManager::LoadSkeletalMesh(SkeletalMeshFileName, Device);
		MeshComponent->SetSkeletalMesh(Asset);
	}

	// 애니메이션 세팅
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationCustom);
	MeshComponent->SetAnimInstanceClass(UAnimGraphInstance::StaticClass());
	UAnimGraphInstance* AnimGraphInstance = Cast<UAnimGraphInstance>(MeshComponent->GetAnimInstance());
	if (AnimGraphInstance)
	{
		AnimGraphInstance->DefaultSequencePath = "Content/Mesh/Horse/Reexport/Horse_A_Scene.uasset";
		FString AnimGraphPath = "Content/Mesh/Horse/Reexport/HorseAnimGraph.uasset";
		UAnimGraphAsset* Asset = FAnimGraphManager::Get().Load(AnimGraphPath);
		AnimGraphInstance->SetGraphAsset(Asset);
		AnimGraphInstance->GraphAssetPath = AnimGraphPath;
		MeshComponent->InitializeAnimation();
	}

	// ── AI 관련 ──
	// Decision Layer: 어떠한 동작을 취할지 결정
	BTAgentComponent = AddComponent<UBTAgentComponent>();
	if (BTAgentComponent)
	{
		BTAgentComponent->SetBehaviorTreeScript("BT/HorseBT.lua");
	}

	// Guidance Layer: 어떠한 방향으로 나아갈지 결정
	// 탑승 상태 여부에 따라 둘 중 하나만 활성화.
	UserGuidanceComponent = AddComponent<UHorseUserGuidanceComponent>();
	CallNavigationComponent = AddComponent<UHorseCallNavigationComponent>();

	// Reactive Layer: 상위 계층의 요청에 따라 실질적인 움직임 수행
	BlackboardComponent = AddComponent<UBlackboardComponent>();
	LocomotionComponent = AddComponent<UHorseLocomotionComponent>(); 
	ObstacleFanSensorComponent = AddComponent<UObstacleFanSensorComponent>();
	if(ObstacleFanSensorComponent)
	{
		ObstacleFanSensorComponent->AttachToComponent(RootSceneComponent);
		ObstacleFanSensorComponent->SetRelativeLocation(FVector(0.75f, 0.0f, -StandHeight + 1.2f));
	}
	JumpObstacleSensorComponent = AddComponent<UJumpObstacleSensorComponent>();
	if (JumpObstacleSensorComponent)
	{
		JumpObstacleSensorComponent->AttachToComponent(RootSceneComponent);
		JumpObstacleSensorComponent->SetRelativeLocation(FVector(1.0f, 0.0f, -StandHeight + 1.0f));
	}
	CliffFanSensorComponent = AddComponent<UCliffFanSensorComponent>();
	if(CliffFanSensorComponent)
	{
		CliffFanSensorComponent->AttachToComponent(RootSceneComponent);
		CliffFanSensorComponent->SetRelativeLocation(FVector(1.0f, 0.0f, -StandHeight + 1.2f));
	}
	RoadSensorComponent = AddComponent<URoadSensorComponent>();
	if (RoadSensorComponent)
	{
		RoadSensorComponent->AttachToComponent(RootSceneComponent);
		RoadSensorComponent->SetRelativeLocation(FVector(10.0f, 0.0f, 0.0f));
	}

	// ── 카메라 관련 ──
	SpringArmComponent = AddComponent<USpringArmComponent>();
	SpringArmComponent->AttachToComponent(RootSceneComponent);   // root 기준으로 카메라 추종
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
	if (!InputComponent) return;

	InputComponent->AddAxisMapping("HorseSteering", "D", 1.0f);
	InputComponent->AddAxisMapping("HorseSteering", "A", -1.0f);
	InputComponent->AddGamepadAxisMapping("HorseSteering", EInputAxisSourceType::GamepadLeftStickX, 1.0f);
	InputComponent->BindAxis("HorseSteering", [this](float Value) { SetSteeringInput(Value); });

	InputComponent->AddActionMapping("HorseGiddyup", "W");
	InputComponent->AddActionMapping("HorseSlowDown", "S");
	InputComponent->AddActionMapping("HorseStop", "X");
	InputComponent->AddActionMapping("HorseGiddyup", "GamepadFaceButtonBottom");
	InputComponent->AddActionMapping("HorseSlowDown", "GamepadFaceButtonRight");
	InputComponent->AddActionMapping("HorseStop", "GamepadFaceButtonLeft");
	InputComponent->BindAction("HorseGiddyup", EInputEvent::Pressed, [this]() { RequestGiddyup(); });
	InputComponent->BindAction("HorseSlowDown", EInputEvent::Pressed, [this]() { RequestSlowDown(); });
	InputComponent->BindAction("HorseStop", EInputEvent::Pressed, [this]() { RequestStop(); });

	InputComponent->AddAxisMapping("HorseGaze", "LeftShift", 1.0f);
	InputComponent->AddGamepadAxisMapping("HorseGaze", EInputAxisSourceType::GamepadLeftTrigger, 1.0f);
	InputComponent->BindAxis("HorseGaze", [this](float Value) { SetGazeInput(Value); });

	InputComponent->AddAxisMapping("HorseStrafeForward", "W", 1.0f);
	InputComponent->AddAxisMapping("HorseStrafeForward", "S", -1.0f);
	InputComponent->AddGamepadAxisMapping("HorseStrafeForward", EInputAxisSourceType::GamepadLeftStickY, 1.0f);
	InputComponent->BindAxis("HorseStrafeForward", [this](float Value) { SetStrafeForwardInput(Value); });

	if (bAutoInputCamera && bEnableMouseLook)
	{
		InputComponent->AddMouseAxisMapping("HorseCameraHorizontal", EInputAxisSourceType::MouseX, MouseSensitivity);
		InputComponent->AddMouseAxisMapping("HorseCameraVertical", EInputAxisSourceType::MouseY, MouseSensitivity);
		InputComponent->BindAxis("HorseCameraHorizontal", [this](float Value) { AddCameraHorizontalInput(Value); });
		InputComponent->BindAxis("HorseCameraVertical", [this](float Value) { AddCameraVerticalInput(Value); });
	}
}

void AHorseCharacter::SetSteeringInput(float Value)
{
	const float SteeringInput = std::clamp(Value, -1.0f, 1.0f);
	LastSteeringInput = SteeringInput;
	if (UserGuidanceComponent) UserGuidanceComponent->OnSteeringInput(SteeringInput);
	if (LocomotionComponent) LocomotionComponent->SetStrafeHorizontalInput(SteeringInput);
}

void AHorseCharacter::RequestGiddyup() { if (LocomotionComponent) LocomotionComponent->RequestGiddyup(); }
void AHorseCharacter::RequestSlowDown() { if (LocomotionComponent) LocomotionComponent->RequestSlowDown(); }
void AHorseCharacter::RequestStop() { if (LocomotionComponent) LocomotionComponent->RequestStop(); }

int32 AHorseCharacter::GetCurrentGait() const
{
	return LocomotionComponent ? static_cast<int32>(LocomotionComponent->GetGait()) : static_cast<int32>(EHorseGait::Stop);
}

void AHorseCharacter::SetRiderMounted(bool bInRiderMounted)
{
	bRiderMounted = bInRiderMounted;
	if (bRiderMounted && CallNavigationComponent)
	{
		CallNavigationComponent->NotifyMounted();
	}

	ApplyRiderControlState();
}

void AHorseCharacter::RequestWhistleCall(const FVector& TargetLocation)
{
	if (bPlayerOwnedHorse && CallNavigationComponent)
	{
		CallNavigationComponent->RequestCall(TargetLocation);
	}
}

void AHorseCharacter::SetGazeInput(float Value)
{
	bGazeInput = Value > GamepadTriggerHoldThreshold;
	if (LocomotionComponent) LocomotionComponent->SetStrafeMode(bGazeInput);
}

void AHorseCharacter::SetStrafeForwardInput(float Value)
{
	if (LocomotionComponent) LocomotionComponent->SetStrafeVerticalInput(std::clamp(Value, -1.0f, 1.0f));
}

void AHorseCharacter::AddCameraHorizontalInput(float Value)
{
	if (std::abs(Value) <= 0.0001f) return;
	CameraTimeSinceLookInput = 0.0f;
	bCameraLookInputThisFrame = true;
	CameraYawOffset = ClampCameraYawOffset(CameraYawOffset + Value, MaxCameraYawOffset);
	UpdateCameraControlRotation();
}

void AHorseCharacter::AddCameraVerticalInput(float Value)
{
	if (std::abs(Value) <= 0.0001f) return;
	CameraTimeSinceLookInput = 0.0f;
	bCameraLookInputThisFrame = true;
	CameraPitch = ClampCameraPitch(CameraPitch + Value * (bInvertMouseY ? -1.0f : 1.0f), MinCameraPitch, MaxCameraPitch);
	UpdateCameraControlRotation();
}
void AHorseCharacter::ResetCameraToDefault()
{
	CameraYawOffset = 0.0f;
	CameraPitch = ClampCameraPitch(DefaultCameraPitch, MinCameraPitch, MaxCameraPitch);
	CameraTimeSinceLookInput = CameraReturnDelay;
	bCameraLookInputThisFrame = false;
	UpdateCameraControlRotation();
}
void AHorseCharacter::RebindComponents()
{
	RootSceneComponent = GetRootComponent();
	CollisionComponent = GetComponentByClass<UCapsuleComponent>();
	MeshComponent = GetComponentByClass<USkeletalMeshComponent>();
	MovementComponent = GetComponentByClass<UHorseMovementComponent>();
	StepBlockComponent = GetComponentByClass<UBoxComponent>();
	LocomotionComponent = GetComponentByClass<UHorseLocomotionComponent>();
	BTAgentComponent = GetComponentByClass<UBTAgentComponent>();
	BlackboardComponent = GetComponentByClass<UBlackboardComponent>();
	CallNavigationComponent = GetComponentByClass<UHorseCallNavigationComponent>();
	UserGuidanceComponent = GetComponentByClass<UHorseUserGuidanceComponent>();
	ObstacleFanSensorComponent = GetComponentByClass<UObstacleFanSensorComponent>();
	JumpObstacleSensorComponent = GetComponentByClass<UJumpObstacleSensorComponent>();
	CliffFanSensorComponent = GetComponentByClass<UCliffFanSensorComponent>();
	RoadSensorComponent = GetComponentByClass<URoadSensorComponent>();
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
	bGazeInput = false;
	UpdateCameraControlRotation();

	Super::BeginPlay();
	ApplyRiderControlState();
}

void AHorseCharacter::ApplyRiderControlState()
{
	if (UserGuidanceComponent)
	{
		UserGuidanceComponent->SetGuidanceActive(bRiderMounted);
		if (bRiderMounted)
		{
			UserGuidanceComponent->OnSteeringInput(LastSteeringInput);
		}
	}
	WriteControlProfile(BlackboardComponent.Get(),
		bRiderMounted, false, bRiderMounted, bRiderMounted);
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
