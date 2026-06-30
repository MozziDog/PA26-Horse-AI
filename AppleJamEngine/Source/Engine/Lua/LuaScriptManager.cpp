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
#include "UI/Canvas/UICanvas.h"
#include "UI/Canvas/UICanvasManager.h"
#include "UI/Canvas/UIElement.h"
#include "UI/UIManager.h"
#include "UI/UserWidget.h"
#include "Viewport/GameViewportClient.h"

std::unique_ptr<sol::state>                 FLuaScriptManager::Lua;
sol::protected_function                     FLuaScriptManager::OnEscapePressedCallback;
sol::protected_function                     FLuaScriptManager::OnUnpausedTickCallback;
TMap<FString, sol::protected_function>      FLuaScriptManager::UIButtonCallbacks;
std::mutex                                  FLuaScriptManager::ComponentMutex;
TArray<TWeakObjectPtr<ULuaScriptComponent>> FLuaScriptManager::RegisteredComponents;
TArray<TWeakObjectPtr<ULuaAnimInstance>>    FLuaScriptManager::RegisteredAnimInstances;
FSubscriptionID                             FLuaScriptManager::WatchSub = 0;

namespace
{
    struct FLuaReflectedEventOverride
    {
        TWeakObjectPtr<UObject> Target;
        sol::protected_function Callback;
    };

    TMap<FString, FLuaReflectedEventOverride> GLuaReflectedEventOverrides;

    FString MakeLuaReflectedEventKey(const UObject* Object, const FFunction& Function)
    {
        if (!Object)
        {
            return {};
        }
        return std::to_string(Object->GetUUID()) + "::" + Function.GetSignature();
    }

    void CompactLuaReflectedEventOverrides()
    {
        for (auto It = GLuaReflectedEventOverrides.begin(); It != GLuaReflectedEventOverrides.end();)
        {
            if (!It->second.Target.IsValid() || !It->second.Callback.valid())
            {
                It = GLuaReflectedEventOverrides.erase(It);
                continue;
            }
            ++It;
        }
    }
}

void FLuaScriptManager::Initialize()
{
    Lua = std::make_unique<sol::state>();
    Lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::coroutine, sol::lib::debug);
    (*Lua)["package"]["path"] = FPaths::ToUtf8(FPaths::Combine(FPaths::ScriptDir(), L"?.lua").c_str());

    // 한글 경로 호환을 위해 require 의 파일 검색을 wide-aware 로 교체.
    // Lua 5.2+ 는 package.searchers, Lua 5.1/LuaJIT 은 package.loaders 를 사용한다.
    sol::table  Package       = (*Lua)["package"];
    sol::object Searchers     = Package["searchers"];
    sol::table  ModuleLoaders = Searchers.valid() && Searchers.get_type() == sol::type::table
            ? Searchers.as<sol::table>()
            : Package["loaders"].get<sol::table>();
    ModuleLoaders[2] = [](sol::this_state ts, const std::string& ModName) -> sol::object
    {
        sol::state_view    L(ts);
        const std::wstring WidePath = FPaths::Combine(FPaths::ScriptDir(), FPaths::ToWide(ModName + ".lua"));
        std::error_code    EC;
        if (!std::filesystem::exists(WidePath, EC))
        {
            return sol::make_object(L, std::string("\n\tno file '") + FPaths::ToUtf8(WidePath) + "'");
        }

        FString Content;
        if (!ReadScriptFileContent(ModName + ".lua", Content))
        {
            return sol::make_object(L, std::string("\n\tcannot read '") + FPaths::ToUtf8(WidePath) + "'");
        }

        const FString    ChunkName = FPaths::ToUtf8(WidePath);
        sol::load_result LR        = L.load(Content, ChunkName);
        if (!LR.valid())
        {
            sol::error Err = LR;
            return sol::make_object(L, std::string("\n\t") + Err.what());
        }
        return LR.get<sol::object>();
    };

    // 모든 sol::protected_function 호출의 default error handler 를 debug.traceback 으로 설정.
    // RegisterBindings 안에서 helper Lua 파일을 로드하기 전에 먼저 걸어야 helper load error 도 stacktrace 를 갖는다.
    if (sol::object DebugObject = (*Lua)["debug"]; DebugObject.valid() && DebugObject.get_type() == sol::type::table)
    {
        sol::table  DebugTable      = DebugObject.as<sol::table>();
        sol::object TracebackObject = DebugTable["traceback"];
        if (TracebackObject.valid() && TracebackObject.get_type() == sol::type::function)
        {
            sol::protected_function::set_default_handler(TracebackObject.as<sol::function>());
        }
    }

    FLuaDebugManager::Initialize(Lua->lua_state());
    FLuaDebugManager::RegisterLuaBindings(*Lua);
    RegisterBindings(*Lua);

    FWatchID WatchID = FDirectoryWatcher::Get().Watch(FPaths::ScriptDir(), "");
    if (WatchID != 0)
    {
        WatchSub = FDirectoryWatcher::Get().Subscribe(
            WatchID,
            [](const TSet<FString>& Files)
            {
                FLuaScriptManager::OnScriptsChanged(Files);
            }
        );
    }
}

void FLuaScriptManager::Shutdown()
{
    if (WatchSub != 0)
    {
        FDirectoryWatcher::Get().Unsubscribe(WatchSub);
        WatchSub = 0;
    }

    TArray<TWeakObjectPtr<ULuaScriptComponent>> ComponentsToRelease;
    TArray<TWeakObjectPtr<ULuaAnimInstance>>    AnimInstancesToRelease;
    {
        std::lock_guard<std::mutex> Lock(ComponentMutex);
        ComponentsToRelease    = RegisteredComponents;
        AnimInstancesToRelease = RegisteredAnimInstances;
    }

    // lua_State 가 살아있는 동안 런타임 객체들이 들고 있는 sol reference 를 먼저 해제한다.
    // 이 작업을 Lua.reset() 뒤로 미루면 GC/dtor 단계에서 sol::basic_reference::~basic_reference 가
    // 닫힌 lua_State 에 luaL_unref 를 호출하며 lua51.dll 내부에서 크래시난다.
    for (const TWeakObjectPtr<ULuaScriptComponent>& ComponentPtr : ComponentsToRelease)
    {
        if (ULuaScriptComponent* Component = ComponentPtr.GetEvenIfPendingKill())
        {
            if (IsAliveObject(Component))
            {
                Component->ReleaseLuaRuntimeForShutdown();
            }
        }
    }
    for (const TWeakObjectPtr<ULuaAnimInstance>& InstancePtr : AnimInstancesToRelease)
    {
        if (ULuaAnimInstance* Instance = InstancePtr.GetEvenIfPendingKill())
        {
            if (IsAliveObject(Instance))
            {
                Instance->ReleaseLuaRuntimeForShutdown();
            }
        }
    }

    // ULuaBlueprintComponent 는 ULuaScriptComponent/ULuaAnimInstance 와 달리 별도 레지스트리에
    // 등록되지 않으므로, 전역 객체 배열을 훑어 lua_State 가 살아있는 동안 sol 핸들을 해제한다.
    // (누락 시 FEngineLoop::Shutdown 의 최종 GC sweep → BeginDestroy → ClearLuaRuntime 이
    //  이미 닫힌 lua_State 에 luaL_unref 를 호출하며 lua51.dll 에서 크래시)
    for (UObject* Obj : GUObjectArray)
    {
        if (!IsAliveObject(Obj) || !Obj->IsA<ULuaBlueprintComponent>())
        {
            continue;
        }
        static_cast<ULuaBlueprintComponent*>(Obj)->ReleaseLuaRuntimeForShutdown();
    }

    {
        std::lock_guard<std::mutex> Lock(ComponentMutex);
        RegisteredComponents.clear();
        RegisteredAnimInstances.clear();
    }

    // 등록된 Lua 콜백 (sol::protected_function 들) 을 lua_State 가 살아있는 동안 먼저 release.
    // static 멤버라 프로그램 종료 시점까지 살아있는데, 그때 destructor 가 luaL_unref 를
    // 호출하면서 이미 reset 된 lua_State 를 만지면 크래시. 빈 함수로 덮어써 deref 를 지금
    // (Lua 가 valid 한 동안) 일으킨다.
    OnEscapePressedCallback = sol::protected_function();
    OnUnpausedTickCallback = sol::protected_function();
    GLuaReflectedEventOverrides.clear();

    FLuaDebugManager::Shutdown();
    Lua.reset();
}

FString FLuaScriptManager::ResolveScriptPath(const FString& ScriptFile)
{
    std::wstring FullPath = FPaths::Combine(FPaths::ScriptDir(), FPaths::ToWide(ScriptFile));
    return FPaths::ToUtf8(FullPath);
}

bool FLuaScriptManager::OpenOrCreateScript(const FString& ScriptFile)
{
    std::wstring FullPath = FPaths::Combine(FPaths::ScriptDir(), FPaths::ToWide(ScriptFile));
    if (!std::filesystem::exists(FullPath))
    {
        FPaths::CreateDir(FPaths::ScriptDir());

        const std::wstring TemplatePath = FPaths::Combine(FPaths::ScriptDir(), L"template.lua");
        std::error_code    Error;
        if (std::filesystem::exists(TemplatePath))
        {
            std::filesystem::copy_file(TemplatePath, FullPath, std::filesystem::copy_options::none, Error);
            if (Error)
            {
                UE_LOG("Failed to copy Lua script template: %s", Error.message().c_str());
            }
        }

        if (!std::filesystem::exists(FullPath))
        {
            std::ofstream Out(FullPath);
            if (!Out)
            {
                return false;
            }
        }
    }

    HINSTANCE HInst = ShellExecuteW(nullptr, L"open", FullPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    if ((INT_PTR)HInst <= 32)
    {
        return false;
    }

    return true;
}

bool FLuaScriptManager::ReadScriptFileContent(const FString& ScriptFile, FString& OutContent)
{
    const std::wstring WidePath = FPaths::Combine(FPaths::ScriptDir(), FPaths::ToWide(ScriptFile));
    std::ifstream      File(WidePath.c_str(), std::ios::binary);
    if (!File.is_open())
    {
        return false;
    }
    std::ostringstream SS;
    SS << File.rdbuf();
    OutContent = SS.str();
    return true;
}

bool FLuaScriptManager::IsInitialized()
{
    return Lua != nullptr;
}

void FLuaScriptManager::SetOnEscapePressed(sol::protected_function Callback)
{
    OnEscapePressedCallback = std::move(Callback);
}

void FLuaScriptManager::FireOnEscapePressed()
{
    if (!OnEscapePressedCallback.valid())
    {
        return;
    }
    FScopedGarbageCollectionBlocker GCBlocker;
    sol::protected_function_result  Result = OnEscapePressedCallback();
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("[Lua] OnEscapePressed callback error: %s", Err.what());
	}
}

void FLuaScriptManager::SetOnUnpausedTick(sol::protected_function Callback)
{
    OnUnpausedTickCallback = std::move(Callback);
    UE_LOG("[Lua] OnUnpausedTick registered valid=%d", OnUnpausedTickCallback.valid() ? 1 : 0);
}

void FLuaScriptManager::FireUnpausedTick(float DeltaTime)
{
    if (!OnUnpausedTickCallback.valid())
    {
        return;
    }
    FScopedGarbageCollectionBlocker GCBlocker;
    sol::protected_function_result  Result = OnUnpausedTickCallback(DeltaTime);
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("[Lua] OnUnpausedTick callback error: %s", Err.what());
    }
}

void FLuaScriptManager::BindUIButton(const FString& ElementName, sol::protected_function Callback)
{
    // (b) UI.BindButton("name", fn) — ElementName 키로 클릭 콜백 등록. 빈 이름은 무시(바인딩 대상 아님).
    if (ElementName.empty())
    {
        return;
    }
    UIButtonCallbacks[ElementName] = std::move(Callback);
}

void FLuaScriptManager::InvokeUIButtonCallback(const FString& ElementName)
{
    if (ElementName.empty())
    {
        return;
    }
    auto It = UIButtonCallbacks.find(ElementName);
    if (It == UIButtonCallbacks.end() || !It->second.valid())
    {
        return;
    }
    FScopedGarbageCollectionBlocker GCBlocker;
    sol::protected_function_result Result = It->second();
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("[Lua] UI button callback error: %s", Err.what());
    }
}

