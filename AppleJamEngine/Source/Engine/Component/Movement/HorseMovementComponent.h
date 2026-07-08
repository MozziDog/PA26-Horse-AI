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
	Falling,    // 공중 — gravity 적분. 착지 시 Grounded 복귀.
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

	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Max Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float MaxSpeed = 8.0f;             // m/s — gallop 최고 속도
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Max Acceleration", Min=0.0f, Max=100.0f, Speed=0.1f)
	float MaxAcceleration = 10.0f;     // m/s^2
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Braking Deceleration", Min=0.0f, Max=100.0f, Speed=0.1f)
	float BrakingDeceleration = 12.0f; // m/s^2 — 입력 없을 때 감속률
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Max Turn Rate", Min=0.0f, Max=720.0f, Speed=1.0f)
	float MaxTurnRate = 120.0f;        // deg/s — 조향 각속도
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Gravity", Min=0.0f, Max=100.0f, Speed=0.1f)
	float Gravity = 9.8f;              // m/s^2 (양수 — 적용 시 Velocity.Z -= Gravity*dt)
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Ground Probe Up", Min=0.0f, Max=5.0f, Speed=0.01f)
	float GroundProbeUp = 0.5f;        // 몸통 위 이만큼에서 아래로 raycast 시작
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Ground Snap Max Step", Min=0.0f, Max=5.0f, Speed=0.01f)
	float GroundSnapMaxStep = 0.5f;    // 한 frame 스냅 허용 높이차. 넘게 벌어지면 낭떠러지 → 낙하
	UPROPERTY(Edit, Save, Category="HorseMovement", DisplayName="Foot Height Offset", Min=-5.0f, Max=5.0f, Speed=0.01f)
	float FootHeightOffset = 0.0f;     // root pivot ~ 발바닥 보정(지면 위 배치 높이)

protected:
	// From 에서 아래로 (GroundProbeUp + GroundSnapMaxStep) raycast. WorldStatic 만 지면 후보.
	bool TraceGround(const FVector& From, FHitResult& OutHit) const;

	// Grounded 전용 — 입력을 yaw 조향 + forward 목표속도(가감속)로 분해해 Velocity 갱신.
	void ApplySteeringAndSpeed(const FVector& Desired, float DeltaTime);
	void TickGrounded(float DeltaTime);
	void TickFalling(float DeltaTime);

	FVector        Velocity = FVector(0.0f, 0.0f, 0.0f);
	// 시작 시 지면 잡을 때까지 Falling — 첫 frame TickFalling 이 raycast 후 자동 Grounded 스냅.
	EHorseMoveMode MoveMode = EHorseMoveMode::Falling;
};
