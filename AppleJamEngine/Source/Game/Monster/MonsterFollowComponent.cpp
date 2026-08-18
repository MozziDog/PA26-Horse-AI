#include "pch.h"

#include "Game/Monster/MonsterFollowComponent.h"

#include "AI/Navigation/VoxelNavigationVolume.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Core/Logging/Log.h"
#include "Core/TickFunction.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"

#include <algorithm>

namespace
{
	constexpr float MinimumWaypointRadius = 0.05f;

	float PlanarDistance(const FVector& A, const FVector& B)
	{
		FVector Delta = A - B;
		Delta.Z = 0.0f;
		return Delta.Length();
	}
}

UMonsterFollowComponent::UMonsterFollowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PrePhysics);
}

void UMonsterFollowComponent::BeginPlay()
{
	Super::BeginPlay();
	SetStatus(EMonsterFollowStatus::Idle);
}

void UMonsterFollowComponent::EndPlay()
{
	StopCharacterMovement();
	ClearPath();
	TargetActor.Reset();
	SetStatus(EMonsterFollowStatus::Idle);
	Super::EndPlay();
}

void UMonsterFollowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	AActor* Owner = GetOwner();
	AActor* Target = ResolveTargetActor();
	if (!Owner)
	{	
		StopFollowing(EMonsterFollowStatus::Idle);
		return;
	}
	UCharacterMovementComponent* CharacterMovement = Owner->GetComponentByClass<UCharacterMovementComponent>();
	if (!CharacterMovement)
	{
		if (!bLoggedMissingCharacterMovement)
		{
			UE_LOG("[MonsterFollow] Owner=%s requires UCharacterMovementComponent to follow a navigation path.", Owner->GetName().c_str());
			bLoggedMissingCharacterMovement = true;
		}
		StopFollowing(EMonsterFollowStatus::FailedNoCharacterMovement);
		return;
	}

	const bool bHasTarget = Target && Target != Owner;
	const FVector TargetLocation = bHasTarget ? Target->GetActorLocation() : FVector::ZeroVector;
	const float DistanceToTarget = bHasTarget
		? PlanarDistance(Owner->GetActorLocation(), TargetLocation)
		: 0.0f;
	const bool bTargetInSight = bHasTarget && DistanceToTarget <= std::max(0.0f, SightRange);
	const bool bTargetReached = bHasTarget && DistanceToTarget <= std::max(MinimumWaypointRadius, AcceptableRadius);

	if (Status != EMonsterFollowStatus::Tracking)
	{
		// After reaching the last known location, wait until the target comes into
		// sight again instead of immediately starting another path request.
		if (Status == EMonsterFollowStatus::LostTarget && !bTargetInSight)
		{
			return;
		}

		if (!bTargetInSight)
		{
			StopFollowing(EMonsterFollowStatus::Idle);
			return;
		}
		if (bTargetReached)
		{
			StopFollowing(EMonsterFollowStatus::Reached);
			return;
		}

		RepathElapsed += std::max(0.0f, DeltaTime);
		if (ShouldReplan(TargetLocation))
		{
			BuildPath(*Owner, TargetLocation);
		}
		if (PathPoints.empty())
		{
			StopCharacterMovement();
			return;
		}
	}
	else
	{
		if (bTargetReached)
		{
			StopFollowing(EMonsterFollowStatus::Reached);
			return;
		}

		RepathElapsed += std::max(0.0f, DeltaTime);
		// Once tracking has started, losing sight only prevents replanning. The
		// current path remains valid until its final waypoint is reached.
		if (bTargetInSight && ShouldReplan(TargetLocation))
		{
			BuildPath(*Owner, TargetLocation);
			if (PathPoints.empty())
			{
				StopCharacterMovement();
				return;
			}
		}
	}

	if (PathPoints.empty())
	{
		StopFollowing(EMonsterFollowStatus::LostTarget);
		return;
	}
	SetStatus(EMonsterFollowStatus::Tracking);
	ConfigureCharacterMovement(*CharacterMovement);
	if (bDrawDebug)
	{
		DrawPathDebug(Owner->GetActorLocation());
	}
	AdvanceAlongPath(*Owner, *CharacterMovement);

	if (PathPoints.empty())
	{
		const bool bReachedAfterMove = Target && Target != Owner &&
			PlanarDistance(Owner->GetActorLocation(), Target->GetActorLocation()) <= std::max(MinimumWaypointRadius, AcceptableRadius);
		StopFollowing(bReachedAfterMove ? EMonsterFollowStatus::Reached : EMonsterFollowStatus::LostTarget);
	}
}

AActor* UMonsterFollowComponent::ResolveTargetActor()
{
	if (TargetActorName.empty())
	{
		TargetActor.Reset();
		return nullptr;
	}

	if (AActor* CachedTarget = TargetActor.Get())
	{
		if (CachedTarget->GetName() == TargetActorName)
		{
			return CachedTarget;
		}
	}

	TargetActor.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (Actor && Actor->GetName() == TargetActorName)
		{
			TargetActor = Actor;
			return Actor;
		}
	}

	return nullptr;
}

AVoxelNavigationVolume* UMonsterFollowComponent::FindNavigationVolume(const FVector& Start, const FVector& Goal) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

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