void FLuaScriptManager::RunScriptFile(const FString& ScriptFile)
{
    // CallLua 액션 — 선택한 .lua 파일을 읽어 실행. 클릭 시 1회. (전역 함수명은 에디터에서 열거 불가하므로
    // 파일 단위로 다룬다 — 핸들러 스크립트는 최상위에서 Engine.LoadScene 등 원하는 동작을 수행하면 된다.)
    if (ScriptFile.empty() || !Lua)
    {
        return;
    }
    FString Content;
    if (!ReadScriptFileContent(ScriptFile, Content))
    {
        UE_LOG("[Lua] UI button CallLua: 스크립트 읽기 실패 — %s", ScriptFile.c_str());
        return;
    }
    FScopedGarbageCollectionBlocker GCBlocker;
    sol::protected_function_result Result = Lua->safe_script(Content, sol::script_pass_on_error);
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("[Lua] UI button CallLua '%s' error: %s", ScriptFile.c_str(), Err.what());
    }
}

void FLuaScriptManager::FireWorldReset()
{
    if (!Lua) return;

    // [UI 버튼 액션] (b) 콜백은 옛 월드의 위젯/클로저를 캡처할 수 있으므로 씬 전환 시 모두 비운다.
    UIButtonCallbacks.clear();
    OnUnpausedTickCallback = sol::protected_function();

    // require 로 한 번 로드된 모듈 테이블은 package.loaded 에 캐시된다. 씬 전환 시에도
    // 살아남기 때문에, 이 두 모듈이 보유한 죽은-월드 참조를 비워준다.
    sol::table Loaded = (*Lua)["package"]["loaded"];
    if (!Loaded.valid()) return;

    // 1) CoroutineManager — 옛 액터의 lua 클로저가 캡처한 환경의 obj 가 dangling.
    //    Wait(30) 도중에 씬 전환되면 새 월드 Tick 에서 만료되면서 freed AActor* deref.
    if (sol::object Coro = Loaded["CoroutineManager"]; Coro.valid() && Coro.get_type() == sol::type::table)
    {
        Coro.as<sol::table>()["coroutines"] = Lua->create_table();
    }

    // 2) ObjRegistry — 액터 핸들 캐시. 새 월드의 BeginPlay 가 다시 등록해줄 때까지 nil 로.
    if (sol::object Reg = Loaded["ObjRegistry"]; Reg.valid() && Reg.get_type() == sol::type::table)
    {
        sol::table T    = Reg.as<sol::table>();
        T["car"]        = sol::nil;
        T["carCamera"]  = sol::nil;
        T["carGas"]     = sol::nil;
        T["manObj"]     = sol::nil;
        T["manCamera"]  = sol::nil;
        T["gasNozzle"]  = sol::nil;
        T["carWasher"]  = sol::nil;
        T["dirtyCar"]   = sol::nil;
        T["policeCars"] = Lua->create_table();
    }
}

void FLuaScriptManager::RegisterComponent(ULuaScriptComponent* Component)
{
    if (!IsAliveObject(Component)) return;

    std::lock_guard<std::mutex> Lock(ComponentMutex);
    for (const TWeakObjectPtr<ULuaScriptComponent>& Existing : RegisteredComponents)
    {
        if (Existing.Get() == Component)
        {
            return;
        }
    }
    RegisteredComponents.push_back(Component);
}

void FLuaScriptManager::UnregisterComponent(ULuaScriptComponent* Component)
{
    if (!Component) return;

    std::lock_guard<std::mutex> Lock(ComponentMutex);
    RegisteredComponents.erase(
        std::remove_if(
            RegisteredComponents.begin(),
            RegisteredComponents.end(),
            [Component](const TWeakObjectPtr<ULuaScriptComponent>& Existing)
            {
                return Existing.Get() == Component || !Existing.IsValid();
            }
        ),
        RegisteredComponents.end()
    );
}

void FLuaScriptManager::RegisterAnimInstance(ULuaAnimInstance* Instance)
{
    if (!IsAliveObject(Instance)) return;
    std::lock_guard<std::mutex> Lock(ComponentMutex);
    for (const TWeakObjectPtr<ULuaAnimInstance>& Existing : RegisteredAnimInstances)
    {
        if (Existing.Get() == Instance)
        {
            return;
        }
    }
    RegisteredAnimInstances.push_back(Instance);
}

void FLuaScriptManager::UnregisterAnimInstance(ULuaAnimInstance* Instance)
{
    if (!Instance) return;
    std::lock_guard<std::mutex> Lock(ComponentMutex);
    RegisteredAnimInstances.erase(
        std::remove_if(
            RegisteredAnimInstances.begin(),
            RegisteredAnimInstances.end(),
            [Instance](const TWeakObjectPtr<ULuaAnimInstance>& Existing)
            {
                return Existing.Get() == Instance || !Existing.IsValid();
            }
        ),
        RegisteredAnimInstances.end()
    );
}

void FLuaScriptManager::OnScriptsChanged(const TSet<FString>& ChangedFiles)
{
    TSet<ULuaScriptComponent*> Targets;

    InvalidateChangedModules(ChangedFiles);

    {
        std::lock_guard<std::mutex> Lock(ComponentMutex);
        RegisteredComponents.erase(
            std::remove_if(
                RegisteredComponents.begin(),
                RegisteredComponents.end(),
                [](const TWeakObjectPtr<ULuaScriptComponent>& Component)
                {
                    return !Component.IsValid();
                }
            ),
            RegisteredComponents.end()
        );
        for (const TWeakObjectPtr<ULuaScriptComponent>& ComponentPtr : RegisteredComponents)
        {
            ULuaScriptComponent* Component = ComponentPtr.Get();
            if (!IsValid(Component)) continue;

            const FString& ScriptFile = Component->GetScriptFile();
            if (ScriptFile.empty()) continue;

            for (const FString& File : ChangedFiles)
            {
                if (File == ScriptFile)
                {
                    Targets.insert(Component);
                    break;
                }
            }
        }
    }

    for (ULuaScriptComponent* Component : Targets)
    {
        if (!Component) continue;

        UE_LOG("[LuaHotReload] Reloading: %s", Component->GetScriptFile().c_str());
        FNotificationManager::Get().AddNotification("Lua Reloaded: " + Component->GetScriptFile(), ENotificationType::Success, 3.0f);
        Component->ReloadScript();
    }

    // AnimInstance 측도 같은 패턴 — 매칭되는 ScriptFile 의 인스턴스 reload.
    TSet<ULuaAnimInstance*> AnimTargets;
    {
        std::lock_guard<std::mutex> Lock(ComponentMutex);
        RegisteredAnimInstances.erase(
            std::remove_if(
                RegisteredAnimInstances.begin(),
                RegisteredAnimInstances.end(),
                [](const TWeakObjectPtr<ULuaAnimInstance>& Instance)
                {
                    return !Instance.IsValid();
                }
            ),
            RegisteredAnimInstances.end()
        );
        for (const TWeakObjectPtr<ULuaAnimInstance>& InstPtr : RegisteredAnimInstances)
        {
            ULuaAnimInstance* Inst = InstPtr.Get();
            if (!IsValid(Inst)) continue;
            const FString& AnimScript = Inst->ScriptFile;
            if (AnimScript.empty()) continue;
            for (const FString& File : ChangedFiles)
            {
                if (File == AnimScript)
                {
                    AnimTargets.insert(Inst);
                    break;
                }
            }
        }
    }
    for (ULuaAnimInstance* Inst : AnimTargets)
    {
        if (!Inst) continue;
        UE_LOG("[LuaHotReload] Reloading Anim: %s", Inst->ScriptFile.c_str());
        FNotificationManager::Get().AddNotification("Anim Reloaded: " + Inst->ScriptFile, ENotificationType::Success, 3.0f);
        Inst->ReloadScript();
    }
}

void FLuaScriptManager::InvalidateChangedModules(const TSet<FString>& ChangedFiles)
{
    if (!Lua) return;

    sol::table Loaded = (*Lua)["package"]["loaded"];
    if (!Loaded.valid()) return;

    for (const FString& File : ChangedFiles)
    {
        FString ModuleName = GetModuleNameFromPath(File);
        if (ModuleName.empty()) continue;

        Loaded[ModuleName] = sol::nil;
        UE_LOG("[LuaHotReload] Invalidated module: %s", ModuleName.c_str());
    }
}

FString FLuaScriptManager::GetModuleNameFromPath(const FString& ScriptPath)
{
    if (ScriptPath.empty())
    {
        return {};
    }

    FString Normalized = ScriptPath;
    for (char& Ch : Normalized)
    {
        if (Ch == '\\')
        {
            Ch = '/';
        }
    }

    constexpr const char* LuaExt = ".lua";
    if (Normalized.size() <= 4 || Normalized.substr(Normalized.size() - 4) != LuaExt)
    {
        return {};
    }

    Normalized.erase(Normalized.size() - 4);
    for (char& Ch : Normalized)
    {
        if (Ch == '/')
        {
            Ch = '.';
        }
    }

    return Normalized;
}

namespace
{
    const char* LuaPropertyTypeName(EPropertyType Type)
    {
        switch (Type)
        {
        case EPropertyType::Bool:
            return "Bool";
        case EPropertyType::ByteBool:
            return "ByteBool";
        case EPropertyType::Int:
            return "Int";
        case EPropertyType::Float:
            return "Float";
        case EPropertyType::Vec3:
            return "Vec3";
        case EPropertyType::Vec4:
            return "Vec4";
        case EPropertyType::Rotator:
            return "Rotator";
        case EPropertyType::String:
            return "String";
        case EPropertyType::Name:
            return "Name";
        case EPropertyType::ObjectRef:
            return "ObjectRef";
        case EPropertyType::Color4:
            return "Color4";
        case EPropertyType::ClassRef:
            return "ClassRef";
        case EPropertyType::Enum:
            return "Enum";
        case EPropertyType::Struct:
            return "Struct";
        case EPropertyType::SoftObjectRef:
            return "SoftObjectRef";
        case EPropertyType::Array:
            return "Array";
        default:
            return "Unknown";
        }
    }

    bool LuaReadNumber(const sol::object& Object, double& OutValue)
    {
        if (!Object.valid() || Object == sol::nil || Object.get_type() != sol::type::number)
        {
            return false;
        }
        OutValue = Object.as<double>();
        return true;
    }

    bool LuaReadFloatField(const sol::table& Table, const char* Name, int Index, float& OutValue)
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

    bool LuaObjectToVector(const sol::object& Object, FVector& OutVector)
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

    bool LuaObjectToVector4(const sol::object& Object, FVector4& OutVector)
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

    sol::table LuaVector4ToTable(sol::this_state State, const FVector4& Value)
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

    sol::object LuaValueToObject(sol::this_state State, const FProperty& Property, void* ValuePtr);
    bool        LuaObjectToValue(
        const FProperty&   Property,
        void*              ValuePtr,
        const sol::object& Object,
        FString*           OutError = nullptr
    );

    bool LuaObjectToEnumValue(const FEnum* EnumType, const sol::object& Object, int64& OutValue)
    {
        if (!Object.valid() || Object == sol::nil)
        {
            return false;
        }
        if (Object.get_type() == sol::type::number)
        {
            OutValue = static_cast<int64>(Object.as<double>());
            return true;
        }
        if (Object.get_type() == sol::type::string)
        {
            FString Name = Object.as<FString>();
            if (!EnumType)
            {
                return false;
            }
            for (uint32 Index = 0; Index < EnumType->GetCount(); ++Index)
            {
                const char* EntryName = EnumType->GetNameAt(Index);
                if (EntryName && Name == EntryName)
                {
                    OutValue = EnumType->GetValueAt(Index);
                    return true;
                }
            }
        }
        return false;
    }

    sol::object LuaStructToTable(sol::this_state State, const FStructProperty& Property, void* ValuePtr)
    {
        sol::state_view Lua(State);
        sol::table      Table      = Lua.create_table();
        UStruct*        StructType = Property.GetStructType();
        if (!StructType || !ValuePtr)
        {
            return sol::make_object(Lua, Table);
        }
        TArray<const FProperty*> Children;
        StructType->GetPropertyRefs(Children);
        for (const FProperty* Child : Children)
        {
            if (!Child || !Child->Name)
            {
                continue;
            }
            Table[Child->Name] = LuaValueToObject(State, *Child, Child->GetValuePtrFor(ValuePtr));
        }
        return sol::make_object(Lua, Table);
    }

