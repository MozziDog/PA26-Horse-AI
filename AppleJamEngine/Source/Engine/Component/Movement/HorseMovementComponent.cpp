#include "HorseMovementComponent.h"

#include "Component/SceneComponent.h"
#include "Core/TickFunction.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Rotator.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float RadToDeg = 57.2957795131f;

	// [-180, 180] 로 정규화한 각도(도).
	float NormalizeDegrees(float Angle)
	{
		Angle = std::fmod(Angle, 360.0f);
		if (Angle > 180.0f)       Angle -= 360.0f;
		else if (Angle <= -180.0f) Angle += 360.0f;
		return Angle;
	}

	// Current 를 Target 방향으로 MaxDelta 만큼만 이동.
	float MoveTowards(float Current, float Target, float MaxDelta)
	{
		const float Diff = Target - Current;
		if (std::abs(Diff) <= MaxDelta)
		{
			return Target;
		}
		return Current + (Diff > 0.0f ? MaxDelta : -MaxDelta);
	}
}

UHorseMovementComponent::UHorseMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// physics 결과 이후의 지면 raycast 가 최신이도록 PostPhysics. BT(입력 생산)와 같은 그룹이지만
	// add/consume 규약이라 순서에 민감하지 않다(입력이 sticky — 최대 1 frame 지연 허용).
	PrimaryComponentTick.SetTickGroup(TG_PostPhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PostPhysics);
}

void UHorseMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	if (!GetUpdatedComponent() || DeltaTime <= 0.0f)
	{
		return;
	}

	FVector Desired;
	ConsumeInputVector(Desired);
	Desired.Z = 0.0f;   // 조향/전진은 XY 평면만. 고저(낙하)는 mode 가 결정.

	if (MoveMode == EHorseMoveMode::Grounded)
	{
		ApplySteeringAndSpeed(Desired, DeltaTime);
		TickGrounded(DeltaTime);
	}
	else
	{
		TickFalling(DeltaTime);
	}
}

void UHorseMovementComponent::ApplySteeringAndSpeed(const FVector& Desired, float DeltaTime)
{
	USceneComponent* Updated = GetUpdatedComponent();
	const float Strength = std::clamp(Desired.Length(), 0.0f, 1.0f);

	// 1) 조향 — 입력 방향으로 yaw 를 MaxTurnRate 로 회전(비홀로노믹: 즉시 옆으로 못 가고 돌아선다).
	if (Strength > 1.e-3f)
	{
		const FVector Dir = Desired.Normalized();
		const float   DesiredYaw = std::atan2(Dir.Y, Dir.X) * RadToDeg;
		FRotator      Rot = Updated->GetWorldRotation();
		const float   Delta = NormalizeDegrees(DesiredYaw - Rot.Yaw);
		Rot.Yaw += std::clamp(Delta, -MaxTurnRate * DeltaTime, MaxTurnRate * DeltaTime);
		Updated->SetWorldRotation(Rot);
	}

	// 2) 전진 — forward 방향 목표속도로 가감속(스칼라). velocity 는 항상 forward 정렬(후진 없음).
	FVector Forward = Updated->GetForwardVector();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	const float Target = MaxSpeed * Strength;
	float       Speed  = std::max(0.0f, Velocity.Dot(Forward));
	const float Rate   = (Target > Speed) ? MaxAcceleration : BrakingDeceleration;
	Speed = MoveTowards(Speed, Target, Rate * DeltaTime);

	Velocity.X = Forward.X * Speed;
	Velocity.Y = Forward.Y * Speed;
}

void UHorseMovementComponent::TickGrounded(float DeltaTime)
{
	USceneComponent* Updated = GetUpdatedComponent();
	Velocity.Z = 0.0f;

	FVector Loc = Updated->GetWorldLocation();
	Loc.X += Velocity.X * DeltaTime;
	Loc.Y += Velocity.Y * DeltaTime;

	FHitResult Ground;
	if (TraceGround(Loc + FVector(0.0f, 0.0f, GroundProbeUp), Ground))
	{
		const float TargetZ = Ground.WorldHitLocation.Z + FootHeightOffset;
		// 지면이 발밑 근처(오르막이면 위, 내리막이 step 이내면 아래)면 스냅.
		if (Loc.Z - TargetZ <= GroundSnapMaxStep)
		{
			Loc.Z = TargetZ;
			Updated->SetWorldLocation(Loc);
			return;
		}
		// 지면이 step 이상 아래 → 낭떠러지.
	}

	// 지면 없음(또는 너무 아래) → 낙하 시작. XY 는 진행시키되 Z 는 유지하고 mode 전환.
	Updated->SetWorldLocation(Loc);
	MoveMode = EHorseMoveMode::Falling;
}

void UHorseMovementComponent::TickFalling(float DeltaTime)
{
	USceneComponent* Updated = GetUpdatedComponent();
	Velocity.Z -= Gravity * DeltaTime;

	FVector Loc = Updated->GetWorldLocation();
	Loc += Velocity * DeltaTime;   // XY 관성 유지 + Z gravity
	Updated->SetWorldLocation(Loc);

	if (Velocity.Z > 0.0f)
	{
		return;   // 상승 중엔 착지 체크 skip.
	}

	FHitResult Ground;
	if (TraceGround(Loc + FVector(0.0f, 0.0f, GroundProbeUp), Ground))
	{
		const float TargetZ = Ground.WorldHitLocation.Z + FootHeightOffset;
		if (Loc.Z <= TargetZ)   // 발이 지면에 도달/관통 → 착지.
		{
			Loc.Z = TargetZ;
			Updated->SetWorldLocation(Loc);
			Velocity.Z = 0.0f;
			MoveMode = EHorseMoveMode::Grounded;
		}
	}
}

bool UHorseMovementComponent::TraceGround(const FVector& From, FHitResult& OutHit) const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Dir(0.0f, 0.0f, -1.0f);
	const float   MaxDist = GroundProbeUp + GroundSnapMaxStep;
	// 바닥은 WorldStatic ObjectType 만 후보(다이내믹/폰을 바닥으로 오인하지 않음) — CMC 와 동일.
	return World->PhysicsRaycastByObjectTypes(From, Dir, MaxDist, OutHit,
		ObjectTypeBit(ECollisionChannel::WorldStatic), Owner);
}

float UHorseMovementComponent::GetForwardSpeed() const
{
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated)
	{
		return 0.0f;
	}
	FVector Forward = Updated->GetForwardVector();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return 0.0f;
	}
	return Velocity.Dot(Forward.Normalized());
}

void UHorseMovementComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << MaxSpeed;
	Ar << MaxAcceleration;
	Ar << BrakingDeceleration;
	Ar << MaxTurnRate;
	Ar << Gravity;
	Ar << GroundProbeUp;
	Ar << GroundSnapMaxStep;
	Ar << FootHeightOffset;
}
