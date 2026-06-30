local SPEED_VAR = "Speed"
local IS_GROUND_VAR = "IsGround"
local IS_FALLING_VAR = "IsFalling"
local IS_DEAD_VAR = "IsDead"

local TRACE_DISTANCE = 2.0
local GROUNDED_CENTER_DISTANCE = 1.15
local VERTICAL_EPSILON = 0.05
local DEBUG_DEATH_ANIM = true

local movement = nil
local anim_instance = nil
local actor = nil
local previous_location = nil
local last_is_falling = false
local last_is_dead = nil
local missing_health_api_logged = false
local missing_is_dead_var_logged = false

local function log(message)
    print("[Haru] " .. message)
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

-- Component 캐싱
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

-- Ground Raycast
local function get_ground_state()
    local owner = resolve_actor()
    if owner == nil then
        return false, TRACE_DISTANCE
    end

    local start = owner.Location
    if start == nil then
        return false, TRACE_DISTANCE
    end

    local finish = Vec3(start.X, start.Y, start.Z - TRACE_DISTANCE)
    local hit = World.LineTrace(start, finish, owner)

    if hit ~= nil and hit.Hit then
        local distance = hit.Distance or TRACE_DISTANCE
        return distance <= GROUNDED_CENTER_DISTANCE, distance
    end

    return false, TRACE_DISTANCE
end

-- Animation
local function resolve_health_owner(owner)
    if owner ~= nil and owner.GetCurrentHealth ~= nil then
        return owner
    end

    if owner ~= nil and owner.AsPawn ~= nil then
        local pawn = owner:AsPawn()
        if pawn ~= nil and pawn.GetCurrentHealth ~= nil then
            return pawn
        end
    end

    return nil
end

local function is_owner_dead(owner)
    local health_owner = resolve_health_owner(owner)
    if health_owner == nil then
        if DEBUG_DEATH_ANIM and not missing_health_api_logged then
            missing_health_api_logged = true
            if owner == nil then
                log("death anim debug: owner unavailable")
            else
                local class_name = owner.GetClassName ~= nil and owner:GetClassName() or "unknown"
                log("death anim debug: GetCurrentHealth unavailable ownerClass=" .. tostring(class_name))
            end
        end
        return false
    end

    local health = health_owner:GetCurrentHealth()
    return health ~= nil and health <= 0.0
end

local function update_anim_graph(speed, is_ground, is_falling, is_dead)
    if anim_instance == nil then
        return
    end

    anim_instance:SetGraphVariableFloat(SPEED_VAR, speed)
    anim_instance:SetGraphVariableBool(IS_GROUND_VAR, is_ground)
    anim_instance:SetGraphVariableBool(IS_FALLING_VAR, is_falling)
    local is_dead_set = anim_instance:SetGraphVariableBool(IS_DEAD_VAR, is_dead)

    if DEBUG_DEATH_ANIM then
        if not is_dead_set and not missing_is_dead_var_logged then
            missing_is_dead_var_logged = true
            log("death anim debug: AnimGraph variable not found: " .. IS_DEAD_VAR)
        end

        if last_is_dead ~= is_dead then
            local owner = resolve_actor()
            local health = "nil"
            local health_owner = resolve_health_owner(owner)
            if health_owner ~= nil then
                health = tostring(health_owner:GetCurrentHealth())
            end
            log("death anim debug: health=" .. health
                .. " isDead=" .. tostring(is_dead)
                .. " setResult=" .. tostring(is_dead_set))
            last_is_dead = is_dead
        end
    end
end

function BeginPlay()
    refresh_components()
    local owner = resolve_actor()
    if owner ~= nil then
        previous_location = owner.Location
    end
    update_anim_graph(0.0, false, false, is_owner_dead(owner))
end

function EndPlay()
    movement = nil
    anim_instance = nil
    actor = nil
    previous_location = nil
    last_is_dead = nil
    missing_health_api_logged = false
    missing_is_dead_var_logged = false
end

function OnOverlap(OtherActor)
end

function Tick(dt)
    refresh_components()

    local owner = resolve_actor()
    local current_location = nil
    if owner ~= nil then
        current_location = owner.Location
    end

    -- velocity
    local speed = 0.0
    local vertical_velocity = 0.0

    if movement ~= nil and movement.GetVelocity ~= nil then
        local velocity = movement:GetVelocity()
        if velocity ~= nil then
            speed = math.sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y)
            vertical_velocity = velocity.Z
        elseif movement.GetSpeed ~= nil then
            speed = movement:GetSpeed()
        end
    elseif movement ~= nil and movement.GetSpeed ~= nil then
        speed = movement:GetSpeed()
    end

    if current_location ~= nil and previous_location ~= nil and dt ~= nil and dt > 0.000001 then
        local dx = current_location.X - previous_location.X
        local dy = current_location.Y - previous_location.Y
        local dz = current_location.Z - previous_location.Z

        if speed <= 0.000001 then
            speed = math.sqrt(dx * dx + dy * dy) / dt
        end

        if math.abs(vertical_velocity) <= 0.000001 then
            vertical_velocity = dz / dt
        end
    end

    -- 지면 체크
    local is_ground = get_ground_state()
    local is_falling = last_is_falling

    if is_ground then
        is_falling = false
    elseif vertical_velocity < -VERTICAL_EPSILON then
        is_falling = true
    elseif vertical_velocity > VERTICAL_EPSILON then
        is_falling = false
    elseif movement ~= nil and movement.IsFalling ~= nil then
        is_falling = movement:IsFalling()
    end

    last_is_falling = is_falling
    previous_location = current_location
    update_anim_graph(speed, is_ground, is_falling, is_owner_dead(owner))
end
