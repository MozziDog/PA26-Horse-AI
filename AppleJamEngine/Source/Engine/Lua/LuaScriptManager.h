#pragma once

#include "Core/Types/CoreTypes.h"
#include "Engine/Platform/DirectoryWatcher.h"
#include "Input/InputSystem.h"
#include "Object/Ptr/WeakObjectPtr.h"
#include <sol/sol.hpp>
#include <mutex>

class ULuaScriptComponent;
class ULuaAnimInstance;
class UBTAgentComponent;

class FLuaScriptManager
{
public:
	static void Initialize();
	static void Shutdown();

	static FString ResolveScriptPath(const FString& ScriptFile);
	static bool OpenOrCreateScript(const FString& ScriptFile);

	// 한글 경로 호환 — wide ifstream 으로 스크립트 파일 내용을 읽어 반환. fopen(UTF-8) 은
	// Windows 에서 ANSI 코드페이지로 해석돼 한글 경로에서 실패하므로 항상 wide 로 우회.
	static bool ReadScriptFileContent(const FString& ScriptFile, FString& OutContent);

	static sol::state& GetState();
	static bool IsInitialized();
	static void RegisterBindings(sol::state& Lua);

	static FInputSystemSnapshot GetLuaInputSnapshot();

	// World pause 와 무관하게 매 frame 발화되는 ESC 콜백. UIManager 가 등록하면 메뉴 토글이
	// pause 도중에도 동작한다 (component-tick 은 World pause 시 멈추므로 거기엔 못 둠).
	static void SetOnEscapePressed(sol::protected_function Callback);
	static void FireOnEscapePressed();
	static void SetOnUnpausedTick(sol::protected_function Callback);
	static void FireUnpausedTick(float DeltaTime);

	// [UI 버튼 액션] (b) Lua 콜백 — ElementName 키로 SimpleUI 버튼 클릭 콜백을 등록/호출한다
	// (UI.BindButton 로 바인딩, FUICanvasManager 의 클릭 디스패처가 InvokeUIButtonCallback 호출).
	static void BindUIButton(const FString& ElementName, sol::protected_function Callback);
	static void InvokeUIButtonCallback(const FString& ElementName);
	// CallLua 액션 — 지정한 .lua 스크립트 파일(Content/Script)을 읽어 클릭 시 1회 실행한다.
	// 경로는 ReadScriptFileContent 가 해석(한글 경로 wide 처리). 파일 없거나 실패 시 무시.
	static void RunScriptFile(const FString& ScriptFile);

	// 씬 전환 시 호출. require 캐시된 모듈 (ObjRegistry / CoroutineManager) 이 보유한 stale
	// actor 포인터와 dangling 코루틴을 비운다. 안 하면 새 월드의 첫 Tick 에서 옛 코루틴이
	// Wait(30) 만료 후 재개되며 freed AActor* 를 deref → 크래시.
	static void FireWorldReset();

	static void RegisterComponent(ULuaScriptComponent* Component);
	static void UnregisterComponent(ULuaScriptComponent* Component);

	// Lua 로 구동되는 AnimInstance — .lua 변경 시 ReloadScript 받음.
	static void RegisterAnimInstance(ULuaAnimInstance* Instance);
	static void UnregisterAnimInstance(ULuaAnimInstance* Instance);

	// Lua 로 트리를 구성하는 BTAgent — .lua 변경 시 RebuildBehaviorTree, 셧다운 시 ReleaseLuaForShutdown 받음.
	static void RegisterBTAgent(UBTAgentComponent* Agent);
	static void UnregisterBTAgent(UBTAgentComponent* Agent);

private:
	static void RegisterLuaHelpers(sol::state& Lua);
	static void RegisterCoreBindings(sol::state& Lua);
	static void RegisterMathBindings(sol::state& Lua);
	static void RegisterReflectionBindings(sol::state& Lua);
	static void RegisterActorBindings(sol::state& Lua);
	// RegisterActorBindings 본문을 순서 보존 분할한 sub-function들(LuaActorBindings_*.cpp).
	// 동일 sol::state 위에 원본과 같은 순서로 new_usertype를 호출한다. 정의는 각 1개 TU(ODR).
	static void RegisterActorBindings_1(sol::state& Lua);
	static void RegisterActorBindings_2(sol::state& Lua);
	static void RegisterActorBindings_3(sol::state& Lua);
	static void RegisterActorBindings_4(sol::state& Lua);
	static void RegisterActorBindings_5(sol::state& Lua);
	static void RegisterActorBindings_6(sol::state& Lua);
	static void RegisterUIBindings(sol::state& Lua);

	static void OnScriptsChanged(const TSet<FString>& ChangedFiles);
	static void InvalidateChangedModules(const TSet<FString>& ChangedFiles);
	static FString GetModuleNameFromPath(const FString& ScriptPath);

private:
	static std::unique_ptr<sol::state> Lua;
	static sol::protected_function OnEscapePressedCallback;
	static sol::protected_function OnUnpausedTickCallback;
	// [UI 버튼 액션] ElementName → 클릭 콜백(b). 씬 전환/캔버스 해제 시 정리는 FireWorldReset 에서.
	static TMap<FString, sol::protected_function> UIButtonCallbacks;
	static std::mutex ComponentMutex;
	static TArray<TWeakObjectPtr<ULuaScriptComponent>> RegisteredComponents;
	static TArray<TWeakObjectPtr<ULuaAnimInstance>>    RegisteredAnimInstances;
	static TArray<TWeakObjectPtr<UBTAgentComponent>>   RegisteredBTAgents;
	static FSubscriptionID WatchSub;
};
