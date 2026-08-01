#pragma once

#include "Component/Movement/PawnMovementComponent.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Quat.h"
#include "Object/Ptr/WeakObjectPtr.h"

struct FHitResult;
class USkeletalMeshComponent;
class UCapsuleComponent;
class UBoxComponent;
class UAnimGraphInstance;

// 말 전용 이동 — root motion 구동. 수평 전진/선회는 애니메이션 root motion 이 만든다(발 미끄러짐 방지)
// Movement 는 (1) 입력을 AnimGraph 변수로 번역하고, (2) mesh AnimInstance 가 누적한 root motion 을
// 소비해 root(box) transform 에 적용하며, (3) 지면 스냅·몸통 충돌·낙하·점프 같은 physical 처리를 맡는다.
//
// AnimGraph 변수 (HorseAnimGraph):
//   float NormalizedSpeed  [0, 1] 최대 속도 대비 현재 속력. 단, 평행이동 중엔 종방향 입력[-1,1]로 재사용(+전진).
//   bool  bStrafeMode      평행이동(전방 주시 홀드) 모드. true 면 strafe blend space 로 전환.
//   float LateralSpeed     [-1, 1] 평행이동 횡방향(+우측). 평행이동 아닐 땐 0.
//   float SteeringAngle    초당 회전각 (deg/s, ±MaxTurnRate).
//							Forward와 Desired 간의 오차를 YawAlignTime으로 나눠서 계산,
//							YawAlignTime에 걸쳐서 AI가 계산한 '가고 싶은 방향'에 도달할 수 있도록 함
//   float InclineAngle     [-1, +1] 내리막길/오르막길 각도. 접지 probe 로 추정한 앞뒤방향 경사
//   bool  bBrake           급정지는 Canter 이상에서만 사용
//   bool  bJump
//   bool  bRearing         전방 진행 불가로 급정지하는 순간 1회 pulse (뒷발서기+울음). 진입 에지에서만 true.
//   float AirTime          second 단위 공중에 머문 시간 (JumpUp→Falling 이행 판단용)
//
// yaw 회전은 root motion으로 처리: 애니메이션이 없는 각도의 회전은 자연스레 차단됨

#include "Source/Game/Horse/HorseMovementComponent.generated.h"

UENUM()
enum class EHorseMoveMode : uint8
{
	Grounded,   // 지면 위 — root motion XY + 지면 Z 스냅. Velocity.Z = 0.
	Sliding,    // 급경사(보행 불가) — 입력/root motion 무시, 경사면 접선으로 미끄러짐. 완경사 도달 시 Grounded.
	Falling,    // 낙하 — 접지를 잃었거나 점프 중. gravity 적분 + 이륙 시점 수평 관성(ballistic).
	            // 접지 상실로 진입한 경우엔 고속 강제 낙마 / 끼임 탈출 규칙이 함께 돈다.
};

// 현재 프레임의 지면 샘플링 데이터
// 접지점 위치는 actor 로컬(yaw 만 반영된 프레임) 기준 offset 으로 정의된다.
// roll은 계산하지 않는다 → 좌/우 probe 따로 두지 않음
struct FHorseGroundSample
{
	bool  bCenterHit = false;   // 무게중심 아래 sphere sweep 성공 = "밟을 게 있다" (= 접지 판정)
	bool  bFrontHit  = false;   // 앞발 중앙. 빗나가면 = 앞이 낭떠러지 → 실족(EdgeSlipSpeed)
	bool  bRearHit   = false;   // 뒷발 중앙

	// 각 probe 의 접점(world). Front/Rear 를 잇는 선이 곧 몸통 기울기의 기준이다.
	FVector CenterPoint = FVector(0.0f, 0.0f, 0.0f);
	FVector FrontPoint  = FVector(0.0f, 0.0f, 0.0f);
	FVector RearPoint   = FVector(0.0f, 0.0f, 0.0f);
	// 무게중심 접점의 면 노멀. 앞뒤 probe 가 둘 다 비었을 때 pitch 를 근사하는 용도로만 쓴다.
	// 보행 가능/불가(Sliding) 판정에는 쓰지 않는다 — IsWalkableSlope() 주석 참고.
	FVector CenterNormal = FVector(0.0f, 0.0f, 1.0f);

	// 앞뒤 접점을 잇는 선의 기울기 (actor forward 방향, 클 수록 오르막)
	float PitchSlope = 0.0f;
	// PitchSlope로 만든 평면 노멀(actor 로컬, roll=0) = 몸통 up 목표 방향
	FVector LocalUp = FVector(0.0f, 0.0f, 1.0f);
	// MaxBodyPitch로 제한한 '위' 방향 — 실제 몸통 기울기 목표
	FVector ClampedTiltUp = FVector(0.0f, 0.0f, 1.0f);

	// 발이 놓일 지면 높이(world Z) — '기울기 pivot 의 XY 위치' 에서의 추정 평면 높이.
	// 회전 pivot 과 Z 스냅 기준이 같은 점이어야 기울기가 바뀔 때 발이 지면을 뚫거나 뜨지 않는다.
	float SupportZ     = 0.0f;
	bool  bHasSupportZ = false;
};

// 접지 계산이 쓰는 기준 프레임 — actor pivot 에서 yaw 만 반영한 축과 발 평면 높이.
// 지면 probe/기울기 계산이 전부 이 프레임 위에서 돌아서, 함수마다 축을 다시 뽑지 않으려고 묶어 둔다.
struct FHorseLocalFrame
{
	FVector Origin  = FVector(0.0f, 0.0f, 0.0f);   // actor pivot(world)
	FVector Forward = FVector(1.0f, 0.0f, 0.0f);   // 수평 전방(단위)
	FVector Right   = FVector(0.0f, 1.0f, 0.0f);   // 수평 우측(단위)
	float   FootZ   = 0.0f;                        // 발 평면 높이(world Z) = Origin.Z - StandHeight

