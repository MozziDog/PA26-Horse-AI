#include "HorseLocomotionComponent.h"

#include "HorseMovementComponent.h"
#include "Game/Horse/HorseConstants.h"
#include "Component/AI/BlackboardComponent.h"
#include "Core/TickFunction.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "Serialization/Archive.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
	constexpr int MinGaitValue = static_cast<int>(EHorseGait::Stop);
	constexpr int MaxGaitValue = static_cast<int>(EHorseGait::Gallop);

	EHorseGait ClampGait(EHorseGait Gait)
	{
		return static_cast<EHorseGait>(std::clamp(static_cast<int>(Gait), MinGaitValue, MaxGaitValue));
	}

	EHorseGait GaitStep(EHorseGait Gait, int Delta)
	{
		return ClampGait(static_cast<EHorseGait>(static_cast<int>(Gait) + Delta));
	}

	// V 를 world +Z 축 기준 Deg(도) 만큼 회전(수평 부채꼴 slot 생성용). Z 성분 보존.
	FVector RotateAroundZ(const FVector& V, float Deg)
	{
		const float R = Deg * FMath::DegToRad;
		const float C = std::cos(R);
		const float S = std::sin(R);
		return FVector(V.X * C - V.Y * S, V.X * S + V.Y * C, V.Z);
	}

	bool IsExtraSteeringSlot(int SlotIndex)
	{
		return SlotIndex == HorseBBKeys::ExtraSlotLeftIndex ||
			SlotIndex == HorseBBKeys::ExtraSlotRightIndex;
	}

	// SteeringSlot: ExtraSlot과 SensorSlot 모두 포함. 총 7개
	// SensorSlot(ObsSlot): 전방 센서로 감지하는 슬롯. 총 5개
	// ExtraSlot: 유턴 판정용 후방 슬롯. danger 계산하지 않고 NavDir만 계산
	int ToSensorSlotIndex(int SteeringSlotIndex)
	{
		return SteeringSlotIndex - 1;
	}
}

UHorseLocomotionComponent::UHorseLocomotionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEnabled = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// 센서(기본 TG_PrePhysics)가 Blackboard를 갱신한 뒤, Movement(TG_PostPhysics)가 입력을
	// 소비하기 전에 판단한다. 별도 prerequisite API가 없어 tick group으로 순서를 보장한다.
	PrimaryComponentTick.SetTickGroup(TG_DuringPhysics);
	PrimaryComponentTick.SetEndTickGroup(TG_DuringPhysics);
}

void UHorseLocomotionComponent::BeginPlay()
{
	UActorComponent::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		Movement       = Owner->GetComponentByClass<UHorseMovementComponent>();
		BlackboardComp = Owner->GetComponentByClass<UBlackboardComponent>();
	}
	World = GetWorld();

	Gait        = EHorseGait::Stop;
	GaitUpTimer = 0.0f;
	bUTurnActive = false;
	UTurnExtraSlotIndex = -1;
}

void UHorseLocomotionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	(void)TickType;
	(void)ThisTickFunction;

	AActor* Owner = GetOwner();
	if (!Movement || !Owner)
	{
		return;
	}

	FVector Forward;
	if (!GetPlanarForward(*Owner, Forward))
	{
		return;
	}

	FBlackboard* BB = BlackboardComp ? &BlackboardComp->GetBlackboard() : nullptr;
	if (!BB)	// 이하 blackboard는 무조건 valid하다고 전제
	{
		return;
	}

	// 평행이동은 별도 direct input 경로지만 policy가 명시적으로 허용할 때만 소비한다.
	const bool bEnableStrafe = IsPolicyEnabled(*BB, HorseBBKeys::ControlEnableStrafe, false);
	UpdateStrafeMode(bEnableStrafe);
	if (bStrafeMode)
	{
		Gait = EHorseGait::Stop;   // 평행이동 중 gait 개념 미적용
		Movement->SetStrafeInput(true, StrafeLongitudinal, StrafeLateral);
		return;
	}
	else
	{
		Movement->SetStrafeInput(false, 0.0f, 0.0f);
	}

	UpdateGait(*BB, DeltaTime);
	const FHorseSteeringInfluence Influence = GatherSteeringInfluences(*BB);
	UpdateJumpGate(*BB, DeltaTime);
	UpdateContextSteering(*BB, *Owner, Forward, Influence, DeltaTime);
}

