#include "pch.h"
#include "BTLuaBuilder.h"

#include "AI/BT/BehaviorTree.h"
#include "AI/BT/BTBehaviorRegistry.h"
#include "AI/Blackboard.h"
#include "GameFramework/AActor.h"
#include "Lua/LuaScriptManager.h"
#include "Core/Logging/Log.h"
#include "Object/GarbageCollection.h"

#include <sol/sol.hpp>
#include <string>

namespace
{
	// BT.* 빌더 프리앰블. task 는 이름(C++ 등록 behavior 참조) 또는 함수(Lua 인라인 task) 를 받는다.
	const char* kPrelude = R"LUA(
BT = BT or {}
BT.Running = EBTResult.Running
BT.Success = EBTResult.Success
BT.Fail    = EBTResult.Fail
function BT.task(name_or_fn, label)
    if type(name_or_fn) == 'function' then return { kind = 'task', fn = name_or_fn, label = label } end
    return { kind = 'task', name = name_or_fn }
end
function BT.condition(name) return { kind = 'condition', name = name } end
function BT.sequence(t)     t.kind = 'sequence'; return t end
function BT.selector(t)     t.kind = 'selector'; return t end
function BT.force_success(child, label) return { kind = 'force_success', child = child, label = label } end
function BT.invert(child, label)        return { kind = 'invert', child = child, label = label } end
)LUA";

	// FBTContext/FBlackboard/EBTResult 를 Lua 에 노출 + BT.* 프리앰블 주입. 1회만(가드).
	void EnsureBindings()
	{
		sol::state& Lua = FLuaScriptManager::GetState();
		if (Lua["BT"].valid())
		{
			return;   // 이미 주입됨
		}

		Lua.new_enum("EBTResult",
			"Running", EBTResult::Running,
			"Success", EBTResult::Success,
			"Fail",    EBTResult::Fail);

		// 블랙보드 접근: 키는 문자열, 타입별 getter/setter. 없는 키의 getter 는 기본값.
		Lua.new_usertype<FBlackboard>("FBlackboard",
			"get_bool",  [](FBlackboard& BB, const std::string& K) { bool V = false;  BB.TryGetBool(FName(K.c_str()), V);  return V; },
			"set_bool",  [](FBlackboard& BB, const std::string& K, bool V) { BB.SetBool(FName(K.c_str()), V); },
			"get_int",   [](FBlackboard& BB, const std::string& K) { int V = 0;        BB.TryGetInt(FName(K.c_str()), V);   return V; },
			"set_int",   [](FBlackboard& BB, const std::string& K, int V) { BB.SetInt(FName(K.c_str()), V); },
			"get_float", [](FBlackboard& BB, const std::string& K) { float V = 0.0f;   BB.TryGetFloat(FName(K.c_str()), V); return V; },
			"set_float", [](FBlackboard& BB, const std::string& K, float V) { BB.SetFloat(FName(K.c_str()), V); });

		// 매 틱 주입되는 컨텍스트. owner 는 AActor(별도 바인딩) — 이동/회전 등 그대로 사용. blackboard 는 없으면 nil.
		Lua.new_usertype<FBTContext>("FBTContext",
			"owner",      &FBTContext::Owner,
			"dt",         &FBTContext::DeltaTime,
			"frame",      &FBTContext::FrameNumber,
			"blackboard", &FBTContext::Blackboard);

		sol::protected_function_result Result = Lua.safe_script(kPrelude, sol::script_pass_on_error);
		if (!Result.valid())
		{
			sol::error Err = Result;
			UE_LOG("[BT] 빌더 바인딩/프리앰블 주입 실패: %s", Err.what());
		}
	}

	// Lua 인라인 task 노드. sol 함수를 직접 보유 → 트리와 수명을 함께한다.
	// (BTAgent 가 hot-reload 시 재빌드, 셧다운 시 트리 drop 으로 sol ref 를 Lua state 살아있을 때 해제)
	class FLuaTask : public FBehaviorNode
	{
	public:
		FLuaTask(FName InName, sol::protected_function InFunc) : Func(std::move(InFunc)) { DebugLabel = InName; }
		const char* GetNodeTypeName() const override { return "LuaTask"; }
	protected:
		EBTResult OnBehave(FBTContext& Context) override
		{
			if (!Func.valid())
			{
				return EBTResult::Fail;
			}

			// Lua 호출 중 GC 가 owner 등을 수거하지 못하도록 블록(AnimInstance 의 FLuaCallScope 와 동일 취지).
			FGarbageCollector::Get().PushCollectionBlock();
			sol::protected_function_result R = Func(&Context);
			FGarbageCollector::Get().PopCollectionBlock();

			if (!R.valid())
			{
				sol::error Err = R;
				UE_LOG("[BT] Lua task '%s' 오류: %s", GetDebugLabel().ToString().c_str(), Err.what());
				return EBTResult::Fail;
			}

			sol::object Ret = R;
			if (Ret.get_type() == sol::type::number)
			{
				return static_cast<EBTResult>(Ret.as<int>());
			}
			return EBTResult::Running;   // 반환 안 하면 Running 으로 간주
		}
	private:
		sol::protected_function Func;
	};

	// 서술 테이블 1개 → 노드 1개(재귀). 알 수 없는 종류/누락 behavior 는 로그 후 fallback 노드.
	std::unique_ptr<FBehaviorNode> BuildNode(const sol::table& Desc)
	{
		const std::string Kind = Desc.get_or("kind", std::string());

		auto ApplyLabel = [&](FBehaviorNode& Node)
		{
			const std::string Label = Desc.get_or("label", std::string());
			if (!Label.empty())
			{
				Node.SetDebugLabel(FName(Label.c_str()));
			}
		};

		// --- Leaf: Task ---
		if (Kind == "task")
		{
			// 인라인 Lua task (함수) — 빠른 반복/프로토타이핑용.
			sol::object FnObj = Desc["fn"];
			if (FnObj.get_type() == sol::type::function)
			{
				const std::string Label = Desc.get_or("label", std::string("Lua"));
				return std::make_unique<FLuaTask>(FName(Label.c_str()), FnObj.as<sol::protected_function>());
			}

			// 이름 참조 — C++ 레지스트리의 안정화된 behavior.
			const std::string Name = Desc.get_or("name", std::string());
			const FName NameKey(Name.c_str());
			if (const FBTBehaviorRegistry::FTaskFn* Fn = FBTBehaviorRegistry::FindTask(NameKey))
			{
				return std::make_unique<FBehaviorTask>(NameKey, *Fn);
			}
			UE_LOG("[BT] 등록되지 않은 task: '%s' — Fail 노드로 대체", Name.c_str());
			return std::make_unique<FBehaviorTask>(NameKey, [](FBTContext&) { return EBTResult::Fail; });
		}

		// --- Leaf: Condition ---
		if (Kind == "condition")
		{
			const std::string Name = Desc.get_or("name", std::string());
			const FName NameKey(Name.c_str());
			if (const FBTBehaviorRegistry::FConditionFn* Fn = FBTBehaviorRegistry::FindCondition(NameKey))
			{
				return std::make_unique<FConditional>(NameKey, *Fn);
			}
			UE_LOG("[BT] 등록되지 않은 condition: '%s' — false 노드로 대체", Name.c_str());
			return std::make_unique<FConditional>(NameKey, [](FBTContext&) { return false; });
		}

		// --- Composite ---
		if (Kind == "sequence" || Kind == "selector")
		{
			TArray<std::unique_ptr<FBehaviorNode>> Children;
			for (std::size_t Index = 1; ; ++Index)   // 배열 파트(1-based)만 자식으로, label 등 해시 키는 제외
			{
				sol::object ChildObj = Desc[Index];
				if (!ChildObj.valid())
				{
					break;
				}
				if (ChildObj.is<sol::table>())
				{
					if (std::unique_ptr<FBehaviorNode> Child = BuildNode(ChildObj.as<sol::table>()))
					{
						Children.push_back(std::move(Child));
					}
				}
			}

			std::unique_ptr<FBehaviorNode> Node;
			if (Kind == "sequence")
			{
				Node = std::make_unique<FSequence>(std::move(Children));
			}
			else
			{
				Node = std::make_unique<FSelector>(std::move(Children));
			}
			ApplyLabel(*Node);
			return Node;
		}

		// --- Decorator ---
		if (Kind == "force_success" || Kind == "invert")
		{
			sol::object ChildObj = Desc["child"];
			std::unique_ptr<FBehaviorNode> Child;
			if (ChildObj.is<sol::table>())
			{
				Child = BuildNode(ChildObj.as<sol::table>());
			}
			if (!Child)
			{
				UE_LOG("[BT] 데코레이터 '%s' 에 유효한 child 가 없음", Kind.c_str());
				return nullptr;
			}

			std::unique_ptr<FBehaviorNode> Node;
			if (Kind == "force_success")
			{
				Node = std::make_unique<FForceSuccess>(std::move(Child));
			}
			else
			{
				Node = std::make_unique<FInvert>(std::move(Child));
			}
			ApplyLabel(*Node);
			return Node;
		}

		UE_LOG("[BT] 알 수 없는 노드 kind: '%s'", Kind.c_str());
		return nullptr;
	}
}

