#include "pch.h"
#include "CliffFanSensorComponent.h"

#include "AI/HorseBlackboardKeys.h"
#include "Component/Movement/HorseLocomotionComponent.h"
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

void UCliffFanSensorComponent::BeginPlay()
{
	Super::BeginPlay();
	World = GetWorld();
	BlackboardComp = Owner->GetComponentByClass<UBlackboardComponent>();
	LocomotionComp = Owner->GetComponentByClass<UHorseLocomotionComponent>();
}

float UCliffFanSensorComponent::GetProbeDistance() const
{
	if (!LocomotionComp.IsValid())
	{
		return WalkProbeDistance;
	}
	switch (LocomotionComp->GetGait())
	{
	case EHorseGait::Gallop:		return GallopProbeDistance;
	case EHorseGait::Canter:		return CanterProbeDistance;
	case EHorseGait::Trot:			return TrotProbeDistance;
	default: /* Stop or Walk */		return WalkProbeDistance;
	}
}

void UCliffFanSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
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

	// 각도는 Owner Actor forward (수평) 기준.
	FVector Forward = Owner->GetActorForward();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	const FVector Origin      = GetWorldLocation();
	const float   ProbeDist   = GetProbeDistance();
	const FVector Down        = FVector::DownVector;	// 컴포넌트의 회전과 상관없이 World down 방향 사용
	const float   RayLen      = ProbeUpDist + ProbeDownDist;

	// slot 별 지면 유무 판정
	for (int i = 0; i < HorseBBKeys::ObsFanCount; ++i)
	{
		const FVector Dir        = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);
		const FVector ProbePoint = Origin + Dir * ProbeDist;
		const FVector RayStart   = ProbePoint + FVector(0.0f, 0.0f, ProbeUpDist);

		FHitResult Hit;
		Physics->Raycast(RayStart, Down, RayLen, Hit, ECollisionChannel::WorldStatic, Owner);   // 자기 몸통 box 제외
		const bool bGround = Hit.bHit;

		BlackboardComp->GetBlackboard().SetBool(HorseBBKeys::ObsGround[i], bGround);

		if (bDrawDebug)
		{
			// 초록=밟을 지면 있음, 빨강=낭떠러지(허공).
			const FVector RayEnd = bGround ? Hit.WorldHitLocation : RayStart + Down * RayLen;
			DrawDebugLine(World, RayStart, RayEnd, bGround ? FColor::Green() : FColor::Red());
			if (!bGround)
			{
				DrawDebugSphere(World, RayStart + Down * RayLen, 0.15f, 8, FColor::Red());
			}
		}
	}
}

// 에디터 타임 중 센서 범위 프리뷰 (표시는 Walk 기준)
void UCliffFanSensorComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	FVector Forward = Owner->GetActorForward();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	const FVector Origin = GetWorldLocation();
	const FVector Down   = FVector(0.0f, 0.0f, -1.0f);
	const float   RayLen = ProbeUpDist + ProbeDownDist;
	for (int i = 0; i < HorseBBKeys::ObsFanCount; ++i)
	{
		const FVector Dir      = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);
		const FVector RayStart = Origin + Dir * WalkProbeDistance + FVector(0.0f, 0.0f, ProbeUpDist);
		Scene.AddDebugLine(RayStart, RayStart + Down * RayLen, FColor(0, 200, 255));
	}
}
