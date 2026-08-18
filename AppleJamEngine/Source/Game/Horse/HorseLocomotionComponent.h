#pragma once

#include "Component/ActorComponent.h"

class UHorseMovementComponent;
class UBlackboardComponent;
class FBlackboard;
class AActor;

// 말 이동 제어 계층 — BT(모드 결정)와 Movement(actuation) 사이의 중간 계층.
// 책임 둘: (1) 조향 방향 산출(지금은 플레이어 steering, 후속 phase 에서 도로추종·장애물회피),
//          (2) 보법(gait) 상태머신 소유 — Stop→Walk→Trot→Canter→Gallop 를 한 단계씩 전환.
// Movement는 gait 단계 대신 여기서 채워넣은 InputVector만 보고 이동 속도와 선회반경 계산
#include "Source/Game/Horse/HorseLocomotionComponent.generated.h"

UENUM()
enum class EHorseGait : uint8
{
	None = 0, // 정보 없음. DesiredGait가 없음 등을 표현할 때 사용
	Stop = 1, // 정지
	Walk,     // 평보
	Trot,     // 속보
	Canter,   // 구보
	Gallop,   // 습보(최고속)
};

constexpr int HORSE_MAX_FAN_SLOTS = 8;   // slot 병렬 버퍼 상한. cpp 에서 SteeringSlotCount <= 이 값 검증.

// context-steering 작업 버퍼 — slot 병렬 배열을 한 덩어리로 묶어 하위 단계 간 전달.
struct FSteerContext
{
	FVector SlotDir[HORSE_MAX_FAN_SLOTS];           // slot 별 world direction
	float   Danger[HORSE_MAX_FAN_SLOTS] = {};       // slot 별 danger 수치
	bool    bHardBlk[HORSE_MAX_FAN_SLOTS] = {};     // hard block - 아예 후보로 선택 불가능한 경우(벽)
	bool    bCliff[HORSE_MAX_FAN_SLOTS] = {};       // 낭떠러지(지면 없음) slot. 무입력이면 hard-block 취급(선회 회피), 유저가 밀면 개방 후 가장자리 정지.
	float   Score[HORSE_MAX_FAN_SLOTS] = {};        // slot 별 최종 스코어
	int     CenterIdx = 0;                     // 정면 slot 인덱스
	int     BestIdx = -1;                      // 최고점 slot (0보다 작으면 미결정)
};

// 조향 입력 플러스 요소 — GatherSteeringInfluences에서 수집한 소스
struct FHorseSteeringInfluence
{
	// Guidance: 상위 계층에서 생산한 '가고자하는 방향'
	FVector GuidanceDir = FVector::ZeroVector;
	float   GuidanceWeight = 0.0f;
	bool    bGuidance = false;
	FVector RoadDir = FVector(0.0f, 0.0f, 0.0f);	// 도로 방향(수평, 정규화)
	bool    bRoad = false;                      // 도로 추종 적용 여부 (유효한 도로 방향 존재 + 도로 추종 활성화됨)
	float   RoadWeightEff = 0.0f;               // 거리 감쇠를 적용한 도로추종 가중치
};

