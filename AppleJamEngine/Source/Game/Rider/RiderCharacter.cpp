#include "pch.h"
#include "RiderCharacter.h"

#include "Animation/Graph/AnimGraphManager.h"
#include "Animation/Graph/AnimGraphInstance.h"
#include "Animation/AnimationManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/Montage/AnimMontage.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/ParentConstraintComponent.h"
#include "Component/SceneComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Game/Horse/HorseCharacter.h"
#include "Game/Rider/MountTriggerComponent.h"
#include "Game/Rider/NavigationStreamingComponent.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/World.h"
#include "Core/Logging/Log.h"

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
	CapsuleComponent->SetCapsuleSize(0.13f, 0.9f);   // 하차시에 말과 충돌판정 겹치지 않도록 캡슐 얇게 설정
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -0.9f)); // 메시 pivot이 발 끝이라 half height 만큼 내림

	// 애니메이션 세팅
	Mesh->SetAnimationMode(EAnimationMode::AnimationCustom);
	Mesh->SetAnimInstanceClass(UAnimGraphInstance::StaticClass());
	UAnimGraphInstance* AnimGraphInstance = Cast<UAnimGraphInstance>(Mesh->GetAnimInstance());
	if (AnimGraphInstance)
	{
		AnimGraphInstance->DefaultSequencePath = "Content/Mesh/HorseRider/Rider_Unmounted_Idle.uasset";
		FString AnimGraphPath = "Content/Mesh/HorseRider/RiderAnimGraph.uasset";
		UAnimGraphAsset* Asset = FAnimGraphManager::Get().Load(AnimGraphPath);
		AnimGraphInstance->SetGraphAsset(Asset);
		AnimGraphInstance->GraphAssetPath = AnimGraphPath;
		Mesh->InitializeAnimation();
	}
	ULuaScriptComponent* LuaAnimComp = AddComponent<ULuaScriptComponent>();
	LuaAnimComp->SetScriptFile("RiderAnim.lua");

	ParentConstraintComponent = AddComponent<UParentConstraintComponent>();
	NavigationStreamingComp = AddComponent<UNavigationStreamingComponent>();

	// 3인칭 카메라 체인 — Capsule → SpringArm → Camera. lag 적용해 부드럽게 따라옴.
	SpringArm = AddComponent<USpringArmComponent>();
	SpringArm->AttachToComponent(CapsuleComponent);
	SpringArm->TargetArmLength = 5.0f;
	SpringArm->SocketOffset = FVector(0.0f, 0.0f, 1.5f);
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
	NavigationStreamingComp = GetComponentByClass<UNavigationStreamingComponent>();
}

void ARiderCharacter::OnPostLoad(FArchive& Ar)
{
	Super::OnPostLoad(Ar);
	ParentConstraintComponent = GetComponentByClass<UParentConstraintComponent>();
	NavigationStreamingComp = GetComponentByClass<UNavigationStreamingComponent>();
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

	InputComponent->AddActionMapping("Whistle", "H");
	InputComponent->AddActionMapping("Whistle", "GamepadDPadDown");
	InputComponent->BindAction("Whistle", EInputEvent::Pressed, [this]() { Whistle(); });

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
	if (IsMountingOrDismounting())
	{
		return false;
	}

	UMountTriggerComponent* Trigger = AvailableMountTrigger.Get();
	AHorseCharacter* Horse = Trigger ? Trigger->GetMountTarget() : nullptr;
	if (!Horse || !ParentConstraintComponent || !Horse->GetRootComponent())
	{
		return false;
	}
	if (Horse->IsRiderMounted())
	{
		return false;
	}

	const EMountDirection Direction = Trigger->GetMountDirection();
	UAnimMontage* Montage = LoadAnimMontageByMountDirection(Direction);
	return DoMount(Horse, Montage, Direction);
}

bool ARiderCharacter::Unmount()
{
	AHorseCharacter* Horse = MountedHorse.Get();
	if (!Horse || !ParentConstraintComponent || IsMountingOrDismounting())
	{
		return false;
	}

	UAnimMontage* Montage = LoadAnimMontageByMountDirection(LastMountDirection);
	return DoUnmount(Horse, Montage);
}

