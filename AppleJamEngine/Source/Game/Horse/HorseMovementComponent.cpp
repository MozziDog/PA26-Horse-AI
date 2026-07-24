#include "HorseMovementComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/Graph/AnimGraphInstance.h"
#include "Component/SceneComponent.h"
#include "Component/Shape/CapsuleComponent.h"
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

namespace
{
	// 코너 등에서 한 번 밀어내도 다른 벽에 다시 걸릴 수 있어 몇 회 재시도한다. 
	// 프레임 드랍 방지를 위해 최대 반복 횟수 제한, 완전 해소는 보장하지 않음 (약간의 겹침 허용)
	constexpr int MaxDenetrationIter = 4;
	// 겹침 방지용 미세 sweep 거리 (World sweep 은 MaxDist<=0 이면 실패하므로 0 이 아니어야 함)
	constexpr float DepenetrationSweepDist = 0.01f;
	// depenetration push 여유 공간(m)
	// 너무 크면 부드럽게 밀리지 않고 튕겨나옴이 짧은 시간동안 반복 → 떨림 생길 수 있음
	constexpr float DepenetrationMargin = 0.005f;
	// 이 정도 겹침은 허용 (떨림 억제 목적)
	constexpr float DepenetraionAllow = 0.005f;
	// 전진 판단 sweep에 사용할 '앞부분'(전체 forward 반길이 대비)비율. 엉덩이가 벽에 약간 닿았다고 전진 불가 판단 방지
	constexpr float TorsoFrontRatio = 0.6f;
}

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
		Mesh      = Owner->GetComponentByClass<USkeletalMeshComponent>();
		Collision = Owner->GetComponentByClass<UCapsuleComponent>(); // 매번 sweep할 때마다 가져오지 않고 BeginPlay에서 캐싱
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

	// ── 입력 → 목표 속도 스칼라 + 조향각 ──
	FVector Desired;
	ConsumeInputVector(Desired);   // strafe 여부와 무관하게 pending 입력은 소비해 둔다.
	Desired.Z = 0.0f;   // 조향/전진은 XY 평면만.

	if (bStrafeMode)
	{
		// 평행이동(strafe): 선회 없이 종/횡 root motion 으로만 이동. gait/brake/rearing 미적용.
		// 실제 이동은 아래 ConsumeRootMotion 이 strafe 클립 root motion 을 소비해 만든다.
		TurnRate          = 0.0f;
		NormalizedSpeed   = StrafeLongitudinal;   // 종방향(+ 전진), [-1,1]
		LateralSpeed      = StrafeLateral;        // 횡방향(+ 우측), [-1,1]
		bRearingRequested = false;
	}
	else
	{
		LateralSpeed = 0.0f;

		float TargetSpeed = std::clamp(Desired.Length(), 0.0f, 1.0f);
		if (bBrakeRequested || MoveMode == EHorseMoveMode::Sliding)
		{
			TargetSpeed = 0.0f;   // 급정지·미끄러짐 중엔 전진 의사 0.
		}

		// ── Rearing 게이팅 ──
		// Brake() 는 막힌 동안 매 frame 재호출되므로 bBrake rising edge에만 1회 pulse
		const bool bBrakeRising = bBrakeRequested && !bWasBraking;
		bRearingRequested = bBrakeRising
			&& MoveMode == EHorseMoveMode::Grounded
			&& NormalizedSpeed >= RearMinSpeed;
		if (bRearingRequested)
		{
			// Skidding 상태 진입
			// 미끄러지는 처음 속도는 기존 진행하던 속도 그대로 사용
			SkidVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
			bSkidding = true;
			UE_LOG("[HorseMovement] Rearing requested");
		}

		// 조향
		// 목표 진행방향을 yaw rate(deg/s)으로 변환 (anim blend space에 맞추기)
		if (!Desired.IsNearlyZero())
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
	}

	// ── root motion 소비(yaw 즉시 적용) + 모드별 위치 처리 ──
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

	// ── AnimGraph 변수 push ──
	PushAnimGraphVariables();

	// 다음 frame 에지/속도 판정용 상태 갱신 후 brake 소비.
	bWasBraking         = bBrakeRequested;
	bBrakeRequested     = false;   // frame 소비 후 클리어(다음 frame Locomotion 이 재요청).
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

	// 움직임 입력이 있으면 skid 강제 중지
	const float MinMovementThreshold = 0.2f;
	if (bSkidding && NormalizedSpeed > MinMovementThreshold)
	{
		bSkidding    = false;
		SkidVelocity = FVector(0.0f, 0.0f, 0.0f);
	}

	// Rearing 등, skid 상황에는 root motion 대신 관성으로 이동
	// SkidStopSpeed 보다 느려지면 운동마찰력→정지마찰력, 미끄러짐 중지.
	FVector MoveXY;
	if (bSkidding)
	{
		SkidVelocity *= std::clamp(1.0f - SkidFriction * DeltaTime, 0.0f, 1.0f);
		if (SkidVelocity.Length() < SkidStopSpeed)
		{
			SkidVelocity = FVector(0.0f, 0.0f, 0.0f);
			bSkidding    = false;
		}
		MoveXY = FVector(SkidVelocity.X, SkidVelocity.Y, 0.0f) * DeltaTime;
	}
	else
	{
		MoveXY = FVector(WorldDelta.X, WorldDelta.Y, 0.0f);
	}

	FVector Loc = Updated->GetWorldLocation();
	// 전진 판단 — 앞부분 몸통 sweep 으로 벽 관통 차단
	const FVector DeltaXY = ResolveTorsoMove(Loc, MoveXY);
	Loc.X += DeltaXY.X;
	Loc.Y += DeltaXY.Y;
	// NOTE: Root motion Z는 버림.
	// 걷는 중의 Bobbing 등은 루트 모션이 아닌 애니메이션으로 처리하고 점프는 PerformJump()에서 직접 처리

	// 수평 속도 리포팅/관성 — root motion 이 만든 실제 이동에서 역산(이륙 시 momentum 으로 넘어감).
	Velocity.X = DeltaXY.X / DeltaTime;
	Velocity.Y = DeltaXY.Y / DeltaTime;

	// 겹침 해소 — 제자리 회전이나 지형 자체의 움직임 등으로 몸통이 벽에 파고들면 MTD(최소이동거리)로 해소
	// Velocity 에는 반영 X: 벽 밀기는 locomotion 속도가 아니므로 anim/rearing 을 오염시키면 안 됨.
	const FVector Push = DepenetrateTorso(Loc);
	Loc.X += Push.X;
	Loc.Y += Push.Y;

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
				bSkidding      = false;   // 경사 미끄러짐이 관성을 대체.
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
	bSkidding      = false;     // skid 관성은 Velocity 로 넘겨져 ballistic 으로 이어진다.
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
	bSkidding      = false;                       // 이륙 — skid 관성은 Velocity 로 이미 반영됨.
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

