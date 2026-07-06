#pragma once
#include "Core/Types/CoreTypes.h"
#include "Object/FName.h"

class FBlackboard
{
private:
	TMap<FName, bool> BoolData;
	TMap<FName, int> IntData;
	TMap<FName, float> FloatData;
	TMap<FName, FVector> VectorData;
	// NOTE: actor나 component 등 추가 타입을 처리하려면 여기에 추가
public:
	bool TryGetBool(FName InKey, bool& OutValue);
	void SetBool(FName InKey, bool InValue);

	bool TryGetInt(FName InKey, int& OutValue);
	void SetInt(FName InKey, int InValue);

	bool TryGetFloat(FName InKey, float& OutValue);
	void SetFloat(FName InKey, float InValue);

	bool TryGetVector(FName InKey, FVector& OutValue);
	void SetVector(FName InKey, FVector InValue);
};