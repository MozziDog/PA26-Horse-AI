local HaruUltimateCutscene = {}

local EYE_DURATION = 2.5
local DRAW_DURATION = 3.0
local LETTERBOX_AMOUNT = 1.0
local LETTERBOX_THICKNESS = 0.18
local BLACK = { X = 0.0, Y = 0.0, Z = 0.0, W = 1.0 }
local HIDDEN_DURING_CUTSCENE_UI_ASSETS = {
    ["Content/UI/character_magical_aim.uasset"] = true,
    ["Content/UI/BossHealthBar.uasset"] = true,
    ["Content/UI/Character_Health_Hud.uasset"] = true,
    ["Content/UI/Character_Guage_Hud.uasset"] = true,
    ["Content/UI/Character_Gauge_Hud.uasset"] = true
}

local EYE_CAMERA_START = {
    arm_length = 0.55,
    socket_offset = Vec3(4.0, -0.45, -0.49),
    inherit_pitch = false,
    inherit_yaw = false,
    inherit_roll = false,
    camera_rotation = Vec3(0.0, -1.0, -180.0),
    fov = 0.22
}

local EYE_CAMERA_END = {
    arm_length = 0.55,
    socket_offset = Vec3(4.0, -0.48, -0.49),
    inherit_pitch = false,
    inherit_yaw = false,
    inherit_roll = false,
    camera_rotation = Vec3(0.0, -1.0, -180.0),
    fov = 0.22
}

local DRAW_CAMERA = {
    arm_length = 3.25,
    socket_offset = Vec3(8.0, 1.82, -0.3),
    inherit_pitch = false,
    inherit_yaw = false,
    inherit_roll = false,
    camera_rotation = Vec3(0.0, 0.0, 0.0),
    fov = 0.62
}

local state = {
    active = false
}
local unpaused_tick_registered = false

local function ensure_unpaused_tick()
    if unpaused_tick_registered then
        return true
    end
    if Engine == nil or Engine.SetOnUnpausedTick == nil then
        return false
    end

    Engine.SetOnUnpausedTick(function(dt)
        if state.active == true then
            HaruUltimateCutscene.Tick(dt)
        end
    end)
    unpaused_tick_registered = true
    return true
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

local function ease_in_cubic(value)
    local t = clamp01(value)
    return t * t * t * t
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

local function get_camera_state(spring_arm, camera)
    return {
        arm_length = spring_arm ~= nil and spring_arm.GetTargetArmLength ~= nil and spring_arm:GetTargetArmLength() or nil,
        socket_offset = spring_arm ~= nil and spring_arm.GetSocketOffset ~= nil and spring_arm:GetSocketOffset() or nil,
        inherit_pitch = spring_arm ~= nil and spring_arm.GetInheritPitch ~= nil and spring_arm:GetInheritPitch() or nil,
        inherit_yaw = spring_arm ~= nil and spring_arm.GetInheritYaw ~= nil and spring_arm:GetInheritYaw() or nil,
        inherit_roll = spring_arm ~= nil and spring_arm.GetInheritRoll ~= nil and spring_arm:GetInheritRoll() or nil,
        camera_relative_location = camera ~= nil and camera.RelativeLocation or nil,
        camera_rotation = camera ~= nil and camera.GetRotation ~= nil and camera:GetRotation() or nil,
        fov = camera ~= nil and camera.GetFOV ~= nil and camera:GetFOV() or nil
    }
end

local function get_direct_camera_relative_location(camera_state)
    if camera_state == nil then
        return nil
    end

    local arm_length = camera_state.arm_length or 0.0
    local socket_offset = camera_state.socket_offset or Vec3(0.0, 0.0, 0.0)
    return Vec3(
        (socket_offset.X or 0.0) - arm_length,
        socket_offset.Y or 0.0,
        socket_offset.Z or 0.0
    )
end