void UHorseMovementComponent::SetStrafeInput(bool bEnabled, float Longitudinal, float Lateral)
{
	// Locomotion 이 매 frame 호출. 실제 소비는 TickComponent 내부에서.
	bStrafeMode        = bEnabled;
	StrafeLongitudinal = std::clamp(Longitudinal, -1.0f, 1.0f);
	StrafeLateral      = std::clamp(Lateral, -1.0f, 1.0f);
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
	Graph->SetGraphVariableBool(FName("bStrafeMode"),      bStrafeMode);
	Graph->SetGraphVariableFloat(FName("LateralSpeed"),    LateralSpeed);
	Graph->SetGraphVariableFloat(FName("TurnRate"),   TurnRate);
	Graph->SetGraphVariableFloat(FName("InclineAngle"),    bGrounded ? InclineAngle : 0.0f);
	Graph->SetGraphVariableFloat(FName("AirTime"),         AirTime);
	Graph->SetGraphVariableBool(FName("bBrake"),           bBrakeRequested);
	// bJump 은 점프 애니 진입 pulse — bJumpRequested 만(공중 내내 유지되는 bJumpActive 는 넣지 않는다).
	// 점프 스테이트 exit 가 Automatic Sequence End 라, bJump 을 공중 동안 true 로 물고 있으면 클립이
	// 끝나 자동 exit 된 직후 진입 전환(bJump==true)이 다시 걸려 무한 재진입한다. takeoff(PerformJump)
	// 에서 bJumpRequested 가 꺼지므로 bJump 은 클립 도중 false 로 떨어지고 클립은 1회만 재생된다.
	Graph->SetGraphVariableBool(FName("bJump"),            bJumpRequested);
	// bRearing 은 급정지 진입 에지에서만 1 frame true 인 pulse — bJump 과 동일한 이유로 계속 물고 있으면
	// Rearing 클립 auto-exit 직후 재진입해 무한 반복된다. TickComponent 가 다음 frame 즉시 false 로 내린다.
	Graph->SetGraphVariableBool(FName("bRearing"),         bRearingRequested);
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

FVector UHorseMovementComponent::ResolveTorsoMove(const FVector& FromLoc, const FVector& DeltaXY)
{
	if (!bTorsoCollision || DeltaXY.IsNearlyZero())
	{
		return DeltaXY;
	}
	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	USceneComponent* Updated = GetUpdatedComponent();
	UCapsuleComponent* Capsule = Collision.Get();
	if (!World || !Updated || !Capsule)
	{
		return DeltaXY;   // 콜라이더 없으면 몸통 충돌 skip.
	}

	// Sweep 판정에는 캡슐 콜라이더의 World space 값을 그대로 사용
	// NOTE: actor yaw 는 ConsumeRootMotion 에서 이미 반영된 상태
	const FQuat   RootRot        = Updated->GetWorldRotation().ToQuaternion();
	const float   Radius         = Capsule->GetScaledCapsuleRadius();
	const float   HalfLen        = Capsule->GetScaledCapsuleHalfHeight();
	const FVector ColliderCenter = Capsule->GetWorldLocation();
	const FQuat   ColliderRot	 = Capsule->GetWorldRotation().ToQuaternion();

	// 엉덩이 부분이 전진 판단에 영향 주지 않도록 Torso의 앞부분만 전진 판단에 사용
	FVector Forward = Updated->GetForwardVector();
	Forward.Z = 0.0f;
	Forward = Forward.IsNearlyZero() ? FVector(1.0f, 0.0f, 0.0f) : Forward.Normalized();

	const float           FrontHalf = std::max(Radius, HalfLen * TorsoFrontRatio);
	const FCollisionShape Shape     = FCollisionShape::MakeCapsule(Radius, FrontHalf);
	const FVector         Center    = ColliderCenter + Forward * (HalfLen - FrontHalf);

	FHitResult Hit;
	if (!World->PhysicsSweepByObjectTypes(Center, Center + DeltaXY, ColliderRot, Shape, Hit,
			ObjectTypeBit(ECollisionChannel::WorldStatic), Owner))
	{
		return DeltaXY;   // 막힘 없음
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

FVector UHorseMovementComponent::DepenetrateTorso(const FVector& FromLoc)
{
	FVector Accum(0.0f, 0.0f, 0.0f);
	if (!bTorsoCollision)
	{
		return Accum;
	}
	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	USceneComponent* Updated = GetUpdatedComponent();
	UCapsuleComponent* Capsule = Collision.Get();
	if (!World || !Updated || !Capsule)
	{
		return Accum;   // 콜라이더 없으면 skip.
	}

	// 겹침 판정은 CapsuleComponent의 수치를 그대로 사용 — 에디터 시각화와 동일한 소스 유지
	const FQuat   RootRot = Updated->GetWorldRotation().ToQuaternion();
	const float   Radius  = Capsule->GetScaledCapsuleRadius();
	const float   HalfLen = Capsule->GetScaledCapsuleHalfHeight();
	const FQuat   Rot     = Capsule->GetWorldRotation().ToQuaternion();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfLen);

	// solve 반복 계산
	FVector Center = Capsule->GetWorldLocation();
	for (int Iter = 0; Iter < MaxDenetrationIter; ++Iter)
	{
		FHitResult Hit;
		// 미세 거리 sweep - (거의) 제자리에서 겹침 판정 수행
		const FVector ProbeEnd = Center + FVector(DepenetrationSweepDist, 0.0f, 0.0f);
		if (!World->PhysicsSweepByObjectTypes(Center, ProbeEnd, Rot, Shape, Hit,
				ObjectTypeBit(ECollisionChannel::WorldStatic), Owner))
		{
			break;   // 근처에 아무것도 없음 → 해소할 필요 X
		}
		if (!Hit.bStartPenetrating || Hit.PenetrationDepth <= DepenetraionAllow)
		{
			break;   // sweep 시작 시점에 안겹침 또는 allow 수치보다 얕음 → 해소로 판정
		}

		// 만에 하나 부딪힌 게 등반 가능한 오르막길이었을 경우, 밀어내지 않고 그대로 이동
		const FVector N = GroundNormal(Hit);
		if (N.Z >= WalkableSlopeZ)
		{
			break;
		}
		// 수평 성분만 밀어낸다. 밀어낼 거 없으면 완전 해소된 것으로 보고 반복 종료
		FVector PushN(N.X, N.Y, 0.0f);
		if (PushN.IsNearlyZero())
		{
			break;
		}
		PushN = PushN.Normalized();

		// 침투 깊이 + margin 만큼 밀어내기
		// margin은 최소로 둬야 떨림 생기지 않음!
		const FVector Step = PushN * (Hit.PenetrationDepth + DepenetrationMargin);
		Center += Step;
		Accum  += Step;
	}

	// Z는 지면 스냅 혹은 낙하 판정에서 계산할거니 수평 성분만 반환
	Accum.Z = 0.0f;
	return Accum;
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
	Ar << RearMinSpeed;
	Ar << SkidFriction;
	Ar << SkidStopSpeed;
	Ar << YawAlignTime;
	Ar << MaxTurnRate;
}
