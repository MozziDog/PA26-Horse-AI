#include "LuaActorBindings.internal.h"

using namespace LuaActorBindingsDetail;

void FLuaScriptManager::RegisterActorBindings_4(sol::state& Lua)
{
    Lua.new_usertype<AActor>(
        "Actor",
        sol::base_classes,
        sol::bases<UObject>(),
        "Location",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorLocation();
            },
            [](AActor& Actor, const FVector& Location)
            {
                Actor.SetActorLocation(Location);
            }
        ),
        "Rotation",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorRotation().ToVector();
            },
            [](AActor& Actor, const FVector& Rotation)
            {
                Actor.SetActorRotation(Rotation);
            }
        ),

        "Scale",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorScale();
            },
            [](AActor& Actor, const FVector& Scale)
            {
                Actor.SetActorScale(Scale);
            }
        ),

        "Forward",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorForward();
            }
        ),

        "FaceYawToControlRotation",
        [](AActor& Actor) -> bool
        {
            APawn* Pawn = Cast<APawn>(&Actor);
            if (!Pawn)
            {
                return false;
            }

            FRotator Rotation = Actor.GetActorRotation();
            Rotation.Yaw = Pawn->GetControlRotation().Yaw;
            Actor.SetActorRotation(Rotation);
            return true;
        },

        "GetControlRotation",
        [](AActor& Actor)
        {
            APawn* Pawn = Cast<APawn>(&Actor);
            return Pawn ? Pawn->GetControlRotation().ToVector() : FVector(0.0f, 0.0f, 0.0f);
        },
        "SetControlRotation",
        [](AActor& Actor, const FVector& Rotation)
        {
            APawn* Pawn = Cast<APawn>(&Actor);
            if (!Pawn)
            {
                return false;
            }

            Pawn->SetControlRotation(FRotator(Rotation));
            return true;
        },

        "ResetHealth",
        [](AActor& Actor)
        {
            if (APawn* Pawn = Cast<APawn>(&Actor))
            {
                Pawn->ResetHealth();
            }
        },

        "GetDamaged",
        [](AActor& Actor, float DamageAmount)
        {
            APawn* Pawn = Cast<APawn>(&Actor);
            return Pawn ? Pawn->GetDamaged(DamageAmount) : 0.0f;
        },

        "GetCurrentHealth",
        [](AActor& Actor)
        {
            APawn* Pawn = Cast<APawn>(&Actor);
            return Pawn ? Pawn->GetCurrentHealth() : 0.0f;
        },

        "GetMaxHealth",
        [](AActor& Actor)
        {
            APawn* Pawn = Cast<APawn>(&Actor);
            return Pawn ? Pawn->GetMaxHealth() : 0.0f;
        },

        "GetHealthRatio",
        [](AActor& Actor)
        {
            APawn* Pawn = Cast<APawn>(&Actor);
            return Pawn ? Pawn->GetHealthRatio() : 0.0f;
        },

        "GetHealthHitCount",
        [](AActor& Actor)
        {
            APawn* Pawn = Cast<APawn>(&Actor);
            return Pawn ? Pawn->GetHealthHitCount() : 0;
        },

        "Right",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetActorRight();
            }
        ),

        "AddWorldOffset",
        [](AActor& Actor, const FVector& Offset)
        {
            Actor.AddActorWorldOffset(Offset);
        },

        "Destroy",
        [](AActor& Actor)
        {
            // World->DestroyActor가 EndPlay + 정리. Lua는 호출 후 해당 액터를 더 참조하지 말 것.
            if (UWorld* W = Actor.GetWorld()) W->DestroyActor(&Actor);
        },

        "IsValid",
        [](AActor* Actor)
        {
            // Lua가 보유한 actor 핸들이 cpp 측에서 destroy됐는지 확인. nil/destroyed면 false.
            return IsValid(Actor);
        },

        "HasTag",
        [](AActor& Actor, const FString& Tag)
        {
            return Actor.HasTag(FName(Tag));
        },
        "AddTag",
        [](AActor& Actor, const FString& Tag)
        {
            Actor.AddTag(FName(Tag));
        },
        "RemoveTag",
        [](AActor& Actor, const FString& Tag)
        {
            Actor.RemoveTag(FName(Tag));
        },
        "GetTags",
        [](AActor& Actor) -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            int        Index  = 1;
            for (const FName& Tag : Actor.GetTags())
            {
                Result[Index++] = Tag.ToString();
            }
            return Result;
        },
        "SetTags",
        [](AActor& Actor, sol::table Tags)
        {
            TArray<FName> Names;
            for (auto& Entry : Tags)
            {
                sol::object Value = Entry.second;
                if (Value.is<std::string>()) Names.push_back(FName(FString(Value.as<std::string>())));
            }
            Actor.SetTags(Names);
        },
        "GetComponents",
        [](AActor& Actor) -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            int        Index  = 1;
            for (UActorComponent* Component : Actor.GetComponents())
            {
                if (IsValid(Component)) Result[Index++] = Component;
            }
            return Result;
        },

        "GetFloatingPawnMovement",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UFloatingPawnMovementComponent>();
        },

        "GetCharacterMovementComponent",
        [](AActor& Actor) -> UCharacterMovementComponent*
        {
            return Actor.GetComponentByClass<UCharacterMovementComponent>();
        },

        "StartCharacterDash",
        [](AActor& Actor, const FVector& WorldDirection, float Distance, float Duration) -> bool
        {
            UCharacterMovementComponent* Movement = Actor.GetComponentByClass<UCharacterMovementComponent>();
            return Movement ? Movement->StartDash(WorldDirection, Distance, Duration) : false;
        },

        "StopCharacterMovementImmediately",
        [](AActor& Actor)
        {
            if (UCharacterMovementComponent* Movement = Actor.GetComponentByClass<UCharacterMovementComponent>())
            {
                Movement->StopMovementImmediately();
            }
        },

        "SetCharacterMovementInputBlocked",
        [](AActor& Actor, bool bBlocked)
        {
            if (UCharacterMovementComponent* Movement = Actor.GetComponentByClass<UCharacterMovementComponent>())
            {
                Movement->SetMovementInputBlocked(bBlocked);
            }
        },

        "IsCharacterMovementInputBlocked",
        [](AActor& Actor) -> bool
        {
            UCharacterMovementComponent* Movement = Actor.GetComponentByClass<UCharacterMovementComponent>();
            return Movement ? Movement->IsMovementInputBlocked() : false;
        },

        "IsCharacterDashing",
        [](AActor& Actor) -> bool
        {
            UCharacterMovementComponent* Movement = Actor.GetComponentByClass<UCharacterMovementComponent>();
            return Movement ? Movement->IsDashing() : false;
        },

        "GetVehicleMovement",
        [](AActor& Actor) -> UWheeledVehicleMovementComponent*
        {
            if (UWheeledVehicleMovementComponent* Movement = Actor.GetComponentByClass<UWheeledVehicleMovementComponent>())
            {
                return Movement;
            }
            return nullptr;
        },

        "GetStaticMeshComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UStaticMeshComponent>();
        },

        "GetCamera",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UCameraComponent>();
        },

        "GetCameraComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UCameraComponent>();
        },

        "GetSpringArmComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<USpringArmComponent>();
        },

        "GetSkeletalMeshComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<USkeletalMeshComponent>();
        },

        "GetSkinnedMeshComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<USkinnedMeshComponent>();
        },

        "GetLuaBlueprintComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<ULuaBlueprintComponent>();
        },

        "GetLuaScriptComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<ULuaScriptComponent>();
        },

        "GetParticleSystemComponent",
        [](AActor& Actor) -> UParticleSystemComponent*
        {
            if (AParticleSystemActor* ParticleActor = Cast<AParticleSystemActor>(&Actor))
            {
                return ParticleActor->GetParticleSystemComponent();
            }
            return Actor.GetComponentByClass<UParticleSystemComponent>();
        },

        "GetActionComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UActionComponent>();
        },

        "GetOrCreateActionComponent",
        [](AActor& Actor) -> UActionComponent*
        {
            if (UActionComponent* Action = Actor.GetComponentByClass<UActionComponent>())
            {
                return Action;
            }
            return Actor.AddComponent<UActionComponent>();
        },

        "Slomo",
        [](AActor& Actor, float Duration, float TimeDilation) -> bool
        {
            UActionComponent* Action = Actor.GetComponentByClass<UActionComponent>();
            if (!Action)
            {
                Action = Actor.AddComponent<UActionComponent>();
            }

            if (!Action)
            {
                return false;
            }

            Action->Slomo(Duration, TimeDilation);
            return true;
        },

        "GetActionVisualEffectComponent",
        [](AActor& Actor)
        {
            return Actor.GetComponentByClass<UActionVisualEffectComponent>();
        },

        "StartAfterImage",
        [](AActor& Actor, const FVector& WorldDirection, float Duration, sol::optional<float> Intensity,
            sol::optional<float> Radius, sol::optional<int32> SampleCount) -> bool
        {
            UActionVisualEffectComponent* VisualEffect = Actor.GetComponentByClass<UActionVisualEffectComponent>();
            if (!VisualEffect)
            {
                VisualEffect = Actor.AddComponent<UActionVisualEffectComponent>();
            }

            if (!VisualEffect)
            {
                return false;
            }

            VisualEffect->StartAfterImage(
                WorldDirection,
                Duration,
                Intensity.value_or(0.85f),
                Radius.value_or(18.0f),
                SampleCount.value_or(10)
            );
            return VisualEffect->IsAfterImageActive();
        },

        "StopAfterImage",
        [](AActor& Actor)
        {
            if (UActionVisualEffectComponent* VisualEffect = Actor.GetComponentByClass<UActionVisualEffectComponent>())
            {
                VisualEffect->StopAfterImage();
            }
        },

        "GetRootComponent",
        [](AActor& Actor) -> USceneComponent*
        {
            return Actor.GetRootComponent();
        },

        "GetRootPrimitiveComponent",
        [](AActor& Actor) -> UPrimitiveComponent*
        {
            return Cast<UPrimitiveComponent>(Actor.GetRootComponent());
        },

        "AddForceToRoot",
        [](AActor& Actor, const FVector& Force)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->AddForce(Force);
        },
        "AddTorqueToRoot",
        [](AActor& Actor, const FVector& Torque)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->AddTorque(Torque);
        },
        "AddImpulseToRoot",
        [](AActor& Actor, const FVector& Impulse)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->AddImpulse(Impulse);
        },
        "GetRootLinearVelocity",
        [](AActor& Actor) -> FVector
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            return IsValid(Root) ? Root->GetLinearVelocity() : FVector::ZeroVector;
        },
        "SetRootLinearVelocity",
        [](AActor& Actor, const FVector& Velocity)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->SetLinearVelocity(Velocity);
        },
        "SetRootSimulatePhysics",
        [](AActor& Actor, bool bSimulate)
        {
            UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
            if (IsValid(Root)) Root->SetSimulatePhysics(bSimulate);
        },

        "GetPrimitiveComponent",
        [](AActor& Actor) -> UPrimitiveComponent*
        {
            return Actor.GetComponentByClass<UPrimitiveComponent>();
        },

        "GetPrimitiveComponentByName",
        [](AActor& Actor, const FString& ComponentName) -> UPrimitiveComponent*
        {
            for (UActorComponent* Component : Actor.GetComponents())
            {
                UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component);
                if (PrimitiveComponent && PrimitiveComponent->GetFName().ToString() == ComponentName)
                {
                    return PrimitiveComponent;
                }
            }
            return nullptr;
        },

        "GetComponentByName",
        [](AActor& Actor, const FString& ComponentName) -> USceneComponent*
        {
            for (UActorComponent* Component : Actor.GetComponents())
            {
                USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
                if (SceneComponent && SceneComponent->GetFName().ToString() == ComponentName)
                {
                    return SceneComponent;
                }
            }
            return nullptr;
        },

        "UUID",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetUUID();
            }
        ),

        "Name",
        sol::property(
            [](AActor& Actor)
            {
                return Actor.GetFName().ToString();
            }
        )
    );
}
