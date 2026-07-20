#include "pch.h"
#include "ObstacleFanSensorComponent.h"

#include "AI/HorseBlackboardKeys.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Physics/IPhysicsScene.h"
#include "Math/MathUtils.h"

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
}

void UObstacleFanSensorComponent::BeginPlay()
{
	Super::BeginPlay();
	World          = GetWorld();
	BlackboardComp = Owner->GetComponentByClass<UBlackboardComponent>();
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

	// 부채꼴 기준: 소유 액터 forward(수평). arbiter 가 각도를 해석하는 축과 동일해야 한다.
	FVector Forward = Owner->GetActorForward();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	const FVector Origin = GetWorldLocation();

	// ── 부채꼴 clearance ── 
	// NOTE: sphere sweep 거리는 벽 면보다 radius만큼 앞에서 멈춤에 유의
	const FCollisionShape BodyShape = FCollisionShape::MakeSphere(BodyRadius);
	for (int i = 0; i < HorseBBKeys::ObsFanCount; ++i)
	{
		const FVector Dir = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);
		const FVector End = Origin + Dir * ProbeRange;

		FHitResult Hit;
		Physics->Sweep(Origin, End, FQuat::Identity, BodyShape, Hit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외.
		const float Clear = Hit.bHit ? Hit.Distance : ProbeRange;

		BlackboardComp->GetBlackboard().SetFloat(HorseBBKeys::ObsClear[i], Clear);

		if (bDrawDebug)
		{
			const FVector StopCenter = Origin + Dir * Clear;   // sweep 이 멈춘 sphere 중심.
			DrawDebugLine(World, Origin, StopCenter, Hit.bHit ? FColor::Red() : FColor::Green());
			DrawDebugSphere(World, StopCenter, BodyRadius, 12, Hit.bHit ? FColor::Red() : FColor::Green());
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
	FVector RayOrigin = GetWorldLocation();
	FVector Forward = Owner->GetActorForward();
	// ── 스티어링 판단용 부채꼴 sphere sweep 센서 ── 끝점에 BodyRadius sphere 로 폭을 표시.
	for (int i = 0; i < HorseBBKeys::ObsFanCount; i++)
	{
		FVector RayDir = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);
		const FVector RayStart = GetWorldLocation();
		const FVector RayEnd = RayStart + RayDir * ProbeRange;
		Scene.AddDebugLine(RayStart, RayEnd, FColor::Green());
		Scene.AddDebugSphere(RayEnd, BodyRadius, 12, FColor::Green());
	}
	// ── 점프 가능 판정 센서 ──
	FVector RayStart = RayOrigin + FVector(0.0f, 0.0f, JumpProbeUp);
	FVector RayEnd = RayStart + Owner->GetActorForward() * ProbeRange;
	Scene.AddDebugLine(RayStart, RayEnd, FColor::Yellow());
}