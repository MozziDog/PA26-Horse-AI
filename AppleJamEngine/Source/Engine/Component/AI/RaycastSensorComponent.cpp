#include "RaycastSensorComponent.h"
#include "GameFramework/World.h"
#include "Debug/DrawDebugHelpers.h"

void URaycastSensorComponent::BeginPlay()
{
	Super::BeginPlay();
	World = GetWorld();
	BlackboardComp = Owner->GetComponentByClass<UBlackboardComponent>();
}

void URaycastSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	FVector RotatedRayDir = USceneComponent::GetWorldRotation().ToQuaternion().RotateVector(RayDir);

	// Update Blackboard values;
	FVector RayStart = GetWorldLocation();
	FHitResult RayHit;
	World->GetPhysicsScene()->Raycast(
		RayStart, RotatedRayDir, RotatedRayDir.Length(), RayHit, ECollisionChannel::WorldStatic
	);

	bool bRayHit = RayHit.bHit;
	BlackboardComp->GetBlackboard().SetBool(BlackboardKey, bRayHit);

	// Debug draw
	FVector LineStartPosition = GetWorldLocation();
	FVector LineEndPosition = bRayHit ? RayHit.WorldHitLocation : LineStartPosition + RotatedRayDir;
	FColor LineColor = bRayHit ? FColor::Red() : FColor::Green();
	DrawDebugLine(World, LineStartPosition, LineEndPosition, LineColor);
	if (bRayHit)
	{
		DrawDebugSphere(World, LineEndPosition, 0.3f, 16, FColor::Red());
	}
}
