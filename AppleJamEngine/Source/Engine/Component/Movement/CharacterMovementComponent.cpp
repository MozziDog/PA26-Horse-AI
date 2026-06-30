#include "CharacterMovementComponent.h"

#include "Animation/AnimInstance.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/SceneComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Types/PropertyTypes.h"
#include "Core/TickFunction.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Pawn/Character.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Physics/IPhysicsScene.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

namespace
{
	const char* MovementModeToString(EMovementMode Mode)
	{
		switch (Mode)
		{
		case EMovementMode::Walking:
			return "Walking";
		case EMovementMode::Falling:
			return "Falling";
		default:
			return "Unknown";
		}
	}
}

UCharacterMovementComponent::UCharacterMovementComponent()
{
	// USkeletalMeshComponent::TickComponent (TG_PrePhysics, default) 가 UpdateAnimation 으로
	// AnimInstance->PendingRootMotion 을 채운 다음에 CMC 가 그 값을 가져가야 같은 frame 데이터를
	// 쓸 수 있다. Prerequisite API 가 우리 엔진에 없으므로 TickGroup 분리로 순서 보장.
	// FTickManager 가 group 순서대로 실행하므로 PrePhysics 가 모두 끝난 뒤 DuringPhysics 가 돈다.
	PrimaryComponentTick.SetTickGroup(TG_DuringPhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_DuringPhysics);
}

void UCharacterMovementComponent::AddInputVector(const FVector& WorldDirection, float ScaleValue)
{
	AccumulatedInput = AccumulatedInput + WorldDirection * ScaleValue;
}

void UCharacterMovementComponent::ConsumeInputVector(FVector& Out)
{
	Out = AccumulatedInput;
	AccumulatedInput = FVector(0.0f, 0.0f, 0.0f);
}

void UCharacterMovementComponent::AddRootMotionDelta(const FTransform& LocalDelta)
{
	if (!bHasPendingRootMotion)
	{
		PendingRootMotion = LocalDelta;
		bHasPendingRootMotion = true;
		return;
	}

	const FMatrix M = PendingRootMotion.ToMatrix() * LocalDelta.ToMatrix();
	PendingRootMotion.Location = FVector(M.M[3][0], M.M[3][1], M.M[3][2]);
	PendingRootMotion.Rotation = (PendingRootMotion.Rotation * LocalDelta.Rotation).GetNormalized();
	// Scale 은 root motion 에서 보통 1 — 무시.
}

bool UCharacterMovementComponent::ConsumePendingRootMotion(FTransform& OutLocalDelta)
{
	if (!bHasPendingRootMotion)
	{
		OutLocalDelta = FTransform();   // Identity
		return false;
	}
	OutLocalDelta = PendingRootMotion;
	PendingRootMotion = FTransform();
	bHasPendingRootMotion = false;
	return true;
}

void UCharacterMovementComponent::SetMovementMode(EMovementMode NewMode)
{
	if (MovementMode == NewMode) return;
	const EMovementMode OldMode = MovementMode;
	MovementMode = NewMode;
	const FVector Loc = GetUpdatedComponent() ? GetUpdatedComponent()->GetWorldLocation() : FVector::ZeroVector;
	UE_LOG("[CharacterMovement] MovementMode changed actor=%s %s->%s loc=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f)",
		GetOwner() ? GetOwner()->GetName().c_str() : "None",
		MovementModeToString(OldMode),
		MovementModeToString(NewMode),
		Loc.X, Loc.Y, Loc.Z,
		Velocity.X, Velocity.Y, Velocity.Z);
	// 추후 OnMovementModeChanged delegate 위치.
}

