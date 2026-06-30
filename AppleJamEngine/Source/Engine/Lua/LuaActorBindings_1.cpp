#include "LuaActorBindings.internal.h"

using namespace LuaActorBindingsDetail;

void FLuaScriptManager::RegisterActorBindings_1(sol::state& Lua)
{
    Lua.new_usertype<UTexture2D>(
        "Texture2D",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetSourcePath",
        &UTexture2D::GetSourcePath,
        "GetWidth",
        &UTexture2D::GetWidth,
        "GetHeight",
        &UTexture2D::GetHeight,
        "IsLoaded",
        &UTexture2D::IsLoaded
    );

    Lua.new_usertype<UCameraShakeBase>(
        "CameraShakeBase",
        sol::base_classes,
        sol::bases<UObject>(),
        "StopShake",
        [](UCameraShakeBase& S, sol::optional<bool> bImmediately)
        {
            S.StopShake(bImmediately.value_or(true));
        },
        "IsFinished",
        &UCameraShakeBase::IsFinished,
        "GetPlaySpace",
        [](UCameraShakeBase& S)
        {
            return static_cast<int32>(S.GetPlaySpace());
        }
    );

    Lua.new_usertype<UCameraModifier>(
        "CameraModifier",
        sol::base_classes,
        sol::bases<UObject>(),
        "Enable",
        &UCameraModifier::EnableModifier,
        "Disable",
        [](UCameraModifier& M, sol::optional<bool> bImmediate)
        {
            M.DisableModifier(bImmediate.value_or(false));
        },
        "IsDisabled",
        &UCameraModifier::IsDisabled
    );

    Lua.new_usertype<UCameraShakeAsset>(
        "CameraShakeAsset",
        sol::base_classes,
        sol::bases<UObject>(),
        "LoadFromFile",
        &UCameraShakeAsset::LoadFromFile,
        "SaveToFile",
        &UCameraShakeAsset::SaveToFile,
        "SetSourcePath",
        &UCameraShakeAsset::SetSourcePath,
        "GetSourcePath",
        &UCameraShakeAsset::GetSourcePath
    );

    Lua.new_usertype<APlayerCameraManager>(
        "PlayerCameraManager",
        sol::base_classes,
        sol::bases<AActor, UObject>(),
        "RegisterCamera",
        &APlayerCameraManager::RegisterCamera,
        "UnregisterCamera",
        &APlayerCameraManager::UnregisterCamera,
        "AutoPossessDefaultCamera",
        &APlayerCameraManager::AutoPossessDefaultCamera,
        "ToggleActiveCameraForActor",
        sol::overload(
            [](APlayerCameraManager& M, const FString& ActorName, sol::optional<float> BlendTime)
            {
                return M.ToggleActiveCameraForActor(ActorName, BlendTime.value_or(0.0f));
            },
            [](APlayerCameraManager& M, const AActor* Actor, sol::optional<float> BlendTime)
            {
                return M.ToggleActiveCameraForActor(Actor, BlendTime.value_or(0.0f));
            }
        ),
        "GetActiveCamera",
        &APlayerCameraManager::GetActiveCamera,
        "SetActiveCamera",
        &APlayerCameraManager::SetActiveCamera,
        "SetActiveCameraWithBlend",
        [](APlayerCameraManager& M, UCameraComponent* NewCamera, sol::optional<float> BlendTime)
        {
            if (IsValid(NewCamera)) M.SetActiveCameraWithBlend(NewCamera, BlendTime.value_or(0.0f));
        },
        "GetPossessedCamera",
        &APlayerCameraManager::GetPossessedCamera,
        "Possess",
        &APlayerCameraManager::Possess,
        "SetViewTarget",
        [](APlayerCameraManager& M, AActor* Target)
        {
            if (IsValid(Target)) M.SetViewTarget(Target);
        },
        "GetViewTarget",
        &APlayerCameraManager::GetViewTarget,
        "GetPendingViewTarget",
        &APlayerCameraManager::GetPendingViewTarget,
        "StartCameraShakeAssetByPath",
        [](APlayerCameraManager& M, const FString& Path, sol::optional<float> Scale)
        {
            return M.StartCameraShakeAsset(Path, Scale.value_or(1.0f));
        },
        "StartCameraShakeAsset",
        [](APlayerCameraManager& M, UCameraShakeAsset* Asset, sol::optional<float> Scale)
        {
            return IsValid(Asset) ? M.StartCameraShakeAsset(Asset, Scale.value_or(1.0f)) : nullptr;
        },
        "StopAllCameraShakes",
        [](APlayerCameraManager& M, sol::optional<bool> bImmediately)
        {
            M.StopAllCameraShakes(bImmediately.value_or(true));
        },
        "StartCameraFade",
        [](APlayerCameraManager& M, float FromAlpha, float ToAlpha, float Duration, sol::optional<bool> bHold)
        {
            M.StartCameraFade(FromAlpha, ToAlpha, Duration, FLinearColor::Black(), false, bHold.value_or(false));
        },
        "StopCameraFade",
        &APlayerCameraManager::StopCameraFade,
        "SetCameraVignette",
        [](APlayerCameraManager& M, float Intensity, float Radius, float Softness)
        {
            M.SetCameraVignette(Intensity, Radius, Softness, FLinearColor::Black());
        },
        "ClearCameraVignette",
        &APlayerCameraManager::ClearCameraVignette,
        "IsFadeEnabled",
        &APlayerCameraManager::IsFadeEnabled,
        "GetFadeAmount",
        &APlayerCameraManager::GetFadeAmount,
        "IsVignetteEnabled",
        &APlayerCameraManager::IsVignetteEnabled,
        "GetVignetteIntensity",
        &APlayerCameraManager::GetVignetteIntensity,
        "GetVignetteRadius",
        &APlayerCameraManager::GetVignetteRadius,
        "GetVignetteSoftness",
        &APlayerCameraManager::GetVignetteSoftness,
        "SetDepthOfField",
        &APlayerCameraManager::SetDepthOfField,
        "SetBokeh",
        &APlayerCameraManager::SetBokeh,
        "ClearDepthOfField",
        &APlayerCameraManager::ClearDepthOfField,
        "IsDepthOfFieldEnabled",
        &APlayerCameraManager::IsDepthOfFieldEnabled,
        "GetDoFFocusDistance",
        &APlayerCameraManager::GetDoFFocusDistance,
        "GetDoFFocusRange",
        &APlayerCameraManager::GetDoFFocusRange,
        "GetDoFMaxBlurRadius",
        &APlayerCameraManager::GetDoFMaxBlurRadius,
        "GetDoFBokehRadiusThreshold",
        &APlayerCameraManager::GetDoFBokehRadiusThreshold,
        "GetDoFBokehLumaThreshold",
        &APlayerCameraManager::GetDoFBokehLumaThreshold,
        "GetDoFBokehIntensity",
        &APlayerCameraManager::GetDoFBokehIntensity
    );

    // Broad engine/gameplay bindings. The generic Reflection/CallFunction path can call
    // UFUNCTIONs, but concrete usertypes make LuaBlueprint scripting discoverable and
    // usable without hand-writing reflection strings for every common gameplay task.
    Lua.new_usertype<UMovementComponent>(
        "MovementComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "SetUpdatedComponent",
        &UMovementComponent::SetUpdatedComponent,
        "GetUpdatedComponent",
        &UMovementComponent::GetUpdatedComponent,
        "HasValidUpdatedComponent",
        &UMovementComponent::HasValidUpdatedComponent,
        "GetUpdatedComponentDisplayName",
        &UMovementComponent::GetUpdatedComponentDisplayName,
        "ResolveUpdatedComponent",
        &UMovementComponent::ResolveUpdatedComponent
    );

    Lua.new_usertype<UCharacterMovementComponent>(
        "CharacterMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>(),
        "AddInputVector",
        sol::overload(
            [](UCharacterMovementComponent& C, const FVector& Direction, float Scale)
            {
                C.AddInputVector(Direction, Scale);
            },
            [](UCharacterMovementComponent& C, const FVector& Direction)
            {
                C.AddInputVector(Direction, 1.0f);
            }
        ),
        "GetVelocity",
        &UCharacterMovementComponent::GetVelocityValue,
        "GetVelocityValue",
        &UCharacterMovementComponent::GetVelocityValue,
        "GetSpeed",
        &UCharacterMovementComponent::GetSpeed,
        "GetMovementMode",
        [](UCharacterMovementComponent& C)
        {
            return static_cast<int32>(C.GetMovementMode());
        },
        "SetMovementMode",
        [](UCharacterMovementComponent& C, int32 Mode)
        {
            C.SetMovementMode(static_cast<EMovementMode>(Mode));
        },
        "IsWalking",
        &UCharacterMovementComponent::IsWalking,
        "IsFalling",
        &UCharacterMovementComponent::IsFalling,
        "Jump",
        &UCharacterMovementComponent::Jump,
        "SetGravity",
        [](UCharacterMovementComponent& C, float InGravity)
        {
            C.Gravity = InGravity;
        },
        "GetGravity",
        [](UCharacterMovementComponent& C)
        {
            return C.Gravity;
        },
        "StopMovementImmediately",
        &UCharacterMovementComponent::StopMovementImmediately,
        "SetMovementInputBlocked",
        &UCharacterMovementComponent::SetMovementInputBlocked,
        "IsMovementInputBlocked",
        &UCharacterMovementComponent::IsMovementInputBlocked,
        "StartDash",
        &UCharacterMovementComponent::StartDash,
        "StopDash",
        &UCharacterMovementComponent::StopDash,
        "IsDashing",
        &UCharacterMovementComponent::IsDashing,
        "HasPendingRootMotion",
        &UCharacterMovementComponent::HasPendingRootMotion,
        "HasYawDrivenByRootMotion",
        &UCharacterMovementComponent::HasYawDrivenByRootMotion
    );

    Lua.new_usertype<UProjectileMovementComponent>(
        "ProjectileMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>(),
        "SetVelocity",
        &UProjectileMovementComponent::SetVelocity,
        "GetVelocity",
        &UProjectileMovementComponent::GetVelocity,
        "SetInitialSpeed",
        &UProjectileMovementComponent::SetInitialSpeed,
        "GetInitialSpeed",
        &UProjectileMovementComponent::GetInitialSpeed,
        "GetMaxSpeed",
        &UProjectileMovementComponent::GetMaxSpeed,
        "GetPreviewVelocity",
        &UProjectileMovementComponent::GetPreviewVelocity,
        "StopSimulating",
        &UProjectileMovementComponent::StopSimulating
    );

    Lua.new_usertype<URotatingMovementComponent>(
        "RotatingMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>(),
        "SetRotationRate",
        [](URotatingMovementComponent& C, const FVector& Rate)
        {
            C.SetRotationRate(FRotator(Rate));
        },
        "GetRotationRate",
        [](URotatingMovementComponent& C)
        {
            return C.GetRotationRate().ToVector();
        },
        "SetRotationInLocalSpace",
        &URotatingMovementComponent::SetRotationInLocalSpace,
        "IsRotationInLocalSpace",
        &URotatingMovementComponent::IsRotationInLocalSpace,
        "SetPivotTranslation",
        &URotatingMovementComponent::SetPivotTranslation,
        "GetPivotTranslation",
        &URotatingMovementComponent::GetPivotTranslation
    );

    Lua.new_usertype<UPendulumMovementComponent>(
        "PendulumMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>()
    );

    Lua.new_usertype<UShapeComponent>(
        "ShapeComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "IsDrawOnlyIfSelected",
        &UShapeComponent::IsDrawOnlyIfSelected,
        "GetShapeColor",
        [](UShapeComponent& C, sol::this_state State)
        {
            return LuaVector4ToTable(State, C.GetShapeColorVec4());
        }
    );

    Lua.new_usertype<UBoxComponent>(
        "BoxComponent",
        sol::base_classes,
        sol::bases<UShapeComponent, UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetBoxExtent",
        &UBoxComponent::SetBoxExtent,
        "GetScaledBoxExtent",
        &UBoxComponent::GetScaledBoxExtent,
        "GetUnscaledBoxExtent",
        &UBoxComponent::GetUnscaledBoxExtent
    );

    Lua.new_usertype<USphereComponent>(
        "SphereComponent",
        sol::base_classes,
        sol::bases<UShapeComponent, UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetSphereRadius",
        &USphereComponent::SetSphereRadius,
        "GetScaledSphereRadius",
        &USphereComponent::GetScaledSphereRadius,
        "GetUnscaledSphereRadius",
        &USphereComponent::GetUnscaledSphereRadius
    );

    Lua.new_usertype<UCapsuleComponent>(
        "CapsuleComponent",
        sol::base_classes,
        sol::bases<UShapeComponent, UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetCapsuleSize",
        &UCapsuleComponent::SetCapsuleSize,
        "GetScaledCapsuleRadius",
        &UCapsuleComponent::GetScaledCapsuleRadius,
        "GetScaledCapsuleHalfHeight",
        &UCapsuleComponent::GetScaledCapsuleHalfHeight,
        "GetUnscaledCapsuleRadius",
        &UCapsuleComponent::GetUnscaledCapsuleRadius,
        "GetUnscaledCapsuleHalfHeight",
        &UCapsuleComponent::GetUnscaledCapsuleHalfHeight
    );

    Lua.new_usertype<ULightComponentBase>(
        "LightComponentBase",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>(),
        "GetIntensity",
        &ULightComponentBase::GetIntensity,
        "SetIntensity",
        &ULightComponentBase::SetIntensity,
        "GetLightColor",
        [](ULightComponentBase& C, sol::this_state State)
        {
            return LuaVector4ToTable(State, C.GetLightColor());
        },
        "SetLightColor",
        [](ULightComponentBase& C, float R, float G, float B, sol::optional<float> A)
        {
            C.SetLightColor(FVector4(R, G, B, A.value_or(1.0f)));
        },
        "IsVisible",
        &ULightComponentBase::IsVisible,
        "CastShadows",
        &ULightComponentBase::CastShadows,
        "GetLightType",
        [](ULightComponentBase& C)
        {
            return static_cast<int32>(C.GetLightType());
        },
        "PushToScene",
        &ULightComponentBase::PushToScene,
        "DestroyFromScene",
        &ULightComponentBase::DestroyFromScene
    );

    Lua.new_usertype<ULightComponent>(
        "LightComponent",
        sol::base_classes,
        sol::bases<ULightComponentBase, USceneComponent, UActorComponent, UObject>(),
        "GetShadowResolutionScale",
        &ULightComponent::GetShadowResolutionScale,
        "GetShadowBias",
        &ULightComponent::GetShadowBias,
        "SetShadowBias",
        &ULightComponent::SetShadowBias,
        "GetShadowSlopeBias",
        &ULightComponent::GetShadowSlopeBias,
        "SetShadowSlopeBias",
        &ULightComponent::SetShadowSlopeBias,
        "GetShadowNormalBias",
        &ULightComponent::GetShadowNormalBias,
        "SetShadowNormalBias",
        &ULightComponent::SetShadowNormalBias,
        "GetShadowSharpen",
        &ULightComponent::GetShadowSharpen,
        "SetShadowSharpen",
        &ULightComponent::SetShadowSharpen
    );

    Lua.new_usertype<UAmbientLightComponent>("AmbientLightComponent", sol::base_classes, sol::bases<ULightComponent, ULightComponentBase, USceneComponent, UActorComponent, UObject>());
    Lua.new_usertype<UDirectionalLightComponent>("DirectionalLightComponent", sol::base_classes, sol::bases<ULightComponent, ULightComponentBase, USceneComponent, UActorComponent, UObject>());
    Lua.new_usertype<UPointLightComponent>(
        "PointLightComponent",
        sol::base_classes,
        sol::bases<ULightComponent, ULightComponentBase, USceneComponent, UActorComponent, UObject>(),
        "GetAttenuationRadius",
        &UPointLightComponent::GetAttenuationRadius,
        "SetAttenuationRadius",
        &UPointLightComponent::SetAttenuationRadius
    );
    Lua.new_usertype<USpotLightComponent>(
        "SpotLightComponent",
        sol::base_classes,
        sol::bases<UPointLightComponent, ULightComponent, ULightComponentBase, USceneComponent, UActorComponent, UObject>(),
        "GetOuterConeAngle",
        &USpotLightComponent::GetOuterConeAngle
    );

    Lua.new_usertype<UTextRenderComponent>(
        "TextRenderComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetText",
        &UTextRenderComponent::SetText,
        "GetText",
        &UTextRenderComponent::GetText,
        "SetFont",
        [](UTextRenderComponent& C, const FString& FontName)
        {
            C.SetFont(FName(FontName));
        },
        "GetFontName",
        [](UTextRenderComponent& C)
        {
            return C.GetFontName().ToString();
        },
        "SetColor",
        [](UTextRenderComponent& C, float R, float G, float B, sol::optional<float> A)
        {
            C.SetColor(FVector4(R, G, B, A.value_or(1.0f)));
        },
        "GetColor",
        [](UTextRenderComponent& C, sol::this_state State)
        {
            return LuaVector4ToTable(State, C.GetColor());
        },
        "SetFontSize",
        &UTextRenderComponent::SetFontSize,
        "GetFontSize",
        &UTextRenderComponent::GetFontSize,
        "SetRenderSpace",
        [](UTextRenderComponent& C, int32 Space)
        {
            C.SetRenderSpace(static_cast<ETextRenderSpace>(Space));
        },
        "GetRenderSpace",
        [](UTextRenderComponent& C)
        {
            return static_cast<int32>(C.GetRenderSpace());
        },
        "SetScreenPosition",
        &UTextRenderComponent::SetScreenPosition,
        "GetScreenX",
        &UTextRenderComponent::GetScreenX,
        "GetScreenY",
        &UTextRenderComponent::GetScreenY,
        "SetHorizontalAlignment",
        [](UTextRenderComponent& C, int32 Align)
        {
            C.SetHorizontalAlignment(static_cast<ETextHAlign>(Align));
        },
        "GetHorizontalAlignment",
        [](UTextRenderComponent& C)
        {
            return static_cast<int32>(C.GetHorizontalAlignment());
        },
        "SetVerticalAlignment",
        [](UTextRenderComponent& C, int32 Align)
        {
            C.SetVerticalAlignment(static_cast<ETextVAlign>(Align));
        },
        "GetVerticalAlignment",
        [](UTextRenderComponent& C)
        {
            return static_cast<int32>(C.GetVerticalAlignment());
        }
    );

    Lua.new_usertype<UBillboardComponent>(
        "BillboardComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetBillboardEnabled",
        &UBillboardComponent::SetBillboardEnabled
    );

    Lua.new_usertype<USpringArmComponent>(
        "SpringArmComponent",
        sol::base_classes,
        sol::bases<USceneComponent, UActorComponent, UObject>(),
        "SetTargetArmLength",
        [](USpringArmComponent& C, float InTargetArmLength)
        {
            C.TargetArmLength = InTargetArmLength;
        },
        "GetTargetArmLength",
        [](USpringArmComponent& C)
        {
            return C.TargetArmLength;
        },
        "SetSocketOffset",
        [](USpringArmComponent& C, const FVector& InSocketOffset)
        {
            C.SocketOffset = InSocketOffset;
        },
        "GetSocketOffset",
        [](USpringArmComponent& C)
        {
            return C.SocketOffset;
        },
        "SetUsePawnControlRotation",
        [](USpringArmComponent& C, bool bUse)
        {
            C.bUsePawnControlRotation = bUse;
        },
        "GetUsePawnControlRotation",
        [](USpringArmComponent& C)
        {
            return C.bUsePawnControlRotation;
        },
        "SetInheritPitch",
        [](USpringArmComponent& C, bool bInherit)
        {
            C.bInheritPitch = bInherit;
        },
        "GetInheritPitch",
        [](USpringArmComponent& C)
        {
            return C.bInheritPitch;
        },
        "SetInheritYaw",
        [](USpringArmComponent& C, bool bInherit)
        {
            C.bInheritYaw = bInherit;
        },
        "GetInheritYaw",
        [](USpringArmComponent& C)
        {
            return C.bInheritYaw;
        },
        "SetInheritRoll",
        [](USpringArmComponent& C, bool bInherit)
        {
            C.bInheritRoll = bInherit;
        },
        "GetInheritRoll",
        [](USpringArmComponent& C)
        {
            return C.bInheritRoll;
        },
        "ResetLagState",
        [](USpringArmComponent& C)
        {
            C.ResetLagState();
        },
        "RefreshSpringArm",
        [](USpringArmComponent& C, sol::optional<float> DeltaTime)
        {
            C.RefreshSpringArm(DeltaTime.value_or(0.0f));
        }
    );
    Lua.new_usertype<UCineCameraComponent>(
        "CineCameraComponent",
        sol::base_classes,
        sol::bases<UCameraComponent, USceneComponent, UActorComponent, UObject>(),
        "SetLetterboxEnabled",
        &UCineCameraComponent::SetLetterboxEnabled,
        "SetLetterboxAmount",
        &UCineCameraComponent::SetLetterboxAmount,
        "SetLetterboxThickness",
        &UCineCameraComponent::SetLetterboxThickness,
        "SetLetterboxColor",
        [](UCineCameraComponent& C, float R, float G, float B, sol::optional<float> A)
        {
            C.SetLetterboxColor(FLinearColor(R, G, B, A.value_or(1.0f)));
        }
    );

}
