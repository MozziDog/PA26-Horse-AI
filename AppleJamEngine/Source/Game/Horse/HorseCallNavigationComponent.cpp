#include "pch.h"

#include "Game/Horse/HorseCallNavigationComponent.h"

#include "AI/Blackboard.h"
#include "AI/Navigation/VoxelNavigationVolume.h"
#include "Component/AI/BlackboardComponent.h"
#include "Core/Logging/Log.h"
#include "Core/TickFunction.h"
#include "Debug/DrawDebugHelpers.h"
#include "Game/Horse/HorseCharacter.h"
#include "Game/Horse/HorseConstants.h"
#include "Game/Horse/HorseLocomotionComponent.h"
#include "GameFramework/World.h"

#include <algorithm>
#include <cmath>

namespace
{
	float PlanarDistance(const FVector& A, const FVector& B)
	{
		const float DX = A.X - B.X;
		const float DY = A.Y - B.Y;
		return std::sqrt(DX * DX + DY * DY);
	}

	const char* ToString(EHorseCallNavigationStatus Status)
	{
		switch (Status)
		{
		case EHorseCallNavigationStatus::Idle: return "Idle";
		case EHorseCallNavigationStatus::Planning: return "Planning";
		case EHorseCallNavigationStatus::Aligning: return "Aligning";
		case EHorseCallNavigationStatus::Following: return "Following";
		case EHorseCallNavigationStatus::Reached: return "Reached";
		case EHorseCallNavigationStatus::ReachedPartial: return "ReachedPartial";
		case EHorseCallNavigationStatus::CompletedByMount: return "CompletedByMount";
		case EHorseCallNavigationStatus::AbortedStuck: return "AbortedStuck";
		case EHorseCallNavigationStatus::AbortedAlignment: return "AbortedAlignment";
		case EHorseCallNavigationStatus::FailedNoVolume: return "FailedNoVolume";
		case EHorseCallNavigationStatus::FailedNoData: return "FailedNoData";
		case EHorseCallNavigationStatus::FailedNoStart: return "FailedNoStart";
		case EHorseCallNavigationStatus::FailedNoPath: return "FailedNoPath";
		default: return "Unknown";
		}
	}
}

UHorseCallNavigationComponent::UHorseCallNavigationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PrePhysics);
}

void UHorseCallNavigationComponent::BeginPlay()
{
	Super::BeginPlay();
	RebindOwnerComponents();
	RecommendedGait = EHorseGait::Trot;
	SetStatus(EHorseCallNavigationStatus::Idle);
	ClearGuidance();
}

void UHorseCallNavigationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (bDrawPath && !PathPoints.empty())
	{
		DrawPathDebug();
	}
	if (!IsCallActive())
	{
		return;
	}

	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse)
	{
		return;
	}
	if (OwnerHorse->IsRiderMounted())
	{
		NotifyMounted();
		return;
	}

	if (Status == EHorseCallNavigationStatus::Aligning)
	{
		if (!HasValidCurrentWaypoint())
		{
			StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoPath);
			return;
		}

		FVector Direction = PathPoints[CurrentWaypoint] - OwnerHorse->GetActorLocation();
		Direction.Z = 0.0f;
		if (Direction.IsNearlyZero() || GetPlanarAngleTo(Direction) <= AlignmentCompleteAngleDeg)
		{
			AlignmentTime = 0.0f;
			SetStatus(EHorseCallNavigationStatus::Following);
			PublishGuidance(Direction);
			return;
		}

		AlignmentTime += (std::max)(0.0f, DeltaTime);
		PublishGuidance(Direction);
		if (AlignmentTime >= AlignmentTimeout)
		{
			StopAtTerminalStatus(EHorseCallNavigationStatus::AbortedAlignment);
		}
		return;
	}

	if (Status == EHorseCallNavigationStatus::Following)
	{
		AdvanceFollowing(DeltaTime);
	}
}

