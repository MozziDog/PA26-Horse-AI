#include "PawnMovementComponent.h"

#include "Object/Reflection/ObjectFactory.h"

// 입력 API 만 제공하는 중간 베이스 — 단독으로는 이동하지 않으므로 Add Component 목록에서 숨기기
HIDE_FROM_COMPONENT_LIST(UPawnMovementComponent)

void UPawnMovementComponent::AddInputVector(const FVector& WorldDirection, float ScaleValue)
{
	AccumulatedInput = AccumulatedInput + WorldDirection * ScaleValue;
}

void UPawnMovementComponent::ConsumeInputVector(FVector& OutAccumulated)
{
	OutAccumulated = AccumulatedInput;
	AccumulatedInput = FVector(0.0f, 0.0f, 0.0f);
}
