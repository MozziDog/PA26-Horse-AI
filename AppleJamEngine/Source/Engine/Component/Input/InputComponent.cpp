#include "InputComponent.h"

#include "Core/Logging/Log.h"
#include "Input/InputSystem.h"
#include "Input/InputKeyCodes.h"
#include "Object/Reflection/ObjectFactory.h"

#include <algorithm>
#include <cmath>

UInputComponent::UInputComponent()
{
	bTickEnable = false;
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.SetTickEnabled(false);
}

void UInputComponent::AddAxisMapping(const FString& Name, int VKey, float Scale)
{
	AddAxisMappingForOwner(nullptr, Name, VKey, Scale);
}

void UInputComponent::AddAxisMapping(const FString& Name, const FString& KeyName, float Scale)
{
	AddAxisMappingForOwner(nullptr, Name, KeyName, Scale);
}

void UInputComponent::AddAxisMappingForOwner(const void* OwnerKey, const FString& Name, const FString& KeyName, float Scale)
{
	AddAxisMappingForOwner(OwnerKey, Name, ResolveInputKeyCode(KeyName), Scale);
}

void UInputComponent::AddAxisMappingForOwner(const void* OwnerKey, const FString& Name, int VKey, float Scale)
{
	FAxisMapping M;
	M.Name = Name;
	M.SourceType = EInputAxisSourceType::Key;
	M.VKey = VKey;
	M.Scale = Scale;
	M.OwnerKey = OwnerKey;
	AxisMappings.push_back(std::move(M));
}

void UInputComponent::AddMouseAxisMapping(const FString& Name, EInputAxisSourceType Axis, float Scale)
{
	AddMouseAxisMappingForOwner(nullptr, Name, Axis, Scale);
}

void UInputComponent::AddMouseAxisMappingForOwner(const void* OwnerKey, const FString& Name, EInputAxisSourceType Axis, float Scale)
{
	if (Axis == EInputAxisSourceType::Key)
	{
		return;
	}

	FAxisMapping M;
	M.Name = Name;
	M.SourceType = Axis;
	M.Scale = Scale;
	M.OwnerKey = OwnerKey;
	AxisMappings.push_back(std::move(M));
}

void UInputComponent::AddGamepadAxisMapping(const FString& Name, EInputAxisSourceType Axis, float Scale)
{
	AddGamepadAxisMappingForOwner(nullptr, Name, Axis, Scale);
}

void UInputComponent::AddGamepadAxisMappingForOwner(const void* OwnerKey, const FString& Name, EInputAxisSourceType Axis, float Scale)
{
	switch (Axis)
	{
	case EInputAxisSourceType::GamepadLeftStickX:
	case EInputAxisSourceType::GamepadLeftStickY:
	case EInputAxisSourceType::GamepadRightStickX:
	case EInputAxisSourceType::GamepadRightStickY:
	case EInputAxisSourceType::GamepadLeftTrigger:
	case EInputAxisSourceType::GamepadRightTrigger:
		break;
	default:
		return;
	}

	FAxisMapping M;
	M.Name = Name;
	M.SourceType = Axis;
	M.Scale = Scale;
	M.OwnerKey = OwnerKey;
	AxisMappings.push_back(std::move(M));
}

void UInputComponent::AddActionMapping(const FString& Name, int VKey)
{
	AddActionMappingForOwner(nullptr, Name, VKey);
}

void UInputComponent::AddActionMapping(const FString& Name, const FString& KeyName)
{
	AddActionMappingForOwner(nullptr, Name, KeyName);
}

void UInputComponent::AddActionMappingForOwner(const void* OwnerKey, const FString& Name, const FString& KeyName)
{
	AddActionMappingForOwner(OwnerKey, Name, ResolveInputKeyCode(KeyName));
}

void UInputComponent::AddActionMappingForOwner(const void* OwnerKey, const FString& Name, int VKey)
{
	FActionMapping M;
	M.Name = Name;
	M.VKey = VKey;
	M.OwnerKey = OwnerKey;
	ActionMappings.push_back(std::move(M));
}

void UInputComponent::BindAxis(const FString& Name, TFunction<void(float)> Callback)
{
	BindAxisForOwner(nullptr, Name, std::move(Callback));
}

void UInputComponent::BindAxisForOwner(const void* OwnerKey, const FString& Name, TFunction<void(float)> Callback)
{
	FAxisBinding B;
	B.Name = Name;
	B.OwnerKey = OwnerKey;
	B.Callback = std::move(Callback);
	AxisBindings.push_back(std::move(B));
}

void UInputComponent::BindAction(const FString& Name, EInputEvent Event, TFunction<void()> Callback)
{
	BindActionForOwner(nullptr, Name, Event, std::move(Callback));
}

void UInputComponent::BindActionForOwner(const void* OwnerKey, const FString& Name, EInputEvent Event, TFunction<void()> Callback)
{
	FActionBinding B;
	B.Name = Name;
	B.Event = Event;
	B.OwnerKey = OwnerKey;
	B.Callback = std::move(Callback);
	ActionBindings.push_back(std::move(B));
}

void UInputComponent::ClearBindings()
{
	AxisMappings.clear();
	ActionMappings.clear();
	AxisBindings.clear();
	ActionBindings.clear();
}

