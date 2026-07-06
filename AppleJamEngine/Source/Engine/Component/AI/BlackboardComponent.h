#pragma once

#include "Component/ActorComponent.h"
#include "AI/Blackboard.h"

#include "Source/Engine/Component/AI/BlackboardComponent.generated.h"

UCLASS()
class UBlackboardComponent : public UActorComponent
{
public:
	GENERATED_BODY();

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override { }

	FBlackboard& GetBlackboard() { return Blackboard; }
	const FBlackboard& GetBlackboard() const { return Blackboard; }

	// 패널이 프로퍼티를 읽기 직전에 호출 → DebugEntries 갱신 (패널 닫혀 있으면 호출 X)
	void PreGetEditableProperties() override;

private:
	FBlackboard Blackboard;

	// Details 패널 표시용 미러. 매 프레임 PreGetEditableProperties()에서 Blackboard의 정보로 업데이트
	UPROPERTY(Edit, ReadOnly, Transient, Category = "Blackboard", DisplayName = "Entries", Type = Array, Struct = FBlackboardDebugEntry)
	TArray<FBlackboardDebugEntry> DebugEntries;
};