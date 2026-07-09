#include "HorseLocomotionComponent.h"

#include "HorseMovementComponent.h"
#include "AI/HorseBlackboardKeys.h"
#include "Component/AI/BlackboardComponent.h"
#include "Core/TickFunction.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
	EHorseGait GaitStep(EHorseGait Gait, int Delta)
	{
		return static_cast<EHorseGait>(static_cast<uint8>(Gait) + Delta);
	}

	// V 를 world +Z 축 기준 Deg(도) 만큼 회전(수평 부채꼴 slot 생성용). Z 성분 보존.
	FVector RotateAroundZ(const FVector& V, float Deg)
	{
		const float R = Deg * (3.14159265358979f / 180.0f);
		const float C = std::cos(R);
		const float S = std::sin(R);
		return FVector(V.X * C - V.Y * S, V.X * S + V.Y * C, V.Z);
	}
}

UHorseLocomotionComponent::UHorseLocomotionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Movement(TG_PostPhysics)가 consume 하기 전에 이번 frame 입력을 실어주도록 앞 그룹에서 tick.
	// 순서 어긋나면 최대 1프레임 지연 발생할 수 있음. (critical한 요소는 아님)
	PrimaryComponentTick.SetTickGroup(TG_PrePhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PrePhysics);
}

void UHorseLocomotionComponent::BeginPlay()
{
	UActorComponent::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		Movement       = Owner->GetComponentByClass<UHorseMovementComponent>();
		BlackboardComp = Owner->GetComponentByClass<UBlackboardComponent>();
	}
	Gait        = EHorseGait::Stop;
	GaitUpTimer = 0.0f;
}

void UHorseLocomotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (GaitUpTimer > 0.0f)
	{
		GaitUpTimer = std::max(0.0f, GaitUpTimer - DeltaTime);
	}

	// BT에서 요청한 DesiredGait를 쿨타임 등 고려 후 실제 Gait에 반영
	if (BlackboardComp)
	{
		int Desired = 0;
		if (BlackboardComp->GetBlackboard().TryGetInt(HorseBBKeys::DesiredGait, Desired))
		{
			const int Cur  = static_cast<int>(Gait);
			const int Want = std::clamp(Desired, 0, static_cast<int>(EHorseGait::Gallop));
			if (Want > Cur)      RequestGiddyup();
			else if (Want < Cur) RequestSlowDown();
		}
	}

	// BT등에서 결정한 범위로 현재의 gait 클램핑
	ClampGaitToEnvelope();   

	AActor* Owner = GetOwner();
	if (!Movement || !Owner || Gait == EHorseGait::Stop)
	{
		return;   // 정지 상태면 무입력 → Movement 가 braking 감속.
	}

	FVector Forward = Owner->GetActorForward();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	const FVector Loc = Owner->GetActorLocation();
	UWorld*       W   = Owner->GetWorld();
	FBlackboard*  BB  = BlackboardComp ? &BlackboardComp->GetBlackboard() : nullptr;

	// ── influence 소스 수집 ── 없으면 무영향(기본 = 직진). 우선순위는 가중치로 표현:
	//    User(최상) > Road > Inertia. 장애물은 아래 slot veto 로 이들 모두를 hard 하게 이긴다.
	FVector UserDir(0.0f, 0.0f, 0.0f);
	float   UserMag = 0.0f;
	FVector RoadDir(0.0f, 0.0f, 0.0f);
	bool    bRoad   = false;
	if (BB)
	{
		FVector V;
		if (BB->TryGetVector(HorseBBKeys::UserMoveDir, V) && !V.IsNearlyZero())
		{
			UserMag = std::clamp(V.Length(), 0.0f, 1.0f);
			V.Z = 0.0f;
			if (!V.IsNearlyZero()) { UserDir = V.Normalized(); }
			else                   { UserMag = 0.0f; }
		}
		if (BB->TryGetVector(HorseBBKeys::RoadDir, V) && !V.IsNearlyZero())
		{
			V.Z = 0.0f;
			if (!V.IsNearlyZero()) { RoadDir = V.Normalized(); bRoad = true; }
		}
	}

	// ── 점프 게이트 ── 정면 장애물이 점프 가능(ObsJumpable)하고 트리거 거리 안이면 도약(heading 유지).
	if (BB)
	{
		bool  bJumpable = false;
		float FwdDist   = 0.0f;
		if (BB->TryGetBool(HorseBBKeys::ObsJumpable, bJumpable) && bJumpable
			&& BB->TryGetFloat(HorseBBKeys::ObsFwdDist, FwdDist) && FwdDist < JumpTriggerDist)
		{
			Movement->Jump();
		}
	}

	// ── context steering ── 부채꼴 slot 마다 danger veto 후 interest 최고 방향 선택.
	//    두 우회 방향이 모두 열려 있으면 interest(User>Road>Inertia)가 tie 를 깨 좌/우가 자연히 결정된다.
	const FVector DebugBase = Loc + FVector(0.0f, 0.0f, 0.3f);
	float   BestScore = -1.0f;
	FVector BestDir   = Forward;
	bool    bAnyOpen  = false;
	for (int i = 0; i < HorseBBKeys::ObsFanCount; ++i)
	{
		const FVector SlotDir = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);

		float      Clear    = 0.0f;
		const bool bBlocked = BB && BB->TryGetFloat(HorseBBKeys::ObsClear[i], Clear) && Clear < SafeDistance;

		float Interest = InertiaWeight * std::max(0.0f, SlotDir.Dot(Forward));
		if (UserMag > 0.0f) { Interest += UserWeight * UserMag * std::max(0.0f, SlotDir.Dot(UserDir)); }
		if (bRoad)          { Interest += RoadWeight * std::max(0.0f, SlotDir.Dot(RoadDir)); }

		if (bDrawSteeringDebug && W)
		{
			const FColor  Col = bBlocked ? FColor::Red() : FColor::Green();
			DrawDebugLine(W, DebugBase, DebugBase + SlotDir * (1.0f + Interest), Col);
		}

		if (bBlocked)
		{
			continue;   // 막힌 방향 veto.
		}
		bAnyOpen = true;
		if (Interest > BestScore)
		{
			BestScore = Interest;
			BestDir   = SlotDir;
		}
	}

	if (!bAnyOpen)
	{
		// 모든 방향 막힘(점프도 불가) → 급브레이크: 입력 미부여 시 Movement 가 감속.
		if (bDrawSteeringDebug && W)
		{
			DrawDebugSphere(W, DebugBase, 0.4f, 12, FColor::Red());
		}
		return;
	}

	if (bDrawSteeringDebug && W)
	{
		DrawDebugLine(W, DebugBase, DebugBase + BestDir * 3.0f, FColor::Blue());   // 선택된 heading.
	}

	// gait → scale([0,1]). Movement 는 MaxSpeed*scale 을 목표속도로 삼는다.
	Movement->AddInputVector(BestDir.Normalized(), GetGaitScaledSpeed());
}

