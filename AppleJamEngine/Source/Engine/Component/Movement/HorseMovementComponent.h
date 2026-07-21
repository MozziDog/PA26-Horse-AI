#pragma once

#include "PawnMovementComponent.h"
#include "Object/Ptr/WeakObjectPtr.h"

struct FHitResult;
class USkeletalMeshComponent;
class UAnimGraphInstance;

// 말 전용 이동 — root motion 구동. 수평 전진/선회는 애니메이션 root motion 이 만든다(발 미끄러짐 방지)
// Movement 는 (1) 입력을 AnimGraph 변수로 번역하고, (2) mesh AnimInstance 가 누적한 root motion 을
// 소비해 root(box) transform 에 적용하며, (3) 지면 스냅·몸통 충돌·낙하·점프 같은 physical 처리를 맡는다.
//
// AnimGraph 변수 (HorseAnimGraph):
//   float NormalizedSpeed  [0, 1] 최대 속도 대비 현재 속력
//   float SteeringAngle    초당 회전각 (deg/s, ±MaxTurnRate). 
//							Forward와 Desired 간의 오차를 YawAlignTime으로 나눠서 계산,
//							YawAlignTime에 걸쳐서 AI가 계산한 '가고 싶은 방향'에 도달할 수 있도록 함
//   float InclineAngle     [-1, +1] 내리막길/오르막길 각도 ( 현재 placeholder )
//   bool  bBrake           급정지는 Canter 이상에서만 사용
//   bool  bJump            
//   float AirTime          second 단위 공중에 머문 시간 (JumpUp→Falling 이행 판단용)
//
// yaw 회전은 root motion으로 처리: 애니메이션이 없는 각도의 회전은 자연스레 차단됨

#include "Source/Engine/Component/Movement/HorseMovementComponent.generated.h"

UENUM()
enum class EHorseMoveMode : uint8
{
	Grounded,   // 지면 위 — root motion XY + 지면 Z 스냅. Velocity.Z = 0.
	Sliding,    // 급경사(보행 불가) — 입력/root motion 무시, 경사면 접선으로 미끄러짐. 완경사 도달 시 Grounded.
	Falling,    // 공중 — gravity 적분 + 이륙 시점 수평 관성(ballistic). 착지 시 Grounded/Sliding 복귀.
};

UCLASS()
class UHorseMovementComponent : public UPawnMovementComponent
{
public:
	GENERATED_BODY()
	UHorseMovementComponent();
	~UHorseMovementComponent() override = default;

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void Serialize(FArchive& Ar) override;

	UFUNCTION(Pure, Category="HorseMovement")
	FVector GetVelocity() const { return Velocity; }
	UFUNCTION(Pure, Category="HorseMovement")
	float   GetForwardSpeed() const;
	UFUNCTION(Pure, Category="HorseMovement")
	bool    IsFalling() const { return MoveMode == EHorseMoveMode::Falling; }
	UFUNCTION(Pure, Category="HorseMovement")
	bool    IsSliding() const { return MoveMode == EHorseMoveMode::Sliding; }
	// root box가 지면 접점 위로 뜨는 높이
	UFUNCTION(Pure, Category="HorseMovement")
	float   GetStandHeight() const { return StandHeight; }
	// gallop(습보) 시의 최고 속도 — Locomotion 이 gait 목표속도를 NormalizedSpeed([0,1])로 환산할 때 분모.
	// root motion 이 실제 속도를 만들므로 여기서 속도를 강제하진 않는다(정규화 기준값일 뿐).
	UFUNCTION(Pure, Category="HorseMovement")
	float   GetMaxSpeed() const { return MaxSpeed; }
	// 낙하 중이면 가속 명령 수용 불가
	UFUNCTION(Pure, Category="HorseMovement")
	bool    CanAccelerate() const { return MoveMode != EHorseMoveMode::Falling; }

	// 점프 시작 요청
	// 도움닫기 + 점프 애니메이션을 시작한다. 실제 이륙은 AnimNotify_HorseJump → NotifyJumpTakeoff() 가 담당
	UFUNCTION(Callable, Category="HorseMovement")
	void    StartJump();
	// AnimNotify_HorseJump에 의해 콜백으로 호출
	// bWantJump를 통해 간접적으로 점프하므로 중복 호출도 문제 없음
	UFUNCTION(Callable, Category="HorseMovement")
	void    OnJumpNotify();
	// 급정지 요청 — 이 frame bBrake AnimGraph 변수를 켜고 목표 NormalizedSpeed 를 0 으로 끌어내린다.
	// Locomotion 이 막다른 벽 등에서 매 frame 호출한다(무입력만으로도 자연 감속은 됨).
	UFUNCTION(Callable, Category="HorseMovement")
	void	Brake();

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Max Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float MaxSpeed = 20.0f;             // m/s — NormalizedSpeed 정규화 기준(gallop 최고 속도)
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Ground Snap Max Step", Min=0.0f, Max=5.0f, Speed=0.01f)
	float GroundSnapMaxStep = 0.5f;    // 한 frame 스냅 허용 높이차. 넘게 벌어지면 낭떠러지 → 낙하
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Root Stand Height", Min=0.0f, Max=5.0f, Speed=0.01f)
	float StandHeight = 1.05f;         // root(=몸통 box 중심)가 지면 접점 위로 뜨는 높이. Mesh 는 이만큼 아래로 offset.

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Walkable Slope Z", Min=0.0f, Max=1.0f, Speed=0.01f)
	float WalkableSlopeZ = 0.7f;       // 지면 노멀 Z 가 이 값 미만이면 보행 불가 → Sliding. InclineAngle 정규화 한계이기도.
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Slide Friction", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SlideFriction = 1.5f;        // 1/s — 미끄러질 때 속도 감쇠율
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Slide Ground Probe", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SlideGroundProbe = 3.0f;     // m — 미끄러지는 동안 아래로 지면 탐색 거리(급강하 추적용, snap step 보다 크게)

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Torso Collision")
	bool  bTorsoCollision = true;      // 몸통 box(root) sweep 으로 벽/절벽 관통·비비기(rubbing climb) 차단
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Torso Skin", Min=0.0f, Max=1.0f, Speed=0.001f)
	float TorsoSkin = 0.05f;           // Torso box에 여유 줘서 벽 근처에서 지형과 캐릭터 메시 교차 방지

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Jump Speed", Min=0.0f, Max=30.0f, Speed=0.1f)
	float JumpSpeed = 5.8f;            // m/s — 점프 시 초기 상향 속도. h≈v²/2g (5.8m/s → 약 1.7m)


