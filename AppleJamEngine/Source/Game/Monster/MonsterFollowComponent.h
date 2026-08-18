#pragma once

#include "Component/ActorComponent.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Game/Monster/MonsterFollowComponent.generated.h"

class AActor;
class AVoxelNavigationVolume;
class UCharacterMovementComponent;

UENUM()
enum class EMonsterFollowStatus : uint8
{
	Idle,
	Tracking,
	Reached,
	LostTarget,
	FailedNoCharacterMovement,
	FailedNoNavigation,
	FailedNoPath,
};

// Navigation 테스트용 컴포넌트
// TargetActorName 이름을 가진 액터 방향으로 길찾기 & 경로 추종
UCLASS()
class UMonsterFollowComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UMonsterFollowComponent();
	~UMonsterFollowComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;

	UFUNCTION(Pure, Category="Monster|Follow")
	EMonsterFollowStatus GetStatus() const { return Status; }
	UFUNCTION(Pure, Category="Monster|Follow")
	bool IsFollowing() const { return Status == EMonsterFollowStatus::Tracking; }

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	AActor* ResolveTargetActor();
	AVoxelNavigationVolume* FindNavigationVolume(const FVector& Start, const FVector& Goal) const;
	bool BuildPath(AActor& Owner, const FVector& TargetLocation);
	bool ShouldReplan(const FVector& TargetLocation) const;
	void AdvanceAlongPath(AActor& Owner, UCharacterMovementComponent& CharacterMovement);
	void ConfigureCharacterMovement(UCharacterMovementComponent& CharacterMovement);
	void StopCharacterMovement();
	void DrawPathDebug(const FVector& OwnerLocation) const;
	void ClearPath();
	void StopFollowing(EMonsterFollowStatus NewStatus);
	void SetStatus(EMonsterFollowStatus NewStatus);

	TWeakObjectPtr<AActor> TargetActor = nullptr;
	TWeakObjectPtr<UCharacterMovementComponent> ActiveCharacterMovement = nullptr;
	TArray<FVector> PathPoints;
	FVector LastPlannedTargetLocation = FVector::ZeroVector;
	int32 CurrentWaypoint = 0;
	float RepathElapsed = 1000.0f;
	float SavedMaxWalkSpeed = 0.0f;
	bool bSavedUseInstantMovementInput = false;
	bool bCharacterMovementSettingsOverridden = false;
	bool bLoggedMissingCharacterMovement = false;

	UPROPERTY(Edit, Save, Category="Monster|Follow", DisplayName="Target Actor Name")
	FString TargetActorName;
	UPROPERTY(Edit, Save, Category="Monster|Follow", DisplayName="Sight Range", Min=0.1f, Max=10000.0f, Speed=0.1f)
	float SightRange = 15.0f;
	UPROPERTY(Edit, Save, Category="Monster|Follow", DisplayName="Acceptable Radius", Min=0.05f, Max=100.0f, Speed=0.05f)
	float AcceptableRadius = 1.5f;
	UPROPERTY(Edit, Save, Category="Monster|Follow", DisplayName="Move Speed", Min=0.0f, Max=1000.0f, Speed=0.1f)
	float MoveSpeed = 3.0f;
	UPROPERTY(Edit, Save, Category="Monster|Follow", DisplayName="Path Retry Interval", Min=0.05f, Max=10.0f, Speed=0.05f)
	float PathRefreshInterval = 0.5f;
	UPROPERTY(Edit, Save, Category="Monster|Follow", DisplayName="Target Repath Distance", Min=0.05f, Max=100.0f, Speed=0.05f)
	float TargetRepathDistance = 0.5f;
	UPROPERTY(Edit, Save, Category="Monster|Follow|Debug", DisplayName="Draw Debug")
	bool bDrawDebug = false;
	UPROPERTY(Edit, ReadOnly, Transient, Category="Monster|Follow", DisplayName="Status")
	EMonsterFollowStatus Status = EMonsterFollowStatus::Idle;
};
