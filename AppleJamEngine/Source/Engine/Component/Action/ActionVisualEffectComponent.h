#pragma once

#include "Component/ActorComponent.h"
#include "Math/Vector.h"

#include "Source/Engine/Component/Action/ActionVisualEffectComponent.generated.h"

UCLASS()
class UActionVisualEffectComponent : public UActorComponent
{
public:
	GENERATED_BODY()
	UActionVisualEffectComponent() = default;
	~UActionVisualEffectComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

	UFUNCTION(Callable, Category="ActionVisualEffect|AfterImage")
	void StartAfterImage(const FVector& WorldDirection, float Duration, float Intensity = 0.85f, float Radius = 18.0f, int32 SampleCount = 10);

	UFUNCTION(Callable, Category="ActionVisualEffect|AfterImage")
	void StopAfterImage();

	UFUNCTION(Pure, Category="ActionVisualEffect|AfterImage")
	bool IsAfterImageActive() const;

	FVector GetAfterImageWorldDirection() const { return AfterImage.WorldDirection; }
	float GetAfterImageIntensity() const;
	float GetAfterImageRadius() const { return AfterImage.Radius; }
	int32 GetAfterImageSampleCount() const { return AfterImage.SampleCount; }

private:
	struct FAfterImageAction
	{
		bool bActive = false;
		float Duration = 0.0f;
		float RemainingTime = 0.0f;
		float Intensity = 0.85f;
		float Radius = 18.0f;
		int32 SampleCount = 10;
		FVector WorldDirection = FVector::ZeroVector;
	};

	FAfterImageAction AfterImage;
};