bool UHorseLocomotionComponent::GetPlanarForward(const AActor& Owner, FVector& OutForward) const
{
	FVector Forward = Owner.GetActorForward();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return false;
	}
	OutForward = Forward.Normalized();
	return true;
}

// ── influence 소스 수집 ── guidance는 producer 출처와 무관한 공통 goal interest다.
FHorseSteeringInfluence UHorseLocomotionComponent::GatherSteeringInfluences(FBlackboard& BB) const
{
	FHorseSteeringInfluence Inf;

	FVector Temp;
	if (BB.TryGetVector(HorseBBKeys::GuidanceDirection, Temp) &&
		BB.TryGetFloat(HorseBBKeys::GuidanceWeight, Inf.GuidanceWeight))
	{
		Temp.Z = 0.0f;
		Inf.GuidanceWeight = std::max(0.0f, Inf.GuidanceWeight);
		if (Inf.GuidanceWeight > 1.e-3f && !Temp.IsNearlyZero())
		{
			Inf.GuidanceDir = Temp.Normalized();
			Inf.bGuidance = true;
		}
	}

	if (IsPolicyEnabled(BB, HorseBBKeys::ControlEnableRoadAssist, false) && Gait != EHorseGait::Stop
		&& BB.TryGetVector(HorseBBKeys::RoadDir, Temp) && !Temp.IsNearlyZero())
	{
		Temp.Z = 0.0f;
		if (!Temp.IsNearlyZero())
		{
			Inf.RoadDir = Temp.Normalized();
			Inf.bRoad = true;

			// 도로에서 멀수록 추종 약화(RoadDist 미기록 시 INF → 가중치 0).
			float RoadDist = FLT_MAX;
			BB.TryGetFloat(HorseBBKeys::RoadDist, RoadDist);
			const float Span  = std::max(1.e-3f, RoadFarDistance - RoadNearDistance);
			const float Atten = std::clamp((RoadFarDistance - RoadDist) / Span, 0.0f, 1.0f);
			Inf.RoadWeightEff = RoadWeight * Atten;
		}
	}

	return Inf;
}

bool UHorseLocomotionComponent::IsPolicyEnabled(FBlackboard& BB, FName Key, bool bDefault) const
{
	bool bValue = bDefault;
	BB.TryGetBool(Key, bValue);
	return bValue;
}

// ── 점프 게이트 ── 정면 장애물이 점프 가능(ObsJumpable)하고 트리거 거리 안이면 도약(heading 유지).
// 이미 bJumpPerformed인 경우에는 다시 점프 안함. (제자리 혹은 점프 후 연속 점프 방지)
void UHorseLocomotionComponent::UpdateJumpGate(FBlackboard& BB, float DeltaTime)
{
	if (!IsPolicyEnabled(BB, HorseBBKeys::ControlEnableAutoJump, false))
	{
		JumpCandidateTime = 0.0f;
		bJumpPerformed = false;
		return;
	}
	bool  bJumpable = false;
	float FwdDist   = 0.0f;
	const float JumpTriggerDist =
		GetGait() == EHorseGait::Gallop ? GallopJumpTriggerDist :
		GetGait() == EHorseGait::Canter ? CanterJumpTriggerDist :
		GetGait() == EHorseGait::Trot ? TrotJumpTriggerDist : -1.0f;

	const bool bJumpGateActive =
		BB.TryGetBool(HorseBBKeys::ObsJumpable, bJumpable) && bJumpable
		&& BB.TryGetFloat(HorseBBKeys::ObsFwdDist, FwdDist) && FwdDist < JumpTriggerDist
		&& Movement->GetForwardSpeed() >= MinJumpApproachSpeed;

	if (Movement->IsFalling())   // Falling 상태에서는 점프 불가
	{
		JumpCandidateTime = 0.0f;
		return;
	}

	if (bJumpGateActive)
	{
		JumpCandidateTime += std::max(0.0f, DeltaTime);
		if (JumpCandidateTime >= JumpConfirmTime && !bJumpPerformed)
		{
			Movement->StartJumpSequence();
			bJumpPerformed = true;   // 이번 접근에 대한 점프 소진
		}
	}
	else
	{
		JumpCandidateTime = 0.0f;
		bJumpPerformed = false;  // 장애물 벗어남 → 다음 장애물 상황을 위해 리셋
		// NOTE: 한 번 요청한 점프는 취소하지 않는다. 
		// 점프 시퀀스 중 도움닫기에서 장애물에 의해 점프 조건이 만족되지 않는 상황이 발생해버림
	}
}