local function apply_saved_camera_state(spring_arm, camera, camera_state)
    if camera_state == nil then
        return
    end

    if spring_arm ~= nil then
        if camera_state.arm_length ~= nil and spring_arm.SetTargetArmLength ~= nil then
            spring_arm:SetTargetArmLength(camera_state.arm_length)
        end
        if camera_state.socket_offset ~= nil and spring_arm.SetSocketOffset ~= nil then
            spring_arm:SetSocketOffset(camera_state.socket_offset)
        end
        if camera_state.inherit_pitch ~= nil and spring_arm.SetInheritPitch ~= nil then
            spring_arm:SetInheritPitch(camera_state.inherit_pitch)
        end
        if camera_state.inherit_yaw ~= nil and spring_arm.SetInheritYaw ~= nil then
            spring_arm:SetInheritYaw(camera_state.inherit_yaw)
        end
        if camera_state.inherit_roll ~= nil and spring_arm.SetInheritRoll ~= nil then
            spring_arm:SetInheritRoll(camera_state.inherit_roll)
        end
        if spring_arm.ResetLagState ~= nil then
            spring_arm:ResetLagState()
        end
    end

    if camera ~= nil and camera_state.camera_relative_location ~= nil then
        camera.RelativeLocation = camera_state.camera_relative_location
    end
    if camera ~= nil and camera_state.camera_rotation ~= nil and camera.SetRotation ~= nil then
        camera:SetRotation(camera_state.camera_rotation)
    end
    if camera ~= nil and camera_state.fov ~= nil and camera.SetFOV ~= nil then
        camera:SetFOV(camera_state.fov)
    end
end

local function apply_cutscene_camera_state(camera, camera_state)
    if camera == nil or camera_state == nil then
        return
    end

    local relative_location = get_direct_camera_relative_location(camera_state)
    if relative_location ~= nil then
        camera.RelativeLocation = relative_location
    end
    if camera_state.camera_rotation ~= nil and camera.SetRotation ~= nil then
        camera:SetRotation(camera_state.camera_rotation)
    end
    if camera_state.fov ~= nil and camera.SetFOV ~= nil then
        camera:SetFOV(camera_state.fov)
    end
end

local function lerp_camera_state(from_state, to_state, alpha)
    return {
        arm_length = lerp_number(from_state ~= nil and from_state.arm_length or nil, to_state.arm_length, alpha),
        socket_offset = lerp_vec3(from_state ~= nil and from_state.socket_offset or nil, to_state.socket_offset, alpha),
        inherit_pitch = to_state.inherit_pitch,
        inherit_yaw = to_state.inherit_yaw,
        inherit_roll = to_state.inherit_roll,
        camera_rotation = lerp_vec3(from_state ~= nil and from_state.camera_rotation or nil, to_state.camera_rotation, alpha),
        fov = lerp_number(from_state ~= nil and from_state.fov or nil, to_state.fov, alpha)
    }
end

local function set_postprocess(camera, enabled)
    if camera ~= nil then
        if enabled and camera.SetLetterbox ~= nil then
            camera:SetLetterbox(true, LETTERBOX_AMOUNT, LETTERBOX_THICKNESS, BLACK)
        elseif not enabled and camera.ClearLetterbox ~= nil then
            camera:ClearLetterbox()
        elseif not enabled and camera.SetLetterbox ~= nil then
            camera:SetLetterbox(false, 0.0, 0.0)
        end
    end
end

local function get_ui_asset_path(actor)
    if actor == nil or actor.GetProperty == nil then
        return nil
    end

    local value = actor:GetProperty("UIAssetPath")
    return value ~= nil and tostring(value) or nil
end