	// world 점 → (전방, 우측, 발 평면 대비 높이). Z 기준만 Origin 이 아니라 FootZ 인 점에 주의.
	FVector ToLocal(const FVector& World) const
	{
		const FVector D = World - Origin;
		return FVector(D.Dot(Forward), D.Dot(Right), World.Z - FootZ);
	}
	// pivot 기준 (전방, 우측, 높이) offset → world. 이쪽 Z 는 Origin 기준이다(root 로컬과 같은 규약).
	FVector OffsetToWorld(const FVector& RootLocal) const
	{
		return Origin + Forward * RootLocal.X + Right * RootLocal.Y + FVector(0.0f, 0.0f, RootLocal.Z);
	}
};

// 몸통 콜라이더의 world 스냅샷. 에디터 시각화와 어긋나지 않게 CapsuleComponent 값을 그대로 읽는다.
struct FHorseTorsoCapsule
{
	float   Radius   = 0.0f;
	float   HalfLen  = 0.0f;
	FVector Center   = FVector(0.0f, 0.0f, 0.0f);
	FQuat   Rotation = FQuat(0.0f, 0.0f, 0.0f, 1.0f);
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
	float   GetJumpSpeed() const { return JumpSpeed; }
	// 센서의 점프 궤적 예측용. 실제 TickFalling과 같은 world gravity를 반환한다.
	FVector GetJumpPredictionGravity() const { return GetGravity(); }
	UFUNCTION(Pure, Category="HorseMovement")
	bool    IsFalling() const { return MoveMode == EHorseMoveMode::Falling; }
	UFUNCTION(Pure, Category="HorseMovement")
	bool    IsSliding() const { return MoveMode == EHorseMoveMode::Sliding; }
	UFUNCTION(Pure, Category="HorseMovement")
	bool    IsGrounded() const { return MoveMode == EHorseMoveMode::Grounded; }
	// 끼임 탈출을 위해 몸통 충돌을 잠시 꺼둔 상태인지
	UFUNCTION(Pure, Category="HorseMovement")
	bool    IsEscapingStuck() const { return bEscapingStuck; }
	// actor pivot이 발 접지 평면 위로 뜨는 높이 (0 이면 pivot이 곧 지면 높이)
	UFUNCTION(Pure, Category="HorseMovement")
	float   GetStandHeight() const { return StandHeight; }
	// gallop(습보) 시의 최고 속도 — Locomotion 이 gait 목표속도를 NormalizedSpeed([0,1])로 환산할 때 분모.
	// root motion 이 실제 속도를 만들므로 여기서 속도를 강제하진 않는다(정규화 기준값일 뿐).
	UFUNCTION(Pure, Category="HorseMovement")
	float   GetMaxSpeed() const { return MaxSpeed; }
	// 낙하 중이면 가속 명령 수용 불가
	UFUNCTION(Pure, Category="HorseMovement")
	bool    CanAccelerate() const { return MoveMode != EHorseMoveMode::Falling; }
	// 현재 몸통에 적용 중인 지면 정렬 기울기(actor 로컬, yaw 제외). 카메라/IK 등이 참고 가능.
	UFUNCTION(Pure, Category="HorseMovement")
	FVector GetBodyUpVector() const { return BodyTilt.GetUpVector(); }
	// 센서용 실제 world-space 지면 노멀. 몸통 자세와 달리 roll 제거/각도 clamp를 적용하지 않는다.
	UFUNCTION(Pure, Category="HorseMovement")
	FVector GetCurrentGroundNormal() const { return CurrentGroundNormal; }

