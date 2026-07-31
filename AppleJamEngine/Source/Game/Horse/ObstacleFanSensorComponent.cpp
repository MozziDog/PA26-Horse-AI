#include "pch.h"
#include "ObstacleFanSensorComponent.h"

#include "Game/Horse/HorseConstants.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "HorseMovementComponent.h"
#include "Physics/IPhysicsScene.h"
#include "Core/TickFunction.h"
#include "Math/Matrix.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cmath>

namespace
{
	// V 를 world +Z 축 기준 Deg(도) 만큼 회전
	FVector RotateAroundZ(const FVector& V, float Deg)
	{
		const float Rad = Deg * DEG_TO_RAD;
		const float C = std::cos(Rad);
		const float S = std::sin(Rad);
		return FVector(V.X * C - V.Y * S, V.X * S + V.Y * C, V.Z);
	}

	bool BuildGroundAlignedFrame(const FVector& PlanarDir, const FVector& GroundUp,
		FVector& OutForward, FVector& OutRight, FVector& OutUp, FQuat& OutRotation)
	{
		OutUp = GroundUp.IsNearlyZero() ? FVector::UpVector : GroundUp.Normalized();
		OutForward = PlanarDir - OutUp * PlanarDir.Dot(OutUp);
		if (OutForward.IsNearlyZero())
		{
			return false;
		}
		OutForward = OutForward.Normalized();
		OutRight = OutUp.Cross(OutForward);
		if (OutRight.IsNearlyZero())
		{
			return false;
		}
		OutRight = OutRight.Normalized();
		OutUp = OutForward.Cross(OutRight).Normalized();

		FMatrix Basis = FMatrix::Identity;
		Basis.SetAxes(OutForward, OutRight, OutUp);
		OutRotation = Basis.ToQuat();
		return true;
	}

	void DrawOrientedBox(UWorld* World, const FVector& Center, const FVector& Extent,
		const FVector& Forward, const FVector& Right, const FVector& Up, const FColor& Color)
	{
		const FVector X = Forward * Extent.X;
		const FVector Y = Right   * Extent.Y;
		const FVector Z = Up      * Extent.Z;
		DrawDebugBox(World,
			Center - X - Y - Z, Center + X - Y - Z, Center + X + Y - Z, Center - X + Y - Z,
			Center - X - Y + Z, Center + X - Y + Z, Center + X + Y + Z, Center - X + Y + Z,
			Color);
	}

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

	bool IsWalkableGroundHit(const FHitResult& Hit, float MinNormalZ)
	{
		const FVector Normal = GetQueryNormal(Hit);
		return Hit.bHit && !Normal.IsNearlyZero() && Normal.Z >= MinNormalZ;
	}
}

UObstacleFanSensorComponent::UObstacleFanSensorComponent()
{
	// Blackboard를 소비하는 Locomotion(TG_DuringPhysics)보다 먼저 최신 센서 값을 기록한다.
	PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PrePhysics);
}

void UObstacleFanSensorComponent::BeginPlay()
{
	Super::BeginPlay();
	World          = GetWorld();
	BlackboardComp = Owner->GetComponentByClass<UBlackboardComponent>();
	MovementComp   = Owner->GetComponentByClass<UHorseMovementComponent>();
}

void UObstacleFanSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	(void)DeltaTime; (void)TickType; (void)ThisTickFunction;

	if (!World.IsValid() || !Owner)
	{
		return;
	}
	IPhysicsScene* Physics = World->GetPhysicsScene();
	if (!Physics || !BlackboardComp.IsValid())
	{
		return;
	}

	// 부채꼴 slot은 world XY yaw 기준. 실제 sweep 방향과 box는 지면 접평면에 정렬한다.
	FVector Forward = GetForwardVector();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	const FVector Origin = GetWorldLocation();
	FVector GroundUp = FVector::UpVector;
	if (MovementComp.IsValid())
	{
		GroundUp = MovementComp->GetCurrentGroundNormal();
	}

	// sweep이 지면을 장애물로 맞힌 경우에만 경로의 지면을 연속 표본화한다.
	// 모든 표본이 보행 가능한 경사이고 인접 높이 차가 허용 단차 이내면 지형으로 본다.
	const auto IsTraversableTerrain = [&](const FVector& PlanarDir, const FHitResult& SweepHit)
	{
		const float Spacing = std::max(TerrainSampleSpacing, 0.1f);
		const int SampleCount = std::max(1, static_cast<int>(std::ceil(ProbeRange / Spacing)));
		const float ContactDistance = std::clamp(
			(SweepHit.WorldHitLocation - Origin).Dot(PlanarDir), 0.0f, ProbeRange);

		bool bHasPrevious = false;
		bool bGroundChangesAtContact = false;
		float PreviousHeight = 0.0f;
		float PreviousDistance = 0.0f;

		for (int SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
		{
			const float Distance = ProbeRange * static_cast<float>(SampleIndex) / static_cast<float>(SampleCount);
			const FVector ProbePoint = Origin + PlanarDir * Distance;
			const float RayStartZ = bHasPrevious
				? PreviousHeight + TerrainProbeUp
				: Origin.Z + TerrainProbeUp;
			const FVector RayStart(ProbePoint.X, ProbePoint.Y, RayStartZ);
			const float RayLength = bHasPrevious
				? TerrainProbeUp + TerrainProbeDown
				: TerrainProbeUp + TerrainProbeDown + BodyHalfHeight;

			FHitResult GroundHit;
			Physics->Raycast(RayStart, FVector::DownVector, RayLength, GroundHit,
				ECollisionChannel::WorldStatic, Owner);

			FVector Normal = GroundHit.ImpactNormal;
			if (Normal.IsNearlyZero())
			{
				Normal = GroundHit.WorldNormal;
			}
			if (!GroundHit.bHit || Normal.IsNearlyZero() ||
				Normal.Normalized().Z < WalkableTerrainNormalZ)
			{
				if (bDrawDebug)
				{
					DrawDebugLine(World, RayStart, RayStart + FVector::DownVector * RayLength, FColor::Red());
				}
				return false;
			}

			if (bHasPrevious)
			{
				const float HeightDelta = std::abs(GroundHit.WorldHitLocation.Z - PreviousHeight);
				if (HeightDelta > MaxTerrainStep)
				{
					if (bDrawDebug)
					{
						DrawDebugLine(World, RayStart, GroundHit.WorldHitLocation, FColor::Red());
					}
					return false;
				}

				if (PreviousDistance <= ContactDistance + Spacing &&
					Distance >= ContactDistance - Spacing &&
					HeightDelta > 1.e-2f)
				{
					bGroundChangesAtContact = true;
				}
			}

			if (bDrawDebug)
			{
				DrawDebugLine(World, RayStart, GroundHit.WorldHitLocation, FColor(0, 200, 255));
			}
			PreviousHeight = GroundHit.WorldHitLocation.Z;
			PreviousDistance = Distance;
			bHasPrevious = true;
		}

		FVector ContactNormal = SweepHit.ImpactNormal;
		if (ContactNormal.IsNearlyZero())
		{
			ContactNormal = SweepHit.WorldNormal;
		}
		const bool bGroundFacingContact = !ContactNormal.IsNearlyZero() &&
			ContactNormal.Normalized().Z >= WalkableTerrainNormalZ;

		// 평평한 지면 접촉이거나, steep edge가 실제 허용 단차와 일치할 때만 지형으로 무시한다.
		return bGroundFacingContact || bGroundChangesAtContact;
	};

	// ── 부채꼴 clearance ── 
	const FVector BodyExtent(BodyRadius, BodyRadius, BodyHalfHeight);
	const FCollisionShape BodyShape = FCollisionShape::MakeBox(BodyExtent);
	bool bForwardHitIsTerrain = false;
	for (int i = 0; i < HorseBBKeys::ObsFanCount; ++i)
	{
		const FVector PlanarDir = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);
		FVector Dir, Right, Up;
		FQuat BoxRotation;
		if (!BuildGroundAlignedFrame(PlanarDir, GroundUp, Dir, Right, Up, BoxRotation))
		{
			continue;
		}
		const float BiasSlope = std::tan(std::clamp(GroundBiasAngle, 0.0f, 89.0f) * DEG_TO_RAD);
		const FVector SweepDir = (Dir + Up * BiasSlope).Normalized();
		const FVector End = Origin + SweepDir * ProbeRange;

		FHitResult Hit;
		Physics->Sweep(Origin, End, BoxRotation, BodyShape, Hit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외.
		const bool bTerrain = Hit.bHit && IsTraversableTerrain(PlanarDir, Hit);
		const float Clear = Hit.bHit && !bTerrain ? Hit.Distance : ProbeRange;
		if (std::abs(HorseBBKeys::ObsFanAngles[i]) < 1.e-3f)
		{
			bForwardHitIsTerrain = bTerrain;
		}

		BlackboardComp->GetBlackboard().SetFloat(HorseBBKeys::ObsClear[i], Clear);

		if (bDrawDebug)
		{
			const FVector StopCenter = Origin + SweepDir * Clear;   // sweep 이 멈춘 box 중심.
			const bool bObstacle = Hit.bHit && !bTerrain;
			DrawDebugLine(World, Origin, StopCenter, bObstacle ? FColor::Red() : FColor::Green());
			DrawOrientedBox(World, StopCenter, BodyExtent, Dir, Right, Up,
				bObstacle ? FColor::Red() : FColor::Green());
		}
	}

	// ── 점프 가능 판정 ── 
	const FVector LowOrigin = Origin - FVector(0.0f, 0.0f, JumpProbeDown);
	FHitResult LowHit;
	Physics->Raycast(LowOrigin, Forward, ProbeRange, LowHit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외.
	const float LowClear = LowHit.bHit && !bForwardHitIsTerrain ? LowHit.Distance : ProbeRange;

	const FVector HighOrigin = Origin + FVector(0.0f, 0.0f, JumpProbeUp);
	FHitResult HighHit;
	Physics->Raycast(HighOrigin, Forward, ProbeRange, HighHit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외.
	const float HighClear = HighHit.bHit ? HighHit.Distance : ProbeRange;

	// 단순 low hit는 오르막/도로 이음매도 장애물로 오인한다. 먼저 진행 방향을 실제로 막는
	// face인지 검사하고, hit 전후로 동일한 walkable ground가 이어지면 지면 전환으로 제외한다.
	const FVector LowNormal = GetQueryNormal(LowHit);
	const float ObstacleFacing = LowNormal.IsNearlyZero() ? 1.0f : -LowNormal.Dot(Forward);
	const bool bWallLikeHit = LowHit.bHit && ObstacleFacing >= MinObstacleFacing;

	FHitResult GroundBeforeHit;
	FHitResult GroundAfterHit;
	bool bGroundTransition = false;
	if (bWallLikeHit)
	{
		const FVector Down = FVector::DownVector;
		const float GroundProbeLength = GroundTransitionProbeUp + GroundTransitionProbeDown;
		const FVector BeforePoint = LowHit.WorldHitLocation - Forward * GroundTransitionProbeOffset;
		const FVector AfterPoint = LowHit.WorldHitLocation + Forward * GroundTransitionProbeOffset;
		const FVector BeforeStart = BeforePoint + FVector::UpVector * GroundTransitionProbeUp;
		const FVector AfterStart = AfterPoint + FVector::UpVector * GroundTransitionProbeUp;

		Physics->Raycast(BeforeStart, Down, GroundProbeLength, GroundBeforeHit,
			ECollisionChannel::WorldStatic, Owner);
		Physics->Raycast(AfterStart, Down, GroundProbeLength, GroundAfterHit,
			ECollisionChannel::WorldStatic, Owner);

		const bool bWalkableOnBothSides =
			IsWalkableGroundHit(GroundBeforeHit, MinWalkableGroundNormalZ)
			&& IsWalkableGroundHit(GroundAfterHit, MinWalkableGroundNormalZ);
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

	const float MinJumpUpSpace = 0.3f;	// NOTE: 적당히 고른 임시값. 튜닝 필요
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
	FVector JumpPassageExtent(
		BodyRadius + JumpPassagePadding,
		BodyRadius + JumpPassagePadding,
		BodyHalfHeight + JumpPassagePadding);
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
			const float AngleRad =
				std::clamp(JumpTrajectoryAngle, 5.0f, 85.0f) * DEG_TO_RAD;
			const float TanAngle = std::tan(AngleRad);
			const float VerticalSpeed = MovementComp->GetJumpSpeed();
			const float HorizontalSpeed =
				TanAngle > 1.e-3f ? VerticalSpeed / TanAngle : 0.0f;
			const float PassageStartDistance =
				std::max(0.0f, LowClear - std::max(0.0f, JumpPathBeforeObstacle));
			const float PassageEndDistance =
				std::min(ProbeRange, LowClear + std::max(0.0f, JumpPathBeyondObstacle));

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

				const FCollisionShape JumpBodyShape =
					FCollisionShape::MakeBox(JumpPassageExtent);
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

	if (bDrawJumpDebug)
	{
		// low ray: 초록=clear, 빨강=실제 장애물 후보, 파랑=지면/경사 전환으로 기각.
		const FVector LowEnd = LowHit.bHit ? LowHit.WorldHitLocation : LowOrigin + Forward * ProbeRange;
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

		const FVector End = HighHit.bHit ? HighHit.WorldHitLocation : HighOrigin + Forward * ProbeRange;
		// 점프 가능하면 노란색(넘어라), 아니면 회색.
		DrawDebugLine(World, HighOrigin, End, bJumpable ? FColor::Yellow() : FColor::Gray());
	}
}

// 에디터 타임 중 센서 범위 프리뷰
void UObstacleFanSensorComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	const FVector RayOrigin = GetWorldLocation();
	FVector Forward = GetForwardVector();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	FVector GroundUp = FVector::UpVector;
	if (MovementComp.IsValid() && Owner)
	{
		GroundUp = MovementComp->GetCurrentGroundNormal();
	}

	// ── 스티어링 판단용 부채꼴 box sweep 센서 ──
	for (int i = 0; i < HorseBBKeys::ObsFanCount; i++)
	{
		const FVector PlanarDir = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);
		FVector Dir, Right, Up;
		FQuat BoxRotation;
		if (!BuildGroundAlignedFrame(PlanarDir, GroundUp, Dir, Right, Up, BoxRotation))
		{
			continue;
		}

		const float BiasSlope = std::tan(std::clamp(GroundBiasAngle, 0.0f, 89.0f) * DEG_TO_RAD);
		const FVector SweepDir = (Dir + Up * BiasSlope).Normalized();
		const FVector RayEnd = RayOrigin + SweepDir * ProbeRange;
		Scene.AddDebugLine(RayOrigin, RayEnd, FColor::Green());

		const FVector X = Dir   * BodyRadius;
		const FVector Y = Right * BodyRadius;
		const FVector Z = Up    * BodyHalfHeight;
		const FVector P[8] =
		{
			RayEnd - X - Y - Z, RayEnd + X - Y - Z, RayEnd + X + Y - Z, RayEnd - X + Y - Z,
			RayEnd - X - Y + Z, RayEnd + X - Y + Z, RayEnd + X + Y + Z, RayEnd - X + Y + Z
		};
		Scene.AddDebugLine(P[0], P[1], FColor::Green());
		Scene.AddDebugLine(P[1], P[2], FColor::Green());
		Scene.AddDebugLine(P[2], P[3], FColor::Green());
		Scene.AddDebugLine(P[3], P[0], FColor::Green());
		Scene.AddDebugLine(P[4], P[5], FColor::Green());
		Scene.AddDebugLine(P[5], P[6], FColor::Green());
		Scene.AddDebugLine(P[6], P[7], FColor::Green());
		Scene.AddDebugLine(P[7], P[4], FColor::Green());
		Scene.AddDebugLine(P[0], P[4], FColor::Green());
		Scene.AddDebugLine(P[1], P[5], FColor::Green());
		Scene.AddDebugLine(P[2], P[6], FColor::Green());
		Scene.AddDebugLine(P[3], P[7], FColor::Green());
	}
	// ── 점프 가능 판정 센서 ──
	FVector RayStart = RayOrigin + FVector(0.0f, 0.0f, JumpProbeUp);
	FVector RayEnd = RayStart + Owner->GetActorForward() * ProbeRange;
	Scene.AddDebugLine(RayStart, RayEnd, FColor::Yellow());
}