// ── context steering ── 각 slot 의 interest 에서 graded danger 를 빼 최고점 방향을 고르고,
//    sub-slot 보간으로 연속 heading 을 만든다(20° 스냅 떨림 제거). 커밋은 danger 로 게이팅해
//    장애물 앞에서만 좌/우 핑퐁을 억제하고 열린 공간에선 forward 로 복귀한다. 미초기화면 forward.
void UHorseLocomotionComponent::UpdateContextSteering(FBlackboard& BB, const AActor& Owner, const FVector& Forward, const FHorseSteeringInfluence& Influence, float DeltaTime)
{
	static_assert(HorseBBKeys::SteeringSlotCount <= HORSE_MAX_FAN_SLOTS, "PrevDanger 버퍼(MaxFanSlots)보다 steering slot 이 많음");
	constexpr int N = HorseBBKeys::SteeringSlotCount;

	if (SteerDir.IsNearlyZero())
	{
		SteerDir = Forward;
	}

	// Stop + 무입력에서는 회전 입력을 Movement로 보내지 않는다. 다만 이전 입력의
	// smoothing 상태는 중립으로 감쇠시켜 다음 반대 방향 입력이 stale angle에서 시작하지 않게 한다.
	if (GetGait() == EHorseGait::Stop && !Influence.bGuidance)
	{
		RelaxSteeringToNeutral(Forward, DeltaTime);
		bUTurnActive = false;
		UTurnExtraSlotIndex = -1;
		return;
	}

	FSteerContext Field;

	// 정면 방향 슬롯 인덱스 구하기
	for (int i = 1; i < N; ++i)
	{
		if (std::abs(HorseBBKeys::SteeringSlotAngles[i]) < std::abs(HorseBBKeys::SteeringSlotAngles[Field.CenterIdx]))
		{
			Field.CenterIdx = i;
		}
	}

	UpdateUTurnState(Forward, Influence);
	const bool bContextAvoidance = IsPolicyEnabled(BB, HorseBBKeys::ControlEnableContextAvoidance, true);
	BuildDangerField(BB, Forward, DeltaTime, bContextAvoidance, Field);
	ScoreSlots(Owner, Forward, Influence, Field);

	if (GetGait() == EHorseGait::Stop)
	{
		// DesiredGait=Stop에서도 guidance가 있으면 안전 steering 결과를 향해 제자리 회전한다.
		if (Influence.bGuidance && Field.BestIdx >= 0 && Field.Score[Field.BestIdx] > -FLT_MAX)
		{
			ApplySteering(Owner, Forward, Field, DeltaTime);
		}
		return;
	}

	// 진행가능한 방향의 슬롯이 있으면 그쪽으로 조향
	// 진행할 수 없는 방향은 -FLT_MAX 로 배제
	if (Field.BestIdx >= 0 && Field.Score[Field.BestIdx] > -FLT_MAX)
	{
		ApplySteering(Owner, Forward, Field, DeltaTime);
	}
	else
	{
		if (Movement->IsJumpInProgress())
		{
			Movement->AddInputVector(Forward, GetGaitScaledSpeed());
		}
		else
		{
			Movement->Brake();
			Gait = EHorseGait::Stop;
		}
	}
}

void UHorseLocomotionComponent::RelaxSteeringToNeutral(const FVector& Forward, float DeltaTime)
{
	if (DeltaTime <= 0.0f || NeutralSteeringReturnSpeed <= 0.0f)
	{
		return;
	}

	const float Alpha = 1.0f - std::exp(-NeutralSteeringReturnSpeed * DeltaTime);
	SteerAngle += (0.0f - SteerAngle) * Alpha;
	if (std::abs(SteerAngle) < 0.01f)
	{
		SteerAngle = 0.0f;
		SteerDir = Forward;
	}
	else
	{
		SteerDir = RotateAroundZ(Forward, SteerAngle).Normalized();
	}
}

