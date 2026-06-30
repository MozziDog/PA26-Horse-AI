#include "ActionVisualEffectComponent.h"

#include "Math/MathUtils.h"
#include "Profiling/Time/Timer.h"
#include "Runtime/Engine.h"

#include <algorithm>

void UActionVisualEffectComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
}

void UActionVisualEffectComponent::EndPlay()
{
	StopAfterImage();
	UActorComponent::EndPlay();
}

void UActionVisualEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!AfterImage.bActive)
	{
		return;
	}

	const float RawDeltaTime = GEngine && GEngine->GetTimer() ? GEngine->GetTimer()->GetRawDeltaTime() : DeltaTime;
	AfterImage.RemainingTime -= RawDeltaTime > 0.0f ? RawDeltaTime : DeltaTime;
	if (AfterImage.RemainingTime <= 0.0f)
	{
		StopAfterImage();
	}
}

void UActionVisualEffectComponent::StartAfterImage(const FVector& WorldDirection, float Duration, float Intensity, float Radius, int32 SampleCount)
{
	if (Duration <= 0.0f)
	{
		StopAfterImage();
		return;
	}

	FVector Direction = WorldDirection;
	if (Direction.Length() <= FMath::Epsilon)
	{
		StopAfterImage();
		return;
	}

	Direction.Normalize();

	AfterImage.bActive = true;
	AfterImage.Duration = Duration;
	AfterImage.RemainingTime = Duration;
	AfterImage.Intensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	AfterImage.Radius = FMath::Clamp(Radius, 0.0f, 64.0f);
	AfterImage.SampleCount = std::max(1, std::min(SampleCount, 16));
	AfterImage.WorldDirection = Direction;
}

void UActionVisualEffectComponent::StopAfterImage()
{
	AfterImage.bActive = false;
	AfterImage.Duration = 0.0f;
	AfterImage.RemainingTime = 0.0f;
}

bool UActionVisualEffectComponent::IsAfterImageActive() const
{
	return AfterImage.bActive && AfterImage.RemainingTime > 0.0f && AfterImage.Intensity > 0.0f && AfterImage.Radius > 0.0f;
}

float UActionVisualEffectComponent::GetAfterImageIntensity() const
{
	if (!IsAfterImageActive() || AfterImage.Duration <= FMath::Epsilon)
	{
		return 0.0f;
	}

	const float LifeAlpha = FMath::Clamp(AfterImage.RemainingTime / AfterImage.Duration, 0.0f, 1.0f);
	return AfterImage.Intensity * LifeAlpha;
}
