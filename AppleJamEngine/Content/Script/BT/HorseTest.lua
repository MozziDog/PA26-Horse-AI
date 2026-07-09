-- Lua로 작성된 테스트용 BT 스크립트
-- leaf node의 각 Task들은 C++ 에서 구현, 여기서는 이름으로만 참조(FBTBehaviorRegistry)
-- (hot-reload/씬 전환 시 Lua 클로저에서 댕글링 포인터 발생 우려)
--
-- 이동에 관해서: 
-- task는 Movement 를 직접 만지지 않고 Blackboard 의 "DesiredGait"(0=Stop..4=Gallop)만 쓴다.
-- HorseLocomotion이 이를 읽어 쿨타임·envelope를 고려해 실제 gait에 반영하는 간접 컨트롤 방식.
--
-- 동작(12초 주기):
--   0~5s  : Hungry     → Graze (Chew  → DesiredGait=Stop)
--   5~8s  : (없음)     → Idle         (DesiredGait=Stop)
--   8~11s : ThreatNear → Flee  (Run   → DesiredGait=Gallop)  ← 우선순위가 높아 Graze 선점
--   11~12s: (없음)     → Idle         (DesiredGait=Stop)

local task          = BT.task
local condition     = BT.condition
local sequence      = BT.sequence
local selector      = BT.selector
local force_success = BT.force_success

return selector {
    label = "Root",

    -- Flee (최우선): 위협이 가까우면 forward 로 전진
    sequence {
        label = "FleeSeq",
        condition "ThreatNear",
        -- [Lua 인라인 task] C++ "Run" 과 동일 동작(Blackboard 에 DesiredGait 만 기록).
        -- 여기서 바로 로직을 수정하며 hot-reload 로 반복 가능.
        task(function(ctx)
            if ctx.blackboard then
                --ctx.blackboard:set_int("DesiredGait", 4)  -- 4 = Gallop: 위협 회피는 전력 질주
                ctx.blackboard:set_int("DesiredGait", 2) -- 2 = Trot
            end
            return BT.Running
        end, "Run"),
    },

    -- Graze: 배고프면 Chew(항상 Running). ForceSuccess 로 감싸 Selector 흐름 유지
    sequence {
        label = "GrazeSeq",
        condition "Hungry",
        force_success(task "Chew", "MaybeChew"),
    },

    -- 기본 행동
    task "Idle",
}
