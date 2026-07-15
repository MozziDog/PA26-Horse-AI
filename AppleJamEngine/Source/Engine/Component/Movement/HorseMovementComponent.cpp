#include "HorseMovementComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/Graph/AnimGraphInstance.h"
#include "Component/SceneComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/TickFunction.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"
#include "Math/Transform.h"
#include "Math/MathUtils.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>

UHorseMovementComponent::UHorseMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Mesh 의 UpdateAnimation(TG_PrePhysics) 이 root motion 을 누적한 뒤 소비해야 같은 frame 데이터를
	// 쓸 수 있고, physics 이후의 지면 raycast 도 최신이 된다. 그래서 PostPhysics.
	// 이번 frame 계산한 AnimGraph 변수는 다음 frame Mesh update 가 읽는다(add/consume 규약, 최대 1 frame 지연).
	PrimaryComponentTick.SetTickGroup(TG_PostPhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_PostPhysics);
}

void UHorseMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		Mesh = Owner->GetComponentByClass<USkeletalMeshComponent>();
	}
}

void UHorseMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated || DeltaTime <= 0.0f)
	{
		return;
	}

	// ── 1) 입력 → 목표 속도 스칼라 + 조향각 ──
	FVector Desired;
	ConsumeInputVector(Desired);
	Desired.Z = 0.0f;   // 조향/전진은 XY 평면만.

	float TargetSpeed = std::clamp(Desired.Length(), 0.0f, 1.0f);
	if (bBrakeRequested || MoveMode == EHorseMoveMode::Sliding)
	{
		TargetSpeed = 0.0f;   // 급정지·미끄러짐 중엔 전진 의사 0.
	}

	// 조향각 — 현재 forward 기준 목표 heading 까지 부호 각. 입력이 거의 없으면 직진(0) 유지.
	if (TargetSpeed > 1.e-3f && !Desired.IsNearlyZero())
	{
		FVector Forward = Updated->GetForwardVector();
		Forward.Z = 0.0f;
		FVector Heading = Desired;
		Heading.Z = 0.0f;
		if (!Forward.IsNearlyZero() && !Heading.IsNearlyZero())
		{
			Forward = Forward.Normalized();
			Heading = Heading.Normalized();
			const float Dot   = std::clamp(Forward.X * Heading.X + Forward.Y * Heading.Y, -1.0f, 1.0f);
			const float Cross = Forward.X * Heading.Y - Forward.Y * Heading.X;   // +Z 성분
			SteeringAngle = std::atan2(Cross, Dot) * RAD_TO_DEG;
		}
	}
	else
	{
		SteeringAngle = 0.0f;
	}
	NormalizedSpeed = TargetSpeed;

	// ── 2) root motion 소비(yaw 즉시 적용) + 모드별 위치 처리 ──
	FVector WorldDelta(0.0f, 0.0f, 0.0f);
	ConsumeRootMotion(WorldDelta);

	if (MoveMode == EHorseMoveMode::Grounded)
	{
		TickGrounded(DeltaTime, WorldDelta);
	}
	else if (MoveMode == EHorseMoveMode::Sliding)
	{
		TickSliding(DeltaTime);   // 입력/root motion 무시 — 물리로만.
	}
	else
	{
		TickFalling(DeltaTime);   // 이륙 시점 수평 관성 + 중력. root motion XY 는 공중에서 무시.
	}

	// ── 3) AnimGraph 변수 push ──
	PushAnimGraphVariables();
	bBrakeRequested = false;   // frame 소비 후 클리어(다음 frame Locomotion 이 재요청).
}

