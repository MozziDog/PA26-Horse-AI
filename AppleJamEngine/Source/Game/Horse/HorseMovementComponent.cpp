#include "HorseMovementComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/Graph/AnimGraphInstance.h"
#include "Component/SceneComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/TickFunction.h"
#include "Core/Types/CollisionTypes.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "HorseRagdollTestComponent.h"
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
	// 겹침 방지용 미세 sweep 거리 (RotatedWorld sweep 은 MaxDist<=0 이면 실패하므로 0 이 아니어야 함)
	constexpr float DepenetrationSweepDist = 0.01f;
	// depenetration push 여유 공간(m)
	// 너무 크면 부드럽게 밀리지 않고 튕겨나옴이 짧은 시간동안 반복 → 떨림 생길 수 있음
	constexpr float DepenetrationMargin = 0.005f;
	// 이 정도 겹침은 허용 (떨림 억제 목적)
	constexpr float DepenetraionAllow = 0.005f;
	// 전진 판단 sweep에 사용할 '앞부분'(전체 forward 반길이 대비)비율. 엉덩이가 벽에 약간 닿았다고 전진 불가 판단 방지
	constexpr float TorsoFrontRatio = 0.6f;
	// 지면 판정 시작 높이에 더하는 추가 여유분
	// 허용 step up 보다 위에서 쏴야 벽이나 높은 단차 등 '오를 수 없는 지형에 막힘'을 낭떠러지로 오판하지 않음
	// + 빠른 낙하에서 발이 지면을 지나쳐 버리는 tunneling 완화 역할도 겸해 조금 크게 설정
	constexpr float GroundProbeExtraUpMargin = 0.5f;
	// 접지 sphere 는 overlap query 가 없어 '아주 짧은 sweep 의 초기 침투'로 대신한다.
	// sweep 은 MaxDist<=0 이면 실패하므로 0 이 아니어야 하고, 이 거리 안의 hit 은 곧 접촉으로 본다.
	constexpr float SupportProbeSweepDist = 0.02f;
	// 급경사에서 '이동했다' 로 볼 최소 프레임 이동거리(m). idle 클립의 미세한 root motion 이
	// 제자리 미끄러짐을 유발하지 않도록 0 이 아닌 값을 쓴다. 60fps 기준 약 0.06 m/s.
	constexpr float SteepSlideMinMoveDist = 1.e-3f;

	const FQuat IdentityQuat(0.0f, 0.0f, 0.0f, 1.0f);

	// 지수 감쇠 보간 계수 — frame rate 에 무관하게 "Speed(1/s) 로 수렴" 을 보장한다.
	float DampAlpha(float DeltaTime, float Speed)
	{
		if (DeltaTime <= 0.0f || Speed <= 0.0f)
		{
			return 1.0f;
		}
		return std::clamp(1.0f - std::exp(-Speed * DeltaTime), 0.0f, 1.0f);
	}

	// +Z 를 Up 으로 보내는 최소 회전(swing). twist(yaw) 성분이 생기지 않아 몸통 방향을 오염시키지 않는다.
	// Up 은 정규화되어 있고 Up.Z > 0 이어야 한다(뒤집힘은 호출 전에 걸러진다).
	FQuat MakeUpAlignQuat(const FVector& Up)
	{
		// Axis = (0,0,1) x Up
		const FVector Axis(-Up.Y, Up.X, 0.0f);
		if (Axis.IsNearlyZero())
		{
			return IdentityQuat;
		}
		return FQuat(Axis.X, Axis.Y, Axis.Z, 1.0f + Up.Z).GetNormalized();
	}
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

	// 지면 정렬은 mesh 의 relative transform 을 덮어쓰므로, 기울기 0 일 때의 원본을 여기서 잡아둔다.
	BodyTilt          = IdentityQuat;
	BodyHeightZ       = 0.0f;
	LastAppliedTilt   = IdentityQuat;
	LastAppliedHeight = 0.0f;
	bTiltApplied      = false;
	bMeshBaseCached = false;
	if (USkeletalMeshComponent* MeshComp = Mesh.Get())
	{
		MeshBaseRotation = MeshComp->GetRelativeQuat();
		MeshBaseLocation = MeshComp->GetRelativeLocation();
		bMeshBaseCached  = true;
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

	// 끼임 탈출 타이머는 모드와 무관하게 흐른다(탈출 도중 지면을 다시 잡아 Grounded 로 돌아오기 때문).
	UpdateStuckEscape(DeltaTime);

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

		// 끼임 판정용 '마지막 조작 방향' — strafe 는 AddInputVector 를 타지 않으므로 여기서 만든다.
		FVector Forward = Updated->GetForwardVector();
		FVector Right   = Updated->GetRightVector();
		Forward.Z = 0.0f;
		Right.Z   = 0.0f;
		const FVector StrafeDir = Forward * StrafeLongitudinal + Right * StrafeLateral;
		if (!StrafeDir.IsNearlyZero())
		{
			LastInputDirXY = StrafeDir.Normalized();
		}
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
				LastInputDirXY = Heading;   // 끼임 판정 기준 방향
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

	// ── 지면 정렬 결과를 mesh 에 반영 ──
	// BodyTilt는 이미 앞서 EHorseMoveMode별 tick에서 계산 완료된 것으로 봄
	ApplyBodyTiltToMesh();

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

	const FVector PivotToMesh = MeshComp->GetWorldLocation() - Updated->GetWorldLocation();

	// Rotation — 방어적으로 up(+Z)축 yaw(twist)만 적분해 몸통 box 를 항상 세워 둔다
	// (YawOnly 클립이면 delta 가 이미 순수 yaw 라 no-op, Full 클립이 섞여도 box 는 안 기움).
	// NOTE: 지면 정렬(suspension)은 root 가 아니라 mesh 의 relative rotation 에만 얹으므로
	//       (UpdateBodyTilt / ApplyBodyTiltToMesh) 여기의 yaw 적분과 축이 겹치지 않는다.
	//       순서상으로도 root yaw 가 먼저 확정된 뒤 그 로컬 프레임에서 기울기를 계산한다.
	FQuat Swing, YawTwist;
	Delta.Rotation.GetNormalized().ToSwingTwist(FVector(0.0f, 0.0f, 1.0f), Swing, YawTwist);
	if (std::fabs(YawTwist.Z) > 1.e-7f)
	{
		const FQuat NewBasis = (Basis * YawTwist).GetNormalized();
		Updated->SetWorldRotation(NewBasis);

		// 몸통 기울기(pitch/roll) 로직이 추가될 것을 고려해서 단순 yaw 차이로만 계산하지 않고 (NewBasis * Basis⁻¹)로 계산
		const FQuat WorldRotDelta = (NewBasis * Basis.Inverse()).GetNormalized();
		// actor pivot과 mesh pivot의 거리 + 회전으로 인해 발생한 호(arc) 형태의 오차 보정
		OutWorldDelta -= WorldRotDelta.RotateVector(PivotToMesh) - PivotToMesh;
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

	// 급경사 위에서는 '가만히 서 있는 동안' 은 버티고, 실제로 이동이 발생하는 frame 에 미끄러짐으로
	// 이행한다(원안: "다음 이동시에 비탈면 미끄러짐으로 이행"). bSteepGround 는 직전 frame 표본에서 왔다.
	if (bSteepGround && MoveXY.Length() > SteepSlideMinMoveDist)
	{
		MoveMode       = EHorseMoveMode::Sliding;
		bJumpRequested = false;   // wind-up 중 미끄러짐 진입 → 점프 요청 취소.
		bSkidding      = false;   // 경사 미끄러짐이 관성을 대체.
		bSteepGround   = false;
		return;
	}

	// 앞발이 허공이면(직전 frame 표본) 전방으로 조금씩 밀어 결국 무게중심까지 넘겨 떨어뜨린다.
	// 조작으로 물러설 여지를 남기려고 root motion 을 덮어쓰지 않고 더하기만 한다 —
	// 뒤로 걷는 속도가 EdgeSlipSpeed 보다 빠르면 그대로 빠져나올 수 있다.
	if (bEdgeSlipping && EdgeSlipSpeed > 0.0f)
	{
		FVector SlipDir = Updated->GetForwardVector();
		SlipDir.Z = 0.0f;
		if (!SlipDir.IsNearlyZero())
		{
			MoveXY += SlipDir.Normalized() * (EdgeSlipSpeed * DeltaTime);
		}
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

	// ── 접지 샘플링 ──
	// sphere overlap(무게중심 아래) + 앞다리 2점 + 무게중심 수직 raycast.
	FHorseGroundSample Sample;
	const bool bSupported = !bJumpActive && SampleGround(Loc, Sample);
	if (bDrawGroundProbes)
	{
		DrawGroundProbeDebug(Loc, Sample);
	}

	if (bSupported)
	{
		float TargetZ = Loc.Z;
		bool  bWithinStep = true;
		if (Sample.bHasSupportZ)
		{
			// 발높이보다 GroundSnapMaxUp 이상 높은 곳은 밟을 지면이 아니라 벽으로 판정
			// 이 경우 스냅 없이 현재 Z 유지
			const float Rise = Sample.SupportZ - (Loc.Z - StandHeight);
			if (Rise <= GroundSnapMaxUp)
			{
				TargetZ = Sample.SupportZ + StandHeight;
			}
			bWithinStep = (Loc.Z - TargetZ) <= GroundSnapMaxDown;
		}
		// SupportZ 가 없는 경우 = sphere 는 지면을 잡았는데 ray 는 전부 틈으로 빠진 상황.
		// 높이를 새로 정할 근거가 없으므로 현재 높이를 유지한 채 접지만 인정한다(좁은 틈 위 보행).

		if (bWithinStep)
		{
			Loc.Z = TargetZ;
			Updated->SetWorldLocation(Loc);

			// 경사 — 표본 평면의 종방향 기울기. 급격한 변화는 이징해서 anim 이 튀지 않게 한다.
			const float TargetIncline = ComputeInclineAngle(Sample);
			InclineAngle += (TargetIncline - InclineAngle) * DampAlpha(DeltaTime, BodyAlignSpeed);

			UpdateBodyTilt(DeltaTime, Sample, true);

			// 보행 불가 경사면 표식만 남긴다. 실제 Sliding 이행은 '다음 이동' frame 에서(위쪽 가드).
			// 판정 기준은 추정 평면이 아니라 '실제로 밟고 있는 면' 의 노멀 — 단차 앞에서 평면 기울기가
			// 아무리 커도, 발밑이 평평하면 미끄러지지 않고 그냥 몸만 기울어야 한다.
			bSteepGround = (Sample.CenterNormal.Z < WalkableSlopeZ);
			// 같은 규약으로 실족 표식도 남긴다 — 밀기는 다음 frame 이동에 얹힌다.
			bEdgeSlipping = !Sample.bFrontHit;
			return;
		}
		// 지면이 step 이상 아래 → 낭떠러지.
	}

	// 지면 없음(또는 너무 아래) → 물리 기반 이동. XY 는 진행시키되 Z 는 유지하고 mode 전환.
	Updated->SetWorldLocation(Loc);
	UpdateBodyTilt(DeltaTime, Sample, false);
	EnterFalling(false);
}

void UHorseMovementComponent::TickFalling(float DeltaTime)
{
	USceneComponent* Updated = GetUpdatedComponent();
	Velocity.Z += GetGravity().Z * DeltaTime;   // 전역 중력(하향, Z<0)
	AirTime    += DeltaTime;

	FVector Loc = Updated->GetWorldLocation();
	Loc += Velocity * DeltaTime;   // XY 관성(이륙 시점) 유지 + Z gravity
	Updated->SetWorldLocation(Loc);

	// 공중에서는 몸통을 서서히 수평으로 되돌린다(착지 준비 자세).
	UpdateBodyTilt(DeltaTime, FHorseGroundSample(), false);

	// 고속 강제 낙마 / 끼임 판정 — 접지를 잃은 상태에서만 의미가 있다.
	EvaluateDismountRules(DeltaTime);

	if (Velocity.Z > 0.0f)
	{
		return;   // 상승 중엔 착지 체크 skip.
	}

	// 착지 판정은 TickGrounded 의 접지 유지 판정과 반드시 같은 기준(SampleGround)이어야 한다.
	// 중심 ray 하나로 착지를 인정하면 무게중심 sphere 와 모양·위치·탐색범위가 달라서, 둘의 결과가
	// 엇갈리는 자리에서 Grounded/Falling 이 매 frame 뒤집힌다.
	// 탐침 범위(ProbeUp/Down)가 발 평면 위아래를 넉넉히 덮으므로 한 frame 에 발이 지면을
	// 지나쳐 버려도 잡힌다.
	FHorseGroundSample Sample;
	const bool bSupported = SampleGround(Loc, Sample);
	if (bDrawGroundProbes)
	{
		DrawGroundProbeDebug(Loc, Sample);
	}
	if (!bSupported || !Sample.bHasSupportZ)
	{
		return;
	}

	const float TargetZ = Sample.SupportZ + StandHeight;
	if (Loc.Z > TargetZ)
	{
		return;   // 아직 지면 위 — 계속 낙하.
	}

	// 발이 지면에 도달/관통 → 착지.
	Loc.Z = TargetZ;
	Updated->SetWorldLocation(Loc);
	Velocity.Z  = 0.0f;
	bJumpActive = false;
	AirTime     = 0.0f;
	StuckTime   = 0.0f;
	bCollapseRequested = false;
	// 급경사면에 착지하면 곧바로 미끄러짐. 기준은 추정 선이 아니라 실제로 밟은 면의 노멀 —
	// TickGrounded 의 bSteepGround 와 같아야 착지 직후 판정이 엇갈리지 않는다.
	MoveMode = (Sample.CenterNormal.Z < WalkableSlopeZ)
		? EHorseMoveMode::Sliding
		: EHorseMoveMode::Grounded;
	bSteepGround  = false;   // 착지 판정에서 이미 결론이 났다.
	bEdgeSlipping = !Sample.bFrontHit;   // 낭떠러지 턱에 착지했으면 곧바로 실족 밀기로 이어진다.
}

void UHorseMovementComponent::TickSliding(float DeltaTime)
{
	USceneComponent* Updated = GetUpdatedComponent();

	FVector Loc = Updated->GetWorldLocation();

	// 지면 조사(급강하 추적 위해 아래로 길게 — 발밑에서 SlideGroundProbe 만큼).
	FHitResult Ground;
	if (!TraceGround(Loc, Ground))
	{
		UpdateBodyTilt(DeltaTime, FHorseGroundSample(), false);
		EnterFalling(false);   // 지면 사라짐 → 낙하.
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
	if (!TraceGround(Loc, After))
	{
		Updated->SetWorldLocation(Loc);
		UpdateBodyTilt(DeltaTime, FHorseGroundSample(), false);
		EnterFalling(false);   // 가장자리 넘어감 → 낙하.
		return;
	}
	Loc.Z = After.WorldHitLocation.Z + StandHeight;
	Updated->SetWorldLocation(Loc);

	const float TargetIncline = ComputeInclineAngle(After);
	InclineAngle += (TargetIncline - InclineAngle) * DampAlpha(DeltaTime, BodyAlignSpeed);

	// 미끄러지는 중에도 몸통은 경사면을 따라 눕는다 — 표본이 없으니 지면 노멀 하나로 만든다.
	{
		FHorseGroundSample SlideSample;
		const FVector NAfterLocal = LocalizeGroundNormal(GroundNormal(After));
		SlideSample.LocalUp     = NAfterLocal;
		SlideSample.LocalTiltUp = ClampTiltUp(NAfterLocal);
		UpdateBodyTilt(DeltaTime, SlideSample, true);
	}

	// 속도를 지면 접선으로 투영(수직 성분 제거) — 표면을 따라 미끄러지게 유지.
	const FVector NAfter = GroundNormal(After);
	Velocity = Velocity - NAfter * Velocity.Dot(NAfter);

	// 완경사/평지 도달 → 보행 복귀.
	if (NAfter.Z >= WalkableSlopeZ)
	{
		Velocity.Z = 0.0f;
		MoveMode = EHorseMoveMode::Grounded;
		bSteepGround = false;
	}
}

// ============================================================================
// 접지 표본
// ============================================================================

bool UHorseMovementComponent::ProbeCenterOfMass(const FVector& PivotLoc, FHitResult& OutHit) const
{
	OutHit = FHitResult();

	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!World || !Updated)
	{
		return false;
	}

	FVector Forward = Updated->GetForwardVector();
	Forward.Z = 0.0f;
	Forward = Forward.IsNearlyZero() ? FVector(1.0f, 0.0f, 0.0f) : Forward.Normalized();

	const float Radius = std::max(SupportSphereRadius, 0.01f);
	const float Up     = std::max(ProbeUp, 0.0f);
	const float Down   = std::max(ProbeDown, 0.0f);
	if (Up + Down <= 1.e-4f)
	{
		return false;
	}

	// sphere '중심' 을 훑지만 접지 판정 기준은 sphere 의 바닥이라 시작/끝 높이에 Radius 를 더한다.
	// 즉 접점이 [FootZ - ProbeDown, FootZ + ProbeUp] 안에 있으면 지면으로 본다 — 앞다리 ray 와 같은 범위.
	const float   FootZ  = PivotLoc.Z - StandHeight;
	const FVector XY     = FVector(PivotLoc.X, PivotLoc.Y, 0.0f) + Forward * SupportSphereForward;
	const FVector Start(XY.X, XY.Y, FootZ + Up + Radius);
	const FVector End  (XY.X, XY.Y, FootZ - Down + Radius);

	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
	if (!World->PhysicsSweepByObjectTypes(Start, End, IdentityQuat, Shape, OutHit,
			ObjectTypeBit(ECollisionChannel::WorldStatic), Owner))
	{
		return false;
	}

	// 시작부터 지형에 박혀 있으면 접점 좌표를 믿을 수 없다 → 발 높이를 접점으로 간주한다.
	if (OutHit.bStartPenetrating)
	{
		OutHit.WorldHitLocation = FVector(XY.X, XY.Y, FootZ);
		if (OutHit.ImpactNormal.IsNearlyZero())
		{
			OutHit.ImpactNormal = FVector(0.0f, 0.0f, 1.0f);
		}
	}
	return true;
}

FVector UHorseMovementComponent::LocalizeGroundNormal(const FVector& WorldNormal) const
{
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated)
	{
		return FVector(0.0f, 0.0f, 1.0f);
	}
	FVector Forward = Updated->GetForwardVector();
	FVector Right   = Updated->GetRightVector();
	Forward.Z = 0.0f;
	Right.Z   = 0.0f;
	Forward = Forward.IsNearlyZero() ? FVector(1.0f, 0.0f, 0.0f) : Forward.Normalized();
	Right   = Right.IsNearlyZero()   ? FVector(0.0f, 1.0f, 0.0f) : Right.Normalized();

	// root 는 yaw 만 갖고 있으므로 로컬 Z 는 world Z 와 같다.
	FVector Local(WorldNormal.Dot(Forward), WorldNormal.Dot(Right), WorldNormal.Z);
	return Local.IsNearlyZero() ? FVector(0.0f, 0.0f, 1.0f) : Local.Normalized();
}

FVector UHorseMovementComponent::ClampTiltUp(const FVector& LocalUp) const
{
	if (LocalUp.Z <= 1.e-3f)
	{
		return FVector(0.0f, 0.0f, 1.0f);   // 거의 수직/뒤집힌 면은 몸통 기울기 대상이 아니다.
	}
	// 노멀을 기울기(slope) 로 되돌려 각도 한계로 자른 뒤 다시 노멀로.
	// roll 성분(Y)은 여기서 통째로 버린다 — 몸통은 항상 위를 보고 pitch 로만 지면을 따라간다.
	// SampleGround 는 애초에 roll 을 만들지 않지만, 지면 노멀을 그대로 넘기는 경로(Sliding)도
	// 이 함수를 지나므로 여기서 잘라야 모드가 바뀔 때 몸통이 튀지 않는다.
	const float MaxPitchSlope = std::tan(std::clamp(MaxBodyPitch, 0.0f, 80.0f) * DEG_TO_RAD);
	const float Pitch = std::clamp(-LocalUp.X / LocalUp.Z, -MaxPitchSlope, MaxPitchSlope);
	return FVector(-Pitch, 0.0f, 1.0f).Normalized();
}

bool UHorseMovementComponent::SampleGround(const FVector& PivotLoc, FHorseGroundSample& OutSample) const
{
	OutSample = FHorseGroundSample();

	const USceneComponent* Updated = GetUpdatedComponent();
	if (!Updated)
	{
		return false;
	}

	FVector Forward = Updated->GetForwardVector();
	FVector Right   = Updated->GetRightVector();
	Forward.Z = 0.0f;
	Right.Z   = 0.0f;
	Forward = Forward.IsNearlyZero() ? FVector(1.0f, 0.0f, 0.0f) : Forward.Normalized();
	Right   = Right.IsNearlyZero()   ? FVector(0.0f, 1.0f, 0.0f) : Right.Normalized();

	const float FootZ = PivotLoc.Z - StandHeight;

	// (1) 무게중심 아래 sphere sweep — 접지 판정 전담. 기울기는 아래 앞뒤 probe 가 만든다.
	FHitResult CenterHit;
	OutSample.bCenterHit = ProbeCenterOfMass(PivotLoc, CenterHit);
	if (OutSample.bCenterHit)
	{
		OutSample.CenterPoint  = CenterHit.WorldHitLocation;
		OutSample.CenterNormal = GroundNormal(CenterHit);
	}

	// (2) 앞발/뒷발 중앙 raycast — 몸통 기울기의 앞뒤 표본.
	// 좌우로 벌려 쏘지 않는 건 의도된 것이다. 말은 좌우 발 간격이 좁아서 좌/우 접점으로 roll 을
	// 만들면 단차를 비스듬히 내려올 때 한쪽 발이 먼저 떨어지며 몸이 크게 요동친다.
	// 중앙 1개로 앞뒤만 재고 roll 은 포기한다.
	const FVector FrontXY = PivotLoc + Forward * FrontProbeForward;
	const FVector RearXY  = PivotLoc - Forward * RearFootBack;

	FHitResult FrontHit;
	FHitResult RearHit;
	OutSample.bFrontHit = TraceGroundAt(FrontXY, FootZ, ProbeUp, ProbeDown, FrontHit);
	OutSample.bRearHit  = TraceGroundAt(RearXY,  FootZ, ProbeUp, ProbeDown, RearHit);
	if (OutSample.bFrontHit)
	{
		OutSample.FrontPoint = FrontHit.WorldHitLocation;
	}
	if (OutSample.bRearHit)
	{
		OutSample.RearPoint = RearHit.WorldHitLocation;
	}

	// ── 앞뒤 접점을 잇는 선 ──
	// 각 접점을 actor 로컬(전방, 우측, 높이) 로 옮긴다.
	// (probe XY 를 그대로 쓰지 않고 실제 접점 XY 를 쓴다 — sphere 는 접점이 옆으로 밀릴 수 있다)
	auto ToLocal = [&](const FVector& World)
	{
		const FVector D = World - PivotLoc;
		return FVector(D.Dot(Forward), D.Dot(Right), World.Z - FootZ);
	};

	// 기울기는 '가장 앞' 과 '가장 뒤' 접점으로 잰다. 셋 다 있으면 자연히 앞발~뒷발이 뽑히고,
	// 한쪽이 빠지면 남은 것 중 가장 멀리 떨어진 쌍으로 degrade 된다(단차에 걸친 상황).
	// 무게중심을 후보에 넣는 건 앞/뒤가 둘 다 비었을 때의 대비책이다 — 기울기 기준은 어디까지나 앞뒤 발.
	const FVector Candidates[3] = { OutSample.RearPoint, OutSample.CenterPoint, OutSample.FrontPoint };
	const bool    CandidateHit[3] = { OutSample.bRearHit, OutSample.bCenterHit, OutSample.bFrontHit };

	bool    bHasAnchor = false;
	FVector Rearmost(0.0f, 0.0f, 0.0f);
	FVector Frontmost(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < 3; ++i)
	{
		if (!CandidateHit[i])
		{
			continue;
		}
		const FVector L = ToLocal(Candidates[i]);
		if (!bHasAnchor)
		{
			Rearmost   = L;
			Frontmost  = L;
			bHasAnchor = true;
			continue;
		}
		Rearmost  = (L.X < Rearmost.X)  ? L : Rearmost;
		Frontmost = (L.X > Frontmost.X) ? L : Frontmost;
	}

	if (bHasAnchor)
	{
		const float Base = Frontmost.X - Rearmost.X;
		if (std::fabs(Base) > 1.e-3f)
		{
			OutSample.PitchSlope = (Frontmost.Z - Rearmost.Z) / Base;
		}
		else if (OutSample.bCenterHit)
		{
			// 표본이 사실상 한 점뿐 — 밟고 있는 면 노멀로 근사.
			const FVector N = LocalizeGroundNormal(OutSample.CenterNormal);
			OutSample.PitchSlope = (N.Z > 1.e-3f) ? (-N.X / N.Z) : 0.0f;
		}
		OutSample.LocalUp = FVector(-OutSample.PitchSlope, 0.0f, 1.0f).Normalized();
	}
	OutSample.LocalTiltUp = ClampTiltUp(OutSample.LocalUp);

	// ── Z 스냅 높이 ──
	// 회전 pivot 의 XY 위치에서의 선 높이. 회전 중심과 높이 기준이 같은 점이어야
	// 기울기가 바뀔 때 발이 지면을 뚫거나 뜨지 않는다.
	// TiltPivotForward 가 앞발과 뒷발 사이에 있으면 외삽이 아니라 내삽이라, 단차에 걸친 자세에서도
	// 뒷발이 지면에서 크게 뜨지 않는다(앞발+무게중심 기준일 때의 문제).
	if (bHasAnchor)
	{
		OutSample.SupportZ     = (Frontmost.Z + FootZ) + OutSample.PitchSlope * (TiltPivotForward - Frontmost.X);
		OutSample.bHasSupportZ = true;
	}

	// ── 접지 유지 판정 ──
	// 무게중심 sphere 하나로만 판단한다. 앞발이 허공인 건 접지 상실이 아니라 '실족' 이고,
	// 호출자가 bFrontHit 를 보고 EdgeSlipSpeed 로 앞으로 밀어 무게중심까지 넘기는 방식으로 처리한다.
	// 앞발 miss 를 여기서 접지 상실로 처리하면, 착지 판정은 여전히 무게중심을 잡으므로
	// Grounded/Falling 이 매 frame 뒤집힌다.
	return OutSample.bCenterHit;
}

// ============================================================================
// 몸통 기울기(지면 정렬)
// ============================================================================

void UHorseMovementComponent::UpdateBodyTilt(float DeltaTime, const FHorseGroundSample& Sample, bool bGrounded)
{
	if (!bAlignBodyToGround)
	{
		BodyTilt = IdentityQuat;
		return;
	}

	FQuat Target = IdentityQuat;
	if (bGrounded)
	{
		FVector Up = Sample.LocalTiltUp;
		Up = Up.IsNearlyZero() ? FVector(0.0f, 0.0f, 1.0f) : Up.Normalized();
		if (Up.Z > 1.e-3f)
		{
			Target = MakeUpAlignQuat(Up);
		}
	}

	const float Alpha = DampAlpha(DeltaTime, bGrounded ? BodyAlignSpeed : AirLevelSpeed);
	BodyTilt = FQuat::Slerp(BodyTilt, Target, Alpha).GetNormalized();
}

FVector UHorseMovementComponent::GetTiltPivotLocal() const
{
	// 회전 중심은 '접지면 중앙, 발 높이'. root 로컬에서 발 평면은 z = -StandHeight 다.
	// 여기가 지면 높이와 어긋나면 기울기가 변할 때 발이 수평으로 쓸린다(미끄러짐).
	return FVector(TiltPivotForward, 0.0f, -StandHeight + TiltPivotHeight);
}

void UHorseMovementComponent::GetBodyTiltSlopes(float& OutPitchSlope, float& OutRollSlope) const
{
	const FVector Up = BodyTilt.GetUpVector();   // root 로컬
	const float   UpZ = std::max(Up.Z, 1.e-3f);
	OutPitchSlope = -Up.X / UpZ;
	OutRollSlope  = -Up.Y / UpZ;
}

void UHorseMovementComponent::ApplyBodyTiltToMesh()
{
	USkeletalMeshComponent* MeshComp = Mesh.Get();
	if (!MeshComp)
	{
		return;
	}
	if (!bMeshBaseCached)
	{
		MeshBaseRotation = MeshComp->GetRelativeQuat();
		MeshBaseLocation = MeshComp->GetRelativeLocation();
		bMeshBaseCached  = true;
	}

	// 기울기가 사실상 그대로면 건드리지 않는다 — relative transform write 는 octree/행렬 dirty 를 유발한다.
	if (bTiltApplied && BodyTilt.Equals(LastAppliedTilt, 1.e-5f)
		&& std::fabs(BodyHeightZ - LastAppliedHeight) < 1.e-4f)
	{
		return;
	}

	// BodyTilt 는 root 로컬(yaw 만 반영된 프레임) 기준이라 mesh 의 relative 에 그대로 얹으면 된다.
	// 회전 중심은 '네 발 접지면의 중앙 + 발 높이' — 지면 높이에 놓아야 기울기가 변할 때 발이 안 쓸린다.
	const FVector Pivot = GetTiltPivotLocal();
	FVector NewLocation = Pivot + BodyTilt.RotateVector(MeshBaseLocation - Pivot);
	// 2차 패스가 구한 높이 보정은 mesh 에만 얹는다 — actor 를 올리면 다음 frame probe 위치가 바뀌어
	// 회전 solve 에 피드백이 걸린다. 캡슐/충돌은 1차 스냅 높이 그대로 둔다.
	NewLocation.Z += BodyHeightZ;

	MeshComp->SetRelativeRotation((BodyTilt * MeshBaseRotation).GetNormalized());
	MeshComp->SetRelativeLocation(NewLocation);
	LastAppliedTilt   = BodyTilt;
	LastAppliedHeight = BodyHeightZ;
	bTiltApplied      = true;
}

// ============================================================================
// 물리 기반 이동 — 강제 낙마 / 끼임 탈출
// ============================================================================

void UHorseMovementComponent::EnterFalling(bool bFromJump)
{
	MoveMode      = EHorseMoveMode::Falling;
	AirTime       = 0.0f;
	StuckTime     = 0.0f;
	bSteepGround  = false;
	bEdgeSlipping = false;   // 이미 떨어졌으니 실족 밀기는 끝.
	bSkidding     = false;   // skid 관성은 Velocity 로 넘겨져 ballistic 으로 이어진다.
	bCollapseRequested = false;
	if (!bFromJump)
	{
		bJumpRequested = false;   // wind-up 중 발밑 지면이 사라짐 → 점프 요청 취소.
	}
}

void UHorseMovementComponent::EvaluateDismountRules(float DeltaTime)
{
	// 의도한 점프는 '사고' 가 아니다 — 기본적으로 낙마 판정에서 제외한다.
	if (bJumpActive && !bDismountDuringJump)
	{
		StuckTime = 0.0f;
		return;
	}
	// 작은 턱을 넘거나 잠깐 접지를 놓치는 정도로는 판정하지 않는다.
	if (AirTime < PhysicsGraceTime)
	{
		return;
	}

	const float Speed = Velocity.Length();

	// (1) 고속 — 강제 낙마 + 래그돌.
	if (ForcedDismountSpeed > 0.0f && Speed >= ForcedDismountSpeed)
	{
		HandleCollapse();
		return;
	}

	// (2) 저속 — 마지막 조작 방향으로 전혀 나아가지 못하면 끼인 것으로 본다.
	if (bEscapingStuck)
	{
		return;
	}
	FVector Reference = LastInputDirXY;
	if (Reference.IsNearlyZero())
	{
		// 조작 이력이 없으면 몸이 향한 방향을 기준으로 삼는다.
		if (const USceneComponent* Updated = GetUpdatedComponent())
		{
			FVector Forward = Updated->GetForwardVector();
			Forward.Z = 0.0f;
			Reference = Forward.IsNearlyZero() ? FVector(0.0f, 0.0f, 0.0f) : Forward.Normalized();
		}
	}
	const float Along = Velocity.X * Reference.X + Velocity.Y * Reference.Y;

	// StuckMaxSpeed 가드는 원안에 없는 추가분 — 없으면 '수직 자유낙하' 가 (수평속도 0 이라)
	// 그대로 끼임으로 오판된다.
	const bool bWedged = (Speed <= StuckMaxSpeed) && (Along <= StuckAlongSpeed);
	if (bWedged)
	{
		StuckTime += DeltaTime;
		if (StuckTime >= StuckDetectTime)
		{
			BeginStuckEscape();
		}
	}
	else
	{
		StuckTime = 0.0f;
	}
}

bool UHorseMovementComponent::TryTriggerRagdoll()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	UHorseRagdollTestComponent* Ragdoll = Owner->GetComponentByClass<UHorseRagdollTestComponent>();
	if (!Ragdoll)
	{
		return false;
	}
	Ragdoll->RequestRagdoll(true);
	return true;
}