void UCharacterMovementComponent::Jump()
{
	// Walking 중에만 점프 허용 — 공중 다단 점프 막음. (필요 시 자식 override.)
	const FVector Loc = GetUpdatedComponent() ? GetUpdatedComponent()->GetWorldLocation() : FVector::ZeroVector;
	UE_LOG("[CharacterMovement] Jump requested actor=%s mode=%s loc=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f) wantsJump=%s",
		GetOwner() ? GetOwner()->GetName().c_str() : "None",
		MovementModeToString(MovementMode),
		Loc.X, Loc.Y, Loc.Z,
		Velocity.X, Velocity.Y, Velocity.Z,
		bWantsJump ? "true" : "false");

	if (MovementMode != EMovementMode::Walking)
	{
		FHitResult Floor;
		if (TraceFloor(Floor) && IsWalkableFloorHit(Floor))
		{
			if (USceneComponent* Updated = GetUpdatedComponent())
			{
				FVector RecoverLoc = Updated->GetWorldLocation();
				RecoverLoc.Z = Floor.WorldHitLocation.Z + GetCapsuleHalfHeight();
				Updated->SetWorldLocation(RecoverLoc);
			}
			Velocity.Z = 0.0f;
			SetMovementMode(EMovementMode::Walking);
			UE_LOG("[CharacterMovement] Jump recovered walking actor=%s floor=(%.3f,%.3f,%.3f) normal=(%.3f,%.3f,%.3f)",
				GetOwner() ? GetOwner()->GetName().c_str() : "None",
				Floor.WorldHitLocation.X, Floor.WorldHitLocation.Y, Floor.WorldHitLocation.Z,
				Floor.WorldNormal.X, Floor.WorldNormal.Y, Floor.WorldNormal.Z);
		}
		else
		{
			UE_LOG("[CharacterMovement] Jump rejected actor=%s reason=not_walking mode=%s floorHit=%s loc=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f)",
				GetOwner() ? GetOwner()->GetName().c_str() : "None",
				MovementModeToString(MovementMode),
				Floor.bHit ? "true" : "false",
				Loc.X, Loc.Y, Loc.Z,
				Velocity.X, Velocity.Y, Velocity.Z);
			return;
		}
	}

	bWantsJump = true;
	UE_LOG("[CharacterMovement] Jump queued actor=%s jumpZ=%.3f",
		GetOwner() ? GetOwner()->GetName().c_str() : "None",
		JumpZVelocity);
}

void UCharacterMovementComponent::StopMovementImmediately()
{
	Velocity.X = 0.0f;
	Velocity.Y = 0.0f;
	Velocity.Z = 0.0f;
	AccumulatedInput = FVector(0.0f, 0.0f, 0.0f);
}

void UCharacterMovementComponent::SetMovementInputBlocked(bool bBlocked)
{
	bMovementInputBlocked = bBlocked;
	if (bMovementInputBlocked)
	{
		AccumulatedInput = FVector(0.0f, 0.0f, 0.0f);
		Velocity.X = 0.0f;
		Velocity.Y = 0.0f;
	}
}

bool UCharacterMovementComponent::StartDash(const FVector& WorldDirection, float Distance, float Duration)
{
	if (Distance <= 0.0f || Duration <= 0.0f)
	{
		return false;
	}

	FVector Direction(WorldDirection.X, WorldDirection.Y, 0.0f);
	if (Direction.IsNearlyZero())
	{
		if (USceneComponent* Updated = GetUpdatedComponent())
		{
			const FRotator ActorRot = Updated->GetWorldRotation();
			Direction = FRotator(0.0f, 0.0f, ActorRot.Yaw).ToQuaternion().RotateVector(FVector(1.0f, 0.0f, 0.0f));
		}
	}

	if (Direction.IsNearlyZero())
	{
		return false;
	}

	Direction = Direction.Normalized();
	Velocity.X = 0.0f;
	Velocity.Y = 0.0f;
	AccumulatedInput = FVector(0.0f, 0.0f, 0.0f);
	DashVelocity = Direction * (Distance / Duration);
	DashRemainingTime = Duration;
	return true;
}

void UCharacterMovementComponent::StopDash()
{
	DashVelocity = FVector(0.0f, 0.0f, 0.0f);
	DashRemainingTime = 0.0f;
}

void UCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated) return;
	if (DeltaTime <= 0.0f) return;

	bAppliedRootMotionRotationThisFrame = false;

	FVector Input;
	ConsumeInputVector(Input);
	Input.Z = 0.0f;   // XY 평면만 — Z 는 mode 가 결정.
	if (bMovementInputBlocked || (bIgnoreInputWhileDashing && IsDashing()))
	{
		Input = FVector(0.0f, 0.0f, 0.0f);
	}

	// 1) Input 처리 — XY velocity 갱신 (양 mode 공통).
	ApplyInputToVelocity(Input, DeltaTime);

	// 1.5) Owner Character 의 Mesh AnimInstance 가 누적해둔 root motion 을 가져와 자기 buffer 로 push.
	//      Mesh tick (TG_PrePhysics) 이 이미 끝나 PendingRootMotion 이 채워진 상태.
	//      Mode 가 Ignore 면 가져갈 필요 자체가 없음 (AccumulateRootMotion 측에서 누적도 안 됨).
	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh())
		{
			if (UAnimInstance* AI = Mesh->GetAnimInstance())
			{
				if (AI->GetRootMotionMode() != ERootMotionMode::IgnoreRootMotion)
				{
					if (AI->HasPendingRootMotion())
					{
						AddRootMotionDelta(AI->ConsumeRootMotion());
					}
				}
			}
		}
	}

	// 2) Root motion 소비 — extracted translation is already authored in the sequence's
	//    root-track frame. Keep one world-space basis for a continuous root-motion segment;
	//    otherwise the root rotation applied to the capsule re-rotates later translation deltas.
	FTransform RootMotionDelta;
	const bool bHadRootMotion = ConsumePendingRootMotion(RootMotionDelta);
	FVector RootMotionWorldDelta(0.0f, 0.0f, 0.0f);
	if (bHadRootMotion)
	{
		if (!bHasRootMotionTranslationBasis)
		{
			RootMotionTranslationBasis = Updated->GetWorldRotation().ToQuaternion().GetNormalized();
			bHasRootMotionTranslationBasis = true;
		}
		RootMotionWorldDelta = RootMotionTranslationBasis.RotateVector(RootMotionDelta.Location);
	}
	else
	{
		bHasRootMotionTranslationBasis = false;
	}

	const FVector DashWorldXY = ConsumeDashOffset(DeltaTime);
	const FVector ExtraWorldDelta = RootMotionWorldDelta + DashWorldXY;

	// 3) Mode 별 root motion 해석 + 위치 적용.
	if (MovementMode == EMovementMode::Walking)
	{
		TickWalking(DeltaTime, ExtraWorldDelta);
	}
	else
	{
		TickFalling(DeltaTime, ExtraWorldDelta);
	}

	// 4) Root motion rotation 적용. translation 과 같은 local delta 를 rotation 에도 그대로 쓴다.
	if (bHadRootMotion)
	{
		const FRotator DeltaRot = RootMotionDelta.Rotation.ToRotator();
		if (std::fabs(DeltaRot.Pitch) > 1e-4f ||
			std::fabs(DeltaRot.Yaw) > 1e-4f ||
			std::fabs(DeltaRot.Roll) > 1e-4f)
		{
			Updated->AddLocalRotation(RootMotionDelta.Rotation);
			bAppliedRootMotionRotationThisFrame = true;
		}
	}

	// 5) Orient yaw to movement direction. Root motion rotation 이 활성인 frame 은 skip —
	//    그렇지 않으면 PhysOrient 가 root motion 회전을 다시 덮어쓴다.
	if (bOrientRotationToMovement && !bAppliedRootMotionRotationThisFrame)
	{
		PhysOrientToMovement(DeltaTime);
	}
}

void UCharacterMovementComponent::PhysOrientToMovement(float DeltaTime)
{
	USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated) return;

	// 평면 속도 작으면 회전 skip — 마지막 facing 유지.
	const float SpeedSq2D = Velocity.X * Velocity.X + Velocity.Y * Velocity.Y;
	constexpr float MinSpeedSq = 1e-4f;
	if (SpeedSq2D < MinSpeedSq) return;

	// Target yaw — Velocity 방향. UE 의 atan2(Y, X) 는 +X 가 0°, +Y 가 90° (좌표계 가정).
	const float TargetYaw = std::atan2(Velocity.Y, Velocity.X) * (180.0f / 3.14159265f);

	FRotator R = Updated->GetRelativeRotation();
	const float CurrentYaw = R.Yaw;

	if (bSnapRotationToMovement)
	{
		R.Yaw = TargetYaw;
		Updated->SetRelativeRotation(R);
		return;
	}

	// 최단 회전 방향 (delta ∈ [-180, 180])
	float Delta = TargetYaw - CurrentYaw;
	while (Delta >  180.0f) Delta -= 360.0f;
	while (Delta < -180.0f) Delta += 360.0f;

	const float Step = RotationYawRate * DeltaTime;
	if (std::fabs(Delta) <= Step)
	{
		R.Yaw = TargetYaw;
	}
	else
	{
		R.Yaw = CurrentYaw + (Delta > 0.0f ? Step : -Step);
	}
	Updated->SetRelativeRotation(R);
}