UCLASS()
class UHorseLocomotionComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UHorseLocomotionComponent();
	~UHorseLocomotionComponent() override = default;

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void Serialize(FArchive& Ar) override;

	// gait 요청 API
	// NOTE: 컴포넌트의 판단에 따라 '요청'은 무시될 수 있음
	UFUNCTION(Callable, Category="Locomotion|Gait")
	void RequestGiddyup();   // 한 단계 up (cooldown + envelope + Movement 가속 가능성 체크)
	UFUNCTION(Callable, Category="Locomotion|Gait")
	void RequestSlowDown();  // 한 단계 down (감속은 쿨타임과 조건 체크 없음)
	UFUNCTION(Callable, Category="Locomotion|Gait")
	void RequestStop();      // 즉시 Stop으로

	// BT가 모드에 맞춰 gait 범위를 제약할 때 사용
	// 현재 gait가 범위를 벗어나면 tick()에서 끌어당긴다
	UFUNCTION(Callable, Category="Locomotion|Gait")
	void SetMaxGait(EHorseGait InMax);

	// 평행이동(strafe) 플레이어 입력 라우팅 — HorseCharacter에서 매 frame 호출.
	// 평행이동 모드 시에는 유저 입력을 blackboard를 통해 간접으로 받지 않고 직접 받음
	UFUNCTION(Callable, Category="Locomotion|Strafe")
	void SetStrafeMode(bool bInValue);
	UFUNCTION(Callable, Category = "Locomotion|Strafe")
	void SetStrafeVerticalInput(float InValue);
	UFUNCTION(Callable, Category="Locomotion|Strafe")
	void SetStrafeHorizontalInput(float InValue);
	UFUNCTION(Pure, Category="Locomotion|Strafe")
	bool IsStrafing() const { return bStrafeMode; }

	UFUNCTION(Pure, Category="Locomotion|Gait")
	EHorseGait GetGait() const { return Gait; }
	UFUNCTION(Pure, Category="Locomotion|Gait")
	float GetGaitTargetSpeed() const;   // 현재 gait 의 목표 속도(m/s)