void UHorseCallNavigationComponent::RequestCall(const FVector& TargetLocation)
{
	RebindOwnerComponents();
	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse || OwnerHorse->IsRiderMounted())
	{
		return;
	}

	RawTarget = TargetLocation;
	ResetPlanState();
	RecommendedGait = EHorseGait::Trot;
	ClearGuidance();
	SetStatus(EHorseCallNavigationStatus::Idle);
	SetCallRequested(true);
}

bool UHorseCallNavigationComponent::BeginPlan()
{
	RebindOwnerComponents();
	AHorseCharacter* OwnerHorse = Horse.Get();
	bool bRequested = false;
	if (!OwnerHorse || !BlackboardComponent ||
		!BlackboardComponent->GetBlackboard().TryGetBool(HorseBBKeys::CallRequested, bRequested) || !bRequested)
	{
		return false;
	}
	if (bPlanReady)
	{
		return true;
	}

	ResetPlanState();
	ClearGuidance();
	SetStatus(EHorseCallNavigationStatus::Planning);

	AVoxelNavigationVolume* Volume = FindNavigationVolume(OwnerHorse->GetActorLocation(), RawTarget);
	if (!Volume)
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoVolume);
		return false;
	}
	if (!Volume->IsNavigationBuilt())
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoData);
		return false;
	}
	ActiveVolume = Volume;

	const FVoxelNavigationPathResult Path = Volume->FindPath(OwnerHorse->GetActorLocation(), RawTarget);
	if (!Path.bSuccess)
	{
		EHorseCallNavigationStatus FailureStatus = EHorseCallNavigationStatus::FailedNoPath;
		if (Path.Failure == FVoxelNavigationPathResult::EFailure::NoData)
		{
			FailureStatus = EHorseCallNavigationStatus::FailedNoData;
		}
		else if (Path.Failure == FVoxelNavigationPathResult::EFailure::NoStart)
		{
			FailureStatus = EHorseCallNavigationStatus::FailedNoStart;
		}
		UE_LOG("[HorseCallNavigation] Plan failed Horse=%s Failure=%d",
			OwnerHorse->GetName().c_str(), static_cast<int32>(Path.Failure));
		StopAtTerminalStatus(FailureStatus);
		return false;
	}

	PathPoints = Path.Points;
	bPlannedPartial = Path.bPartial;
	if (PathPoints.empty())
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoPath);
		return false;
	}

	CurrentWaypoint = PathPoints.size() > 1 ? 1 : 0;
	ProgressAnchor = OwnerHorse->GetActorLocation();
	bPlanReady = true;
	UE_LOG("[HorseCallNavigation] Planned Horse=%s Points=%d Partial=%d Length=%.3f SearchMs=%.3f Expanded=%d",
		OwnerHorse->GetName().c_str(), PathPoints.size(), Path.bPartial ? 1 : 0,
		Path.PathLength, Path.SearchTimeMs, Path.NumExpandedNodes);
	return true;
}

bool UHorseCallNavigationComponent::BeginAlignToPathStart()
{
	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse || !bPlanReady || !HasValidCurrentWaypoint())
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoPath);
		return false;
	}
	if (FVector::Distance(OwnerHorse->GetActorLocation(), RawTarget) <= ArrivalRadius)
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::Reached);
		return false;
	}

	AlignmentTime = 0.0f;
	SetStatus(EHorseCallNavigationStatus::Aligning);
	FVector Direction = PathPoints[CurrentWaypoint] - OwnerHorse->GetActorLocation();
	Direction.Z = 0.0f;
	PublishGuidance(Direction);
	return true;
}

