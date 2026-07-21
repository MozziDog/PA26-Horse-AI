#include "pch.h"
#include "RoadSensorComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Debug/DrawDebugHelpers.h"

#include <cfloat>

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
	
	const FRoadEdgeQueryResult Result = RoadGraphComp->GetRoadGraph().FindClosestPoint(GetWorldLocation());

	FVector RoadDir  = FVector::ZeroVector;
	float   RoadDist = FLT_MAX;   // 도로망 밖이면 거리 무한대로 취급 → 소비자 측에서 가중치 0으로 계산되게
	if (Result.bValid)
	{
		// Carrot: 목표로 하는 도로 상의 지점.
		// RoadDir 벡터는 Owner(액터)를 pivot으로 하는 World space vector 
		FVector ToCarrot = Result.Position - Owner->GetActorLocation();
		ToCarrot.Z = 0.0f;   // 수평면 조향만
		if (!ToCarrot.IsNearlyZero())
		{
			RoadDir = ToCarrot.Normalized();
		}
		// 가중치 falloff 판정용 거리 - 구체적인 가중치 계산은 값을 소비하는 측에서.
		RoadDist = (Result.Position - GetWorldLocation()).Length();
	}

	BlackboardComp->GetBlackboard().SetVector(RoadDirBlackboardKey, RoadDir);
	if (DistBlackboardKey.IsValid())
	{
		BlackboardComp->GetBlackboard().SetFloat(DistBlackboardKey, RoadDist);
	}

	// Debug draw
	DrawDebugSphere(World, GetWorldLocation(), 0.3f, 16, FColor::Green());
	if (Result.bValid)
	{
		DrawDebugSphere(World, Result.Position, 0.1f, 16, FColor::Red());
		DrawDebugLine(World, Owner->GetActorLocation(), Result.Position, FColor::Red());
		DrawDebugLine(World, GetWorldLocation(), Result.Position, FColor::Green());
	}
}

void URoadSensorComponent::ContributeSelectedVisuals(FScene& Scene) const
{
	Scene.AddDebugSphere(GetWorldLocation(), 0.3f, 16, FColor::Green());
}