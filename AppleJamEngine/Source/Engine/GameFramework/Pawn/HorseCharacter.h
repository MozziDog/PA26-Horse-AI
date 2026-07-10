#pragma once
#include "GameFramework/Pawn/Pawn.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Pawn/HorseCharacter.generated.h"

class USkeletalMeshComponent;
class UBoxComponent;
class UHorseMovementComponent;
class UHorseLocomotionComponent;
class UBTAgentComponent;
class UObstacleFanSensorComponent;
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

protected:
	void SetupInputComponent() override;
	void RebindComponents();
	void UpdateCameraControlRotation();
	void UpdateCameraReturn(float DeltaTime);
	float GetCameraBaseYaw() const;

protected:
	TWeakObjectPtr<UBoxComponent> CollisionComponent = nullptr;
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent = nullptr;
	TWeakObjectPtr<UHorseMovementComponent> MovementComponent = nullptr;
	TWeakObjectPtr<UHorseLocomotionComponent> LocomotionComponent = nullptr;
	TWeakObjectPtr<UBTAgentComponent> BTAgentComponent = nullptr;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComponent = nullptr;
	TWeakObjectPtr<UObstacleFanSensorComponent> ObstacleFanSensorComponent = nullptr;
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

	// steering 축 콜백이 매 frame 채우는 값(0 포함). 카메라 자동복귀의 "입력 활성" 판정에 쓴다.
	// 실제 조향은 콜백에서 BB UserMoveDir 로 기록되어 Locomotion arbiter 가 소비한다.
	float LastSteeringInput = 0.0f;
};