bool UHorseCallNavigationComponent::BeginFollowPath()
{
	if (IsTerminalCallNavigationStatus(Status))
	{
		return false;
	}

	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse || !bPlanReady || !HasValidCurrentWaypoint())
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoPath);
		return false;
	}

	SetStatus(EHorseCallNavigationStatus::Following);
	const FVector LookaheadPoint = GetLookaheadPoint(OwnerHorse->GetActorLocation());
	FVector Direction = LookaheadPoint - OwnerHorse->GetActorLocation();
	Direction.Z = 0.0f;
	UpdatePurePursuitRecommendedGait(OwnerHorse->GetActorLocation(), LookaheadPoint);
	PublishGuidance(Direction);
	return true;
}

void UHorseCallNavigationComponent::NotifyMounted()
{
	if (IsCallActive() || bPlanReady)
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::CompletedByMount);
	}
	SetCallRequested(false);
}

void UHorseCallNavigationComponent::CancelCall()
{
	ClearGuidance();
	ResetPlanState();
	RecommendedGait = EHorseGait::Trot;
	SetCallRequested(false);
	SetStatus(EHorseCallNavigationStatus::Idle);
}

void UHorseCallNavigationComponent::ClearGuidance()
{
	PublishGuidance(FVector::ZeroVector);
}

bool UHorseCallNavigationComponent::IsCallActive() const
{
	return Status == EHorseCallNavigationStatus::Planning ||
		Status == EHorseCallNavigationStatus::Aligning ||
		Status == EHorseCallNavigationStatus::Following;
}

void UHorseCallNavigationComponent::RebindOwnerComponents()
{
	Horse = Cast<AHorseCharacter>(GetOwner());
	BlackboardComponent.Reset();
	if (AHorseCharacter* OwnerHorse = Horse.Get())
	{
		BlackboardComponent = OwnerHorse->GetComponentByClass<UBlackboardComponent>();
	}
}

AVoxelNavigationVolume* UHorseCallNavigationComponent::FindNavigationVolume(const FVector& Start, const FVector& Goal) const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	for (AActor* Actor : World->GetActors())
	{
		AVoxelNavigationVolume* Volume = Cast<AVoxelNavigationVolume>(Actor);
		if (Volume && Volume->Contains(Start) && Volume->Contains(Goal))
		{
			return Volume;
		}
	}
	return nullptr;
}

void UHorseCallNavigationComponent::SetStatus(EHorseCallNavigationStatus NewStatus)
{
	if (Status != NewStatus)
	{
		UE_LOG("[HorseCallNavigation] Status Horse=%s %s -> %s",
			Horse ? Horse->GetName().c_str() : "None", ToString(Status), ToString(NewStatus));
	}
	Status = NewStatus;
	if (BlackboardComponent)
	{
		BlackboardComponent->GetBlackboard().SetInt(HorseBBKeys::CallStatus, static_cast<int>(Status));
	}
}

void UHorseCallNavigationComponent::PublishGuidance(const FVector& Direction)
{
	if (!BlackboardComponent)
	{
		RebindOwnerComponents();
	}
	if (!BlackboardComponent)
	{
		return;
	}

	FVector PlanarDirection = Direction;
	PlanarDirection.Z = 0.0f;
	const bool bHasDirection = !PlanarDirection.IsNearlyZero();
	if (bHasDirection)
	{
		PlanarDirection.Normalize();
	}
	FBlackboard& Blackboard = BlackboardComponent->GetBlackboard();
	Blackboard.SetVector(HorseBBKeys::GuidanceDirection, bHasDirection ? PlanarDirection : FVector::ZeroVector);
	Blackboard.SetFloat(HorseBBKeys::GuidanceWeight, bHasDirection ? std::max(0.0f, NavigationWeight) : 0.0f);
}

void UHorseCallNavigationComponent::StopAtTerminalStatus(EHorseCallNavigationStatus TerminalStatus)
{
	ClearGuidance();
	bPlanReady = false;
	SetStatus(TerminalStatus);
}

