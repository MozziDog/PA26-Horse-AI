#pragma once

#include "Core/Types/CoreTypes.h"

#include <memory>

class FBehaviorTree;

// Lua 스크립트로 BehaviorTree 를 조립한다.
// 스크립트는 BT.sequence / BT.selector / BT.task / BT.condition / BT.force_success / BT.invert 로
// '구조 서술 테이블'을 만들어 루트를 return 한다. 리프의 실제 로직은 FBTBehaviorRegistry 에서 이름으로 조회.
// (Lua 클로저를 노드에 저장하지 않음 → dangling 회피)
class FBTLuaBuilder
{
public:
	// 스크립트 파일(Content/Script 기준 상대경로)을 실행해 트리를 만든다. 실패 시 nullptr.
	static std::unique_ptr<FBehaviorTree> BuildTree(const FString& ScriptFile);
};
