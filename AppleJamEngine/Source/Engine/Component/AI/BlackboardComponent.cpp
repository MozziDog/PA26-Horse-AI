#include "pch.h"
#include "BlackboardComponent.h"

void UBlackboardComponent::BeginPlay()
{
	Blackboard.Clear();
}

void UBlackboardComponent::PreGetEditableProperties()
{
	UActorComponent::PreGetEditableProperties();
	Blackboard.CollectDebugEntries(DebugEntries);
}