void UHorseCallNavigationComponent::ResetPlanState()
{
	PathPoints.clear();
	CurrentWaypoint = 0;
	AlignmentTime = 0.0f;
	NoProgressTime = 0.0f;
	ProgressAnchor = FVector::ZeroVector;
	bPlannedPartial = false;
	bPlanReady = false;
	ActiveVolume.Reset();
}

bool UHorseCallNavigationComponent::HasValidCurrentWaypoint() const
{
	return CurrentWaypoint >= 0 && CurrentWaypoint < static_cast<int>(PathPoints.size());
}

float UHorseCallNavigationComponent::GetCurrentWaypointAcceptanceRadius() const
{
	const bool bIsFinalWaypoint = CurrentWaypoint == static_cast<int>(PathPoints.size()) - 1;
	if (!bIsFinalWaypoint || bPlannedPartial)
	{
		return WaypointRadius;
	}

	const float EndpointTargetDistance = FVector::Distance(PathPoints.back(), RawTarget);
	return std::max(0.05f, ArrivalRadius - EndpointTargetDistance);
}

void UHorseCallNavigationComponent::AdvanceFollowing(float DeltaTime)
{
	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse || !HasValidCurrentWaypoint())
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoPath);
		return;
	}
	const FVector HorseLocation = OwnerHorse->GetActorLocation();
	if (FVector::Distance(HorseLocation, RawTarget) <= ArrivalRadius)
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::Reached);
		return;
	}

	while (HasValidCurrentWaypoint())
	{
		const bool bIsExactFinalWaypoint =
			CurrentWaypoint == static_cast<int>(PathPoints.size()) - 1 && !bPlannedPartial;
		const float WaypointDistance = bIsExactFinalWaypoint
			? FVector::Distance(HorseLocation, PathPoints[CurrentWaypoint])
			: PlanarDistance(HorseLocation, PathPoints[CurrentWaypoint]);
		if (WaypointDistance > GetCurrentWaypointAcceptanceRadius())
		{
			break;
		}
		++CurrentWaypoint;
	}
	if (CurrentWaypoint >= static_cast<int>(PathPoints.size()))
	{
		const bool bReachedRawTarget = FVector::Distance(HorseLocation, RawTarget) <= ArrivalRadius;
		StopAtTerminalStatus(bReachedRawTarget
			? EHorseCallNavigationStatus::Reached
			: EHorseCallNavigationStatus::ReachedPartial);
		return;
	}

	const FVector LookaheadPoint = GetLookaheadPoint(HorseLocation);
	FVector Direction = LookaheadPoint - HorseLocation;
	Direction.Z = 0.0f;
	UpdatePurePursuitRecommendedGait(HorseLocation, LookaheadPoint);
	PublishGuidance(Direction);

	if (PlanarDistance(HorseLocation, ProgressAnchor) >= MinProgressDist)
	{
		ProgressAnchor = HorseLocation;
		NoProgressTime = 0.0f;
	}
	else
	{
		NoProgressTime += (std::max)(0.0f, DeltaTime);
		if (NoProgressTime >= StuckTimeout)
		{
			StopAtTerminalStatus(EHorseCallNavigationStatus::AbortedStuck);
		}
	}
}

FVector UHorseCallNavigationComponent::GetLookaheadPoint(const FVector& HorseLocation) const
{
	if (!HasValidCurrentWaypoint())
	{
		UE_LOG("[UHorseCallNavigationComponent] CurrentWaypoint is out of range");
		return HorseLocation;
	}

	float RemainingDistance = std::max(0.01f, LookaheadDistance);
	FVector SegmentStart = HorseLocation;
	for (int Index = CurrentWaypoint; Index < static_cast<int>(PathPoints.size()); ++Index)
	{
		const FVector SegmentEnd = PathPoints[Index];
		const FVector Segment = SegmentEnd - SegmentStart;
		const float SegmentLength = PlanarDistance(SegmentEnd, SegmentStart);
		if (SegmentLength > RemainingDistance)
		{
			return SegmentStart + Segment * (RemainingDistance / SegmentLength);
		}
		RemainingDistance -= SegmentLength;
		SegmentStart = SegmentEnd;
	}

	return PathPoints.back();
}