void UHorseMovementComponent::ConsumeRootMotion(FVector& OutWorldDelta)
{
	OutWorldDelta = FVector(0.0f, 0.0f, 0.0f);

	USkeletalMeshComponent* MeshComp = Mesh.Get();
	USceneComponent*        Updated  = GetUpdatedComponent();
	if (!MeshComp || !Updated)
	{
		return;
	}
	UAnimInstance* AI = MeshComp->GetAnimInstance();
	if (!AI || !AI->HasPendingRootMotion())
	{
		return;
	}

	// Root motion 성분 분해는 클립 속성 (UAnimSequence: RootRotationLock=YawOnly 면 delta 에
	// yaw 만, bExtractRootMotionZ=false 면 delta Z=0 — 나머지는 pose 가 애니메이션 절대값으로 표현).
	// 말 보행류 클립은 YawOnly + Z 미추출로 세팅되어 있다.
	const FTransform Delta = AI->ConsumeRootMotion();

	// Translate — world frame 전체(XYZ). Z 처리는 모드별 tick 이 결정 (Grounded 는 지면 스냅이
	// step 범위 내에서 최종 Z 를 소유).
	const FQuat   Basis = Updated->GetWorldRotation().ToQuaternion().GetNormalized();
	OutWorldDelta = Basis.RotateVector(Delta.Location);

	// Rotation — 방어적으로 up(+Z)축 yaw(twist)만 적분해 몸통 box 를 항상 세워 둔다
	// (YawOnly 클립이면 delta 가 이미 순수 yaw 라 no-op, Full 클립이 섞여도 box 는 안 기움).
	// actor 가 yaw-only 로 유지되므로 Z-twist 합성은 world/local 곱 순서와 무관(가환).
	// NOTE: Raycast를 사용한 지면과의 정렬 (Suspension) 추가 시에 animation에 의한 rotation과 처리 순서 연구 필요
	FQuat Swing, YawTwist;
	Delta.Rotation.GetNormalized().ToSwingTwist(FVector(0.0f, 0.0f, 1.0f), Swing, YawTwist);
	if (std::fabs(YawTwist.Z) > 1.e-7f)
	{
		Updated->SetWorldRotation((Basis * YawTwist).GetNormalized());
	}
}

