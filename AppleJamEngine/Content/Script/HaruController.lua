local AbilitySystem = require("AbilitySystem")
local HaruUltimateCutscene = require("HaruUltimateCutscene")

-- Dash
local DASH_SKILL_NAME = "Dash"
local DASH_SKILL_KEY = "LeftShift"
local DASH_SKILL_KEYS = { DASH_SKILL_KEY, "GamepadLeftTrigger" }
local DASH_DISTANCE = 5.0
local DASH_DURATION = 0.5
local DASH_AFTERIMAGE_INTENSITY = 1.0
local DASH_AFTERIMAGE_RADIUS = 128.0
local DASH_AFTERIMAGE_SAMPLES = 64
-- Roll
local ROLL_SKILL_NAME = "Roll"
local ROLL_SKILL_KEY = "LeftControl"
local ROLL_SKILL_KEYS = { ROLL_SKILL_KEY, "GamepadFaceButtonRight" }
local ROLL_DISTANCE = 12.0
local ROLL_DURATION = 1.2
-- Arrow
local BOW_SKILL_NAME = "AirBowShot"
local BOW_SKILL_KEY = "RightMouseButton"
local BOW_SKILL_KEYS = { BOW_SKILL_KEY, "GamepadRightShoulder" }
local BOW_AIM_GRAVITY = 0.1
local BOW_AIM_MAX_DURATION = 60.0

local BOW_CAMERA_ARM_LENGTH = 3.0
local BOW_CAMERA_SOCKET_OFFSET = Vec3(0.0, 0.5, 1.0)
local BOW_CAMERA_FOV = 0.55
local BOW_CAMERA_BLEND_DURATION = 0.3
local BOW_CAMERA_RESTORE_DURATION = 0.22

local BOW_RADIAL_BLUR_INTENSITY = 2.0
local BOW_RADIAL_BLUR_RADIUS = 0.12
local BOW_RADIAL_BLUR_SAMPLE_COUNT = 22

local BOW_PROJECTILE_SPEED = 45.0
local BOW_PROJECTILE_OFFSET = Vec3(0.1, 0.15, 0.3)

local BOW_AIM_PARTICLE_PATH = "Content/Particle System/Aim.uasset"
local BOW_AIM_PARTICLE_OFFSET = Vec3(1.5, 0.15, 0.3)

local BOW_RELEASE_SLOMO_DURATION = 0.3
local BOW_RELEASE_SLOMO_DILATION = 0.1

local BOW_ULTIMATE_IGNORE_GAUGE_FOR_TEST = false
local STAFF_STATIC_MESH_PATH = "Content/Data/Staff/Staff_StaticMesh.uasset"
local BOW_STATIC_MESH_PATH = "Content/Data/Bow/Bow_StaticMesh.uasset"
-- Attack
local ATTACK_SKILL_NAME = "SprayAttack"
local FIRE_PROJECTILE_KEY = "LeftMouseButton"
local FIRE_PROJECTILE_KEYS = { FIRE_PROJECTILE_KEY, "GamepadRightTrigger" }
local ATTACK_MAX_DURATION = 60.0
local SPRAY_TARGET_ACTOR_NAME = "Boss"
local SPRAY_FACE_BLEND_DURATION = 0.25
local DEMO_KILL_PLAYER_KEY = "F1"
local DEMO_KILL_BOSS_KEY = "F2"
-- Ultimate
-- Q 입력 → ultimate_spell(시전 애니) 즉시 재생 → 종료 시 false. (ready 단계 제거됨)
-- AnimGraph(AG_Haru) 측에 UltSpell 상태 + ultimate_spell bool 전이(AnyState/Idle→UltSpell)가 있어야 동작한다.
local ULTIMATE_SKILL_NAME = "Ultimate"
local ULTIMATE_SKILL_KEY = "Q"
local ULTIMATE_SKILL_KEYS = { ULTIMATE_SKILL_KEY }
local ULTIMATE_SPELL_ANIM_VAR = "ultimate_spell"
local ULTIMATE_BEAM_SCALE = 5.0       -- 빔 크기 배율(런타임 사용자 설정에 맞춰 조절). Q 발동 시 BeamAttackComponent 에 적용.
-- 전역 빔 에셋 크기(공유 Beam.uasset template 직접 수정 → 모든 빔에 반영). 아래 값을 키우면 빔 자체가 굵고 길어짐.
local BEAM_TEMPLATE_PATH = "Content/Particle System/Beam.uasset"
local ULTIMATE_BEAM_WIDTH = 0.25      -- 전역 빔 굵기(WidthDistribution). 기본 0.25.
local ULTIMATE_BEAM_DISTANCE = 8.6    -- 전역 빔 길이(DistanceDistribution). 기본 8.6.
local ULTIMATE_BEAM_GROW_SPEED = 0.0  -- 빔 끝점 연장 속도(TypeDataBeam.Speed, 로컬 단위/초). 0=즉시 full(연장 없음).
                                      -- 월드 연장속도 = 이 값 × ULTIMATE_BEAM_SCALE. 카메라(ULTIMATE_BEAM_SPEED)와 맞추려면 ≈ SPEED/SCALE. ← 여기 튜닝.
local ULTIMATE_SPELL_DURATION = 1.5   -- spell 클립 길이(초) — AG_Haru UltSpell 클립에 맞춰 튜닝
local ULTIMATE_BEAM_LEAD_TIME = 0.5   -- spell 애니 종료 이 시간(초) 전에 빔(particle) 시전
local ULTIMATE_BEAM_SPEED = 8.0       -- 카메라 dolly 속도(월드 단위/초). 빔 끝점 연장은 ULTIMATE_BEAM_GROW_SPEED 가 담당.
local BEAM_CAM_FOLLOW_RATIO = 0.7     -- 카메라가 빔 거리의 이 비율(4/5)까지 따라감.
local BEAM_CAM_RETURN_DURATION = 0.4  -- 추적 종료 후 원래 카메라로 복귀하는 블렌드 시간(초).
local BEAM_CAM_DURATION_MARGIN = 0.3  -- 카메라 연출 종료 후 빔을 더 유지할 여유(초). 테스트하며 튜닝.
local BEAM_CAM_RIGHT_OFFSET = 0.3     -- 카메라를 빔 축에서 우측(+Y)으로 띄우는 거리(월드) → 빔과 나란히 진행.
                                      -- 0=빔 축 정면(이전 동작). 음수=좌측. 좌우가 반대로 보이면 부호만 바꾸면 됨. ← 여기 튜닝.
local BEAM_CAM_START_DELAY = 0.5      -- 빔 발사 후 카메라 dolly 시작을 늦추는 시간(초). 그동안 빔이 먼저 뻗어나감.
                                      -- 0=즉시 시작(이전 동작). 빔 수명도 이만큼 자동 연장됨. ← 여기 튜닝.
local BEAM_CAM_KILL_LINGER = 0.6      -- 마무리 킬 hit 후 카메라를 더 dolly 시킨 뒤 복귀하기까지의 시간(초).
                                      -- 0=킬 즉시 복귀(이전 동작). 키우면 킬 후 카메라가 더 멀리 진행한 뒤 복귀. ← 여기 튜닝.
-- Q 궁극기 카메라(빔 따라가기 대체) — 기존 카메라 위치(뒤)에서 반바퀴 돌아 캐릭터 정면(얼굴) → 보스에 시점 고정.
local ULT_CAM_ORBIT_DURATION = 1.5   -- Phase0: 캐릭터 주위를 도는 시간(게임시간 초). 0=오빗 생략(이전 동작). ← 여기 튜닝.
local ULT_CAM_ORBIT_SWEEP = 180.0    -- 오빗 회전량(도). 180=반바퀴. 시작=정면−SWEEP(=뒤, 기존 카메라 위치), 종료=정면. 부호로 좌/우 방향.
local ULT_CAM_ORBIT_SLOMO = 0.3      -- Phase0(오빗) 동안 전역 시간 배율(슬로우 모션). 1.0=끔(정상속도), 0.3=30% 속도. 모션·빔·궁 타이머·카메라가 함께 느려져 동기화 유지. 실제 오빗 길이(초)=ORBIT_DURATION/SLOMO. ← 여기 튜닝.
local ULT_CAM_SIDE_DURATION = 1.0    -- Phase1: 오빗 후 그 자리(정면)에서 잠깐 머무는 시간(초).
local ULT_CAM_SIDE_YAW = 180.0       -- 오빗 종료(anchor) 위치 = 캐릭터 시선 기준 각도(도). 180=정면(얼굴), 90/-90=옆, 0=뒤. 부호로 좌/우 전환.
local ULT_CAM_SIDE_PITCH = 0.0       -- 옆 얼굴 카메라 pitch(도).
local ULT_CAM_SIDE_DIST = 4.8        -- 옆 얼굴 카메라 거리(arm length).
local ULT_CAM_SIDE_HEIGHT = 1.5      -- 머리 높이 맞춤(socket +Z).
local ULT_CAM_BLEND_DURATION = 0.6   -- 옆 얼굴 → 보스 시점 전환 blend 시간(초). 0=즉시 스냅. ← 여기 튜닝.
local ULT_CAM_BOSS_HOLD = 3.0        -- Phase2: 보스 시점 고정 유지(초, 킬 안 났을 때 폴백 종료).
local ULT_CAM_BOSS_DIST = 0.0        -- 보스 고정 시 arm length(0 = 뒤로 안 빠지고 옆 위치 유지).
local ULT_CAM_BOSS_SIDE = 3.0        -- 보스 고정 시 측면 offset(socket +Y) — 옆 얼굴 보던 위치 유지. 부호로 좌/우 맞춤.
local ULT_CAM_BOSS_HEIGHT = 1.5      -- 보스 고정 시 socket +Z(머리 높이).
-- Weapon Setting
local STAFF_WEAPON_TRANSFORM = {
    location = Vec3(0.0, 40.0, 0.0),
    rotation = Vec3(-90.0, 0.0, 180.0),
    scale = Vec3(25.0, 25.0, 25.0)
}
local BOW_WEAPON_TRANSFORM = {
    location = Vec3(7.0, 50.0, 25.0),
    rotation = Vec3(-90.0, 0.0, 170.0),
    scale = Vec3(25.0, 25.0, 25.0)
}

local bow_aim_particle_component = nil
local DASH_ANIM_VAR = "Dash"
local ROLL_ANIM_VAR = "Roll"
local ATTACK_ANIM_VAR = "Attack"
local ARROW_ANIM_VAR = "Arrow"
local WALKING_AUDIO_LOOP_NAME = "HaruWalking"
local WALKING_AUDIO_MIN_SPEED = 0.05

local actor = nil
local ability_system = nil
local movement = nil
local anim_instance = nil
local dash_trail_particle = nil
local spring_arm = nil
local camera = nil
local weapon_mesh = nil
local camera_blend = nil
local beam_cam = nil
local get_owner_to_actor_aim_rotation   -- forward 선언: tick_beam_camera(앞쪽 정의)가 뒤의 정의를 upvalue 로 참조.
local boss_death_active = false   -- 보스 HP<=1(death) 진입 — aim→DeathAim 교체 + 카메라 보스 고정 시작.
local boss_death_q_done = false   -- death 중 Q 입력으로 DeathAim 끄고 카메라 고정 해제했는지.
local unlock_movement   -- forward 선언: tick_beam_camera 가 정의(아래)보다 앞에 있어 upvalue 로 참조한다.
local movement_locks = {}
local walking_audio_playing = false

local function log(message)
    print("[HaruController] " .. message)
end

local function join_keys(keys)
    if keys == nil then
        return ""
    end
    return table.concat(keys, ", ")
end

local function first_pressed_key(keys)
    if Input == nil or Input.GetKeyDown == nil or keys == nil then
        return nil
    end

    for index = 1, #keys do
        local key = keys[index]
        if key ~= nil and Input.GetKeyDown(key) then
            return key
        end
    end

    return nil
end

local function first_released_key(keys)
    if Input == nil or Input.GetKeyUp == nil or keys == nil then
        return nil
    end

    for index = 1, #keys do
        local key = keys[index]
        if key ~= nil and Input.GetKeyUp(key) then
            return key
        end
    end

    return nil
end

local function format_vec3(value)
    if value == nil then
        return "nil"
    end

    return string.format("(%.3f, %.3f, %.3f)", value.X or 0.0, value.Y or 0.0, value.Z or 0.0)
end

local function clamp01(value)
    if value == nil or value < 0.0 then
        return 0.0
    end
    if value > 1.0 then
        return 1.0
    end
    return value
end

local function smoothstep(value)
    local t = clamp01(value)
    return t * t * (3.0 - 2.0 * t)
end

local function get_ultimate_gauge(owner)
    if World ~= nil and World.GetPlayerUltimateGauge ~= nil then
        return World.GetPlayerUltimateGauge(owner)
    end
    return 0.0
end

local function get_ultimate_gauge_max(owner)
    if World ~= nil and World.GetPlayerUltimateGaugeMax ~= nil then
        return World.GetPlayerUltimateGaugeMax(owner)
    end
    return 100.0
end

local function is_ultimate_ready(owner)
    if World ~= nil and World.IsPlayerUltimateReady ~= nil then
        return World.IsPlayerUltimateReady(owner)
    end
    return get_ultimate_gauge(owner) >= get_ultimate_gauge_max(owner)
end

