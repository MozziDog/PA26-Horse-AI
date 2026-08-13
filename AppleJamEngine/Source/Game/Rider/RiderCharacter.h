#pragma once

#include "GameFramework/Pawn/Character.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Game/Rider/RiderCharacter.generated.h"

class AHorseCharacter;
class UParentConstraintComponent;
class USpringArmComponent;
class UCameraComponent;
class UNavigationStreamingComponent;

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
	AHorseCharacter* GetMountedHorse() const { return MountedHorse; }

	UFUNCTION(Callable, Category="Rider|Horse")
	void Whistle();

protected:
	void SetupInputComponent() override;

private:
	UPROPERTY(Edit, Save, Category="Rider|Mount", DisplayName="Mount Location Offset", Type=Vec3, Speed=0.01f)
	FVector MountLocationOffset = FVector(0.0f, 0.0f, 1.25f);
	UPROPERTY(Edit, Save, Category="Rider|Mount", DisplayName="Mount Rotation Offset", Type=Rotator, Speed=0.1f)
	FRotator MountRotationOffset = FRotator::ZeroRotator;
	UPROPERTY(Edit, Save, Category="Rider|Mount", DisplayName="Mount Camera Transition", Min=0.0f, Max=2.0f, Speed=0.1f)
	float MountCameraTransitionTime = 0.35f;

private:
	// 임시로 하드코딩된 이름으로 탑승 대상 액터(말) 찾기
	AHorseCharacter* FindMountTarget() const;
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void JumpOrGiddyup();
	void SlowDownHorse();
	void StopHorse();
	void SetHorseGaze(float Value);

	TWeakObjectPtr<UParentConstraintComponent> ParentConstraintComponent = nullptr;
	TWeakObjectPtr<AHorseCharacter> MountedHorse = nullptr;
	TWeakObjectPtr<USpringArmComponent> SpringArm = nullptr;
	TWeakObjectPtr<UCameraComponent> Camera = nullptr;
	TWeakObjectPtr<UNavigationStreamingComponent> NavigationStreamingComp = nullptr;
};
