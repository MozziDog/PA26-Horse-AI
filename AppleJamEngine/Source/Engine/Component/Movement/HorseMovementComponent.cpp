#include "HorseMovementComponent.h"

#include "Component/SceneComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Core/TickFunction.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"
#include "Math/MathUtils.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
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
	else if (MoveMode == EHorseMoveMode::Sliding)
	{
		TickSliding(DeltaTime);   // 입력 무시 — 미끄러짐 상태에서는 제어 불가.
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

	// 1) 조향 — 입력 방향으로 yaw 회전
	//    속도 기반 선회율 캡: ω_max = v / MinTurnRadius (rad/s). 저속이면 작아 제자리회전을 막고,
	//    고속이면 MaxTurnRate 로 상한 → 실제 선회반경 = max(v/MaxTurnRate, MinTurnRadius)(빠를수록 넓게).
	if (Strength > 1.e-3f)
	{
		const FVector Dir = Desired.Normalized();
		const float   DesiredYaw = std::atan2(Dir.Y, Dir.X) * RAD_TO_DEG;
		FRotator      Rot = Updated->GetWorldRotation();
		const float   Delta = NormalizeDegrees(DesiredYaw - Rot.Yaw);

		float EffTurnRate = MaxTurnRate;
		if (MinTurnRadius > 1.e-3f)
		{
			const float HorizSpeed = std::max(TurnSpeedFloor,
				std::sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y));
			EffTurnRate = std::min(MaxTurnRate, (HorizSpeed / MinTurnRadius) * RAD_TO_DEG);
		}
		Rot.Yaw += std::clamp(Delta, -EffTurnRate * DeltaTime, EffTurnRate * DeltaTime);
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
	// 몸통 box sweep 으로 벽/급경사면 관통·비비기 차단(램프는 통과). Velocity 도 벽 접선으로 투영될 수 있음.
	FVector DeltaXY(Velocity.X * DeltaTime, Velocity.Y * DeltaTime, 0.0f);
	DeltaXY = ResolveTorsoCollision(Loc, DeltaXY);
	Loc.X += DeltaXY.X;
	Loc.Y += DeltaXY.Y;

	// 지면 raycast 는 몸통 box 중심(root)에서 아래로. snap 시 root=지면+StandHeight(발이 지면 접점).
	FHitResult Ground;
	if (TraceGround(Loc, Ground))
	{
		const float TargetZ = Ground.WorldHitLocation.Z + StandHeight;
		// 지면이 발밑 근처(오르막이면 위, 내리막이 step 이내면 아래)면 스냅.
		if (Loc.Z - TargetZ <= GroundSnapMaxStep)
		{
			Loc.Z = TargetZ;
			Updated->SetWorldLocation(Loc);
			// 경사가 급하면 보행 불가 → 미끄러짐 진입(다음 tick 부터 TickSliding).
			if (GroundNormal(Ground).Z < WalkableSlopeZ)
			{
				MoveMode = EHorseMoveMode::Sliding;
			}
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
	Velocity.Z += GetGravity().Z * DeltaTime;   // 전역 중력(하향, Z<0)

	FVector Loc = Updated->GetWorldLocation();
	Loc += Velocity * DeltaTime;   // XY 관성 유지 + Z gravity
	Updated->SetWorldLocation(Loc);

	if (Velocity.Z > 0.0f)
	{
		return;   // 상승 중엔 착지 체크 skip.
	}

	FHitResult Ground;
	if (TraceGround(Loc, Ground))
	{
		const float TargetZ = Ground.WorldHitLocation.Z + StandHeight;
		if (Loc.Z <= TargetZ)   // 발이 지면에 도달/관통 → 착지.
		{
			Loc.Z = TargetZ;
			Updated->SetWorldLocation(Loc);
			Velocity.Z = 0.0f;
			// 급경사면에 착지하면 곧바로 미끄러짐.
			MoveMode = (GroundNormal(Ground).Z < WalkableSlopeZ)
				? EHorseMoveMode::Sliding
				: EHorseMoveMode::Grounded;
		}
	}
}

void UHorseMovementComponent::TickSliding(float DeltaTime)
{
	USceneComponent* Updated = GetUpdatedComponent();

	FVector Loc = Updated->GetWorldLocation();

	// 지면 조사(급강하 추적 위해 아래로 길게 — StandHeight 아래 발밑에서 SlideGroundProbe 만큼 더).
	FHitResult Ground;
	if (!TraceGround(Loc, Ground, StandHeight + SlideGroundProbe))
	{
		MoveMode = EHorseMoveMode::Falling;   // 지면 사라짐 → 낙하.
		return;
	}

	const FVector N = GroundNormal(Ground);

	// 중력의 경사면 접선 성분(크기 g*sinθ)으로 가속 후 감쇠.
	const FVector G = GetGravity();
	const FVector DownSlope = G - N * G.Dot(N);
	Velocity += DownSlope * DeltaTime;
	Velocity *= std::clamp(1.0f - SlideFriction * DeltaTime, 0.0f, 1.0f);

	// 이동 후 경사면에 재스냅.
	Loc += Velocity * DeltaTime;

	FHitResult After;
	if (!TraceGround(Loc, After, StandHeight + SlideGroundProbe))
	{
		Updated->SetWorldLocation(Loc);
		MoveMode = EHorseMoveMode::Falling;   // 가장자리 넘어감 → 낙하.
		return;
	}
	Loc.Z = After.WorldHitLocation.Z + StandHeight;
	Updated->SetWorldLocation(Loc);

	// 속도를 지면 접선으로 투영(수직 성분 제거) — 표면을 따라 미끄러지게 유지.
	const FVector NAfter = GroundNormal(After);
	Velocity = Velocity - NAfter * Velocity.Dot(NAfter);

	// 완경사/평지 도달 → 보행 복귀.
	if (NAfter.Z >= WalkableSlopeZ)
	{
		Velocity.Z = 0.0f;
		MoveMode = EHorseMoveMode::Grounded;
	}
}

void UHorseMovementComponent::Jump()
{
	if (MoveMode != EHorseMoveMode::Grounded)
	{
		return;   // 공중/미끄러짐 중엔 재점프 불가.
	}
	Velocity.Z = JumpSpeed;
	MoveMode   = EHorseMoveMode::Falling;
}

void UHorseMovementComponent::Brake()
{
	// 아무것도 안해도 자연적으로 BrakingDecleration 적용됨.
	// 정지 시의 별도 로직 필요하면 여기에 작성 (e.g. 애니메이션, 먼지 효과 등등)
}

FVector UHorseMovementComponent::GetGravity() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetWorldSettings().Gravity;
	}
	return FVector(0.0f, 0.0f, -9.81f);   // world 부재 시 지구 중력 fallback.
}

