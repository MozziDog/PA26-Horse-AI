#pragma once
#include "Core/Types/CoreTypes.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/AI/Blackboard.generated.h"

// Blackboard 내용물을 Details 패널에 노출시키기 위한 직렬화용 엔트리
USTRUCT()
struct FBlackboardDebugEntry
{
	GENERATED_BODY()

	UPROPERTY(Edit, ReadOnly, Category = "Blackboard", DisplayName = "Key")
	FName Key;

	UPROPERTY(Edit, ReadOnly, Category = "Blackboard", DisplayName = "Type")
	FName Type;

	UPROPERTY(Edit, ReadOnly, Category = "Blackboard", DisplayName = "Value")
	FString Value;
};

USTRUCT()
class FBlackboard
{
public:
	GENERATED_BODY()

	void Clear();

	bool TryGetBool(FName InKey, bool& OutValue) const;
	void SetBool(FName InKey, bool InValue);

	bool TryGetInt(FName InKey, int& OutValue) const;
	void SetInt(FName InKey, int InValue);

	bool TryGetFloat(FName InKey, float& OutValue) const;
	void SetFloat(FName InKey, float InValue);

	bool TryGetVector(FName InKey, FVector& OutValue) const;
	void SetVector(FName InKey, FVector InValue);

	// Details 패널 노출(디버그용): Blackboard의 내용물을 (Key, Type, Value) 문자열 엔트리로 평탄화
	void CollectDebugEntries(TArray<FBlackboardDebugEntry>& OutEntries) const;
private:
	TMap<FName, bool> BoolData;
	TMap<FName, int> IntData;
	TMap<FName, float> FloatData;
	TMap<FName, FVector> VectorData;
	// NOTE: actor나 component 등 추가 타입을 처리하려면 여기에 추가
};