    bool LuaTableToStruct(const FStructProperty& Property, void* ValuePtr, const sol::object& Object, FString* OutError)
    {
        if (!ValuePtr)
        {
            if (OutError) *OutError = "null struct storage";
            return false;
        }
        if (Object.get_type() != sol::type::table)
        {
            if (OutError) *OutError = "expected table for struct";
            return false;
        }
        UStruct* StructType = Property.GetStructType();
        if (!StructType)
        {
            if (OutError) *OutError = "struct type is not registered";
            return false;
        }
        sol::table               Table = Object.as<sol::table>();
        TArray<const FProperty*> Children;
        StructType->GetPropertyRefs(Children);
        for (const FProperty* Child : Children)
        {
            if (!Child || !Child->Name)
            {
                continue;
            }
            sol::object Field = Table[Child->Name];
            if (!Field.valid() || Field == sol::nil)
            {
                continue;
            }
            if (!LuaObjectToValue(*Child, Child->GetValuePtrFor(ValuePtr), Field, OutError))
            {
                return false;
            }
        }
        return true;
    }

    sol::object LuaArrayToTable(sol::this_state State, const FArrayProperty& Property, void* ValuePtr)
    {
        sol::state_view                  Lua(State);
        sol::table                       Table = Lua.create_table();
        const FArrayProperty::FArrayOps* Ops   = Property.GetArrayOps();
        const FProperty*                 Inner = Property.GetInnerProperty();
        if (!ValuePtr || !Ops || !Ops->GetNum || !Ops->GetElementPtr || !Inner)
        {
            return sol::make_object(Lua, Table);
        }
        const size_t Count = Ops->GetNum(ValuePtr);
        for (size_t Index = 0; Index < Count; ++Index)
        {
            Table[static_cast<int>(Index + 1)] = LuaValueToObject(State, *Inner, Ops->GetElementPtr(ValuePtr, Index));
        }
        return sol::make_object(Lua, Table);
    }

    bool LuaTableToArray(const FArrayProperty& Property, void* ValuePtr, const sol::object& Object, FString* OutError)
    {
        if (Object.get_type() != sol::type::table)
        {
            if (OutError) *OutError = "expected table for array";
            return false;
        }
        const FArrayProperty::FArrayOps* Ops   = Property.GetArrayOps();
        const FProperty*                 Inner = Property.GetInnerProperty();
        if (!ValuePtr || !Ops || !Ops->Resize || !Ops->GetElementPtr || !Inner)
        {
            if (OutError) *OutError = "array reflection ops are missing";
            return false;
        }
        sol::table   Table = Object.as<sol::table>();
        const size_t Count = static_cast<size_t>(Table.size());
        Ops->Resize(ValuePtr, Count);
        for (size_t Index = 0; Index < Count; ++Index)
        {
            sol::object Element = Table[static_cast<int>(Index + 1)];
            if (!LuaObjectToValue(*Inner, Ops->GetElementPtr(ValuePtr, Index), Element, OutError))
            {
                return false;
            }
        }
        return true;
    }

    sol::object LuaValueToObject(sol::this_state State, const FProperty& Property, void* ValuePtr)
    {
        sol::state_view Lua(State);
        if (!ValuePtr)
        {
            return sol::make_object(Lua, sol::nil);
        }

        switch (Property.GetType())
        {
        case EPropertyType::Bool:
            return sol::make_object(Lua, *static_cast<bool*>(ValuePtr));
        case EPropertyType::ByteBool:
            return sol::make_object(Lua, *static_cast<uint8*>(ValuePtr) != 0);
        case EPropertyType::Int:
            return sol::make_object(Lua, *static_cast<int32*>(ValuePtr));
        case EPropertyType::Float:
            return sol::make_object(Lua, *static_cast<float*>(ValuePtr));
        case EPropertyType::String:
            return sol::make_object(Lua, *static_cast<FString*>(ValuePtr));
        case EPropertyType::Name:
            return sol::make_object(Lua, static_cast<FName*>(ValuePtr)->ToString());
        case EPropertyType::Vec3:
            return sol::make_object(Lua, *static_cast<FVector*>(ValuePtr));
        case EPropertyType::Rotator:
            return sol::make_object(Lua, static_cast<FRotator*>(ValuePtr)->ToVector());
        case EPropertyType::Vec4:
        case EPropertyType::Color4:
            return sol::make_object(Lua, LuaVector4ToTable(State, *static_cast<FVector4*>(ValuePtr)));
        case EPropertyType::Enum:
        {
            const FEnum* EnumType  = Property.GetEnumType();
            int64        EnumValue = 0;
            std::memcpy(&EnumValue, ValuePtr, std::min<size_t>(Property.Size, sizeof(EnumValue)));
            if (EnumType)
            {
                const char* Name = EnumType->GetNameByValue(EnumValue);
                if (Name && std::strcmp(Name, "Unknown") != 0)
                {
                    return sol::make_object(Lua, FString(Name));
                }
            }
            return sol::make_object(Lua, EnumValue);
        }
        case EPropertyType::ObjectRef:
        {
            const FObjectProperty* ObjectProperty = Property.AsObjectProperty();
            return sol::make_object(
                Lua,
                ObjectProperty ? ObjectProperty->GetObjectValueFromValuePtr(ValuePtr) : nullptr
            );
        }
        case EPropertyType::ClassRef:
        {
            const FClassProperty* ClassProperty = Property.AsClassProperty();
            UClass*               Class         = ClassProperty ? ClassProperty->GetClassValueFromValuePtr(ValuePtr) : nullptr;
            return Class ? sol::make_object(Lua, FString(Class->GetName())) : sol::make_object(Lua, sol::nil);
        }
        case EPropertyType::SoftObjectRef:
        {
            const FSoftObjectProperty* SoftProperty = Property.AsSoftObjectProperty();
            return SoftProperty ? sol::make_object(Lua, SoftProperty->GetPathFromValuePtr(ValuePtr))
                    : sol::make_object(Lua, sol::nil);
        }
        case EPropertyType::Struct:
        {
            const FStructProperty* StructProperty = Property.AsStructProperty();
            return StructProperty ? LuaStructToTable(State, *StructProperty, ValuePtr)
                    : sol::make_object(Lua, sol::nil);
        }
        case EPropertyType::Array:
        {
            const FArrayProperty* ArrayProperty = Property.AsArrayProperty();
            return ArrayProperty ? LuaArrayToTable(State, *ArrayProperty, ValuePtr) : sol::make_object(Lua, sol::nil);
        }
        default:
            return sol::make_object(Lua, sol::nil);
        }
    }

    bool LuaObjectToValue(const FProperty& Property, void* ValuePtr, const sol::object& Object, FString* OutError)
    {
        if (!ValuePtr)
        {
            if (OutError) *OutError = "null value storage";
            return false;
        }
        if (!Object.valid() || Object == sol::nil)
        {
            if (Property.GetType() == EPropertyType::ObjectRef)
            {
                if (const FObjectProperty* ObjectProperty = Property.AsObjectProperty())
                {
                    ObjectProperty->SetObjectValueFromValuePtr(ValuePtr, nullptr);
                    return true;
                }
            }
            if (Property.GetType() == EPropertyType::ClassRef)
            {
                if (const FClassProperty* ClassProperty = Property.AsClassProperty())
                {
                    ClassProperty->SetClassValueFromValuePtr(ValuePtr, nullptr);
                    return true;
                }
            }
            if (OutError) *OutError = FString("nil is not assignable to ") + LuaPropertyTypeName(Property.GetType());
            return false;
        }

        switch (Property.GetType())
        {
        case EPropertyType::Bool:
            if (!Object.is<bool>())
            {
                if (OutError) *OutError = "expected bool";
                return false;
            }
            *static_cast<bool*>(ValuePtr) = Object.as<bool>();
            return true;
        case EPropertyType::ByteBool:
            if (!Object.is<bool>())
            {
                if (OutError) *OutError = "expected bool";
                return false;
            }
            *static_cast<uint8*>(ValuePtr) = Object.as<bool>() ? 1 : 0;
            return true;
        case EPropertyType::Int:
            if (Object.get_type() != sol::type::number)
            {
                if (OutError) *OutError = "expected number";
                return false;
            }
            *static_cast<int32*>(ValuePtr) = static_cast<int32>(Object.as<double>());
            return true;
        case EPropertyType::Float:
            if (Object.get_type() != sol::type::number)
            {
                if (OutError) *OutError = "expected number";
                return false;
            }
            *static_cast<float*>(ValuePtr) = static_cast<float>(Object.as<double>());
            return true;
        case EPropertyType::String:
            if (Object.get_type() != sol::type::string)
            {
                if (OutError) *OutError = "expected string";
                return false;
            }
            *static_cast<FString*>(ValuePtr) = Object.as<FString>();
            return true;
        case EPropertyType::Name:
            if (Object.get_type() != sol::type::string)
            {
                if (OutError) *OutError = "expected string";
                return false;
            }
            *static_cast<FName*>(ValuePtr) = FName(Object.as<FString>());
            return true;
        case EPropertyType::Vec3:
        {
            FVector Vector;
            if (!LuaObjectToVector(Object, Vector))
            {
                if (OutError) *OutError = "expected Vector or {X,Y,Z}";
                return false;
            }
            *static_cast<FVector*>(ValuePtr) = Vector;
            return true;
        }
        case EPropertyType::Rotator:
        {
            FVector Vector;
            if (!LuaObjectToVector(Object, Vector))
            {
                if (OutError) *OutError = "expected Vector or {X,Y,Z}";
                return false;
            }
            *static_cast<FRotator*>(ValuePtr) = FRotator(Vector);
            return true;
        }
        case EPropertyType::Vec4:
        case EPropertyType::Color4:
        {
            FVector4 Vector;
            if (!LuaObjectToVector4(Object, Vector))
            {
                if (OutError) *OutError = "expected {X,Y,Z,W}";
                return false;
            }
            *static_cast<FVector4*>(ValuePtr) = Vector;
            return true;
        }
        case EPropertyType::Enum:
        {
            int64 EnumValue = 0;
            if (!LuaObjectToEnumValue(Property.GetEnumType(), Object, EnumValue))
            {
                if (OutError) *OutError = "expected enum name or value";
                return false;
            }
            std::memcpy(ValuePtr, &EnumValue, std::min<size_t>(Property.Size, sizeof(EnumValue)));
            return true;
        }
        case EPropertyType::ObjectRef:
        {
            if (!Object.is<UObject*>())
            {
                if (OutError) *OutError = "expected Object";
                return false;
            }
            UObject*               SourceObject   = Object.as<UObject*>();
            const FObjectProperty* ObjectProperty = Property.AsObjectProperty();
            if (!ObjectProperty)
            {
                if (OutError) *OutError = "object property ops missing";
                return false;
            }
            if (SourceObject && !IsValid(SourceObject))
            {
                if (OutError) *OutError = "object is no longer valid";
                return false;
            }
            if (UClass* AllowedClass = ObjectProperty->GetAllowedClassType())
            {
                if (SourceObject && !SourceObject->GetClass()->IsA(AllowedClass))
                {
                    if (OutError) *OutError = "object class is not assignable to parameter";
                    return false;
                }
            }
            ObjectProperty->SetObjectValueFromValuePtr(ValuePtr, SourceObject);
            return true;
        }
        case EPropertyType::ClassRef:
        {
            const FClassProperty* ClassProperty = Property.AsClassProperty();
            if (!ClassProperty)
            {
                if (OutError) *OutError = "class property ops missing";
                return false;
            }
            if (Object.get_type() != sol::type::string)
            {
                if (OutError) *OutError = "expected class name string";
                return false;
            }
            UClass* Class = UClass::FindByName(Object.as<FString>().c_str());
            if (!Class)
            {
                if (OutError) *OutError = "class was not found";
                return false;
            }
            if (UClass* AllowedClass = ClassProperty->GetAllowedClassType())
            {
                if (!Class->IsA(AllowedClass))
                {
                    if (OutError) *OutError = "class is not assignable to parameter";
                    return false;
                }
            }
            ClassProperty->SetClassValueFromValuePtr(ValuePtr, Class);
            return true;
        }
        case EPropertyType::SoftObjectRef:
        {
            const FSoftObjectProperty* SoftProperty = Property.AsSoftObjectProperty();
            if (!SoftProperty)
            {
                if (OutError) *OutError = "soft object property ops missing";
                return false;
            }
            if (Object.get_type() != sol::type::string)
            {
                if (OutError) *OutError = "expected asset path string";
                return false;
            }
            SoftProperty->SetPathFromValuePtr(ValuePtr, Object.as<FString>());
            return true;
        }
        case EPropertyType::Struct:
        {
            const FStructProperty* StructProperty = Property.AsStructProperty();
            return StructProperty ? LuaTableToStruct(*StructProperty, ValuePtr, Object, OutError) : false;
        }
        case EPropertyType::Array:
        {
            const FArrayProperty* ArrayProperty = Property.AsArrayProperty();
            return ArrayProperty ? LuaTableToArray(*ArrayProperty, ValuePtr, Object, OutError) : false;
        }
        default:
            if (OutError) *OutError = "unsupported reflected property type for Lua";
            return false;
        }
    }