bool UHorseMovementComponent::TraceGround(const FVector& From, FHitResult& OutHit, float MaxDist) const
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
	const float   Dist = (MaxDist > 0.0f) ? MaxDist : (StandHeight + GroundSnapMaxStep);
	// 바닥은 WorldStatic ObjectType 만 후보(다이내믹/폰을 바닥으로 오인하지 않음) — CMC 와 동일.
	return World->PhysicsRaycastByObjectTypes(From, Dir, Dist, OutHit,
		ObjectTypeBit(ECollisionChannel::WorldStatic), Owner);
}

FVector UHorseMovementComponent::GroundNormal(const FHitResult& Hit) const
{
	FVector N = Hit.ImpactNormal;   // 면 노멀 우선(경사 판정에 적합).
	if (N.IsNearlyZero())
	{
		N = Hit.WorldNormal;
	}
	if (N.IsNearlyZero())
	{
		return FVector(0.0f, 0.0f, 1.0f);
	}
	return N.Normalized();
}

FVector UHorseMovementComponent::ResolveTorsoCollision(const FVector& FromLoc, const FVector& DeltaXY)
{
	if (!bTorsoCollision || DeltaXY.IsNearlyZero())
	{
		return DeltaXY;
	}
	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	USceneComponent* Updated = GetUpdatedComponent();
	UBoxComponent* BoxRoot = Cast<UBoxComponent>(Updated);
	if (!World || !BoxRoot)
	{
		return DeltaXY;   // root 가 몸통 box 가 아니면 몸통 충돌 skip.
	}

	// root(=몸통 box 중심)에서 facing 정렬 box 를 sweep. 형상은 root box 에서 읽어 단일 소스 유지.
	const FVector         Center = FromLoc;
	const FQuat           Rot    = Updated->GetWorldRotation().ToQuaternion();
	const FCollisionShape Box    = FCollisionShape::MakeBox(BoxRoot->GetScaledBoxExtent());

	FHitResult Hit;
	if (!World->PhysicsSweepByObjectTypes(Center, Center + DeltaXY, Rot, Box, Hit,
			ObjectTypeBit(ECollisionChannel::WorldStatic), Owner))
	{
		return DeltaXY;   // 막힘 없음.
	}

	// walkable 면(램프 등)은 무시 — 지면 스냅이 처리. 급경사/벽만 차단.
	const FVector N = GroundNormal(Hit);
	if (N.Z >= WalkableSlopeZ)
	{
		return DeltaXY;
	}

	// 벽 앞 skin 만큼 남기고 허용 이동 거리 산출.
	const float   MoveLen = DeltaXY.Length();
	const float   Allowed = std::max(0.0f, std::min(MoveLen, Hit.Distance - TorsoSkin));
	const FVector Dir     = (MoveLen > 1.e-4f) ? DeltaXY * (1.0f / MoveLen) : FVector(0.0f, 0.0f, 0.0f);
	const FVector Result  = Dir * Allowed;

	// 벽의 수평 노멀 방향 velocity 성분 제거 → 벽을 따라 미끄러지되 관통·비비기 금지.
	FVector WallN(N.X, N.Y, 0.0f);
	if (!WallN.IsNearlyZero())
	{
		WallN = WallN.Normalized();
		Velocity = Velocity - WallN * Velocity.Dot(WallN);
	}
	return Result;
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
	Ar << GroundSnapMaxStep;
	Ar << StandHeight;
	Ar << MinTurnRadius;
	Ar << TurnSpeedFloor;
	Ar << WalkableSlopeZ;
	Ar << SlideFriction;
	Ar << SlideGroundProbe;
	Ar << bTorsoCollision;
	Ar << TorsoSkin;
	Ar << JumpSpeed;
}
