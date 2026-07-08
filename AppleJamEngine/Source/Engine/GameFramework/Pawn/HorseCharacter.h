#pragma once
#include "GameFramework/Pawn/Pawn.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Pawn/HorseCharacter.generated.h"

class USkeletalMeshComponent;
class UHorseMovementComponent;
class UBTAgentComponent;
class UBlackboardComponent;
class USpringArmComponent;
class UCameraComponent;

UCLASS()
class AHorseCharacter : public APawn
{
public:
	GENERATED_BODY()
	AHorseCharacter() = default;
	~AHorseCharacter() override = default;

	virtual void InitDefaultComponents(const FString& SkeletalMeshFileName);
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void PostDuplicate() override;

protected:
	void OnPostLoad(FArchive& Ar) override; // Re-Initialize after save & load

public:
	// Component Getters
	UFUNCTION(Pure, Category = "Horse|Components")
	USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }
	// 실제 이동(입력 소비·지면 처리)을 담당하는 컴포넌트.
	UFUNCTION(Pure, Category = "Horse|Components")
	UHorseMovementComponent* GetMovementComponent() const { return MovementComponent; }
	// 플레이어 입력(throttle/steering)을 받아 이동 컴포넌트로 전달하는 컴포넌트.
	UFUNCTION(Pure, Category = "Horse|Components")
	UHorsePlayerInputComponent* GetPlayerInputComponent() const { return PlayerInputComponent; }
	UFUNCTION(Pure, Category = "Horse|Components")
	USpringArmComponent* GetSpringArmComponent() const { return SpringArmComponent; }
	UFUNCTION(Pure, Category = "Horse|Components")
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }

protected:
	void SetupInputComponent() override;
	void RebindComponents();
	void UpdateCameraControlRotation();
	void UpdateCameraReturn(float DeltaTime);
	float GetCameraBaseYaw() const;

protected:
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent = nullptr;
	TWeakObjectPtr<UHorseMovementComponent> MovementComponent = nullptr;
	TWeakObjectPtr<UHorsePlayerInputComponent> PlayerInputComponent = nullptr;
	TWeakObjectPtr<UBTAgentComponent> BTAgentComponent = nullptr;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComponent = nullptr;
	TWeakObjectPtr<USpringArmComponent> SpringArmComponent = nullptr;
	TWeakObjectPtr<UCameraComponent> CameraComponent = nullptr;

	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Auto Camera Input")
	bool bAutoInputCamera = true;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Enable Mouse Look")
	bool bEnableMouseLook = true;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Invert Mouse Y")
	bool bInvertMouseY = false;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Mouse Sensitivity", Min = 0.0f, Max = 10.0f, Speed = 0.01f)
	float MouseSensitivity = 0.10f;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Default Camera Pitch", Min = -89.0f, Max = 89.0f, Speed = 0.1f)
	float DefaultCameraPitch = 10.0f;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Min Camera Pitch", Min = -89.0f, Max = 89.0f, Speed = 0.1f)
	float MinCameraPitch = -5.0f;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Max Camera Pitch", Min = -89.0f, Max = 89.0f, Speed = 0.1f)
	float MaxCameraPitch = 25.0f;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Max Yaw Look Offset", Min = 0.0f, Max = 180.0f, Speed = 1.0f)
	float MaxCameraYawOffset = 135.0f;

	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Auto Return Camera")
	bool bAutoReturnCamera = true;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Camera Return Delay", Min = 0.0f, Max = 5.0f, Speed = 0.01f)
	float CameraReturnDelay = 1.5f;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Yaw Return Speed", Min = 0.0f, Max = 30.0f, Speed = 0.1f)
	float CameraYawReturnSpeed = 1.0f;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Pitch Return Speed", Min = 0.0f, Max = 30.0f, Speed = 0.1f)
	float CameraPitchReturnSpeed = 4.0f;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Moving Return Speed Multiplier", Min = 1.0f, Max = 10.0f, Speed = 0.1f)
	float CameraMovingReturnMultiplier = 1.75f;
	UPROPERTY(Edit, Save, Category = "Horse|Camera", DisplayName = "Moving Return Speed Threshold", Min = 0.0f, Max = 100.0f, Speed = 0.1f)
	float CameraMovingReturnSpeedThreshold = 0.5f;

	float CameraYawOffset = 0.0f;
	float CameraPitch = 10.0f;
	float CameraTimeSinceLookInput = 1000.0f;
	bool bCameraLookInputThisFrame = false;
	float LastThrottleInput = 0.0f;
	float LastSteeringInput = 0.0f;

	// TODO: [테스트] 센서 스탠드인용 누적 시간 — 실제 센서 컴포넌트 도입 시 제거.
	float BTTestElapsed = 0.0f;
};