    bool LuaCanSkipMissingArgument(const FProperty& Parameter)
    {
        if ((Parameter.Flags & PF_OutParm) != 0 && (Parameter.Flags & PF_ConstParm) == 0)
        {
            return true;
        }
        return Parameter.Metadata.find("defaultvalue") != Parameter.Metadata.end();
    }

    bool LuaTryPrepareFunctionCall(const FFunction& Function, sol::variadic_args Args, void* Storage, FString& OutError)
    {
        const TArray<FProperty*>& Parameters  = Function.GetParameters();
        const size_t              LuaArgCount = Args.size();
        size_t                    LuaArgIndex = 0;

        for (const FProperty* Parameter : Parameters)
        {
            if (!Parameter)
            {
                continue;
            }

            const bool bOutOnly = (Parameter->Flags & PF_OutParm) != 0 && (Parameter->Flags & PF_ConstParm) == 0;
            if (bOutOnly && LuaArgIndex >= LuaArgCount)
            {
                continue;
            }

            if (LuaArgIndex >= LuaArgCount)
            {
                if (LuaCanSkipMissingArgument(*Parameter))
                {
                    continue;
                }
                OutError = FString("missing Lua argument for parameter '") + (Parameter->Name ? Parameter->Name : "") +
                        "'";
                return false;
            }

            sol::object Arg = Args[static_cast<int>(LuaArgIndex)];
            FString     ConvertError;
            if (!LuaObjectToValue(*Parameter, Parameter->GetValuePtrFor(Storage), Arg, &ConvertError))
            {
                OutError = FString("parameter '") + (Parameter->Name ? Parameter->Name : "") + "': " + ConvertError;
                return false;
            }
            ++LuaArgIndex;
        }

        if (LuaArgIndex < LuaArgCount)
        {
            OutError = "too many Lua arguments";
            return false;
        }
        return true;
    }

    sol::object LuaCollectFunctionResult(sol::this_state State, const FFunction& Function, void* Storage)
    {
        sol::state_view          Lua(State);
        const FProperty*         ReturnProperty = Function.GetReturnProperty();
        TArray<const FProperty*> OutParameters;
        for (const FProperty* Parameter : Function.GetParameters())
        {
            if (Parameter && (Parameter->Flags & PF_OutParm) != 0 && (Parameter->Flags & PF_ConstParm) == 0)
            {
                OutParameters.push_back(Parameter);
            }
        }

        if (!ReturnProperty && OutParameters.empty())
        {
            return sol::make_object(Lua, true);
        }
        if (ReturnProperty && OutParameters.empty())
        {
            return LuaValueToObject(State, *ReturnProperty, ReturnProperty->GetValuePtrFor(Storage));
        }

        sol::table Result = Lua.create_table();
        if (ReturnProperty)
        {
            Result["ReturnValue"] = LuaValueToObject(State, *ReturnProperty, ReturnProperty->GetValuePtrFor(Storage));
        }
        for (const FProperty* Parameter : OutParameters)
        {
            Result[(Parameter->Name ? Parameter->Name : "")] = LuaValueToObject(
                State,
                *Parameter,
                Parameter->GetValuePtrFor(Storage)
            );
        }
        return sol::make_object(Lua, Result);
    }

    const FFunction* LuaFindFunctionByNameOrSignature(UStruct* TargetStruct, const FString& FunctionNameOrSignature)
    {
        if (!TargetStruct || FunctionNameOrSignature.empty())
        {
            return nullptr;
        }

        if (FunctionNameOrSignature.find('(') != FString::npos)
        {
            return TargetStruct->FindFunctionBySignature(FunctionNameOrSignature.c_str(), true);
        }

        return TargetStruct->FindFunctionByName(FunctionNameOrSignature.c_str(), true);
    }

    bool LuaCopyFunctionResultFromObject(
        sol::this_state    State,
        const FFunction&   Function,
        void*              Storage,
        const sol::object& ResultObject,
        FString&           OutError
    )
    {
        (void)State;
        const bool bResultIsTable = ResultObject.valid() && ResultObject.get_type() == sol::type::table;

        if (const FProperty* ReturnProperty = Function.GetReturnProperty())
        {
            sol::object ReturnValue = bResultIsTable ? ResultObject.as<sol::table>()["ReturnValue"].get<sol::object>()
                    : ResultObject;
            if (ReturnValue.valid() && ReturnValue != sol::nil)
            {
                FString ConvertError;
                if (!LuaObjectToValue(
                    *ReturnProperty,
                    ReturnProperty->GetValuePtrFor(Storage),
                    ReturnValue,
                    &ConvertError
                ))
                {
                    OutError = FString("return value: ") + ConvertError;
                    return false;
                }
            }
        }

        if (bResultIsTable)
        {
            sol::table ResultTable = ResultObject.as<sol::table>();
            for (const FProperty* Parameter : Function.GetParameters())
            {
                if (!Parameter || (Parameter->Flags & PF_OutParm) == 0 || (Parameter->Flags & PF_ConstParm) != 0)
                {
                    continue;
                }

                sol::object OutValue = ResultTable[Parameter->Name ? Parameter->Name : ""].get<sol::object>();
                if (!OutValue.valid() || OutValue == sol::nil)
                {
                    continue;
                }

                FString ConvertError;
                if (!LuaObjectToValue(*Parameter, Parameter->GetValuePtrFor(Storage), OutValue, &ConvertError))
                {
                    OutError = FString("out parameter '") + (Parameter->Name ? Parameter->Name : "") + "': " +
                            ConvertError;
                    return false;
                }
            }
        }

        return true;
    }

    enum class ELuaEventOverrideResult : uint8
    {
        NotBound,
        Invoked,
        Failed,
    };

    ELuaEventOverrideResult LuaTryInvokeReflectedEventOverride(
        sol::this_state  State,
        UObject*         Instance,
        const FFunction& Function,
        void*            Storage,
        FString&         OutError
    )
    {
        if (!IsValid(Instance) || !Function.HasAnyFunctionFlags(FUNC_Event))
        {
            return ELuaEventOverrideResult::NotBound;
        }

        CompactLuaReflectedEventOverrides();
        auto It = GLuaReflectedEventOverrides.find(MakeLuaReflectedEventKey(Instance, Function));
        if (It == GLuaReflectedEventOverrides.end() || It->second.Target.Get() != Instance || !It->second.Callback.valid())
        {
            return ELuaEventOverrideResult::NotBound;
        }

        TArray<sol::object> LuaArgs;
        for (const FProperty* Parameter : Function.GetParameters())
        {
            if (!Parameter)
            {
                continue;
            }
            const bool bOutOnly = (Parameter->Flags & PF_OutParm) != 0 && (Parameter->Flags & PF_ConstParm) == 0;
            if (bOutOnly)
            {
                continue;
            }
            LuaArgs.push_back(LuaValueToObject(State, *Parameter, Parameter->GetValuePtrFor(Storage)));
        }

        FScopedGarbageCollectionBlocker GCBlocker;
        sol::protected_function_result  Result = It->second.Callback(sol::as_args(LuaArgs));
        if (!Result.valid())
        {
            sol::error Err = Result;
            OutError       = Err.what();
            return ELuaEventOverrideResult::Failed;
        }

        sol::object ResultObject = Result.get<sol::object>();
        if (!LuaCopyFunctionResultFromObject(State, Function, Storage, ResultObject, OutError))
        {
            return ELuaEventOverrideResult::Failed;
        }

        return ELuaEventOverrideResult::Invoked;
    }

    sol::object LuaInvokeReflectedFunctionBySignature(
        sol::this_state    State,
        UObject*           Instance,
        UClass*            StaticClass,
        const FString&     Signature,
        sol::variadic_args Args
    )
    {
        FScopedGarbageCollectionBlocker GCBlocker;
        sol::state_view                 Lua(State);
        if (Instance && !IsValid(Instance))
        {
            UE_LOG("[LuaReflection] Reflection.CallSignature failed: target object is no longer valid");
            return sol::make_object(Lua, sol::nil);
        }
        UStruct* TargetStruct = Instance ? Instance->GetClass() : StaticClass;
        if (!TargetStruct)
        {
            UE_LOG("[LuaReflection] Reflection.CallSignature failed: target has no reflected class");
            return sol::make_object(Lua, sol::nil);
        }

        const FFunction* Function = TargetStruct->FindFunctionBySignature(Signature.c_str(), true);
        if (!Function)
        {
            UE_LOG("[LuaReflection] Reflection.CallSignature failed: function not found: %s", Signature.c_str());
            return sol::make_object(Lua, sol::nil);
        }
        if (!Instance && !Function->IsStatic())
        {
            UE_LOG(
                "[LuaReflection] Reflection.CallSignature failed: non-static function requires object instance: %s",
                Signature.c_str()
            );
            return sol::make_object(Lua, sol::nil);
        }

        void* Storage = Function->CreateParameterStorage();
        if (!Storage)
        {
            UE_LOG(
                "[LuaReflection] Reflection.CallSignature failed: failed to allocate parameter storage: %s",
                Signature.c_str()
            );
            return sol::make_object(Lua, sol::nil);
        }

        FString PrepareError;
        if (!LuaTryPrepareFunctionCall(*Function, Args, Storage, PrepareError))
        {
            Function->DestroyParameterStorage(Storage);
            UE_LOG("[LuaReflection] Reflection.CallSignature failed: %s: %s", Signature.c_str(), PrepareError.c_str());
            return sol::make_object(Lua, sol::nil);
        }

        FString                       EventError;
        const ELuaEventOverrideResult EventResult = LuaTryInvokeReflectedEventOverride(
            State,
            Instance,
            *Function,
            Storage,
            EventError
        );
        if (EventResult == ELuaEventOverrideResult::Failed)
        {
            Function->DestroyParameterStorage(Storage);
            UE_LOG(
                "[LuaReflection] Reflection.CallSignature event override failed: %s: %s",
                Signature.c_str(),
                EventError.c_str()
            );
            return sol::make_object(Lua, sol::nil);
        }

        if (EventResult == ELuaEventOverrideResult::NotBound)
        {
            const bool bInvoked = Function->IsStatic()
                    ? Function->Invoke(nullptr, Storage, nullptr)
                    : Instance->ProcessEvent(Function, Storage, nullptr);
            if (!bInvoked)
            {
                Function->DestroyParameterStorage(Storage);
                UE_LOG("[LuaReflection] Reflection.CallSignature failed: native invoke failed: %s", Signature.c_str());
                return sol::make_object(Lua, sol::nil);
            }
        }

        sol::object Result = LuaCollectFunctionResult(State, *Function, Storage);
        Function->DestroyParameterStorage(Storage);
        return Result;
    }

    bool LuaBindReflectedEventOverride(
        UObject*                Object,
        const FString&          FunctionNameOrSignature,
        sol::protected_function Callback
    )
    {
        if (!IsValid(Object) || !Object->GetClass() || !Callback.valid())
        {
            return false;
        }

        const FFunction* Function = LuaFindFunctionByNameOrSignature(Object->GetClass(), FunctionNameOrSignature);
        if (!Function || !Function->HasAnyFunctionFlags(FUNC_Event))
        {
            UE_LOG("[LuaReflection] BindEvent failed: reflected event not found: %s", FunctionNameOrSignature.c_str());
            return false;
        }

        FLuaReflectedEventOverride Override;
        Override.Target                                                          = Object;
        Override.Callback                                                        = std::move(Callback);
        GLuaReflectedEventOverrides[MakeLuaReflectedEventKey(Object, *Function)] = std::move(Override);
        CompactLuaReflectedEventOverrides();
        return true;
    }

    bool LuaUnbindReflectedEventOverride(UObject* Object, const FString& FunctionNameOrSignature)
    {
        if (!IsValid(Object) || !Object->GetClass())
        {
            return false;
        }

        const FFunction* Function = LuaFindFunctionByNameOrSignature(Object->GetClass(), FunctionNameOrSignature);
        if (!Function)
        {
            return false;
        }

        return GLuaReflectedEventOverrides.erase(MakeLuaReflectedEventKey(Object, *Function)) > 0;
    }

