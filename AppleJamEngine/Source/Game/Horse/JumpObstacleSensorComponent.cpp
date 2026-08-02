#include "pch.h"
#include "JumpObstacleSensorComponent.h"

#include "Game/Horse/HorseConstants.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "HorseMovementComponent.h"
#include "Physics/IPhysicsScene.h"
#include "Core/TickFunction.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace
{
	FVector GetQueryNormal(const FHitResult& Hit)
	{
		if (!Hit.ImpactNormal.IsNearlyZero())
		{
			return Hit.ImpactNormal.Normalized();
		}
		if (!Hit.WorldNormal.IsNearlyZero())
		{
			return Hit.WorldNormal.Normalized();
		}
		return FVector::ZeroVector;
	}

	float NormalZFromSlopeDeg(float SlopeDeg)
	{
		return std::cos(std::clamp(SlopeDeg, 0.0f, 90.0f) * DEG_TO_RAD);
	}

	bool IsWalkableGroundHit(const FHitResult& Hit, float MaxSlopeDeg)
	{
		const FVector Normal = GetQueryNormal(Hit);
		return Hit.bHit && !Normal.IsNearlyZero() && Normal.Z >= NormalZFromSlopeDeg(MaxSlopeDeg);
	}
}

UJumpObstacleSensorComponent::UJumpObstacleSensorComponent()
{
	// Blackboard를 소비하는 Locomotion(TG_DuringPhysics)보다 먼저 최신 센서 값을 기록한다.
	PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PrePhysics);
}

void UJumpObstacleSensorComponent::BeginPlay()
{
	Super::BeginPlay();
	World = GetWorld();
	BlackboardComp = Owner->GetComponentByClass<UBlackboardComponent>();
	MovementComp = Owner->GetComponentByClass<UHorseMovementComponent>();
	ResetBlackboardResult();
}

void UJumpObstacleSensorComponent::Deactivate()
{
	ResetBlackboardResult();
	LastJumpProbeResult = EJumpProbeResult::NoObstacle;
	Super::Deactivate();
}

void UJumpObstacleSensorComponent::ResetBlackboardResult() const
{
	if (!BlackboardComp.IsValid())
	{
		return;
	}
	BlackboardComp->GetBlackboard().SetFloat(HorseBBKeys::ObsFwdDist, JumpProbeRange);
	BlackboardComp->GetBlackboard().SetBool(HorseBBKeys::ObsJumpable, false);
}

void UJumpObstacleSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	(void)DeltaTime; (void)TickType; (void)ThisTickFunction;

	if (!World.IsValid() || !Owner)
	{
		return;
	}
	IPhysicsScene* Physics = World->GetPhysicsScene();
	if (!Physics || !BlackboardComp.IsValid())
	{
		ResetBlackboardResult();
		return;
	}

	FVector Forward = GetForwardVector();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		ResetBlackboardResult();
		return;
	}
	Forward = Forward.Normalized();

	const FVector Origin = GetWorldLocation();
	const FVector LowOrigin = Origin - FVector(0.0f, 0.0f, JumpProbeDown);
	FHitResult LowHit;
	Physics->Raycast(LowOrigin, Forward, JumpProbeRange, LowHit,
		ECollisionChannel::WorldStatic, Owner);
	const float LowClear = LowHit.bHit ? LowHit.Distance : JumpProbeRange;

	const FVector HighOrigin = Origin + FVector(0.0f, 0.0f, JumpProbeUp);
	FHitResult HighHit;
	Physics->Raycast(HighOrigin, Forward, JumpProbeRange, HighHit,
		ECollisionChannel::WorldStatic, Owner);
	const float HighClear = HighHit.bHit ? HighHit.Distance : JumpProbeRange;

	// 오르막/도로 이음매를 실제 장애물과 구분한다. 진행 방향을 막는 face인지 검사하고,
	// hit 전후로 동일한 walkable ground가 이어지면 지면 전환으로 제외한다.
	const FVector LowNormal = GetQueryNormal(LowHit);
	const float ObstacleFacing = LowNormal.IsNearlyZero() ? 1.0f : -LowNormal.Dot(Forward);
	const bool bWallLikeHit = LowHit.bHit && ObstacleFacing >= MinObstacleFacing;

	FHitResult GroundBeforeHit;
	FHitResult GroundAfterHit;
	bool bGroundTransition = false;
	if (bWallLikeHit)
	{
		const float GroundProbeLength = GroundTransitionProbeUp + GroundTransitionProbeDown;
		const FVector BeforePoint = LowHit.WorldHitLocation - Forward * GroundTransitionProbeOffset;
		const FVector AfterPoint = LowHit.WorldHitLocation + Forward * GroundTransitionProbeOffset;
		const FVector BeforeStart = BeforePoint + FVector::UpVector * GroundTransitionProbeUp;
		const FVector AfterStart = AfterPoint + FVector::UpVector * GroundTransitionProbeUp;

		Physics->Raycast(BeforeStart, FVector::DownVector, GroundProbeLength, GroundBeforeHit,
			ECollisionChannel::WorldStatic, Owner);
		Physics->Raycast(AfterStart, FVector::DownVector, GroundProbeLength, GroundAfterHit,
			ECollisionChannel::WorldStatic, Owner);

		const bool bWalkableOnBothSides =
			IsWalkableGroundHit(GroundBeforeHit, MaxWalkableGroundDeg)
			&& IsWalkableGroundHit(GroundAfterHit, MaxWalkableGroundDeg);
		const bool bLowHitBelongsToGround =
			LowHit.HitComponent != nullptr
			&& (LowHit.HitComponent == GroundBeforeHit.HitComponent
				|| LowHit.HitComponent == GroundAfterHit.HitComponent);
		const float GroundHeightDelta =
			std::abs(GroundAfterHit.WorldHitLocation.Z - GroundBeforeHit.WorldHitLocation.Z);

		bGroundTransition =
			bWalkableOnBothSides
			&& bLowHitBelongsToGround
			&& GroundHeightDelta <= MaxGroundTransitionHeight;
	}

	constexpr float MinJumpUpSpace = 0.3f; // NOTE: 적당히 고른 임시값. 튜닝 필요
	const bool bHasUpperSpace = HighClear > LowClear + MinJumpUpSpace;

	EJumpProbeResult JumpProbeResult = EJumpProbeResult::NoObstacle;
	if (LowHit.bHit)
	{
		JumpProbeResult =
			!bWallLikeHit ? EJumpProbeResult::SurfaceNotFacing :
			bGroundTransition ? EJumpProbeResult::GroundTransition :
			!bHasUpperSpace ? EJumpProbeResult::InsufficientUpperSpace :
			EJumpProbeResult::Jumpable;
	}

	bool bJumpPassageTested = false;
	FVector JumpPassageStart = Origin;
	FVector JumpPassageEnd = Origin;
	const float JumpPassagePadding = std::max(0.0f, JumpPathBoxPadding);
	const FVector JumpPassageExtent(
		JumpPathHalfWidth + JumpPassagePadding,
		JumpPathHalfWidth + JumpPassagePadding,
		JumpPathHalfHeight + JumpPassagePadding);
	FHitResult JumpPathHit;
	if (JumpProbeResult == EJumpProbeResult::Jumpable && bCheckJumpTrajectory)
	{
		// 실제 JumpSpeed를 수직 성분으로 쓰되, 수평 성분은 고정 발사각으로 역산한다.
		// 궤적 전체가 아니라 장애물 앞~뒤 통과 구간만 넉넉한 box 한 번으로 근사한다.
		if (!MovementComp.IsValid())
		{
			JumpProbeResult = EJumpProbeResult::PathBlocked;
		}
		else
		{
			const float AngleRad = std::clamp(JumpTrajectoryAngle, 5.0f, 85.0f) * DEG_TO_RAD;
			const float TanAngle = std::tan(AngleRad);
			const float VerticalSpeed = MovementComp->GetJumpSpeed();
			const float HorizontalSpeed = TanAngle > 1.e-3f ? VerticalSpeed / TanAngle : 0.0f;
			const float PassageStartDistance =
				std::max(0.0f, LowClear - std::max(0.0f, JumpPathBeforeObstacle));
			const float PassageEndDistance =
				std::min(JumpProbeRange, LowClear + std::max(0.0f, JumpPathBeyondObstacle));

			if (VerticalSpeed <= 1.e-3f || HorizontalSpeed <= 1.e-3f
				|| PassageEndDistance <= PassageStartDistance)
			{
				JumpProbeResult = EJumpProbeResult::PathBlocked;
			}
			else
			{
				const float GravityZ = MovementComp->GetJumpPredictionGravity().Z;
				auto GetTrajectoryCenter = [&](float HorizontalDistance)
				{
					const float Time = HorizontalDistance / HorizontalSpeed;
					const float VerticalOffset =
						VerticalSpeed * Time + 0.5f * GravityZ * Time * Time;
					return Origin + Forward * HorizontalDistance
						+ FVector::UpVector * VerticalOffset;
				};

				JumpPassageStart = GetTrajectoryCenter(PassageStartDistance);
				JumpPassageEnd = GetTrajectoryCenter(PassageEndDistance);
				bJumpPassageTested = true;

				const FCollisionShape JumpBodyShape = FCollisionShape::MakeBox(JumpPassageExtent);
				if (Physics->Sweep(JumpPassageStart, JumpPassageEnd, FQuat::Identity,
					JumpBodyShape, JumpPathHit, ECollisionChannel::WorldStatic, Owner))
				{
					JumpProbeResult = EJumpProbeResult::PathBlocked;
				}
			}
		}
	}

	LastJumpProbeResult = JumpProbeResult;
	const bool bJumpable = JumpProbeResult == EJumpProbeResult::Jumpable;
	BlackboardComp->GetBlackboard().SetFloat(HorseBBKeys::ObsFwdDist, LowClear);
	BlackboardComp->GetBlackboard().SetBool(HorseBBKeys::ObsJumpable, bJumpable);

	if (!bDrawJumpDebug)
	{
		return;
	}

	// low ray: 초록=clear, 빨강=실제 장애물 후보, 파랑=지면/경사 전환으로 기각.
	const FVector LowEnd = LowHit.bHit ? LowHit.WorldHitLocation : LowOrigin + Forward * JumpProbeRange;
	const FColor LowColor =
		JumpProbeResult == EJumpProbeResult::NoObstacle ? FColor::Green() :
		JumpProbeResult == EJumpProbeResult::SurfaceNotFacing
			|| JumpProbeResult == EJumpProbeResult::GroundTransition
			? FColor::Blue() : FColor::Red();
	DrawDebugLine(World, LowOrigin, LowEnd, LowColor,
		LowHit.bHit ? JumpDebugHitDuration : 0.0f);
	if (LowHit.bHit)
	{
		DrawDebugPoint(World, LowHit.WorldHitLocation, 0.12f, LowColor, JumpDebugHitDuration);
		if (!LowNormal.IsNearlyZero())
		{
			DrawDebugLine(World, LowHit.WorldHitLocation,
				LowHit.WorldHitLocation + LowNormal * 0.6f,
				LowColor, JumpDebugHitDuration);
		}
	}

	if (bWallLikeHit)
	{
		const FVector BeforeStart =
			LowHit.WorldHitLocation - Forward * GroundTransitionProbeOffset
			+ FVector::UpVector * GroundTransitionProbeUp;
		const FVector AfterStart =
			LowHit.WorldHitLocation + Forward * GroundTransitionProbeOffset
			+ FVector::UpVector * GroundTransitionProbeUp;
		const FVector BeforeEnd = GroundBeforeHit.bHit
			? GroundBeforeHit.WorldHitLocation
			: BeforeStart + FVector::DownVector * (GroundTransitionProbeUp + GroundTransitionProbeDown);
		const FVector AfterEnd = GroundAfterHit.bHit
			? GroundAfterHit.WorldHitLocation
			: AfterStart + FVector::DownVector * (GroundTransitionProbeUp + GroundTransitionProbeDown);
		DrawDebugLine(World, BeforeStart, BeforeEnd,
			bGroundTransition ? FColor::Blue() : FColor::Gray(), JumpDebugHitDuration);
		DrawDebugLine(World, AfterStart, AfterEnd,
			bGroundTransition ? FColor::Blue() : FColor::Gray(), JumpDebugHitDuration);
	}

	if (bJumpPassageTested)
	{
		const FColor PassageColor = JumpPathHit.bHit ? FColor::Red() : FColor::Yellow();
		DrawDebugLine(World, JumpPassageStart, JumpPassageEnd, PassageColor);
		DrawDebugBox(World, JumpPassageStart, JumpPassageExtent, PassageColor);
		DrawDebugBox(World, JumpPassageEnd, JumpPassageExtent, PassageColor);
		if (JumpPathHit.bHit)
		{
			DrawDebugPoint(World, JumpPathHit.WorldHitLocation, 0.18f,
				FColor::Red(), JumpDebugHitDuration);
		}
	}

	const FVector HighEnd = HighHit.bHit
		? HighHit.WorldHitLocation
		: HighOrigin + Forward * JumpProbeRange;
	DrawDebugLine(World, HighOrigin, HighEnd, bJumpable ? FColor::Yellow() : FColor::Gray());
}

void UJumpObstacleSensorComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	FVector Forward = GetForwardVector();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	const FVector Origin = GetWorldLocation();
	const FVector LowStart = Origin - FVector(0.0f, 0.0f, JumpProbeDown);
	const FVector HighStart = Origin + FVector(0.0f, 0.0f, JumpProbeUp);
	Scene.AddDebugLine(LowStart, LowStart + Forward * JumpProbeRange, FColor::Green());
	Scene.AddDebugLine(HighStart, HighStart + Forward * JumpProbeRange, FColor::Yellow());
}
