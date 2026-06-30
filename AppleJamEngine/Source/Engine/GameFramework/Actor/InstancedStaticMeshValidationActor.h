#pragma once

#include "GameFramework/AActor.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/GameFramework/Actor/InstancedStaticMeshValidationActor.generated.h"

class UInstancedStaticMeshValidationComponent;

// Debug-only editor placement actor for UInstancedStaticMeshComponent regression/stress validation.
// This is not a production gameplay actor; BulletHell owns gameplay projectile state separately.
UCLASS()
class AInstancedStaticMeshValidationActor : public AActor
{
public:
	GENERATED_BODY()
	AInstancedStaticMeshValidationActor();
	~AInstancedStaticMeshValidationActor() override = default;

	void InitDefaultComponents();

	UFUNCTION(Pure, Category="Actor|Components")
	UInstancedStaticMeshValidationComponent* GetValidationComponent() const { return ValidationComponent.Get(); }

private:
	TWeakObjectPtr<UInstancedStaticMeshValidationComponent> ValidationComponent = nullptr;
};