local function set_player_damage_enabled(owner, enabled, reason)
    if World ~= nil and World.SetPlayerDamageEnabled ~= nil then
        local ok = World.SetPlayerDamageEnabled(owner, enabled == true)
        log("Player damage " .. (enabled and "enabled" or "disabled")
            .. " reason=" .. tostring(reason)
            .. " ok=" .. tostring(ok))
        return ok
    end

    log("Player damage toggle unavailable reason=" .. tostring(reason))
    return false
end

local function set_bow_radial_blur(enabled, reason)
    if World == nil then
        return false
    end

    if enabled then
        if World.SetCameraRadialBlur ~= nil then
            local ok = World.SetCameraRadialBlur(
                BOW_RADIAL_BLUR_INTENSITY,
                BOW_RADIAL_BLUR_RADIUS,
                BOW_RADIAL_BLUR_SAMPLE_COUNT,
                0.5,
                0.5)
            log("AirBowShot radial blur enabled reason=" .. tostring(reason) .. " ok=" .. tostring(ok))
            return ok
        end
    elseif World.ClearCameraRadialBlur ~= nil then
        local ok = World.ClearCameraRadialBlur()
        log("AirBowShot radial blur cleared reason=" .. tostring(reason) .. " ok=" .. tostring(ok))
        return ok
    end

    log("AirBowShot radial blur unavailable reason=" .. tostring(reason))
    return false
end

local function lerp_number(a, b, alpha)
    if a == nil then
        return b
    end
    if b == nil then
        return a
    end
    return a + (b - a) * alpha
end

local function normalize_angle_delta(angle)
    local result = angle or 0.0
    while result > 180.0 do
        result = result - 360.0
    end
    while result < -180.0 do
        result = result + 360.0
    end
    return result
end

local function lerp_angle(a, b, alpha)
    if a == nil then
        return b
    end
    if b == nil then
        return a
    end
    return a + normalize_angle_delta(b - a) * alpha
end

local function lerp_vec3(a, b, alpha)
    if a == nil then
        return b
    end
    if b == nil then
        return a
    end
    return Vec3(
        lerp_number(a.X or 0.0, b.X or 0.0, alpha),
        lerp_number(a.Y or 0.0, b.Y or 0.0, alpha),
        lerp_number(a.Z or 0.0, b.Z or 0.0, alpha)
    )
end

local function resolve_actor()
    if actor ~= nil then
        return actor
    end

    if obj == nil then
        return nil
    end

    if obj.GetOwner ~= nil then
        actor = obj:GetOwner()
    else
        actor = obj
    end

    return actor
end

local function find_component_by_class(owner, class_name)
    if owner == nil or owner.GetComponents == nil then
        return nil
    end

    local components = owner:GetComponents()
    if components == nil then
        return nil
    end

    for index = 1, #components do
        local component = components[index]
        if component ~= nil and component.IsA ~= nil and component:IsA(class_name) then
            return component
        end
    end

    return nil
end

local function has_character_movement_api(candidate)
    return candidate ~= nil
        and candidate.GetMovementMode ~= nil
        and candidate.IsFalling ~= nil
        and (candidate.GetVelocity ~= nil or candidate.GetVelocityValue ~= nil)
end

local function get_character_movement(owner)
    if has_character_movement_api(movement) then
        return movement
    end

    movement = nil

    if owner ~= nil and owner.GetCharacterMovement ~= nil then
        movement = owner:GetCharacterMovement()
    end

    if not has_character_movement_api(movement) and owner ~= nil and owner.GetCharacterMovementComponent ~= nil then
        movement = owner:GetCharacterMovementComponent()
    end

    if not has_character_movement_api(movement) then
        movement = find_component_by_class(owner, "UCharacterMovementComponent")
    end

    return movement
end

local function get_dash_trail_particle(owner)
    if dash_trail_particle ~= nil then
        return dash_trail_particle
    end

    if owner ~= nil and owner.GetParticleSystemComponent ~= nil then
        dash_trail_particle = owner:GetParticleSystemComponent()
    end

    if dash_trail_particle == nil then
        dash_trail_particle = find_component_by_class(owner, "UParticleSystemComponent")
    end

    return dash_trail_particle
end

local function get_spring_arm(owner)
    if spring_arm ~= nil then
        return spring_arm
    end

    if owner ~= nil and owner.GetSpringArmComponent ~= nil then
        spring_arm = owner:GetSpringArmComponent()
    end

    if spring_arm == nil then
        spring_arm = find_component_by_class(owner, "USpringArmComponent")
    end

    return spring_arm
end

local function get_camera(owner)
    if camera ~= nil then
        return camera
    end

    if owner ~= nil and owner.GetCameraComponent ~= nil then
        camera = owner:GetCameraComponent()
    end

    if camera == nil and owner ~= nil and owner.GetCamera ~= nil then
        camera = owner:GetCamera()
    end

    if camera == nil then
        camera = find_component_by_class(owner, "UCameraComponent")
    end

    return camera
end

local function get_weapon_mesh(owner)
    if weapon_mesh ~= nil then
        return weapon_mesh
    end

    local fallback = nil
    if owner ~= nil and owner.GetStaticMeshComponent ~= nil then
        fallback = owner:GetStaticMeshComponent()
    end

    if owner ~= nil and owner.GetComponents ~= nil then
        local components = owner:GetComponents()
        if components ~= nil then
            for index = 1, #components do
                local component = components[index]
                if component ~= nil and component.IsA ~= nil and component:IsA("UStaticMeshComponent") then
                    if fallback == nil then
                        fallback = component
                    end

                    local mesh_path = ""
                    if component.GetMeshPath ~= nil then
                        mesh_path = tostring(component:GetMeshPath())
                    elseif component.MeshPath ~= nil then
                        mesh_path = tostring(component.MeshPath)
                    end

                    if string.find(mesh_path, "Staff_StaticMesh", 1, true) ~= nil
                        or string.find(mesh_path, "Bow_StaticMesh", 1, true) ~= nil then
                        weapon_mesh = component
                        return weapon_mesh
                    end
                end
            end
        end
    end

    weapon_mesh = fallback
    return weapon_mesh
end

local function apply_weapon_transform(mesh, transform, label)
    if mesh == nil or transform == nil then
        return
    end

    if transform.location ~= nil then
        if mesh.RelativeLocation ~= nil then
            mesh.RelativeLocation = transform.location
        elseif mesh.SetLocation ~= nil then
            mesh:SetLocation(transform.location)
        end
    end

    if transform.rotation ~= nil then
        if mesh.Rotation ~= nil then
            mesh.Rotation = transform.rotation
        elseif mesh.SetRotation ~= nil then
            mesh:SetRotation(transform.rotation)
        end
    end

    if transform.scale ~= nil then
        if mesh.RelativeScale ~= nil then
            mesh.RelativeScale = transform.scale
        end
    end

    log("weapon transform applied: " .. tostring(label)
        .. " location=" .. format_vec3(transform.location)
        .. " rotation=" .. format_vec3(transform.rotation)
        .. " scale=" .. format_vec3(transform.scale))
end

local function set_weapon_mesh(owner, mesh_path, label, transform)
    local mesh = get_weapon_mesh(owner)
    if mesh == nil or mesh.SetStaticMeshByPath == nil then
        log("weapon swap skipped: StaticMeshComponent unavailable label=" .. tostring(label))
        return false
    end

    local ok, result = pcall(function()
        return mesh:SetStaticMeshByPath(mesh_path)
    end)

    if ok and result then
        log("weapon swapped: " .. tostring(label) .. " path=" .. tostring(mesh_path))
        apply_weapon_transform(mesh, transform, label)
        return true
    end

    log("weapon swap failed: " .. tostring(label) .. " result=" .. tostring(result))
    return false
end

local function get_camera_state(owner)
    local arm = get_spring_arm(owner)
    local cam = get_camera(owner)

    return {
        arm_length = arm ~= nil and arm.GetTargetArmLength ~= nil and arm:GetTargetArmLength() or nil,
        socket_offset = arm ~= nil and arm.GetSocketOffset ~= nil and arm:GetSocketOffset() or nil,
        inherit_pitch = arm ~= nil and arm.GetInheritPitch ~= nil and arm:GetInheritPitch() or nil,
        inherit_yaw = arm ~= nil and arm.GetInheritYaw ~= nil and arm:GetInheritYaw() or nil,
        inherit_roll = arm ~= nil and arm.GetInheritRoll ~= nil and arm:GetInheritRoll() or nil,
        camera_rotation = cam ~= nil and cam.GetRotation ~= nil and cam:GetRotation() or nil,
        fov = cam ~= nil and cam.GetFOV ~= nil and cam:GetFOV() or nil
    }
end

local function apply_camera_state(owner, state)
    if state == nil then
        return
    end

    local arm = get_spring_arm(owner)
    if arm ~= nil then
        if state.arm_length ~= nil and arm.SetTargetArmLength ~= nil then
            arm:SetTargetArmLength(state.arm_length)
        end
        if state.socket_offset ~= nil and arm.SetSocketOffset ~= nil then
            arm:SetSocketOffset(state.socket_offset)
        end
        if state.inherit_pitch ~= nil and arm.SetInheritPitch ~= nil then
            arm:SetInheritPitch(state.inherit_pitch)
        end
        if state.inherit_yaw ~= nil and arm.SetInheritYaw ~= nil then
            arm:SetInheritYaw(state.inherit_yaw)
        end
        if state.inherit_roll ~= nil and arm.SetInheritRoll ~= nil then
            arm:SetInheritRoll(state.inherit_roll)
        end
        if arm.ResetLagState ~= nil then
            arm:ResetLagState()
        end
    end

    local cam = get_camera(owner)
    if cam ~= nil and state.camera_rotation ~= nil and cam.SetRotation ~= nil then
        cam:SetRotation(state.camera_rotation)
    end
    if cam ~= nil and state.fov ~= nil and cam.SetFOV ~= nil then
        cam:SetFOV(state.fov)
    end
end

local function start_camera_blend(owner, target_state, duration, reason)
    local from_state = get_camera_state(owner)
    camera_blend = {
        owner = owner,
        elapsed = 0.0,
        duration = duration or 0.0,
        reason = reason,
        from = from_state,
        target = target_state
    }

    log("camera blend started: " .. tostring(reason)
        .. " duration=" .. tostring(camera_blend.duration)
        .. " fromArm=" .. tostring(from_state.arm_length)
        .. " targetArm=" .. tostring(target_state ~= nil and target_state.arm_length or nil))

    if camera_blend.duration <= 0.0 then
        apply_camera_state(owner, target_state)
        camera_blend = nil
    end
end

local function tick_camera_blend(dt)
    if camera_blend == nil then
        return
    end

    camera_blend.elapsed = camera_blend.elapsed + (dt or 0.0)
    local alpha = smoothstep(camera_blend.elapsed / camera_blend.duration)
    local state = {
        arm_length = lerp_number(camera_blend.from.arm_length, camera_blend.target.arm_length, alpha),
        socket_offset = lerp_vec3(camera_blend.from.socket_offset, camera_blend.target.socket_offset, alpha),
        inherit_pitch = camera_blend.target.inherit_pitch,
        inherit_yaw = camera_blend.target.inherit_yaw,
        inherit_roll = camera_blend.target.inherit_roll,
        camera_rotation = camera_blend.target.camera_rotation,
        fov = lerp_number(camera_blend.from.fov, camera_blend.target.fov, alpha)
    }

    apply_camera_state(camera_blend.owner, state)

    if alpha >= 1.0 then
        apply_camera_state(camera_blend.owner, camera_blend.target)
        log("camera blend ended: " .. tostring(camera_blend.reason))
        camera_blend = nil
    end
end

-- ─────────────────────────────────────────────────────────────────
-- Beam camera — Q 궁극기 빔 생성 시 카메라를 빔 방향으로 고정하고 SocketOffset 을
-- 빔 전방(arm 로컬 +X)으로 dolly 시켜 "빔을 따라 이동"하는 연출.
-- 빔 거리의 BEAM_CAM_FOLLOW_RATIO(4/5)만큼, ULTIMATE_BEAM_SPEED(빔 속도)로 전진한다.
-- ult 어빌리티가 빔보다 먼저 끝나므로 드라이버(tick_beam_camera)는 메인 update 루프에서 돈다.
-- ─────────────────────────────────────────────────────────────────
-- 전역 시간 배율(슬로우 모션) 설정. value=1.0 이면 정상 속도 복원. Time 바인딩 없으면 무시(안전).
local function set_ultimate_time_dilation(value, reason)
    if Time ~= nil and Time.SetTimeDilation ~= nil then
        Time.SetTimeDilation(value)
        log("Ult time dilation -> " .. tostring(value) .. " reason=" .. tostring(reason))
        return true
    end
    log("Ult time dilation unavailable reason=" .. tostring(reason))
    return false
end

