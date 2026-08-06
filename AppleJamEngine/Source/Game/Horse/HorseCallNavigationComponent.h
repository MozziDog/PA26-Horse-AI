#pragma once

#include "Component/ActorComponent.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Game/Horse/HorseCallNavigationComponent.generated.h"

class AHorseCharacter;
class AVoxelNavigationVolume;
class UBlackboardComponent;
class UHorseLocomotionComponent;
enum class EHorseGait : uint8;

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
	void NotifyMounted();		// 플레이어가 탑승하면 경로 추종은 종료
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
	FVector GetLookaheadPoint(const FVector& HorseLocation) const;
	void UpdatePurePursuitGaitLimit(const FVector& HorseLocation, const FVector& LookaheadPoint);
	EHorseGait GetMaxGaitForCurvature(float RequiredCurvatureDeg) const;
	void SetMaxGait(EHorseGait InMaxGait);

	TWeakObjectPtr<AHorseCharacter> Horse = nullptr;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComponent = nullptr;
	TWeakObjectPtr<UHorseLocomotionComponent> LocomotionComponent = nullptr;
	TWeakObjectPtr<AVoxelNavigationVolume> ActiveVolume = nullptr;

	TArray<FVector> PathPoints;
	FVector RawTarget = FVector::ZeroVector;
	int CurrentWaypoint = 0;
	float AlignmentTime = 0.0f;
	bool bPlannedPartial = false;

	// StuckTimeout 시간동안 위치가 MinProgressDist이상 변화하지 않으면 끼임으로 판정, 경로 추종 종료
	float NoProgressTime = 0.0f;	
	FVector ProgressAnchor = FVector::ZeroVector;

	UPROPERTY(Edit, ReadOnly, Transient, Category="Horse|Call", DisplayName="Status")
	EHorseCallNavigationStatus Status = EHorseCallNavigationStatus::Idle;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Arrival Radius", Min=0.1f, Max=20.0f, Speed=0.1f)
	float ArrivalRadius = 2.0f;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Waypoint Radius", Min=0.1f, Max=5.0f, Speed=0.05f)
	float WaypointRadius = 2.0f;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Alignment Complete Angle", Min=1.0f, Max=90.0f)
	float AlignmentCompleteAngleDeg = 30.0f;
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Alignment Timeout", Min=0.1f, Max=30.0f, Speed=0.1f)
	float AlignmentTimeout = 5.0f;  // 출발 시 align을 이 시간동안 완수하지 못하면 '실패'로 판정 (좁은 골목에 갇혀있는 등)
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Minimum Progress Distance", Min=0.01f, Max=5.0f)
	float MinProgressDist = 0.25f;	// Stuck 아님으로 판정하기 위한 최소 이동 거리
	UPROPERTY(Edit, Save, Category="Horse|Call", DisplayName="Stuck Timeout", Min=0.1f, Max=30.0f, Speed=0.1f)
	float StuckTimeout = 3.0f;		// 이 시간보다 길게 유의미한 위치 변화가 없으면 '실패'로 판정
	UPROPERTY(Edit, Save, Category="Horse|Call|Pure Pursuit", DisplayName="Lookahead Distance", Min=0.1f, Max=30.0f)
	float LookaheadDistance = 3.0f;

	// deg/m. 각 보법의 최대 '초당 yaw 변화량'(deg/s)를 보법의 속력(m/s)로 나눠서 구한 최대 선회 곡률
	// pure-pursuit curvature랑 비교해서 적절한 보법을 선택할 때 사용
	// NOTE: 실제 값보다 조금 작게 설정하면 보수적으로, 조금 크게 설정하면 과감하게 움직임
	UPROPERTY(Edit, Save, Category="Horse|Pure Pursuit", DisplayName="Walk Max Yaw Rate Per Speed", Min=0.0f, Max=720.0f)
	float WalkMaxCurvature = 70.0f;
	UPROPERTY(Edit, Save, Category="Horse|Pure Pursuit", DisplayName="Trot Max Yaw Rate Per Speed", Min=0.0f, Max=720.0f)
	float TrotMaxCurvature = 45.0f;
	UPROPERTY(Edit, Save, Category="Horse|Pure Pursuit", DisplayName="Canter Max Yaw Rate Per Speed", Min=0.0f, Max=720.0f)
	float CanterMaxCurvature = 36.0f;
	UPROPERTY(Edit, Save, Category="Horse|Pure Pursuit", DisplayName="Gallop Max Yaw Rate Per Speed", Min=0.0f, Max=720.0f)
	float GallopMaxCurvature = 10.0f;

	UPROPERTY(Edit, Save, Category="Horse|Debug", DisplayName="Draw Path")
	bool bDrawPath = true;
	UPROPERTY(Edit, Save, Category="Horse|Debug", DisplayName="Draw Pure Pursuit")
	bool bDrawPurePursuit = true;
};
