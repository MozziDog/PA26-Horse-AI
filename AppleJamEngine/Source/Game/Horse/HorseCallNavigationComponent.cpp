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
		if (CurrentWaypoint < 0 || CurrentWaypoint >= static_cast<int32>(PathPoints.size()))
		{
			StopAtTerminalStatus(EHorseCallNavigationStatus::FailedNoPath);
			return;
		}

		FVector Direction = PathPoints[static_cast<size_t>(CurrentWaypoint)] - OwnerHorse->GetActorLocation();
		Direction.Z = 0.0f;
		if (Direction.IsNearlyZero() || GetPlanarAngleTo(Direction) <= AlignmentCompleteAngle)
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
	FVector InitialDirection = PathPoints[static_cast<size_t>(CurrentWaypoint)] - OwnerHorse->GetActorLocation();
	InitialDirection.Z = 0.0f;
	if (FVector::Distance(OwnerHorse->GetActorLocation(), RawTarget) <= ArrivalRadius)
	{
		StopAtTerminalStatus(EHorseCallNavigationStatus::Reached);
	}
	else if (!InitialDirection.IsNearlyZero() && GetPlanarAngleTo(InitialDirection) > AlignmentCompleteAngle)
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
		OwnerHorse->GetName().c_str(), static_cast<int32>(PathPoints.size()), Path.bPartial ? 1 : 0,
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

	while (CurrentWaypoint < static_cast<int32>(PathPoints.size()) &&
		FVector::Distance(HorseLocation, PathPoints[static_cast<size_t>(CurrentWaypoint)]) <= WaypointRadius)
	{
		++CurrentWaypoint;
	}
	if (CurrentWaypoint >= static_cast<int32>(PathPoints.size()))
	{
		StopAtTerminalStatus(bPlannedPartial
			? EHorseCallNavigationStatus::ReachedPartial
			: EHorseCallNavigationStatus::Reached);
		return;
	}

	FVector Direction = PathPoints[static_cast<size_t>(CurrentWaypoint)] - HorseLocation;
	Direction.Z = 0.0f;
	SetNavigationDirection(Direction, false);

	if (FVector::Distance(HorseLocation, ProgressAnchor) >= MinimumProgressDistance)
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

void UHorseCallNavigationComponent::DrawPathDebug() const
{
	UWorld* World = GetWorld();
	if (!World) return;
	for (size_t Index = 0; Index < PathPoints.size(); ++Index)
	{
		const FVector Point = PathPoints[Index] + FVector::UpVector * 0.12f;
		DrawDebugSphere(World, Point, 0.12f, 8,
			static_cast<int32>(Index) == CurrentWaypoint ? FColor::Yellow() : FColor(0, 190, 255));
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
