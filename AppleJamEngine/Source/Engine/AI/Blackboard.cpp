#include "pch.h"
#include "Blackboard.h"

bool FBlackboard::TryGetBool(FName InKey, bool& OutValue)
{
	if (BoolData.contains(InKey))
	{
		OutValue = BoolData[InKey];
		return true;
	}
	return false;
}

void FBlackboard::SetBool(FName InKey, bool InValue)
{
	BoolData[InKey] = InValue;
}

bool FBlackboard::TryGetInt(FName InKey, int& OutValue)
{
	if (IntData.contains(InKey))
	{
		OutValue = IntData[InKey];
		return true;
	}
	return false;
}

void FBlackboard::SetInt(FName InKey, int InValue)
{
	IntData[InKey] = InValue;
}

bool FBlackboard::TryGetFloat(FName InKey, float& OutValue)
{
	if (FloatData.contains(InKey))
	{
		OutValue = FloatData[InKey];
		return true;
	}
	return false;
}

void FBlackboard::SetFloat(FName InKey, float InValue)
{
	FloatData[InKey] = InValue;
}

bool FBlackboard::TryGetVector(FName InKey, FVector& OutValue)
{
	if (VectorData.contains(InKey))
	{
		OutValue = VectorData[InKey];
		return true;
	}
	return false;
}

void FBlackboard::SetVector(FName InKey, FVector InValue)
{
	VectorData[InKey] = InValue;
}