bool UMonsterFollowComponent::BuildPath(AActor& Owner, const FVector& TargetLocation)
{
	ClearPath();
	RepathElapsed = 0.0f;
	LastPlannedTargetLocation = TargetLocation;

	AVoxelNavigationVolume* Volume = FindNavigationVolume(Owner.GetActorLocation(), TargetLocation);
	if (!Volume || !Volume->IsNavigationBuilt())
	{
		SetStatus(EMonsterFollowStatus::FailedNoNavigation);
		return false;
	}

	const FVoxelNavigationPathResult Path = Volume->FindPath(Owner.GetActorLocation(), TargetLocation);
	if (!Path.bSuccess || Path.Points.empty())
	{
		SetStatus(EMonsterFollowStatus::FailedNoPath);
		return false;
	}

	PathPoints = Path.Points;
	CurrentWaypoint = PathPoints.size() > 1 ? 1 : 0;
	return true;
}

bool UMonsterFollowComponent::ShouldReplan(const FVector& TargetLocation) const
{
	const float RefreshInterval = std::max(0.05f, PathRefreshInterval);
	const float TargetMoveThreshold = std::max(MinimumWaypointRadius, TargetRepathDistance);
	// A valid path must keep its waypoint progress. RefreshInterval is only a retry delay
	// for a failed/consumed path; replanning an active path makes corners visibly jitter.
	if (PathPoints.empty())
	{
		return RepathElapsed >= RefreshInterval;
	}

	return PlanarDistance(TargetLocation, LastPlannedTargetLocation) >= TargetMoveThreshold;
}

void UMonsterFollowComponent::AdvanceAlongPath(AActor& Owner, UCharacterMovementComponent& CharacterMovement)
{
	const float WaypointRadius = std::max(MinimumWaypointRadius, AcceptableRadius * 0.25f);
	const FVector OwnerLocation = Owner.GetActorLocation();

	while (CurrentWaypoint < static_cast<int32>(PathPoints.size()))
	{
		FVector ToWaypoint = PathPoints[CurrentWaypoint] - OwnerLocation;
		ToWaypoint.Z = 0.0f;
		const float DistanceToWaypoint = ToWaypoint.Length();
		if (DistanceToWaypoint <= WaypointRadius)
		{
			++CurrentWaypoint;
			continue;
		}

		CharacterMovement.AddInputVector(ToWaypoint * (1.0f / DistanceToWaypoint));
		return;
	}

	ClearPath();
}

void UMonsterFollowComponent::ConfigureCharacterMovement(UCharacterMovementComponent& CharacterMovement)
{
	if (!bCharacterMovementSettingsOverridden || ActiveCharacterMovement.Get() != &CharacterMovement)
	{
		StopCharacterMovement();
		ActiveCharacterMovement = &CharacterMovement;
		SavedMaxWalkSpeed = CharacterMovement.MaxWalkSpeed;
		bSavedUseInstantMovementInput = CharacterMovement.bUseInstantMovementInput;
		bCharacterMovementSettingsOverridden = true;
	}

	CharacterMovement.MaxWalkSpeed = std::max(0.0f, MoveSpeed);
	CharacterMovement.bUseInstantMovementInput = true;
}

void UMonsterFollowComponent::StopCharacterMovement()
{
	UCharacterMovementComponent* CharacterMovement = ActiveCharacterMovement.Get();
	if (!CharacterMovement)
	{
		bCharacterMovementSettingsOverridden = false;
		return;
	}

	CharacterMovement->StopMovementImmediately();
	if (bCharacterMovementSettingsOverridden)
	{
		CharacterMovement->MaxWalkSpeed = SavedMaxWalkSpeed;
		CharacterMovement->bUseInstantMovementInput = bSavedUseInstantMovementInput;
	}
	ActiveCharacterMovement.Reset();
	bCharacterMovementSettingsOverridden = false;
}

void UMonsterFollowComponent::DrawPathDebug(const FVector& OwnerLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector PreviousPoint = OwnerLocation + FVector::UpVector * 0.12f;
	for (int32 Index = CurrentWaypoint; Index < static_cast<int32>(PathPoints.size()); ++Index)
	{
		const FVector Point = PathPoints[Index] + FVector::UpVector * 0.12f;
		const FColor Color = Index == CurrentWaypoint ? FColor::Yellow() : FColor(0, 190, 255);
		DrawDebugSphere(World, Point, 0.12f, 4, Color);
		DrawDebugLine(World, PreviousPoint, Point, Color);
		PreviousPoint = Point;
	}
}

void UMonsterFollowComponent::ClearPath()
{
	PathPoints.clear();
	CurrentWaypoint = 0;
}

void UMonsterFollowComponent::StopFollowing(EMonsterFollowStatus NewStatus)
{
	if (bCharacterMovementSettingsOverridden)
	{
		StopCharacterMovement();
	}
	ClearPath();
	RepathElapsed = std::max(0.05f, PathRefreshInterval);
	SetStatus(NewStatus);
}

void UMonsterFollowComponent::SetStatus(EMonsterFollowStatus NewStatus)
{
	Status = NewStatus;
}