void ARiderCharacter::SetMountTrigger(UMountTriggerComponent* Trigger)
{
	if (!Trigger || IsMountingOrDismounting() || MountedHorse)
	{
		return;
	}
	AvailableMountTrigger = Trigger;
}

void ARiderCharacter::ClearMountTrigger(UMountTriggerComponent* Trigger)
{
	if (AvailableMountTrigger.Get() == Trigger)
	{
		AvailableMountTrigger.Reset();
	}
}

void ARiderCharacter::OnMountFinishNotify()
{
	// Mount 동작 마무리
	AHorseCharacter* Horse = PendingHorse.Get();
	if (!Horse || !ParentConstraintComponent || !Horse->GetRootComponent() ||
		!ParentConstraintComponent->AttachTo(Horse->GetRootComponent(), FName::None, false))
	{
		ResetMountTransition();
		return;
	}

	ParentConstraintComponent->SetRelativeOffset(MountLocationOffset, MountRotationOffset);
	Horse->SetRiderMounted(true);
	MountedHorse = Horse;
	LastMountDirection = PendingMountDirection;
	ResetMountTransition();

	// 메인 카메라를 HorseCharacter의 것으로 전환
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (PlayerController)
	{
		Horse->ResetCameraToDefault();	// Unmount 도중에는 카메라 리셋 안되어서 unmount 후 다시 mount했을 때
										// 카메라가 이상한 곳 바라보고 있을 수 있음
		PlayerController->SetViewTargetWithBlend(Horse, MountCameraTransitionTime, 
											EViewTargetBlendFunction::VTBlend_EaseInOut);
	}
}

void ARiderCharacter::OnUnmountFinishNotify()
{
	// Unmount 동작 마무리
	AHorseCharacter* Horse = PendingHorse.Get();
	SnapToUnmountEnd(Horse, PendingMontage.Get());
	FRotator RiderRotation = GetActorRotation();
	RiderRotation.Pitch = 0.0f;
	RiderRotation.Roll = 0.0f;
	SetActorRotation(RiderRotation);
	if (Horse)
	{
		Horse->SetRiderMounted(false);
		Horse->RequestStop();
		Horse->SetSteeringInput(0.0f);
		Horse->SetStrafeForwardInput(0.0f);
		Horse->SetGazeInput(0.0f);
		SetControlRotation(GetActorRotation());
	}

	MountedHorse.Reset();
	ResetMountTransition();
	
	// 메인 카메라를 RiderCharacter의 것으로 전환
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (PlayerController)
	{
		PlayerController->SetViewTargetWithBlend(this, MountCameraTransitionTime, 
											EViewTargetBlendFunction::VTBlend_EaseInOut);
	}
}

UAnimMontage* ARiderCharacter::LoadAnimMontageByMountDirection(EMountDirection Direction) const
{
	const FString* MontagePath = nullptr;
	switch (Direction)
	{
	case EMountDirection::Left:  MontagePath = &LeftMountMontagePath; break;
	case EMountDirection::Right: MontagePath = &RightMountMontagePath; break;
	case EMountDirection::Back:  MontagePath = &BackMountMontagePath; break;
	default: return nullptr;
	}

	return MontagePath && !MontagePath->empty()
		? FAnimationManager::Get().LoadMontage(*MontagePath)
		: nullptr;
}

bool ARiderCharacter::DoMount(AHorseCharacter* Horse, UAnimMontage* Montage, EMountDirection Direction)
{
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage || !Horse || Montage->GetSectionIndex(FName("Mount")) < 0)
	{
		UE_LOG("[RiderCharacter] Rider montage has no 'Mount' section.");
		return false;
	}

	bMountTransitionInProgress = true;
	PendingHorse = Horse;
	PendingMountDirection = Direction;
	CharacterMovement->StopMovementImmediately(); // 누적된 input vector 정리, CMC로 인한 yaw 회전 방지
	SnapToMountStart(Horse, Montage);
	AnimInstance->PlayMontage(Montage, FName("Mount"));
	return true;
}

