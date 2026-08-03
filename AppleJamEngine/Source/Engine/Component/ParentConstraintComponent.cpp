#include "Component/ParentConstraintComponent.h"

#include "Component/SceneComponent.h"
#include "GameFramework/AActor.h"

namespace
{
    FMatrix GetTransformWithoutScale(const FMatrix& Matrix)
    {
        const auto NormalizeAxis = [](const FVector& Axis, const FVector& Fallback)
        {
            const float Length = Axis.Length();
            return Length > 1.0e-6f ? Axis / Length : Fallback;
        };

        const FVector XAxis = NormalizeAxis(FVector(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]), FVector(1.0f, 0.0f, 0.0f));
        const FVector YAxis = NormalizeAxis(FVector(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]), FVector(0.0f, 1.0f, 0.0f));
        const FVector ZAxis = NormalizeAxis(FVector(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]), FVector(0.0f, 0.0f, 1.0f));

        FMatrix Result = FMatrix::Identity;
        Result.M[0][0] = XAxis.X; Result.M[0][1] = XAxis.Y; Result.M[0][2] = XAxis.Z;
        Result.M[1][0] = YAxis.X; Result.M[1][1] = YAxis.Y; Result.M[1][2] = YAxis.Z;
        Result.M[2][0] = ZAxis.X; Result.M[2][1] = ZAxis.Y; Result.M[2][2] = ZAxis.Z;
		// translate는 유지
        Result.M[3][0] = Matrix.M[3][0];
        Result.M[3][1] = Matrix.M[3][1];
        Result.M[3][2] = Matrix.M[3][2];
        return Result;
    }
}

UParentConstraintComponent::UParentConstraintComponent()
{
	// 애니메이션이 PrePhysics에서 적용되니 소켓 위치 받아오려면 타이밍이 그 이후여야 함
    PrimaryComponentTick.SetTickGroup(TG_PostUpdateWork);
    PrimaryComponentTick.SetEndTickGroup(TG_PostUpdateWork);
}

void UParentConstraintComponent::EndPlay()
{
    Detach();
    UActorComponent::EndPlay();
}

void UParentConstraintComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    UActorComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ApplyConstraint();
}

bool UParentConstraintComponent::AttachTo(USceneComponent* InTargetComponent, const FName& InSocketName, bool bKeepWorldTransform)
{
    if (!IsValid(InTargetComponent) || IsTargetInConstrainedHierarchy(InTargetComponent))
    {
        return false;
    }

    USceneComponent* ConstrainedRoot = GetConstrainedRoot();
    if (!ConstrainedRoot)
    {
        return false;
    }

    TargetComponent.Reset(InTargetComponent);
    SocketName = InSocketName;

    if (bKeepWorldTransform)
    {
        const FMatrix TargetWorldMatrix = GetTransformWithoutScale(GetTargetWorldMatrix());
        const FMatrix RootWorldMatrix = GetTransformWithoutScale(ConstrainedRoot->GetWorldMatrix());
        const FMatrix RelativeOffsetMatrix = RootWorldMatrix * TargetWorldMatrix.GetInverse();
        RelativeLocationOffset = RelativeOffsetMatrix.GetLocation();
        RelativeRotationOffset = RelativeOffsetMatrix.ToQuat().ToRotator();
    }
    else
    {
        RelativeLocationOffset = FVector::ZeroVector;
        RelativeRotationOffset = FRotator::ZeroRotator;
    }

    ApplyConstraint();
    return true;
}

void UParentConstraintComponent::Detach()
{
    TargetComponent.Reset();
    SocketName = FName::None;
}

bool UParentConstraintComponent::IsAttached() const
{
    return IsValid(TargetComponent.Get());
}

FTransform UParentConstraintComponent::GetRelativeOffset() const
{
    return FTransform(RelativeLocationOffset, RelativeRotationOffset, FVector(1.0f, 1.0f, 1.0f));
}

void UParentConstraintComponent::SetRelativeOffset(const FTransform& InRelativeOffset)
{
    RelativeLocationOffset = InRelativeOffset.Location;
    RelativeRotationOffset = InRelativeOffset.GetRotator();
    ApplyConstraint();
}

USceneComponent* UParentConstraintComponent::GetConstrainedRoot() const
{
    AActor* Owner = GetOwner();
    return Owner ? Owner->GetRootComponent() : nullptr;
}

bool UParentConstraintComponent::IsTargetInConstrainedHierarchy(const USceneComponent* InTargetComponent) const
{
    const USceneComponent* ConstrainedRoot = GetConstrainedRoot();
    return ConstrainedRoot && InTargetComponent && InTargetComponent->IsDescendantOf(ConstrainedRoot);
}

FMatrix UParentConstraintComponent::GetTargetWorldMatrix() const
{
    USceneComponent* Target = TargetComponent.Get();
    if (!Target)
    {
        return FMatrix::Identity;
    }

    if (SocketName.IsValid() && SocketName != FName::None && Target->HasSocket(SocketName))
    {
        return Target->GetSocketMatrix(SocketName);
    }

    return Target->GetWorldMatrix();
}

void UParentConstraintComponent::ApplyConstraint()
{
    USceneComponent* Target = TargetComponent.Get();
    USceneComponent* ConstrainedRoot = GetConstrainedRoot();
    if (!IsValid(Target) || !IsValid(ConstrainedRoot))
    {
        return;
    }

    // Scale 따로 처리하지 않으면 회전 행렬에 남아있어서 Scale 제거된 행렬을 재계산 필요
    const FMatrix TargetWorldMatrix = GetTransformWithoutScale(GetTargetWorldMatrix());
    const FMatrix DesiredWorldMatrix = GetRelativeOffset().ToMatrix() * TargetWorldMatrix;

    ConstrainedRoot->SetWorldLocation(DesiredWorldMatrix.GetLocation());
    ConstrainedRoot->SetWorldRotation(DesiredWorldMatrix.ToQuat());
}