void UHorseLocomotionComponent::UpdateUTurnState(const FVector& Forward, const FHorseSteeringInfluence& Influence)
{
	if (!Influence.bGuidance)
	{
		bUTurnActive = false;
		UTurnExtraSlotIndex = -1;
		return;
	}

	const float Dot = std::clamp(Forward.Dot(Influence.GuidanceDir), -1.0f, 1.0f);
	const float Cross = Forward.X * Influence.GuidanceDir.Y - Forward.Y * Influence.GuidanceDir.X;
	const float SignedAngle = std::atan2(Cross, Dot) * RAD_TO_DEG;
	const float AbsAngle = std::abs(SignedAngle);
	const float EnterAngle = std::clamp(UTurnEnterAngle, 0.0f, 180.0f);
	const float ExitAngle = std::min(EnterAngle, std::clamp(UTurnExitAngle, 0.0f, 180.0f));

	if (!bUTurnActive)
	{
		if (AbsAngle >= EnterAngle)
		{
			UE_LOG("[UTurnDebug], Starting U-Turn");
			bUTurnActive = true;
			UTurnExtraSlotIndex = SignedAngle < 0.0f
				? HorseBBKeys::ExtraSlotLeftIndex
				: HorseBBKeys::ExtraSlotRightIndex;
		}
	}
	else if (AbsAngle <= ExitAngle)
	{
		UE_LOG("[UTurnDebug], Finishing U-Turn");
		bUTurnActive = false;
		UTurnExtraSlotIndex = -1;
	}

}

void UHorseLocomotionComponent::BuildDangerField(FBlackboard& BB, const FVector& Forward, float DeltaTime, bool bContextAvoidance, FSteerContext& Field)
{
	constexpr int N = HorseBBKeys::SteeringSlotCount;
	if (!bContextAvoidance)
	{
		for (int i = 0; i < N; ++i)
		{
			Field.SlotDir[i] = RotateAroundZ(Forward, HorseBBKeys::SteeringSlotAngles[i]);
			PrevDanger[i] = 0.0f;
		}
		return;
	}

	// 1) slot 별 danger(2단계). clear>=Safe → 0, Hard~Safe → 0..1 램프, clear<=Hard → 1(하드 제외).
	//    hard-block(bHardBlk)은 안전 제외라 항상 즉응. soft danger 는 아래에서 slow-release 로 감쇠.
	const float RampSpan = std::max(1.e-3f, SafeDistance - HardBlockDistance);
	for (int i = 0; i < N; ++i)
	{
		Field.SlotDir[i] = RotateAroundZ(Forward, HorseBBKeys::SteeringSlotAngles[i]);
		if (IsExtraSteeringSlot(i))
		{
			// ExtraSlot은 sensor 값을 읽거나 danger를 유지하지 않는다.
			PrevDanger[i] = 0.0f;
			continue;
		}

		// 장애물 유무에 의한 danger
		const int SensorSlotIndex = ToSensorSlotIndex(i);

		float Clear = SafeDistance;   // 값을 못 읽으면 열린 것으로 간주.
		BB.TryGetFloat(HorseBBKeys::ObsClear[SensorSlotIndex], Clear);

		if      (Clear <= HardBlockDistance) { Field.Danger[i] = 1.0f; Field.bHardBlk[i] = true; }
		else if (Clear <  SafeDistance)      { Field.Danger[i] = (SafeDistance - Clear) / RampSpan; }
		else                                 { Field.Danger[i] = 0.0f; }
	}

	for (int i = 0; i < N; ++i)
	{
		if (IsExtraSteeringSlot(i))
		{
			continue;
		}

		// 낭떠러지 유무에 의한 danger
		bool bGround;
		if (BB.TryGetBool(HorseBBKeys::ObsGround[ToSensorSlotIndex(i)], bGround) && !bGround)
		{
			// NOTE: 유저가 그 방향으로 직접 밀어 접근하려는 슬롯은 ScoreSlots()에서 danger 수치를 걷어낸다.
			//       그 경우에도 bCliff는 남으니 절벽앞 정지를 판단할 수 있음.
			Field.Danger[i] = 1.0f;
			Field.bCliff[i]  = true;
		}
	}

	// 2) danger persistence(fast-attack/slow-release) — 올릴 땐 즉시(max), 내릴 땐 초당 ReleaseRate 로만
	//     감쇠. 회전 중 장애물이 sweep 경계를 들락거려 danger 가 튀는 걸 흡수(조향 떨림 억제). 반응 지연은
	//     내려갈 때만 생기므로 회피 반응은 늦어지지 않는다. 토글 off 면 이전 값을 관측치로 리셋만 한다.
	for (int i = 0; i < N; ++i)
	{
		if (IsExtraSteeringSlot(i))
		{
			continue;
		}

		if (bDangerPersistence)
		{
			Field.Danger[i] = std::max(Field.Danger[i], PrevDanger[i] - DangerReleaseRate * DeltaTime);
		}
		PrevDanger[i] = Field.Danger[i];
	}
}

