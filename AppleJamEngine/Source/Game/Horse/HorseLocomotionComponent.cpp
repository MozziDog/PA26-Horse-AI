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
	EHorseGait GaitStep(EHorseGait Gait, int Delta)
	{
		return static_cast<EHorseGait>(static_cast<uint8>(Gait) + Delta);
	}

	// V 를 world +Z 축 기준 Deg(도) 만큼 회전(수평 부채꼴 slot 생성용). Z 성분 보존.
	FVector RotateAroundZ(const FVector& V, float Deg)
	{
		const float R = Deg * FMath::DegToRad;
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
	World = GetWorld();

	Gait        = EHorseGait::Stop;
	GaitUpTimer = 0.0f;
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

	UpdateGait(DeltaTime);                                                  // BT 요청 + 쿨타임 반영
	const FHorseSteeringInfluence Influence = GatherSteeringInfluences(*BB);     // UserInput/Road 소스
	UpdateJumpGate(*BB);                                                    // 정면 장애물 점프 게이트
	UpdateContextSteering(*BB, *Owner, Forward, Influence, DeltaTime);      // 회피 조향 + Movement 입력
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

// ── influence 소스 수집 ── 없으면 기본 직진.
// 우선순위는 가중치 형태로: UserInput(최상) > Road > Inertia.
// 장애물이 HardBlockDistance 보다 가까우면 slot hard refuse로 위 우선순위 무시함(steering 단계에서).
FHorseSteeringInfluence UHorseLocomotionComponent::GatherSteeringInfluences(FBlackboard& BB) const
{
	FHorseSteeringInfluence Inf;

	FVector Temp;
	if (BB.TryGetVector(HorseBBKeys::UserMoveDir, Temp) && !Temp.IsNearlyZero())
	{
		Inf.UserMag = std::clamp(Temp.Length(), 0.0f, 1.0f);
		Temp.Z = 0.0f;
		if (!Temp.IsNearlyZero())
			Inf.UserDir = Temp.Normalized();
		else
			Inf.UserMag = 0.0f;
	}

	if (BB.TryGetVector(HorseBBKeys::RoadDir, Temp) && !Temp.IsNearlyZero())
	{
		Temp.Z = 0.0f;
		if (!Temp.IsNearlyZero())
		{
			Inf.RoadDir = Temp.Normalized();
			Inf.bRoad = true;

			// 도로에서 멀수록 추종 약화(RoadDist 미기록 시 INF → 가중치 0), 유저 조향 중이면 추가 약화.
			float RoadDist = FLT_MAX;
			BB.TryGetFloat(HorseBBKeys::RoadDist, RoadDist);
			const float Span  = std::max(1.e-3f, RoadFarDistance - RoadNearDistance);
			const float Atten = std::clamp((RoadFarDistance - RoadDist) / Span, 0.0f, 1.0f);
			Inf.RoadWeightEff = RoadWeight * (1.0f - RoadUserYield * Inf.UserMag) * Atten;
		}
	}

	return Inf;
}

// ── 점프 게이트 ── 정면 장애물이 점프 가능(ObsJumpable)하고 트리거 거리 안이면 도약(heading 유지).
// 이미 bJumpPerformed인 경우에는 다시 점프 안함. (제자리 혹은 점프 후 연속 점프 방지)
void UHorseLocomotionComponent::UpdateJumpGate(FBlackboard& BB)
{
	bool  bJumpable = false;
	float FwdDist   = 0.0f;
	const float JumpTriggerDist =
		GetGait() == EHorseGait::Gallop ? GallopJumpTriggerDist :
		GetGait() == EHorseGait::Canter ? CanterJumpTriggerDist :
		GetGait() == EHorseGait::Trot ? TrotJumpTriggerDist : -1.0f;

	const bool bJumpGateActive =
		BB.TryGetBool(HorseBBKeys::ObsJumpable, bJumpable) && bJumpable
		&& BB.TryGetFloat(HorseBBKeys::ObsFwdDist, FwdDist) && FwdDist < JumpTriggerDist;

	if (Movement->IsFalling())   // Falling 상태에서는 점프 불가
	{
		return;
	}

	if (bJumpGateActive && !bJumpPerformed)
	{
		Movement->StartJump();
		bJumpPerformed = true;   // 이번 접근에 대한 점프 소진
	}
	else if (!bJumpGateActive)
	{
		bJumpPerformed = false;  // 장애물 벗어남 → 다음 장애물 상황을 위해 리셋
	}
}

// ── context steering ── 각 slot 의 interest 에서 graded danger 를 빼 최고점 방향을 고르고,
//    sub-slot 보간으로 연속 heading 을 만든다(20° 스냅 떨림 제거). 커밋은 danger 로 게이팅해
//    장애물 앞에서만 좌/우 핑퐁을 억제하고 열린 공간에선 forward 로 복귀한다. 미초기화면 forward.
void UHorseLocomotionComponent::UpdateContextSteering(FBlackboard& BB, const AActor& Owner, const FVector& Forward, const FHorseSteeringInfluence& Influence, float DeltaTime)
{
	static_assert(HorseBBKeys::ObsFanCount <= HORSE_MAX_FAN_SLOTS, "PrevDanger 버퍼(MaxFanSlots)보다 fan slot 이 많음");
	constexpr int N = HorseBBKeys::ObsFanCount;

	if (SteerDir.IsNearlyZero()) 
	{ 
		SteerDir = Forward; 
	}

	FSteerContext Field;

	// 정면 방향 슬롯 인덱스 구하기
	for (int i = 1; i < N; ++i)
	{
		if (std::abs(HorseBBKeys::ObsFanAngles[i]) < std::abs(HorseBBKeys::ObsFanAngles[Field.CenterIdx]))
		{
			Field.CenterIdx = i;
		}
	}

	BuildDangerField(BB, Forward, DeltaTime, Field);
	ScoreSlots(Forward, Influence, Field);

	// 진행가능한 방향의 슬롯이 있으면 그쪽으로 조향
	// 진행할 수 없는 방향은 -FLT_MAX 로 배제
	// NOTE: 유저 입력에 따라 BestIdx 방향이 낭떠러지일 수 있음. 그 경우 ApplySteering에서 '정지' 수행.
	if (Field.BestIdx >= 0 && Field.Score[Field.BestIdx] > -FLT_MAX)
	{
		ApplySteering(Forward, Field, DeltaTime);
	}
	// 진행가능한 slot이 하나도 없으면(=막다른 벽/낭떠러지 앞) 전진 차단 + 급브레이크.
	// 단, 유저가 조향 중이면 제자리 회전만은 허용 (구석 탈출용)
	else
	{
		Movement->Brake();   // 전진 목표속도 0 + 급정지/rearing 트리거
		if (!bJumpPerformed) // 점프 도중에는 급브레이크하지 않음
		{
			Gait = EHorseGait::Stop;
		}

		// 원하는 방향으로 아주 작은 입력 = 제자리 회전
		if (Influence.UserMag > 0.0f)
		{
			Movement->AddInputVector(Influence.UserDir, 0.01f);
		}
	}
}

void UHorseLocomotionComponent::BuildDangerField(FBlackboard& BB, const FVector& Forward, float DeltaTime, FSteerContext& Field)
{
	constexpr int N = HorseBBKeys::ObsFanCount;

	// 1) slot 별 danger(2단계). clear>=Safe → 0, Hard~Safe → 0..1 램프, clear<=Hard → 1(하드 제외).
	//    hard-block(bHardBlk)은 안전 제외라 항상 즉응. soft danger 는 아래에서 slow-release 로 감쇠.
	const float RampSpan = std::max(1.e-3f, SafeDistance - HardBlockDistance);
	for (int i = 0; i < N; ++i)
	{
		// 장애물 유무에 의한 danger
		Field.SlotDir[i] = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);

		float Clear = SafeDistance;   // 값을 못 읽으면 열린 것으로 간주.
		BB.TryGetFloat(HorseBBKeys::ObsClear[i], Clear);

		if      (Clear <= HardBlockDistance) { Field.Danger[i] = 1.0f; Field.bHardBlk[i] = true; }
		else if (Clear <  SafeDistance)      { Field.Danger[i] = (SafeDistance - Clear) / RampSpan; }
		else                                 { Field.Danger[i] = 0.0f; }
	}

	for (int i = 0; i < N; ++i)
	{
		// 낭떠러지 유무에 의한 danger
		bool bGround;
		if (BB.TryGetBool(HorseBBKeys::ObsGround[i], bGround) && !bGround)
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
		if (bDangerPersistence)
		{
			Field.Danger[i] = std::max(Field.Danger[i], PrevDanger[i] - DangerReleaseRate * DeltaTime);
		}
		PrevDanger[i] = Field.Danger[i];
	}
}

