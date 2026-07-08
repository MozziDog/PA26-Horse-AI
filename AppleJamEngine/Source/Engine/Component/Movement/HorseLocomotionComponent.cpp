#include "HorseLocomotionComponent.h"

#include "HorseMovementComponent.h"
#include "Component/AI/BlackboardComponent.h"
#include "Core/TickFunction.h"
#include "GameFramework/AActor.h"
#include "Serialization/Archive.h"

#include <algorithm>

namespace
{
	EHorseGait GaitStep(EHorseGait Gait, int Delta)
	{
		return static_cast<EHorseGait>(static_cast<uint8>(Gait) + Delta);
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
	Gait          = EHorseGait::Stop;
	GaitUpTimer   = 0.0f;
	SteeringInput = 0.0f;
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
		if (BlackboardComp->GetBlackboard().TryGetInt(FName("DesiredGait"), Desired))
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

	// 조향은 단순 yaw 변화로 구현된 상태
	// 주행 속도 별 조향각은 튜닝 필요
	FVector Forward = Owner->GetActorForward();
	FVector Right   = Owner->GetActorRight();
	Forward.Z = 0.0f;
	Right.Z   = 0.0f;
	FVector Dir = Forward + Right * (SteeringInput * SteerStrength);
	if (Dir.IsNearlyZero())
	{
		return;
	}

	// gait → scale([0,1]). Movement 는 MaxSpeed*scale 을 목표속도로 삼는다.
	Movement->AddInputVector(Dir.Normalized(), GetGaitScaledSpeed());
}

void UHorseLocomotionComponent::SetSteeringInput(float Value)
{
	SteeringInput = std::clamp(Value, -1.0f, 1.0f);
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
	Ar << SteerStrength;
	Ar << WalkSpeed;
	Ar << TrotSpeed;
	Ar << CanterSpeed;
	Ar << GallopSpeed;
	Ar << GaitUpCooldown;
}