// Score = guidance + road + inertia - danger. Guidance의 출처는 이 계층에 없다.
void UHorseLocomotionComponent::ScoreSlots(const AActor& Owner, const FVector& Forward, const FHorseSteeringInfluence& Influence, FSteerContext& Field) const
{
	constexpr int N = HorseBBKeys::SteeringSlotCount;

	// 이웃으로 danger 확산 → 장애물에 걸렸을 때 조금 더 넓게 회피
	float SpreadDanger[HORSE_MAX_FAN_SLOTS] = {};
	for (int i = 0; i < N; i++)
	{
		if (IsExtraSteeringSlot(i))
		{
			continue;
		}

		float D = Field.Danger[i];
		if (i > 0 && !IsExtraSteeringSlot(i - 1))     { D = std::max(D, DangerSpread * Field.Danger[i - 1]); }
		if (i < N - 1 && !IsExtraSteeringSlot(i + 1)) { D = std::max(D, DangerSpread * Field.Danger[i + 1]); }
		SpreadDanger[i] = D;
	}

	float DangerActivation = 0.0f;
	for (int i = 0; i < N; i++)
	{
		if (!IsExtraSteeringSlot(i))
		{
			DangerActivation = std::max(DangerActivation, SpreadDanger[i]);
		}
	}

	const bool bDrawDebug = bDrawSteeringDebug && World.IsValid();
	const FVector DebugDrawPivot = bDrawDebug
		? Owner.GetActorLocation() + FVector::UpVector * 0.3f
		: FVector::ZeroVector;
	for (int i = 0; i < N; i++)
	{
		if (IsExtraSteeringSlot(i))
		{
			const bool bEligible = bUTurnActive && i == UTurnExtraSlotIndex;
			Field.Score[i] = bEligible
				? Influence.GuidanceWeight * std::max(0.0f, Field.SlotDir[i].Dot(Influence.GuidanceDir))
				: -FLT_MAX;
			if (Field.BestIdx < 0 || Field.Score[i] > Field.Score[Field.BestIdx])
			{
				Field.BestIdx = i;
			}

			if (bDrawDebug)
			{
				const FColor Col = bEligible ? FColor(80, 160, 255) : FColor(80, 80, 80);
				DrawDebugLine(World, DebugDrawPivot, DebugDrawPivot + Field.SlotDir[i] *
					(1.0f + std::max(0.0f, Field.Score[i])), Col);
			}
			continue;
		}

		float Interest = InertiaWeight * std::max(0.0f, Field.SlotDir[i].Dot(Forward));
		if (Influence.bGuidance)      { Interest += Influence.GuidanceWeight * std::max(0.0f, Field.SlotDir[i].Dot(Influence.GuidanceDir)); }
		if (Influence.bRoad)          { Interest += Influence.RoadWeightEff * std::max(0.0f, Field.SlotDir[i].Dot(Influence.RoadDir)); }

		float EffDanger = SpreadDanger[i];
		// 정면 방향일 경우
		if (i == Field.CenterIdx)
		{
			// spread 오염분을 ForwardLaneGuard 비율만큼 걷어내 raw danger 로 되돌린다.
			// 정면 slot은 danger spread 영향 줄여서 주행에 방해되지 않는 장애물에 과도하게 영향받지 않도록 함.
			EffDanger -= (SpreadDanger[i] - Field.Danger[i]) * ForwardLaneGuard;
		}

		// NOTE: SteeringInertia는 장애물 근처에서만 켜진다. (DangerActivation > 0)
		// 장애물 근처에서는 조향각 떨림을 억제하는 용도로 켜지고 열린 공간에서는 선회 조향 유지하지 않고 직진으로 수렴하게
		const float SteeringInertia = CommitWeight * std::max(0.0f, Field.SlotDir[i].Dot(SteerDir)) * DangerActivation;

		const bool bBlocked = Field.bHardBlk[i] || Field.bCliff[i];

		// 최종 스코어 계산 & 후보 갱신
		Field.Score[i] = bBlocked ? -FLT_MAX : (Interest - DangerWeight * EffDanger + SteeringInertia);
		if (Field.BestIdx < 0 || Field.Score[i] > Field.Score[Field.BestIdx])
		{
			Field.BestIdx = i;
		}

		// Debug Draw
		if (bDrawDebug)
		{
			// 초록(열림)→빨강(위험) 그라데이션으로 danger 표시
			const uint8  R   = static_cast<uint8>(std::clamp(SpreadDanger[i], 0.0f, 1.0f) * 255.0f);
			const FColor Col = (Field.bHardBlk[i] || Field.bCliff[i]) ? FColor::Red() : FColor(R, static_cast<uint8>(255 - R), 0, 255);
			DrawDebugLine(World, DebugDrawPivot, DebugDrawPivot + Field.SlotDir[i] * (1.0f + std::max(0.0f, Field.Score[i])), Col);
		}
	}
}

