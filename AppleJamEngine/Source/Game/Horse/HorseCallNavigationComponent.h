#pragma once

#include "Component/ActorComponent.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Game/Horse/HorseCallNavigationComponent.generated.h"

class AHorseCharacter;
class AVoxelNavigationVolume;
class UBlackboardComponent;

UENUM()
enum class EHorseCallNavigationStatus : uint8
{
	Idle,
	Planning,
	Aligning,
	Following,
	Reached,
	ReachedPartial,
	CompletedByMount,
	AbortedStuck,
	AbortedAlignment,
	FailedNoVolume,
	FailedNoStart,
	FailedNoPath,
};

UCLASS()
class UHorseCallNavigationComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UHorseCallNavigationComponent();

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	UFUNCTION(Callable, Category="Horse|Call")
	void RequestCall(const FVector& TargetLocation);
	UFUNCTION(Callable, Category="Horse|Call")
	void NotifyMounted();
	UFUNCTION(Callable, Category="Horse|Call")
	void CancelCall();

	UFUNCTION(Pure, Category="Horse|Call")
	EHorseCallNavigationStatus GetStatus() const { return Status; }
	UFUNCTION(Pure, Category="Horse|Call")
	bool IsCallActive() const;

private:
	void RebindOwnerComponents();
	AVoxelNavigationVolume* FindNavigationVolume(const FVector& Start, const FVector& Goal) const;
	void SetStatus(EHorseCallNavigationStatus NewStatus);
	void SetNavigationDirection(const FVector& Direction, bool bAligning);
	void StopAtTerminalStatus(EHorseCallNavigationStatus TerminalStatus);
	void AdvanceFollowing(float DeltaTime);
	void DrawPathDebug() const;
	float GetPlanarAngleTo(const FVector& Direction) const;

	TWeakObjectPtr<AHorseCharacter> Horse = nullptr;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComponent = nullptr;
	TWeakObjectPtr<AVoxelNavigationVolume> ActiveVolume = nullptr;
	TArray<FVector> PathPoints;
	FVector RawTarget = FVector::ZeroVector;
	FVector ProgressAnchor = FVector::ZeroVector;
	int32 CurrentWaypoint = 0;
	float NoProgressTime = 0.0f;
	float AlignmentTime = 0.0f;
	bool bPlannedPartial = false;

	UPROPERTY(Edit, ReadOnly, Transient, Category="Horse|Call", DisplayName="Status")
	EHorseCallNavigationStatus Status = EHorseCallNavigationStatus::Idle;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Arrival Radius", Min=0.1f, Max=20.0f, Speed=0.1f)
	float ArrivalRadius = 2.0f;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Waypoint Radius", Min=0.1f, Max=5.0f, Speed=0.05f)
	float WaypointRadius = 0.75f;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Alignment Complete Angle", Min=1.0f, Max=90.0f, Speed=1.0f)
	float AlignmentCompleteAngle = 30.0f;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Alignment Timeout", Min=0.1f, Max=30.0f, Speed=0.1f)
	float AlignmentTimeout = 5.0f;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Minimum Progress Distance", Min=0.01f, Max=5.0f, Speed=0.05f)
	float MinimumProgressDistance = 0.25f;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Stuck Timeout", Min=0.1f, Max=30.0f, Speed=0.1f)
	float StuckTimeout = 3.0f;
	UPROPERTY(Edit, Save, Category="Horse|Call|Debug", DisplayName="Draw Path")
	bool bDrawPath = true;
};