void UHorseCallNavigationComponent::UpdatePurePursuitRecommendedGait(const FVector& HorseLocation, const FVector& LookaheadPoint)
{
	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse)
	{
		return;
	}

	FVector Forward = OwnerHorse->GetActorForward();
	FVector ToLookahead = LookaheadPoint - HorseLocation;
	Forward.Z = 0.0f;
	ToLookahead.Z = 0.0f;
	const float LookaheadLength = ToLookahead.Length();
	if (Forward.IsNearlyZero() || LookaheadLength <= 1.e-3f)
	{
		return;
	}
	Forward.Normalize();

	const float LateralOffset = Forward.X * ToLookahead.Y - Forward.Y * ToLookahead.X;
	const float Curvature = 2.0f * LateralOffset / (LookaheadLength * LookaheadLength);
	const float RequiredCurvatureDeg = std::abs(Curvature) * RAD_TO_DEG;
	RecommendedGait = GetMaxGaitForCurvature(RequiredCurvatureDeg);

	if (bDrawPurePursuit)
	{
		if (UWorld* World = GetWorld())
		{
			const FVector DebugOrigin = HorseLocation + FVector::UpVector * 0.2f;
			const FColor PurePursuitColor(255, 0, 255);
			DrawDebugSphere(World, LookaheadPoint + FVector::UpVector * 0.2f, 0.16f, 8, PurePursuitColor);
			DrawDebugLine(World, DebugOrigin, LookaheadPoint + FVector::UpVector * 0.2f, PurePursuitColor);
		}
	}
}

EHorseGait UHorseCallNavigationComponent::GetMaxGaitForCurvature(float RequiredCurvatureDeg) const
{
	if (RequiredCurvatureDeg <= GallopMaxCurvature) return EHorseGait::Gallop;
	if (RequiredCurvatureDeg <= CanterMaxCurvature) return EHorseGait::Canter;
	if (RequiredCurvatureDeg <= TrotMaxCurvature) return EHorseGait::Trot;
	return EHorseGait::Walk;
}

void UHorseCallNavigationComponent::SetCallRequested(bool bRequested)
{
	if (BlackboardComponent)
	{
		BlackboardComponent->GetBlackboard().SetBool(HorseBBKeys::CallRequested, bRequested);
	}
}

void UHorseCallNavigationComponent::DrawPathDebug() const
{
	UWorld* World = GetWorld();
	if (!World) return;
	for (size_t Index = 0; Index < PathPoints.size(); ++Index)
	{
		const FVector Point = PathPoints[Index] + FVector::UpVector * 0.12f;
		DrawDebugSphere(World, Point, 0.12f, 4,
			(static_cast<int>(Index) == CurrentWaypoint) ? FColor::Yellow() : FColor(0, 190, 255));
		if (Index > 0)
		{
			DrawDebugLine(World, PathPoints[Index - 1] + FVector::UpVector * 0.12f, Point, FColor(0, 190, 255));
		}
	}
}

float UHorseCallNavigationComponent::GetPlanarAngleTo(const FVector& Direction) const
{
	const AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse) return 180.0f;
	FVector Forward = OwnerHorse->GetActorForward();
	FVector PlanarDirection = Direction;
	Forward.Z = 0.0f;
	PlanarDirection.Z = 0.0f;
	if (Forward.IsNearlyZero() || PlanarDirection.IsNearlyZero()) return 0.0f;
	Forward.Normalize();
	PlanarDirection.Normalize();
	const float Dot = std::clamp(Forward.Dot(PlanarDirection), -1.0f, 1.0f);
	return std::acos(Dot) * 180.0f / 3.14159265358979323846f;
}
