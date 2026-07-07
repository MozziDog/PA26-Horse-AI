-- Lua로 작성된 테스트용 BT 스크립트
-- leaf node의 각 Task들은 C++ 에서 구현, 여기서는 이름으로만 참조(FBTBehaviorRegistry)
-- (hot-reload/씬 전환 시 Lua 클로저에서 댕글링 포인터 발생 우려)
--
-- 동작(12초 주기):
--   0~5s  : Hungry     → Graze (Chew, Running)
--   5~8s  : (없음)     → Idle
--   8~11s : ThreatNear → Flee  (Run, Running)  ← 우선순위가 높아 Graze 선점
--   11~12s: (없음)     → Idle

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
        -- [Lua 인라인 task] C++ "Run" 과 동일 동작. 여기서 바로 로직을 수정하며 hot-reload 로 반복 가능.
        task(function(ctx)
            local ownerActor = ctx.owner
            if ownerActor then
                ownerActor:AddWorldOffset(ownerActor.Forward * (3.0 * ctx.dt))
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