void UInputComponent::RemoveBindingsForOwner(const void* OwnerKey)
{
	if (!OwnerKey)
	{
		return;
	}

	AxisMappings.erase(
		std::remove_if(AxisMappings.begin(), AxisMappings.end(), [OwnerKey](const FAxisMapping& M) { return M.OwnerKey == OwnerKey; }),
		AxisMappings.end());
	ActionMappings.erase(
		std::remove_if(ActionMappings.begin(), ActionMappings.end(), [OwnerKey](const FActionMapping& M) { return M.OwnerKey == OwnerKey; }),
		ActionMappings.end());
	AxisBindings.erase(
		std::remove_if(AxisBindings.begin(), AxisBindings.end(), [OwnerKey](const FAxisBinding& B) { return B.OwnerKey == OwnerKey; }),
		AxisBindings.end());
	ActionBindings.erase(
		std::remove_if(ActionBindings.begin(), ActionBindings.end(), [OwnerKey](const FActionBinding& B) { return B.OwnerKey == OwnerKey; }),
		ActionBindings.end());
}

float UInputComponent::EvaluateAxisMapping(const FAxisMapping& Mapping, const FInputSystemSnapshot& Snapshot) const
{
	switch (Mapping.SourceType)
	{
	case EInputAxisSourceType::Key:
		return Snapshot.IsDown(Mapping.VKey) ? Mapping.Scale : 0.0f;
	case EInputAxisSourceType::MouseX:
		return static_cast<float>(Snapshot.MouseDeltaX) * Mapping.Scale;
	case EInputAxisSourceType::MouseY:
		return static_cast<float>(Snapshot.MouseDeltaY) * Mapping.Scale;
	case EInputAxisSourceType::MouseWheel:
		return static_cast<float>(Snapshot.ScrollDelta) * Mapping.Scale;
	case EInputAxisSourceType::GamepadLeftStickX:
		return ApplyAnalogDeadZone(Snapshot.GamepadLeftStickX, GamepadLeftStickDeadZone, true) * Mapping.Scale;
	case EInputAxisSourceType::GamepadLeftStickY:
		return ApplyAnalogDeadZone(Snapshot.GamepadLeftStickY, GamepadLeftStickDeadZone, true) * Mapping.Scale;
	case EInputAxisSourceType::GamepadRightStickX:
		return ApplyAnalogDeadZone(Snapshot.GamepadRightStickX, GamepadRightStickDeadZone, true) * Mapping.Scale;
	case EInputAxisSourceType::GamepadRightStickY:
		return ApplyAnalogDeadZone(Snapshot.GamepadRightStickY, GamepadRightStickDeadZone, true) * Mapping.Scale;
	case EInputAxisSourceType::GamepadLeftTrigger:
		return ApplyAnalogDeadZone(Snapshot.GamepadLeftTrigger, GamepadTriggerDeadZone, false) * Mapping.Scale;
	case EInputAxisSourceType::GamepadRightTrigger:
		return ApplyAnalogDeadZone(Snapshot.GamepadRightTrigger, GamepadTriggerDeadZone, false) * Mapping.Scale;
	default:
		return 0.0f;
	}
}

float UInputComponent::ApplyAnalogDeadZone(float Value, float DeadZone, bool bSignedAxis) const
{
	const float ClampedDeadZone = std::clamp(DeadZone, 0.0f, 0.99f);
	if (bSignedAxis)
	{
		const float AbsValue = std::abs(Value);
		if (AbsValue <= ClampedDeadZone)
		{
			return 0.0f;
		}

		const float Normalized = (AbsValue - ClampedDeadZone) / (1.0f - ClampedDeadZone);
		return std::copysign(std::clamp(Normalized, 0.0f, 1.0f), Value);
	}

	const float ClampedValue = std::clamp(Value, 0.0f, 1.0f);
	if (ClampedValue <= ClampedDeadZone)
	{
		return 0.0f;
	}
	return std::clamp((ClampedValue - ClampedDeadZone) / (1.0f - ClampedDeadZone), 0.0f, 1.0f);
}

void UInputComponent::ProcessInput(const FInputSystemSnapshot& Snapshot, float /*DeltaTime*/)
{
	// Axis: 매핑 평가 → name 별 합산 → 매칭 binding 호출.
	// UE 와 동일 — 매 frame 호출 (value=0 도 호출됨, 자식이 0 분기 처리).
	for (const FAxisBinding& B : AxisBindings)
	{
		float Value = 0.0f;
		for (const FAxisMapping& M : AxisMappings)
		{
			if (M.Name == B.Name)
			{
				Value += EvaluateAxisMapping(M, Snapshot);
			}
		}
		if (B.Callback) B.Callback(Value);
	}

	// Action: edge 감지 (Pressed = KeyDown, Released = KeyUp).
	for (const FActionBinding& B : ActionBindings)
	{
		for (const FActionMapping& M : ActionMappings)
		{
			if (M.Name != B.Name) continue;
			const bool bFired = (B.Event == EInputEvent::Pressed)
				? Snapshot.WasPressed(M.VKey)
				: Snapshot.WasReleased(M.VKey);
			if (bFired && B.Callback)
			{
				B.Callback();
				break;  // 같은 action 의 여러 매핑이 같은 frame 발화해도 1회만.
			}
		}
	}
}

void UInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 입력 처리는 PlayerController → Possessed Pawn → ProcessInput 경로에서만 수행한다.
}