bool ARiderCharacter::DoUnmount(AHorseCharacter* Horse, UAnimMontage* Montage)
{
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage || !Horse || Montage->GetSectionIndex(FName("Unmount")) < 0)
	{
		UE_LOG("[RiderCharacter] Rider montage has no 'Unmount' section.");
		return false;
	}

	bMountTransitionInProgress = true;
	PendingHorse = Horse;
	PendingMontage = Montage;
	CharacterMovement->StopMovementImmediately(); // 누적된 input vector 정리, CMC로 인한 yaw 회전 방지
	SnapToUnmountStart(Horse, Montage);
	AnimInstance->PlayMontage(Montage, FName("Unmount"));
	return true;
}

bool ARiderCharacter::GetMontageSectionRootTransform(
	const UAnimMontage* Montage,
	const FName& SectionName,
	float SectionTime,
	FTransform& OutTransform) const
{
	if (!Montage)
	{
		return false;
	}

	const FCompositeSection* Section = Montage->FindSection(SectionName);
	UAnimSequence* SourceSequence = Montage->GetSourceSequence();
	if (!Section || !SourceSequence)
	{
		return false;
	}

	const float SequenceTime = std::clamp(
		Section->StartTime + SectionTime,
		Section->StartTime,
		Section->LinkTime);
	return SourceSequence->TryGetRawRootMotionTransformAtTime(SequenceTime, OutTransform);
}

void ARiderCharacter::SnapToMountStart(AHorseCharacter* Horse, const UAnimMontage* Montage)
{
	USceneComponent* HorseRoot = Horse ? Horse->GetRootComponent() : nullptr;
	USceneComponent* RiderRoot = GetRootComponent();
	if (!HorseRoot || !RiderRoot)
	{
		return;
	}

	// Mounted Idle의 actor root pose를 기준점으로 사용한다. Mount curve의 마지막 Z를
	// 빼면 animation 종료 시의 visual Z와 constraint가 적용된 뒤의 visual Z가 일치한다.
	const FMatrix MountedRootWorld =
		FTransform(MountLocationOffset, MountRotationOffset, FVector(1.0f, 1.0f, 1.0f)).ToMatrix()
		* HorseRoot->GetWorldMatrix();
	FVector StartLocation = MountedRootWorld.GetLocation();

	const FCompositeSection* MountSection = Montage ? Montage->FindSection(FName("Mount")) : nullptr;
	FTransform MountEndRoot;
	if (MountSection && GetMontageSectionRootTransform(Montage, FName("Mount"),
		MountSection->LinkTime - MountSection->StartTime, MountEndRoot))
	{
		StartLocation.Z -= MountEndRoot.Location.Z;
	}
	else
	{
		UE_LOG("[Rider] Could not read Mount root curve. Mount start Z was not adjusted.");
	}

	RiderRoot->SetWorldLocation(StartLocation);
	RiderRoot->SetWorldRotation(HorseRoot->GetWorldRotation());
}

void ARiderCharacter::SnapToUnmountStart(AHorseCharacter* Horse, const UAnimMontage* Montage)
{
	USceneComponent* HorseRoot = Horse ? Horse->GetRootComponent() : nullptr;
	USceneComponent* RiderRoot = GetRootComponent();
	if (!HorseRoot || !RiderRoot || !ParentConstraintComponent)
	{
		return;
	}

	// Mount 완료 시 constraint가 넣었던 위치 보정을 반대로 적용한다. Unmount curve의
	// 첫 root pose(Z = 말 높이)가 지면 기준 Rider root 위에서 정확히 seat 위치가 된다.
	const FMatrix MountedRootWorld =
		FTransform(MountLocationOffset, MountRotationOffset, FVector(1.0f, 1.0f, 1.0f)).ToMatrix()
		* HorseRoot->GetWorldMatrix();
	FVector StartLocation = MountedRootWorld.GetLocation();

	FTransform UnmountStartRoot;
	if (GetMontageSectionRootTransform(Montage, FName("Unmount"), 0.0f, UnmountStartRoot))
	{
		StartLocation.Z -= UnmountStartRoot.Location.Z;
	}
	else
	{
		UE_LOG("[Rider] Could not read Unmount root curve. Unmount start Z was not adjusted.");
	}

	ParentConstraintComponent->Detach();
	RiderRoot->SetWorldLocation(StartLocation);
	RiderRoot->SetWorldRotation(HorseRoot->GetWorldRotation());
}

