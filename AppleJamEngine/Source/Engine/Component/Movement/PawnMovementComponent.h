#pragma once

#include "MovementComponent.h"
#include "Math/Vector.h"

// 입력 벡터로 구동되는 이동 컴포넌트의 공통 베이스 (UE 의 UPawnMovementComponent 대응).
//
// 외부(Controller / BT task / Lua)는 매 frame AddInputVector 로 "이번 frame 에 가고 싶은 방향·세기"를
// 누적하기만 한다. 실제 velocity 적분·floor 처리 등은 자식 클래스(Character/Horse 등)가 자기 tick 에서
// ConsumeInputVector 로 한 번에 비우며 담당한다.
//
// consume 규약 덕분에 입력을 안 준 frame 은 입력이 0 이 되어(감속/정지가 자연스럽게 동작),
// "직전 task 입력이 남아 계속 움직이는" 문제가 구조적으로 생기지 않는다.
//
// 궤도/물리 기반(Projectile/Rotating/Pendulum 등) 이동은 입력을 쓰지 않으므로 이 계층이 아니라
// UMovementComponent 를 직접 상속한다.

#include "Source/Engine/Component/Movement/PawnMovementComponent.generated.h"

UCLASS()
class UPawnMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY()

	UPawnMovementComponent() = default;
	~UPawnMovementComponent() override = default;

	// 이번 frame 입력 누적. 자식의 TickComponent 가 ConsumeInputVector 로 비운다.
	UFUNCTION(Callable, Category="PawnMovement|Input")
	void AddInputVector(const FVector& WorldDirection, float ScaleValue = 1.0f);

	// 누적 입력을 반환하고 0 으로 비운다. 자식 tick 이 frame 당 1회 호출.
	void ConsumeInputVector(FVector& OutAccumulated);

	// 비우지 않고 현재 누적값만 확인 (read-only).
	UFUNCTION(Pure, Category="PawnMovement|Input")
	FVector GetPendingInputVector() const { return AccumulatedInput; }

protected:
	FVector AccumulatedInput = FVector(0.0f, 0.0f, 0.0f);
};
