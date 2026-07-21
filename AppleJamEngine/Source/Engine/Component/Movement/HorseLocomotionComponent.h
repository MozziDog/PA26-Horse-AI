#pragma once

#include "Component/ActorComponent.h"

class UHorseMovementComponent;
class UBlackboardComponent;

// 말 이동 제어 계층 — BT(모드 결정)와 Movement(actuation) 사이의 중간 계층.
// 책임 둘: (1) 조향 방향 산출(지금은 플레이어 steering, 후속 phase 에서 도로추종·장애물회피),
//          (2) 보법(gait) 상태머신 소유 — Stop→Walk→Trot→Canter→Gallop 를 한 단계씩 전환.
// Movement는 gait 단계 대신 여기서 채워넣은 InputVector만 보고 이동 속도와 선회반경 계산
#include "Source/Engine/Component/Movement/HorseLocomotionComponent.generated.h"

UENUM()
enum class EHorseGait : uint8
{
	Stop,     // 정지
	Walk,     // 평보
	Trot,     // 속보
	Canter,   // 구보
	Gallop,   // 습보(최고속)
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
	void SetGaitEnvelope(EHorseGait InMin, EHorseGait InMax);

	UFUNCTION(Pure, Category="Locomotion|Gait")
	EHorseGait GetGait() const { return Gait; }
	UFUNCTION(Pure, Category="Locomotion|Gait")
	float GetGaitTargetSpeed() const;   // 현재 gait 의 목표 속도(m/s)

protected:
	float GetGaitScaledSpeed() const; // 목표속도 / Movement MaxSpeed 를 [0,1] 로
	void UpdateGait();
	void  ClampGaitToEnvelope();

	TWeakObjectPtr<UHorseMovementComponent> Movement = nullptr;
	// BT가 Blackboard 에 쓴 DesiredGait 등을 읽고 움직임에 반영
	TWeakObjectPtr<UBlackboardComponent> BlackboardComp = nullptr;
	TWeakObjectPtr<UWorld> World = nullptr;		// 디버깅 시각화에 필요

	// ── context-steering 튜닝 : 장애물 회피 관련 ────────────────────────────────────────────────────────
	// 회피는 2단계로 구분: 
	// SafeDistance~HardBlockDistance 구간은 danger 수치 0 ~ 1 선형 증가 (soft penalty)
	// HardBlockDistance 이하는 danger=1 → 해당 slot 을 반드시 제외 (hard penalty)
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Safe Distance", Min=0.0f, Max=20.0f, Speed=0.05f)
	float SafeDistance = 2.0f;    // m — clearance 가 이 값부터 danger 가 붙기 시작(램프 상단).
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Hard Block Distance", Min=0.0f, Max=20.0f, Speed=0.05f)
	float HardBlockDistance = 0.8f;   // m — 이 값 이하 clearance 인 slot 은 danger=1 이며 절대 선택 안 함
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Danger Weight", Min=0.0f, Max=20.0f, Speed=0.05f)
	float DangerWeight = 3.0f;    // danger 가 interest 를 깎는 강도. interest 합보다 커야 실제로 회피한다.
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Danger Spread", Min=0.0f, Max=1.0f, Speed=0.02f)
	float DangerSpread = 0.5f;    // 이웃 slot 으로 번지는 danger 비율(0=번짐 없음). danger field 를 공간적으로 매끄럽게 해 판단 떨림 완화.
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Forward Lane Guard", Min=0.0f, Max=1.0f, Speed=0.02f)
	float ForwardLaneGuard = 1.0f;   // 정면 slot 에서 걷어낼 spread 오염 비율(1=완전 제거, 0=off). 통로 탈출 시 과민 조향(lurch) 억제.
	
	// ── context-steering 튜닝 : 유저 입력 관련 ──────────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="User Weight", Min=0.0f, Max=10.0f, Speed=0.05f)
	float UserWeight = 2.0f;      // 유저 입력 방향 interest 가중(최상위 — 우회 좌/우 tie-break).

