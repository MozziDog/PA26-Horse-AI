-- Rider AnimGraph variables
--   bMounted          : true while the rider is constrained to a horse.
--   MountedGait       : 0=Idle, 1=Walk, 2=Trot, 3=Canter, 4=Gallop.
--   bWalkingUnmounted : existing unmounted walk-state variable.

local ANIMKEY_MOUNTED = "bMounted"
local ANIMKEY_MOUNTED_GAIT = "MountedGait"
local ANIMKEY_UNMOUNTED_WALK = "bWalkingUnmounted"

local MIN_WALKSPEED = 0.01

-- EHorseGait의 값을 AnimVariable 값으로 변환
-- 한쪽 수정했을 때 다른쪽까지 수정할 필요 없도록 EHorseGait 그대로 사용 X
local HORSE_GAIT_WALK = 2
local HORSE_GAIT_TROT = 3
local HORSE_GAIT_CANTER = 4
local HORSE_GAIT_GALLOP = 5

local MOUNTED_GAIT_IDLE = 0
local MOUNTED_GAIT_WALK = 1
local MOUNTED_GAIT_TROT = 2
local MOUNTED_GAIT_CANTER = 3
local MOUNTED_GAIT_GALLOP = 4

local actor = nil
local movement = nil
local anim_instance = nil
local missing_ANIMKEY_MOUNTED_GAIT_logged = false

local function log(message)
    print("[Rider] " .. message)
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

local function find_component_by_class(class_name)
    local owner = resolve_actor()
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

local function refresh_components()
    local owner = resolve_actor()
    if owner == nil then
        return
    end

    if movement == nil then
        if owner.GetCharacterMovement ~= nil then
            movement = owner:GetCharacterMovement()
        end

        if movement == nil then
            movement = find_component_by_class("UCharacterMovementComponent")
        end
    end

    if anim_instance == nil then
        local mesh = nil
        if owner.GetMesh ~= nil then
            mesh = owner:GetMesh()
        end

        if mesh == nil and owner.GetSkeletalMeshComponent ~= nil then
            mesh = owner:GetSkeletalMeshComponent()
        end

        if mesh == nil then
            mesh = find_component_by_class("USkeletalMeshComponent")
        end

        if mesh ~= nil then
            anim_instance = mesh:GetAnimInstance()
        end
    end
end

local function call_reflected(target, signature)
    if target == nil or Reflection == nil or Reflection.CallSignature == nil then
        return nil
    end

    return Reflection.CallSignature(target, signature)
end

local function get_mounted_horse(owner)
    return call_reflected(owner, "GetMountedHorse() const")
end

local function get_mounted_state(owner)
    return call_reflected(owner, "IsMounted() const") == true
end

local function to_mounted_gait(horse_gait)
    if horse_gait == HORSE_GAIT_WALK then
        return MOUNTED_GAIT_WALK
    elseif horse_gait == HORSE_GAIT_TROT then
        return MOUNTED_GAIT_TROT
    elseif horse_gait == HORSE_GAIT_CANTER then
        return MOUNTED_GAIT_CANTER
    elseif horse_gait == HORSE_GAIT_GALLOP then
        return MOUNTED_GAIT_GALLOP
    end

    return MOUNTED_GAIT_IDLE
end

local function get_mounted_gait(owner, is_mounted)
    if not is_mounted then
        return MOUNTED_GAIT_IDLE
    end

    local horse = get_mounted_horse(owner)
    if horse == nil then
        return MOUNTED_GAIT_IDLE
    end

    local horse_gait = call_reflected(horse, "GetCurrentGait() const")
    return to_mounted_gait(horse_gait)
end

local function is_walking_unmounted(is_mounted)
    if is_mounted or movement == nil then
        return false
    end

    if movement.GetSpeed ~= nil then
        return movement:GetSpeed() > MIN_WALKSPEED
    end

    if movement.GetVelocity ~= nil then
        local velocity = movement:GetVelocity()
        if velocity ~= nil then
            return velocity.X * velocity.X + velocity.Y * velocity.Y > MIN_WALKSPEED * MIN_WALKSPEED
        end
    end

    return false
end

local function update_anim_graph()
    local owner = resolve_actor()
    if owner == nil or anim_instance == nil then
        return
    end

    local is_mounted = get_mounted_state(owner)
    anim_instance:SetGraphVariableBool(ANIMKEY_MOUNTED, is_mounted)
    anim_instance:SetGraphVariableBool(ANIMKEY_UNMOUNTED_WALK, is_walking_unmounted(is_mounted))

    local gait_was_set = anim_instance:SetGraphVariableInt(ANIMKEY_MOUNTED_GAIT, get_mounted_gait(owner, is_mounted))
    if not gait_was_set and not missing_ANIMKEY_MOUNTED_GAIT_logged then
        missing_ANIMKEY_MOUNTED_GAIT_logged = true
        log("AnimGraph variable not found: " .. ANIMKEY_MOUNTED_GAIT)
    end
end

function BeginPlay()
    refresh_components()
    update_anim_graph()
end

function EndPlay()
    actor = nil
    movement = nil
    anim_instance = nil
    missing_ANIMKEY_MOUNTED_GAIT_logged = false
end

function Tick(dt)
    refresh_components()
    update_anim_graph()
end
