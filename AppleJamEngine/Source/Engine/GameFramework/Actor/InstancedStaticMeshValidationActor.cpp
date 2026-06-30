#include "GameFramework/Actor/InstancedStaticMeshValidationActor.h"

#include "Component/Debug/InstancedStaticMeshValidationComponent.h"

AInstancedStaticMeshValidationActor::AInstancedStaticMeshValidationActor()
{
	bTickInEditor = true;
}

void AInstancedStaticMeshValidationActor::InitDefaultComponents()
{
	ValidationComponent = AddComponent<UInstancedStaticMeshValidationComponent>();
	SetRootComponent(ValidationComponent.Get());

	if (UInstancedStaticMeshValidationComponent* Component = ValidationComponent.Get())
	{
		Component->RebuildValidationInstances();
	}
}

