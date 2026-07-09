#include "pch.h"
#include "ObstacleFanSensorComponent.h"

#include "AI/HorseBlackboardKeys.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Physics/IPhysicsScene.h"

#include <cmath>

namespace
{
	// V 를 world +Z 축 기준 Deg(도) 만큼 회전(수평 부채꼴용). Z 성분은 보존.
	FVector RotateAroundZ(const FVector& V, float Deg)
	{
		const float R = Deg * (3.14159265358979f / 180.0f);
		const float C = std::cos(R);
		const float S = std::sin(R);
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

	UWorld* W = World.Get();
	if (!W || !Owner)
	{
		return;
	}
	IPhysicsScene* Physics = W->GetPhysicsScene();
	UBlackboardComponent* BBComp = BlackboardComp.Get();
	if (!Physics)
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
	float CenterClear = ProbeRange;
	for (int i = 0; i < HorseBBKeys::ObsFanCount; ++i)
	{
		const FVector Dir = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);

		FHitResult Hit;
		Physics->Raycast(Origin, Dir, ProbeRange, Hit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외.
		const float Clear = Hit.bHit ? Hit.Distance : ProbeRange;

		if (BBComp)
		{
			BBComp->GetBlackboard().SetFloat(HorseBBKeys::ObsClear[i], Clear);
		}
		if (HorseBBKeys::ObsFanAngles[i] == 0.0f)
		{
			CenterClear = Clear;
		}

		if (bDrawDebug)
		{
			const FVector End = Hit.bHit ? Hit.WorldHitLocation : Origin + Dir * ProbeRange;
			DrawDebugLine(W, Origin, End, Hit.bHit ? FColor::Red() : FColor::Green());
			if (Hit.bHit)
			{
				DrawDebugSphere(W, End, 0.15f, 12, FColor::Red());
			}
		}
	}

	// ── 점프 가능 판정 ── center 를 JumpProbeUp 만큼 올려 쏴서, 낮은 장애물 지점 너머가 뚫려 있으면 넘을 수 있다.
	const FVector HighOrigin = Origin + FVector(0.0f, 0.0f, JumpProbeUp);
	FHitResult HighHit;
	Physics->Raycast(HighOrigin, Forward, ProbeRange, HighHit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외.
	const float HighClear = HighHit.bHit ? HighHit.Distance : ProbeRange;

	const bool bObstacleAhead = CenterClear < ProbeRange - 1.e-3f;
	const bool bJumpable      = bObstacleAhead && (HighClear > CenterClear + 0.3f);

	if (BBComp)
	{
		BBComp->GetBlackboard().SetFloat(HorseBBKeys::ObsFwdDist, CenterClear);
		BBComp->GetBlackboard().SetBool(HorseBBKeys::ObsJumpable, bJumpable);
	}

	if (bDrawDebug)
	{
		const FVector End = HighHit.bHit ? HighHit.WorldHitLocation : HighOrigin + Forward * ProbeRange;
		// 점프 가능하면 노란색(넘어라), 아니면 회색.
		DrawDebugLine(W, HighOrigin, End, bJumpable ? FColor::Yellow() : FColor::Gray());
	}
}