	UPROPERTY(Edit, Save, Category="HorseMovement|Steering", DisplayName="Yaw Align Time", Min=0.05f, Max=5.0f, Speed=0.01f)
	float YawAlignTime = 0.4f;         // 초 — 진행 방향을 heading으로 수렴시키는 시간, 작을수록 민첩
	UPROPERTY(Edit, Save, Category="HorseMovement|Steering", DisplayName="Max Turn Rate", Min=0.0f, Max=720.0f, Speed=1.0f)
	float MaxTurnRate = 205.0f;        // deg/s — 선회율 상한 ( NOTE: Turn 계통의 애니메이션과 맞춰야 함 )

protected:
	FVector GetGravity() const;

	// From 에서 아래로 raycast. WorldStatic 만 지면 후보. MaxDist<=0 이면 (StandHeight+GroundSnapMaxStep).
	bool TraceGround(const FVector& From, FHitResult& OutHit, float MaxDist = -1.0f) const;
	// 지면 노멀(면 노멀 우선, 없으면 shape 노멀, 그래도 없으면 +Z). 항상 정규화.
	FVector GroundNormal(const FHitResult& Hit) const;

	// mesh AnimInstance 가 이번 frame 누적한 root motion 을 소비해 world 이동(XYZ) + yaw 회전으로
	// 분해. 이동은 OutWorldDelta 로 반환(모드별 처리는 호출자), yaw 는 여기서 곧바로 root 에 적용.
	// 회전은 yaw only — 몸통 box 를 세운 채 방향만 돌린다(지면 pitch/roll 정렬은 후속 suspension).
	// 클립의 pitch/roll(swing)·Z bob 은 per-asset 옵션(UAnimSequence 의 RootRotationLock=YawOnly,
	// bExtractRootMotionZ=false)으로 pose 쪽에 남는다 — actor 는 기울지 않고 mesh 만 움직인다.
	void ConsumeRootMotion(FVector& OutWorldDelta);

	void TickGrounded(float DeltaTime, const FVector& WorldDelta);
	void TickSliding(float DeltaTime);
	void TickFalling(float DeltaTime);

	// bWantJump 소비 — 상향 임펄스(JumpSpeed) + 즉시 Falling 전환 + 착지 오인 방지용 살짝 띄우기.
	// TickGrounded 가 접지 frame 에서만 호출한다(CharacterMovementComponent 의 launch 패턴).
	void PerformJump();

	// FromLoc 에서 DeltaXY 만큼 몸통 box 를 sweep. 벽/급경사면에 막히면 히트 지점(skin 여유)까지로 줄인다.
	FVector ResolveTorsoCollision(const FVector& FromLoc, const FVector& DeltaXY);

	// 현재 지면 경사를 [-1,+1] InclineAngle 로. 부호: 오르막 +, 내리막 -. 크기: walkable 한계 대비.
	float ComputeInclineAngle(const FHitResult& Ground) const;

	// mesh 의 AnimGraph 인스턴스(변수 set 대상). 없으면 nullptr.
	UAnimGraphInstance* GetGraphInstance() const;
	// 이번 frame 계산한 AnimGraph 변수들을 mesh AnimInstance 에 push.
	void PushAnimGraphVariables();

	FVector        Velocity = FVector(0.0f, 0.0f, 0.0f);   // Z=중력/점프, XY=root motion 파생 관성(리포팅·이륙 관성)
	EHorseMoveMode MoveMode = EHorseMoveMode::Falling;     // 시작 시 지면 잡아야 해서 Falling

	// ── AnimGraph 로 내보낼 상태 ──
	float NormalizedSpeed = 0.0f;   // 이징된 현재 속도 스칼라([0,1])
	UPROPERTY(Edit, ReadOnly, Category = "Debug")	// 디버그 
	float TurnRate        = 0.0f;   // 이번 프레임에서의 초당 선회각
	float InclineAngle    = 0.0f;   // 이징된 경사([-1,1])
	float AirTime         = 0.0f;   // 공중 체류 시간(초)
	UPROPERTY(Edit, ReadOnly, Category="Debug")	// 디버그
	bool  bJumpActive     = false;  // 점프로 [이륙~착지] 동안 true. 비자발적 낙하와 구분 & 착지 리셋용
	bool  bJumpRequested  = false;  // 점프 애니메이션 요청. 'bJump' AnimInstance parameter와 동기화됨
	bool  bWantJump       = false;  // NotifyJumpTakeoff()에서 플래그 설정, TickGrounded()에서 소비, 점프.
	bool  bBrakeRequested = false;  // 이 frame Brake() 호출됨(tick 끝에서 소비 후 클리어)

	TWeakObjectPtr<USkeletalMeshComponent> Mesh = nullptr;
};