// Score = interest - danger + inertia, bHardBlk[i]가 참인 slot은 후보에서 제외
void UHorseLocomotionComponent::ScoreSlots(const FVector& Forward, const FHorseSteeringInfluence& Influence, FSteerContext& Field) const
{
	constexpr int N = HorseBBKeys::ObsFanCount;

	// 유저가 낭떠러지 쪽으로 '직접' 미는 방향 슬롯은 danger 를 걷어낸다 → 강제로 낭떠러지 방향으로 모는 것 허용
	// 유저 입력 방향이랑 일치하지 않는 낭떠러지는 danger를 유지해 회피 성향 유지
	bool bUserIntoCliff[HORSE_MAX_FAN_SLOTS] = {};
	const float CliffOverrideAlignDot = 0.9f;
	for (int i = 0; i < N; i++)
	{
		bUserIntoCliff[i] = Field.bCliff[i]
							&& Influence.UserMag >= CliffOverrideMinInput
							&& Field.SlotDir[i].Dot(Influence.UserDir) >= CliffOverrideAlignDot;
		if (bUserIntoCliff[i]) { Field.Danger[i] = 0.0f; }
	}

	// 이웃으로 danger 확산 → 장애물에 걸렸을 때 조금 더 넓게 회피
	float SpreadDanger[HORSE_MAX_FAN_SLOTS] = {};
	for (int i = 0; i < N; i++)
	{
		float D = Field.Danger[i];
		if (i > 0)     { D = std::max(D, DangerSpread * Field.Danger[i - 1]); }
		if (i < N - 1) { D = std::max(D, DangerSpread * Field.Danger[i + 1]); }
		SpreadDanger[i] = D;
	}

	float DangerActivation = 0.0f;
	for (int i = 0; i < N; i++) 
	{ 
		DangerActivation = std::max(DangerActivation, SpreadDanger[i]); 
	}

	const FVector DebugDrawPivot = Owner->GetActorLocation() + FVector::UpVector * 0.3f;
	for (int i = 0; i < N; i++)
	{
		float Interest = InertiaWeight * std::max(0.0f, Field.SlotDir[i].Dot(Forward));
		if (Influence.UserMag > 0.0f) { Interest += UserWeight * Influence.UserMag * std::max(0.0f, Field.SlotDir[i].Dot(Influence.UserDir)); }
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

		// 낭떠러지 방향 슬롯은 유저가 그 방향으로 직접 밀 때만 후보로 허용
		// 그 외엔 장애물 Hard-block 과 동일하게 후보에서 제외 → 선회 회피 성향 유지
		const bool bBlocked = Field.bHardBlk[i] || (Field.bCliff[i] && !bUserIntoCliff[i]);

		// 최종 스코어 계산 & 후보 갱신
		Field.Score[i] = bBlocked ? -FLT_MAX : (Interest - DangerWeight * EffDanger + SteeringInertia);
		if (Field.BestIdx < 0 || Field.Score[i] > Field.Score[Field.BestIdx])
		{
			Field.BestIdx = i;
		}

		// Debug Draw
		if (bDrawSteeringDebug && World.IsValid())
		{
			// 초록(열림)→빨강(위험) 그라데이션으로 danger 표시
			const uint8  R   = static_cast<uint8>(std::clamp(SpreadDanger[i], 0.0f, 1.0f) * 255.0f);
			const FColor Col = (Field.bHardBlk[i] || Field.bCliff[i]) ? FColor::Red() : FColor(R, static_cast<uint8>(255 - R), 0, 255);
			DrawDebugLine(World, DebugDrawPivot, DebugDrawPivot + Field.SlotDir[i] * (1.0f + std::max(0.0f, Field.Score[i])), Col);
		}
	}
}

