#pragma once
#include "GameFramework/Pawn/Pawn.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Game/Horse/HorseCharacter.generated.h"

class USkeletalMeshComponent;
class USceneComponent;
class UCapsuleComponent;
class UHorseMovementComponent;
class UHorseLocomotionComponent;
class UBTAgentComponent;
class UObstacleFanSensorComponent;
class UCliffFanSensorComponent;
class URoadSensorComponent;
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

	UFUNCTION(Pure, Catergory="Character|Components")
	UHorseMovementComponent* GetHorseMovement() const { return MovementComponent; }

protected:
	void OnPostLoad(FArchive& Ar) override; // Re-Initialize after save & load

protected:
	void SetupInputComponent() override;
	void RebindComponents();
	void UpdateCameraControlRotation();
	void UpdateCameraReturn(float DeltaTime);
	float GetCameraBaseYaw() const;

protected:
	TWeakObjectPtr<USceneComponent> RootSceneComponent = nullptr;
	TWeakObjectPtr<UCapsuleComponent> CollisionComponent = nullptr;
	TWeakObjectPtr<USkeletalMeshComponent> MeshComponent = nullptr;
	TWeakObjectPtr<UHorseMovementComponent> MovementComponent = nullptr;
	TWeakObjectPtr<UHorseLocomotionComponent> LocomotionComponent = nullptr;
	TWeakObjectPtr<UBTAgentComponent> BTAgentComponent = nullptr;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComponent = nullptr;
	TWeakObjectPtr<UObstacleFanSensorComponent> ObstacleFanSensorComponent = nullptr;
	TWeakObjectPtr<UCliffFanSensorComponent> CliffFanSensorComponent = nullptr;
	TWeakObjectPtr<URoadSensorComponent> RoadSensorComponent = nullptr;
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

	// ── 플레이어 입력 관련 ─────────────────────────
	bool  bGazeInput = false;   // '전방 주시' 키(LShift / 게임패드 LT) 홀드 여부. 정지 상태에서 홀드하면 평행이동 모드로 진입
	float LastForwardInput = 0.0f;
	// 횡방향 입력. 현재 2군데서 사용
	// 1. 카메라 자동복귀의 '입력 활성' 판정 (조향은 Actor forward 기준이므로 카메라가 뒤에 있어야 조작 용이)
	// 2. 평행이동 모드에서 횡방향 입력으로 사용
	// 통상 주행 중 조향은 입력 이벤트 콜백에서 UserMoveDir 계산으로 처리하고 LastSteeringInput은 사용하지 않음
	// (SetupInputComponent() 참고)
	float LastSteeringInput = 0.0f;

	static constexpr float GamepadTriggerHoldThreshold = 0.5f;	// 게임패드 트리거는 아날로그 입력, 버튼처럼 쓰려면 기준값 필요
};