    bool LuaHasReflectedEventOverride(UObject* Object, const FString& FunctionNameOrSignature)
    {
        if (!IsValid(Object) || !Object->GetClass())
        {
            return false;
        }

        const FFunction* Function = LuaFindFunctionByNameOrSignature(Object->GetClass(), FunctionNameOrSignature);
        if (!Function)
        {
            return false;
        }

        CompactLuaReflectedEventOverrides();
        auto It = GLuaReflectedEventOverrides.find(MakeLuaReflectedEventKey(Object, *Function));
        return It != GLuaReflectedEventOverrides.end() && It->second.Target.Get() == Object && It->second.Callback.valid();
    }

    sol::object LuaInvokeReflectedFunction(
        sol::this_state    State,
        UObject*           Instance,
        UClass*            StaticClass,
        const FString&     FunctionName,
        sol::variadic_args Args
    )
    {
        FScopedGarbageCollectionBlocker GCBlocker;
        sol::state_view                 Lua(State);
        if (Instance && !IsValid(Instance))
        {
            UE_LOG("[LuaReflection] Reflection.Call failed: target object is no longer valid");
            return sol::make_object(Lua, sol::nil);
        }
        UStruct* TargetStruct = Instance ? Instance->GetClass() : StaticClass;
        if (!TargetStruct)
        {
            UE_LOG("[LuaReflection] Reflection.Call failed: target has no reflected class");
            return sol::make_object(Lua, sol::nil);
        }

        TArray<const FFunction*> Functions;
        TargetStruct->FindFunctionsByName(FunctionName.c_str(), Functions, true);
        if (Functions.empty())
        {
            UE_LOG("[LuaReflection] Reflection.Call failed: function not found: %s", FunctionName.c_str());
            return sol::make_object(Lua, sol::nil);
        }

        FString LastError;
        for (const FFunction* Function : Functions)
        {
            if (!Function)
            {
                continue;
            }
            if (!Instance && !Function->IsStatic())
            {
                LastError = "non-static function requires object instance";
                continue;
            }

            void* Storage = Function->CreateParameterStorage();
            if (!Storage)
            {
                LastError = "failed to allocate parameter storage";
                continue;
            }

            FString PrepareError;
            if (!LuaTryPrepareFunctionCall(*Function, Args, Storage, PrepareError))
            {
                Function->DestroyParameterStorage(Storage);
                LastError = PrepareError;
                continue;
            }

            FString                       EventError;
            const ELuaEventOverrideResult EventResult = LuaTryInvokeReflectedEventOverride(
                State,
                Instance,
                *Function,
                Storage,
                EventError
            );
            if (EventResult == ELuaEventOverrideResult::Failed)
            {
                Function->DestroyParameterStorage(Storage);
                LastError = FString("event override failed: ") + EventError;
                continue;
            }

            if (EventResult == ELuaEventOverrideResult::NotBound)
            {
                const bool bInvoked = Function->IsStatic()
                        ? Function->Invoke(nullptr, Storage, nullptr)
                        : Instance->ProcessEvent(Function, Storage, nullptr);
                if (!bInvoked)
                {
                    Function->DestroyParameterStorage(Storage);
                    LastError = "native invoke failed";
                    continue;
                }
            }

            sol::object Result = LuaCollectFunctionResult(State, *Function, Storage);
            Function->DestroyParameterStorage(Storage);
            return Result;
        }

        UE_LOG(
            "[LuaReflection] Reflection.Call failed: no overload matched for %s: %s",
            FunctionName.c_str(),
            LastError.c_str()
        );
        return sol::make_object(Lua, sol::nil);
    }

    const FProperty* LuaFindProperty(UObject* Object, const FString& PropertyName)
    {
        if (!IsValid(Object) || !Object->GetClass())
        {
            return nullptr;
        }
        TArray<const FProperty*> Properties;
        Object->GetClass()->GetPropertyRefs(Properties);
        for (const FProperty* Property : Properties)
        {
            if (Property && Property->Name && PropertyName == Property->Name)
            {
                return Property;
            }
        }
        return nullptr;
    }

    sol::object LuaGetReflectedProperty(sol::this_state State, UObject* Object, const FString& PropertyName)
    {
        sol::state_view  Lua(State);
        const FProperty* Property = LuaFindProperty(Object, PropertyName);
        if (!Property)
        {
            return sol::make_object(Lua, sol::nil);
        }
        return LuaValueToObject(State, *Property, Property->GetValuePtrFor(Object));
    }

    bool LuaSetReflectedProperty(UObject* Object, const FString& PropertyName, sol::object Value)
    {
        const FProperty* Property = LuaFindProperty(Object, PropertyName);
        if (!IsValid(Object) || !Property)
        {
            return false;
        }
        FString    Error;
        const bool bOk = LuaObjectToValue(*Property, Property->GetValuePtrFor(Object), Value, &Error);
        if (!bOk)
        {
            UE_LOG(
                "[LuaReflection] SetProperty failed: %s.%s: %s",
                Object->GetClass()->GetName(),
                PropertyName.c_str(),
                Error.c_str()
            );
        }
        return bOk;
    }

    sol::table LuaDescribeProperty(sol::this_state State, const FProperty& Property)
    {
        sol::state_view Lua(State);
        sol::table      Desc = Lua.create_table();
        Desc["Name"]         = Property.Name ? Property.Name : "";
        Desc["DisplayName"]  = Property.DisplayName ? Property.DisplayName : (Property.Name ? Property.Name : "");
        Desc["Category"]     = Property.Category ? Property.Category : "";
        Desc["Type"]         = LuaPropertyTypeName(Property.GetType());
        Desc["Flags"]        = Property.Flags;
        Desc["OwnerClass"]   = Property.OwnerClassName ? Property.OwnerClassName : "";
        if (const FArrayProperty* ArrayProperty = Property.AsArrayProperty())
        {
            Desc["ElementType"] = LuaPropertyTypeName(ArrayProperty->GetElementType());
        }
        if (const FEnum* EnumType = Property.GetEnumType())
        {
            Desc["Enum"] = EnumType->GetName();
        }
        if (UStruct* StructType = Property.GetStructType())
        {
            Desc["Struct"] = StructType->GetName();
        }
        return Desc;
    }

    sol::table LuaDescribeFunction(sol::this_state State, const FFunction& Function)
    {
        sol::state_view Lua(State);
        sol::table      Desc = Lua.create_table();
        Desc["Name"]         = Function.GetName();
        Desc["Signature"]    = Function.GetSignature();
        Desc["DisplayName"]  = Function.GetDisplayName();
        Desc["Category"]     = Function.GetCategory();
        Desc["Flags"]        = Function.GetFlags();
        Desc["Const"]        = Function.IsConst();
        Desc["Static"]       = Function.IsStatic();
        Desc["OwnerClass"]   = Function.OwnerClassName ? Function.OwnerClassName : "";
        sol::table Params    = Lua.create_table();
        int        Index     = 1;
        for (const FProperty* Parameter : Function.GetParameters())
        {
            if (Parameter)
            {
                Params[Index++] = LuaDescribeProperty(State, *Parameter);
            }
        }
        Desc["Parameters"] = Params;
        if (const FProperty* ReturnProperty = Function.GetReturnProperty())
        {
            Desc["Return"] = LuaDescribeProperty(State, *ReturnProperty);
        }
        return Desc;
    }
}

sol::state& FLuaScriptManager::GetState()
{
    return *Lua;
}

void FLuaScriptManager::RegisterBindings(sol::state& Lua)
{
    RegisterLuaHelpers(Lua);
    RegisterCoreBindings(Lua);
    RegisterMathBindings(Lua);
    RegisterReflectionBindings(Lua);
    RegisterActorBindings(Lua);
    RegisterUIBindings(Lua);
}

FInputSystemSnapshot FLuaScriptManager::GetLuaInputSnapshot()
{
    if (GEngine)
    {
        if (UGameViewportClient* GameViewportClient = GEngine->GetGameViewportClient())
        {
            if (GameViewportClient->HasGameInputSnapshot())
            {
                return GameViewportClient->GetGameInputSnapshot();
            }
            return FInputSystemSnapshot {};
        }
    }

    return InputSystem::Get().MakeSnapshot();
}

void FLuaScriptManager::RegisterLuaHelpers(sol::state& Lua)
{
    // 한글 경로 호환 — safe_script_file 은 내부적으로 fopen(UTF-8) 을 쓰므로 ANSI 해석에서
    // 깨진다. wide ifstream 으로 직접 읽어 safe_script(string) 으로 실행.
    FString Content;
    if (!ReadScriptFileContent("CoroutineManager.lua", Content))
    {
        UE_LOG("[Lua] Failed to load CoroutineManager.lua");
        return;
    }
    const FString                  ChunkName = ResolveScriptPath("CoroutineManager.lua");
    sol::protected_function_result Result    = Lua.safe_script(Content, sol::script_pass_on_error, ChunkName);
    if (!Result.valid())
    {
        sol::error Err = Result;
        UE_LOG("[Lua] CoroutineManager.lua error: %s", Err.what());
    }
}