local function start_beam_camera(owner, restore_rot)
    if owner == nil or owner.GetControlRotation == nil then
        return
    end
    local rot = owner:GetControlRotation()        -- 빔 방향(=카메라 조준)을 캡처해 고정
    if rot == nil then
        return
    end
    local arm = get_spring_arm(owner)
    if arm == nil then
        return
    end

    beam_cam = {
        owner = owner,
        rot = rot,
        base_yaw = rot.Z or 0.0,                  -- Q 시점 시선 yaw(옆 얼굴 각도 기준).
        restore_rot = restore_rot,                -- 연출 종료 후 복원할 시선(ult 시작 시점). nil 이면 복원 안 함.
        saved = get_camera_state(owner),          -- 복귀용 스냅샷(arm/socket/inherit/fov)
        saved_use_pawn = arm.GetUsePawnControlRotation ~= nil and arm:GetUsePawnControlRotation() or nil,
        slomo_active = false,                     -- Phase0 오빗 슬로우 모션이 켜져 있는지(복원 1회 가드).
        hold_for_beam = true,                     -- 빔 발사 전까지 오빗 종료 지점(정면 anchor)에서 대기 → 그동안 애니 재생.
                                                  -- 빔 발사 시 release_beam_camera_hold 로 해제되면 보스-락 단계로 진행.
        elapsed = 0.0
    }

    -- 암이 ControlRotation 을 따라가게만 설정(거리/회전은 tick 이 옆 얼굴→보스로 제어). 빔 dolly 안 함.
    if arm.SetUsePawnControlRotation ~= nil then arm:SetUsePawnControlRotation(true) end
    if arm.SetInheritYaw ~= nil then arm:SetInheritYaw(true) end
    if arm.SetInheritPitch ~= nil then arm:SetInheritPitch(true) end
    if arm.ResetLagState ~= nil then arm:ResetLagState() end

    -- Phase0(오빗) 슬로우 모션 시작 — 전역 시간 배율을 낮춰 모션·빔·궁 타이머가 카메라 오빗과 함께 느려진다.
    -- (오빗이 끝나는 tick_beam_camera 에서 1.0 으로 복원. 조기 종료 시 finish_beam_camera 도 복원.)
    if ULT_CAM_ORBIT_DURATION > 0.0 and ULT_CAM_ORBIT_SLOMO < 1.0 then
        if set_ultimate_time_dilation(ULT_CAM_ORBIT_SLOMO, "ult orbit slomo start") then
            beam_cam.slomo_active = true
        end
    end

    log("Ult camera started: orbit -> front -> boss lock")
end

-- 빔 카메라 연출 종료 + 원위치 복원. 시간 완료(dolly 끝)와 마무리 킬 hit 양쪽에서 호출한다.
-- 현재 dolly 위치가 어디든 saved 스냅샷으로 블렌드하므로 중간에 일찍 불러도 안전하다.
local function finish_beam_camera(owner, reason)
    if beam_cam == nil then
        return
    end
    -- 오빗 슬로우 모션이 아직 켜진 채 종료(킬 등 조기 종료)되면 정상 속도로 복원.
    if beam_cam.slomo_active then
        set_ultimate_time_dilation(1.0, "beam cam finish slomo restore")
        beam_cam.slomo_active = false
    end
    local arm = get_spring_arm(owner)
    if arm ~= nil and arm.SetUsePawnControlRotation ~= nil and beam_cam.saved_use_pawn ~= nil then
        arm:SetUsePawnControlRotation(beam_cam.saved_use_pawn)
    end
    if beam_cam.restore_rot ~= nil and owner ~= nil and owner.SetControlRotation ~= nil then
        owner:SetControlRotation(beam_cam.restore_rot)           -- #3: ult 시작 시점 시선으로 복원
    end
    start_camera_blend(owner, beam_cam.saved, BEAM_CAM_RETURN_DURATION, reason or "beam cam return")
    unlock_movement(owner, ULTIMATE_SKILL_NAME)                  -- #4: 카메라 연출 종료 후 이동 잠금 해제
    beam_cam = nil
end

-- 오빗(카메라 회전)이 끝난 뒤 정면 anchor 에서 대기하던 빔 카메라를 풀어 보스-락 단계로 진행시킨다.
-- 빔 발사 시점(애니 종료 직전)에 호출 → "카메라 회전 → 애니 → 빔" 순서를 보장.
local function release_beam_camera_hold()
    if beam_cam ~= nil then
        beam_cam.hold_for_beam = false
    end
end

local function tick_beam_camera(dt)
    if beam_cam == nil then
        return
    end
    local owner = beam_cam.owner
    if owner == nil then
        beam_cam = nil
        return
    end

    -- 마무리 킬 hit(보스 HP 1→0) 감지 → 곧바로 끊지 않고 BEAM_CAM_KILL_LINGER 동안 dolly 를 더 진행한 뒤
    -- 빔 정지 + 복귀. (킬하자마자 hit판정이 끝나 카메라가 거의 안 움직이던 문제 보정 — linger 동안 빔도 유지.)
    if beam_cam.kill_linger == nil then
        if World ~= nil and World.HasPlayerBeamKilledBoss ~= nil and World.HasPlayerBeamKilledBoss(owner) then
            beam_cam.kill_linger = BEAM_CAM_KILL_LINGER
            log("Beam camera: boss kill hit -> linger " .. tostring(BEAM_CAM_KILL_LINGER) .. "s then return")
        end
    else
        beam_cam.kill_linger = beam_cam.kill_linger - (dt or 0.0)
        if beam_cam.kill_linger <= 0.0 then
            if World ~= nil and World.StopPlayerBeamAttack ~= nil then
                World.StopPlayerBeamAttack(owner)
            end
            finish_beam_camera(owner, "beam cam return (kill)")
            return
        end
    end

    beam_cam.elapsed = beam_cam.elapsed + (dt or 0.0)
    -- 빔 발사 전(hold_for_beam)에는 오빗 종료 지점(정면 anchor)에서 대기 — 그동안 spell 애니가 재생된다.
    -- 빔이 발사되면 release_beam_camera_hold 로 해제되어 이후 정면 hold→보스-락→복귀로 진행한다.
    if beam_cam.hold_for_beam and beam_cam.elapsed > ULT_CAM_ORBIT_DURATION then
        beam_cam.elapsed = ULT_CAM_ORBIT_DURATION
    end
    local arm = get_spring_arm(owner)
    local cam = get_camera(owner)

    -- Phase0(오빗): 컨트롤 회전 yaw 를 반바퀴(ORBIT_SWEEP=180°) 돌려 카메라가 캐릭터 주위를 회전.
    -- arm 이 ControlRotation 을 따라가므로(SetUsePawnControlRotation=true) yaw 만 돌리면 카메라가 공전한다.
    -- 시작 = 기존 카메라 위치(정면−SWEEP = base_yaw, 캐릭터 뒤), 종료 = 정면(base_yaw+SIDE_YAW).
    -- offset 은 −SWEEP(시작) → 0(종료)으로 움직여 카메라를 뒤→정면으로 반바퀴 스윕시킨다.
    -- 오빗이 끝나면 정면(anchor)에 정확히 도착 → 이후 phase 들이 그 위치를 그대로 이어받는다.
    local orbit_offset = 0.0
    if ULT_CAM_ORBIT_DURATION > 0.0 and beam_cam.elapsed < ULT_CAM_ORBIT_DURATION then
        orbit_offset = ULT_CAM_ORBIT_SWEEP * (smoothstep(beam_cam.elapsed / ULT_CAM_ORBIT_DURATION) - 1.0)
    elseif beam_cam.slomo_active then
        -- 오빗 종료 → 슬로우 모션 해제(1회). 이후 phase(정면 hold→보스)는 정상 속도로 진행.
        set_ultimate_time_dilation(1.0, "ult orbit slomo end")
        beam_cam.slomo_active = false
    end

    -- 카메라 위치 — 컨트롤 회전·arm·socket을 정면값으로 유지(+오빗 동안은 yaw 에 orbit_offset 가산).
    -- → 오빗이 끝나면(=offset 0) 캐릭터를 볼 때나 보스를 볼 때나 카메라 위치가 동일(이동 없음).
    if owner.SetControlRotation ~= nil then
        owner:SetControlRotation(Vec3(0.0, ULT_CAM_SIDE_PITCH, beam_cam.base_yaw + ULT_CAM_SIDE_YAW + orbit_offset))
    end
    if arm ~= nil then
        if arm.SetTargetArmLength ~= nil then arm:SetTargetArmLength(ULT_CAM_SIDE_DIST) end
        if arm.SetSocketOffset ~= nil then arm:SetSocketOffset(Vec3(0.0, 0.0, ULT_CAM_SIDE_HEIGHT)) end
    end

    -- 시선만 카메라 상대회전(cam:SetRotation)으로 캐릭터→보스 회전(위치는 위에서 고정).
    -- 카메라 world회전 = arm world(정면) × 상대회전 → 보스를 보려면 상대 = (보스 world look-at − 정면 회전).
    -- 오빗/정면 phase 동안은 상대 0 = arm 방향(=캐릭터)을 바라봐 캐릭터가 화면 중앙에 유지된다.
    if cam ~= nil and cam.SetRotation ~= nil then
        local side_yaw = beam_cam.base_yaw + ULT_CAM_SIDE_YAW
        local target_rel_yaw, target_rel_pitch = 0.0, 0.0
        local boss = (World ~= nil and World.FindActorByName ~= nil) and World.FindActorByName(SPRAY_TARGET_ACTOR_NAME) or nil
        if boss ~= nil then
            local aim = get_owner_to_actor_aim_rotation(owner, boss, nil)
            if aim ~= nil then
                target_rel_yaw = normalize_angle_delta(aim.yaw - side_yaw)
                target_rel_pitch = aim.pitch - ULT_CAM_SIDE_PITCH
            end
        end

        local side_end = ULT_CAM_ORBIT_DURATION + ULT_CAM_SIDE_DURATION   -- 오빗 + 정면 hold 종료 시각
        local rel_yaw, rel_pitch = 0.0, 0.0
        if beam_cam.elapsed < side_end then
            rel_yaw, rel_pitch = 0.0, 0.0                     -- Phase0~1: 상대 0 = arm 방향(=캐릭터)
        elseif beam_cam.elapsed < side_end + ULT_CAM_BLEND_DURATION then
            local t = smoothstep((beam_cam.elapsed - side_end) / ULT_CAM_BLEND_DURATION)
            rel_yaw = lerp_angle(0.0, target_rel_yaw, t)      -- blend: 같은 자리에서 시선만 캐릭터→보스
            rel_pitch = lerp_number(0.0, target_rel_pitch, t)
        else
            rel_yaw, rel_pitch = target_rel_yaw, target_rel_pitch   -- Phase2: 보스(같은 자리에서)
        end

        cam:SetRotation(Vec3(0.0, rel_pitch, rel_yaw))
    end

    -- 킬이 안 났을 때 폴백 종료.
    if beam_cam.elapsed >= (ULT_CAM_ORBIT_DURATION + ULT_CAM_SIDE_DURATION + ULT_CAM_BLEND_DURATION + ULT_CAM_BOSS_HOLD) then
        finish_beam_camera(owner, "ult cam return")
    end
end

-- 빔이 카메라 연출(옆 얼굴 + 보스 고정 + 복귀) 동안 유지되도록 필요한 빔 수명(초)을 계산한다.
local function beam_cam_total_duration()
    return ULT_CAM_ORBIT_DURATION + ULT_CAM_SIDE_DURATION + ULT_CAM_BLEND_DURATION + ULT_CAM_BOSS_HOLD + BEAM_CAM_RETURN_DURATION
end

local function reset_dash_trail(owner)
    local particle = get_dash_trail_particle(owner)
    if particle == nil then
        return
    end

    if particle.SetAutoActivate ~= nil then
        particle:SetAutoActivate(false)
    end

    if particle.SetResetOnActivate ~= nil then
        particle:SetResetOnActivate(true)
    end

    if particle.Deactivate ~= nil then
        particle:Deactivate()
    end

    if particle.ResetParticles ~= nil then
        particle:ResetParticles()
    end
end

local function start_dash_trail(owner)
    local particle = get_dash_trail_particle(owner)
    if particle == nil then
        return
    end

    local ok, err = pcall(function()
        if particle.Deactivate ~= nil then
            particle:Deactivate()
        end
        if particle.Activate ~= nil then
            particle:Activate(true)
        elseif particle.ResetParticles ~= nil then
            particle:ResetParticles()
        end
    end)

    if not ok then
        log("Dash trail start failed: " .. tostring(err))
    end
end

local function stop_dash_trail(owner)
    local particle = get_dash_trail_particle(owner)
    if particle ~= nil and particle.Deactivate ~= nil then
        particle:Deactivate()
    end
end

local function set_owner_movement_blocked(owner, blocked)
    if owner ~= nil and owner.SetCharacterMovementInputBlocked ~= nil then
        owner:SetCharacterMovementInputBlocked(blocked)
        return true
    end

    local character_movement = get_character_movement(owner)
    if character_movement ~= nil and character_movement.SetMovementInputBlocked ~= nil then
        character_movement:SetMovementInputBlocked(blocked)
        return true
    end

    return false
end

local function stop_owner_movement(owner)
    if owner ~= nil and owner.StopCharacterMovementImmediately ~= nil then
        owner:StopCharacterMovementImmediately()
        return true
    end

    local character_movement = get_character_movement(owner)
    if character_movement ~= nil and character_movement.StopMovementImmediately ~= nil then
        character_movement:StopMovementImmediately()
        return true
    end

    return false
end

local function get_movement_debug_state(character_movement)
    if character_movement == nil then
        return "movement=nil"
    end

    local name = "unknown"
    if character_movement.GetName ~= nil then
        name = tostring(character_movement:GetName())
    end

    local mode = "nil"
    if character_movement.GetMovementMode ~= nil then
        mode = tostring(character_movement:GetMovementMode())
    end

    local is_falling = "unavailable"
    if character_movement.IsFalling ~= nil then
        is_falling = tostring(character_movement:IsFalling())
    end

    local velocity = nil
    if character_movement.GetVelocity ~= nil then
        velocity = character_movement:GetVelocity()
    elseif character_movement.GetVelocityValue ~= nil then
        velocity = character_movement:GetVelocityValue()
    end

    return "movement=" .. name
        .. " mode=" .. mode
        .. " isFalling=" .. is_falling
        .. " velocity=" .. format_vec3(velocity)