void UCharacterMovementComponent::ApplyInputToVelocity(const FVector& Input, float DeltaTime)
{
	const float InputLen = Input.Length();
	if (bUseInstantMovementInput)
	{
		if (InputLen > 0.0f)
		{
			const FVector Direction = Input * (1.0f / InputLen);
			Velocity.X = Direction.X * MaxWalkSpeed;
			Velocity.Y = Direction.Y * MaxWalkSpeed;
		}
		else if (MovementMode == EMovementMode::Walking)
		{
			Velocity.X = 0.0f;
			Velocity.Y = 0.0f;
		}
		return;
	}

	if (InputLen > 0.0f)
	{
		// 입력 방향으로 가속 (XY 만).
		const FVector Direction = Input * (1.0f / InputLen);
		Velocity.X += Direction.X * MaxAcceleration * DeltaTime;
		Velocity.Y += Direction.Y * MaxAcceleration * DeltaTime;
	}
	else if (MovementMode == EMovementMode::Walking)
	{
		// Walking 에선 input 없으면 braking. Falling 중 air control 없음 = 평면 속도 유지.
		FVector V2D(Velocity.X, Velocity.Y, 0.0f);
		const float Speed2D = V2D.Length();
		if (Speed2D > 0.0f)
		{
			const float NewSpeed = std::max(0.0f, Speed2D - BrakingFriction * DeltaTime);
			const FVector Dir    = V2D * (1.0f / Speed2D);
			Velocity.X = Dir.X * NewSpeed;
			Velocity.Y = Dir.Y * NewSpeed;
		}
	}

	// MaxWalkSpeed 클램프 (평면 속도만).
	FVector V2D(Velocity.X, Velocity.Y, 0.0f);
	const float Speed2D = V2D.Length();
	if (Speed2D > MaxWalkSpeed)
	{
		const FVector Dir = V2D * (1.0f / Speed2D);
		Velocity.X = Dir.X * MaxWalkSpeed;
		Velocity.Y = Dir.Y * MaxWalkSpeed;
	}
}

FVector UCharacterMovementComponent::ConsumeDashOffset(float DeltaTime)
{
	if (DashRemainingTime <= 0.0f || DeltaTime <= 0.0f)
	{
		StopDash();
		return FVector(0.0f, 0.0f, 0.0f);
	}

	const float StepTime = (std::min)(DeltaTime, DashRemainingTime);
	DashRemainingTime -= StepTime;

	FVector Offset = DashVelocity * StepTime;
	Offset.Z = 0.0f;

	if (DashRemainingTime <= 0.0f)
	{
		StopDash();
	}

	return Offset;
}

