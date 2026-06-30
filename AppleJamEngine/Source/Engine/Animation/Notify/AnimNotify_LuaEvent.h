#pragma once

#include "Animation/Notify/AnimNotify.h"

#include "Source/Engine/Animation/Notify/AnimNotify_LuaEvent.generated.h"

UCLASS()
class UAnimNotify_LuaEvent : public UAnimNotify
{
public:
	GENERATED_BODY()
	UAnimNotify_LuaEvent() = default;
	~UAnimNotify_LuaEvent() override = default;
};