	// 점프 시작 요청
	// 도움닫기 + 점프 애니메이션을 시작한다. 실제 이륙은 AnimNotify_HorseJump → NotifyJumpTakeoff() 가 담당
	UFUNCTION(Callable, Category="HorseMovement")
	void    StartJump();
	// wind-up 중 점프 조건이 사라졌을 때 pending 요청을 취소한다.
	// 이미 이륙했거나 Notify가 실제 점프를 예약한 뒤에는 건드리지 않는다.
	UFUNCTION(Callable, Category="HorseMovement")
	void    CancelPendingJump();
	// AnimNotify_HorseJump에 의해 콜백으로 호출
	// bWantJump를 통해 간접적으로 점프하므로 중복 호출도 문제 없음
	UFUNCTION(Callable, Category="HorseMovement")
	void    OnJumpNotify();
	// 급정지 요청 — 이 frame bBrake AnimGraph 변수를 켜고 목표 NormalizedSpeed 를 0 으로 끌어내린다.
	// Locomotion 이 막다른 벽 등에서 매 frame 호출한다(무입력만으로도 자연 감속은 됨).
	UFUNCTION(Callable, Category="HorseMovement")
	void	Brake();
	// 평행이동(strafe) 입력 — Locomotion 이 매 frame 설정. bEnabled 면 선회 없이 종/횡 root motion 으로만
	// 이동하고 anim 은 strafe blend space 를 재생한다. Longitudinal(+전진)/Lateral(+우측) 모두 [-1,1].
	UFUNCTION(Callable, Category="HorseMovement")
	void	SetStrafeInput(bool bEnabled, float Longitudinal, float Lateral);
	// 자유낙하, 끼임, 피격 등 자세가 무너지는 상황 — 래그돌 + 강제 낙마 처리
	UFUNCTION(Callable, Category="HorseMovement")
	void    HandleCollapse();

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Max Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float MaxSpeed = 20.0f;             // m/s — NormalizedSpeed 정규화 기준(gallop 최고 속도)
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Ground Snap Max Step", Min=0.0f, Max=5.0f, Speed=0.01f)
	float GroundSnapMaxDown = 0.5f;    // 발밑으로 스냅해 내려갈 수 있는 최대 높이차. 넘게 벌어지면 낭떠러지 → 낙하
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Ground Snap Max Rise", Min=0.0f, Max=5.0f, Speed=0.01f)
	float GroundSnapMaxUp = 0.5f;    // 발 위로 끌어올려 스냅할 수 있는 최대 높이(계단·오르막 허용치).
									   // 이보다 높은 면은 밟을 지면이 아니라 벽으로 판정
									   // (스카이림식 암벽 등반 방지)
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Root Stand Height", Min=0.0f, Max=5.0f, Speed=0.01f)
	float StandHeight = 0.0f;		   // Actor pivot이 가지는 지면 대비 높이
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Walkable Slope Z", Min=0.0f, Max=1.0f, Speed=0.01f)
	float WalkableSlopeZ = 0.7f;       // 지면 노멀 Z 가 이 값 미만이면 보행 불가 → Sliding. InclineAngle 정규화 한계이기도.
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Slide Friction", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SlideFriction = 1.5f;        // 1/s — 미끄러질 때 속도 감쇠율

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Torso Collision")
	bool  bTorsoCollision = true;      // 몸통 Capsule sweep 으로 벽/절벽 관통·비비기 차단
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Torso Skin", Min=0.0f, Max=1.0f, Speed=0.001f)
	float TorsoSkin = 0.05f;           // Torso box에 여유 줘서 벽 근처에서 지형과 캐릭터 메시 교차 방지
	bool  bLegCollision = true;  // 무릎 높이에서 추가적인 Box sweep으로 
									   // '올라서기엔 높고 몸통 캡슐에 걸리기엔 낮은' 애매한 단차 관통 차단. 
									   // Grounded 이동에서만 동작.
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Step Block Wall Normal Z", Min=0.0f, Max=1.0f, Speed=0.01f)
	float StepBlockWallZ = 0.35f;      // 하체 box가 '오르막길'이 아니라 '장애물'로 보는 노멀 Z 상한(0.35 ≈ 70°).
	                                   // 이보다 완만한 면은 오르막으로 보고 통과시킨다
									   // NOTE: WalkableSlopeZ보다 작게 설정해야 경사면에 일단 올라가지고, 이후 미끄러짐

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Jump Speed", Min=0.0f, Max=30.0f, Speed=0.1f)
	float JumpSpeed = 5.8f;            // m/s — 점프 시 초기 상향 속도. h≈v²/2g (5.8m/s → 약 1.7m)

	UPROPERTY(Edit, Save, Category="HorseMovement|Rearing", DisplayName="Rear Min Speed", Min=0.0f, Max=1.0f, Speed=0.01f)
	float RearMinSpeed = 0.25f;        // 급정지 Rearing 발동 최소 정규화 속도. 이 미만으로 느리게 접근해 멈추면 뒷발서기 생략.
	UPROPERTY(Edit, Save, Category="HorseMovement|Rearing", DisplayName="Skid Friction", Min=0.0f, Max=20.0f, Speed=0.05f)
	float SkidFriction = 3.0f;         // 1/s — 뒷발서기 진입 시 관성 미끄러짐 감쇠율. 총 skid 거리 ≈ v0/SkidFriction.
	UPROPERTY(Edit, Save, Category="HorseMovement|Rearing", DisplayName="Skid Stop Speed", Min=0.0f, Max=5.0f, Speed=0.01f)
	float SkidStopSpeed = 0.3f;        // m/s — skid 관성이 이 속도 밑으로 떨어지면 미끄러짐 종료.

	UPROPERTY(Edit, Save, Category="HorseMovement|Steering", DisplayName="Yaw Align Time", Min=0.05f, Max=5.0f, Speed=0.01f)
	float YawAlignTime = 0.4f;         // 초 — 진행 방향을 heading으로 수렴시키는 시간, 작을수록 민첩
	UPROPERTY(Edit, Save, Category="HorseMovement|Steering", DisplayName="Max Turn Rate", Min=0.0f, Max=720.0f, Speed=1.0f)
	float MaxTurnRate = 205.0f;        // deg/s — 선회율 상한 ( NOTE: Turn 계통의 애니메이션과 맞춰야 함 )

	// ── 접지 판정 ────────────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="HorseMovement|Ground", DisplayName="Support Sphere Radius", Min=0.05f, Max=3.0f, Speed=0.01f)
	float SupportSphereRadius = 0.35f; // m — 무게중심 아래로 내리는 지면 판정 sphere 크기.
	                                   // 좁은 틈 지날 때 문제생기지 않게 ray 대신 sphere overlap(sweep) 사용
	UPROPERTY(Edit, Save, Category="HorseMovement|Ground", DisplayName="Support Sphere Forward", Min=-3.0f, Max=3.0f, Speed=0.01f)
	float SupportSphereForward = 0.0f; // m — actor pivot 기준 전방 offset. 실제 무게중심(가슴 쪽)에 맞춘다.
	UPROPERTY(Edit, Save, Category="HorseMovement|Ground", DisplayName="Front Probe Forward", Min=0.0f, Max=5.0f, Speed=0.01f)
	float FrontProbeForward = 0.45f;   // m — 앞발 raycast 지점의 전방 offset(actor pivot 기준). 좌우 중앙 1개뿐.
	UPROPERTY(Edit, Save, Category="HorseMovement|Ground", DisplayName="Front Probe Half Width", Min=0.0f, Max=2.0f, Speed=0.01f)
	float FrontProbeHalfWidth = 0.15f; // m — 좌/우 발 간격의 절반. probe 는 중앙 1줄만 쏘므로(roll 미계산)
	                                   // 지금은 디버그 드로우 폭에만 쓰인다. 다리 IK 가 붙으면 타겟 간격이 될 값.

