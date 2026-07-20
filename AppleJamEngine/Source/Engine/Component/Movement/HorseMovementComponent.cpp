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

	// 조향
	// 목표 진행방향을 yaw rate(deg/s)으로 변환 (anim blend space에 맞추기)
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
			// 한 변(Forwad)의 길이가 1인 마름모의 넓이 == sin(theta) == Heading의, Forward와 수직인 성분
			const float Cross = Forward.X * Heading.Y - Forward.Y * Heading.X;
			const float HeadingError = std::atan2(Cross, Dot) * RAD_TO_DEG;
			const float AlignTime    = std::max(YawAlignTime, 1.e-3f);
			TurnRate = std::clamp(HeadingError / AlignTime, -MaxTurnRate, MaxTurnRate);
		}
	}
	else
	{
		TurnRate = 0.0f;
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

	// 점프 요청 소비 — 접지 상태인 이 frame 에서만 launch 한다. Jump() 는 async(notify) 로 호출될 수
	// 있어 flag 만 세우고, 실제 물리 전환은 여기서 결정론적으로 처리한다(CMC 의 bWantsJump 패턴).
	if (bWantJump)
	{
		PerformJump();
		return;   // 이 frame 의 XY root motion 은 버린다(1 frame). 다음 tick 부터 TickFalling.
	}

	Velocity.Z = 0.0f;

	FVector Loc = Updated->GetWorldLocation();
	// 몸통 box sweep 으로 벽/급경사면 관통·비비기 차단(램프는 통과).
	const FVector DeltaXY = ResolveTorsoCollision(Loc, FVector(WorldDelta.X, WorldDelta.Y, 0.0f));
	Loc.X += DeltaXY.X;
	Loc.Y += DeltaXY.Y;
	// NOTE: Root motion Z는 버림. 
	// 걷는 중의 Bobbing 등은 루트 모션이 아닌 애니메이션으로 처리하고 점프는 PerformJump()에서 직접 처리

	// 수평 속도 리포팅/관성 — root motion 이 만든 실제 이동에서 역산(이륙 시 momentum 으로 넘어감).
	Velocity.X = DeltaXY.X / DeltaTime;
	Velocity.Y = DeltaXY.Y / DeltaTime;

	// 지면 raycast 는 몸통 box 중심(root)에서 아래로. snap 시 root=지면+StandHeight(발이 지면 접점).
	FHitResult Ground;
	if (!bJumpActive && TraceGround(Loc, Ground))
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
				MoveMode       = EHorseMoveMode::Sliding;
				bJumpRequested = false;   // wind-up 중 미끄러짐 진입 → 점프 요청 취소.
			}
			return;
		}
		// 지면이 step 이상 아래 → 낭떠러지.
	}

	// 지면 없음(또는 너무 아래) → 낙하 시작. XY 는 진행시키되 Z 는 유지하고 mode 전환.
	Updated->SetWorldLocation(Loc);
	MoveMode       = EHorseMoveMode::Falling;
	AirTime        = 0.0f;      // walk-off 낙하: bJumpActive 는 false 유지(점프 애니 아님).
	bJumpRequested = false;     // wind-up 중 발밑 지면이 사라짐 → 점프 요청 취소.
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

void UHorseMovementComponent::StartJump()
{
	if (MoveMode != EHorseMoveMode::Grounded)
	{
		return;   // 공중/미끄러짐 중엔 재점프 불가.
	}
	// 점프 애니 시작만 요청(bJump anim 변수). 물리 이륙은 애니 takeoff 의 NotifyJumpTakeoff() 가 건다.
	// 접지 상태에서 매 frame 호출돼도 idempotent — 이미 요청/공중이면 아래에서 걸러진다.
	bJumpRequested = true;
}

void UHorseMovementComponent::OnJumpNotify()
{
	if (MoveMode != EHorseMoveMode::Grounded)
	{
		return;   // Grounded에서만 점프 가능
	}
	bWantJump = true;
}

void UHorseMovementComponent::PerformJump()
{
	bWantJump      = false;
	bJumpRequested = false;                       // bJump anim pulse 종료 — 클립은 이미 진입, auto-exit 로 1회만 재생.
	Velocity.Z     = JumpSpeed;                   // 상향 임펄스. 없으면 즉시 착지 판정되어 지면을 못 벗어난다.
	MoveMode       = EHorseMoveMode::Falling;      // 점프 즉시 Falling 전환.
	bJumpActive    = true;                         // 의도적 점프 표식 — 착지까지 유지(walk-off 낙하와 구분·착지 리셋용). anim 은 구동 안 함.
	AirTime        = 0.0f;

	// 접지 frame 에서 곧바로 낙하 tick 을 돌리면 root box 가 아직 지면 접점에 있어 착지로 오인될 수
	// 있다. TickFalling 이 상승 중(Velocity.Z>0) 지면 체크를 건너뛰긴 하지만, 방어적으로 살짝 띄운다.
	if (USceneComponent* Updated = GetUpdatedComponent())
	{
		const float Lift = std::clamp(GroundSnapMaxStep * 0.1f, 0.02f, 0.05f);
		Updated->SetWorldLocation(Updated->GetWorldLocation() + FVector(0.0f, 0.0f, Lift));
	}
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
	Graph->SetGraphVariableFloat(FName("TurnRate"),   TurnRate);
	Graph->SetGraphVariableFloat(FName("InclineAngle"),    bGrounded ? InclineAngle : 0.0f);
	Graph->SetGraphVariableFloat(FName("AirTime"),         AirTime);
	Graph->SetGraphVariableBool(FName("bBrake"),           bBrakeRequested);
	// bJump 은 점프 애니 진입 pulse — bJumpRequested 만(공중 내내 유지되는 bJumpActive 는 넣지 않는다).
	// 점프 스테이트 exit 가 Automatic Sequence End 라, bJump 을 공중 동안 true 로 물고 있으면 클립이
	// 끝나 자동 exit 된 직후 진입 전환(bJump==true)이 다시 걸려 무한 재진입한다. takeoff(PerformJump)
	// 에서 bJumpRequested 가 꺼지므로 bJump 은 클립 도중 false 로 떨어지고 클립은 1회만 재생된다.
	Graph->SetGraphVariableBool(FName("bJump"),            bJumpRequested);
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
	Ar << YawAlignTime;
	Ar << MaxTurnRate;
}
