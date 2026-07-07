#include "pch.h"
#include "RoadSensorComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Debug/DrawDebugHelpers.h"

void URoadSensorComponent::BeginPlay()
{
	Super::BeginPlay();

	World = GetWorld();
	BlackboardComp = Owner->GetComponentByClass<UBlackboardComponent>();
	for (AActor* Actor : GetWorld()->GetActors())
	{
		URoadGraphComponent* Comp = Actor->GetComponentByClass<URoadGraphComponent>();
		if (Comp)
		{
			RoadGraphComp = Comp;
			break;
		}
	}
	if (!RoadGraphComp.IsValid())
	{
		UE_LOG("[URoadSensorComponent] Cannot find URoadGraphComponent");
	}
}

void URoadSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	if (!RoadGraphComp.IsValid())
	{
		return;
	}
	
	FRoadEdgeQueryResult Result = RoadGraphComp->GetRoadGraph().FindClosestPoint(GetWorldLocation());
	FVector RoadLocation = Result.Position;
	FVector RoadDirection = Result.Direction;

	// '방향'은 무조건 Actor pivot으로부터 멀어지는 방향으로
	FVector ComponentLocation = USceneComponent::GetRelativeLocation();
	if (RoadDirection.Dot(ComponentLocation) < 0)
	{
		RoadDirection *= -1;
	}

	// Update Blackboard values
	BlackboardComp->GetBlackboard().SetVector(BlackboardKey, RoadDirection);

	// Debug draw
	DrawDebugSphere(World, RoadLocation, 0.1f, 16, FColor::Red());
	DrawDebugLine(World, RoadLocation, RoadLocation + RoadDirection * 3.0f, FColor::Red());
}

void URoadSensorComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	Scene.AddDebugSphere(GetWorldLocation(), 0.3f, 16, FColor::Green());
}