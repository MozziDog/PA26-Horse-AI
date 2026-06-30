#include "LuaActorBindings.internal.h"

using namespace LuaActorBindingsDetail;

void FLuaScriptManager::RegisterActorBindings_2(sol::state& Lua)
{
    Lua.new_usertype<UMaterial>(
        "Material",
        sol::base_classes,
        sol::bases<UObject>(),
        "SetScalarParameter",
        &UMaterial::SetScalarParameter,
        "SetVector3Parameter",
        &UMaterial::SetVector3Parameter,
        "SetVector4Parameter",
        &UMaterial::SetVector4Parameter,
        "SetTextureParameter",
        &UMaterial::SetTextureParameter,
        "GetScalarParameterValue",
        &UMaterial::GetScalarParameterValue,
        "GetVector3ParameterValue",
        &UMaterial::GetVector3ParameterValue,
        "IsMaterialInstance",
        &UMaterial::IsMaterialInstance,
        "IsDynamicMaterialInstance",
        &UMaterial::IsDynamicMaterialInstance,
        "IsGraphMaterial",
        &UMaterial::IsGraphMaterial,
        "EnableGraphMaterial",
        &UMaterial::EnableGraphMaterial,
        "DisableGraphMaterial",
        &UMaterial::DisableGraphMaterial,
        "GetAssetPathFileName",
        &UMaterial::GetAssetPathFileName,
        "SetAssetPathFileName",
        &UMaterial::SetAssetPathFileName
    );

    {
        sol::object MaterialLibraryObject = Lua["MaterialLibrary"];
        sol::object MaterialTypeObject = Lua["Material"];
        if (MaterialLibraryObject.is<sol::table>() && MaterialTypeObject.is<sol::table>())
        {
            sol::table MaterialLibrary = MaterialLibraryObject.as<sol::table>();
            sol::table MaterialType = MaterialTypeObject.as<sol::table>();
            const char* LibraryFunctions[] = {
                    "Load",
                    "GetOrCreate",
                    "Create",
                    "CreateGraph",
                    "GetComponentMaterial",
                    "SetComponentMaterial",
                    "SetComponentMaterialByPath",
                    "CreateDynamicInstance",
                    "CreateDynamicInstanceForComponent",
                    "Save",
                    "SetShader",
                    "SetScalarParameter",
                    "SetVectorParameter",
                    "SetColorParameter",
                    "SetTextureParameter"
            };
            for (const char* FunctionName : LibraryFunctions)
            {
                sol::object Function = MaterialLibrary[FunctionName];
                MaterialType[FunctionName] = Function;
            }
        }
    }

    Lua.new_usertype<UMaterialInstanceDynamic>(
        "MaterialInstanceDynamic",
        sol::base_classes,
        sol::bases<UMaterial, UObject>(),
        "SetScalarParameterValue",
        &UMaterialInstanceDynamic::SetScalarParameterValue,
        "SetVector3ParameterValue",
        &UMaterialInstanceDynamic::SetVector3ParameterValue,
        "SetVectorParameterValue",
        &UMaterialInstanceDynamic::SetVectorParameterValue,
        "SetTextureParameterValue",
        &UMaterialInstanceDynamic::SetTextureParameterValue,
        "GetOwnerObject",
        &UMaterialInstanceDynamic::GetOwnerObject
    );

    Lua.new_usertype<UAnimSequence>(
        "AnimSequence",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetNumberOfFrames",
        &UAnimSequence::GetNumberOfFrames,
        "TimeToFrame",
        &UAnimSequence::TimeToFrame,
        "FrameToTime",
        &UAnimSequence::FrameToTime,
        "GetAssetPathFileName",
        &UAnimSequence::GetAssetPathFileName,
        "SetAssetPathFileName",
        &UAnimSequence::SetAssetPathFileName,
        "GetForceRootLock",
        &UAnimSequence::GetForceRootLock,
        "SetForceRootLock",
        &UAnimSequence::SetForceRootLock,
        "GetEnableRootMotion",
        &UAnimSequence::GetEnableRootMotion,
        "SetEnableRootMotion",
        &UAnimSequence::SetEnableRootMotion,
        "GetRootMotionBoneName",
        &UAnimSequence::GetRootMotionBoneName,
        "SetRootMotionBoneName",
        &UAnimSequence::SetRootMotionBoneName
    );

    Lua.new_usertype<UAnimMontage>(
        "AnimMontage",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetSourceSequence",
        &UAnimMontage::GetSourceSequence,
        "SetSourceSequence",
        &UAnimMontage::SetSourceSequence,
        "GetBlendInTime",
        &UAnimMontage::GetBlendInTime,
        "SetBlendInTime",
        &UAnimMontage::SetBlendInTime,
        "GetBlendOutTime",
        &UAnimMontage::GetBlendOutTime,
        "SetBlendOutTime",
        &UAnimMontage::SetBlendOutTime,
        "GetAssetPathFileName",
        &UAnimMontage::GetAssetPathFileName,
        "SetAssetPathFileName",
        &UAnimMontage::SetAssetPathFileName,
        "GetSourceSequencePath",
        &UAnimMontage::GetSourceSequencePath,
        "EnsureDefaultSection",
        &UAnimMontage::EnsureDefaultSection
    );

    sol::table Animation = Lua.create_named_table("Animation");
    Animation.set_function(
        "LoadMontage",
        [](const FString& Path) -> UAnimMontage*
        {
            return Path.empty() ? nullptr : FAnimationManager::Get().LoadMontage(Path);
        }
    );

    Lua.new_usertype<USkeletalMesh>(
        "SkeletalMesh",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetAssetPathFileName",
        &USkeletalMesh::GetAssetPathFileName,
        "SetAssetPathFileName",
        &USkeletalMesh::SetAssetPathFileName,
        "GetPhysicsAssetPath",
        &USkeletalMesh::GetPhysicsAssetPath
    );

    Lua.new_usertype<UAnimInstance>(
        "AnimInstance",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetOwningComponent",
        &UAnimInstance::GetOwningComponent,
        "GetSkeletalMesh",
        &UAnimInstance::GetSkeletalMesh,
        "TryGetPawnOwner",
        &UAnimInstance::TryGetPawnOwner,
        "GetRootMotionMode",
        [](UAnimInstance& I)
        {
            return static_cast<int32>(I.GetRootMotionMode());
        },
        "SetRootMotionMode",
        [](UAnimInstance& I, int32 Mode)
        {
            I.SetRootMotionMode(static_cast<ERootMotionMode>(Mode));
        },
        "PlayMontage",
        [](UAnimInstance& I, UAnimMontage* M, sol::optional<FString> Section, sol::optional<float> Rate, sol::optional<float> BlendIn, sol::optional<FString> Slot)
        {
            if (!IsValid(M))
            {
                return false;
            }
            I.PlayMontage(
                M,
                Section ? FName(Section.value()) : FName::None,
                Rate.value_or(1.0f),
                BlendIn.value_or(-1.0f),
                Slot ? FName(Slot.value()) : FName::None
            );
            return true;
        },
        "StopMontage",
        [](UAnimInstance& I, sol::optional<float> BlendOut, sol::optional<FString> Slot)
        {
            I.StopMontage(BlendOut.value_or(-1.0f), Slot ? FName(Slot.value()) : FName::None);
        },
        "Montage_JumpToSection",
        [](UAnimInstance& I, const FString& Section, sol::optional<FString> Slot)
        {
            I.Montage_JumpToSection(FName(Section), Slot ? FName(Slot.value()) : FName::None);
        },
        "Montage_SetNextSection",
        [](UAnimInstance& I, const FString& From, const FString& To, sol::optional<FString> Slot)
        {
            I.Montage_SetNextSection(FName(From), FName(To), Slot ? FName(Slot.value()) : FName::None);
        },
        "IsMontagePlaying",
        [](UAnimInstance& I, sol::optional<UAnimMontage*> M, sol::optional<FString> Slot)
        {
            return I.IsMontagePlaying(M.value_or(nullptr), Slot ? FName(Slot.value()) : FName::None);
        },
        "IsAnimGraphInstance",
        [](UAnimInstance& I)
        {
            return Cast<UAnimGraphInstance>(&I) != nullptr;
        },
        "SetGraphVariableFloat",
        [](UAnimInstance& I, const FString& VariableName, float Value)
        {
            if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->SetGraphVariableFloat(FName(VariableName), Value);
            }
            return false;
        },
        "SetGraphVariableBool",
        [](UAnimInstance& I, const FString& VariableName, bool bValue)
        {
            if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->SetGraphVariableBool(FName(VariableName), bValue);
            }
            return false;
        },
        "SetGraphVariableInt",
        [](UAnimInstance& I, const FString& VariableName, int32 Value)
        {
            if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->SetGraphVariableInt(FName(VariableName), Value);
            }
            return false;
        },
        "HasGraphVariableFloat",
        [](UAnimInstance& I, const FString& VariableName)
        {
            float Value = 0.0f;
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->GetGraphVariableFloat(FName(VariableName), Value);
            }
            return false;
        },
        "HasGraphVariableBool",
        [](UAnimInstance& I, const FString& VariableName)
        {
            bool bValue = false;
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->GetGraphVariableBool(FName(VariableName), bValue);
            }
            return false;
        },
        "HasGraphVariableInt",
        [](UAnimInstance& I, const FString& VariableName)
        {
            int32 Value = 0;
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                return Graph->GetGraphVariableInt(FName(VariableName), Value);
            }
            return false;
        },
        "GetGraphVariableFloat",
        [](UAnimInstance& I, const FString& VariableName, sol::optional<float> DefaultValue)
        {
            float Value = DefaultValue.value_or(0.0f);
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                float RuntimeValue = Value;
                if (Graph->GetGraphVariableFloat(FName(VariableName), RuntimeValue))
                {
                    return RuntimeValue;
                }
            }
            return Value;
        },
        "GetGraphVariableBool",
        [](UAnimInstance& I, const FString& VariableName, sol::optional<bool> DefaultValue)
        {
            bool bValue = DefaultValue.value_or(false);
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                bool bRuntimeValue = bValue;
                if (Graph->GetGraphVariableBool(FName(VariableName), bRuntimeValue))
                {
                    return bRuntimeValue;
                }
            }
            return bValue;
        },
        "GetGraphVariableInt",
        [](UAnimInstance& I, const FString& VariableName, sol::optional<int32> DefaultValue)
        {
            int32 Value = DefaultValue.value_or(0);
            if (const UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(&I))
            {
                int32 RuntimeValue = Value;
                if (Graph->GetGraphVariableInt(FName(VariableName), RuntimeValue))
                {
                    return RuntimeValue;
                }
            }
            return Value;
        }
    );

    Lua.new_usertype<ACharacter>(
        "Character",
        sol::base_classes,
        sol::bases<APawn, AActor, UObject>(),
        "AddMovementInput",
        sol::overload(
            [](ACharacter& C, const FVector& Direction, float Scale)
            {
                C.AddMovementInput(Direction, Scale);
            },
            [](ACharacter& C, const FVector& Direction)
            {
                C.AddMovementInput(Direction, 1.0f);
            }
        ),
        "Jump",
        &ACharacter::Jump,
        "GetCapsuleComponent",
        &ACharacter::GetCapsuleComponent,
        "GetMesh",
        &ACharacter::GetMesh,
        "GetCharacterMovement",
        &ACharacter::GetCharacterMovement,
        "ResetHealth",
        &ACharacter::ResetHealth,
        "GetDamaged",
        &ACharacter::GetDamaged,
        "GetCurrentHealth",
        &ACharacter::GetCurrentHealth,
        "GetMaxHealth",
        &ACharacter::GetMaxHealth,
        "GetHealthRatio",
        &ACharacter::GetHealthRatio,
        "GetHealthHitCount",
        &ACharacter::GetHealthHitCount
    );

    Lua.new_usertype<UActionComponent>(
        "ActionComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "HitStop",
        &UActionComponent::HitStop,
        "HitSquash",
        &UActionComponent::HitSquash,
        "Knockback",
        &UActionComponent::Knockback,
        "Slomo",
        &UActionComponent::Slomo,
        "StopHitStop",
        &UActionComponent::StopHitStop,
        "StopHitSquash",
        &UActionComponent::StopHitSquash,
        "StopKnockback",
        &UActionComponent::StopKnockback,
        "StopSlomo",
        &UActionComponent::StopSlomo,
        "StopAllActions",
        &UActionComponent::StopAllActions
    );

    Lua.new_usertype<UActionVisualEffectComponent>(
        "ActionVisualEffectComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "StartAfterImage",
        &UActionVisualEffectComponent::StartAfterImage,
        "StopAfterImage",
        &UActionVisualEffectComponent::StopAfterImage,
        "IsAfterImageActive",
        &UActionVisualEffectComponent::IsAfterImageActive
    );

    Lua.new_usertype<UFloatingPawnMovementComponent>(
        "FloatingPawnMovementComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "SetMoveInput",
        &UFloatingPawnMovementComponent::SetMoveInput,
        "SetLookInput",
        &UFloatingPawnMovementComponent::SetLookInput
    );

    Lua.new_usertype<UWheeledVehicleMovementComponent>(
        "WheeledVehicleMovementComponent",
        sol::base_classes,
        sol::bases<UMovementComponent, UActorComponent, UObject>(),
        "SetThrottleInput",
        &UWheeledVehicleMovementComponent::SetThrottleInput,
        "SetBrakeInput",
        &UWheeledVehicleMovementComponent::SetBrakeInput,
        "SetSteeringInput",
        &UWheeledVehicleMovementComponent::SetSteeringInput,
        "SetHandbrakeInput",
        &UWheeledVehicleMovementComponent::SetHandbrakeInput,
        "ResetVehicle",
        &UWheeledVehicleMovementComponent::ResetVehicle,
        "GetForwardSpeed",
        &UWheeledVehicleMovementComponent::GetForwardSpeed,
        "IsVehicleCreated",
        &UWheeledVehicleMovementComponent::IsVehicleCreated
    );

    Lua.new_usertype<UVehicleWheelPoseComponent>(
        "VehicleWheelPoseComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "SetVehicleMovement",
        &UVehicleWheelPoseComponent::SetVehicleMovement,
        "GetVehicleMovement",
        &UVehicleWheelPoseComponent::GetVehicleMovement
    );

    Lua.new_usertype<UParticleModule>(
        "ParticleModule",
        sol::base_classes,
        sol::bases<UObject>(),
        "IsEnabled",
        &UParticleModule::IsEnabled,
        "SetEnabled",
        &UParticleModule::SetEnabled,
        "GetDisplayName",
        &UParticleModule::GetDisplayName,
        "GetCategory",
        [](UParticleModule& Module)
        {
            return static_cast<int32>(Module.GetCategory());
        },
        "IsUnique",
        &UParticleModule::IsUnique
    );

    Lua.new_usertype<UParticleLODLevel>(
        "ParticleLODLevel",
        sol::base_classes,
        sol::bases<UObject>(),
        "Level",
        sol::property(
            [](UParticleLODLevel& LOD)
            {
                return LOD.Level;
            },
            [](UParticleLODLevel& LOD, int32 Level)
            {
                LOD.Level = Level;
            }
        ),
        "Enabled",
        sol::property(
            [](UParticleLODLevel& LOD)
            {
                return LOD.bEnabled;
            },
            [](UParticleLODLevel& LOD, bool bEnabled)
            {
                LOD.bEnabled = bEnabled;
            }
        ),
        "GetRequiredModule",
        [](UParticleLODLevel& LOD) -> UParticleModuleRequired*
        {
            return LOD.RequiredModule;
        },
        "GetSpawnModule",
        [](UParticleLODLevel& LOD) -> UParticleModuleSpawn*
        {
            return LOD.SpawnModule;
        },
        "GetTypeDataModule",
        [](UParticleLODLevel& LOD) -> UParticleModule*
        {
            return LOD.TypeDataModule;
        },
        "GetModuleCount",
        [](UParticleLODLevel& LOD)
        {
            return static_cast<int32>(LOD.Modules.size());
        },
        "GetModule",
        [](UParticleLODLevel& LOD, int32 Index) -> UParticleModule*
        {
            return (Index >= 0 && Index < static_cast<int32>(LOD.Modules.size())) ? LOD.Modules[Index] : nullptr;
        },
        "GetModules",
        [](UParticleLODLevel& LOD, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            int32           Idx    = 1;
            for (UParticleModule* Module : LOD.Modules) if (IsValid(Module)) Result[Idx++] = Module;
            return Result;
        },
        "AddModule",
        &UParticleLODLevel::AddModule,
        "RemoveModule",
        &UParticleLODLevel::RemoveModule,
        "AddModuleByClass",
        [](UParticleLODLevel& LOD, const FString& ClassName) -> UParticleModule*
        {
            UObject*         Obj    = FObjectFactory::Get().Create(ClassName, &LOD);
            UParticleModule* Module = Cast<UParticleModule>(Obj);
            if (!IsValid(Module))
            {
                if (IsValid(Obj)) UObjectManager::Get().DestroyObject(Obj);
                return nullptr;
            }
            if (!LOD.AddModule(Module))
            {
                UObjectManager::Get().DestroyObject(Module);
                return nullptr;
            }
            return Module;
        },
        "ValidateModules",
        &UParticleLODLevel::ValidateModules,
        "UpdateFromLOD0",
        &UParticleLODLevel::UpdateFromLOD0
    );

    Lua.new_usertype<UParticleEmitter>(
        "ParticleEmitter",
        sol::base_classes,
        sol::bases<UObject>(),
        "Name",
        sol::property(
            [](UParticleEmitter& Emitter)
            {
                return Emitter.EmitterName;
            },
            [](UParticleEmitter& Emitter, const FString& Name)
            {
                Emitter.EmitterName = Name;
            }
        ),
        "Enabled",
        sol::property(&UParticleEmitter::IsEnabled, &UParticleEmitter::SetEnabled),
        "IsEnabled",
        &UParticleEmitter::IsEnabled,
        "SetEnabled",
        &UParticleEmitter::SetEnabled,
        "GetQualityLevelSpawnRateMult",
        &UParticleEmitter::GetQualityLevelSpawnRateMult,
        "SetQualityLevelSpawnRateMult",
        &UParticleEmitter::SetQualityLevelSpawnRateMult,
        "InitializeDefaultLODLevel",
        &UParticleEmitter::InitializeDefaultLODLevel,
        "EnsureLODCoreModules",
        &UParticleEmitter::EnsureLODCoreModules,
        "CreateLODLevel",
        &UParticleEmitter::CreateLODLevel,
        "RemoveLODLevel",
        &UParticleEmitter::RemoveLODLevel,
        "GetLODLevel",
        &UParticleEmitter::GetLODLevel,
        "GetCurrentLODLevel",
        &UParticleEmitter::GetCurrentLODLevel,
        "GetLODCount",
        &UParticleEmitter::GetLODCount,
        "CacheEmitterModuleInfo",
        &UParticleEmitter::CacheEmitterModuleInfo,
        "GetParticleSize",
        &UParticleEmitter::GetParticleSize,
        "GetReqInstanceBytes",
        &UParticleEmitter::GetReqInstanceBytes
    );

    Lua.new_usertype<UParticleSystem>(
        "ParticleSystem",
        sol::base_classes,
        sol::bases<UObject>(),
        "Looping",
        sol::property(
            [](UParticleSystem& System)
            {
                return System.bLooping;
            },
            [](UParticleSystem& System, bool bLooping)
            {
                System.bLooping = bLooping;
            }
        ),
        "UpdateTimeFPS",
        sol::property(
            [](UParticleSystem& System)
            {
                return System.UpdateTimeFPS;
            },
            [](UParticleSystem& System, float FPS)
            {
                System.UpdateTimeFPS = FPS;
            }
        ),
        "AddEmitter",
        &UParticleSystem::AddEmitter,
        "RemoveEmitter",
        &UParticleSystem::RemoveEmitter,
        "MoveEmitter",
        &UParticleSystem::MoveEmitter,
        "GetEmitterCount",
        &UParticleSystem::GetEmitterCount,
        "GetEmitter",
        &UParticleSystem::GetEmitter,
        "GetMaxLODCount",
        &UParticleSystem::GetMaxLODCount,
        "EnsureLODDistances",
        &UParticleSystem::EnsureLODDistances,
        "GetLODIndexForDistance",
        &UParticleSystem::GetLODIndexForDistance,
        "GetLODDistance",
        &UParticleSystem::GetLODDistance,
        "SetLODDistance",
        &UParticleSystem::SetLODDistance,
        "BuildEmitters",
        &UParticleSystem::BuildEmitters,
        "GetSourcePath",
        &UParticleSystem::GetSourcePath,
        "SetSourcePath",
        &UParticleSystem::SetSourcePath
    );

    Lua.new_usertype<UParticleSystemComponent>(
        "ParticleSystemComponent",
        sol::base_classes,
        sol::bases<UPrimitiveComponent, USceneComponent, UActorComponent, UObject>(),
        "SetTemplate",
        &UParticleSystemComponent::SetTemplate,
        "SetTemplateByPath",
        [](UParticleSystemComponent& Component, const FString& Path)
        {
            Component.SetTemplate(FParticleSystemManager::Get().Load(Path));
        },
        "GetTemplate",
        &UParticleSystemComponent::GetTemplate,
        "Activate",
        &UParticleSystemComponent::Activate,
        "Deactivate",
        &UParticleSystemComponent::Deactivate,
        "ResetParticles",
        &UParticleSystemComponent::ResetParticles,
        "IsActive",
        &UParticleSystemComponent::IsActive,
        "SetAutoActivate",
        &UParticleSystemComponent::SetAutoActivate,
        "GetAutoActivate",
        &UParticleSystemComponent::GetAutoActivate,
        "SetResetOnActivate",
        &UParticleSystemComponent::SetResetOnActivate,
        "GetResetOnActivate",
        &UParticleSystemComponent::GetResetOnActivate,
        "GetEmitterInstanceCount",
        &UParticleSystemComponent::GetEmitterInstanceCount,
        "GetCurrentLODIndex",
        &UParticleSystemComponent::GetCurrentLODIndex,
        "SetCurrentLODIndex",
        &UParticleSystemComponent::SetCurrentLODIndex,
        "RebuildInstances",
        &UParticleSystemComponent::RebuildInstances,
        "GetTemplatePath",
        &UParticleSystemComponent::GetTemplatePath
    );

}