void UHorseLocomotionComponent::ApplySteering(const FVector& Forward, const FSteerContext& Field, float DeltaTime)
{
	constexpr int N = HorseBBKeys::ObsFanCount;
	const int BestIdx = Field.BestIdx;

	// sub-slot 포물선 보간
	// 최고점 slot 과 양옆 score로 조향각을 구해 조향각이 이산적일 때의 어색함 + snap 경계에서의 떨림 방지
	float TargetAngle = HorseBBKeys::ObsFanAngles[BestIdx];
	if (BestIdx > 0 && BestIdx < N - 1
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
			const float Step   = HorseBBKeys::ObsFanAngles[BestIdx + 1] - HorseBBKeys::ObsFanAngles[BestIdx];
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
		const FVector DebugDrawPivot = Owner->GetActorLocation() + FVector::UpVector * 0.3f;
		DrawDebugLine(World, DebugDrawPivot, DebugDrawPivot + Heading * 3.0f, FColor::Blue());   // 선택된 heading.
	}

	// 유저가 낭떠러지 쪽으로 밀어 그 슬롯이 강제로 선택된 경우 (급)브레이크
	if (Field.bCliff[BestIdx])
	{
		Movement->Brake();
		Gait = EHorseGait::Stop;
		Movement->AddInputVector(Heading, 1.0f);   // 정지 시에 갑자기 yaw 바뀌는 것 방지
		UE_LOG("[HorseLocomotion] Stop before cliff. Heading: (%f, %f)", Heading.X, Heading.Y);
		return;
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

void UHorseLocomotionComponent::UpdateGait(float DeltaTime)
{
	// 가속 쿨타임 타이머 처리
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
			const int CurGait = static_cast<int>(Gait);
			const int TargetGait = std::clamp(Desired, 0, static_cast<int>(EHorseGait::Gallop));
			if (TargetGait > CurGait)
			{
				RequestGiddyup();
			}
			else if (TargetGait < CurGait)
			{
				RequestSlowDown();
			}
		}
	}

	// BT등에서 결정한 범위로 현재의 gait 클램핑
	ClampGaitToEnvelope();
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
	Ar << TrotJumpTriggerDist;
	Ar << CanterJumpTriggerDist;
	Ar << GallopJumpTriggerDist;
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
	Ar << RoadUserYield;
	Ar << RoadNearDistance;
	Ar << RoadFarDistance;
	Ar << CliffOverrideMinInput;
}