void ARiderCharacter::SnapToUnmountEnd(AHorseCharacter* Horse, const UAnimMontage* Montage)
{
	USceneComponent* RiderRoot = GetRootComponent();
	if (!Horse || !RiderRoot)
	{
		return;
	}

	const FCompositeSection* Section = Montage ? Montage->FindSection(FName("Unmount")) : nullptr;
	FTransform UnmountStartRoot;
	FTransform UnmountEndRoot;
	if (!Section ||
		!GetMontageSectionRootTransform(Montage, FName("Unmount"), 0.0f, UnmountStartRoot) ||
		!GetMontageSectionRootTransform(Montage, FName("Unmount"),
			Section->LinkTime - Section->StartTime, UnmountEndRoot))
	{
		UE_LOG("[Rider] Could not read Unmount root curve. Keeping the mounted actor position.");
		return;
	}

	// Root curve는 mesh component local space에 있다. 시작 pose를 기준으로 한 curve delta를
	// mesh relative transform으로 actor root space에 옮긴 뒤, 하차 시작 root world pose에 합성한다.
	const FMatrix MeshRelative = Mesh ? Mesh->GetRelativeTransform().ToMatrix() : FMatrix::Identity;
	const FMatrix RootDelta = UnmountStartRoot.ToMatrix().GetInverse() * UnmountEndRoot.ToMatrix();
	const FMatrix UnmountedRootWorld =
		MeshRelative.GetInverse() * RootDelta * MeshRelative * RiderRoot->GetWorldMatrix();

	RiderRoot->SetWorldLocation(UnmountedRootWorld.GetLocation());
	RiderRoot->SetWorldRotation(UnmountedRootWorld.ToQuat());
}

void ARiderCharacter::ResetMountTransition()
{
	bMountTransitionInProgress = false;
	PendingHorse.Reset();
	PendingMontage.Reset();
}

void ARiderCharacter::MoveForward(float Value)
{
	if (IsMountingOrDismounting()) return;
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
	if (IsMountingOrDismounting()) return;
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
	if (IsMountingOrDismounting()) return;
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
	if (IsMountingOrDismounting()) return;
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
	if (IsMountingOrDismounting()) return;
	if (MountedHorse)
	{
		MountedHorse->RequestGiddyup();
		return;
	}
	Jump();
}

void ARiderCharacter::SlowDownHorse()
{
	if (IsMountingOrDismounting()) return;
	if (MountedHorse)
	{
		MountedHorse->RequestSlowDown();
	}
}

void ARiderCharacter::StopHorse()
{
	if (IsMountingOrDismounting()) return;
	if (MountedHorse)
	{
		MountedHorse->RequestStop();
	}
}

void ARiderCharacter::SetHorseGaze(float Value)
{
	if (IsMountingOrDismounting()) return;
	if (MountedHorse)
	{
		MountedHorse->SetGazeInput(Value);
	}
}

void ARiderCharacter::Whistle()
{
	if (MountedHorse || IsMountingOrDismounting()) return;
	UWorld* World = GetWorld();
	if (!World) return;

	AHorseCharacter* BestHorse = nullptr;
	float BestDistance = FLT_MAX;
	int32 OwnedHorseCount = 0;
	for (AActor* Actor : World->GetActors())
	{
		AHorseCharacter* Horse = Cast<AHorseCharacter>(Actor);
		if (!Horse || !Horse->IsPlayerOwnedHorse()) continue;
		++OwnedHorseCount;
		const float Distance = FVector::Distance(GetActorLocation(), Horse->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestHorse = Horse;
		}
	}
	if (OwnedHorseCount > 1)
	{
		UE_LOG("[Rider] Multiple player-owned horses found (%d); whistling to nearest.", OwnedHorseCount);
	}
	if (BestHorse)
	{
		BestHorse->RequestWhistleCall(GetActorLocation());
	}
}