local function set_canvas_actor_visible(actor, visible)
    if actor == nil then
        return
    end

    if actor.SetVisible ~= nil then
        actor:SetVisible(visible)
    end

    if Reflection ~= nil then
        Reflection.SetProperty(actor, "PendingActorVisible", visible)
    end

    if actor.GetRootComponent ~= nil and Reflection ~= nil then
        local root = actor:GetRootComponent()
        if root ~= nil then
            Reflection.SetProperty(root, "bVisible", visible)
        end
    end
end

local function set_default_hud_visible(visible)
    if World == nil or World.FindActorsByClass == nil then
        return
    end

    local actors = World.FindActorsByClass("AUICanvasActor")
    for _, actor in pairs(actors) do
        local path = get_ui_asset_path(actor)
        if path ~= nil and HIDDEN_DURING_CUTSCENE_UI_ASSETS[path] then
            set_canvas_actor_visible(actor, visible)
        end
    end
end

local function log(message)
    if state.log ~= nil then
        state.log(message)
    else
        print("[HaruUltimateCutscene] " .. message)
    end
end

function HaruUltimateCutscene.IsActive()
    return state.active == true
end

function HaruUltimateCutscene.ResetUnpausedTickRegistration()
    unpaused_tick_registered = false
end

function HaruUltimateCutscene.Start(config)
    if config == nil or state.active then
        return false
    end
    if not ensure_unpaused_tick() then
        log("start failed: Engine.SetOnUnpausedTick unavailable")
        return false
    end

    state = {
        active = true,
        elapsed = 0.0,
        spring_arm = config.spring_arm,
        camera = config.camera,
        on_finish = config.on_finish,
        on_tick_lock = config.on_tick_lock,
        on_tick_projectile = config.on_tick_projectile,
        log = config.log,
        next_tick_log_time = 1.0,
        start_camera_state = get_camera_state(config.spring_arm, config.camera)
    }

    set_postprocess(state.camera, true)
    set_default_hud_visible(false)
    apply_cutscene_camera_state(state.camera, EYE_CAMERA_START)
    Audio.Play("CutScene", 1.0)
    log("started")
    return true
end

function HaruUltimateCutscene.Stop()
    if not state.active then
        set_postprocess(state.camera, false)
        set_default_hud_visible(true)
        if Engine ~= nil and Engine.ResumeGame ~= nil then
            Engine.ResumeGame()
        end
        return
    end

    set_postprocess(state.camera, false)
    set_default_hud_visible(true)
    apply_saved_camera_state(state.spring_arm, state.camera, state.start_camera_state)
    state.active = false
    if Engine ~= nil and Engine.ResumeGame ~= nil then
        Engine.ResumeGame()
    end
    log("stopped")
end

function HaruUltimateCutscene.Tick(dt)
    if not state.active then
        return false
    end

    state.elapsed = (state.elapsed or 0.0) + (dt or 0.0)
    local elapsed = state.elapsed
    if state.next_tick_log_time ~= nil and elapsed >= state.next_tick_log_time then
        log("tick elapsed=" .. tostring(elapsed))
        state.next_tick_log_time = state.next_tick_log_time + 1.0
    end

    if elapsed <= EYE_DURATION then
        local alpha = smoothstep(elapsed / EYE_DURATION)
        apply_cutscene_camera_state(state.camera, lerp_camera_state(EYE_CAMERA_START, EYE_CAMERA_END, alpha))
    elseif elapsed <= EYE_DURATION + DRAW_DURATION then
        local alpha = ease_in_cubic((elapsed - EYE_DURATION) / DRAW_DURATION)
        apply_cutscene_camera_state(state.camera, lerp_camera_state(EYE_CAMERA_END, DRAW_CAMERA, alpha))
    else
        local on_finish = state.on_finish
        HaruUltimateCutscene.Stop()
        if on_finish ~= nil then
            on_finish()
        end
        return true
    end

    if state.on_tick_lock ~= nil then
        state.on_tick_lock()
    end
    if state.on_tick_projectile ~= nil then
        state.on_tick_projectile()
    end

    return true
end

return HaruUltimateCutscene
