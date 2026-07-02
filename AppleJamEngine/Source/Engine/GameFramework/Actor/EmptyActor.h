#pragma once

#include "GameFramework/AActor.h"

#include "Source/Engine/GameFramework/Actor/EmptyActor.generated.h"

UCLASS()
class AEmptyActor : public AActor
{
public:
	GENERATED_BODY()

	void InitDefaultComponent()
	{
		TWeakObjectPtr<USceneComponent> SceneComponent = AddComponent<USceneComponent>();
		SetRootComponent(SceneComponent);
	}
};