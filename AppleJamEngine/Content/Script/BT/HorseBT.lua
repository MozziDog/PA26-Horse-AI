-- 말 행동 트리 — '기수 탑승' 기준
-- (Flee 등 적 대응 이산 행동은 적 추가 시 이 selector에 branch로 후속.)

-- NOTE: 도로 추종, 장애물 회피 등의 로직은 BT가 아닌 HorseLocomotion에서 담당

local task     = BT.task
local selector = BT.selector

return selector {
    label = "Root",

    -- Travel: 주행 모드 유지. gait 미변경(req④ "현재 속도 유지") → 속도는 기수 gear가 소유.
    -- 조향/장애물회피/도로추종은 Locomotion arbiter가 담당하므로 여기서는 Running만 반환한다.
    task(function(_) return BT.Running end, "Travel"),
}