void UCharacterMovementComponent::TickWalking(float DeltaTime, const FVector& RootMotionWorldDelta)
{
	USceneComponent* Updated = GetUpdatedComponent();

	// Jump 의도가 있으면 — Velocity.Z 박고 즉시 Falling 으로 전환. 이 frame 의 XY 는 그대로 진행.
	if (bWantsJump)
	{
		bWantsJump = false;
		Velocity.Z = JumpZVelocity;
		UE_LOG("[CharacterMovement] Jump launched actor=%s velocity=(%.3f,%.3f,%.3f)",
			GetOwner() ? GetOwner()->GetName().c_str() : "None",
			Velocity.X, Velocity.Y, Velocity.Z);
		SetMovementMode(EMovementMode::Falling);

		// 바닥에 붙어있는 frame에서 즉시 falling sweep을 돌리면 capsule의 초기 floor 접촉이
		// 충돌로 잡혀 Velocity.Z가 0이 되고 곧바로 Landed 처리될 수 있다. 점프 frame은
		// 아주 살짝 들어 올리고 다음 tick부터 Falling 이동을 시작한다.
		if (Updated)
		{
			const float Lift = (std::max)(0.02f, (std::min)(0.05f, FloorProbeDistance * 0.5f));
			Updated->SetWorldLocation(Updated->GetWorldLocation() + FVector(0.0f, 0.0f, Lift));
			UE_LOG("[CharacterMovement] Jump lift applied actor=%s lift=%.3f",
				GetOwner() ? GetOwner()->GetName().c_str() : "None",
				Lift);
		}
		return;
	}

	// Walking 중 Z velocity 는 0 — floor stick 으로만 Z 결정.
	Velocity.Z = 0.0f;

	// Walking 은 floor stick 이 Z 를 결정하므로 root motion 도 평면 이동만 소비.
	const FVector XYOffset(
		Velocity.X * DeltaTime + RootMotionWorldDelta.X,
		Velocity.Y * DeltaTime + RootMotionWorldDelta.Y,
		0.0f);
	MoveAlongFloor(XYOffset);

	// Floor 잡혔는지 — 이동 직후 위치에서 다시 trace.
	FHitResult Floor;
	if (!TraceFloor(Floor))
	{
		const FVector CurrentLoc = Updated ? Updated->GetWorldLocation() : FVector::ZeroVector;
		UE_LOG("[CharacterMovement] Walking lost floor actor=%s loc=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f) probe=%.3f",
			GetOwner() ? GetOwner()->GetName().c_str() : "None",
			CurrentLoc.X, CurrentLoc.Y, CurrentLoc.Z,
			Velocity.X, Velocity.Y, Velocity.Z,
			FloorProbeDistance);
		// 발 아래 floor 없음 (예: 절벽 끝) → falling 전환.
		SetMovementMode(EMovementMode::Falling);
		return;
	}

	// Floor stick — capsule 중심 = floor.Z + HalfHeight.
	FVector NewLoc = Updated->GetWorldLocation();
	NewLoc.Z = Floor.WorldHitLocation.Z + GetCapsuleHalfHeight();
	Updated->SetWorldLocation(NewLoc);
}

void UCharacterMovementComponent::TickFalling(float DeltaTime, const FVector& RootMotionWorldDelta)
{
	USceneComponent* Updated = GetUpdatedComponent();

	// Gravity — Z 만. (양수 Gravity → -Z 가속)
	Velocity.Z -= Gravity * DeltaTime;

	// Falling 은 root motion world delta 전체를 gravity 적분과 함께 소비한다.
	const FVector Offset(
		Velocity.X * DeltaTime + RootMotionWorldDelta.X,
		Velocity.Y * DeltaTime + RootMotionWorldDelta.Y,
		Velocity.Z * DeltaTime + RootMotionWorldDelta.Z);
	SafeMoveUpdatedComponent(Offset);

	// 올라가는 중 (점프 arc 상승) 엔 floor 체크 skip — 안 그러면 점프 직후 1 frame 의
	// 작은 상승 (≈ JumpZVelocity * dt) 이 raycast probe 거리 안에 있어 즉시 착지로 잡힘.
	// UE 도 동일 — Velocity.Z > 0 이면 ground 안 잡음.
	if (Velocity.Z > 0.0f) return;

	// 떨어지는 중에만 floor 체크.
	FHitResult Floor;
	if (!TraceFloor(Floor)) return;

	// 착지 — capsule Z 보정 + Walking 전환 + Velocity.Z = 0.
	// raycast 가 hit 했다는 건 capsule bottom 이 floor 위 (또는 약간 안) 에 있다는 뜻.
	// hit 위치를 floor 표면으로 보고 그 위에 stick.
	FVector LandLoc = Updated->GetWorldLocation();
	LandLoc.Z = Floor.WorldHitLocation.Z + GetCapsuleHalfHeight();
	Updated->SetWorldLocation(LandLoc);
	Velocity.Z = 0.0f;
	UE_LOG("[CharacterMovement] Landed actor=%s floor=(%.3f,%.3f,%.3f) loc=(%.3f,%.3f,%.3f)",
		GetOwner() ? GetOwner()->GetName().c_str() : "None",
		Floor.WorldHitLocation.X, Floor.WorldHitLocation.Y, Floor.WorldHitLocation.Z,
		LandLoc.X, LandLoc.Y, LandLoc.Z);
	SetMovementMode(EMovementMode::Walking);
}