end

local function get_movement_velocity(character_movement)
    if character_movement == nil then
        return nil
    end

    if character_movement.GetVelocity ~= nil then
        return character_movement:GetVelocity()
    end

    if character_movement.GetVelocityValue ~= nil then
        return character_movement:GetVelocityValue()
    end

    return nil
end

local function is_character_walking(character_movement)
    local velocity = get_movement_velocity(character_movement)
    if velocity == nil then
        return false
    end

    local horizontal_speed_sq = (velocity.X or 0.0) * (velocity.X or 0.0)
        + (velocity.Y or 0.0) * (velocity.Y or 0.0)
    if horizontal_speed_sq <= WALKING_AUDIO_MIN_SPEED * WALKING_AUDIO_MIN_SPEED then
        return false
    end

    return character_movement.IsFalling == nil or not character_movement:IsFalling()
end

local function update_walking_audio(owner)
    local character_movement = get_character_movement(owner)
    local should_play = is_character_walking(character_movement)

    if should_play then
        if Audio ~= nil and Audio.PlayLoop ~= nil then
            Audio.PlayLoop("Walking", WALKING_AUDIO_LOOP_NAME, 0.8, 1.0)
            walking_audio_playing = true
        end
        return
    end

    if walking_audio_playing and Audio ~= nil and Audio.StopLoop ~= nil then
        Audio.StopLoop(WALKING_AUDIO_LOOP_NAME)
        walking_audio_playing = false
    end
end

local function stop_walking_audio()
    if Audio ~= nil and Audio.StopLoop ~= nil then
        Audio.StopLoop(WALKING_AUDIO_LOOP_NAME)
    end
    walking_audio_playing = false
end

local function is_bow_airborne(character_movement)
    if character_movement == nil then
        return false
    end

    if character_movement.IsFalling ~= nil and character_movement:IsFalling() then
        return true
    end

    if character_movement.GetMovementMode ~= nil and character_movement:GetMovementMode() == 1 then
        return true
    end

    local velocity = nil
    if character_movement.GetVelocity ~= nil then
        velocity = character_movement:GetVelocity()
    elseif character_movement.GetVelocityValue ~= nil then
        velocity = character_movement:GetVelocityValue()
    end

    return velocity ~= nil and (velocity.Z or 0.0) > 0.05
end

local function start_dash_afterimage(owner, forward, duration)
    if owner == nil or owner.StartAfterImage == nil then
        return
    end

    local ok, started = pcall(function()
        return owner:StartAfterImage(
            forward,
            duration,
            DASH_AFTERIMAGE_INTENSITY,
            DASH_AFTERIMAGE_RADIUS,
            DASH_AFTERIMAGE_SAMPLES
        )
    end)

    if not ok then
        log("Dash afterimage failed: " .. tostring(started))
    end
end

local function has_movement_locks()
    for _, locked in pairs(movement_locks) do
        if locked then
            return true
        end
    end
    return false
end

local function lock_movement(owner, reason)
    movement_locks[reason] = true
    stop_owner_movement(owner)
    if not set_owner_movement_blocked(owner, true) then
        log("movement lock failed: CharacterMovementComponent blocking API unavailable reason=" .. tostring(reason))
    end
end

local function lock_movement_input_only(owner, reason)
    movement_locks[reason] = true
    if not set_owner_movement_blocked(owner, true) then
        log("movement input lock failed: CharacterMovementComponent blocking API unavailable reason=" .. tostring(reason))
    end
end

function unlock_movement(owner, reason)   -- 위에서 forward 선언한 local 에 바인딩
    movement_locks[reason] = nil
    if not has_movement_locks() then
        set_owner_movement_blocked(owner, false)
    end
end

local function get_actor_location_safe(target)
    if target == nil then
        return nil
    end

    local ok, location = pcall(function()
        return target.Location
    end)

    if ok then
        return location
    end

    return nil
end

local function set_actor_location_safe(target, location)
    if target == nil or location == nil then
        return false
    end

    local ok, err = pcall(function()
        target.Location = location
    end)

    if not ok then
        log("actor location lock failed: " .. tostring(err))
        return false
    end

    return true
end

local function set_actor_horizontal_location_locked(target, locked_location)
    if target == nil or locked_location == nil then
        return false
    end

    local current_location = get_actor_location_safe(target)
    if current_location == nil then
        return false
    end

    return set_actor_location_safe(target, Vec3(
        locked_location.X or current_location.X or 0.0,
        locked_location.Y or current_location.Y or 0.0,
        current_location.Z or locked_location.Z or 0.0
    ))
end

local function atan2_degrees(y, x)
    local radians = 0.0
    if math.atan2 ~= nil then
        radians = math.atan2(y, x)
    elseif x > 0.0 then
        radians = math.atan(y / x)
    elseif x < 0.0 and y >= 0.0 then
        radians = math.atan(y / x) + math.pi
    elseif x < 0.0 then
        radians = math.atan(y / x) - math.pi
    elseif y > 0.0 then
        radians = math.pi * 0.5
    elseif y < 0.0 then
        radians = -math.pi * 0.5
    end

    return radians * 180.0 / math.pi
end

function get_owner_to_actor_aim_rotation(owner, target_actor, label)   -- forward 선언된 local 에 할당
    local owner_location = get_actor_location_safe(owner)
    local target_location = get_actor_location_safe(target_actor)
    if owner_location == nil or target_location == nil then
        if label ~= nil then
            log(tostring(label) .. " face target skipped: location unavailable")
        end
        return nil
    end

    local dx = (target_location.X or 0.0) - (owner_location.X or 0.0)
    local dy = (target_location.Y or 0.0) - (owner_location.Y or 0.0)
    local dz = (target_location.Z or 0.0) - (owner_location.Z or 0.0)
    if dx * dx + dy * dy <= 0.000001 then
        if label ~= nil then
            log(tostring(label) .. " face target skipped: target too close")
        end
        return nil
    end

    local yaw = atan2_degrees(dy, dx)
    local flat_distance = math.sqrt(dx * dx + dy * dy)
    local pitch = -atan2_degrees(dz, flat_distance)
    return {
        yaw = yaw,
        pitch = pitch,
        owner_location = owner_location,
        target_location = target_location
    }
end

-- owner→target 월드 방향(정규화 Vec3). 빔을 카메라 POV 가 아닌 보스 쪽으로 발사하기 위해
-- BeamAttackComponent(SetPlayerBeamAimDirection)에 넘길 명시 방향을 계산한다. 위치 없으면 nil.
local function get_owner_to_actor_direction(owner, target_actor)
    local owner_location = get_actor_location_safe(owner)
    local target_location = get_actor_location_safe(target_actor)
    if owner_location == nil or target_location == nil then
        return nil
    end
    local dx = (target_location.X or 0.0) - (owner_location.X or 0.0)
    local dy = (target_location.Y or 0.0) - (owner_location.Y or 0.0)
    local dz = (target_location.Z or 0.0) - (owner_location.Z or 0.0)
    local len = math.sqrt(dx * dx + dy * dy + dz * dz)
    if len <= 1e-6 then
        return nil
    end
    return Vec3(dx / len, dy / len, dz / len)
end

-- yaw(도, +X=0°, 화면 평면)로부터 수평 진행 방향 벡터를 만든다. 보스를 못 찾았을 때 폴백 발사 방향용.
local function direction_from_yaw(yaw)
    local rad = (yaw or 0.0) * math.pi / 180.0
    return Vec3(math.cos(rad), math.sin(rad), 0.0)
end

-- 빔 발사 방향을 명시적으로 보스(또는 폴백 yaw)로 지정. 카메라가 캐릭터를 비추는 동안에도 빔이 보스로 진행하게 한다.
-- 반환: 방향을 지정했으면 true. boss 우선, 실패 시 fallback_yaw 가 주어지면 그 방향으로.
local function aim_beam_at_boss(owner, boss, fallback_yaw, label)
    if World == nil or World.SetPlayerBeamAimDirection == nil then
        return false
    end
    local dir = (boss ~= nil) and get_owner_to_actor_direction(owner, boss) or nil
    if dir == nil and fallback_yaw ~= nil then
        dir = direction_from_yaw(fallback_yaw)
    end
    if dir == nil then
        return false
    end
    local ok = World.SetPlayerBeamAimDirection(owner, dir)
    log("Ultimate beam aim dir set (" .. tostring(label) .. ") dir=" .. format_vec3(dir)
        .. " boss=" .. tostring(boss ~= nil) .. " ok=" .. tostring(ok))
    return ok == true
end

-- 카메라(컨트롤 회전)를 대상 액터로 고정 — get_owner_to_actor_aim_rotation 재사용. 매 틱 호출 시 마우스룩 덮어씀.
local function lock_camera_to_actor(owner, target)
    local aim = get_owner_to_actor_aim_rotation(owner, target, nil)
    if aim == nil or owner == nil or owner.SetControlRotation == nil then
        return
    end
    local control_roll = 0.0
    local ok, cr = pcall(function() return owner:GetControlRotation() end)
    if ok and cr ~= nil then control_roll = cr.X or 0.0 end
    pcall(function() owner:SetControlRotation(Vec3(control_roll, aim.pitch, aim.yaw)) end)
end

-- 보스 death 연출: HP<=1 최초 진입 시 aim→DeathAim UI 교체 + 카메라 보스 고정. (Q 입력은 activate_ultimate 에서 처리)
local function update_boss_death_sequence(owner)
    if owner == nil or World == nil or World.FindActorByName == nil then
        return
    end
    local boss = World.FindActorByName(SPRAY_TARGET_ACTOR_NAME)
    if boss == nil or boss.GetCurrentHealth == nil then
        return
    end

    -- death 진입(보스 HP<=1) 최초 1회 — aim 끄고 DeathAim 켜기.
    if not boss_death_active and boss:GetCurrentHealth() <= 1.0 then
        boss_death_active = true
        if UI ~= nil and UI.SetElementVisible ~= nil then
            UI.SetElementVisible("aim", false)
            UI.SetElementVisible("DeathAim", true)
        end
        log("Boss death entered: UI aim->DeathAim, camera lock on boss")
    end

    -- death 동안 카메라를 보스 정면으로 고정. Q 이전 + 빔캠 비활성일 때만(빔캠/마무리 연출이 카메라 우선).
    if boss_death_active and not boss_death_q_done and beam_cam == nil then
        lock_camera_to_actor(owner, boss)
    end
end

local function face_owner_to_actor_yaw(owner, target_actor, label)
    local aim = get_owner_to_actor_aim_rotation(owner, target_actor, label)
    if aim == nil then
        return false
    end

    local current_rotation = nil
    local ok, rotation = pcall(function()
        return owner.Rotation
    end)
    if ok then
        current_rotation = rotation
    end

    local roll = current_rotation ~= nil and (current_rotation.X or 0.0) or 0.0
    local actor_pitch = current_rotation ~= nil and (current_rotation.Y or 0.0) or 0.0
    local new_rotation = Vec3(roll, actor_pitch, aim.yaw)
    local set_ok, err = pcall(function()
        owner.Rotation = new_rotation
    end)

    if not set_ok then
        log(tostring(label) .. " face target failed: " .. tostring(err))
        return false
    end

    if owner.SetControlRotation ~= nil then
        local control_roll = 0.0
        local control_ok, control_rotation = pcall(function()
            return owner:GetControlRotation()
        end)
        if control_ok and control_rotation ~= nil then
            control_roll = control_rotation.X or 0.0
        end

        local control_set_ok, control_err = pcall(function()
            return owner:SetControlRotation(Vec3(control_roll, aim.pitch, aim.yaw))
        end)
        if not control_set_ok then
            log(tostring(label) .. " control rotation set failed: " .. tostring(control_err))
        end
    end

    local target_name = SPRAY_TARGET_ACTOR_NAME
    if target_actor ~= nil and target_actor.GetName ~= nil then
        target_name = tostring(target_actor:GetName())
    end

    log(tostring(label) .. " faced target=" .. tostring(target_name)
        .. " yaw=" .. tostring(aim.yaw)
        .. " pitch=" .. tostring(aim.pitch)
        .. " ownerLoc=" .. format_vec3(aim.owner_location)
        .. " targetLoc=" .. format_vec3(aim.target_location))
    return true
end

local function start_spray_face_blend(owner, target_actor, ability)
    if owner == nil or target_actor == nil or ability == nil then
        return false
    end

    local aim = get_owner_to_actor_aim_rotation(owner, target_actor, "SprayAttack")
    if aim == nil then
        return false
    end

    local actor_rotation = nil
    local actor_ok, actor_rot = pcall(function()
        return owner.Rotation
    end)
    if actor_ok then
        actor_rotation = actor_rot
    end

    local control_rotation = nil
    if owner.GetControlRotation ~= nil then
        local control_ok, control_rot = pcall(function()
            return owner:GetControlRotation()
        end)
        if control_ok then
            control_rotation = control_rot
        end
    end

    ability.spray_face_blend = {
        elapsed = 0.0,
        duration = SPRAY_FACE_BLEND_DURATION,
        actor_roll = actor_rotation ~= nil and (actor_rotation.X or 0.0) or 0.0,
        actor_pitch = actor_rotation ~= nil and (actor_rotation.Y or 0.0) or 0.0,
        from_actor_yaw = actor_rotation ~= nil and (actor_rotation.Z or 0.0) or 0.0,
        from_control_roll = control_rotation ~= nil and (control_rotation.X or 0.0) or 0.0,
        from_control_pitch = control_rotation ~= nil and (control_rotation.Y or 0.0) or 0.0,
        from_control_yaw = control_rotation ~= nil and (control_rotation.Z or 0.0) or (actor_rotation ~= nil and (actor_rotation.Z or 0.0) or 0.0),
        target_pitch = aim.pitch,
        target_yaw = aim.yaw
    }

    log("SprayAttack face blend started duration=" .. tostring(SPRAY_FACE_BLEND_DURATION)
        .. " targetYaw=" .. tostring(aim.yaw)
        .. " targetPitch=" .. tostring(aim.pitch)
        .. " ownerLoc=" .. format_vec3(aim.owner_location)
        .. " targetLoc=" .. format_vec3(aim.target_location))
    return true