void UHorseMovementComponent::HandleCollapse()
{
	if (bCollapseRequested)
	{
		return;   // 중복 처리 방지
	}
	bCollapseRequested = true;
	UE_LOG("[HorseMovement] Posture collapsed (speed=%.2f, airTime=%.2f)", Velocity.Length(), AirTime);

	// TODO: 기수(Rider) 구현되면 여기에 낙마 처리 추가

	bRagdollTookOver = TryTriggerRagdoll();
}

void UHorseMovementComponent::BeginStuckEscape()
{
	if (bEscapingStuck)
	{
		return;
	}
	bEscapingStuck = true;
	EscapeTimer    = std::max(StuckEscapeTime, 0.0f);
	StuckTime      = 0.0f;
	UE_LOG("[HorseMovement] Wedged — escaping for %.2fs (collision off)", EscapeTimer);

	// 낙마부터. 래그돌이 켜지면 래그돌 쪽이 캡슐 collision 을 직접 내렸다 올리므로
	// 여기서 또 저장/복원하면 원래 값이 어긋난다 — 그때는 우리가 손대지 않는다.
	HandleCollapse();

	if (!bRagdollTookOver)
	{
		if (UCapsuleComponent* Capsule = Collision.Get())
		{
			SavedCollisionEnabled = Capsule->GetCollisionEnabled();
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			bEscapeOwnsCollision = true;
		}
	}

	// 실제로 빠져나오도록 마지막 조작 방향으로 밀어준다.
	if (!LastInputDirXY.IsNearlyZero() && StuckEscapeSpeed > 0.0f)
	{
		Velocity.X = LastInputDirXY.X * StuckEscapeSpeed;
		Velocity.Y = LastInputDirXY.Y * StuckEscapeSpeed;
	}
}

