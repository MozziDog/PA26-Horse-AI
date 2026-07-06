#include "pch.h"
#include "Blackboard.h"

#include <string>

void FBlackboard::Clear()
{
	BoolData.clear();
	IntData.clear();
	FloatData.clear();
	VectorData.clear();
}

bool FBlackboard::TryGetBool(FName InKey, bool& OutValue) const
{
	if (auto It = BoolData.find(InKey); It != BoolData.end())
	{
		OutValue = It->second;
		return true;
	}
	return false;
}

void FBlackboard::SetBool(FName InKey, bool InValue)
{
	BoolData[InKey] = InValue;
}

bool FBlackboard::TryGetInt(FName InKey, int& OutValue) const
{
	if (auto It = IntData.find(InKey); It != IntData.end())
	{
		OutValue = It->second;
		return true;
	}
	return false;
}

void FBlackboard::SetInt(FName InKey, int InValue)
{
	IntData[InKey] = InValue;
}

bool FBlackboard::TryGetFloat(FName InKey, float& OutValue) const
{
	if (auto It = FloatData.find(InKey); It != FloatData.end())
	{
		OutValue = It->second;
		return true;
	}
	return false;
}

void FBlackboard::SetFloat(FName InKey, float InValue)
{
	FloatData[InKey] = InValue;
}

bool FBlackboard::TryGetVector(FName InKey, FVector& OutValue) const
{
	if (auto It = VectorData.find(InKey); It != VectorData.end())
	{
		OutValue = It->second;
		return true;
	}
	return false;
}

void FBlackboard::SetVector(FName InKey, FVector InValue)
{
	VectorData[InKey] = InValue;
}

void FBlackboard::CollectDebugEntries(TArray<FBlackboardDebugEntry>& OutEntries) const
{
	OutEntries.clear();
	OutEntries.reserve(BoolData.size() + IntData.size() + FloatData.size() + VectorData.size());

	for (const auto& [Key, Value] : BoolData)
	{
		FBlackboardDebugEntry Entry;
		Entry.Key   = Key;
		Entry.Type  = FName("Bool");
		Entry.Value = Value ? "true" : "false";
		OutEntries.push_back(std::move(Entry));
	}
	for (const auto& [Key, Value] : IntData)
	{
		FBlackboardDebugEntry Entry;
		Entry.Key   = Key;
		Entry.Type  = FName("Int");
		Entry.Value = std::to_string(Value);
		OutEntries.push_back(std::move(Entry));
	}
	for (const auto& [Key, Value] : FloatData)
	{
		FBlackboardDebugEntry Entry;
		Entry.Key   = Key;
		Entry.Type  = FName("Float");
		Entry.Value = std::to_string(Value);
		OutEntries.push_back(std::move(Entry));
	}
	for (const auto& [Key, Value] : VectorData)
	{
		FBlackboardDebugEntry Entry;
		Entry.Key   = Key;
		Entry.Type  = FName("Vector");
		Entry.Value = "(" + std::to_string(Value.X) + ", " + std::to_string(Value.Y) + ", " + std::to_string(Value.Z) + ")";
		OutEntries.push_back(std::move(Entry));
	}
}
