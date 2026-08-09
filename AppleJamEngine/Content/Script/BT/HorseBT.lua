-- 말 행동 트리: reactive root가 탑승을 호출보다 우선한다.
-- 실제 방향 계산과 local steering은 각각 guidance producer와 locomotion이 담당한다.

local task            = BT.task
local condition       = BT.condition
local sequence        = BT.sequence
local stateful_sequence = BT.stateful_sequence
local selector        = BT.selector

return selector {
    label = "Root",

    sequence {
        label = "Mounted",
        condition("Mounted"),
        task("RiderControlGuidance"),
    },

    sequence {
        label = "CallRequested",
        condition("CallRequested"),
        stateful_sequence {
            label = "CallPlayerSequence",
            task("PlanCallPath"),
            task("AlignToPathStart"),
            task("FollowCallPath"),
        },
    },

    task("Idle"),
}
