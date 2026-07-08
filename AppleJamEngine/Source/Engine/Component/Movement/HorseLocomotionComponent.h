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

	// 조향 입력(-1 ~ 1)
	UFUNCTION(Callable, Category="Locomotion|Steering")
	void SetSteeringInput(float Value);

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
	float GetGaitScaledSpeed() const; // 목표속도 / Movement MaxSpeed 를 [0,1] 로.
	void  ClampGaitToEnvelope();

	UHorseMovementComponent* Movement = nullptr;
	// BT가 Blackboard 에 쓴 DesiredGait 등을 읽고 움직임에 반영
	UBlackboardComponent* BlackboardComp = nullptr;

	// ── 조향 튜닝 ──
	UPROPERTY(Edit, Save, Category="Locomotion|Steering", DisplayName="Steer Strength", Min=0.0f, Max=4.0f, Speed=0.05f)
	float SteerStrength = 1.0f;   // steering(±1)이 목표 heading 을 forward 에서 옆으로 편향. 1.0 이면 ±45°.

	// ── gait별 목표 속도(m/s). Stop은 당연히 0 ──
	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Walk Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float WalkSpeed = 1.6f;
	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Trot Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float TrotSpeed = 3.5f;
	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Canter Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float CanterSpeed = 5.5f;
	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Gallop Speed", Min=0.0f, Max=50.0f, Speed=0.1f)
	float GallopSpeed = 8.0f;

	UPROPERTY(Edit, Save, Category="Locomotion|Gait", DisplayName="Gait Up Cooldown", Min=0.0f, Max=5.0f, Speed=0.01f)
	float GaitUpCooldown = 0.6f; 	// 가속 쿨타임(초 단위)

	// ── runtime states ──
	EHorseGait Gait     = EHorseGait::Stop;
	EHorseGait MinGait  = EHorseGait::Stop;
	EHorseGait MaxGait  = EHorseGait::Gallop;
	float      GaitUpTimer   = 0.0f;   // >0 이면 up-shift 대기 중.
	float      SteeringInput = 0.0f;
};