void UHorseLocomotionComponent::RequestGiddyup()
{
	if (GaitUpTimer > 0.0f || Gait >= MaxGait)
	{
		return;
	}
	if (Movement && !Movement->CanAccelerate())
	{
		return;   // 낙하 중 등 — 가속 불가 (gait를 낮추는 건 가능)
	}
	Gait        = GaitStep(Gait, +1);
	GaitUpTimer = GaitUpCooldown;
}

void UHorseLocomotionComponent::RequestSlowDown()
{
	if (Gait <= MinGait)
	{
		return;
	}
	Gait = GaitStep(Gait, -1);
}

void UHorseLocomotionComponent::RequestStop()
{
	Gait = EHorseGait::Stop;
	ClampGaitToEnvelope();
}

void UHorseLocomotionComponent::SetGaitEnvelope(EHorseGait InMin, EHorseGait InMax)
{
	if (InMin > InMax)
	{
		std::swap(InMin, InMax);
	}
	MinGait = InMin;
	MaxGait = InMax;
	ClampGaitToEnvelope();
}

void UHorseLocomotionComponent::ClampGaitToEnvelope()
{
	if (Gait < MinGait)      Gait = MinGait;
	else if (Gait > MaxGait) Gait = MaxGait;
}

float UHorseLocomotionComponent::GetGaitTargetSpeed() const
{
	switch (Gait)
	{
	case EHorseGait::Walk:   return WalkSpeed;
	case EHorseGait::Trot:   return TrotSpeed;
	case EHorseGait::Canter: return CanterSpeed;
	case EHorseGait::Gallop: return GallopSpeed;
	default: /* Stop */      return 0.0f;
	}
}

float UHorseLocomotionComponent::GetGaitScaledSpeed() const
{
	if (!Movement)
	{
		return 0.0f;
	}
	const float MaxSpeed = Movement->GetMaxSpeed();
	if (MaxSpeed <= 1.e-3f)
	{
		return 0.0f;
	}
	return std::clamp(GetGaitTargetSpeed() / MaxSpeed, 0.0f, 1.0f);
}

void UHorseLocomotionComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << WalkSpeed;
	Ar << TrotSpeed;
	Ar << CanterSpeed;
	Ar << GallopSpeed;
	Ar << GaitUpCooldown;
	Ar << SafeDistance;
	Ar << UserWeight;
	Ar << RoadWeight;
	Ar << InertiaWeight;
	Ar << JumpTriggerDist;
	Ar << bDrawSteeringDebug;
}
