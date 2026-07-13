#include "HorseLocomotionComponent.h"

#include "HorseMovementComponent.h"
#include "AI/HorseBlackboardKeys.h"
#include "Component/AI/BlackboardComponent.h"
#include "Core/TickFunction.h"
#include "Debug/DrawDebugHelpers.h"
#include "GameFramework/AActor.h"
#include "Serialization/Archive.h"

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
		const float R = Deg * (3.14159265358979f / 180.0f);
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

	// 가속 쿨타임 타이머 처리
	if (GaitUpTimer > 0.0f)
	{
		GaitUpTimer = std::max(0.0f, GaitUpTimer - DeltaTime);
	}

	// BT 요청 등 고려해서 gait 업데이트
	UpdateGait();

	AActor* Owner = GetOwner();
	if (!Movement || !Owner || Gait == EHorseGait::Stop)
	{
		return;   // 정지 상태면 Movement에 입력 전달하지 않음 → 자연 감속
	}

	FVector Forward = Owner->GetActorForward();
	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return;
	}
	Forward = Forward.Normalized();

	const FVector Location = Owner->GetActorLocation();
	FBlackboard*  BB  = BlackboardComp ? &BlackboardComp->GetBlackboard() : nullptr;

	// ── influence 소스 수집 ── 없으면 기본 직진. 
	// 우선순위는 가중치로 표현: User(최상) > Road > Inertia. 
	// 장애물이 HardBlockDistance 보다 가까우면 아래 slot hard refuse로 위 우선순위 무시함
	FVector UserDir(0.0f, 0.0f, 0.0f);
	float   UserMag = 0.0f;
	FVector RoadDir(0.0f, 0.0f, 0.0f);
	bool    bRoad   = false;
	if (BB)
	{
		FVector Temp;
		if (BB->TryGetVector(HorseBBKeys::UserMoveDir, Temp) && !Temp.IsNearlyZero())
		{
			UserMag = std::clamp(Temp.Length(), 0.0f, 1.0f);
			Temp.Z = 0.0f;
			if (!Temp.IsNearlyZero()) 
				UserDir = Temp.Normalized();
			else                   
				UserMag = 0.0f;
		}
		if (BB->TryGetVector(HorseBBKeys::RoadDir, Temp) && !Temp.IsNearlyZero())
		{
			Temp.Z = 0.0f;
			if (!Temp.IsNearlyZero()) 
			{
				RoadDir = Temp.Normalized(); 
				bRoad = true; 
			}
		}
	}

	// ── 점프 게이트 ── 정면 장애물이 점프 가능(ObsJumpable)하고 트리거 거리 안이면 도약(heading 유지).
	if (BB)
	{
		bool  bJumpable = false;
		float FwdDist   = 0.0f;
		if (BB->TryGetBool(HorseBBKeys::ObsJumpable, bJumpable) && bJumpable
			&& BB->TryGetFloat(HorseBBKeys::ObsFwdDist, FwdDist) && FwdDist < JumpTriggerDist)
		{
			Movement->Jump();
		}
	}

	// ── context steering ── 각 slot 의 interest 에서 graded danger 를 빼 최고점 방향을 고르고,
	//    sub-slot 보간으로 연속 heading 을 만든다(20° 스냅 떨림 제거). 커밋은 danger 로 게이팅해
	//    장애물 앞에서만 좌/우 핑퐁을 억제하고 열린 공간에선 forward 로 복귀한다. 미초기화면 forward.
	const FVector DebugBase = Location + FVector(0.0f, 0.0f, 0.3f);
	if (SteerDir.IsNearlyZero()) { SteerDir = Forward; }

	constexpr int N = HorseBBKeys::ObsFanCount;
	static_assert(N <= MaxFanSlots, "PrevDanger 버퍼(MaxFanSlots)보다 fan slot 이 많음");

	// 1) slot 별 danger(2단계). clear>=Safe → 0, Hard~Safe → 0..1 램프, clear<=Hard → 1(하드 제외).
	//    hard-block(bHardBlk)은 안전 제외라 항상 즉응. soft danger 는 아래에서 slow-release 로 감쇠.
	float   Danger[N]  = {};
	bool    bHardBlk[N] = {};
	FVector SlotDir[N];
	const float RampSpan = std::max(1.e-3f, SafeDistance - HardBlockDistance);
	for (int i = 0; i < N; ++i)
	{
		SlotDir[i] = RotateAroundZ(Forward, HorseBBKeys::ObsFanAngles[i]);

		float Clear = SafeDistance;   // 값을 못 읽으면 열린 것으로 간주.
		if (BB) { BB->TryGetFloat(HorseBBKeys::ObsClear[i], Clear); }

		if      (Clear <= HardBlockDistance) { Danger[i] = 1.0f; bHardBlk[i] = true; }
		else if (Clear <  SafeDistance)      { Danger[i] = (SafeDistance - Clear) / RampSpan; }
		else                                 { Danger[i] = 0.0f; }
	}

	// 1b) danger persistence(fast-attack/slow-release) — 올릴 땐 즉시(max), 내릴 땐 초당 ReleaseRate 로만
	//     감쇠. 회전 중 장애물이 sweep 경계를 들락거려 danger 가 튀는 걸 흡수(조향 떨림 억제). 반응 지연은
	//     내려갈 때만 생기므로 회피 반응은 늦어지지 않는다. 토글 off 면 이전 값을 관측치로 리셋만 한다.
	for (int i = 0; i < N; ++i)
	{
		if (bDangerPersistence)
		{
			Danger[i] = std::max(Danger[i], PrevDanger[i] - DangerReleaseRate * DeltaTime);
		}
		PrevDanger[i] = Danger[i];
	}

	// 2) 이웃으로 danger 확산 → 장애물에 걸렸을 때 조금 더 넓게 회피
	float SpreadDanger[N];
	for (int i = 0; i < N; ++i)
	{
		float D = Danger[i];
		if (i > 0)     { D = std::max(D, DangerSpread * Danger[i - 1]); }
		if (i < N - 1) { D = std::max(D, DangerSpread * Danger[i + 1]); }
		SpreadDanger[i] = D;
	}

	// 3) Score = interest - danger + (danger 로 게이팅된) 커밋. 하드 제외 slot 은 후보에서 뺀다.
	//    커밋은 장애물이 있을 때(DangerActivation>0)만 켜져 좌/우 핑퐁을 억제하고, 열린 공간에선
	//    0 이 되어 관성이 이겨 forward 로 복귀한다 → 조향 고착/나선 방지.
	float DangerActivation = 0.0f;
	for (int i = 0; i < N; ++i) { DangerActivation = std::max(DangerActivation, SpreadDanger[i]); }

	// forward-lane guard: 정면(현재 heading, 0°) slot 은 옆 slot spread 로 페널티받지 않고 자기 lane 의 raw
	// danger 만 본다. 정면 sweep 이 BodyRadius 폭까지 뚫려 있으면 직진은 실제로 안전하므로, 통로 탈출 시 남은
	// 벽의 spread 가 직진을 눌러 열린 쪽으로 홱 꺾이는 현상을 없앤다. ForwardLaneGuard=0 이면 기존 동작.
	int CenterIdx = 0;
	for (int i = 1; i < N; ++i)
	{
		if (std::abs(HorseBBKeys::ObsFanAngles[i]) < std::abs(HorseBBKeys::ObsFanAngles[CenterIdx])) { CenterIdx = i; }
	}

	float Score[N];
	int   BestIdx = -1;
	for (int i = 0; i < N; ++i)
	{
		float Interest = InertiaWeight * std::max(0.0f, SlotDir[i].Dot(Forward));
		if (UserMag > 0.0f) { Interest += UserWeight * UserMag * std::max(0.0f, SlotDir[i].Dot(UserDir)); }
		if (bRoad)          { Interest += RoadWeight * std::max(0.0f, SlotDir[i].Dot(RoadDir)); }

		// 정면 slot 은 spread 오염분을 ForwardLaneGuard 비율만큼 걷어내 raw danger 로 되돌린다.
		float EffDanger = SpreadDanger[i];
		if (i == CenterIdx) { EffDanger -= (SpreadDanger[i] - Danger[i]) * ForwardLaneGuard; }

		const float Commit = CommitWeight * DangerActivation * std::max(0.0f, SlotDir[i].Dot(SteerDir));
		Score[i] = bHardBlk[i] ? -FLT_MAX : (Interest - DangerWeight * EffDanger + Commit);

		if (BestIdx < 0 || Score[i] > Score[BestIdx]) { BestIdx = i; }

		if (bDrawSteeringDebug && World.IsValid())
		{
			// 초록(열림)→빨강(위험) 그라데이션으로 danger 를 표시.
			const uint8  R   = static_cast<uint8>(std::clamp(SpreadDanger[i], 0.0f, 1.0f) * 255.0f);
			const FColor Col = bHardBlk[i] ? FColor::Red() : FColor(R, static_cast<uint8>(255 - R), 0, 255);
			DrawDebugLine(World, DebugBase, DebugBase + SlotDir[i] * (1.0f + std::max(0.0f, Score[i])), Col);
		}
	}

	// 뚫린 방향이 있다면
	if (BestIdx >= 0 && !bHardBlk[BestIdx])
	{
		// 4) sub-slot 포물선 보간 — 최고점 slot 과 양옆 score 로 연속 목표 조향각을 구해 20° 스냅 떨림을 없앤다.
		float TargetAngle = HorseBBKeys::ObsFanAngles[BestIdx];
		if (BestIdx > 0 && BestIdx < N - 1 && !bHardBlk[BestIdx - 1] && !bHardBlk[BestIdx + 1])
		{
			const float sL = Score[BestIdx - 1];
			const float sC = Score[BestIdx];
			const float sR = Score[BestIdx + 1];
			const float Denom = sL - 2.0f * sC + sR;
			if (Denom < -1.e-4f)   // 아래로 볼록(진짜 peak)일 때만 보간.
			{
				const float Offset = std::clamp(0.5f * (sL - sR) / Denom, -1.0f, 1.0f);   // [-1,1] slot 단위.
				const float Step   = HorseBBKeys::ObsFanAngles[BestIdx + 1] - HorseBBKeys::ObsFanAngles[BestIdx];
				TargetAngle += Offset * Step;
			}
		}

		// 5) 조향각 slew — 목표각이 튀어도 초당 SteerRateLimit 이하로만 따라가 잔여 떨림을 뭉갠다.
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
		SteerDir = Heading;   // 다음 프레임 커밋 기준.

		if (bDrawSteeringDebug && World.IsValid())
		{
			DrawDebugLine(World, DebugBase, DebugBase + Heading * 3.0f, FColor::Blue());   // 선택된 heading.
		}

		// gait → scale([0,1]). Movement 는 MaxSpeed*scale 을 목표속도로 삼는다(yaw 선회율은 Movement 가 제한).
		Movement->AddInputVector(Heading, GetGaitScaledSpeed());
	}
	// 모든 slot 이 하드 제외면(막다른 벽) 급브레이크
	else
	{
		Movement->Brake();
		if (bDrawSteeringDebug && World.IsValid())
		{
			DrawDebugSphere(World, DebugBase, 0.4f, 12, FColor::Red());
		}
		return;
	}
}

void UHorseLocomotionComponent::UpdateGait()
{
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
	Ar << JumpTriggerDist;
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
}
