local IntroCutScene = {}

local BOSS_ACTOR_NAME = "Boss"
local CAMERA_ACTOR_NAME = "IntroCutSceneCamera"
local CHARACTER_CAMERA_ACTOR_NAME = "Haru"
local INTRO_UI_ACTOR_NAME = "IntroNameCanvas"
local HIDDEN_DURING_CUTSCENE_UI_ASSETS = {
    ["Content/UI/character_magical_aim.uasset"] = true,
    ["Content/UI/BossHealthBar.uasset"] = true,
    ["Content/UI/Character_Health_Hud.uasset"] = true,
    ["Content/UI/Character_Guage_Hud.uasset"] = true,
    ["Content/UI/Character_Gauge_Hud.uasset"] = true
}

local START_DELAY = 0.25
local DURATION = 10.0
local LETTERBOX_AMOUNT = 1.0
local LETTERBOX_THICKNESS = 0.16
local BLACK = { X = 0.0, Y = 0.0, Z = 0.0, W = 1.0 }

local state = {
    active = false,
    finished = false,
    pending_start = false,
    start_delay = 0.0
}

local function log(message)
    print("[IntroCutScene] " .. tostring(message))
end

local function find_camera_component(actor)
    if actor == nil then
        return nil
    end
    if actor.GetCameraComponent ~= nil then
        local camera = actor:GetCameraComponent()
        if camera ~= nil then
            return camera
        end
    end
    if actor.GetComponents == nil then
        return nil
    end

    local components = actor:GetComponents()
    for _, component in pairs(components) do
        if component ~= nil and component.IsA ~= nil and component:IsA("UCameraComponent") then
            return component
        end
    end

    return nil
end

local function get_cutscene_camera_actor()
    if World == nil or World.FindActorByName == nil then
        return nil
    end

    local intro_camera = World.FindActorByName(CAMERA_ACTOR_NAME)
    if intro_camera ~= nil and find_camera_component(intro_camera) ~= nil then
        return intro_camera
    end
    return nil
end

local function get_character_camera()
    if World == nil or World.FindActorByName == nil then
        return nil
    end

    return find_camera_component(World.FindActorByName(CHARACTER_CAMERA_ACTOR_NAME))
end

local function get_intro_ui_actor()
    if World == nil or World.FindActorByName == nil then
        return nil
    end

    return World.FindActorByName(INTRO_UI_ACTOR_NAME)
end

local function set_intro_ui_visible(visible)
    local actor = state.intro_ui_actor or get_intro_ui_actor()
    if actor == nil then
        return false
    end

    state.intro_ui_actor = actor

    if actor.SetVisible ~= nil then
        actor:SetVisible(visible)
    end

    if Reflection ~= nil then
        Reflection.SetProperty(actor, "PendingActorVisible", visible)

        if actor.GetRootComponent ~= nil then
            local root = actor:GetRootComponent()
            if root ~= nil then
                Reflection.SetProperty(root, "bVisible", visible)
            end
        end
    end

    return true
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

local function set_camera_postprocess(camera, enabled)
    if camera == nil then
        return
    end

    if enabled and camera.SetLetterbox ~= nil then
        camera:SetLetterbox(true, LETTERBOX_AMOUNT, LETTERBOX_THICKNESS, BLACK)
    elseif not enabled and camera.ClearLetterbox ~= nil then
        camera:ClearLetterbox()
    elseif not enabled and camera.SetLetterbox ~= nil then
        camera:SetLetterbox(false, 0.0, 0.0)
    end
end

local function restore_camera()
    set_camera_postprocess(state.camera, false)

    if CameraManager ~= nil then
        local restore_camera_target = state.character_camera or state.previous_possessed_camera or state.previous_active_camera
        if restore_camera_target ~= nil and CameraManager.PossessCamera ~= nil then
            CameraManager.PossessCamera(restore_camera_target)
        elseif state.previous_active_camera ~= nil and CameraManager.SetActiveCameraWithBlend ~= nil then
            CameraManager.SetActiveCameraWithBlend(state.previous_active_camera, 0.0)
        end
    end
end

function IntroCutScene.IsActive()
    return state.active == true
end

function IntroCutScene.ResetUnpausedTickRegistration()
end

function IntroCutScene.Start()
    if state.active or state.finished then
        return false
    end
    if World == nil or World.FindActorByName == nil then
        log("start failed: World.FindActorByName unavailable")
        return false
    end

    local boss = World.FindActorByName(BOSS_ACTOR_NAME)
    if boss == nil then
        log("start failed: Boss actor not found")
        return false
    end

    local camera_actor = get_cutscene_camera_actor()
    local camera = find_camera_component(camera_actor)
    if camera_actor == nil or camera == nil then
        log("start failed: IntroCutSceneCamera not found")
        return false
    end

    local character_camera = get_character_camera()
    if character_camera == nil then
        log("start failed: character camera not found")
        return false
    end

    local intro_ui_actor = get_intro_ui_actor()
    if intro_ui_actor == nil then
        log("start failed: IntroNameCanvas not found")
        return false
    end

    local previous_active_camera = CameraManager ~= nil and CameraManager.GetActiveCamera ~= nil and CameraManager.GetActiveCamera() or nil
    local previous_possessed_camera = CameraManager ~= nil and CameraManager.GetPossessedCamera ~= nil and CameraManager.GetPossessedCamera() or nil

    state = {
        active = true,
        finished = false,
        elapsed = 0.0,
        boss = boss,
        camera_actor = camera_actor,
        camera = camera,
        character_camera = character_camera,
        intro_ui_actor = intro_ui_actor,
        previous_active_camera = previous_active_camera,
        previous_possessed_camera = previous_possessed_camera
    }

    if CameraManager ~= nil and CameraManager.PossessCamera ~= nil then
        CameraManager.PossessCamera(camera)
    elseif CameraManager ~= nil and CameraManager.SetActiveCameraWithBlend ~= nil then
        CameraManager.SetActiveCameraWithBlend(camera, 0.0)
    end

    set_default_hud_visible(false)
    set_intro_ui_visible(true)
    set_camera_postprocess(camera, true)
    log("started")
    return true
end

function IntroCutScene.Stop()
    if state.active then
        set_intro_ui_visible(false)
        set_default_hud_visible(true)
        restore_camera()
    end

    state.active = false
    state.finished = true

    log("stopped")
end

function IntroCutScene.Tick(dt)
    if not state.active then
        return false
    end

    state.elapsed = (state.elapsed or 0.0) + (dt or 0.0)
    if state.elapsed >= DURATION then
        IntroCutScene.Stop()
        return true
    end

    return true
end

function BeginPlay()
    set_intro_ui_visible(false)
    state.pending_start = true
    state.start_delay = START_DELAY
end

function EndPlay()
    IntroCutScene.Stop()
end

function Tick(dt)
    if state.pending_start and not state.active and not state.finished then
        set_intro_ui_visible(false)
        state.start_delay = state.start_delay - (dt or 0.0)
        if state.start_delay <= 0.0 then
            if IntroCutScene.Start() then
                state.pending_start = false
            else
                state.start_delay = 0.25
            end
        end
    end

    if state.active then
        IntroCutScene.Tick(dt)
    end
end

return IntroCutScene