end

local function tick_spray_face_blend(owner, ability, dt)
    if owner == nil or ability == nil or ability.spray_face_blend == nil then
        return false
    end

    local blend = ability.spray_face_blend
    blend.elapsed = blend.elapsed + (dt or 0.0)
    local alpha = smoothstep(blend.duration > 0.0 and (blend.elapsed / blend.duration) or 1.0)
    local yaw = lerp_angle(blend.from_control_yaw, blend.target_yaw, alpha)
    local pitch = lerp_angle(blend.from_control_pitch, blend.target_pitch, alpha)

    local actor_yaw = lerp_angle(blend.from_actor_yaw, blend.target_yaw, alpha)
    local actor_set_ok, actor_err = pcall(function()
        owner.Rotation = Vec3(blend.actor_roll, blend.actor_pitch, actor_yaw)
    end)
    if not actor_set_ok then
        log("SprayAttack face blend actor rotation failed: " .. tostring(actor_err))
    end

    if owner.SetControlRotation ~= nil then
        local control_set_ok, control_err = pcall(function()
            return owner:SetControlRotation(Vec3(blend.from_control_roll, pitch, yaw))
        end)
        if not control_set_ok then
            log("SprayAttack face blend control rotation failed: " .. tostring(control_err))
        end
    end

    if alpha >= 1.0 then
        ability.spray_face_blend = nil
        log("SprayAttack face blend ended")
    end

    return true
end

local function find_spray_target_actor()
    if World == nil or World.FindActorByName == nil then
        return nil
    end

    local ok, target = pcall(function()
        return World.FindActorByName(SPRAY_TARGET_ACTOR_NAME)
    end)

    if ok then
        return target
    end

    log("SprayAttack target lookup failed: " .. tostring(target))
    return nil
end

local function demo_kill_actor(target, label)
    if target == nil then
        log("Demo kill " .. tostring(label) .. " failed: actor is nil")
        return false
    end
    if target.GetCurrentHealth == nil or target.GetDamaged == nil then
        log("Demo kill " .. tostring(label) .. " failed: health API unavailable actor=" .. tostring(target))
        return false
    end

    local health = target:GetCurrentHealth()
    if health == nil then
        health = 0.0
    end
    if health <= 0.0 then
        log("Demo kill " .. tostring(label) .. " skipped: already dead health=" .. tostring(health))
        return true
    end

    local damage = health
    local applied = target:GetDamaged(damage)
    log("Demo kill " .. tostring(label)
        .. " actor=" .. (target.GetName ~= nil and target:GetName() or tostring(target))
        .. " damage=" .. tostring(damage)
        .. " applied=" .. tostring(applied)
        .. " healthAfter=" .. tostring(target:GetCurrentHealth()))
    return true
end

local function handle_demo_kill_inputs(owner)
    if Input == nil or Input.GetKeyDown == nil then
        return
    end

    if Input.GetKeyDown(DEMO_KILL_PLAYER_KEY) then
        demo_kill_actor(owner, "player")
    end

    if Input.GetKeyDown(DEMO_KILL_BOSS_KEY) then
        demo_kill_actor(find_spray_target_actor(), "boss")
    end
end

local function get_anim_instance(owner)
    if anim_instance ~= nil then
        return anim_instance
    end

    local mesh = nil
    if owner ~= nil and owner.GetMesh ~= nil then
        mesh = owner:GetMesh()
    end

    if mesh == nil and owner ~= nil and owner.GetSkeletalMeshComponent ~= nil then
        mesh = owner:GetSkeletalMeshComponent()
    end

    if mesh == nil then
        mesh = find_component_by_class(owner, "USkeletalMeshComponent")
    end

    if mesh ~= nil and mesh.GetAnimInstance ~= nil then
        anim_instance = mesh:GetAnimInstance()
    end

    return anim_instance
end

local function set_anim_bool(owner, variable_name, value)
    local instance = get_anim_instance(owner)
    if instance == nil or instance.SetGraphVariableBool == nil then
        log("AnimGraph bool set skipped: " .. variable_name .. " anim instance unavailable")
        return false
    end

    local ok, result = pcall(function()
        return instance:SetGraphVariableBool(variable_name, value)
    end)

    if not ok then
        log("AnimGraph bool set failed: " .. variable_name .. " error=" .. tostring(result))
        return false
    end

    if not result then
        log("AnimGraph bool set failed: variable not found " .. variable_name)
        return false
    end

    return true
end

local function activate_movement_ability(owner, ability, label, anim_var, distance, duration)
    if owner == nil then
        log(label .. " activate failed: owner is nil")
        ability.active_remaining = 0.0
        return
    end

    local forward = owner.Forward
    if forward == nil then
        forward = Vec3(1.0, 0.0, 0.0)
    end

    set_anim_bool(owner, anim_var, true)
    lock_movement(owner, label)

    if owner.StartCharacterDash ~= nil then
        local ok, started = pcall(function()
            return owner:StartCharacterDash(forward, distance, duration)
        end)

        if not ok then
            log(label .. " activate failed: StartCharacterDash call error: " .. tostring(started))
            set_anim_bool(owner, anim_var, false)
            unlock_movement(owner, label)
            ability.active_remaining = 0.0
            return
        end

        if not started then
            log(label .. " activate failed: StartCharacterDash returned false")
            set_anim_bool(owner, anim_var, false)
            unlock_movement(owner, label)
            ability.active_remaining = 0.0
            return
        end

        if label == "Dash" then
            start_dash_trail(owner)
            start_dash_afterimage(owner, forward, duration)
        end

        log(label .. " activated: distance=" .. tostring(distance)
            .. " duration=" .. tostring(duration)
            .. " forward=" .. format_vec3(forward))
        return
    end

    local dash_movement = get_character_movement(owner)
    if dash_movement == nil then
        log(label .. " activate failed: UCharacterMovementComponent not found")
        set_anim_bool(owner, anim_var, false)
        unlock_movement(owner, label)
        ability.active_remaining = 0.0
        return
    end

    if dash_movement.StartDash == nil then
        local movement_name = "unknown"
        if dash_movement.GetName ~= nil then
            movement_name = dash_movement:GetName()
        end
        log(label .. " activate failed: StartDash binding is unavailable on movement=" .. tostring(movement_name))
        set_anim_bool(owner, anim_var, false)
        unlock_movement(owner, label)
        ability.active_remaining = 0.0
        return
    end

    local ok, started = pcall(function()
        return dash_movement:StartDash(forward, distance, duration)
    end)
    if not ok then
        log(label .. " activate failed: StartDash call error: " .. tostring(started))
        set_anim_bool(owner, anim_var, false)
        unlock_movement(owner, label)
        ability.active_remaining = 0.0
        return
    end

    if not started then
        log(label .. " activate failed: StartDash returned false")
        set_anim_bool(owner, anim_var, false)
        unlock_movement(owner, label)
        ability.active_remaining = 0.0
        return
    end

    if label == "Dash" then
        start_dash_trail(owner)
        start_dash_afterimage(owner, forward, duration)
    end

    log(label .. " activated: distance=" .. tostring(distance)
        .. " duration=" .. tostring(duration)
        .. " forward=" .. format_vec3(forward))
end

local function activate_dash(owner, ability)
    activate_movement_ability(owner, ability, "Dash", DASH_ANIM_VAR, DASH_DISTANCE, DASH_DURATION)
    Audio.Play("Dash", 0.6)
end

local function end_dash(owner, ability)
    if owner ~= nil then
        set_anim_bool(owner, DASH_ANIM_VAR, false)
        unlock_movement(owner, "Dash")
        stop_dash_trail(owner)
    end
    log("Dash ended")
end

local function activate_roll(owner, ability)
    activate_movement_ability(owner, ability, "Roll", ROLL_ANIM_VAR, ROLL_DISTANCE, ROLL_DURATION)
end

local function end_roll(owner, ability)
    if owner ~= nil then
        set_anim_bool(owner, ROLL_ANIM_VAR, false)
        unlock_movement(owner, "Roll")
    end
    log("Roll ended")
end

local function face_owner_to_camera_yaw(owner, ability)
    if owner == nil then
        return false
    end

    if owner.FaceYawToControlRotation ~= nil then
        local ok, result = pcall(function()
            return owner:FaceYawToControlRotation()
        end)
        if ok and result then
            return true
        end
    end

    if ability ~= nil and not ability.face_yaw_warning_logged then
        ability.face_yaw_warning_logged = true
        log("AirBowShot warning: FaceYawToControlRotation unavailable")
    end
    return false
end

local function set_bow_first_person_camera(owner, ability, enabled)
    local arm = get_spring_arm(owner)
    if arm == nil then
        return false
    end

    if enabled then
        if arm.SetInheritPitch ~= nil then
            arm:SetInheritPitch(false)
        elseif ability ~= nil and not ability.inherit_pitch_warning_logged then
            ability.inherit_pitch_warning_logged = true
            log("AirBowShot warning: SpringArmComponent.SetInheritPitch unavailable")
        end
    elseif ability ~= nil and ability.saved_inherit_pitch ~= nil and arm.SetInheritPitch ~= nil then
        arm:SetInheritPitch(ability.saved_inherit_pitch)
    end

    if arm.ResetLagState ~= nil then
        arm:ResetLagState()
    end
    return true
end

local function update_bow_first_person_camera(owner, ability)
    if owner == nil then
        return false
    end

    local cam = get_camera(owner)
    if cam == nil or cam.SetRotation == nil or owner.GetControlRotation == nil then
        return false
    end

    local control = owner:GetControlRotation()
    if control == nil then
        return false
    end

    if owner.Rotation ~= nil then
        local saved = ability ~= nil and ability.saved_actor_rotation or nil
        owner.Rotation = Vec3(saved ~= nil and (saved.X or 0.0) or 0.0, control.Y or 0.0, control.Z or 0.0)
    end

    cam:SetRotation(Vec3(0.0, 0.0, 0.0))
    return true
end

local function restore_bow_actor_rotation(owner, ability)
    if owner == nil or ability == nil or ability.saved_actor_rotation == nil or owner.Rotation == nil then
        return
    end

    local current = owner.Rotation
    local saved = ability.saved_actor_rotation
    owner.Rotation = Vec3(saved.X or 0.0, saved.Y or 0.0, current ~= nil and (current.Z or 0.0) or (saved.Z or 0.0))
end

local function play_arrow_release_slomo(owner)
    if owner == nil then
        log("AirBowShot slomo skipped: owner is nil")
        return false
    end

    if owner.Slomo ~= nil then
        local ok, result = pcall(function()
            return owner:Slomo(BOW_RELEASE_SLOMO_DURATION, BOW_RELEASE_SLOMO_DILATION)
        end)
        if ok and result then
            log("AirBowShot slomo started: duration=" .. tostring(BOW_RELEASE_SLOMO_DURATION)
                .. " dilation=" .. tostring(BOW_RELEASE_SLOMO_DILATION))
            return true
        end
        log("AirBowShot slomo failed: " .. tostring(result))
        return false
    end

    local action = nil
    if owner.GetOrCreateActionComponent ~= nil then
        action = owner:GetOrCreateActionComponent()
    elseif owner.GetActionComponent ~= nil then
        action = owner:GetActionComponent()
    end

    if action ~= nil and action.Slomo ~= nil then
        action:Slomo(BOW_RELEASE_SLOMO_DURATION, BOW_RELEASE_SLOMO_DILATION)
        log("AirBowShot slomo started: duration=" .. tostring(BOW_RELEASE_SLOMO_DURATION)
            .. " dilation=" .. tostring(BOW_RELEASE_SLOMO_DILATION))
        return true
    end

    log("AirBowShot slomo skipped: ActionComponent unavailable")
    return false
end

local function get_bow_aim_particle_location(owner)
    if World ~= nil and World.GetCameraProjectileLocation ~= nil then
        local ok, loc = pcall(function()
            return World.GetCameraProjectileLocation(BOW_AIM_PARTICLE_OFFSET)
        end)
        if ok and loc ~= nil then
            return loc
        end
    end

    if owner ~= nil and owner.GetActorLocation ~= nil then
        return owner:GetActorLocation()
    end
    return Vec3(0.0, 0.0, 0.0)
end

