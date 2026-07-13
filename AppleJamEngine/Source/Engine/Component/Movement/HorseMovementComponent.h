#pragma once

#include "PawnMovementComponent.h"

struct FHitResult;

// 말 전용 이동 — quadruped 라 capsule 전제가 없다(root 는 SkeletalMesh). collision body 없이
// down-raycast 로 지면에 스냅한다. 입력은 UPawnMovementComponent::AddInputVector(world 방향) 하나로
// 받고(플레이어/BT 공통), 내부에서 비홀로노믹하게 분해한다: 전진 성분 → 목표 속도, 좌우 성분 → yaw 조향.
// velocity 를 컴포넌트가 소유하므로 task 가 A→B 로 바뀌어도 이동이 끊기지 않는다.
//
// MVP 범위: 지면 높이 스냅 + yaw 조향 + 낙하. (지면 경사 pitch/roll 정렬·gait 상태머신은 후속 phase.)

#include "Source/Engine/Component/Movement/HorseMovementComponent.generated.h"

UENUM()
enum class EHorseMoveMode : uint8
{
	Grounded,   // 지면 위 — XY 이동 + 지면 Z 스냅. Velocity.Z = 0.
	Sliding,    // 급경사(보행 불가) — 입력 무시, 경사면 접선으로 미끄러짐. 완경사 도달 시 Grounded.
	Falling,    // 공중 — gravity 적분. 착지 시 Grounded(또는 급경사면 Sliding) 복귀.
};

UCLASS()
class UHorseMovementComponent : public UPawnMovementComponent
{
public:
	GENERATED_BODY()
	UHorseMovementComponent();
	~UHorseMovementComponent() override = default;

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
	// gallop(습보, 전력질주) 시의 최고 속도 — Locomotion 이 gait 목표속도를 scale([0,1])로 환산할 때 분모로 쓴다.
	UFUNCTION(Pure, Category="HorseMovement")
	float   GetMaxSpeed() const { return MaxSpeed; }
	// 낙하 중이면 가속 명령 수용 불가
	UFUNCTION(Pure, Category="HorseMovement")
	bool    CanAccelerate() const { return MoveMode != EHorseMoveMode::Falling; }

	// 점프 — 접지 상태에서만 수직 임펄스(JumpSpeed)를 주고 Falling 으로 전환. Locomotion 이 장애물
	// 회피 게이트에서 트리거한다(점프 여부/시점 판단은 Locomotion, 실제 도약은 Movement 소관).
	UFUNCTION(Callable, Category="HorseMovement")
	void    Jump();
	// 정지 - AddInputVector()를 호출하지 않으면 알아서 멈춘다.
	// 현재로써는 명시적으로 멈춘다고 표시하기 위한 Placeholder 역할
	UFUNCTION(Callable, Category = "HorseMovement")
	void	Brake();

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Max Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float MaxSpeed = 20.0f;             // m/s — gallop 최고 속도
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Max Acceleration", Min=0.0f, Max=100.0f, Speed=0.1f)
	float MaxAcceleration = 10.0f;     // m/s^2
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Braking Deceleration", Min=0.0f, Max=100.0f, Speed=0.1f)
	float BrakingDeceleration = 12.0f; // m/s^2 — 입력 없을 때 감속률
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Max Turn Rate", Min=0.0f, Max=720.0f, Speed=1.0f)
	float MaxTurnRate = 120.0f;        // deg/s — 조향 각속도
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Ground Snap Max Step", Min=0.0f, Max=5.0f, Speed=0.01f)
	float GroundSnapMaxStep = 0.5f;    // 한 frame 스냅 허용 높이차. 넘게 벌어지면 낭떠러지 → 낙하
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Root Stand Height", Min=0.0f, Max=5.0f, Speed=0.01f)
	float StandHeight = 1.05f;         // root(=몸통 box 중심)가 지면 접점 위로 뜨는 높이. Mesh 는 이만큼 아래로 offset.

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Min Turn Radius", Min=0.0f, Max=20.0f, Speed=0.05f)
	float MinTurnRadius = 2.0f;        // m — 이 반경보다 좁게는 못 돈다(저속 제자리회전 억제, 고속 광회전). 0=제한없음
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Turn Speed Floor", Min=0.0f, Max=10.0f, Speed=0.05f)
	float TurnSpeedFloor = 0.8f;       // m/s — 선회율 계산 시 속도 하한(정지 상태에서도 최소한의 pivot 허용)
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Walkable Slope Z", Min=0.0f, Max=1.0f, Speed=0.01f)
	float WalkableSlopeZ = 0.7f;       // 지면 노멀 Z 가 이 값 미만이면 보행 불가 → Sliding
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Slide Friction", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SlideFriction = 1.5f;        // 1/s — 미끄러질 때 속도 감쇠율
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Slide Ground Probe", Min=0.0f, Max=10.0f, Speed=0.05f)
	float SlideGroundProbe = 3.0f;     // m — 미끄러지는 동안 아래로 지면 탐색 거리(급강하 추적용, snap step 보다 크게)

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Torso Collision")
	bool  bTorsoCollision = true;      // 몸통 box(root) sweep 으로 벽/절벽 관통·비비기(rubbing climb) 차단
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Torso Skin", Min=0.0f, Max=1.0f, Speed=0.001f)
	float TorsoSkin = 0.05f;           // Torso box에 여유 줘서 벽 근처에서 지형과 캐릭터 메시 교차 방지

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Jump Speed", Min=0.0f, Max=30.0f, Speed=0.1f)
	float JumpSpeed = 5.8f;            // m/s — 점프 시 초기 상향 속도. h≈v²/2g (5m/s → 약 1.3m)

protected:
	FVector GetGravity() const;

	// From 에서 아래로 raycast. WorldStatic 만 지면 후보. MaxDist<=0 이면 (StandHeight+GroundSnapMaxStep).
	bool TraceGround(const FVector& From, FHitResult& OutHit, float MaxDist = -1.0f) const;
	// 지면 노멀(면 노멀 우선, 없으면 shape 노멀, 그래도 없으면 +Z). 항상 정규화.
	FVector GroundNormal(const FHitResult& Hit) const;

	// Grounded 전용 — 입력을 yaw 조향 + forward 목표속도(가감속)로 분해해 Velocity 갱신.
	// NOTE: Steering 로직은 임시 구현인 상태, 미세 조정 필요
	void ApplySteeringAndSpeed(const FVector& Desired, float DeltaTime);
	void TickGrounded(float DeltaTime);
	void TickSliding(float DeltaTime);
	void TickFalling(float DeltaTime);

	// FromLoc 에서 DeltaXY 만큼 몸통 box 를 sweep. 벽/급경사면(walkable 아님)에 막히면 이동을
	// 히트 지점(skin 여유)까지로 줄이고 Velocity 를 벽 접선으로 투영한다. 허용된 XY 이동을 반환.
	// walkable 면(램프)·무충돌이면 DeltaXY 그대로. box 는 발밑 TorsoBoxHeight 높이라 램프 바닥은 통과.
	FVector ResolveTorsoCollision(const FVector& FromLoc, const FVector& DeltaXY);

	FVector        Velocity = FVector(0.0f, 0.0f, 0.0f);
	EHorseMoveMode MoveMode = EHorseMoveMode::Falling; // 시작 시 지면 잡아야 해서 Falling
};