	// ── context-steering 튜닝 : 도로 관련 ───────────────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Road Weight", Min=0.0f, Max=10.0f, Speed=0.05f)
	float RoadWeight = 1.0f;      // 도로 방향 interest 가중.
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Road User Yield", Min=0.0f, Max=1.0f, Speed=0.02f)
	float RoadUserYield = 1.0f;   // 유저 입력 강도에 비례해 도로 추종을 약화하는 비율(1=최대 입력에서 도로 방향 무시)
	// 도로에서 멀어질수록 추종 약화, 블랙보드에 RoadDist 없으면 RoadDist == INF으로 간주.
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Road Near Distance", Min=0.0f, Max=50.0f, Speed=0.05f)
	float RoadNearDistance = 3.0f;
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Road Far Distance", Min=0.0f, Max=50.0f, Speed=0.05f)
	float RoadFarDistance = 14.0f;

	// ── context-steering 튜닝 : 조향 떨림 방지 관련 ─────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Inertia Weight", Min=0.0f, Max=10.0f, Speed=0.05f)
	float InertiaWeight = 0.5f;   // 현재 진행(forward) 유지 관성 가중(최하위).
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Commit Weight", Min=0.0f, Max=10.0f, Speed=0.05f)
	float CommitWeight = 0.75f;   // 직전 선택 heading 을 유지하려는 히스테리시스. 좌/우 argmax 핑퐁(떨림) 억제.
	// danger persistence(fast-attack/slow-release): 
	// 회전 중 장애물이 sweep 경계를 들락거려 clearance 가 튈 때, danger를 천천히 감소시켜 조향 떨림을 억제,
	// danger의 증가는 장애물 회피 반응성 고려해서 즉시 반영되는 상태 유지
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Danger Persistence")
	bool  bDangerPersistence = true;
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Danger Release Rate", Min=0.0f, Max=20.0f, Speed=0.05f)
	float DangerReleaseRate = 3.0f;   // danger/sec — danger 가 내려갈 때 초당 감쇠량. 클수록 빨리 잊음(0=영구 유지).
	// 조향각 slew — 목표 heading(상대 조향각)이 튀어도 초당 SteerRateLimit 이하로만 바꿔 Movement 가 쫓는
	// 레퍼런스를 매끄럽게 한다. 장애물이 sweep 에 "나타나는" 순간의 잔여 떨림을 뭉갠다. 
	// 낮출수록 반응성↓·스무딩↑
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Smooth Steering")
	bool  bSmoothSteering = true;
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Steer Rate Limit", Min=0.0f, Max=720.0f, Speed=1.0f)
	float SteerRateLimit = 90.0f;     // 조향각 변화 상한치(deg/s) — 자연스러운 조향 변화 연출용

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
	float GaitUpCooldown = 0.6f; 	// 가속 쿨타임(초 단위)

	// ── 정면의 장애물이 설정된 거리 이하로 다가왔을 때 점프 동작 시작 ───────────────────────────────────
	UPROPERTY(Edit, Save, Category = "Locomotion|Jump", DisplayName = "Trot Jump Trigger Dist", Min = 0.0f, Max = 20.0f, Speed = 0.05f)
	float TrotJumpTriggerDist = 2.5f;
	UPROPERTY(Edit, Save, Category = "Locomotion|Jump", DisplayName = "Canter Jump Trigger Dist", Min = 0.0f, Max = 20.0f, Speed = 0.05f)
	float CanterJumpTriggerDist = 3.5f;
	UPROPERTY(Edit, Save, Category = "Locomotion|Jump", DisplayName = "Gallop Jump Trigger Dist", Min = 0.0f, Max = 20.0f, Speed = 0.05f)
	float GallopJumpTriggerDist = 5.0f;


	// ── runtime states ──────────────────────────────────────────────────────────────────────────────────
	static constexpr int MaxFanSlots = 8;   // PrevDanger 버퍼 상한. cpp 에서 ObsFanCount <= 이 값 검증.
	EHorseGait Gait     = EHorseGait::Stop;
	EHorseGait MinGait  = EHorseGait::Stop;
	EHorseGait MaxGait  = EHorseGait::Gallop;
	float      GaitUpTimer   = 0.0f;   // >0 이면 up-shift 대기 중.
	FVector    SteerDir      = FVector(0.0f, 0.0f, 0.0f);   // 직전 프레임에 선택한 회피 heading(커밋 히스테리시스용). 0=미초기화.
	float      PrevDanger[MaxFanSlots] = {};   // slot 별 직전 프레임 danger(slow-release 감쇠용).
	float      SteerAngle    = 0.0f;   // 현재 조향각(forward 기준 deg). 목표각으로 slew 되는 상태값.
	bool       bJumpPerformed = false;   // 이번 점프 요청에 실제로 점프했는지 여부 (무한 점프 방지)
};
