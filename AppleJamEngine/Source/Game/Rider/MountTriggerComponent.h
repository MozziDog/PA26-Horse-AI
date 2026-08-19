#pragma once

#include "Component/Shape/BoxComponent.h"
#include "Core/Delegate.h"

#include "Source/Game/Rider/MountTriggerComponent.generated.h"

class AHorseCharacter;
class AActor;
class UPrimitiveComponent;
struct FHitResult;

UENUM()
enum class EMountDirection : uint8
{
	Left,
	Right,
	Back,
};

// 말에게 non-root component로 부착하여 탑승 가능한 위치 표시 
// + 해당 위치에서 탑승했을 때의 MountDirection 정보 보관
UCLASS()
class UMountTriggerComponent : public UBoxComponent
{
public:
	GENERATED_BODY()
	UMountTriggerComponent() = default;
	~UMountTriggerComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;

	UFUNCTION(Pure, Category="Mount Trigger")
	AHorseCharacter* GetMountTarget() const;

	UFUNCTION(Pure, Category="Mount Trigger")
	EMountDirection GetMountDirection() const { return MountDirection; }

	UFUNCTION(Callable, Category="Mount Trigger")
	void SetMountDirection(EMountDirection InDirection) { MountDirection = InDirection; }

private:
	void HandleBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);
	void HandleEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	FDelegateHandle BeginOverlapHandle;
	FDelegateHandle EndOverlapHandle;

	UPROPERTY(Edit, Save, Category="Mount Trigger", DisplayName="Direction", Enum=EMountDirection)
	EMountDirection MountDirection = EMountDirection::Left;
};