void UHorseLocomotionComponent::ApplySteering(const AActor& Owner, const FVector& Forward, const FSteerContext& Field, float DeltaTime)
{
	constexpr int N = HorseBBKeys::SteeringSlotCount;
	const int BestIdx = Field.BestIdx;

	// sub-slot 포물선 보간
	// 최고점 slot 과 양옆 score로 조향각을 구해 조향각이 이산적일 때의 어색함 + snap 경계에서의 떨림 방지
	float TargetAngle = HorseBBKeys::SteeringSlotAngles[BestIdx];
	if (BestIdx > 0 && BestIdx < N - 1
		&& !IsExtraSteeringSlot(BestIdx - 1) && !IsExtraSteeringSlot(BestIdx + 1)
		&& !Field.bHardBlk[BestIdx - 1] && !Field.bHardBlk[BestIdx + 1]
		&& !Field.bCliff[BestIdx - 1]   && !Field.bCliff[BestIdx + 1])   // 낭떠러지 이웃으로 heading 이 휘지 않게
	{
		const float sL = Field.Score[BestIdx - 1];
		const float sC = Field.Score[BestIdx];
		const float sR = Field.Score[BestIdx + 1];
		const float Denom = sL - 2.0f * sC + sR;
		if (Denom < -1.e-4f)   // 아래로 볼록(진짜 peak)일 때만 보간.
		{
			const float Offset = std::clamp(0.5f * (sL - sR) / Denom, -1.0f, 1.0f);   // [-1,1] slot 단위.
			const float Step   = HorseBBKeys::SteeringSlotAngles[BestIdx + 1] - HorseBBKeys::SteeringSlotAngles[BestIdx];
			TargetAngle += Offset * Step;
		}
	}

	// 조향각 서서히 회전 — 목표각이 튀어도 초당 SteerRateLimit 이하로만 따라가 조향각 떨림을 뭉개버림
	if (bSmoothSteering)
	{
		const float MaxStep = SteerRateLimit * DeltaTime;
		SteerAngle += std::clamp(TargetAngle - SteerAngle, -MaxStep, MaxStep);
	}
	else
	{
		SteerAngle = TargetAngle;
	}

	const FVector Heading = RotateAroundZ(Forward, SteerAngle).Normalized();
	SteerDir = Heading;   // 다음 프레임 커밋 기준

	if (bDrawSteeringDebug && World.IsValid())
	{
		const FVector DebugDrawPivot = Owner.GetActorLocation() + FVector::UpVector * 0.3f;
		DrawDebugLine(World, DebugDrawPivot, DebugDrawPivot + Heading * 3.0f, FColor::Blue());   // 선택된 heading.
	}

	if (GetGait() != EHorseGait::Stop)
	{
		// gait → scale([0,1]). Movement 는 MaxSpeed*scale 을 목표속도로 삼는다(yaw 선회율은 Movement 가 제한)
		Movement->AddInputVector(Heading, GetGaitScaledSpeed());
	}
	else
	{
		// 제자리 회전
		const FVector RotateInput = Heading;
		Movement->AddInputVector(RotateInput, 0.01f);
	}
}

void UHorseLocomotionComponent::SetStrafeMode(bool bInValue)
{
	bGazeHeld = bInValue;
}

void UHorseLocomotionComponent::SetStrafeVerticalInput(float InValue)
{
	StrafeLongitudinal = std::clamp(InValue, -1.0f, 1.0f);
}