local function update_bow_aim_particle(owner, ability)
    if ability == nil then
        return
    end

    local loc = get_bow_aim_particle_location(owner)
    if bow_aim_particle_component == nil then
        if World == nil or World.SpawnEmitterAtLocation == nil then
            log("AirBowShot Aim particle skipped: World.SpawnEmitterAtLocation unavailable")
            return
        end

        bow_aim_particle_component = World.SpawnEmitterAtLocation(
            BOW_AIM_PARTICLE_PATH,
            loc,
            Vec3(0.0, 0.0, 0.0),
            true)
        ability.aim_particle_active = bow_aim_particle_component ~= nil
        log("AirBowShot Aim particle spawned: component=" .. tostring(bow_aim_particle_component ~= nil)
            .. " path=" .. tostring(BOW_AIM_PARTICLE_PATH)
            .. " offset=" .. format_vec3(BOW_AIM_PARTICLE_OFFSET)
            .. " loc=" .. format_vec3(loc))
        return
    end

    if bow_aim_particle_component.SetLocation ~= nil then
        bow_aim_particle_component:SetLocation(loc)
    end

    if not ability.aim_particle_active then
        if bow_aim_particle_component.ResetParticles ~= nil then
            bow_aim_particle_component:ResetParticles()
        end
        if bow_aim_particle_component.Activate ~= nil then
            bow_aim_particle_component:Activate(true)
        end
        ability.aim_particle_active = true
        log("AirBowShot Aim particle activated: offset=" .. format_vec3(BOW_AIM_PARTICLE_OFFSET)
            .. " loc=" .. format_vec3(loc))
    end
end

local function stop_bow_aim_particle(ability, reason)
    if ability ~= nil then
        ability.aim_particle_active = false
    end

    if bow_aim_particle_component ~= nil and bow_aim_particle_component.Deactivate ~= nil then
        bow_aim_particle_component:Deactivate()
        log("AirBowShot Aim particle deactivated: reason=" .. tostring(reason))
    end
end

local function prepare_held_arrow(owner, ability, reason)
    if ability == nil then
        log("AirBowShot arrow prepare skipped: ability is nil reason=" .. tostring(reason))
        return nil
    end

    if ability.arrow_projectile ~= nil then
        if World ~= nil and World.UpdateCameraArrowProjectile ~= nil then
            World.UpdateCameraArrowProjectile(ability.arrow_projectile, BOW_PROJECTILE_OFFSET)
        end
        log("AirBowShot arrow prepare skipped: already prepared reason=" .. tostring(reason))
        return ability.arrow_projectile
    end

    if owner ~= nil then
        face_owner_to_camera_yaw(owner, ability)
        update_bow_first_person_camera(owner, ability)
    end

    if World ~= nil and World.PrepareCameraArrowProjectile ~= nil then
        ability.arrow_projectile = World.PrepareCameraArrowProjectile(BOW_PROJECTILE_OFFSET)
        log("AirBowShot arrow prepared by " .. tostring(reason)
            .. ": projectile=" .. tostring(ability.arrow_projectile ~= nil)
            .. " offset=" .. format_vec3(BOW_PROJECTILE_OFFSET))
    else
        log("AirBowShot arrow prepare skipped: World.PrepareCameraArrowProjectile unavailable reason=" .. tostring(reason))
    end

    return ability.arrow_projectile
end

local function launch_prepared_arrow(owner, ability, reason)
    if ability == nil then
        log("AirBowShot launch skipped: ability is nil reason=" .. tostring(reason))
        return false
    end

    if ability.arrow_released then
        log("AirBowShot launch skipped: arrow already released reason=" .. tostring(reason))
        return false
    end

    if ability.arrow_projectile == nil or World == nil or World.LaunchCameraArrowProjectile == nil then
        log("AirBowShot launch failed: prepared arrow or World.LaunchCameraArrowProjectile unavailable reason=" .. tostring(reason))
        return false
    end

    if owner ~= nil then
        face_owner_to_camera_yaw(owner, ability)
        update_bow_first_person_camera(owner, ability)
    end

    local launched = World.LaunchCameraArrowProjectile(ability.arrow_projectile, BOW_PROJECTILE_SPEED, BOW_PROJECTILE_OFFSET)
    log("AirBowShot launched by " .. tostring(reason) .. ": launched=" .. tostring(launched))
    if launched then
        ability.arrow_released = true
        ability.arrow_projectile = nil
        if Audio ~= nil and Audio.Play ~= nil then
            Audio.Play("Arrow", 10.0)
        end
        play_arrow_release_slomo(owner)
        return true
    end

    return false
end

local function launch_prepared_arrow_from_stored_aim(owner, ability, reason)
    if ability == nil then
        log("AirBowShot ultimate launch skipped: ability is nil reason=" .. tostring(reason))
        return false
    end

    if ability.arrow_released then
        log("AirBowShot ultimate launch skipped: arrow already released reason=" .. tostring(reason))
        return false
    end

    if ability.arrow_projectile == nil then
        prepare_held_arrow(owner, ability, tostring(reason) .. " fallback prepare")
    end

    if ability.arrow_projectile == nil then
        log("AirBowShot ultimate launch failed: no prepared arrow reason=" .. tostring(reason))
        return false
    end

    local launched = false
    if World ~= nil and World.LaunchArrowProjectileWithDirection ~= nil
        and ability.cutscene_launch_location ~= nil
        and ability.cutscene_launch_forward ~= nil then
        launched = World.LaunchArrowProjectileWithDirection(
            ability.arrow_projectile,
            ability.cutscene_launch_location,
            ability.cutscene_launch_forward,
            BOW_PROJECTILE_SPEED)
    else
        launched = launch_prepared_arrow(owner, ability, reason)
        return launched
    end

    log("AirBowShot ultimate launched by " .. tostring(reason)
        .. ": launched=" .. tostring(launched)
        .. " loc=" .. format_vec3(ability.cutscene_launch_location)
        .. " forward=" .. format_vec3(ability.cutscene_launch_forward))
    if launched then
        ability.arrow_released = true
        ability.arrow_projectile = nil
        if Audio ~= nil and Audio.Play ~= nil then
            Audio.Play("Arrow", 10.0)
        end
        play_arrow_release_slomo(owner)
        return true
    end
    return false
end

local function restore_bow_aim(owner, ability)
    if ability == nil then
        return
    end

    local character_movement = get_character_movement(owner)
    if character_movement ~= nil and ability.saved_gravity ~= nil and character_movement.SetGravity ~= nil then
        character_movement:SetGravity(ability.saved_gravity)
    end

    start_camera_blend(owner, {
        arm_length = ability.saved_arm_length,
        socket_offset = ability.saved_socket_offset,
        inherit_pitch = ability.saved_inherit_pitch,
        inherit_yaw = ability.saved_inherit_yaw,
        inherit_roll = ability.saved_inherit_roll,
        camera_rotation = ability.saved_camera_rotation,
        fov = ability.saved_fov
    }, BOW_CAMERA_RESTORE_DURATION, "AirBowShot restore")

    log("AirBowShot camera restore requested: armLength=" .. tostring(ability.saved_arm_length)
        .. " socketOffset=" .. format_vec3(ability.saved_socket_offset)
        .. " fov=" .. tostring(ability.saved_fov))
end

local function start_bow_ultimate_cutscene(owner, ability)
    if ability == nil or ability.cutscene_active or HaruUltimateCutscene.IsActive() then
        return
    end

    if camera_blend ~= nil and camera_blend.target ~= nil then
        apply_camera_state(owner, camera_blend.target)
        log("AirBowShot ultimate cutscene forced pending camera blend: " .. tostring(camera_blend.reason))
    end
    camera_blend = nil
    stop_bow_aim_particle(ability, "Ultimate cutscene start")

    local cutscene_target_actor = find_spray_target_actor()
    if cutscene_target_actor ~= nil then
        face_owner_to_actor_yaw(owner, cutscene_target_actor, "AirBowShot ultimate cutscene")
    else
        face_owner_to_camera_yaw(owner, ability)
    end
    set_bow_first_person_camera(owner, ability, true)
    apply_camera_state(owner, {
        arm_length = BOW_CAMERA_ARM_LENGTH,
        socket_offset = BOW_CAMERA_SOCKET_OFFSET,
        inherit_pitch = false,
        fov = BOW_CAMERA_FOV
    })
    update_bow_first_person_camera(owner, ability)
    local cutscene_spring_arm = get_spring_arm(owner)
    if cutscene_spring_arm ~= nil and cutscene_spring_arm.RefreshSpringArm ~= nil then
        cutscene_spring_arm:RefreshSpringArm(0.0)
    end

    ability.cutscene_active = true
    ability.cutscene_finished = false
    ability.cutscene_elapsed = 0.0
    ability.cutscene_eye_from = get_camera_state(owner)
    ability.damage_disabled_for_cutscene = set_player_damage_enabled(owner, false, "Ultimate cutscene start")
    ability.cutscene_launch_location = World ~= nil and World.GetCameraProjectileLocation ~= nil
        and World.GetCameraProjectileLocation(BOW_PROJECTILE_OFFSET) or nil
    ability.cutscene_launch_forward = World ~= nil and World.GetCameraProjectileForward ~= nil
        and World.GetCameraProjectileForward() or nil

    if ability.arrow_projectile == nil then
        prepare_held_arrow(owner, ability, "Ultimate release fallback")
    end
    if ability.arrow_projectile ~= nil and World ~= nil and World.UpdateCameraArrowProjectile ~= nil then
        World.UpdateCameraArrowProjectile(ability.arrow_projectile, BOW_PROJECTILE_OFFSET)
    end

    local cutscene_started = HaruUltimateCutscene.Start({
        owner = owner,
        spring_arm = get_spring_arm(owner),
        camera = get_camera(owner),
        log = function(message)
            log("UltimateCutscene " .. tostring(message))
        end,
        on_tick_lock = function()
            stop_owner_movement(owner)
        end,
        on_tick_projectile = function()
            if ability.arrow_projectile ~= nil and World ~= nil and World.UpdateCameraArrowProjectile ~= nil then
                World.UpdateCameraArrowProjectile(ability.arrow_projectile, BOW_PROJECTILE_OFFSET)
            end
        end,
        on_finish = function()
            if Engine ~= nil and Engine.ResumeGame ~= nil then
                Engine.ResumeGame()
            end
            ability.cutscene_finished = true
            ability.cutscene_active = false
            launch_prepared_arrow_from_stored_aim(owner, ability, "Ultimate cutscene end")
            if World ~= nil and World.ResetPlayerUltimateGauge ~= nil then
                World.ResetPlayerUltimateGauge(owner)
            end
            if ability.damage_disabled_for_cutscene then
                set_player_damage_enabled(owner, true, "Ultimate cutscene finish")
                ability.damage_disabled_for_cutscene = false
            end
            log("AirBowShot ultimate cutscene finished; gauge reset requested")
            if ability_system ~= nil then
                ability_system:EndAbility(ability)
            end
        end
    })

    if not cutscene_started then
        if Engine ~= nil and Engine.ResumeGame ~= nil then
            Engine.ResumeGame()
        end
        if ability.damage_disabled_for_cutscene then
            set_player_damage_enabled(owner, true, "Ultimate cutscene start failed")
            ability.damage_disabled_for_cutscene = false
        end
        ability.cutscene_active = false
        return
    end

    if Engine ~= nil and Engine.PauseGame ~= nil then
        Engine.PauseGame()
        log("AirBowShot ultimate cutscene world paused")
    end

    log("AirBowShot ultimate cutscene requested: gauge=" .. tostring(get_ultimate_gauge(owner))
        .. "/" .. tostring(get_ultimate_gauge_max(owner))
        .. " launchLoc=" .. format_vec3(ability.cutscene_launch_location)
        .. " launchForward=" .. format_vec3(ability.cutscene_launch_forward))
end

local function tick_bow_ultimate_cutscene(owner, ability, dt)
    if ability == nil or not ability.cutscene_active then
        return false
    end

    if Engine ~= nil and Engine.IsPaused ~= nil and Engine.IsPaused() then
        return true
    end

    if HaruUltimateCutscene.IsActive() then
        HaruUltimateCutscene.Tick(dt)
        return true
    end

    return false
end

local function can_activate_bow_ultimate(owner, ability)
    if BOW_ULTIMATE_IGNORE_GAUGE_FOR_TEST then
        if ability ~= nil then
            ability.block_reason = nil
        end
        log("AirBowShot ultimate gauge bypass enabled for test: gauge="
            .. tostring(get_ultimate_gauge(owner)) .. "/" .. tostring(get_ultimate_gauge_max(owner)))
        return true
    end

    local ready = is_ultimate_ready(owner)
    if not ready then
        local gauge = get_ultimate_gauge(owner)
        local gauge_max = get_ultimate_gauge_max(owner)
        if ability ~= nil then
            ability.block_reason = string.format("ultimate gauge %.1f/%.1f", gauge, gauge_max)
        end
        log("AirBowShot blocked: ultimate gauge=" .. tostring(gauge) .. "/" .. tostring(gauge_max))
        return false
    end
    if ability ~= nil then
        ability.block_reason = nil
    end
    return true
end

