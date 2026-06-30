#pragma once
#include "LuaScriptManager.h"
#include "Lua/LuaDebugManager.h"

#include "Audio/AudioManager.h"
#include "Core/Logging/Log.h"
#include "Core/Logging/Notification.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <windows.h>  // PostQuitMessage
#include "Animation/AnimInstance.h"
#include "Animation/AnimationManager.h"
#include "Animation/Graph/AnimGraphInstance.h"
#include "Animation/Instance/LuaAnimInstance.h"
#include "Animation/Montage/AnimMontage.h"
#include "Animation/Sequence/AnimSequence.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "Component/ActorComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Component/ShapeComponent.h"
#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/CineCameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Action/ActionVisualEffectComponent.h"
#include "Component/Input/ActionComponent.h"
#include "Component/Input/InputComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/LightComponent.h"
#include "Component/Light/LightComponentBase.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/Movement/FloatingPawnMovementComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/Movement/PendulumMovementComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/Movement/WheeledVehicleMovementComponent.h"
#include "Component/Particle/ParticleSystemComponent.h"
#include "Component/Primitive/BillboardComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/Primitive/TextRenderComponent.h"
#include "Component/Script/LuaBlueprintComponent.h"
#include "Component/Script/LuaScriptComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Component/Vehicle/VehicleWheelPoseComponent.h"
#include "Core/Property/ArrayProperty.h"
#include "Core/Property/BoolProperty.h"
#include "Core/Property/ClassProperty.h"
#include "Core/Property/EnumProperty.h"
#include "Core/Property/NameProperty.h"
#include "Core/Property/NumericProperty.h"
#include "Core/Property/ObjectProperty.h"
#include "Core/Property/SoftObjectProperty.h"
#include "Core/Property/StringProperty.h"
#include "Core/Property/StructProperty.h"
#include "Core/Types/CollisionTypes.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/Actor/ParticleSystemActor.h"
#include "GameFramework/Camera/CameraModifier.h"
#include "GameFramework/Camera/CameraShakeBase.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "GameFramework/Camera/SequenceCameraShake.h"
#include "GameFramework/Camera/WaveOscillatorCameraShake.h"
#include "GameFramework/GameMode/GameplayStatics.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Pawn/Character.h"
#include "GameFramework/Pawn/Pawn.h"
#include "GameFramework/Pawn/WheeledVehiclePawn.h"
#include "Input/InputKeyCodes.h"
#include "Input/InputSystem.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Static/StaticMesh.h"
#include "Object/GarbageCollection.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include "Object/Reflection/UStruct.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleLODLevel.h"
#include "Particle/ParticleModule.h"
#include "Particle/ParticleSystem.h"
#include "Particle/ParticleSystemManager.h"
#include "Particle/Modules/ParticleModuleRequired.h"
#include "Particle/Modules/ParticleModuleSpawn.h"
#include "Particle/TypeData/ParticleModuleTypeDataBase.h"
#include "Platform/Paths.h"
#include "Platform/WindowsWindow.h"
#include "Profiling/Time/Timer.h"
#include "Runtime/Engine.h"
#include "Texture/Texture2D.h"
#include "UI/UIManager.h"
#include "UI/UserWidget.h"
#include "Viewport/GameViewportClient.h"

namespace LuaActorBindingsDetail
{
    inline bool LuaReadNumber(const sol::object& Object, double& OutValue)
    {
        if (!Object.valid() || Object == sol::nil || Object.get_type() != sol::type::number)
        {
            return false;
        }
        OutValue = Object.as<double>();
        return true;
    }

    inline bool LuaReadFloatField(const sol::table& Table, const char* Name, int Index, float& OutValue)
    {
        double      Number = 0.0;
        sol::object Named  = Table[Name];
        if (LuaReadNumber(Named, Number))
        {
            OutValue = static_cast<float>(Number);
            return true;
        }
        sol::object Indexed = Table[Index];
        if (LuaReadNumber(Indexed, Number))
        {
            OutValue = static_cast<float>(Number);
            return true;
        }
        return false;
    }

    inline bool LuaObjectToVector(const sol::object& Object, FVector& OutVector)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }
        if (Object.is<FVector>())
        {
            OutVector = Object.as<FVector>();
            return true;
        }
        if (Object.get_type() != sol::type::table)
        {
            return false;
        }
        sol::table Table = Object.as<sol::table>();
        float      X     = 0.0f;
        float      Y     = 0.0f;
        float      Z     = 0.0f;
        LuaReadFloatField(Table, "X", 1, X);
        LuaReadFloatField(Table, "Y", 2, Y);
        LuaReadFloatField(Table, "Z", 3, Z);
        OutVector = FVector(X, Y, Z);
        return true;
    }

    inline bool LuaObjectToVector4(const sol::object& Object, FVector4& OutVector)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }
        if (Object.is<FVector4>())
        {
            OutVector = Object.as<FVector4>();
            return true;
        }
        if (Object.get_type() != sol::type::table)
        {
            return false;
        }
        sol::table Table = Object.as<sol::table>();
        float      X     = 0.0f;
        float      Y     = 0.0f;
        float      Z     = 0.0f;
        float      W     = 0.0f;
        LuaReadFloatField(Table, "X", 1, X);
        LuaReadFloatField(Table, "Y", 2, Y);
        LuaReadFloatField(Table, "Z", 3, Z);
        if (!LuaReadFloatField(Table, "W", 4, W))
        {
            LuaReadFloatField(Table, "A", 4, W);
        }
        OutVector = FVector4(X, Y, Z, W);
        return true;
    }

    inline sol::table LuaVector4ToTable(sol::this_state State, const FVector4& Value)
    {
        sol::state_view Lua(State);
        sol::table      Table = Lua.create_table();
        Table["X"]            = Value.X;
        Table["Y"]            = Value.Y;
        Table["Z"]            = Value.Z;
        Table["W"]            = Value.W;
        Table["R"]            = Value.R;
        Table["G"]            = Value.G;
        Table["B"]            = Value.B;
        Table["A"]            = Value.A;
        return Table;
    }
}
