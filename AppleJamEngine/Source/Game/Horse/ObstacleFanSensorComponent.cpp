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

	float NormalZFromSlopeDeg(float SlopeDeg)
	{
		return std::cos(std::clamp(SlopeDeg, 0.0f, 90.0f) * DEG_TO_RAD);
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

	// ── 부채꼴 clearance ── 
	const FVector BodyExtent(BodyHalfWidth, BodyHalfWidth, BodyHalfHeight);
	const FCollisionShape BodyShape = FCollisionShape::MakeBox(BodyExtent);
	for (int i = 0; i < HorseBBKeys::ObsSlotCount; ++i)
	{
		const FVector PlanarDir = RotateAroundZ(Forward, HorseBBKeys::ObsSlotAngles[i]);
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
		// 1차 검사: 뭐 걸리는 거 하나라도 있나 체크
		Physics->Sweep(Origin, End, BoxRotation, BodyShape, Hit, ECollisionChannel::WorldStatic, Owner);   // 자신의 충돌판정은 제외
		// 2차/3차 검사: 걸린 게 실제 장애물(벽 또는 지나갈 수 없는 경사의 지형)인지 체크
		const bool bTerrain = Hit.bHit && IsTraversableTerrain(Physics, Origin, PlanarDir, Hit);
		const float Clear = Hit.bHit && !bTerrain ? Hit.Distance : ProbeRange;
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
}

bool UObstacleFanSensorComponent::IsTraversableTerrain(IPhysicsScene* Physics, const FVector& Origin,
	const FVector& PlanarDir, const FHitResult& SweepHit) const
{
	FVector ContactNormal = SweepHit.ImpactNormal;
	if (ContactNormal.IsNearlyZero())
	{
		ContactNormal = SweepHit.WorldNormal;
	}

	const float WalkableNormalZ = NormalZFromSlopeDeg(WalkableTerrainDeg);
	const bool bGroundFacingContact = !ContactNormal.IsNearlyZero() &&
		ContactNormal.Normalized().Z >= WalkableNormalZ;
	const float ContactDistance = std::clamp(
		(SweepHit.WorldHitLocation - Origin).Dot(PlanarDir), 0.0f, ProbeRange);

	if (!bGroundFacingContact &&
		!IsClearTraversableStepAtContact(Physics, Origin, PlanarDir, ContactDistance, WalkableNormalZ))
	{
		return false;
	}

	return HasTraversableTerrainProfile(Physics, Origin, PlanarDir, WalkableNormalZ);
}

bool UObstacleFanSensorComponent::SampleWalkableGround(IPhysicsScene* Physics, const FVector& RayStart,
	float RayLength, float WalkableNormalZ, FHitResult& OutGroundHit) const
{
	Physics->Raycast(RayStart, FVector::DownVector, RayLength, OutGroundHit, ECollisionChannel::WorldStatic, Owner);

	FVector Normal = OutGroundHit.ImpactNormal;
	if (Normal.IsNearlyZero())
	{
		Normal = OutGroundHit.WorldNormal;
	}
	if (!OutGroundHit.bHit || Normal.IsNearlyZero() || Normal.Normalized().Z < WalkableNormalZ)
	{
		if (bDrawDebug)
		{
			DrawDebugLine(World, RayStart, RayStart + FVector::DownVector * RayLength, FColor::Red());
		}
		return false;
	}

	if (bDrawDebug)
	{
		DrawDebugLine(World, RayStart, OutGroundHit.WorldHitLocation, FColor(0, 200, 255));
	}
	return true;
}

bool UObstacleFanSensorComponent::IsClearTraversableStepAtContact(IPhysicsScene* Physics, const FVector& Origin,
	const FVector& PlanarDir, float ContactDistance, float WalkableNormalZ) const
{
	const float ContactHalfSpan = std::max(ContactSampleHalfSpan, 0.01f);
	const float BeforeDistance = ContactDistance - ContactHalfSpan;
	const float AfterDistance = ContactDistance + ContactHalfSpan;
	if (BeforeDistance < 0.0f || AfterDistance > ProbeRange)
	{
		return false;
	}

	const float RayStartZ = Origin.Z + BodyHalfHeight + TerrainProbeUp + MaxTerrainStep;
	const float RayLength = TerrainProbeUp + TerrainProbeDown + 2.0f * (BodyHalfHeight + MaxTerrainStep);
	auto SampleAtDistance = [&](float Distance, FHitResult& OutGroundHit)
	{
		const FVector ProbePoint = Origin + PlanarDir * Distance;
		const FVector RayStart(ProbePoint.X, ProbePoint.Y, RayStartZ);
		return SampleWalkableGround(Physics, RayStart, RayLength, WalkableNormalZ, OutGroundHit);
	};

	FHitResult BeforeGroundHit;
	FHitResult AfterGroundHit;
	if (!SampleAtDistance(BeforeDistance, BeforeGroundHit) ||
		!SampleAtDistance(AfterDistance, AfterGroundHit))
	{
		return false;
	}

	const float HeightDelta = std::abs(AfterGroundHit.WorldHitLocation.Z - BeforeGroundHit.WorldHitLocation.Z);
	if (HeightDelta <= 1.e-2f || HeightDelta > MaxTerrainStep)
	{
		return false;
	}

	const float HighGroundZ = std::max(BeforeGroundHit.WorldHitLocation.Z, AfterGroundHit.WorldHitLocation.Z);
	const float SphereRadius = std::max(TerrainClearanceSweepRadius, 0.01f);
	const float SweepCenterZ = HighGroundZ + SphereRadius + std::max(TerrainClearanceMargin, 0.0f);
	const FVector ClearanceStart(BeforeGroundHit.WorldHitLocation.X, BeforeGroundHit.WorldHitLocation.Y, SweepCenterZ);
	const FVector ClearanceEnd(AfterGroundHit.WorldHitLocation.X, AfterGroundHit.WorldHitLocation.Y, SweepCenterZ);

	FHitResult ClearanceHit;
	const bool bClearanceBlocked = Physics->Sweep(ClearanceStart, ClearanceEnd, FQuat::Identity,
		FCollisionShape::MakeSphere(SphereRadius), ClearanceHit, ECollisionChannel::WorldStatic, Owner);
	if (bDrawDebug)
	{
		const FColor Color = bClearanceBlocked ? FColor::Red() : FColor::Green();
		DrawDebugSphere(World, ClearanceStart, SphereRadius, 12, Color);
		DrawDebugSphere(World, ClearanceEnd, SphereRadius, 12, Color);
		DrawDebugLine(World, ClearanceStart, bClearanceBlocked ? ClearanceHit.WorldHitLocation : ClearanceEnd, Color);
	}
	return !bClearanceBlocked;
}

bool UObstacleFanSensorComponent::HasTraversableTerrainProfile(IPhysicsScene* Physics, const FVector& Origin,
	const FVector& PlanarDir, float WalkableNormalZ) const
{
	const float Spacing = std::max(TerrainSampleSpacing, 0.1f);
	const int SampleCount = std::max(1, static_cast<int>(std::ceil(ProbeRange / Spacing)));

	bool bHasPrevious = false;
	float PreviousHeight = 0.0f;
	for (int SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
	{
		const float Distance = ProbeRange * static_cast<float>(SampleIndex) / static_cast<float>(SampleCount);
		const FVector ProbePoint = Origin + PlanarDir * Distance;
		const float RayStartZ = bHasPrevious ? PreviousHeight + TerrainProbeUp : Origin.Z + TerrainProbeUp;
		const float RayLength = bHasPrevious
			? TerrainProbeUp + TerrainProbeDown
			: TerrainProbeUp + TerrainProbeDown + BodyHalfHeight;
		const FVector RayStart(ProbePoint.X, ProbePoint.Y, RayStartZ);

		FHitResult GroundHit;
		if (!SampleWalkableGround(Physics, RayStart, RayLength, WalkableNormalZ, GroundHit))
		{
			return false;
		}

		if (bHasPrevious && std::abs(GroundHit.WorldHitLocation.Z - PreviousHeight) > MaxTerrainStep)
		{
			if (bDrawDebug)
			{
				DrawDebugLine(World, RayStart, GroundHit.WorldHitLocation, FColor::Red());
			}
			return false;
		}

		PreviousHeight = GroundHit.WorldHitLocation.Z;
		bHasPrevious = true;
	}
	return true;
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
	for (int i = 0; i < HorseBBKeys::ObsSlotCount; i++)
	{
		const FVector PlanarDir = RotateAroundZ(Forward, HorseBBKeys::ObsSlotAngles[i]);
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

		const FVector X = Dir   * BodyHalfWidth;
		const FVector Y = Right * BodyHalfWidth;
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
}