void UHorseLocomotionComponent::SetStrafeHorizontalInput(float InValue)
{
	StrafeLateral = std::clamp(InValue, -1.0f, 1.0f);
}

void UHorseLocomotionComponent::UpdateStrafeMode(bool bEnabled)
{
	if (!bEnabled)
	{
		bStrafeMode = false;
		return;
	}

	if (!bStrafeMode)
	{
		// '전방 주시' 홀드 + 정지 상태(Gait::Stop & 속도 임계 이하) → Strafe 모드 진입
		// 접지 상태가 아니면(공중/미끄러짐) 진입하지 않는다.
		if (!bGazeHeld || Gait != EHorseGait::Stop || !Movement)
		{
			return;
		}
		// NOTE: 수직 방향 성분 필터링하지 않고 속도 계산함. 점프/낙하 시에도 횡이동 가능하게 하려면 수정할 것.
		const float Speed = Movement->GetVelocity().Length();
		if (Speed <= StrafeEnterMaxSpeed && !Movement->IsFalling() && !Movement->IsSliding())
		{
			bStrafeMode = true;
		}
	}
	else if (!bGazeHeld)	// 홀드 해제 시 Strafe 모드 종료
	{
		bStrafeMode = false;
	}
}

void UHorseLocomotionComponent::UpdateGait(FBlackboard& Blackboard, float DeltaTime)
{
	// 가속 쿨타임 타이머 처리
	if (GaitUpTimer > 0.0f)
	{
		GaitUpTimer = std::max(0.0f, GaitUpTimer - DeltaTime);
	}

	// BT에서 요청한 DesiredGait를 쿨타임 등 고려 후 실제 Gait에 반영
	int Desired = 0;
	if (Blackboard.TryGetInt(HorseBBKeys::DesiredGait, Desired)
		&& Desired != static_cast<int>(EHorseGait::None))
	{
		const int CurGait = static_cast<int>(Gait);
		const int TargetGait = std::clamp(Desired, 0, MaxGaitValue);
		if (TargetGait == static_cast<int>(EHorseGait::Stop))
		{
			if (Gait != EHorseGait::Stop)
			{
				RequestStop();
				if (!Movement->IsJumpInProgress())
				{
					Movement->Brake();
				}
			}
		}
		else if (TargetGait > CurGait)
		{
			RequestGiddyup();
		}
		else if (TargetGait < CurGait)
		{
			RequestSlowDown();
		}
	}

	// BT등에서 결정한 범위로 현재의 gait 클램핑
	ClampGaitToEnvelope();
}

void UHorseLocomotionComponent::RequestGiddyup()
{
	if (bStrafeMode)
	{
		return;   // 평행이동 중엔 가속(gait up) 무시.
	}
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
	// NOTE: Strafe 모드는 정지 상태에서만 진입 가능하므로 Strafe 모드 중의 감속/정지 명령은 자연스럽게 무시됨
	Gait = GaitStep(Gait, -1);
}

void UHorseLocomotionComponent::RequestStop()
{
	Gait = EHorseGait::Stop;
}

void UHorseLocomotionComponent::SetMaxGait(EHorseGait InMax)
{
	MaxGait = ClampGait(InMax);
	ClampGaitToEnvelope();
}

void UHorseLocomotionComponent::ClampGaitToEnvelope()
{
	Gait = ClampGait(Gait);
	MaxGait = ClampGait(MaxGait);
	if (Gait > MaxGait) Gait = MaxGait;
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
	Ar << RoadWeight;
	Ar << InertiaWeight;
	Ar << TrotJumpTriggerDist;
	Ar << CanterJumpTriggerDist;
	Ar << GallopJumpTriggerDist;
	Ar << JumpConfirmTime;
	Ar << MinJumpApproachSpeed;
	Ar << bDrawSteeringDebug;
	Ar << HardBlockDistance;
	Ar << DangerWeight;
	Ar << DangerSpread;
	Ar << CommitWeight;
	Ar << bDangerPersistence;
	Ar << DangerReleaseRate;
	Ar << bSmoothSteering;
	Ar << SteerRateLimit;
	Ar << ForwardLaneGuard;
	Ar << RoadNearDistance;
	Ar << RoadFarDistance;
	Ar << StrafeEnterMaxSpeed;
	Ar << UTurnEnterAngle;
	Ar << UTurnExitAngle;
	Ar << NeutralSteeringReturnSpeed;
}