void FLuaScriptManager::RegisterCoreBindings(sol::state& Lua)
{
    Lua.set_function(
        "print",
        [](sol::variadic_args Args)
        {
            FString Message;

            for (auto Arg : Args)
            {
                if (!Message.empty())
                {
                    Message += "\t";
                }

                Message += Arg.as<FString>();
            }

            UE_LOG("[Lua] %s", Message.c_str());
        }
    );

    sol::table Input = Lua.create_named_table("Input");
    Input.set_function(
        "GetKeyDown",
        sol::overload(
            [](const FString& KeyName)
            {
                return GetLuaInputSnapshot().WasPressed(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return GetLuaInputSnapshot().WasPressed(VK);
            }
        )
    );
    Input.set_function(
        "GetKey",
        sol::overload(
            [](const FString& KeyName)
            {
                return GetLuaInputSnapshot().IsDown(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return GetLuaInputSnapshot().IsDown(VK);
            }
        )
    );
    Input.set_function(
        "GetKeyUp",
        sol::overload(
            [](const FString& KeyName)
            {
                return GetLuaInputSnapshot().WasReleased(ResolveInputKeyCode(KeyName));
            },
            [](int VK)
            {
                return GetLuaInputSnapshot().WasReleased(VK);
            }
        )
    );
    Input.set_function(
        "GetMouseDeltaX",
        []()
        {
            return GetLuaInputSnapshot().MouseDeltaX;
        }
    );
    Input.set_function(
        "GetMouseDeltaY",
        []()
        {
            return GetLuaInputSnapshot().MouseDeltaY;
        }
    );

    // Engine — 게임 일시정지 / 종료.
    sol::table Engine = Lua.create_named_table("Engine");
    Engine.set_function(
        "PauseGame",
        []()
        {
            if (GEngine)
            {
                if (UWorld* World = GEngine->GetWorld())
                {
                    World->SetPaused(true);
                }
            }
        }
    );
    Engine.set_function(
        "ResumeGame",
        []()
        {
            if (GEngine)
            {
                if (UWorld* World = GEngine->GetWorld())
                {
                    World->SetPaused(false);
                }
            }
        }
    );
    Engine.set_function(
        "IsPaused",
        []()
        {
            if (GEngine)
            {
                if (UWorld* World = GEngine->GetWorld())
                {
                    return World->IsPaused();
                }
            }
            return false;
        }
    );
    Engine.set_function(
        "GetViewportSize",
        []() -> sol::table
        {
            sol::table Result = FLuaScriptManager::GetState().create_table();
            Result["Width"]   = 0.0f;
            Result["Height"]  = 0.0f;

            if (GEngine)
            {
                if (FWindowsWindow* Window = GEngine->GetWindow())
                {
                    Result["Width"]  = Window->GetWidth();
                    Result["Height"] = Window->GetHeight();
                }
            }

            return Result;
        }
    );
    Engine.set_function(
        "Exit",
        []()
        {
            // WM_QUIT — FEngineLoop::Run 이 PumpMessages 에서 잡고 정상 shutdown.
            PostQuitMessage(0);
        }
    );
    Engine.set_function(
        "SetOnEscape",
        [](sol::protected_function Callback)
        {
            FLuaScriptManager::SetOnEscapePressed(std::move(Callback));
        }
    );
    Engine.set_function(
        "SetOnUnpausedTick",
        [](sol::protected_function Callback)
        {
            FLuaScriptManager::SetOnUnpausedTick(std::move(Callback));
        }
    );

    sol::table Key = Lua.create_named_table("Key");
    for (const FString& KeyName : GetKnownInputKeyNames())
    {
        Key[KeyName.c_str()] = ResolveInputKeyCode(KeyName);
    }
    Key.set_function(
        "Resolve",
        [](const FString& KeyName)
        {
            return ResolveInputKeyCode(KeyName);
        }
    );
    Key.set_function(
        "Name",
        [](int32 KeyCode)
        {
            return GetInputKeyName(KeyCode);
        }
    );

    sol::table CameraManager = Lua.create_named_table("CameraManager");
    CameraManager.set_function(
        "ToggleActorCamera",
        [](const FString& ActorName, sol::optional<float> BlendTime)
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return false;
            }

            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->ToggleActiveCameraForActor(ActorName, BlendTime.value_or(0.0f)) : false;
        }
    );
    CameraManager.set_function(
        "ToggleOwnerCamera",
        [](AActor* Actor, sol::optional<float> BlendTime)
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return false;
            }

            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->ToggleActiveCameraForActor(Actor, BlendTime.value_or(0.0f)) : false;
        }
    );
    CameraManager.set_function(
        "PossessCamera",
        [](UCameraComponent* Camera)
        {
            if (!GEngine || !GEngine->GetWorld() || !IsValid(Camera))
            {
                return false;
            }

            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (!Manager)
            {
                return false;
            }

            Manager->SetActiveCamera(Camera);
            Manager->Possess(Camera);
            return true;
        }
    );
    CameraManager.set_function(
        "GetActiveCameraOwner",
        []() -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return nullptr;
            }
            APlayerController*    PC           = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager      = PC ? PC->GetPlayerCameraManager() : nullptr;
            UCameraComponent*     ActiveCamera = Manager ? Manager->GetActiveCamera() : nullptr;
            if (!IsValid(ActiveCamera)) return nullptr;
            AActor* Owner = ActiveCamera->GetOwner();
            return IsValid(Owner) ? Owner : nullptr;
        }
    );
    CameraManager.set_function(
        "GetActiveCamera",
        []() -> UCameraComponent*
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return nullptr;
            }
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->GetActiveCamera() : nullptr;
        }
    );
    CameraManager.set_function(
        "GetPossessedCamera",
        []() -> UCameraComponent*
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return nullptr;
            }
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            return Manager ? Manager->GetPossessedCamera() : nullptr;
        }
    );
    CameraManager.set_function(
        "GetPossessedCameraOwner",
        []() -> AActor*
        {
            if (!GEngine || !GEngine->GetWorld())
            {
                return nullptr;
            }
            APlayerController*    PC              = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager         = PC ? PC->GetPlayerCameraManager() : nullptr;
            UCameraComponent*     PossessedCamera = Manager ? Manager->GetPossessedCamera() : nullptr;
            if (!IsValid(PossessedCamera)) return nullptr;
            AActor* Owner = PossessedCamera->GetOwner();
            return IsValid(Owner) ? Owner : nullptr;
        }
    );
    CameraManager.set_function(
        "FadeOut",
        [](float Duration)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraFade(0.0f, 1.0f, Duration, FLinearColor::Black(), false, true);
            }
        }
    );
    CameraManager.set_function(
        "FadeIn",
        [](float Duration)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraFade(1.0f, 0.0f, Duration, FLinearColor::Black(), false, true);
            }
        }
    );
    CameraManager.set_function(
        "SetVignette",
        [](float Intensity, float Radius, float Softness)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetCameraVignette(Intensity, Radius, Softness, FLinearColor::Black());
            }
        }
    );
    CameraManager.set_function(
        "ClearVignette",
        []()
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->ClearCameraVignette();
            }
        }
    );
    CameraManager.set_function(
        "SetViewTargetWithBlend",
        [](AActor* Target, float BlendTime)
        {
            if (!GEngine || !GEngine->GetWorld() || !IsValid(Target)) return;

            APlayerController* PC = GEngine->GetWorld()->GetFirstPlayerController();
            if (PC)
            {
                PC->SetViewTargetWithBlend(Target, BlendTime);
            }
        }
    );
    // ActiveCamera 컴포넌트 단위 blend — 같은 액터 내 1인칭/3인칭 같은 별개 카메라
    // 컴포넌트 사이 부드럽게 전환. BlendTime 미지정 시 0 (즉시 swap).
    CameraManager.set_function(
        "SetActiveCameraWithBlend",
        [](UCameraComponent* NewCamera, sol::optional<float> BlendTime)
        {
            if (!GEngine || !GEngine->GetWorld() || !IsValid(NewCamera)) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetActiveCameraWithBlend(NewCamera, BlendTime.value_or(0.0f));
            }
        }
    );
    // Sample wave-oscillator shake — Lua console / 스크립트에서 즉시 흔들기 테스트용.
    // 호출 예: CameraManager.StartWaveShake(1.0)
    CameraManager.set_function(
        "StartWaveShake",
        [](sol::optional<float> Scale)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraShake<UWaveOscillatorCameraShake>(Scale.value_or(1.0f));
            }
        }
    );
    CameraManager.set_function(
        "StartSequenceShake",
        [](sol::optional<float> Scale)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraShake<USequenceCameraShake>(Scale.value_or(1.0f));
            }
        }
    );
    CameraManager.set_function(
        "StartCameraShakeAsset",
        [](const FString& AssetPath, sol::optional<float> Scale)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->StartCameraShakeAsset(AssetPath, Scale.value_or(1.0f));
            }
        }
    );
    CameraManager.set_function(
        "SetDepthOfField",
        [](float FocusDistance, float FocusRange, float MaxBlurRadius)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetDepthOfField(FocusDistance, FocusRange, MaxBlurRadius);
            }
        }
    );
    CameraManager.set_function(
        "SetBokeh",
        [](float RadiusThreshold, float LumaThreshold, float Intensity)
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->SetBokeh(RadiusThreshold, LumaThreshold, Intensity);
            }
        }
    );
    CameraManager.set_function(
        "ClearDepthOfField",
        []()
        {
            if (!GEngine || !GEngine->GetWorld()) return;
            APlayerController*    PC      = GEngine->GetWorld()->GetFirstPlayerController();
            APlayerCameraManager* Manager = PC ? PC->GetPlayerCameraManager() : nullptr;
            if (Manager)
            {
                Manager->ClearDepthOfField();
            }
        }
    );

    sol::table AudioManager = Lua.create_named_table("AudioManager");
    AudioManager.set_function(
        "Load",
        [](const FString& SoundName, const FString& Path, sol::optional<bool> bLoop)
        {
            return FAudioManager::Get().LoadAudio(SoundName, Path, bLoop.value_or(false));
        }
    );
    AudioManager.set_function(
        "Play",
        [](const FString& SoundName, float Volume)
        {
            FAudioManager::Get().PlayAudio(SoundName, Volume);
        }
    );
    AudioManager.set_function(
        "PlayBGM",
        [](const FString& SoundName, float Volume)
        {
            FAudioManager::Get().PlayBGM(SoundName, Volume);
        }
    );
    AudioManager.set_function(
        "StopBGM",
        []()
        {
            FAudioManager::Get().StopBGM();
        }
    );
    AudioManager.set_function(
        "PlayLoop",
        [](const FString& SoundName, const FString& LoopName, sol::optional<float> Volume, sol::optional<float> Pitch)
        {
            FAudioManager::Get().PlayLoop(SoundName, LoopName, Volume.value_or(1.0f), Pitch.value_or(1.0f));
        }
    );
    AudioManager.set_function(
        "StopLoop",
        [](const FString& LoopName)
        {
            FAudioManager::Get().StopLoop(LoopName);
        }
    );
    AudioManager.set_function(
        "StopAllLoops",
        []()
        {
            FAudioManager::Get().StopAllLoops();
        }
    );
    AudioManager.set_function(
        "SetLoopVolume",
        [](const FString& LoopName, float Volume)
        {
            FAudioManager::Get().SetLoopVolume(LoopName, Volume);
        }
    );
    AudioManager.set_function(
        "SetLoopPitch",
        [](const FString& LoopName, float Pitch)
        {
            FAudioManager::Get().SetLoopPitch(LoopName, Pitch);
        }
    );
    AudioManager.set_function(
        "IsLoopPlaying",
        [](const FString& LoopName)
        {
            return FAudioManager::Get().IsLoopPlaying(LoopName);
        }
    );
    AudioManager.set_function(
        "SetMasterVolume",
        [](float Volume)
        {
            FAudioManager::Get().SetMasterVolume(Volume);
        }
    );

    // Short alias for gameplay scripts.
    Lua["Audio"] = AudioManager;

    sol::table Time = Lua.create_named_table("Time");
    Time.set_function(
        "DeltaTime",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetDeltaTime() : 0.0f;
        }
    );
    Time.set_function(
        "RawDeltaTime",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetRawDeltaTime() : 0.0f;
        }
    );
    Time.set_function(
        "TotalTime",
        []() -> double
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetTotalTime() : 0.0;
        }
    );
    Time.set_function(
        "FPS",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetFPS() : 0.0f;
        }
    );
    Time.set_function(
        "DisplayFPS",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetDisplayFPS() : 0.0f;
        }
    );
    Time.set_function(
        "FrameTimeMs",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetFrameTimeMs() : 0.0f;
        }
    );
    Time.set_function(
        "GetTimeDilation",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetTimeDilation() : 1.0f;
        }
    );
    Time.set_function(
        "SetTimeDilation",
        [](float Dilation)
        {
            if (FTimer* T = GEngine ? GEngine->GetTimer() : nullptr) T->SetTimeDilation(Dilation);
        }
    );
    Time.set_function(
        "GetMaxFPS",
        []() -> float
        {
            FTimer* T = GEngine ? GEngine->GetTimer() : nullptr;
            return T ? T->GetMaxFPS() : 0.0f;
        }
    );
    Time.set_function(
        "SetMaxFPS",
        [](float FPS)
        {
            if (FTimer* T = GEngine ? GEngine->GetTimer() : nullptr) T->SetMaxFPS(FPS);
        }
    );


    sol::table Texture = Lua.create_named_table("Texture");
    Texture.set_function(
        "Load",
        [](const FString& Path, sol::optional<bool> bSRGB) -> UTexture2D*
        {
            return UTexture2D::LoadFromCached(Path, bSRGB.value_or(true) ? ETextureColorSpace::SRGB : ETextureColorSpace::Linear);
        }
    );

    sol::table StaticMeshLibrary = Lua.create_named_table("StaticMeshLibrary");
    Lua["StaticMeshes"] = StaticMeshLibrary;
    StaticMeshLibrary.set_function(
        "Load",
        [](const FString& Path) -> UStaticMesh*
        {
            if (!GEngine || Path.empty() || Path == "None") return nullptr;
            ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
            return FMeshManager::LoadStaticMesh(Path, Device);
        }
    );

    sol::table Material = Lua.create_named_table("Material");
    Lua["MaterialLibrary"] = Material;
    Lua["Materials"] = Material;
    Material.set_function(
        "Load",
        [](const FString& Path) -> UMaterial*
        {
            return FMaterialManager::Get().GetOrCreateMaterial(Path);
        }
    );
    Material.set_function(
        "GetOrCreate",
        [](const FString& Path) -> UMaterial*
        {
            return FMaterialManager::Get().GetOrCreateMaterial(Path);
        }
    );
    Material.set_function(
        "Create",
        [](const FString& Path) -> UMaterial*
        {
            return FMaterialManager::Get().CreateMaterialAsset(Path);
        }
    );
    Material.set_function(
        "CreateGraph",
        [](const FString& Path) -> UMaterial*
        {
            return FMaterialManager::Get().CreateGraphMaterialAsset(Path);
        }
    );
    Material.set_function(
        "GetComponentMaterial",
        [](UPrimitiveComponent* Component, int32 ElementIndex) -> UMaterial*
        {
            if (!IsValid(Component)) return nullptr;
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                return StaticMeshComponent->GetMaterial(ElementIndex);
            }
            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                return SkinnedMeshComponent->GetMaterial(ElementIndex);
            }
            return nullptr;
        }
    );
    Material.set_function(
        "SetComponentMaterial",
        [](UPrimitiveComponent* Component, int32 ElementIndex, UMaterial* InMaterial) -> bool
        {
            if (!IsValid(Component) || !IsValid(InMaterial)) return false;
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                StaticMeshComponent->SetMaterial(ElementIndex, InMaterial);
                return true;
            }
            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                SkinnedMeshComponent->SetMaterial(ElementIndex, InMaterial);
                return true;
            }
            return false;
        }
    );
    Material.set_function(
        "SetComponentMaterialByPath",
        [](UPrimitiveComponent* Component, int32 ElementIndex, const FString& MaterialPath) -> bool
        {
            if (!IsValid(Component)) return false;
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                return StaticMeshComponent->SetMaterialByPath(ElementIndex, MaterialPath);
            }
            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                return SkinnedMeshComponent->SetMaterialByPath(ElementIndex, MaterialPath);
            }
            return false;
        }
    );
    Material.set_function(
        "CreateDynamicInstance",
        [](UMaterial* Parent, UObject* Owner, sol::optional<FString> DebugName) -> UMaterialInstanceDynamic*
        {
            return IsValid(Parent) ? UMaterialInstanceDynamic::Create(Parent, Owner, DebugName.value_or(FString())) : nullptr;
        }
    );
    Material.set_function(
        "CreateDynamicInstanceForComponent",
        [](UPrimitiveComponent* Component, int32 ElementIndex, sol::optional<FString> DebugName) -> UMaterialInstanceDynamic*
        {
            if (!IsValid(Component)) return nullptr;

            UMaterial* ParentMaterial = nullptr;
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                ParentMaterial = StaticMeshComponent->GetMaterial(ElementIndex);
                if (UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(ParentMaterial, Component, DebugName.value_or(FString())))
                {
                    StaticMeshComponent->SetMaterial(ElementIndex, Instance);
                    return Instance;
                }
                return nullptr;
            }
            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                ParentMaterial = SkinnedMeshComponent->GetMaterial(ElementIndex);
                if (UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(ParentMaterial, Component, DebugName.value_or(FString())))
                {
                    SkinnedMeshComponent->SetMaterial(ElementIndex, Instance);
                    return Instance;
                }
                return nullptr;
            }
            return nullptr;
        }
    );
    Material.set_function(
        "Save",
        [](UMaterial* Mat, const FString& Path)
        {
            return IsValid(Mat) && FMaterialManager::Get().SaveMaterial(Mat, Path);
        }
    );
    Material.set_function(
        "SetShader",
        [](UMaterial* Mat, const FString& ShaderPath)
        {
            return IsValid(Mat) && FMaterialManager::Get().SetMaterialShader(Mat, ShaderPath);
        }
    );
    Material.set_function(
        "SetScalarParameter",
        [](UMaterial* Mat, const FString& ParamName, float Value) -> bool
        {
            return IsValid(Mat) && Mat->SetScalarParameter(ParamName, Value);
        }
    );
    Material.set_function(
        "SetVectorParameter",
        [](UMaterial* Mat, const FString& ParamName, const sol::object& Value) -> bool
        {
            if (!IsValid(Mat)) return false;
            FVector VectorValue;
            if (!LuaObjectToVector(Value, VectorValue)) return false;
            return Mat->SetVector3Parameter(ParamName, VectorValue);
        }
    );
    Material.set_function(
        "SetColorParameter",
        [](UMaterial* Mat, const FString& ParamName, const sol::object& Value) -> bool
        {
            if (!IsValid(Mat)) return false;
            FVector4 ColorValue;
            if (!LuaObjectToVector4(Value, ColorValue)) return false;
            return Mat->SetVector4Parameter(ParamName, ColorValue);
        }
    );
    Material.set_function(
        "SetTextureParameter",
        [](UMaterial* Mat, const FString& ParamName, UTexture2D* Texture) -> bool
        {
            return IsValid(Mat) && IsValid(Texture) && Mat->SetTextureParameter(ParamName, Texture);
        }
    );

    sol::table CameraShake = Lua.create_named_table("CameraShake");
    CameraShake.set_function(
        "Load",
        [](const FString& Path) -> UCameraShakeAsset*
        {
            return FCameraShakeManager::Get().Load(Path);
        }
    );
    CameraShake.set_function(
        "Find",
        [](const FString& Path) -> UCameraShakeAsset*
        {
            return FCameraShakeManager::Get().Find(Path);
        }
    );
    CameraShake.set_function(
        "Save",
        [](UCameraShakeAsset* Asset)
        {
            return IsValid(Asset) && FCameraShakeManager::Get().Save(Asset);
        }
    );

    Lua.set_function(
        "LoadAudio",
        [](const FString& SoundName, const FString& Path, sol::optional<bool> bLoop)
        {
            return FAudioManager::Get().LoadAudio(SoundName, Path, bLoop.value_or(false));
        }
    );
}

