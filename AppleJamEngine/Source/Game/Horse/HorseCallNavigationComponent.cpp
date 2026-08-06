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
	SetStatus(EHorseCallNavigationStatus::Idle);
	SetNavigationDirection(FVector::ZeroVector, false);
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
	if (!IsCallActive()) return;

	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse) return;
	if (OwnerHorse->IsRiderMounted())
	{
		NotifyMounted();
		return;
	}

	if (Status == EHorseCallNavigationStatus::Aligning)
	{
		if (CurrentWaypoint < 0 || CurrentWaypoint >= PathPoints.size())
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
			SetNavigationDirection(Direction, false);
			if (BlackboardComponent)
			{
				BlackboardComponent->GetBlackboard().SetInt(HorseBBKeys::DesiredGait, static_cast<int>(EHorseGait::Trot));
			}
			return;
		}

		AlignmentTime += (std::max)(0.0f, DeltaTime);
		SetNavigationDirection(Direction, true);
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
	SetMaxGait(EHorseGait::Gallop);
	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse || OwnerHorse->IsRiderMounted())
	{
		return;
	}

	RawTarget = TargetLocation;
	PathPoints.clear();
	CurrentWaypoint = 0;
	NoProgressTime = 0.0f;
	AlignmentTime = 0.0f;
	bPlannedPartial = false;
	ActiveVolume.Reset();
	SetStatus(EHorseCallNavigationStatus::Planning);
	if (BlackboardComponent)
	{
		FBlackboard& BB = BlackboardComponent->GetBlackboard();
		BB.SetBool(HorseBBKeys::CallRequested, true);
		BB.SetInt(HorseBBKeys::DesiredGait, static_cast<int>(EHorseGait::Stop));
	}
	OwnerHorse->RequestStop();

	AVoxelNavigationVolume* Volume = FindNavigationVolume(OwnerHorse->GetActorLocation(), RawTarget);
	if (!Volume)
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoVolume);
		return;
	}
	if (!Volume->IsNavigationBuilt() && !Volume->RebuildNavigation())
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoPath);
		return;
	}
	ActiveVolume = Volume;

	const FVoxelNavigationPathResult Path = Volume->FindPath(OwnerHorse->GetActorLocation(), RawTarget);
	if (!Path.bSuccess)
	{
		StopAtTerminalStatus(
			Path.Failure == FVoxelNavigationPathResult::EFailure::NoStart
				? EHorseCallNavigationStatus::FailedNoStart
				: EHorseCallNavigationStatus::FailedNoPath);
		return;
	}

	PathPoints = Path.Points;
	bPlannedPartial = Path.bPartial;
	if (PathPoints.empty())
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoPath);
		return;
	}

	CurrentWaypoint = PathPoints.size() > 1 ? 1 : 0;
	ProgressAnchor = OwnerHorse->GetActorLocation();
	FVector InitialDirection = PathPoints[CurrentWaypoint] - OwnerHorse->GetActorLocation();
	InitialDirection.Z = 0.0f;
	if (FVector::Distance(OwnerHorse->GetActorLocation(), RawTarget) <= ArrivalRadius)
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::Reached);
	}
	else if (!InitialDirection.IsNearlyZero() && GetPlanarAngleTo(InitialDirection) > AlignmentCompleteAngleDeg)
	{
		SetStatus(EHorseCallNavigationStatus::Aligning);
		SetNavigationDirection(InitialDirection, true);
	}
	else
	{
		SetStatus(EHorseCallNavigationStatus::Following);
		SetNavigationDirection(InitialDirection, false);
		if (BlackboardComponent)
		{
			BlackboardComponent->GetBlackboard().SetInt(HorseBBKeys::DesiredGait, static_cast<int>(EHorseGait::Trot));
		}
	}

	UE_LOG("[HorseCallNavigation] Planned Horse=%s Points=%d Partial=%d Length=%.3f SearchMs=%.3f Expanded=%d",
		OwnerHorse->GetName().c_str(), PathPoints.size(), Path.bPartial ? 1 : 0,
		Path.PathLength, Path.SearchTimeMs, Path.NumExpandedNodes);
}

void UHorseCallNavigationComponent::NotifyMounted()
{
	if (IsCallActive())
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::CompletedByMount);
	}
}