	// 세 probe 가 공유하는 수직 탐색 범위(발 평면 기준).
	UPROPERTY(Edit, Save, Category="HorseMovement|Ground", DisplayName="Probe Up", Min=0.0f, Max=5.0f, Speed=0.01f)
	float ProbeUp = 1.20f;             // m — 발 높이 위로 이만큼에서 아래로 쏜다.
	                                   // tan(보행가능 최대경사) * FrontProbeForward 보다 커야 급경사 오르막에서
	                                   // ray/sweep 시작점이 지형 속에 들어가지 않는다.
	UPROPERTY(Edit, Save, Category="HorseMovement|Ground", DisplayName="Probe Down", Min=0.0f, Max=5.0f, Speed=0.01f)
	float ProbeDown = 1.00f;           // m — 발 높이 아래로 지면을 찾는 최대 깊이. 이보다 깊으면 '비었다'.

	// 뒷발 probe 위치. 앞발(FrontProbeForward)과 짝을 이뤄 몸통 pitch 를 만든다.
	// 접지 판정에는 관여하지 않는다 — 앞뒤 발이 다 떠도 무게중심만 지면 위면 접지 유지.
	UPROPERTY(Edit, Save, Category="HorseMovement|Ground", DisplayName="Rear Foot Back", Min=0.0f, Max=5.0f, Speed=0.01f)
	float RearFootBack = 0.65f;        // m — actor pivot 기준 뒷발 후방 offset

	// 무게중심은 지면 위인데 앞발 probe가 공중일 경우 실족 유도하도록 미는 속도
	// NOTE: 조작으로 탈출 여지를 주려면 보행 속도보다 충분히 느리게 세팅, 탈출 안되게 하려면 크게 설정
	UPROPERTY(Edit, Save, Category="HorseMovement|Ground", DisplayName="Edge Slip Speed", Min=0.0f, Max=5.0f, Speed=0.01f)
	float EdgeSlipSpeed = 1.0f;        // m/s 

	// ── 몸통 기울기(지면 정렬) ──
	UPROPERTY(Edit, Save, Category="HorseMovement|BodyTilt", DisplayName="Align Body To Ground")
	bool  bAlignBodyToGround = true;
	UPROPERTY(Edit, Save, Category="HorseMovement|BodyTilt", DisplayName="Max Body Pitch", Min=0.0f, Max=89.0f, Speed=0.5f)
	float MaxBodyPitch = 60.0f;        // deg — 종방향 기울기 한계(+오르막). 발산 방지용 안전 클램프.
	                                   // roll 은 항상 0 이라 대응하는 한계값이 없다(ClampTiltUp).
	UPROPERTY(Edit, Save, Category="HorseMovement|BodyTilt", DisplayName="Ground Align Speed", Min=0.1f, Max=60.0f, Speed=0.1f)
	float BodyAlignSpeed = 3.0f;      // 1/s — 접지 중 목표 기울기로 수렴하는 지수 감쇠율

	// 회전 중심은 '네 발이 만드는 접지면의 중앙, 지면 높이' 여야 한다.
	// pivot 이 지면보다 h 만큼 위에 있으면 기울기가 Δθ 바뀔 때 발이 수평으로 h·Δθ 만큼 쓸린다(미끄러짐).
	// 지면 높이에 두면 수평 이동이 d·(1-cosθ) 의 2차 항으로 떨어져 사실상 안 미끄러진다.
	UPROPERTY(Edit, Save, Category="HorseMovement|BodyTilt", DisplayName="Tilt Pivot Forward", Min=-3.0f, Max=3.0f, Speed=0.01f)
	float TiltPivotForward = 0.30f;    // m — 접지면 중앙의 전방 offset. 앞발/뒷발 접지점의 중간.
	                                   // 기본값은 무게중심 probe(SupportSphereForward)와 같게 둔다 —
	                                   // 두 값이 같으면 Z 스냅이 무게중심 접점 그대로라 평면 외삽 오차가 0 이다.
	                                   // 어긋나게 두면 그 거리만큼 추정 평면을 외삽해서 높이를 잡는다.
	UPROPERTY(Edit, Save, Category="HorseMovement|BodyTilt", DisplayName="Tilt Pivot Height", Min=-3.0f, Max=3.0f, Speed=0.01f)
	float TiltPivotHeight = 0.0f;      // m — 발 평면(= actor pivot - StandHeight) 기준 추가 높이. 0 이 정답이고,
	                                   // 일부러 위로 올려 미끄러짐이 생기는지 비교해볼 때만 건드린다.

	// ── Falling 관련 ───────────────────────────────────────────────────────
	UPROPERTY(Edit, Save, Category="HorseMovement|Physics", DisplayName="Forced Dismount Speed", Min=0.0f, Max=60.0f, Speed=0.1f)
	float ForcedDismountSpeed = 14.0f; // m/s — 물리 이동 중 이 속력을 넘으면 강제 낙마 + 래그돌
	UPROPERTY(Edit, Save, Category="HorseMovement|Physics", DisplayName="Physics Grace Time", Min=0.0f, Max=3.0f, Speed=0.01f)
	float PhysicsGraceTime = 0.35f;    // 초 — 진입 직후 이 시간 동안은 낙마/끼임 판정을 하지 않는다(작은 턱 넘기 보호)
	UPROPERTY(Edit, Save, Category="HorseMovement|Physics", DisplayName="Dismount During Jump")
	bool  bDismountDuringJump = false; // 의도한 점프(bJumpActive) 중에도 낙마 판정을 돌릴지