protected:
	float GetGaitScaledSpeed() const; // 목표속도 / Movement MaxSpeed 를 [0,1] 로
	void UpdateStrafeMode(bool bEnabled); // policy가 허용할 때만 평행이동 진입/유지
	void UpdateGait(FBlackboard& Blackboard, float DeltaTime); // BT에서 요청한 DesiredGait를 쿨타임 등 고려 후 실제 Gait에 반영
	void  ClampGaitToEnvelope();
	bool GetPlanarForward(const AActor& Owner, FVector& OutForward) const;   // 수평 forward, degenerate 면 false
	FHorseSteeringInfluence GatherSteeringInfluences(FBlackboard& BB) const;
	bool IsPolicyEnabled(FBlackboard& BB, FName Key) const;
	void UpdateJumpGate(FBlackboard& BB, float DeltaTime);
	void UpdateContextSteering(FBlackboard& BB, const AActor& Owner, const FVector& Forward, const FHorseSteeringInfluence& Influence, float DeltaTime);
	void SmoothSteeringToNeutral(const FVector& Forward, float DeltaTime); // 정지 상태에서 입력 없더라도 조향각 smoothing은 계속 진행
	// UpdateContextSteering 하위 루틴
	void UpdateUTurnState(const FVector& Forward, const FHorseSteeringInfluence& Influence);
	void BuildDangerField(FBlackboard& BB, const FVector& Forward, float DeltaTime, bool bContextAvoidance, FSteerContext& Field);
	void ScoreSlots(const AActor& Owner, const FVector& Forward, const FHorseSteeringInfluence& Influence, FSteerContext& Field) const;   // 슬롯 별 최종 스코어 계산
	void ApplySteering(const AActor& Owner, const FVector& Forward, const FSteerContext& Field, float DeltaTime);				// 보간까지 거친 후 Movement에 전달

	TWeakObjectPtr<UHorseMovementComponent> Movement = nullptr;
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp = nullptr;
	TWeakObjectPtr<UWorld> World = nullptr;		// 디버깅 시각화용

	// ── context-steering 튜닝 : 장애물 회피 관련 ────────────────────────────────────────────────────────
	// 회피는 2단계로 구분: 
	// SafeDistance~HardBlockDistance 구간은 danger 수치 0 ~ 1 선형 증가 (soft penalty)
	// HardBlockDistance 이하는 danger=1 → 해당 slot 을 반드시 제외 (hard penalty)
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Safe Distance", Min=0.0f, Max=20.0f, Speed=0.05f)
	float SafeDistance = 5.0f;    // m — clearance 가 이 값부터 danger 가 붙기 시작(램프 상단).
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Hard Block Distance", Min=0.0f, Max=20.0f, Speed=0.05f)
	float HardBlockDistance = 1.8f;   // m — 이 값 이하 clearance 인 slot 은 danger=1 이며 절대 선택 안 함
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Danger Weight", Min=0.0f, Max=20.0f, Speed=0.05f)
	float DangerWeight = 3.0f;    // danger 가 interest 를 깎는 강도. interest 합보다 커야 실제로 회피한다.
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Danger Spread", Min=0.0f, Max=1.0f, Speed=0.02f)
	float DangerSpread = 0.5f;    // 이웃 slot 으로 번지는 danger 비율(0=번짐 없음). context steering의 판단 떨림 완화용
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Forward Lane Guard", Min=0.0f, Max=1.0f, Speed=0.02f)
	float ForwardLaneGuard = 1.0f;   // 정면 slot에서 DangerSpread 억제(1=완전 제거, 0=off). 터널 탈출 시 어느쪽부터 빠져나오냐에 따라 조향 떨림 억제용
	
	// ── context-steering 튜닝 : 도로 관련 ───────────────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Road Weight", Min=0.0f, Max=10.0f, Speed=0.05f)
	float RoadWeight = 1.0f;      // 도로 방향 interest 가중.
	// 도로에서 멀어질수록 추종 약화, 블랙보드에 RoadDist 없으면 RoadDist == INF으로 간주.
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Road Near Distance", Min=0.0f, Max=50.0f, Speed=0.05f)
	float RoadNearDistance = 3.0f;
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Road Far Distance", Min=0.0f, Max=50.0f, Speed=0.05f)
	float RoadFarDistance = 14.0f;

	// guidance 목표가 전방 sensor fan 밖으로 크게 벗어나면 유턴 상태에 진입
	// 진입/해제 각도를 분리해 경계에서 유턴↔정지로 상태가 떨리는 것 방지
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="U-Turn Enter Angle", Min=0.0f, Max=180.0f, Speed=1.0f)
	float UTurnEnterAngle = 65.0f;
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="U-Turn Exit Angle", Min=0.0f, Max=180.0f, Speed=1.0f)
	float UTurnExitAngle = 30.0f;

	// ── context-steering 튜닝 : 조향 떨림 방지 관련 ─────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Inertia Weight", Min=0.0f, Max=10.0f, Speed=0.05f)
	float InertiaWeight = 0.5f;   // 현재 진행(forward) 유지 관성 가중(최하위).
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Commit Weight", Min=0.0f, Max=10.0f, Speed=0.05f)
	float CommitWeight = 0.75f;   // 직전 선택 heading 을 유지하려는 히스테리시스. 좌/우 argmax 핑퐁(떨림) 억제.
	// 회전 중 장애물이 센서 경계를 들락거려 clearance가 튈 때, danger를 천천히 감소시켜 조향 떨림을 억제,
	// danger의 증가는 장애물 회피 반응성 고려해서 즉시 반영
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Danger Persistence")
	bool  bDangerPersistence = true;
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Danger Release Rate", Min=0.0f, Max=20.0f, Speed=0.05f)
	float DangerReleaseRate = 3.0f;   // danger/sec — danger 가 내려갈 때 초당 감쇠량. 클수록 빨리 잊음(0=영구 유지)
	// 조향각 스무딩 — 목표 조향각(heading)이 튀어도 초당 SteerRateLimit 이하로만 바꿔
	// 장애물이 센서에 걸리기 시작하는 순간의 조향각 튐을 뭉갠다. 낮출수록 반응성↓·튐억제↑
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Smooth Steering")
	bool  bSmoothSteering = true;
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Steer Rate Limit", Min=0.0f, Max=720.0f, Speed=1.0f)
	float SteerRateLimit = 90.0f;     // 조향각 변화 상한치(deg/s) — 자연스러운 조향 변화 연출용
	// 정지 상태에서 guidance가 사라졌을 때 마지막 조향값이 다음 입력에 섞이지 않도록 0도로 감쇠한다.
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Neutral Steering Return Speed", Min=0.0f, Max=30.0f, Speed=0.1f)
	float NeutralSteeringReturnSpeed = 8.0f;

	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Draw Steering Debug")
	bool  bDrawSteeringDebug = true;

	// ── gait별 목표 속도(m/s). Stop은 당연히 0 ──────────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Walk Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float WalkSpeed = 1.8f;
	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Trot Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float TrotSpeed = 3.7f;
	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Canter Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float CanterSpeed = 5.5f;
	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Gallop Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float GallopSpeed = 16.6f;

	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Gait Up Cooldown", Min=0.0f, Max=5.0f, Speed=0.01f)
	float GaitUpCooldown = 1.0f; 	// 가속 쿨타임(sec)

	// ── 평행이동(strafe) ────────────────────────────────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="Locomotion|Strafe", DisplayName="Strafe Enter Max Speed", Min=0.0f, Max=5.0f, Speed=0.01f)
	float StrafeEnterMaxSpeed = 0.2f;   // m/s — Strafe 모드 진입을 위한 '거의 멈춤' 기준 속도

	// ── 정면의 장애물이 설정된 거리 이하로 다가왔을 때 점프 동작 시작 ───────────────────────────────────
	UPROPERTY(Edit, Save, Category = "Locomotion|Jump", DisplayName = "Trot Jump Trigger Dist", Min = 0.0f, Max = 20.0f, Speed = 0.05f)
	float TrotJumpTriggerDist = 2.5f;
	UPROPERTY(Edit, Save, Category = "Locomotion|Jump", DisplayName = "Canter Jump Trigger Dist", Min = 0.0f, Max = 20.0f, Speed = 0.05f)
	float CanterJumpTriggerDist = 4.0f;
	UPROPERTY(Edit, Save, Category = "Locomotion|Jump", DisplayName = "Gallop Jump Trigger Dist", Min = 0.0f, Max = 20.0f, Speed = 0.05f)
	float GallopJumpTriggerDist = 7.0f;

	UPROPERTY(Edit, Save, Category = "Locomotion|Jump", DisplayName = "Jump Confirm Time", Min = 0.0f, Max = 1.0f, Speed = 0.01f)
	float JumpConfirmTime = 0.05f; 	// 짧은 센서 노이즈로 점프가 발동되지 않도록하는 최소 '점프 조건 만족' 유지시간

	UPROPERTY(Edit, Save, Category = "Locomotion|Jump", DisplayName = "Min Jump Approach Speed", Min = 0.0f, Max = 20.0f, Speed = 0.05f)
	float MinJumpApproachSpeed = 0.5f; // 점프를 위한 최소 전진 속도. 정지/후진 등에서 점프 발동되는 것 방지용


	// ── runtime states ──────────────────────────────────────────────────────────────────────────────────
	EHorseGait Gait     = EHorseGait::Stop;
	EHorseGait MaxGait  = EHorseGait::Gallop;	// 현재 속력으로는 선회각 모자름 등의 사유로 잠시 속도 늦출 때 사용
	float      GaitUpTimer   = 0.0f;   // >0 이면 up-shift 대기 중.
	FVector    SteerDir      = FVector(0.0f, 0.0f, 0.0f);   // 직전 프레임에 선택한 회피 heading(커밋 히스테리시스용). 0=미초기화.
	float      PrevDanger[HORSE_MAX_FAN_SLOTS] = {};   // slot 별 직전 프레임 danger(slow-release 감쇠용).
	float      SteerAngle    = 0.0f;   // 현재 조향각(forward 기준 deg). 목표각으로 slew 되는 상태값.
	bool       bUTurnActive = false;   // guidance 기반 generic U-turn 상태.
	int        UTurnExtraSlotIndex = -1; // 유턴 중 고정할 좌/우 ExtraSlot.
	bool       bJumpPerformed = false;   // 이번 점프 요청에 실제로 점프했는지 여부 (무한 점프 방지)
	float      JumpCandidateTime = 0.0f; // 현재 점프 후보가 연속으로 유지된 시간.

	// ── 평행이동(strafe) 상태 — SetStrafeMode 가 입력, UpdateStrafeMode 가 모드 전이 ──
	bool  bStrafeMode        = false;  // 현재 평행이동 모드 여부.
	bool  bGazeHeld          = false;  // '전방 주시' 키 홀드 여부(플레이어 입력).
	float StrafeLongitudinal = 0.0f;   // 종방향 입력([-1,1], +전진).
	float StrafeLateral      = 0.0f;   // 횡방향 입력([-1,1], +우측).
};