local function activate_bow_aim(owner, ability)
    if owner == nil then
        log("AirBowShot activate failed: owner is nil")
        ability.active_remaining = 0.0
        return
    end

    local character_movement = get_character_movement(owner)
    ability.started_airborne = is_bow_airborne(character_movement)
    log("AirBowShot activate: startedAirborne=" .. tostring(ability.started_airborne)
        .. " " .. get_movement_debug_state(character_movement))

    ability.saved_gravity = ability.started_airborne and character_movement ~= nil and character_movement.GetGravity ~= nil and character_movement:GetGravity() or nil
    ability.saved_actor_rotation = owner.Rotation

    local arm = get_spring_arm(owner)
    if arm ~= nil then
        ability.saved_arm_length = arm.GetTargetArmLength ~= nil and arm:GetTargetArmLength() or nil
        ability.saved_socket_offset = arm.GetSocketOffset ~= nil and arm:GetSocketOffset() or nil
        ability.saved_inherit_pitch = arm.GetInheritPitch ~= nil and arm:GetInheritPitch() or nil
        ability.saved_inherit_yaw = arm.GetInheritYaw ~= nil and arm:GetInheritYaw() or nil
        ability.saved_inherit_roll = arm.GetInheritRoll ~= nil and arm:GetInheritRoll() or nil
        log("AirBowShot spring arm found: savedArmLength=" .. tostring(ability.saved_arm_length)
            .. " savedSocketOffset=" .. format_vec3(ability.saved_socket_offset)
            .. " savedInheritPitch=" .. tostring(ability.saved_inherit_pitch)
            .. " hasSetTargetArmLength=" .. tostring(arm.SetTargetArmLength ~= nil)
            .. " hasSetSocketOffset=" .. tostring(arm.SetSocketOffset ~= nil))
    else
        log("AirBowShot warning: SpringArmComponent not found")
    end

    local cam = get_camera(owner)
    if cam ~= nil then
        ability.saved_fov = cam.GetFOV ~= nil and cam:GetFOV() or nil
        ability.saved_camera_rotation = cam.GetRotation ~= nil and cam:GetRotation() or nil
        log("AirBowShot camera found: savedFov=" .. tostring(ability.saved_fov)
            .. " hasSetFOV=" .. tostring(cam.SetFOV ~= nil))
    else
        log("AirBowShot warning: CameraComponent not found")
    end

    stop_owner_movement(owner)
    lock_movement(owner, BOW_SKILL_NAME)
    set_weapon_mesh(owner, BOW_STATIC_MESH_PATH, "Bow", BOW_WEAPON_TRANSFORM)
    set_anim_bool(owner, ARROW_ANIM_VAR, true)
    face_owner_to_camera_yaw(owner, ability)
    set_bow_first_person_camera(owner, ability, true)
    update_bow_first_person_camera(owner, ability)
    ability.arrow_projectile = nil
    ability.arrow_released = false
    ability.cutscene_active = false
    ability.cutscene_finished = false
    ability.cutscene_elapsed = 0.0
    ability.cutscene_launch_location = nil
    ability.cutscene_launch_forward = nil
    ability.damage_disabled_for_cutscene = false

    if ability.started_airborne and character_movement ~= nil and character_movement.SetGravity ~= nil then
        character_movement:SetGravity(BOW_AIM_GRAVITY)
    elseif ability.started_airborne then
        log("AirBowShot warning: CharacterMovementComponent.SetGravity unavailable")
    else
        log("AirBowShot ground aim: gravity unchanged")
    end

    start_camera_blend(owner, {
        arm_length = BOW_CAMERA_ARM_LENGTH,
        socket_offset = BOW_CAMERA_SOCKET_OFFSET,
        inherit_pitch = false,
        fov = BOW_CAMERA_FOV
    }, BOW_CAMERA_BLEND_DURATION, "AirBowShot aim")
    set_bow_radial_blur(true, "RightMouseButton press")

    log("AirBowShot aim started: gravity=" .. tostring(BOW_AIM_GRAVITY)
        .. " armLength=" .. tostring(BOW_CAMERA_ARM_LENGTH)
        .. " socketOffset=" .. format_vec3(BOW_CAMERA_SOCKET_OFFSET)
        .. " fov=" .. tostring(BOW_CAMERA_FOV))

    update_bow_aim_particle(owner, ability)
    log("AirBowShot aim started; FireArrow AnimNotify will prepare the held arrow")
end

local function tick_bow_aim(owner, ability, dt)
    if tick_bow_ultimate_cutscene(owner, ability, dt) then
        return
    end

    face_owner_to_camera_yaw(owner, ability)
    update_bow_first_person_camera(owner, ability)

    if ability.arrow_projectile ~= nil and World ~= nil and World.UpdateCameraArrowProjectile ~= nil then
        World.UpdateCameraArrowProjectile(ability.arrow_projectile, BOW_PROJECTILE_OFFSET)
    end
    update_bow_aim_particle(owner, ability)

    local released_key = first_released_key(BOW_SKILL_KEYS)
    if released_key ~= nil then
        if ability.arrow_projectile == nil then
            log("AirBowShot release requested before FireArrow AnimNotify: preparing arrow immediately")
            prepare_held_arrow(owner, ability, "early release fallback")
        end

        stop_bow_aim_particle(ability, released_key .. " release")
        set_bow_radial_blur(false, released_key .. " release")
        start_bow_ultimate_cutscene(owner, ability)
    end
end

local function end_bow_aim(owner, ability)
    stop_bow_aim_particle(ability, "AirBowShot end")
    set_bow_radial_blur(false, "AirBowShot end")
    HaruUltimateCutscene.Stop()

    if ability ~= nil and ability.arrow_projectile ~= nil and not ability.arrow_released then
        if World ~= nil and World.ReleaseProjectile ~= nil then
            World.ReleaseProjectile(ability.arrow_projectile)
            log("AirBowShot held arrow released back to pool")
        end
        ability.arrow_projectile = nil
    end
    if ability ~= nil then
        if ability.damage_disabled_for_cutscene then
            set_player_damage_enabled(owner, true, "AirBowShot end")
        end
        ability.cutscene_active = false
        ability.cutscene_elapsed = 0.0
        ability.cutscene_launch_location = nil
        ability.cutscene_launch_forward = nil
        ability.damage_disabled_for_cutscene = false
        ability.block_reason = nil
    end

    restore_bow_aim(owner, ability)
    set_bow_first_person_camera(owner, ability, false)
    restore_bow_actor_rotation(owner, ability)
    if owner ~= nil then
        set_anim_bool(owner, ARROW_ANIM_VAR, false)
        set_weapon_mesh(owner, STAFF_STATIC_MESH_PATH, "Staff", STAFF_WEAPON_TRANSFORM)
        unlock_movement(owner, BOW_SKILL_NAME)
    end
    log("AirBowShot ended")
end

local function activate_spray_attack(owner, ability)
    if owner == nil then
        log("SprayAttack activate failed: owner is nil")
        ability.active_remaining = 0.0
        return
    end

    ability.locked_location = get_actor_location_safe(owner)
    ability.spray_target_actor = find_spray_target_actor()
    if ability.spray_target_actor ~= nil then
        start_spray_face_blend(owner, ability.spray_target_actor, ability)
    else
        log("SprayAttack warning: target actor not found name=" .. SPRAY_TARGET_ACTOR_NAME)
    end

    lock_movement_input_only(owner, ATTACK_SKILL_NAME)
    set_anim_bool(owner, ATTACK_ANIM_VAR, true)
    if World ~= nil and World.StartPlayerSprayAttack ~= nil then
        local started = World.StartPlayerSprayAttack(owner)
        log("SprayAttack started: " .. tostring(started))
        if not started then
            unlock_movement(owner, ATTACK_SKILL_NAME)
            set_anim_bool(owner, ATTACK_ANIM_VAR, false)
            ability.active_remaining = 0.0
        end
    else
        log("SprayAttack failed: World.StartPlayerSprayAttack unavailable")
        unlock_movement(owner, ATTACK_SKILL_NAME)
        set_anim_bool(owner, ATTACK_ANIM_VAR, false)
        ability.active_remaining = 0.0
    end
end

local function tick_spray_attack(owner, ability, dt)
    if owner ~= nil and ability ~= nil and ability.locked_location ~= nil then
        set_actor_horizontal_location_locked(owner, ability.locked_location)
    end

    if not tick_spray_face_blend(owner, ability, dt) then
        face_owner_to_camera_yaw(owner, ability)
    end

    local released_key = first_released_key(FIRE_PROJECTILE_KEYS)
    if released_key ~= nil then
        log("SprayAttack release: " .. released_key)
        ability_system:EndAbility(ability)
    end
end

local function end_spray_attack(owner, ability)
    if owner ~= nil then
        if World ~= nil and World.StopPlayerSprayAttack ~= nil then
            World.StopPlayerSprayAttack(owner)
        end
        set_anim_bool(owner, ATTACK_ANIM_VAR, false)
        unlock_movement(owner, ATTACK_SKILL_NAME)
    end
    if ability ~= nil then
        ability.locked_location = nil
        ability.spray_target_actor = nil
        ability.spray_face_blend = nil
    end
    log("SprayAttack ended")
end

local function activate_ultimate(owner, ability)
    if owner == nil then
        log("Ultimate activate failed: owner is nil")
        ability.active_remaining = 0.0
        return
    end

    -- ready 제거: Q 입력 시 바로 spell 모션 재생. ultimate_spell=true 로 (AnimGraph) UltSpell 진입.
    -- (AnyState/Idle→UltSpell 직접 전이는 AG_Haru 에서 사용자가 구성)
    -- 순서: 카메라 회전(오빗) → spell 애니 → 빔. Q 시점엔 카메라 오빗만 시작하고 애니/빔은 보류한다.
    ability.phase = "orbit"
    ability.phase_elapsed = 0.0  -- 현재 phase 경과(게임시간). orbit→spell 전환·빔 발사 타이밍 판정에 사용.
    ability.beam_fired = false   -- 이번 시전에서 빔이 이미 나갔는지(1회 시전 가드)
    ability.pre_ult_control = (owner.GetControlRotation ~= nil) and owner:GetControlRotation() or nil  -- #3: Q 누른 시점 시선(연출 후 복원용)
    -- death 상태에서 Q(마무리) → DeathAim 끄고 카메라 보스 고정 해제(이후 빔캠/마무리 연출이 카메라 우선).
    if boss_death_active and not boss_death_q_done then
        boss_death_q_done = true
        if UI ~= nil and UI.SetElementVisible ~= nil then
            UI.SetElementVisible("DeathAim", false)
        end
        log("Boss death: Q pressed -> DeathAim off, camera unlock")
    end
    lock_movement(owner, ULTIMATE_SKILL_NAME)
    -- Q 발동 시 사용자 설정 배율로 빔 크기를 미리 적용 (FireBeam 시 spawn 에 반영, 컴포넌트 없으면 자동 생성).
    if World ~= nil and World.SetPlayerBeamScale ~= nil then
        World.SetPlayerBeamScale(owner, ULTIMATE_BEAM_SCALE)
    end
    -- 카메라 오빗(회전)을 먼저 시작. hold_for_beam=true 이므로 오빗 종료 후 정면 anchor 에서 대기하고,
    -- tick_ultimate 가 오빗 종료를 감지해 애니(ultimate_spell)를 켜고, 애니 종료 직전에 빔을 발사한다.
    start_beam_camera(owner, ability.pre_ult_control)
    log("Ultimate orbit started (cam rotate -> anim -> beam): orbitDur=" .. tostring(ULT_CAM_ORBIT_DURATION)
        .. " spellDur=" .. tostring(ULTIMATE_SPELL_DURATION))
end

local function tick_ultimate(owner, ability, dt)
    ability.phase_elapsed = (ability.phase_elapsed or 0.0) + (dt or 0.0)

    -- Phase "orbit": 카메라 회전(오빗)이 끝나면 spell 애니를 켜고 "spell" phase 로 전환한다.
    -- (오빗 길이는 게임시간 ULT_CAM_ORBIT_DURATION. 슬로우모션 중에도 dt 가 게임시간이라 beam_cam 오빗과 동기화됨.)
    if ability.phase == "orbit" then
        if ability.phase_elapsed >= ULT_CAM_ORBIT_DURATION then
            set_anim_bool(owner, ULTIMATE_SPELL_ANIM_VAR, true)   -- 오빗 종료 후 spell 모션 on
            ability.phase = "spell"
            ability.phase_elapsed = 0.0
            log("Ultimate orbit finished -> spell anim started (spellDur=" .. tostring(ULTIMATE_SPELL_DURATION) .. ")")
        end
        return
    end

    -- Phase "spell": 애니가 끝나기 ULTIMATE_BEAM_LEAD_TIME(0.5초) 전에 빔(particle) 시전 — 1회만.
    -- 동시에 카메라 hold 를 풀어(정면 anchor→보스-락) 빔을 따라가도록 한다.
    if ability.phase == "spell" and not ability.beam_fired
        and ability.phase_elapsed >= (ULTIMATE_SPELL_DURATION - ULTIMATE_BEAM_LEAD_TIME) then
        if owner ~= nil and World ~= nil and World.StartPlayerBeamAttack ~= nil then
            -- 빔 방향 보정: 오빗 후 control rotation 이 캐릭터 정면(역방향)을 향하므로,
            -- 발사 직전에 actor Rotation + control rotation 을 boss 쪽으로 일시 정렬.
            -- tick_beam_camera 가 같은 프레임 내 control rotation 을 복원 → 카메라 연출 유지.
            local beam_fire_boss = (World.FindActorByName ~= nil) and World.FindActorByName(SPRAY_TARGET_ACTOR_NAME) or nil
            local beam_fire_aim = beam_fire_boss ~= nil and get_owner_to_actor_aim_rotation(owner, beam_fire_boss, "beam fire dir") or nil
            if beam_fire_aim ~= nil then
                pcall(function()
                    local cur = owner.Rotation
                    owner.Rotation = Vec3(cur ~= nil and (cur.X or 0.0) or 0.0, cur ~= nil and (cur.Y or 0.0) or 0.0, beam_fire_aim.yaw)
                end)
                if owner.SetControlRotation ~= nil then
                    pcall(function() owner:SetControlRotation(Vec3(0.0, beam_fire_aim.pitch, beam_fire_aim.yaw)) end)
                end
                log("Ultimate beam fire: actor+control yaw -> boss yaw=" .. tostring(beam_fire_aim.yaw))
            elseif beam_cam ~= nil then
                -- boss 못 찾으면 Q 시점 시선(base_yaw) 폴백
                pcall(function()
                    local cur = owner.Rotation
                    owner.Rotation = Vec3(cur ~= nil and (cur.X or 0.0) or 0.0, cur ~= nil and (cur.Y or 0.0) or 0.0, beam_cam.base_yaw)
                end)
                if owner.SetControlRotation ~= nil then
                    pcall(function() owner:SetControlRotation(Vec3(0.0, 0.0, beam_cam.base_yaw)) end)
                end
                log("Ultimate beam fire: boss not found, fallback base_yaw=" .. tostring(beam_cam.base_yaw))
            end
            -- 빔 방향을 명시적으로 보스로 지정 — 카메라 POV(캐릭터 얼굴을 비춤)로 발사되는 것을 막아
            -- 빔이 캐릭터 뒤가 아니라 보스로 진행하게 한다. BeamAttackComponent.ComputeAim 이 이 방향을 우선 사용.
            aim_beam_at_boss(owner, beam_fire_boss, beam_cam ~= nil and beam_cam.base_yaw or nil, "tick")
            World.StartPlayerBeamAttack(owner)
            if World.SetBeamDuration ~= nil then
                World.SetBeamDuration(owner, beam_cam_total_duration() + BEAM_CAM_DURATION_MARGIN + BEAM_CAM_START_DELAY)  -- 카메라 연출 내내 빔 유지
            end
            release_beam_camera_hold()   -- 오빗 후 정면에서 대기하던 카메라를 풀어 보스-락 단계로 진행
            log("Ultimate beam fired (" .. tostring(ULTIMATE_BEAM_LEAD_TIME) .. "s before spell ends)")
        end
        ability.beam_fired = true
    end