	UPROPERTY(Edit, Save, Category="HorseMovement|Physics", DisplayName="Stuck Detect Time", Min=0.0f, Max=5.0f, Speed=0.01f)
	float StuckDetectTime = 0.5f;      // 초 — '끼임' 조건이 이만큼 연속 유지되면 끼인 것으로 확정
	UPROPERTY(Edit, Save, Category="HorseMovement|Physics", DisplayName="Stuck Along Speed", Min=0.0f, Max=5.0f, Speed=0.01f)
	float StuckAlongSpeed = 0.05f;     // m/s — 마지막 조작 방향 속도가 이 이하면 '앞으로 못 나감'
	UPROPERTY(Edit, Save, Category="HorseMovement|Physics", DisplayName="Stuck Max Speed", Min=0.0f, Max=10.0f, Speed=0.01f)
	float StuckMaxSpeed = 1.0f;        // m/s — 전체 속력이 이보다 크면 자유낙하/미끄러짐 중으로 보고 끼임 판정 제외
	                                   // (원안에는 없는 가드. 없으면 수직 자유낙하가 끼임으로 오판된다)
	UPROPERTY(Edit, Save, Category="HorseMovement|Physics", DisplayName="Stuck Escape Time", Min=0.0f, Max=5.0f, Speed=0.01f)
	float StuckEscapeTime = 1.0f;      // 초 — 끼임 확정 후 몸통 충돌을 꺼두는 시간
	UPROPERTY(Edit, Save, Category="HorseMovement|Physics", DisplayName="Stuck Escape Speed", Min=0.0f, Max=10.0f, Speed=0.01f)
	float StuckEscapeSpeed = 2.0f;     // m/s — 탈출 시 마지막 조작 방향으로 밀어주는 속도

	UPROPERTY(Edit, Category="HorseMovement|Debug", DisplayName="Draw Ground Probes")
	bool  bDrawGroundProbes = false;   // 접지 sphere / 앞다리 ray / 추정 평면 디버그 드로우

protected:
	// ── 기본 질의 ────────────────────────────────────────────────────────────
	FVector GetGravity() const;
	// actor 의 수평 기저(전방/우측). Z 를 버리고 정규화하며, 퇴화 시 world 축으로 대체한다.
	void GetPlanarBasis(FVector& OutForward, FVector& OutRight) const;
	// 주어진 pivot 위치에서의 접지 계산용 기준 프레임.
	FHorseLocalFrame MakeLocalFrame(const FVector& PivotLoc) const;
	// 몸통 콜라이더의 world 스냅샷. 콜라이더가 없으면 false — 호출자는 몸통 충돌을 skip 한다.
	bool GetTorsoCapsule(FHorseTorsoCapsule& OutTorso) const;
	// mesh 의 AnimGraph 인스턴스(변수 set 대상). 없으면 nullptr.
	UAnimGraphInstance* GetGraphInstance() const;

	// ── 지면 trace ───────────────────────────────────────────────────────────
	// 발 높이(= ActorLocation.Z - StandHeight) 기준 위아래로 지면 raycast. WorldStatic 만 지면 후보.
	// NOTE: OutHit.Distance 는 pivot 이 아니라 위로 띄운 시작점 기준!!!
	//       높이 판정에는 Distance 대신 WorldHitLocation.Z 를 발 높이와 비교해서 쓸 것.
	bool TraceGround(const FVector& PivotLoc, FHitResult& OutHit) const;
	// 임의 지점(world XY) 아래로 지면 raycast. 시작 높이는 FootZ + Up, 최대 길이는 Up + Down.
	bool TraceGroundAt(const FVector& ProbeXY, float FootZ, float Up, float Down, FHitResult& OutHit) const;
	// 지면 노멀(면 노멀 우선, 없으면 shape 노멀, 그래도 없으면 +Z). 항상 정규화.
	FVector GroundNormal(const FHitResult& Hit) const;
	// 무게중심 수직 아래로 sphere 를 훑어 접점을 찾는다 = 접지 판정.
	// raycast 가 아니라 sphere 인 이유는 바위 사이 좁은 틈에서 ray 가 틈으로 빠지지 않게 하기 위해서다.
	bool ProbeCenterOfMass(const FVector& PivotLoc, FHitResult& OutHit) const;