void UHorseMovementComponent::TickGrounded(float DeltaTime, const FVector& WorldDelta)
{
	USceneComponent* Updated = GetUpdatedComponent();
	Velocity.Z = 0.0f;

	FVector Loc = Updated->GetWorldLocation();
	// 몸통 box sweep 으로 벽/급경사면 관통·비비기 차단(램프는 통과).
	const FVector DeltaXY = ResolveTorsoCollision(Loc, FVector(WorldDelta.X, WorldDelta.Y, 0.0f));
	Loc.X += DeltaXY.X;
	Loc.Y += DeltaXY.Y;
	// Root motion Z 소비 — 지면 스냅이 step 범위 내 최종 Z 를 소유하므로 보행 bob 은 스냅에
	// 흡수되고, 점프 클립처럼 스냅 범위를 벗어나는 상승만 모드 전환(낙하 판정)으로 이어진다.
	Loc.Z += WorldDelta.Z;

	// 수평 속도 리포팅/관성 — root motion 이 만든 실제 이동에서 역산(이륙 시 momentum 으로 넘어감).
	Velocity.X = DeltaXY.X / DeltaTime;
	Velocity.Y = DeltaXY.Y / DeltaTime;

	// 지면 raycast 는 몸통 box 중심(root)에서 아래로. snap 시 root=지면+StandHeight(발이 지면 접점).
	FHitResult Ground;
	if (TraceGround(Loc, Ground))
	{
		const float TargetZ = Ground.WorldHitLocation.Z + StandHeight;
		if (Loc.Z - TargetZ <= GroundSnapMaxStep)
		{
			Loc.Z = TargetZ;
			Updated->SetWorldLocation(Loc);
			InclineAngle = ComputeInclineAngle(Ground);
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
	AirTime  = 0.0f;   // walk-off 낙하: bJumpActive 는 false 유지(점프 애니 아님).
}

void UHorseMovementComponent::TickFalling(float DeltaTime)
{
	USceneComponent* Updated = GetUpdatedComponent();
	Velocity.Z += GetGravity().Z * DeltaTime;   // 전역 중력(하향, Z<0)
	AirTime    += DeltaTime;

	FVector Loc = Updated->GetWorldLocation();
	Loc += Velocity * DeltaTime;   // XY 관성(이륙 시점) 유지 + Z gravity
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
			bJumpActive = false;
			AirTime     = 0.0f;
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
		AirTime  = 0.0f;
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
		AirTime  = 0.0f;
		return;
	}
	Loc.Z = After.WorldHitLocation.Z + StandHeight;
	Updated->SetWorldLocation(Loc);
	InclineAngle = ComputeInclineAngle(After);

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
	Velocity.Z  = JumpSpeed;
	MoveMode    = EHorseMoveMode::Falling;
	bJumpActive = true;   // 의도적 점프 — 착지까지 bJump 유지(walk-off 와 구분).
	AirTime     = 0.0f;
}

void UHorseMovementComponent::Brake()
{
	// 이 frame bBrake 를 켜고 목표 속도를 0 으로(위 TickComponent 가 소비). 자연 감속과 별개로 급정지 애니 트리거.
	bBrakeRequested = true;
}

float UHorseMovementComponent::ComputeInclineAngle(const FHitResult& Ground) const
{
	// 잠정 구현 — 지면 노멀만 본다. 부호(오르막/내리막)는 forward 와 downhill 방향의 관계로 판정.
	// 정밀한 다리별 지면 접촉은 후속 suspension 작업에서 대체 예정.
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated)
	{
		return 0.0f;
	}
	const FVector N = GroundNormal(Ground);

	const float SlopeAngle = std::acos(std::clamp(N.Z, -1.0f, 1.0f));
	const float MaxAngle   = std::acos(std::clamp(WalkableSlopeZ, 0.0f, 1.0f));
	if (MaxAngle <= 1.e-4f)
	{
		return 0.0f;
	}
	const float Mag = std::clamp(SlopeAngle / MaxAngle, 0.0f, 1.0f);

	// downhill = 지면 노멀의 수평 성분 반대방향. forward 가 그쪽을 향하면 내리막(-), 반대면 오르막(+).
	FVector Downhill(-N.X, -N.Y, 0.0f);
	if (Downhill.IsNearlyZero())
	{
		return 0.0f;   // 평지.
	}
	Downhill = Downhill.Normalized();
	FVector Forward = Updated->GetForwardVector();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return 0.0f;
	}
	Forward = Forward.Normalized();

	const float FacingDownhill = Forward.X * Downhill.X + Forward.Y * Downhill.Y;
	const float Sign = (FacingDownhill > 0.0f) ? -1.0f : 1.0f;   // 내리막 -, 오르막 +
	return Sign * Mag;
}

UAnimGraphInstance* UHorseMovementComponent::GetGraphInstance() const
{
	USkeletalMeshComponent* MeshComp = Mesh.Get();
	if (!MeshComp)
	{
		return nullptr;
	}
	return Cast<UAnimGraphInstance>(MeshComp->GetAnimInstance());
}

void UHorseMovementComponent::PushAnimGraphVariables()
{
	UAnimGraphInstance* Graph = GetGraphInstance();
	if (!Graph)
	{
		return;
	}
	const bool bGrounded = (MoveMode != EHorseMoveMode::Falling);
	Graph->SetGraphVariableFloat(FName("NormalizedSpeed"), NormalizedSpeed);
	Graph->SetGraphVariableFloat(FName("SteeringAngle"),   SteeringAngle);
	Graph->SetGraphVariableFloat(FName("InclineAngle"),    bGrounded ? InclineAngle : 0.0f);
	Graph->SetGraphVariableFloat(FName("AirTime"),         AirTime);
	Graph->SetGraphVariableBool(FName("bBrake"),           bBrakeRequested);
	Graph->SetGraphVariableBool(FName("bJump"),            bJumpActive);
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

	// 벽의 수평 노멀 방향 velocity 성분 제거 → 벽을 따라 미끄러지되 관통·비비기 금지(이륙 관성에 반영).
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
	Ar << GroundSnapMaxStep;
	Ar << StandHeight;
	Ar << WalkableSlopeZ;
	Ar << SlideFriction;
	Ar << SlideGroundProbe;
	Ar << bTorsoCollision;
	Ar << TorsoSkin;
	Ar << JumpSpeed;
}