std::unique_ptr<FBehaviorTree> FBTLuaBuilder::BuildTree(const FString& ScriptFile)
{
	if (!FLuaScriptManager::IsInitialized())
	{
		UE_LOG("[BT] Lua 미초기화 — 트리 빌드 불가: %s", ScriptFile.c_str());
		return nullptr;
	}

	FString Content;
	if (!FLuaScriptManager::ReadScriptFileContent(ScriptFile, Content))
	{
		UE_LOG("[BT] 스크립트 읽기 실패: %s", ScriptFile.c_str());
		return nullptr;
	}

	EnsureBindings();

	sol::state&                    Lua       = FLuaScriptManager::GetState();
	const FString                  ChunkName = FLuaScriptManager::ResolveScriptPath(ScriptFile);
	sol::protected_function_result Result    = Lua.safe_script(Content, sol::script_pass_on_error, ChunkName);
	if (!Result.valid())
	{
		sol::error Err = Result;
		UE_LOG("[BT] 스크립트 실행 오류 '%s': %s", ScriptFile.c_str(), Err.what());
		return nullptr;
	}

	sol::object RootObj = Result;
	if (!RootObj.is<sol::table>())
	{
		UE_LOG("[BT] 스크립트가 루트 노드 테이블을 return 하지 않음: %s", ScriptFile.c_str());
		return nullptr;
	}

	std::unique_ptr<FBehaviorNode> Root = BuildNode(RootObj.as<sol::table>());
	if (!Root)
	{
		UE_LOG("[BT] 루트 노드 빌드 실패: %s", ScriptFile.c_str());
		return nullptr;
	}
	return std::make_unique<FBehaviorTree>(std::move(Root));
}
