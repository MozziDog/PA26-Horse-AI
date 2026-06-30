#include "LuaActorBindings.internal.h"

using namespace LuaActorBindingsDetail;

void FLuaScriptManager::RegisterActorBindings_3(sol::state& Lua)
{
    Lua.new_usertype<AParticleSystemActor>(
        "ParticleSystemActor",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "GetParticleSystemComponent",
        &AParticleSystemActor::GetParticleSystemComponent
    );

    sol::table Particle = Lua.create_named_table("Particle");
    Particle.set_function(
        "LoadSystem",
        [](const FString& Path) -> UParticleSystem*
        {
            return FParticleSystemManager::Get().Load(Path);
        }
    );
    Particle.set_function(
        "FindSystem",
        [](const FString& Path) -> UParticleSystem*
        {
            return FParticleSystemManager::Get().Find(Path);
        }
    );
    Particle.set_function(
        "SaveSystem",
        [](UParticleSystem* System)
        {
            return FParticleSystemManager::Get().Save(System);
        }
    );
    Particle.set_function(
        "NewSystem",
        []() -> UParticleSystem*
        {
            return UObjectManager::Get().CreateObject<UParticleSystem>();
        }
    );
    Particle.set_function(
        "NewEmitter",
        [](UObject* Outer) -> UParticleEmitter*
        {
            return UObjectManager::Get().CreateObject<UParticleEmitter>(Outer);
        }
    );
    Particle.set_function(
        "NewModule",
        [](const FString& ClassName, UObject* Outer) -> UParticleModule*
        {
            UObject* Obj = FObjectFactory::Get().Create(ClassName, Outer);
            return Cast<UParticleModule>(Obj);
        }
    );
    Particle.set_function(
        "AddEmitter",
        [](UParticleSystem* System) -> UParticleEmitter*
        {
            return IsValid(System) ? System->AddEmitter() : nullptr;
        }
    );
    Particle.set_function(
        "AddModule",
        [](UParticleLODLevel* LOD, const FString& ClassName) -> UParticleModule*
        {
            if (!IsValid(LOD)) return nullptr;
            UObject*         Obj    = FObjectFactory::Get().Create(ClassName, LOD);
            UParticleModule* Module = Cast<UParticleModule>(Obj);
            if (!IsValid(Module))
            {
                if (IsValid(Obj)) UObjectManager::Get().DestroyObject(Obj);
                return nullptr;
            }
            if (!LOD->AddModule(Module))
            {
                UObjectManager::Get().DestroyObject(Module);
                return nullptr;
            }
            return Module;
        }
    );
    Particle.set_function(
        "SetComponentTemplate",
        [](UParticleSystemComponent* Component, UParticleSystem* System)
        {
            if (IsValid(Component)) Component->SetTemplate(System);
        }
    );
    Particle.set_function(
        "SetComponentTemplateByPath",
        [](UParticleSystemComponent* Component, const FString& Path)
        {
            if (IsValid(Component)) Component->SetTemplate(FParticleSystemManager::Get().Load(Path));
        }
    );

    Lua.new_usertype<USceneComponent>(
        "SceneComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "Location",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetWorldLocation();
            },
            [](USceneComponent& Component, const FVector& Location)
            {
                Component.SetWorldLocation(Location);
            }
        ),
        "Rotation",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetRelativeRotation().ToVector();
            },
            [](USceneComponent& Component, const FVector& Rotation)
            {
                Component.SetRelativeRotation(Rotation);
            }
        ),
        "Forward",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetForwardVector();
            }
        ),
        "Right",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetRightVector();
            }
        ),
        "Up",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetUpVector();
            }
        ),
        "GetLocation",
        [](USceneComponent& Component)
        {
            return Component.GetWorldLocation();
        },
        "SetLocation",
        [](USceneComponent& Component, const FVector& Location)
        {
            Component.SetWorldLocation(Location);
        },
        "GetRotation",
        [](USceneComponent& Component)
        {
            return Component.GetRelativeRotation().ToVector();
        },
        "SetRotation",
        [](USceneComponent& Component, const FVector& Rotation)
        {
            Component.SetRelativeRotation(Rotation);
        },

        // 부모 기준 상대 위치 — 동일한 메시를 4개 깐 바퀴 같은 케이스에서 앞/뒤 구분 등
        // 위치 기반 필터링에 쓰인다. 월드 위치는 위 "Location" 프로퍼티 참고.
        "AttachToComponent",
        [](USceneComponent& Component, USceneComponent* Parent, sol::optional<FString> SocketName)
        {
            if (IsValid(Parent))
            {
                Component.AttachToComponent(Parent, SocketName ? FName(SocketName.value()) : FName::None);
            }
        },
        "GetParent",
        &USceneComponent::GetParent,
        "GetAttachSocketName",
        [](USceneComponent& Component)
        {
            return Component.GetAttachSocketName().ToString();
        },
        "HasSocket",
        [](USceneComponent& Component, const FString& SocketName)
        {
            return Component.HasSocket(FName(SocketName));
        },
        "GetSocketWorldLocation",
        [](USceneComponent& Component, const FString& SocketName)
        {
            return Component.GetSocketWorldLocation(FName(SocketName));
        },
        "GetSocketWorldRotation",
        [](USceneComponent& Component, const FString& SocketName)
        {
            return Component.GetSocketWorldRotation(FName(SocketName)).ToVector();
        },
        "GetSocketWorldScale",
        [](USceneComponent& Component, const FString& SocketName)
        {
            return Component.GetSocketWorldScale(FName(SocketName));
        },
        "GetSocketForwardVector",
        [](USceneComponent& Component, const FString& SocketName)
        {
            return Component.GetSocketForwardVector(FName(SocketName));
        },
        "RelativeLocation",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetRelativeLocation();
            },
            [](USceneComponent& Component, const FVector& V)
            {
                Component.SetRelativeLocation(V);
            }
        ),
        "RelativeScale",
        sol::property(
            [](USceneComponent& Component)
            {
                return Component.GetRelativeScale();
            },
            [](USceneComponent& Component, const FVector& V)
            {
                Component.SetRelativeScale(V);
            }
        )
    );

    Lua.new_usertype<UPrimitiveComponent>(
        "PrimitiveComponent",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>(),
        "IsValid",
        [](UPrimitiveComponent* Component)
        {
            return IsValid(Component);
        },
        "SetSimulatePhysics",
        [](UPrimitiveComponent* Component, bool bSimulate)
        {
            if (IsValid(Component)) Component->SetSimulatePhysics(bSimulate);
        },
        "GetSimulatePhysics",
        [](UPrimitiveComponent* Component) -> bool
        {
            return IsValid(Component) ? Component->GetSimulatePhysics() : false;
        },
        "AddForce",
        [](UPrimitiveComponent* Component, const FVector& Force)
        {
            if (IsValid(Component)) Component->AddForce(Force);
        },
        "AddForceAtLocation",
        [](UPrimitiveComponent* Component, const FVector& Force, const FVector& Location)
        {
            if (IsValid(Component)) Component->AddForceAtLocation(Force, Location);
        },
        "AddTorque",
        [](UPrimitiveComponent* Component, const FVector& Torque)
        {
            if (IsValid(Component)) Component->AddTorque(Torque);
        },
        "AddImpulse",
        [](UPrimitiveComponent* Component, const FVector& Impulse)
        {
            if (IsValid(Component)) Component->AddImpulse(Impulse);
        },
        "GetLinearVelocity",
        [](UPrimitiveComponent* Component) -> FVector
        {
            return IsValid(Component) ? Component->GetLinearVelocity() : FVector::ZeroVector;
        },
        "SetLinearVelocity",
        [](UPrimitiveComponent* Component, const FVector& Vel)
        {
            if (IsValid(Component)) Component->SetLinearVelocity(Vel);
        },
        "GetAngularVelocity",
        [](UPrimitiveComponent* Component) -> FVector
        {
            return IsValid(Component) ? Component->GetAngularVelocity() : FVector::ZeroVector;
        },
        "SetAngularVelocity",
        [](UPrimitiveComponent* Component, const FVector& Vel)
        {
            if (IsValid(Component)) Component->SetAngularVelocity(Vel);
        },
        "GetMass",
        [](UPrimitiveComponent* Component) -> float
        {
            return IsValid(Component) ? Component->GetMass() : 0.0f;
        },
        "SetMass",
        [](UPrimitiveComponent* Component, float Mass)
        {
            if (IsValid(Component)) Component->SetMass(Mass);
        },
        "GetGenerateOverlapEvents",
        [](UPrimitiveComponent* Component) -> bool
        {
            return IsValid(Component) ? Component->GetGenerateOverlapEvents() : false;
        }
    );

    Lua.new_usertype<UStaticMesh>(
        "StaticMesh",
        sol::base_classes,
        sol::bases<UObject>(),
        "AssetPath",
        sol::property(
            [](UStaticMesh& Mesh)
            {
                return Mesh.GetAssetPathFileName();
            }
        ),
        "GetAssetPath",
        [](UStaticMesh& Mesh)
        {
            return Mesh.GetAssetPathFileName();
        }
    );

    // 메시 에셋 경로로 컴포넌트 식별 가능하게 노출. 자동 생성된 FName ("UStaticMeshComponent_41")
    // 은 월드 초기화 순서에 따라 카운터가 달라져 빌드별로 매칭이 깨질 수 있다. 메시 경로는
    // 씬 파일에 명시 저장되므로 deterministic.
    Lua.new_usertype<UStaticMeshComponent>(
        "StaticMeshComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "MeshPath",
        sol::property(
            [](UStaticMeshComponent& C)
            {
                return C.GetStaticMeshPath();
            }
        ),
        "GetMeshPath",
        [](UStaticMeshComponent& C)
        {
            return C.GetStaticMeshPath();
        },
        "SetStaticMesh",
        &UStaticMeshComponent::SetStaticMesh,
        "SetStaticMeshByPath",
        &UStaticMeshComponent::SetStaticMeshByPath,
        "ClearStaticMesh",
        &UStaticMeshComponent::ClearStaticMesh,
        "GetStaticMesh",
        &UStaticMeshComponent::GetStaticMesh,
        "SetMaterialByPath",
        &UStaticMeshComponent::SetMaterialByPath,
        "SetMaterial",
        &UStaticMeshComponent::SetMaterial,
        "GetMaterial",
        &UStaticMeshComponent::GetMaterial,
        "GetMaterialPath",
        &UStaticMeshComponent::GetMaterialPath,
        "GetMaterialSlotCount",
        &UStaticMeshComponent::GetMaterialSlotCount
    );

    Lua.new_usertype<USkinnedMeshComponent>(
        "SkinnedMeshComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetSkeletalMeshByPath",
        &USkinnedMeshComponent::SetSkeletalMeshByPath,
        "ClearSkeletalMesh",
        &USkinnedMeshComponent::ClearSkeletalMesh,
        "GetSkeletalMesh",
        &USkinnedMeshComponent::GetSkeletalMesh,
        "GetSkeletalMeshPathValue",
        &USkinnedMeshComponent::GetSkeletalMeshPathValue,
        "SetMaterialByPath",
        &USkinnedMeshComponent::SetMaterialByPath,
        "SetMaterial",
        &USkinnedMeshComponent::SetMaterial,
        "GetMaterial",
        &USkinnedMeshComponent::GetMaterial,
        "GetMaterialPath",
        &USkinnedMeshComponent::GetMaterialPath,
        "GetMaterialSlotCount",
        &USkinnedMeshComponent::GetMaterialSlotCount
    );

    Lua.new_usertype<USkeletalMeshComponent>(
        "SkeletalMeshComponent",
        sol::base_classes,
        sol::bases<USkinnedMeshComponent, UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "PlayAnimationByPath",
        &USkeletalMeshComponent::PlayAnimationByPath,
        "StopAnimation",
        &USkeletalMeshComponent::StopAnimation,
        "SetAnimationByPath",
        &USkeletalMeshComponent::SetAnimationByPath,
        "SetPlayRate",
        &USkeletalMeshComponent::SetPlayRate,
        "SetLooping",
        &USkeletalMeshComponent::SetLooping,
        "SetPlaying",
        &USkeletalMeshComponent::SetPlaying,
        "GetAnimInstance",
        &USkeletalMeshComponent::GetAnimInstance,
        "GetAnimationMode",
        &USkeletalMeshComponent::GetAnimationMode,
        "GetAnimation",
        &USkeletalMeshComponent::GetAnimation
    );

    Lua.new_usertype<FHitResult>(
        "HitResult",
        "HitComponent",
        sol::property(
            [](const FHitResult& Hit) -> UPrimitiveComponent*
            {
                return IsValid(Hit.HitComponent) ? Hit.HitComponent : nullptr;
            }
        ),
        "HitActor",
        sol::property(
            [](const FHitResult& Hit) -> AActor*
            {
                return IsValid(Hit.HitActor) ? Hit.HitActor : nullptr;
            }
        ),
        "GetHitComponent",
        [](const FHitResult& Hit) -> UPrimitiveComponent*
        {
            return IsValid(Hit.HitComponent) ? Hit.HitComponent : nullptr;
        },
        "GetHitActor",
        [](const FHitResult& Hit) -> AActor*
        {
            return IsValid(Hit.HitActor) ? Hit.HitActor : nullptr;
        },
        "Distance",
        &FHitResult::Distance,
        "PenetrationDepth",
        &FHitResult::PenetrationDepth,
        "WorldHitLocation",
        &FHitResult::WorldHitLocation,
        "WorldNormal",
        &FHitResult::WorldNormal,
        "ImpactNormal",
        &FHitResult::ImpactNormal,
        "FaceIndex",
        &FHitResult::FaceIndex,
        "bHit",
        &FHitResult::bHit
    );

    Lua.new_usertype<UCameraComponent>(
        "CameraComponent",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>(),
        "LookAt",
        &UCameraComponent::LookAt,
        "SetFOV",
        &UCameraComponent::SetFOV,
        "GetFOV",
        &UCameraComponent::GetFOV,
        "SetAspectRatio",
        &UCameraComponent::SetAspectRatio,
        "GetAspectRatio",
        &UCameraComponent::GetAspectRatio,
        "SetNearPlane",
        &UCameraComponent::SetNearPlane,
        "GetNearPlane",
        &UCameraComponent::GetNearPlane,
        "SetFarPlane",
        &UCameraComponent::SetFarPlane,
        "GetFarPlane",
        &UCameraComponent::GetFarPlane,
        "SetOrthoWidth",
        &UCameraComponent::SetOrthoWidth,
        "GetOrthoWidth",
        &UCameraComponent::GetOrthoWidth,
        "SetOrthographic",
        &UCameraComponent::SetOrthographic,
        "IsOrthographic",
        &UCameraComponent::IsOrthogonal,
        "SetLetterbox",
        [](UCameraComponent& Camera, bool bEnabled, sol::optional<float> Amount, sol::optional<float> Thickness, sol::optional<sol::object> Color)
        {
            Camera.SetLetterboxEnabled(bEnabled);
            if (Amount) Camera.SetLetterboxAmount(Amount.value());
            if (Thickness) Camera.SetLetterboxThickness(Thickness.value());
            if (Color)
            {
                FVector4 ColorValue;
                if (LuaObjectToVector4(Color.value(), ColorValue))
                {
                    Camera.SetLetterboxColor(FLinearColor(ColorValue.X, ColorValue.Y, ColorValue.Z, ColorValue.W));
                }
            }
        },
        "ClearLetterbox",
        [](UCameraComponent& Camera)
        {
            Camera.SetLetterboxEnabled(false);
        },
        "OnResize",
        &UCameraComponent::OnResize
    );

}
