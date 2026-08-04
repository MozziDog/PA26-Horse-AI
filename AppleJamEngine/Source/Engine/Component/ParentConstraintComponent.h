#pragma once

#include "Component/ActorComponent.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Object/Ptr/WeakObjectPtr.h"

#include "Source/Engine/Component/ParentConstraintComponent.generated.h"

class USceneComponent;

// 다른 액터의 컴포넌트의 Location + Rotation만 따라감. Scale은 shear 문제로 지원하지 않음. 
// 부착할 컴포넌트가 지정된 이름의 소켓을 가지고 있다면 컴포넌트 위치 대신 소켓 위치를 기준으로 사용
// 초기화/삭제 시의 문제를 회피하기 위해 컴포넌트 계층구조를 사용하지 않음. 
// 단순히 매 프레임 대상의 transform을 가져와서 actor transform을 업데이트하는 방식
// (부착 대상이 사라져도 같이 사라지거나 댕글링 참조 발생하지 않으니 안전)
UCLASS()
class UParentConstraintComponent : public UActorComponent
{
public:
    GENERATED_BODY()

    UParentConstraintComponent();
    ~UParentConstraintComponent() override = default;

    void EndPlay() override;
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

    // Keeps the constrained actor at its current world pose when bKeepWorldTransform is true.
    UFUNCTION(Callable, Category="Constraint|Parent")
    bool AttachTo(USceneComponent* InTargetComponent, const FName& InSocketName = FName::None, bool bKeepWorldTransform = true);

    UFUNCTION(Callable, Category="Constraint|Parent")
    void Detach();

    UFUNCTION(Pure, Category="Constraint|Parent")
    bool IsAttached() const;

    UFUNCTION(Pure, Category="Constraint|Parent")
    USceneComponent* GetTargetComponent() const { return TargetComponent.Get(); }

    UFUNCTION(Pure, Category="Constraint|Parent")
    FName GetSocketName() const { return SocketName; }

    FTransform GetRelativeOffset() const;
	void SetRelativeOffset(const FVector& InRelativeLocation, const FRotator& InRelativeRotation);

private:
    // NOTE: 컴포넌트를 프로퍼티로 노출을 지원하지 않아 현재로썬 코드를 통해서만 Attach 가능
    // Runtime-only weak reference: 초기화/삭제 시의 문제를 회피하기 위해 계층구조 사용 X, 대신 약한 참조만 사용
    TWeakObjectPtr<USceneComponent> TargetComponent;
    FName SocketName = FName::None;

    UPROPERTY(Edit, Save, Category="Constraint|Parent", DisplayName="Relative Location Offset", Type=Vec3, Speed=0.1f)
    FVector RelativeLocationOffset = FVector::ZeroVector;

    UPROPERTY(Edit, Save, Category="Constraint|Parent", DisplayName="Relative Rotation Offset", Type=Rotator, Speed=0.1f)
    FRotator RelativeRotationOffset = FRotator::ZeroRotator;

private:
    USceneComponent* GetConstrainedRoot() const;
    bool IsTargetInConstrainedHierarchy(const USceneComponent* InTargetComponent) const;
    FMatrix GetTargetWorldMatrix() const;
    void ApplyConstraint();
};