	// ── 지면 샘플링 ────────────────────────────────────────────────────────────
	// 무게중심 sphere sweep + 앞발/뒷발 raycast 를 모아 한 프레임의 지면 표본을 만든다.
	// 반환값 = 이 샘플로 '접지 상태' 를 유지할 수 있는가 (= 무게중심 sphere 가 지면을 잡았는가).
	// 앞발이 허공인지(Sample.bFrontHit)는 접지가 아니라 실족 처리로 이어진다.
	bool SampleGround(const FVector& PivotLoc, FHorseGroundSample& OutSample) const;
	// probe 3발을 쏴서 접점/노멀만 채운다(기울기 계산 전).
	void ProbeGroundContacts(const FHorseLocalFrame& Frame, FHorseGroundSample& OutSample) const;
	// 접점 중 가장 앞/가장 뒤를 골라 로컬 좌표로 돌려준다. 하나도 없으면 false.
	bool FindPitchAnchors(const FHorseGroundSample& Sample, const FHorseLocalFrame& Frame,
		FVector& OutRearmost, FVector& OutFrontmost) const;
	// 앞뒤 anchor 로 PitchSlope / LocalUp / ClampedTiltUp / SupportZ 를 채운다.
	void SolveGroundPitch(const FHorseLocalFrame& Frame, FHorseGroundSample& OutSample) const;
	// 이 표본 위에서 걸을 수 있는가(= Sliding 으로 넘어가지 않아도 되는가).
	// 접지 유지(TickGrounded)와 착지(TickFalling)가 반드시 같은 답을 내야 해서 한 곳에 모아 둔다.
	bool IsWalkableSlope(const FHorseGroundSample& Sample) const;
	// world 지면 노멀 → actor 로컬(yaw 만 반영된 프레임) 노멀.
	FVector LocalizeGroundNormal(const FVector& WorldNormal) const;
	// 로컬 노멀에서 roll 을 버리고 pitch 만 MaxBodyPitch 한계로 잘라 몸통 기울기 목표로 만든다.
	FVector ClampTiltUp(const FVector& LocalUp) const;
	// 현재 지면 경사를 [-1,+1] InclineAngle 로. 부호: 오르막 +, 내리막 -. 크기: walkable 한계 대비.
	// probe 표본이 있으면 종방향 기울기(PitchSlope)를, 없으면 지면 노멀 하나로 근사한다.
	float ComputeInclineAngle(const FHitResult& Ground) const;
	float ComputeInclineAngle(const FHorseGroundSample& Sample) const;

	// ── 몸통 기울기 ──────────────────────────────────────────────────────────
	// 표본의 LocalUp 으로 목표 기울기를 만들어 BodyTilt 를 수렴시킨다. bGrounded=false 면 수평으로 복귀.
	void UpdateBodyTilt(float DeltaTime, const FHorseGroundSample& Sample);
	// BodyTilt 를 mesh + 하체 box에 반영(TiltPivot 기준 회전).
	// root(=actor)의 기울기는 건드리지 않음 — 기존 root가 yaw만 움직인다는 것을 전제로한 기능들 보존 (이동/지면 스냅/sweep 등)
	void ApplyBodyTilt();
	// 회전하는 컴포넌트들 대상으로 초기 상태의 relative transform 보관, 매 프레임 회전 시 기준으로 사용
	void CacheTiltBases();
	// 단일 컴포넌트 대상으로 Pivot 기준으로 회전 + 위치까지 업데이트
	void TiltComponent(USceneComponent* Comp, const FQuat& BaseRotation, const FVector& BaseLocation,
		const FVector& Pivot) const;
	// 기울기 회전 중심(root 로컬). Z 는 발 평면(= -StandHeight) 기준.
	FVector GetTiltPivotLocal() const;
	// BodyTilt 를 (전방기울기, 우측기울기) slope 쌍으로 되돌린다. 파고듦 계산용.
	void GetBodyTiltSlopes(float& OutPitchSlope, float& OutRollSlope) const;

	// ── 입력 → AnimGraph 상태 ────────────────────────────────────────────────
	// pending 입력을 소비해 NormalizedSpeed / LateralSpeed / TurnRate / rearing pulse 를 만든다.
	void UpdateMoveInput();
	// 평행이동 입력 처리 — 선회 없이 종/횡 성분만.
	void UpdateStrafeInput(const FVector& InForward, const FVector& InRight);
	// 일반 보행 입력 처리 — 목표 속도 + 급정지/뒷발서기 게이팅 + 조향각.
	void UpdateSteeringInput(const FVector& InDesired, const FVector& InForward);
	// 이번 frame 계산한 AnimGraph 변수들을 mesh AnimInstance 에 push.
	void PushAnimGraphVariables();

	// mesh AnimInstance가 이번 frame 누적한 root motion 을 소비해서
	// 이동은 OutWorldDelta 로 반환(모드별 처리는 호출자), yaw 는 여기서 곧바로 root 에 적용,
	// pitch/roll은 버림 (root motion 대신 지면 정렬(BodyTilt)이 mesh 쪽에서 담당)
	// Mesh 의 relative location 을 반영하므로 actor pivot은 임의 위치로 설정 가능
	// NOTE: 클립의 pitch/roll(swing)·Z bobbing 은 root motion이 아니라 pose로 처리,
	//       UAnimSequence의 옵션 (RootRotationLock, bExtractRootMotionZ) 참고
	void ConsumeRootMotion(FVector& OutWorldDelta, FQuat& OutYawTwist);

	// ── 모드별 tick ──────────────────────────────────────────────────────────
	void TickGrounded(float DeltaTime, const FVector& WorldDelta, const FQuat& RootYawTwist);
	void TickSliding(float DeltaTime, const FQuat& RootYawTwist);
	void TickFalling(float DeltaTime);	// 끼임 탈출도 Falling에서 처리함

	// ── Grounded 세부 ────────────────────────────────────────────────────────
	// 이번 frame 실제로 적용할 수평 이동량. root motion / skid 관성 중 하나를 고르고 실족 밀기를 얹는다.
	FVector ConsumeGroundedMoveXY(float DeltaTime, const FVector& WorldDelta);
	// 몸통 충돌(전진 sweep + 겹침 해소)을 계산한 뒤 위치와 yaw를 반환
	TPair<FVector, FQuat> CalculateSafeMovement(float DeltaTime, FVector Loc, const FVector& MoveXY, const FQuat& RootYawTwist);
	// 표본 높이로 Z 스냅. step 범위를 벗어나면(낭떠러지) false — 호출자가 낙하로 넘긴다.
	bool TrySnapToGround(const FHorseGroundSample& Sample, FVector& Loc);
	// 접지 유지가 확정된 뒤의 상태 갱신 — 경사 이징, 몸통 기울기, 다음 frame 용 표식들.
	void UpdateGroundedState(float DeltaTime, const FHorseGroundSample& Sample);
	// 급경사에서 이동이 발생 → 미끄러짐 모드로.
	void EnterSliding();