bool UCharacterMovementComponent::IsWalkableFloorHit(const FHitResult& Hit) const
{
	constexpr float WalkableFloorZ = 0.7f;
	const FVector& Normal = !Hit.ImpactNormal.IsNearlyZero() ? Hit.ImpactNormal : Hit.WorldNormal;
	return Hit.bHit && Normal.Z >= WalkableFloorZ;
}

bool UCharacterMovementComponent::MoveAlongFloor(const FVector& Delta, FHitResult* OutHit)
{
	if (OutHit)
	{
		*OutHit = FHitResult();
	}

	if (Delta.Length() <= 1.e-6f)
	{
		return true;
	}

	USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Updated);
	if (!Owner || !World || !Capsule)
	{
		Updated->SetWorldLocation(Updated->GetWorldLocation() + Delta);
		return true;
	}

	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	if (Radius <= 0.0f || HalfHeight <= 0.0f)
	{
		Updated->SetWorldLocation(Updated->GetWorldLocation() + Delta);
		return true;
	}

	ECollisionChannel TraceChannel = ECollisionChannel::Pawn;
	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Updated))
	{
		TraceChannel = Primitive->GetCollisionObjectType();
	}

	const FVector Start = Updated->GetWorldLocation();
	const float SweepLift = (std::max)(0.02f, (std::min)(0.05f, FloorProbeDistance * 0.5f));
	const FVector SweepStart = Start + FVector(0.0f, 0.0f, SweepLift);
	const FVector SweepEnd = SweepStart + Delta;
	const FQuat Rot = Updated->GetWorldMatrix().ToQuat();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);

	FHitResult Hit;
	if (!World->PhysicsSweep(SweepStart, SweepEnd, Rot, Shape, Hit, TraceChannel, Owner))
	{
		Updated->SetWorldLocation(Start + Delta);
		return true;
	}

	if (OutHit)
	{
		*OutHit = Hit;
	}

	if (IsWalkableFloorHit(Hit))
	{
		Updated->SetWorldLocation(Start + Delta);
		return true;
	}

	const FVector MoveDir = Delta.Normalized();
	const float SafeDistance = (std::max)(0.0f, Hit.Distance - SweepPullbackDistance);
	Updated->SetWorldLocation(Start + MoveDir * SafeDistance);

	if (UPrimitiveComponent* MovingPrimitive = Cast<UPrimitiveComponent>(Updated))
	{
		MovingPrimitive->NotifyComponentHit(
			MovingPrimitive,
			Hit.HitActor,
			Hit.HitComponent,
			FVector::ZeroVector,
			Hit
		);
	}

	if (!Hit.ImpactNormal.IsNearlyZero())
	{
		const float VelocityIntoSurface = Velocity.Dot(Hit.ImpactNormal);
		if (VelocityIntoSurface < 0.0f)
		{
			Velocity = Velocity - Hit.ImpactNormal * VelocityIntoSurface;
		}
	}

	return false;
}