void FLuaScriptManager::RegisterMathBindings(sol::state& Lua)
{
    Lua.new_usertype<FVector>(
        "Vector",
        sol::constructors<FVector(), FVector(float, float, float)>(),
        "X",
        &FVector::X,
        "Y",
        &FVector::Y,
        "Z",
        &FVector::Z,
        "Length",
        &FVector::Length,
        "Normalize",
        &FVector::Normalize,
        "Normalized",
        &FVector::Normalized,
        "Dot",
        &FVector::Dot,
        "Cross",
        sol::overload(
            static_cast<FVector(FVector::*)(const FVector&) const>(&FVector::Cross),
            static_cast<FVector(*)(const FVector&, const FVector&)>(&FVector::Cross)
        ),
        "Distance",
        &FVector::Distance,
        "DistSquared",
        &FVector::DistSquared,
        "Lerp",
        &FVector::Lerp,
        sol::meta_function::addition,
        sol::overload(
            static_cast<FVector(FVector::*)(const FVector&) const>(&FVector::operator+),
            static_cast<FVector(FVector::*)(float) const>(&FVector::operator+)
        ),
        sol::meta_function::subtraction,
        sol::overload(
            static_cast<FVector(FVector::*)(const FVector&) const>(&FVector::operator-),
            static_cast<FVector(FVector::*)(float) const>(&FVector::operator-)
        ),
        sol::meta_function::multiplication,
        static_cast<FVector(FVector::*)(float) const>(&FVector::operator*),
        sol::meta_function::division,
        &FVector::operator/,
        "Zero",
        []()
        {
            return FVector::ZeroVector;
        },
        "One",
        []()
        {
            return FVector::OneVector;
        },
        "Up",
        []()
        {
            return FVector::UpVector;
        },
        "Down",
        []()
        {
            return FVector::DownVector;
        },
        "Forward",
        []()
        {
            return FVector::ForwardVector;
        },
        "Backward",
        []()
        {
            return FVector::BackwardVector;
        },
        "Right",
        []()
        {
            return FVector::RightVector;
        },
        "Left",
        []()
        {
            return FVector::LeftVector;
        },
        "XAxis",
        []()
        {
            return FVector::XAxisVector;
        },
        "YAxis",
        []()
        {
            return FVector::YAxisVector;
        },
        "ZAxis",
        []()
        {
            return FVector::ZAxisVector;
        }
    );

    Lua.set_function(
        "Vec3",
        [](sol::optional<float> X, sol::optional<float> Y, sol::optional<float> Z)
        {
            return FVector(X.value_or(0.0f), Y.value_or(0.0f), Z.value_or(0.0f));
        }
    );
    sol::table Math = Lua.create_named_table("Math");
    Math.set_function(
        "Vector",
        [](sol::optional<float> X, sol::optional<float> Y, sol::optional<float> Z)
        {
            return FVector(X.value_or(0.0f), Y.value_or(0.0f), Z.value_or(0.0f));
        }
    );
    Math.set_function(
        "Clamp",
        [](float Value, float Min, float Max)
        {
            return max(Min, min(Max, Value));
        }
    );
    Math.set_function(
        "Lerp",
        [](float A, float B, float Alpha)
        {
            return A + (B - A) * Alpha;
        }
    );
    Math.set_function(
        "Distance",
        [](const FVector& A, const FVector& B)
        {
            return (A - B).Length();
        }
    );
    Math.set_function(
        "Normalize",
        [](const FVector& V)
        {
            return V.Length() > 0.000001f ? V.Normalized() : FVector::ZeroVector;
        }
    );
    Math.set_function(
        "Dot",
        [](const FVector& A, const FVector& B)
        {
            return A.Dot(B);
        }
    );
    Math.set_function(
        "Cross",
        [](const FVector& A, const FVector& B)
        {
            return A.Cross(B);
        }
    );
}

