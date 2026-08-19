#pragma once

#include "GameFramework/Pawn/Character.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include "Game/Rider/MountTriggerComponent.h"

#include "Source/Game/Rider/RiderCharacter.generated.h"

class AHorseCharacter;
class UAnimMontage;
class UParentConstraintComponent;
class USpringArmComponent;
class UCameraComponent;
class UNavigationStreamingComponent;
class UAnimGraphInstance;
class ULuaScriptComponent;

// Player-controlled walking character.
// While mounted it sends commands to AHorseCharacter.
UCLASS()
class ARiderCharacter : public ACharacter
{
public:
	GENERATED_BODY()
	ARiderCharacter();
	~ARiderCharacter() override = default;

	void InitDefaultComponents(const FString& SkeletalMeshFileName) override;
	void PostDuplicate() override;
	void OnPostLoad(FArchive& Ar) override;

	UFUNCTION(Callable, Category = "Rider|Mount")
	bool Mount();

	UFUNCTION(Callable, Category = "Rider|Mount")
	bool Unmount();

	UFUNCTION(Pure, Category = "Rider|Mount")
	bool IsMounted() const { return MountedHorse != nullptr; }

	UFUNCTION(Pure, Category = "Rider|Mount")
	bool IsMountingOrDismounting() const { return bMountTransitionInProgress; }

	UFUNCTION(Pure, Category = "Rider|Mount")
	AHorseCharacter* GetMountedHorse() const { return MountedHorse; }

	// UMountTriggerComponent overlap 콜백 → MountDirection 세팅
	void SetMountTrigger(UMountTriggerComponent* Trigger);
	void ClearMountTrigger(UMountTriggerComponent* Trigger);

	// Mount/Unmount section 끝에 배치한 AnimNotify 이벤트 핸들러
	void OnMountFinishNotify();
	void OnUnmountFinishNotify();

	UFUNCTION(Callable, Category="Rider|Horse")
	void Whistle();

protected:
	void SetupInputComponent() override;

private:
	UPROPERTY(Edit, Save, Category="Rider|Mount", DisplayName="Mount Location Offset", Type=Vec3, Speed=0.01f)
	FVector MountLocationOffset = FVector(0.18f, 0.0f, 1.9f);
	UPROPERTY(Edit, Save, Category="Rider|Mount", DisplayName="Mount Rotation Offset", Type=Rotator, Speed=0.1f)
	FRotator MountRotationOffset = FRotator(FVector(0.0, 7.0f, 0.0f)); // pitch만 7도
	UPROPERTY(Edit, Save, Category="Rider|Mount", DisplayName="Mount Camera Transition", Min=0.0f, Max=2.0f, Speed=0.1f)
	float MountCameraTransitionTime = 0.35f;
	UPROPERTY(Edit, Save, Category="Rider|Mount|Montage", DisplayName="Left Montage")
	FString LeftMountMontagePath = "Content/Montages/Rider_Mount_Dismount_Left_Take_001_Montage.uasset";
	UPROPERTY(Edit, Save, Category="Rider|Mount|Montage", DisplayName="Right Montage")
	FString RightMountMontagePath = "Content/Montages/Rider_Mount_Dismount_Right_Take_001_Montage.uasset";
	UPROPERTY(Edit, Save, Category="Rider|Mount|Montage", DisplayName="Back Montage")
	FString BackMountMontagePath = "Content/Montages/Rider_Mount_Dismount_Back_Take_001_Montage.uasset";

private:
	// ─── Mount/Unmount 관련 ───
	UAnimMontage* LoadAnimMontageByMountDirection(EMountDirection Direction) const;
	bool DoMount(AHorseCharacter* Horse, UAnimMontage* Montage, EMountDirection Direction);
	bool DoUnmount(AHorseCharacter* Horse, UAnimMontage* Montage);
	// 몽타주 섹션의 특정 시각에서의 Transform 가져오기: Root motion 없이 Root bone과 원하는 시점에만 동기화
	// 이하의 Snap 계통의 함수에서 사용
	bool GetMontageSectionRootTransform(const UAnimMontage* Montage, const FName& SectionName,
										float SectionTime, FTransform& OutTransform) const; 
	void SnapToMountStart(AHorseCharacter* Horse, const UAnimMontage* Montage);
	void SnapToUnmountStart(AHorseCharacter* Horse, const UAnimMontage* Montage);
	void SnapToUnmountEnd(AHorseCharacter* Horse, const UAnimMontage* Montage);
	void ResetMountTransition();

	// ─── Input 전달 관련 ───
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void JumpOrGiddyup();
	void SlowDownHorse();
	void StopHorse();
	void SetHorseGaze(float Value);

	TWeakObjectPtr<UParentConstraintComponent> ParentConstraintComponent = nullptr;
	TWeakObjectPtr<UMountTriggerComponent> AvailableMountTrigger = nullptr;
	TWeakObjectPtr<AHorseCharacter> MountedHorse = nullptr;
	TWeakObjectPtr<AHorseCharacter> PendingHorse = nullptr;
	TWeakObjectPtr<UAnimMontage> PendingMontage = nullptr;
	bool bMountTransitionInProgress = false;
	EMountDirection PendingMountDirection = EMountDirection::Left;
	EMountDirection LastMountDirection = EMountDirection::Left;
	TWeakObjectPtr<USpringArmComponent> SpringArm = nullptr;
	TWeakObjectPtr<UCameraComponent> Camera = nullptr;
	TWeakObjectPtr<UNavigationStreamingComponent> NavigationStreamingComp = nullptr;
};