	// ── Sliding / Falling 세부 ───────────────────────────────────────────────
	// 중력의 경사면 접선 성분으로 가속한 뒤 마찰 감쇠.
	void ApplySlideAcceleration(float DeltaTime, const FVector& GroundN);
	// 미끄러짐 중의 경사/기울기 갱신 + 접선 투영 + 완경사 도달 시 보행 복귀.
	void UpdateSlidingState(float DeltaTime, const FHitResult& Ground);
	// 접지 상실 → Physics 모드 진입. bFromJump 면 의도한 점프라 낙마 규칙을 유예한다.
	void EnterFalling(bool bFromJump);
	// 착지 확정 — Z 스냅 + 공중 상태 리셋 + 경사에 따라 Grounded/Sliding 결정.
	void Land(FVector Loc, float TargetZ, const FHorseGroundSample& Sample);

	// ── 낙마 / 끼임 ──────────────────────────────────────────────────────────
	// 고속 낙마 / 끼임 판정. Physics 모드에서만 호출.
	void EvaluateDismountRules(float DeltaTime);
	// 마지막 조작 방향으로 전혀 나아가지 못하는 상태인지(= 끼임 후보).
	bool IsWedged(float Speed) const;
	// 낙마 연출 위임 — HorseRagdollTestComponent 가 붙어 있으면 래그돌을 켠다. 켜졌으면 true.
	bool TryTriggerRagdoll();
	// 끼임 확정 — 몸통 충돌을 잠시 내리고 마지막 조작 방향으로 밀어낸다(+ 낙마).
	void BeginStuckEscape();
	void UpdateStuckEscape(float DeltaTime);
	void EndStuckEscape();

	// bWantJump 소비 — 상향 임펄스(JumpSpeed) + 즉시 Falling 전환 + 착지 오인 방지용 살짝 띄우기.
	// TickGrounded 가 접지 frame 에서만 호출한다(CharacterMovementComponent 의 launch 패턴).
	void PerformJump();

	// ── 몸통 충돌 ────────────────────────────────────────────────────────────
	// NOTE: 판정 기준은 캡슐의 '현재' world transform 이다. actor 를 아직 옮기지 않은 시점에 부르므로
	//       이번 frame 이미 확정한 이동분이 있다면 호출자가 넘겨서 보정해야 한다(PendingMoveXY).
	// 이동하려는 방향에 장애물이 있는지 판단, 있다면 장애물에 겹치지 않을 만큼만 이동
	FVector ResolveTorso(const FVector& DeltaXY);
	// 몸통 앞부분만 잘라낸 캡슐로 진행 방향 sweep. 엉덩이가 벽에 스쳤다고 전진이 막히지 않게 한다.
	bool SweepTorsoFront(const FHorseTorsoCapsule& Torso, const FVector& DeltaXY, FHitResult& OutHit) const;
	// 벽 hit → 이번 frame 허용 이동량. skin 만 남기고 자르고 벽 노멀 방향 velocity 성분을 제거한다.
	FVector ClipMoveAtWall(const FVector& DeltaXY, const FHitResult& Hit);

	// 하체 box(LegCollider) sweep — '올라서기엔 높고 몸통 캡슐에 걸리기엔 낮은' 단차를 벽으로 취급한다.
	// box 밑면보다 낮은 지형은 그대로 통과 = 다리 사이로 지나간다(맨홀 턱, 낮은 통나무 등).
	// TickGrounded 경로에서만 호출되므로 낙하/미끄러짐 중엔 자동으로 꺼진다 —
	// 발이 구덩이에 빠져 낙하로 넘어가는 흐름을 이 box 가 막지 않는다.
	FVector ResolveLegBox(const FVector& DeltaXY);
	// 제자리 회전 등, 몸통이 장애물과 겹치는 상황 발생했을 때 수평방향으로 밀어내어 겹침 해소
	// solve 횟수는 최대 MaxDenetrationIter까지만, 그 후에도 겹쳐있다면 겹쳐진대로 방치 (완전 분리 보장 X)
	// PendingMoveXY = 이번 frame 전진분(ResolveTorso 결과). 캡슐은 아직 그만큼 안 움직였으므로
	// 여기서 더해 '이동 후' 위치에서 겹침을 푼다 — 안 더하면 한 frame 전 위치를 푸는 셈이 된다.
	FVector DepenetrateTorso(const FVector& PendingMoveXY, bool& bOutFullySolved);
	// 하체 box 겹침 해소. 제자리 회전(root motion yaw)은 sweep 을 거치지 않아 상자가 단차에
	// 파고들 수 있고, 겹친 뒤에는 MTD 노멀만으로 판단해야 해서 판정이 헐거워진다 → 매 frame 풀어 준다.
	FVector DepenetrateLegBox(const FVector& PendingMoveXY);
	// 겹침 1회분 해소량. 더 밀어낼 게 없으면 false 로 반복을 끝낸다.
	// SkipNormalZ = 이 값 이상의 노멀 Z(=올라갈 수 있는 면)는 밀어내지 않는다.
	bool SolvePenetrationXY(const FCollisionShape& Shape, const FVector& Center, const FQuat& Rotation,
		float SkipNormalZ, FVector& OutStep) const;