void UHorseMovementComponent::UpdateStuckEscape(float DeltaTime)
{
	if (!bEscapingStuck)
	{
		return;
	}
	EscapeTimer -= DeltaTime;
	if (EscapeTimer <= 0.0f)
	{
		EndStuckEscape();
	}
}

void UHorseMovementComponent::EndStuckEscape()
{
	if (!bEscapingStuck)
	{
		return;
	}
	bEscapingStuck = false;
	EscapeTimer    = 0.0f;
	if (bEscapeOwnsCollision)
	{
		if (UCapsuleComponent* Capsule = Collision.Get())
		{
			Capsule->SetCollisionEnabled(SavedCollisionEnabled);
		}
		bEscapeOwnsCollision = false;
	}
	UE_LOG("[HorseMovement] Stuck escape finished");
}

// ============================================================================

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
	Velocity.Z     = JumpSpeed;                   // 상향 임펄스. 없으면 즉시 착지 판정되어 지면을 못 벗어난다.
	EnterFalling(true);                       // 점프 즉시 물리 이동 전환(bJumpRequested 는 아래에서 별도 처리).
	bJumpRequested = false;                       // bJump anim pulse 종료 — 클립은 이미 진입, auto-exit 로 1회만 재생.
	bJumpActive    = true;                        // 의도적 점프 표식 — 착지까지 유지(walk-off 낙하와 구분·착지 리셋용). anim 은 구동 안 함.

	// 접지 frame 에서 곧바로 낙하 tick 을 돌리면 root box 가 아직 지면 접점에 있어 착지로 오인될 수
	// 있다. TickFalling 가 상승 중(Velocity.Z>0) 지면 체크를 건너뛰긴 하지만, 방어적으로 살짝 띄운다.
	if (USceneComponent* Updated = GetUpdatedComponent())
	{
		const float Lift = std::clamp(GroundSnapMaxDown * 0.1f, 0.02f, 0.05f);
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

float UHorseMovementComponent::ComputeInclineAngle(const FHorseGroundSample& Sample) const
{
	const float MaxAngle = std::acos(std::clamp(WalkableSlopeZ, 0.0f, 1.0f));
	if (MaxAngle <= 1.e-4f)
	{
		return 0.0f;
	}
	// PitchSlope 는 전방 기울기(+오르막) 자체라 부호 판정이 따로 필요 없다.
	const float SlopeAngle = std::atan(Sample.PitchSlope);
	return std::clamp(SlopeAngle / MaxAngle, -1.0f, 1.0f);
}

float UHorseMovementComponent::ComputeInclineAngle(const FHitResult& Ground) const
{
	// 표본 평면이 없을 때(Sliding 등)의 fallback — 지면 노멀만 본다.
	// 부호(오르막/내리막)는 forward 와 downhill 방향의 관계로 판정.
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

bool UHorseMovementComponent::TraceGroundAt(const FVector& ProbeXY, float FootZ, float Up, float Down, FHitResult& OutHit) const
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

	const float ProbeUp   = std::max(Up, 0.0f);
	const float ProbeDown = std::max(Down, 0.0f);
	const float RayLength = ProbeUp + ProbeDown;
	if (RayLength <= 1.e-4f)
	{
		return false;
	}

	const FVector From(ProbeXY.X, ProbeXY.Y, FootZ + ProbeUp);
	const FVector DownDir(0.0f, 0.0f, -1.0f);
	// 바닥은 WorldStatic ObjectType 만 후보(다이내믹/폰을 바닥으로 오인하지 않음) — CMC 와 동일.
	return World->PhysicsRaycastByObjectTypes(From, DownDir, RayLength, OutHit,
		ObjectTypeBit(ECollisionChannel::WorldStatic), Owner);
}

bool UHorseMovementComponent::TraceGround(const FVector& PivotLoc, FHitResult& OutHit) const
{
	// 지면 판정의 기준은 발 높이 기준 (actor pivot은 발높이보다 StandHeight만큼 위쪽)
	return TraceGroundAt(PivotLoc, PivotLoc.Z - StandHeight,
		GroundSnapMaxUp + GroundProbeExtraUpMargin, GroundSnapMaxDown, OutHit);
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
	// 끼임 탈출 중엔 몸통 충돌을 통과시켜 실제로 빠져나오게 한다.
	if (!bTorsoCollision || bEscapingStuck || DeltaXY.IsNearlyZero())
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

	// Sweep 판정에는 캡슐 콜라이더의 RotatedWorld space 값을 그대로 사용
	// NOTE: actor yaw 는 ConsumeRootMotion 에서 이미 반영된 상태
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
	// 끼임 탈출 중엔 밀어내기도 멈춘다 — 어차피 통과시켜서 빠져나오는 게 목적.
	if (!bTorsoCollision || bEscapingStuck)
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

void UHorseMovementComponent::DrawGroundProbeDebug(const FVector& PivotLoc, const FHorseGroundSample& Sample) const
{
	UWorld* World = GetWorld();
	const USceneComponent* Updated = GetUpdatedComponent();
	if (!World || !Updated)
	{
		return;
	}

	FVector Forward = Updated->GetForwardVector();
	FVector Right   = Updated->GetRightVector();
	Forward.Z = 0.0f;
	Right.Z   = 0.0f;
	Forward = Forward.IsNearlyZero() ? FVector(1.0f, 0.0f, 0.0f) : Forward.Normalized();
	Right   = Right.IsNearlyZero()   ? FVector(0.0f, 1.0f, 0.0f) : Right.Normalized();

	const float  FootZ = PivotLoc.Z - StandHeight;
	const FColor Green = FColor::Green();
	const FColor Red   = FColor::Red();
	const FColor PlaneColor(60, 120, 255);

	const float Up   = std::max(ProbeUp, 0.0f);
	const float Down = std::max(ProbeDown, 0.0f);

	// (1) 무게중심 sphere — 훑는 구간의 시작/끝과 최종 접점.
	const FVector ComXY = FVector(PivotLoc.X, PivotLoc.Y, 0.0f) + Forward * SupportSphereForward;
	const float   Radius = std::max(SupportSphereRadius, 0.01f);
	DrawDebugLine(World, FVector(ComXY.X, ComXY.Y, FootZ + Up), FVector(ComXY.X, ComXY.Y, FootZ - Down),
		Sample.bCenterHit ? Green : Red);
	if (Sample.bCenterHit)
	{
		// 접점 기준으로 sphere 를 그려서 어디에 얹혔는지 보이게 한다.
		DrawDebugSphere(World, FVector(ComXY.X, ComXY.Y, Sample.CenterPoint.Z + Radius), Radius, 16, Green);
		DrawDebugPoint(World, Sample.CenterPoint, 0.10f, Green);
	}
	else
	{
		DrawDebugSphere(World, FVector(ComXY.X, ComXY.Y, FootZ - Down + Radius), Radius, 16, Red);
	}

	// (2) 앞발/뒷발 ray. 좌우로는 벌리지 않는다(roll 미계산).
	const FVector Probes[2] = { PivotLoc + Forward * FrontProbeForward, PivotLoc - Forward * RearFootBack };
	const bool    Hits[2]   = { Sample.bFrontHit, Sample.bRearHit };
	const FVector Points[2] = { Sample.FrontPoint, Sample.RearPoint };
	for (int i = 0; i < 2; ++i)
	{
		const FVector Start = FVector(Probes[i].X, Probes[i].Y, FootZ + Up);
		const FVector End   = Hits[i] ? Points[i] : FVector(Probes[i].X, Probes[i].Y, FootZ - Down);
		DrawDebugLine(World, Start, End, Hits[i] ? Green : Red);
		if (Hits[i])
		{
			DrawDebugPoint(World, Points[i], 0.10f, Green);
		}
	}

	// (3) 앞뒤 접점을 잇는 선 = pitch 계산에 실제로 쓰인 기준. 흰색.
	if (Sample.bFrontHit && Sample.bRearHit)
	{
		DrawDebugLine(World, Sample.RearPoint, Sample.FrontPoint, FColor(255, 255, 255));
	}

	// (4) 회전 pivot — 이 점이 지면 위에 얹혀 있어야 기울기가 변할 때 발이 안 쓸린다. 노란 구.
	const FVector PivotLocal = GetTiltPivotLocal();
	const FVector PivotWorld = PivotLoc
		+ Forward * PivotLocal.X + Right * PivotLocal.Y + FVector(0.0f, 0.0f, PivotLocal.Z);
	DrawDebugSphere(World, PivotWorld, 0.10f, 10, FColor(255, 220, 0));

	// (5) 현재 mesh 에 적용 중인 발 평면(스무딩 반영). pivot 을 지나는 사각형 — 파랑.
	{
		float A = 0.0f; // XZ 기울기
		float B = 0.0f; // XY 기울기
		GetBodyTiltSlopes(A, B);
		const float HalfF = std::max(FrontProbeForward, 0.1f);
		const float HalfR = std::max(FrontProbeHalfWidth, 0.1f) * 2.0f;
		const float Corners[4][2] = { { HalfF, -HalfR }, { HalfF, HalfR }, { -HalfF, HalfR }, { -HalfF, -HalfR } };
		FVector P[4];
		for (int i = 0; i < 4; ++i)
		{
			const float dF = Corners[i][0];
			const float dR = Corners[i][1];
			P[i] = PivotWorld + Forward * dF + Right * dR + FVector(0.0f, 0.0f, A * dF + B * dR + BodyHeightZ);
		}
		for (int i = 0; i < 4; ++i)
		{
			DrawDebugLine(World, P[i], P[(i + 1) % 4], PlaneColor);
		}
	}

	// (6) 목표 up 벡터(클램프 후) — 파란 선.
	const FVector UpWorld = Forward * Sample.LocalTiltUp.X + Right * Sample.LocalTiltUp.Y
		+ FVector(0.0f, 0.0f, Sample.LocalTiltUp.Z);
	DrawDebugLine(World, PivotWorld, PivotWorld + UpWorld * 1.5f, PlaneColor);
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
	Ar << GroundSnapMaxDown;
	Ar << GroundSnapMaxUp;
	Ar << StandHeight;
	Ar << WalkableSlopeZ;
	Ar << SlideFriction;
	Ar << bTorsoCollision;
	Ar << TorsoSkin;
	Ar << JumpSpeed;
	Ar << RearMinSpeed;
	Ar << SkidFriction;
	Ar << SkidStopSpeed;
	Ar << YawAlignTime;
	Ar << MaxTurnRate;
	// ── 접지 probe ──
	Ar << SupportSphereRadius;
	Ar << SupportSphereForward;
	Ar << FrontProbeForward;
	Ar << FrontProbeHalfWidth;
	Ar << ProbeUp;
	Ar << ProbeDown;
	Ar << RearFootBack;
	Ar << EdgeSlipSpeed;
	// ── 몸통 기울기 ──
	Ar << bAlignBodyToGround;
	Ar << MaxBodyPitch;
	Ar << BodyAlignSpeed;
	Ar << AirLevelSpeed;
	Ar << TiltPivotForward;
	Ar << TiltPivotHeight;
	Ar << bSolveBodyHeight;
	Ar << BodyHeightSpeed;
	Ar << MaxBodyHeightOffset;
	// ── 물리 기반 이동 ──
	Ar << ForcedDismountSpeed;
	Ar << PhysicsGraceTime;
	Ar << bDismountDuringJump;
	Ar << StuckDetectTime;
	Ar << StuckAlongSpeed;
	Ar << StuckMaxSpeed;
	Ar << StuckEscapeTime;
	Ar << StuckEscapeSpeed;
}
