#include "LuaActorBindings.internal.h"

using namespace LuaActorBindingsDetail;

void FLuaScriptManager::RegisterActorBindings_5(sol::state& Lua)
{

    Lua.new_usertype<APlayerController>(
        "PlayerController",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "Possess",
        &APlayerController::Possess,
        "UnPossess",
        &APlayerController::UnPossess,
        "GetPossessedPawn",
        &APlayerController::GetPossessedPawn,
        "GetPlayerCameraManager",
        &APlayerController::GetPlayerCameraManager,
        "SetViewTargetWithBlend",
        [](APlayerController& Self, AActor* Target, sol::optional<float> BlendTime)
        {
            if (IsValid(Target))
            {
                Self.SetViewTargetWithBlend(Target, BlendTime.value_or(0.0f));
            }
        }
    );

    Lua.new_usertype<APawn>(
        "Pawn",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "GetController",
        &APawn::GetController,
        "IsPossessed",
        &APawn::IsPossessed,
        "SetAutoPossessPlayer",
        &APawn::SetAutoPossessPlayer,
        "GetAutoPossessPlayer",
        &APawn::GetAutoPossessPlayer,
        "GetInputComponent",
        &APawn::GetInputComponent,
        "ResetHealth",
        &APawn::ResetHealth,
        "GetDamaged",
        &APawn::GetDamaged,
        "GetCurrentHealth",
        &APawn::GetCurrentHealth,
        "GetMaxHealth",
        &APawn::GetMaxHealth,
        "GetHealthRatio",
        &APawn::GetHealthRatio,
        "GetHealthHitCount",
        &APawn::GetHealthHitCount
    );

    Lua.new_usertype<AWheeledVehiclePawn>(
        "WheeledVehiclePawn",
        sol::base_classes,
        sol::bases<APawn, AActor, UObject>(),
        "GetMesh",
        &AWheeledVehiclePawn::GetMesh,
        "GetVehicleMovement",
        &AWheeledVehiclePawn::GetVehicleMovement,
        "GetWheelPoseComponent",
        &AWheeledVehiclePawn::GetWheelPoseComponent,
        "GetSpringArm",
        &AWheeledVehiclePawn::GetSpringArm,
        "GetCamera",
        &AWheeledVehiclePawn::GetCamera
    );

    // UInputComponent — Pawn::GetInputComponent 로 얻어 lua 에서 직접 매핑/binding 추가 가능.
    // 예 (BeginPlay 안):
    //   local input = obj:AsPawn():GetInputComponent()
    //   input:AddActionMapping("Jump", "Space")
    //   input:BindAction("Jump", "Pressed", function() print("jump!") end)
    Lua.new_usertype<UInputComponent>(
        "InputComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "AddAxisMapping",
        sol::overload(
            [](UInputComponent& Self, const FString& Name, const FString& KeyName, float Scale)
            {
                Self.AddAxisMapping(Name, ResolveInputKeyCode(KeyName), Scale);
            },
            [](UInputComponent& Self, const FString& Name, const FString& KeyName)
            {
                Self.AddAxisMapping(Name, ResolveInputKeyCode(KeyName), 1.0f);
            },
            [](UInputComponent& Self, const FString& Name, int32 KeyCode, float Scale)
            {
                Self.AddAxisMapping(Name, KeyCode, Scale);
            },
            [](UInputComponent& Self, const FString& Name, int32 KeyCode)
            {
                Self.AddAxisMapping(Name, KeyCode, 1.0f);
            }
        ),
        "AddMouseAxisMapping",
        sol::overload(
            [](UInputComponent& Self, const FString& Name, const FString& AxisName, float Scale)
            {
                EInputAxisSourceType Axis = EInputAxisSourceType::MouseX;
                if (AxisName == "MouseY") Axis = EInputAxisSourceType::MouseY;
                else if (AxisName == "MouseWheel") Axis = EInputAxisSourceType::MouseWheel;
                Self.AddMouseAxisMapping(Name, Axis, Scale);
            },
            [](UInputComponent& Self, const FString& Name, const FString& AxisName)
            {
                EInputAxisSourceType Axis = EInputAxisSourceType::MouseX;
                if (AxisName == "MouseY") Axis = EInputAxisSourceType::MouseY;
                else if (AxisName == "MouseWheel") Axis = EInputAxisSourceType::MouseWheel;
                Self.AddMouseAxisMapping(Name, Axis, 1.0f);
            }
        ),
        "AddActionMapping",
        sol::overload(
            [](UInputComponent& Self, const FString& Name, const FString& KeyName)
            {
                Self.AddActionMapping(Name, ResolveInputKeyCode(KeyName));
            },
            [](UInputComponent& Self, const FString& Name, int32 KeyCode)
            {
                Self.AddActionMapping(Name, KeyCode);
            }
        ),
        "BindAxis",
        [](UInputComponent& Self, const FString& Name, sol::protected_function Cb)
        {
            Self.BindAxis(
                Name,
                [Cb](float V)
                {
                    FScopedGarbageCollectionBlocker GCBlocker;
                    auto                            R = Cb(V);
                    if (!R.valid())
                    {
                        sol::error e = R;
                        UE_LOG("[Lua] BindAxis cb error: %s", e.what());
                    }
                }
            );
        },
        "BindAction",
        [](UInputComponent& Self, const FString& Name, const FString& EventStr, sol::protected_function Cb)
        {
            const EInputEvent Ev = (EventStr == "Released") ? EInputEvent::Released : EInputEvent::Pressed;
            Self.BindAction(
                Name,
                Ev,
                [Cb]()
                {
                    FScopedGarbageCollectionBlocker GCBlocker;
                    auto                            R = Cb();
                    if (!R.valid())
                    {
                        sol::error e = R;
                        UE_LOG("[Lua] BindAction cb error: %s", e.what());
                    }
                }
            );
        },
        "ClearBindings",
        &UInputComponent::ClearBindings
    );

    // --- World binding — 런타임 액터 spawn 용 (Engine 일반 기능) ---
}
