#include "pch.h"
#include "ObstacleFanSensorComponent.h"

#include "Game/Horse/HorseConstants.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "HorseMovementComponent.h"
#include "Physics/IPhysicsScene.h"
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
	const FVector BodyExtent(BodyRadius, BodyRadius, BodyHalfHeight);
	const FCollisionShape BodyShape = FCollisionShape::MakeBox(BodyExtent);
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
		const float Clear = Hit.bHit ? Hit.Distance : ProbeRange;

		BlackboardComp->GetBlackboard().SetFloat(HorseBBKeys::ObsClear[i], Clear);

		if (bDrawDebug)
		{
			const FVector StopCenter = Origin + SweepDir * Clear;   // sweep 이 멈춘 box 중심.
			DrawDebugLine(World, Origin, StopCenter, Hit.bHit ? FColor::Red() : FColor::Green());
			DrawOrientedBox(World, StopCenter, BodyExtent, Dir, Right, Up,
				Hit.bHit ? FColor::Red() : FColor::Green());
		}
	}

	// ── 점프 가능 판정 ── 
	const FVector LowOrigin = Origin - FVector(0.0f, 0.0f, JumpProbeDown);
	FHitResult LowHit;
	Physics->Raycast(LowOrigin, Forward, ProbeRange, LowHit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외.
	const float LowClear = LowHit.bHit ? LowHit.Distance : ProbeRange;

	const FVector HighOrigin = Origin + FVector(0.0f, 0.0f, JumpProbeUp);
	FHitResult HighHit;
	Physics->Raycast(HighOrigin, Forward, ProbeRange, HighHit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외.
	const float HighClear = HighHit.bHit ? HighHit.Distance : ProbeRange;

	const bool bObstacleAhead = LowClear < ProbeRange - 1.e-3f;
	const float MinJumpUpSpace = 0.3f;	// NOTE: 적당히 고른 임시값. 튜닝 필요
	const bool bJumpable      = bObstacleAhead && (HighClear > LowClear + MinJumpUpSpace);

	BlackboardComp->GetBlackboard().SetFloat(HorseBBKeys::ObsFwdDist, LowClear);
	BlackboardComp->GetBlackboard().SetBool(HorseBBKeys::ObsJumpable, bJumpable);

	if (bDrawDebug)
	{
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