end

local function end_ultimate(owner, ability)
    if owner ~= nil then
        -- 안전망: tick 의 "애니 종료 0.5초 전" 시전 창을 프레임 hitch 등으로 놓쳤으면 종료 시점에라도 1회 시전.
        -- 카메라(beam_cam)는 activate 에서 이미 시작됐으므로 여기선 빔만 쏘고 hold 만 풀어준다(재시작 금지).
        if not ability.beam_fired and World ~= nil and World.StartPlayerBeamAttack ~= nil then
            -- 폴백 경로에서도 빔 방향 보정 (tick_ultimate 와 동일 로직)
            local beam_end_boss = (World.FindActorByName ~= nil) and World.FindActorByName(SPRAY_TARGET_ACTOR_NAME) or nil
            local beam_end_aim = beam_end_boss ~= nil and get_owner_to_actor_aim_rotation(owner, beam_end_boss, "fallback beam dir") or nil
            if beam_end_aim ~= nil then
                pcall(function()
                    local cur = owner.Rotation
                    owner.Rotation = Vec3(cur ~= nil and (cur.X or 0.0) or 0.0, cur ~= nil and (cur.Y or 0.0) or 0.0, beam_end_aim.yaw)
                end)
                if owner.SetControlRotation ~= nil then
                    pcall(function() owner:SetControlRotation(Vec3(0.0, beam_end_aim.pitch, beam_end_aim.yaw)) end)
                end
            elseif beam_cam ~= nil then
                pcall(function()
                    local cur = owner.Rotation
                    owner.Rotation = Vec3(cur ~= nil and (cur.X or 0.0) or 0.0, cur ~= nil and (cur.Y or 0.0) or 0.0, beam_cam.base_yaw)
                end)
                if owner.SetControlRotation ~= nil then
                    pcall(function() owner:SetControlRotation(Vec3(0.0, 0.0, beam_cam.base_yaw)) end)
                end
            end
            -- tick 경로와 동일하게 빔 방향을 명시적으로 보스로 지정(카메라 POV 발사 방지).
            aim_beam_at_boss(owner, beam_end_boss, beam_cam ~= nil and beam_cam.base_yaw or nil, "fallback")
            World.StartPlayerBeamAttack(owner)
            if World.SetBeamDuration ~= nil then
                World.SetBeamDuration(owner, beam_cam_total_duration() + BEAM_CAM_DURATION_MARGIN + BEAM_CAM_START_DELAY)
            end
            release_beam_camera_hold()   -- 폴백 경로에서도 카메라 보스-락 단계로 진행
            log("Ultimate beam fired (fallback at ult end)")
            ability.beam_fired = true
        end
        set_anim_bool(owner, ULTIMATE_SPELL_ANIM_VAR, false)   -- spell==false → ultimate_motion(state)→idle 복귀 (전이는 expected_false 여야 함)
        -- #4: 빔 카메라 연출이 진행 중이면 이동 잠금 해제를 연출 종료(tick_beam_camera)까지 미룬다.
        if beam_cam == nil then
            unlock_movement(owner, ULTIMATE_SKILL_NAME)
        end
    end
    ability.phase = nil
    ability.phase_elapsed = nil
    ability.beam_fired = nil
    log("Ultimate ended")
end

local function setup_abilities()
    local owner = resolve_actor()
    if owner == nil then
        log("setup failed: owner is nil")
        return
    end

    ability_system = AbilitySystem.new(owner)
    ability_system:RegisterAbility({
        Name = DASH_SKILL_NAME,
        Key = DASH_SKILL_KEY,
        Keys = DASH_SKILL_KEYS,
        Duration = DASH_DURATION,
        Cooldown = 0.0,
        BlockWhileAnyActive = true,
        OnActivate = activate_dash,
        OnEnd = end_dash
    })
    ability_system:RegisterAbility({
        Name = ROLL_SKILL_NAME,
        Key = ROLL_SKILL_KEY,
        Keys = ROLL_SKILL_KEYS,
        Duration = ROLL_DURATION,
        Cooldown = 0.0,
        BlockWhileAnyActive = true,
        OnActivate = activate_roll,
        OnEnd = end_roll
    })
    ability_system:RegisterAbility({
        Name = BOW_SKILL_NAME,
        Key = BOW_SKILL_KEY,
        Keys = BOW_SKILL_KEYS,
        Duration = BOW_AIM_MAX_DURATION,
        Cooldown = 0.0,
        BlockWhileAnyActive = true,
        CanActivate = can_activate_bow_ultimate,
        OnActivate = activate_bow_aim,
        OnTick = tick_bow_aim,
        OnEnd = end_bow_aim
    })
    ability_system:RegisterAbility({
        Name = ATTACK_SKILL_NAME,
        Key = FIRE_PROJECTILE_KEY,
        Keys = FIRE_PROJECTILE_KEYS,
        Duration = ATTACK_MAX_DURATION,
        Cooldown = 0.0,
        BlockWhileAnyActive = true,
        OnActivate = activate_spray_attack,
        OnTick = tick_spray_attack,
        OnEnd = end_spray_attack
    })
    ability_system:RegisterAbility({
        Name = ULTIMATE_SKILL_NAME,
        Key = ULTIMATE_SKILL_KEY,
        Keys = ULTIMATE_SKILL_KEYS,
        Duration = ULT_CAM_ORBIT_DURATION + ULTIMATE_SPELL_DURATION,   -- 오빗(카메라 회전)+spell 애니 길이. orbit→spell 동안 어빌리티 유지.
        Cooldown = 0.0,
        BlockWhileAnyActive = true,
        OnActivate = activate_ultimate,
        OnTick = tick_ultimate,
        OnEnd = end_ultimate
    })

    log("registered Dash on " .. join_keys(DASH_SKILL_KEYS))
    log("registered Roll on " .. join_keys(ROLL_SKILL_KEYS))
    log("registered AirBowShot on " .. join_keys(BOW_SKILL_KEYS))
    log("registered SprayAttack on " .. join_keys(FIRE_PROJECTILE_KEYS))
    log("registered Ultimate on " .. join_keys(ULTIMATE_SKILL_KEYS))

    -- 전역 빔 에셋 크기 적용 (공유 Beam.uasset template 의 굵기/길이 distribution 직접 수정 → 모든 빔에 반영).
    if World ~= nil and World.SetBeamTemplateSize ~= nil then
        World.SetBeamTemplateSize(BEAM_TEMPLATE_PATH, ULTIMATE_BEAM_WIDTH, ULTIMATE_BEAM_DISTANCE, ULTIMATE_BEAM_GROW_SPEED)  -- Speed>0: 빔 끝점이 시간에 따라 연장(asset loop off 전제)
        log("applied global beam template size: width=" .. tostring(ULTIMATE_BEAM_WIDTH)
            .. " distance=" .. tostring(ULTIMATE_BEAM_DISTANCE))
    end

    set_anim_bool(owner, DASH_ANIM_VAR, false)
    set_anim_bool(owner, ROLL_ANIM_VAR, false)
    set_anim_bool(owner, ATTACK_ANIM_VAR, false)
    set_anim_bool(owner, ARROW_ANIM_VAR, false)
    set_anim_bool(owner, ULTIMATE_SPELL_ANIM_VAR, false)
    set_weapon_mesh(owner, STAFF_STATIC_MESH_PATH, "Staff", STAFF_WEAPON_TRANSFORM)
    reset_dash_trail(owner)
end

function BeginPlay()
    if HaruUltimateCutscene.ResetUnpausedTickRegistration ~= nil then
        HaruUltimateCutscene.ResetUnpausedTickRegistration()
    end
    setup_abilities()
end

function EndPlay()
    if ability_system ~= nil then
        local ability = ability_system:GetAbility(BOW_SKILL_NAME)
        if ability ~= nil then
            ability_system:EndAbility(ability)
        end
        ability = ability_system:GetAbility(ATTACK_SKILL_NAME)
        if ability ~= nil then
            ability_system:EndAbility(ability)
        end
    end

    if actor ~= nil then
        stop_walking_audio()
        set_anim_bool(actor, DASH_ANIM_VAR, false)
        set_anim_bool(actor, ROLL_ANIM_VAR, false)
        set_anim_bool(actor, ATTACK_ANIM_VAR, false)
        set_anim_bool(actor, ARROW_ANIM_VAR, false)
        set_anim_bool(actor, ULTIMATE_SPELL_ANIM_VAR, false)
        set_weapon_mesh(actor, STAFF_STATIC_MESH_PATH, "Staff", STAFF_WEAPON_TRANSFORM)
        movement_locks = {}
        set_owner_movement_blocked(actor, false)
        reset_dash_trail(actor)
    end

    -- 오빗 슬로우 모션 도중 레벨이 끝나면 전역 시간 배율이 남지 않도록 정상 속도로 복원.
    if beam_cam ~= nil and beam_cam.slomo_active then
        set_ultimate_time_dilation(1.0, "EndPlay slomo restore")
    end

    actor = nil
    ability_system = nil
    movement = nil
    anim_instance = nil
    dash_trail_particle = nil
    spring_arm = nil
    camera = nil
    weapon_mesh = nil
    camera_blend = nil
    beam_cam = nil
    walking_audio_playing = false
end

function OnOverlap(OtherActor)
end

function OnAnimNotify(name)
    log("AnimNotify received: " .. tostring(name))
end

function OnAnimNotify_FireArrow()
    log("AnimNotify_FireArrow received")
    if ability_system == nil then
        log("AirBowShot notify ignored: ability system unavailable")
        return
    end

    local ability = ability_system:GetAbility(BOW_SKILL_NAME)
    if ability == nil or not ability.is_active then
        log("AirBowShot notify ignored: ability is not active")
        return
    end

    local owner = resolve_actor()
    prepare_held_arrow(owner, ability, "FireArrow AnimNotify")
end

function Tick(dt)
    if ability_system == nil then
        setup_abilities()
    end

    if ability_system == nil then
        return
    end

    local owner = resolve_actor()
    handle_demo_kill_inputs(owner)
    update_walking_audio(owner)

    local dash_key = first_pressed_key(DASH_SKILL_KEYS)
    if dash_key ~= nil then
        log("input pressed: " .. dash_key)
        local activated, reason = ability_system:TryActivateByKey(dash_key)
        if not activated then
            log("Dash blocked: " .. (reason or "unknown"))
        end
    end

    local roll_key = first_pressed_key(ROLL_SKILL_KEYS)
    if roll_key ~= nil then
        log("input pressed: " .. roll_key)
        local activated, reason = ability_system:TryActivateByKey(roll_key)
        if not activated then
            log("Roll blocked: " .. (reason or "unknown"))
        end
    end

    local bow_key = first_pressed_key(BOW_SKILL_KEYS)
    if bow_key ~= nil then
        log("input pressed: " .. bow_key)
        local activated, reason = ability_system:TryActivateByKey(bow_key)
        if not activated then
            log("AirBowShot blocked: " .. (reason or "unknown"))
        end
    end

    local fire_key = first_pressed_key(FIRE_PROJECTILE_KEYS)
    if fire_key ~= nil then
        log("input pressed: " .. fire_key)
        local activated, reason = ability_system:TryActivateByKey(fire_key)
        if not activated then
            log("SprayAttack blocked: " .. (reason or "unknown"))
        end
    end

    local ultimate_key = first_pressed_key(ULTIMATE_SKILL_KEYS)
    if ultimate_key ~= nil then
        log("input pressed: " .. ultimate_key)
        local activated, reason = ability_system:TryActivateByKey(ultimate_key)
        if not activated then
            log("Ultimate blocked: " .. (reason or "unknown"))
        end
    end

    ability_system:Tick(dt)
    tick_camera_blend(dt)
    tick_beam_camera(dt)
    update_boss_death_sequence(owner)   -- 보스 HP<=1 감지 → UI 교체 + 카메라 보스 고정(빔캠 비활성 시).
end