void UHorseCallNavigationComponent::CancelCall()
{
	SetNavigationDirection(FVector::ZeroVector, false);
	if (BlackboardComponent)
	{
		BlackboardComponent->GetBlackboard().SetBool(HorseBBKeys::CallRequested, false);
	}
	PathPoints.clear();
	ActiveVolume.Reset();
	SetMaxGait(EHorseGait::Gallop);
	SetStatus(EHorseCallNavigationStatus::Idle);
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
	if (AHorseCharacter* OwnerHorse = Horse.Get())
	{
		BlackboardComponent = OwnerHorse->GetComponentByClass<UBlackboardComponent>();
		LocomotionComponent = OwnerHorse->GetComponentByClass<UHorseLocomotionComponent>();
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

void UHorseCallNavigationComponent::SetNavigationDirection(const FVector& Direction, bool bAligning)
{
	if (!BlackboardComponent) return;
	FVector PlanarDirection = Direction;
	PlanarDirection.Z = 0.0f;
	const bool bHasDirection = !PlanarDirection.IsNearlyZero();
	if (bHasDirection) PlanarDirection.Normalize();
	FBlackboard& BB = BlackboardComponent->GetBlackboard();
	BB.SetVector(HorseBBKeys::NavigationDirection, PlanarDirection);
	BB.SetBool(HorseBBKeys::NavigationHasDirection, bHasDirection);
	BB.SetBool(HorseBBKeys::NavigationAligning, bAligning && bHasDirection);
}

void UHorseCallNavigationComponent::StopAtTerminalStatus(EHorseCallNavigationStatus TerminalStatus)
{
	SetNavigationDirection(FVector::ZeroVector, false);
	SetStatus(TerminalStatus);
	if (BlackboardComponent)
	{
		BlackboardComponent->GetBlackboard().SetInt(HorseBBKeys::DesiredGait, static_cast<int>(EHorseGait::Stop));
	}
	if (Horse)
	{
		Horse->RequestStop();
	}
	SetMaxGait(EHorseGait::Gallop); // MaxGait 제한 해제
}

void UHorseCallNavigationComponent::AdvanceFollowing(float DeltaTime)
{
	AHorseCharacter* OwnerHorse = Horse.Get();
	if (!OwnerHorse) return;
	const FVector HorseLocation = OwnerHorse->GetActorLocation();
	if (FVector::Distance(HorseLocation, RawTarget) <= ArrivalRadius)
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::Reached);
		return;
	}

	// 현재 waypoint에 도달했다면 다음 waypoint 선정
	while (CurrentWaypoint < PathPoints.size())
	{
		const bool bIsFinalWaypoint = CurrentWaypoint == (PathPoints.size() - 1);
		float AcceptanceRadius = WaypointRadius;
		if (bIsFinalWaypoint && !bPlannedPartial)
		{
			const float EndpointTargetDistance = FVector::Distance(PathPoints.back(), RawTarget);
			AcceptanceRadius = std::max(0.05f, ArrivalRadius - EndpointTargetDistance);
		}
		if (FVector::Distance(HorseLocation, PathPoints[CurrentWaypoint]) > AcceptanceRadius)
		{
			break;
		}
		CurrentWaypoint++;
	}
	if (CurrentWaypoint >= PathPoints.size())
	{
		const bool bReachedRawTarget = FVector::Distance(HorseLocation, RawTarget) <= ArrivalRadius;
		StopAtTerminalStatus(bReachedRawTarget
			? EHorseCallNavigationStatus::Reached
			: EHorseCallNavigationStatus::ReachedPartial);
		return;
	}

	// Pure pursuit을 사용하여 Gait 계산
	const FVector LookaheadPoint = GetLookaheadPoint(HorseLocation);
	FVector Direction = LookaheadPoint - HorseLocation;
	Direction.Z = 0.0f;
	UpdatePurePursuitGaitLimit(HorseLocation, LookaheadPoint);

	SetNavigationDirection(Direction, false);
	if (FVector::Distance(HorseLocation, ProgressAnchor) >= MinProgressDist)
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
	if (CurrentWaypoint < 0 || CurrentWaypoint >= PathPoints.size())
	{
		UE_LOG("[UHorseCallNavigationComponent] CurrentWaypoint is out of range");
		return HorseLocation;
	}

	float RemainingDistance = std::max(0.01f, LookaheadDistance);
	FVector SegmentStart = HorseLocation;
	for (int Index = CurrentWaypoint; Index < PathPoints.size(); Index++)
	{
		const FVector SegmentEnd = PathPoints[Index];
		FVector Segment = SegmentEnd - SegmentStart;
		const float SegmentLength = Segment.Length();
		if (SegmentLength > RemainingDistance)
		{
			return SegmentStart + (SegmentEnd - SegmentStart) * (RemainingDistance / SegmentLength);
		}
		RemainingDistance -= SegmentLength;
		SegmentStart = SegmentEnd;
	}

	return PathPoints.back();
}

void UHorseCallNavigationComponent::UpdatePurePursuitGaitLimit(const FVector& HorseLocation, const FVector& LookaheadPoint)
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

	// 곡률 기반으로 보법 계산
	const float LateralOffset =  Forward.X * ToLookahead.Y - Forward.Y * ToLookahead.X; // Left에 투영한 ToLookahead의 길이
	const float Curvature = 2.0f * LateralOffset / (LookaheadLength * LookaheadLength); // rad/m
	const float RequiredCurvatureDeg = std::abs(Curvature) * RAD_TO_DEG;              // deg/m
	SetMaxGait(GetMaxGaitForCurvature(RequiredCurvatureDeg));

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
	if (RequiredCurvatureDeg <= TrotMaxCurvature)   return EHorseGait::Trot;
	return EHorseGait::Walk;
}

void UHorseCallNavigationComponent::SetMaxGait(EHorseGait InMaxGait)
{
	if (UHorseLocomotionComponent* Locomotion = LocomotionComponent.Get())
	{
		Locomotion->SetMaxGait(InMaxGait);
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
			(Index == CurrentWaypoint) ? FColor::Yellow() : FColor(0, 190, 255) );
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