void FLuaScriptManager::RegisterReflectionBindings(sol::state& Lua)
{
    Lua.new_usertype<UClass>(
        "UClass",
        "GetName",
        [](const UClass& Class)
        {
            return FString(Class.GetName() ? Class.GetName() : "");
        },
        "GetSuperClass",
        &UClass::GetSuperClass,
        "GetFlags",
        &UClass::GetClassFlags,
        "IsA",
        [](const UClass& Class, UClass* Other)
        {
            return Other && Class.IsA(Other);
        },
        "HasAnyClassFlags",
        &UClass::HasAnyClassFlags
    );

    sol::table Class = Lua.create_named_table("Class");
    Class.set_function(
        "Find",
        [](const FString& ClassName) -> UClass*
        {
            return UClass::FindByName(ClassName.c_str());
        }
    );
    Class.set_function(
        "Exists",
        [](const FString& ClassName)
        {
            return UClass::FindByName(ClassName.c_str()) != nullptr;
        }
    );
    Class.set_function(
        "All",
        [](sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            int32           Idx    = 1;
            for (UClass* C : UClass::GetAllClasses()) if (C) Result[Idx++] = C;
            return Result;
        }
    );
    Class.set_function(
        "GetFunctions",
        [](const FString& ClassName, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            UClass*         C      = UClass::FindByName(ClassName.c_str());
            if (!C) return Result;
            TArray<const FFunction*> Functions;
            C->GetFunctionRefs(Functions);
            int32 Idx = 1;
            for (const FFunction* F : Functions) if (F) Result[Idx++] = LuaDescribeFunction(State, *F);
            return Result;
        }
    );
    Class.set_function(
        "GetProperties",
        [](const FString& ClassName, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            UClass*         C      = UClass::FindByName(ClassName.c_str());
            if (!C) return Result;
            TArray<const FProperty*> Props;
            C->GetPropertyRefs(Props);
            int32 Idx = 1;
            for (const FProperty* P : Props) if (P) Result[Idx++] = LuaDescribeProperty(State, *P);
            return Result;
        }
    );

    Lua.new_usertype<UObject>(
        "Object",
        "GetName",
        [](UObject& Object)
        {
            return Object.GetName();
        },
        "GetClass",
        [](UObject& Object) -> UClass*
        {
            return IsValid(&Object) ? Object.GetClass() : nullptr;
        },
        "GetClassName",
        [](UObject& Object)
        {
            return IsValid(&Object) && Object.GetClass() ? FString(Object.GetClass()->GetName()) : FString();
        },
        "IsA",
        [](UObject& Object, const FString& ClassName)
        {
            UClass* C = UClass::FindByName(ClassName.c_str());
            return IsValid(&Object) && Object.GetClass() && C && Object.GetClass()->IsA(C);
        },
        "AsActor",
        [](UObject& Object) -> AActor*
        {
            return IsValid(&Object) ? Cast<AActor>(&Object) : nullptr;
        },
        "AsPawn",
        [](UObject& Object) -> APawn*
        {
            return IsValid(&Object) ? Cast<APawn>(&Object) : nullptr;
        },
        "AsPrimitiveComponent",
        [](UObject& Object) -> UPrimitiveComponent*
        {
            return IsValid(&Object) ? Cast<UPrimitiveComponent>(&Object) : nullptr;
        },
        "AsSceneComponent",
        [](UObject& Object) -> USceneComponent*
        {
            return IsValid(&Object) ? Cast<USceneComponent>(&Object) : nullptr;
        },
        "AsComponent",
        [](UObject& Object) -> UActorComponent*
        {
            return IsValid(&Object) ? Cast<UActorComponent>(&Object) : nullptr;
        },
        "GetUUID",
        &UObject::GetUUID,
        "IsValid",
        [](UObject* Object)
        {
            return IsValid(Object);
        },
        "CallFunction",
        [](UObject& Object, const FString& FunctionName, sol::variadic_args Args, sol::this_state State)
        {
            if (!IsValid(&Object))
            {
                return sol::make_object(sol::state_view(State), sol::nil);
            }
            return LuaInvokeReflectedFunction(State, &Object, nullptr, FunctionName, Args);
        },
        "CallFunctionSignature",
        [](UObject& Object, const FString& Signature, sol::variadic_args Args, sol::this_state State)
        {
            if (!IsValid(&Object))
            {
                return sol::make_object(sol::state_view(State), sol::nil);
            }
            return LuaInvokeReflectedFunctionBySignature(State, &Object, nullptr, Signature, Args);
        },
        "BindEvent",
        [](UObject& Object, const FString& FunctionNameOrSignature, sol::protected_function Callback)
        {
            return IsValid(&Object) && LuaBindReflectedEventOverride(&Object, FunctionNameOrSignature, std::move(Callback));
        },
        "UnbindEvent",
        [](UObject& Object, const FString& FunctionNameOrSignature)
        {
            return IsValid(&Object) && LuaUnbindReflectedEventOverride(&Object, FunctionNameOrSignature);
        },
        "HasEventBinding",
        [](UObject& Object, const FString& FunctionNameOrSignature)
        {
            return IsValid(&Object) && LuaHasReflectedEventOverride(&Object, FunctionNameOrSignature);
        },
        "GetProperty",
        [](UObject& Object, const FString& PropertyName, sol::this_state State)
        {
            return IsValid(&Object) ? LuaGetReflectedProperty(State, &Object, PropertyName) : sol::make_object(sol::state_view(State), sol::nil);
        },
        "SetProperty",
        [](UObject& Object, const FString& PropertyName, sol::object Value)
        {
            return IsValid(&Object) && LuaSetReflectedProperty(&Object, PropertyName, Value);
        },
        "GetFunctions",
        [](UObject& Object, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            if (!IsValid(&Object) || !Object.GetClass())
            {
                return Result;
            }
            TArray<const FFunction*> Functions;
            Object.GetClass()->GetFunctionRefs(Functions);
            int Index = 1;
            for (const FFunction* Function : Functions)
            {
                if (Function)
                {
                    Result[Index++] = LuaDescribeFunction(State, *Function);
                }
            }
            return Result;
        },
        "GetProperties",
        [](UObject& Object, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            if (!IsValid(&Object) || !Object.GetClass())
            {
                return Result;
            }
            TArray<const FProperty*> Properties;
            Object.GetClass()->GetPropertyRefs(Properties);
            int Index = 1;
            for (const FProperty* Property : Properties)
            {
                if (Property)
                {
                    Result[Index++] = LuaDescribeProperty(State, *Property);
                }
            }
            return Result;
        }
    );

    Lua.new_usertype<UActorComponent>(
        "ActorComponent",
        sol::base_classes,
        sol::bases<UObject>(),
        "GetOwner",
        &UActorComponent::GetOwner,
        "IsActive",
        &UActorComponent::IsActive,
        "SetActive",
        &UActorComponent::SetActive,
        "Activate",
        &UActorComponent::Activate,
        "Deactivate",
        &UActorComponent::Deactivate
    );


    Lua.new_usertype<ULuaBlueprintComponent>(
        "LuaBlueprintComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "ReloadBlueprint",
        &ULuaBlueprintComponent::ReloadBlueprint,
        "CallFunction",
        &ULuaBlueprintComponent::CallFunction,
        "CallCustomEvent",
        [](ULuaBlueprintComponent& Component, const FString& EventName, sol::variadic_args Args, sol::this_state State)
        {
            sol::state_view Lua(State);
            auto GetArg = [&](int Index) -> sol::object
            {
                return Index < static_cast<int>(Args.size())
                    ? Args.get<sol::object>(Index)
                    : sol::make_object(Lua, sol::nil);
            };
            return Component.CallCustomEvent(EventName, GetArg(0), GetArg(1), GetArg(2), GetArg(3));
        },
        "CallLuaBlueprintFileFunction",
        &ULuaBlueprintComponent::CallLuaBlueprintFileFunction,
        "CallLuaScriptFileFunction",
        &ULuaBlueprintComponent::CallLuaScriptFileFunction,
        "GetBlueprintPath",
        &ULuaBlueprintComponent::GetBlueprintPath,
        "SetBlueprintPath",
        &ULuaBlueprintComponent::SetBlueprintPath
    );

    Lua.new_usertype<ULuaScriptComponent>(
        "LuaScriptComponent",
        sol::base_classes,
        sol::bases<UActorComponent, UObject>(),
        "ReloadScript",
        &ULuaScriptComponent::ReloadScript,
        "CallFunction",
        &ULuaScriptComponent::CallFunction,
        "GetScriptFile",
        &ULuaScriptComponent::GetScriptFile,
        "SetScriptFile",
        &ULuaScriptComponent::SetScriptFile
    );

    sol::table Reflection = Lua.create_named_table("Reflection");
    Reflection.set_function(
        "Call",
        [](UObject* Object, const FString& FunctionName, sol::variadic_args Args, sol::this_state State)
        {
            return LuaInvokeReflectedFunction(State, Object, nullptr, FunctionName, Args);
        }
    );
    Reflection.set_function(
        "CallSignature",
        [](UObject* Object, const FString& Signature, sol::variadic_args Args, sol::this_state State)
        {
            return LuaInvokeReflectedFunctionBySignature(State, Object, nullptr, Signature, Args);
        }
    );
    Reflection.set_function(
        "CallStatic",
        [](const FString& ClassName, const FString& FunctionName, sol::variadic_args Args, sol::this_state State)
        {
            UClass* Class = UClass::FindByName(ClassName.c_str());
            return LuaInvokeReflectedFunction(State, nullptr, Class, FunctionName, Args);
        }
    );
    Reflection.set_function(
        "CallStaticSignature",
        [](const FString& ClassName, const FString& Signature, sol::variadic_args Args, sol::this_state State)
        {
            UClass* Class = UClass::FindByName(ClassName.c_str());
            return LuaInvokeReflectedFunctionBySignature(State, nullptr, Class, Signature, Args);
        }
    );
    Reflection.set_function(
        "BindEvent",
        [](UObject* Object, const FString& FunctionNameOrSignature, sol::protected_function Callback)
        {
            return LuaBindReflectedEventOverride(Object, FunctionNameOrSignature, std::move(Callback));
        }
    );
    Reflection.set_function(
        "UnbindEvent",
        [](UObject* Object, const FString& FunctionNameOrSignature)
        {
            return LuaUnbindReflectedEventOverride(Object, FunctionNameOrSignature);
        }
    );
    Reflection.set_function(
        "HasEventBinding",
        [](UObject* Object, const FString& FunctionNameOrSignature)
        {
            return LuaHasReflectedEventOverride(Object, FunctionNameOrSignature);
        }
    );
    Reflection.set_function(
        "GetProperty",
        [](UObject* Object, const FString& PropertyName, sol::this_state State)
        {
            return LuaGetReflectedProperty(State, Object, PropertyName);
        }
    );
    Reflection.set_function(
        "SetProperty",
        [](UObject* Object, const FString& PropertyName, sol::object Value)
        {
            return LuaSetReflectedProperty(Object, PropertyName, Value);
        }
    );
    Reflection.set_function(
        "GetFunctions",
        [](UObject* Object, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            if (!IsValid(Object) || !Object->GetClass())
            {
                return Result;
            }
            TArray<const FFunction*> Functions;
            Object->GetClass()->GetFunctionRefs(Functions);
            int Index = 1;
            for (const FFunction* Function : Functions)
            {
                if (Function)
                {
                    Result[Index++] = LuaDescribeFunction(State, *Function);
                }
            }
            return Result;
        }
    );
    // LuaBlueprint Cast 노드용 — IsA 통과 시 같은 객체 반환, 실패 시 nil.
    // 실제 Unreal Blueprint Cast 와 동일한 의미 (성공/실패 분기).
    Reflection.set_function(
        "Cast",
        [](UObject* Object, const FString& ClassName) -> UObject*
        {
            if (!IsValid(Object)) return nullptr;
            UClass* Target = UClass::FindByName(ClassName.c_str());
            if (!Target) return nullptr;
            UClass* Source = Object->GetClass();
            if (!Source) return nullptr;
            return Source->IsA(Target) ? Object : nullptr;
        }
    );
    Reflection.set_function(
        "GetStaticFunctions",
        [](const FString& ClassName, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            UClass*         Class  = UClass::FindByName(ClassName.c_str());
            if (!Class)
            {
                return Result;
            }
            TArray<const FFunction*> Functions;
            Class->GetFunctionRefs(Functions);
            int Index = 1;
            for (const FFunction* Function : Functions)
            {
                if (Function && Function->IsStatic())
                {
                    Result[Index++] = LuaDescribeFunction(State, *Function);
                }
            }
            return Result;
        }
    );
    Reflection.set_function(
        "GetProperties",
        [](UObject* Object, sol::this_state State)
        {
            sol::state_view L(State);
            sol::table      Result = L.create_table();
            if (!IsValid(Object) || !Object->GetClass())
            {
                return Result;
            }
            TArray<const FProperty*> Properties;
            Object->GetClass()->GetPropertyRefs(Properties);
            int Index = 1;
            for (const FProperty* Property : Properties)
            {
                if (Property)
                {
                    Result[Index++] = LuaDescribeProperty(State, *Property);
                }
            }
            return Result;
        }
    );
}

void FLuaScriptManager::RegisterActorBindings(sol::state& Lua)
{
    RegisterActorBindings_1(Lua);
    RegisterActorBindings_2(Lua);
    RegisterActorBindings_3(Lua);
    RegisterActorBindings_4(Lua);
    RegisterActorBindings_5(Lua);
    RegisterActorBindings_6(Lua);

    // 게임 특화 usertype/enum/global(GetGameState 등) 은 Game 모듈의
    // RegisterGameLuaBindings 가 등록한다. 호출 순서는 GameEngine/EditorEngine::Init
    // 에서 UEngine::Init() 직후.
}

void FLuaScriptManager::RegisterUIBindings(sol::state& Lua)
{
    Lua.new_usertype<UUserWidget>(
        "UserWidget",
        sol::base_classes,
        sol::bases<UObject>(),
        "AddToViewport",
        [](UUserWidget& Widget)
        {
            Widget.AddToViewport();
        },
        "AddToViewportZ",
        [](UUserWidget& Widget, int32 ZOrder)
        {
            Widget.AddToViewport(ZOrder);
        },
        "RemoveFromParent",
        &UUserWidget::RemoveFromParent,
        "Show",
        [](UUserWidget& Widget)
        {
            Widget.AddToViewport();
        },
        "Hide",
        &UUserWidget::RemoveFromParent,
        "show",
        [](UUserWidget& Widget)
        {
            Widget.AddToViewport();
        },
        "hide",
        &UUserWidget::RemoveFromParent,
        "IsInViewport",
        &UUserWidget::IsInViewport,
        "bind_click",
        [](UUserWidget& Widget, const FString& ElementId, sol::protected_function Callback)
        {
            Widget.BindClick(ElementId, Callback);
        },
        "SetText",
        &UUserWidget::SetText,
        "set_text",
        &UUserWidget::SetText,
        "SetProperty",
        &UUserWidget::SetProperty,
        "set_property",
        &UUserWidget::SetProperty,
        "SetWantsMouse",
        &UUserWidget::SetWantsMouse,
        "WantsMouse",
        &UUserWidget::WantsMouse
    );

    sol::table UI = Lua.create_named_table("UI");
    UI.set_function(
        "CreateWidget",
        [](const FString& DocumentPath)
        {
            return UUIManager::Get().CreateWidget(nullptr, DocumentPath);
        }
    );

    // [버튼 액션] (b) SimpleUI 버튼 클릭 콜백 바인딩 — UI.BindButton("ElementName", function() ... end).
    // 클릭 디스패처(FUICanvasManager::TickRuntimeInput)가 InvokeUIButtonCallback 으로 호출한다.
    UI.set_function(
        "BindButton",
        [](const FString& ElementName, sol::protected_function Callback)
        {
            FLuaScriptManager::BindUIButton(ElementName, std::move(Callback));
        }
    );

    // [가시성 토글] 등록된 모든 Canvas 트리에서 ElementName 을 FindByName 후 SetVisible.
    // 첫 매치에 적용하고 찾으면 true(없으면 false). Lua: UI.SetElementVisible("DeathAim", true)
    UI.set_function(
        "SetElementVisible",
        [](const FString& ElementName, bool bVisible) -> bool
        {
            for (UUICanvas* Canvas : FUICanvasManager::Get().GetCanvases())
            {
                if (!Canvas)
                {
                    continue;
                }
                if (UUIElement* Element = Canvas->FindByName(ElementName))
                {
                    Element->SetVisible(bVisible);
                    return true;
                }
            }
            return false;
        }
    );

    // 씬 전환을 Lua 에 노출(기존 미노출) — (b) 콜백/스크립트에서 Engine.LoadScene("Map") 형태로 호출.
    // (a) ChangeScene 액션은 C++(ExecuteButtonAction)가 직접 RequestTransitionToScene 한다.
    sol::optional<sol::table> EngineTable = Lua["Engine"];
    if (EngineTable)
    {
        EngineTable->set_function(
            "LoadScene",
            [](const FString& ScenePath)
            {
                if (GEngine)
                {
                    GEngine->RequestTransitionToScene(ScenePath);
                }
            }
        );
    }
}