	// ── 디버그 드로우 ────────────────────────────────────────────────────────
	void DrawGroundProbeDebug(const FVector& PivotLoc, const FHorseGroundSample& Sample) const;
	// probe 자체 — 무게중심 sphere, 앞뒤 ray, 그 둘을 잇는 pitch 기준선.
	void DrawProbeDebug(const FHorseLocalFrame& Frame, const FHorseGroundSample& Sample) const;
	// 기울기 결과 — 회전 pivot, 현재 적용 중인 발 평면, 목표 up 벡터.
	void DrawBodyTiltDebug(const FHorseLocalFrame& Frame, const FHorseGroundSample& Sample) const;

	FVector        Velocity = FVector(0.0f, 0.0f, 0.0f);   // Z=중력/점프, XY=root motion 파생 관성(리포팅·이륙 관성)
	EHorseMoveMode MoveMode = EHorseMoveMode::Falling;     // 시작 시 지면 잡아야 해서 낙하
	FVector CurrentGroundNormal = FVector::UpVector;        // 마지막 유효 지면의 unclamped world-space normal

	// ── AnimGraph 로 내보낼 상태 ──
	float NormalizedSpeed = 0.0f;   // 이징된 현재 속도 스칼라([0,1]). 평행이동 중엔 종방향 입력[-1,1].
	float LateralSpeed    = 0.0f;   // 평행이동 횡방향([-1,1], +우측). 평행이동 아닐 땐 0.
	float TurnRate        = 0.0f;   // 이번 프레임에서의 초당 선회각
	float InclineAngle    = 0.0f;   // 이징된 경사([-1,1])
	float AirTime         = 0.0f;   // 물리 기반 이동 체류 시간(초)
	bool  bJumpActive     = false;  // 점프로 [이륙~착지] 동안 true. 비자발적 낙하와 구분 & 착지 리셋용
	bool  bJumpRequested  = false;  // 점프 애니메이션 요청. 'bJump' AnimInstance parameter와 동기화됨
	bool  bWantJump       = false;  // NotifyJumpTakeoff()에서 플래그 설정, TickGrounded()에서 소비, 점프.
	bool  bBrakeRequested = false;  // 이 frame Brake() 호출됨(tick 끝에서 소비 후 클리어)
	bool  bWasBraking     = false;  // 직전 frame brake 상태 — 급정지 진입 에지(rising) 검출용
	bool  bRearingRequested = false;  // 급정지 진입 에지에서만 1 frame true. 'bRearing' AnimGraph pulse

	// 뒷발서기 등으로 root motion 이 끊길 때 이어받는 관성 skid. rearing 에지에서 켜지고 마찰로 감쇠.
	FVector SkidVelocity = FVector(0.0f, 0.0f, 0.0f);
	bool    bSkidding    = false;

	// ── 평행이동(strafe) 입력 — SetStrafeInput 이 매 frame 설정 ──
	bool  bStrafeMode        = false;
	float StrafeLongitudinal = 0.0f;   // 종방향 입력([-1,1], +전진)
	float StrafeLateral      = 0.0f;   // 횡방향 입력([-1,1], +우측)

	// ── 지면 정렬 상태 ──
	FQuat   BodyTilt        = FQuat(0.0f, 0.0f, 0.0f, 1.0f);   // actor 로컬(yaw 제외) 몸통 기울기
	FQuat   LastAppliedTilt = FQuat(0.0f, 0.0f, 0.0f, 1.0f);   // 마지막으로 mesh 에 써넣은 값(중복 write 방지)
	bool    bTiltApplied    = false;
	FQuat   MeshBaseRotation= FQuat(0.0f, 0.0f, 0.0f, 1.0f);   // BeginPlay 시점의 mesh relative rotation
	FVector MeshBaseLocation= FVector(0.0f, 0.0f, 0.0f);   // BeginPlay 시점의 mesh relative location
	bool    bMeshBaseCached = false;
	FQuat   LegBoxBaseRotation = FQuat(0.0f, 0.0f, 0.0f, 1.0f);   // 기울기 0 일 때의 하체 box relative transform
	FVector LegBoxBaseLocation = FVector(0.0f, 0.0f, 0.0f);
	bool    bLegBoxBaseCached  = false;
	// 직전 표본이 '보행 불가 경사' 였는지. 실제 Sliding 이행은 다음 이동이 발생하는 frame 에 한다.
	bool    bSteepGround    = false;
	// 직전 표본에서 앞발 probe 가 허공이었는지 = 낭떠러지에 앞발을 걸친 상태.
	// bSteepGround 와 같은 규약으로, 실제 실족 밀기는 다음 frame 이동에 얹는다.
	bool    bEdgeSlipping   = false;

	// ── Falling 상태 관련 ──
	FVector LastInputDirXY  = FVector(0.0f, 0.0f, 0.0f);   // 마지막 유효 조작 방향(수평, 정규화)
	float   StuckTime       = 0.0f;                        // 끼임 조건 연속 유지 시간
	float   EscapeTimer     = 0.0f;                        // 남은 끼임 탈출 시간
	bool    bEscapingStuck  = false;
	bool    bEscapeOwnsCollision = false;                  // 캡슐 collision 을 우리가 껐는지(래그돌이 껐으면 false)
	bool    bRagdollTookOver = false;                      // 낙마 처리를 래그돌 컴포넌트가 받아갔는지
	ECollisionEnabled SavedCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool    bCollapseRequested = false;                    // 이번 물리 이동 중 이미 낙마 처리했는지

	TWeakObjectPtr<USkeletalMeshComponent> Mesh = nullptr;
	// 몸통 콜라이더. Root component와는 별개
	TWeakObjectPtr<UCapsuleComponent> BodyCollider = nullptr;
	// 하체 콜라이더 — 몸통이랑 부딪히기엔 낮고 step up하기에는 높은 대략 무릎 높이의 애매한 단차 차단용
	TWeakObjectPtr<UBoxComponent> LegCollider = nullptr;
};