bool UCharacterMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, FHitResult* OutHit)
{
    if (OutHit)
    {
        *OutHit = FHitResult();
    }

    if (Delta.Length() <= 1.e-6f)
    {
        return true;
    }

    USceneComponent* Updated = GetUpdatedComponent();
    if (!Updated)
    {
        return false;
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        Updated->SetWorldLocation(Updated->GetWorldLocation() + Delta);
        return true;
    }

    UWorld* World = Owner->GetWorld();
    if (!World)
    {
        Updated->SetWorldLocation(Updated->GetWorldLocation() + Delta);
        return true;
    }

    UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Updated);
    if (!Capsule)
    {
        Updated->SetWorldLocation(Updated->GetWorldLocation() + Delta);
        return true;
    }

    const float Radius     = Capsule->GetScaledCapsuleRadius();
    const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    if (Radius <= 0.0f || HalfHeight <= 0.0f)
    {
        Updated->SetWorldLocation(Updated->GetWorldLocation() + Delta);
        return true;
    }

    ECollisionChannel TraceChannel = ECollisionChannel::Pawn;
    if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Updated))
    {
        TraceChannel = Primitive->GetCollisionObjectType();
    }

    const FVector Start = Updated->GetWorldLocation();
    const FVector End   = Start + Delta;
    const FQuat   Rot   = Updated->GetWorldMatrix().ToQuat();
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);

    FHitResult Hit;
    if (!World->PhysicsSweep(Start, End, Rot, Shape, Hit, TraceChannel, Owner))
    {
        Updated->SetWorldLocation(End);
        return true;
    }

    if (OutHit)
    {
        *OutHit = Hit;
    }

    const FVector MoveDir = Delta.Normalized();
    const float SafeDistance = (std::max)(0.0f, Hit.Distance - SweepPullbackDistance);
    Updated->SetWorldLocation(Start + MoveDir * SafeDistance);

    if (UPrimitiveComponent* MovingPrimitive = Cast<UPrimitiveComponent>(Updated))
    {
        MovingPrimitive->NotifyComponentHit(
            MovingPrimitive,
            Hit.HitActor,
            Hit.HitComponent,
            FVector::ZeroVector,
            Hit
        );
    }

    // 벽/천장/바닥으로 계속 밀어 넣는 속도 성분은 제거한다. 남은 접선 성분은 다음 tick에서 유지되어
    // 최소한의 slide와 비슷하게 동작한다.
    if (!Hit.ImpactNormal.IsNearlyZero())
    {
        const float VelocityIntoSurface = Velocity.Dot(Hit.ImpactNormal);
        if (VelocityIntoSurface < 0.0f)
        {
            Velocity = Velocity - Hit.ImpactNormal * VelocityIntoSurface;
        }
    }

    return false;
}

bool UCharacterMovementComponent::TraceFloor(FHitResult& OutHit) const
{
	USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated) return false;
	AActor* Owner = GetOwner();
	if (!Owner) return false;
	UWorld* World = Owner->GetWorld();
	if (!World) return false;

	const float HalfHeight = GetCapsuleHalfHeight();
	if (HalfHeight <= 0.0f) return false;   // capsule 아니면 floor 의미 없음

	// capsule 중심에서 down — bottom 까지 HalfHeight + 약간의 probe.
	const FVector  Start = Updated->GetWorldLocation();
	const FVector  Dir(0.0f, 0.0f, -1.0f);
	const float    MaxDist = HalfHeight + FloorProbeDistance;

	// 바닥은 WorldStatic ObjectType 만 후보로 본다. 채널 raycast (응답=Block) 시맨틱으로 가면
	// 다이내믹/폰도 기본 응답이 Block 이라 바닥으로 잘못 잡힌다. ObjectType 마스크는
	// shape의 ObjectType 자체를 검사하므로 다이내믹 박스 위 / 다른 폰 머리 위에서도 바닥으로
	// 인식되지 않는다.
	return World->PhysicsRaycastByObjectTypes(Start, Dir, MaxDist, OutHit,
		ObjectTypeBit(ECollisionChannel::WorldStatic), Owner);
}

float UCharacterMovementComponent::GetCapsuleHalfHeight() const
{
	// UpdatedComponent 가 capsule 이라야 의미 있음 — 다른 shape 면 0.
	if (UCapsuleComponent* Cap = Cast<UCapsuleComponent>(GetUpdatedComponent()))
	{
		return Cap->GetScaledCapsuleHalfHeight();
	}
	return 0.0f;
}

void UCharacterMovementComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar << MaxWalkSpeed;
	Ar << MaxAcceleration;
	Ar << BrakingFriction;
	Ar << Gravity;
	Ar << FloorProbeDistance;
	Ar << JumpZVelocity;
	Ar << bOrientRotationToMovement;
	Ar << RotationYawRate;
	Ar << SweepPullbackDistance;
	Ar << bUseInstantMovementInput;
	Ar << bSnapRotationToMovement;
	Ar << bIgnoreInputWhileDashing;
}
