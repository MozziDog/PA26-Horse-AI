local AbilitySystem = {}

local function clamp_zero(value)
    if value == nil or value < 0.0 then
        return 0.0
    end
    return value
end

function AbilitySystem.new(owner)
    local system = {
        owner = owner,
        abilities = {},
        abilities_by_key = {}
    }

    function system:RegisterAbility(config)
        if config == nil or config.Name == nil or config.Key == nil then
            return nil
        end

        local ability = {
            name = config.Name,
            key = config.Key,
            duration = config.Duration or 0.0,
            cooldown = config.Cooldown or 0.0,
            block_while_any_active = config.BlockWhileAnyActive or false,
            active_remaining = 0.0,
            cooldown_remaining = 0.0,
            is_active = false,
            can_activate = config.CanActivate,
            on_activate = config.OnActivate,
            on_tick = config.OnTick,
            on_end = config.OnEnd
        }

        self.abilities[ability.name] = ability
        self.abilities_by_key[ability.key] = ability
        if config.Keys ~= nil then
            for index = 1, #config.Keys do
                local key = config.Keys[index]
                if key ~= nil then
                    self.abilities_by_key[key] = ability
                end
            end
        end
        return ability
    end

    function system:GetAbility(name)
        return self.abilities[name]
    end

    function system:GetActiveAbility(except_ability)
        for _, ability in pairs(self.abilities) do
            if ability ~= except_ability and ability.is_active then
                return ability
            end
        end
        return nil
    end

    function system:CanActivate(ability)
        if ability == nil then
            return false
        end
        if ability.is_active then
            return false
        end
        if ability.cooldown_remaining > 0.0 then
            return false
        end
        if ability.block_while_any_active and self:GetActiveAbility(ability) ~= nil then
            return false
        end
        if ability.can_activate ~= nil and not ability.can_activate(self.owner, ability) then
            return false
        end
        return true
    end

    function system:TryActivateByKey(key)
        local ability = self.abilities_by_key[key]
        if not self:CanActivate(ability) then
            if ability == nil then
                return false, "not registered"
            end
            if ability.is_active then
                return false, "already active"
            end
            if ability.cooldown_remaining > 0.0 then
                return false, string.format("cooldown %.2fs", ability.cooldown_remaining)
            end
            if ability.block_while_any_active then
                local active_ability = self:GetActiveAbility(ability)
                if active_ability ~= nil then
                    return false, "blocked by active " .. active_ability.name
                end
            end
            if ability.block_reason ~= nil then
                return false, ability.block_reason
            end
            return false, "cannot activate"
        end

        ability.is_active = true
        ability.active_remaining = ability.duration

        if ability.on_activate ~= nil then
            ability.on_activate(self.owner, ability)
        end

        if ability.duration <= 0.0 then
            self:EndAbility(ability)
        end

        return true, "activated"
    end

    function system:EndAbility(ability)
        if ability == nil or not ability.is_active then
            return
        end

        ability.is_active = false
        ability.active_remaining = 0.0
        ability.cooldown_remaining = ability.cooldown

        if ability.on_end ~= nil then
            ability.on_end(self.owner, ability)
        end
    end

    function system:Tick(dt)
        dt = dt or 0.0

        for _, ability in pairs(self.abilities) do
            if ability.is_active then
                ability.active_remaining = ability.active_remaining - dt

                if ability.on_tick ~= nil then
                    ability.on_tick(self.owner, ability, dt)
                end

                if ability.active_remaining <= 0.0 then
                    self:EndAbility(ability)
                end
            elseif ability.cooldown_remaining > 0.0 then
                ability.cooldown_remaining = clamp_zero(ability.cooldown_remaining - dt)
            end
        end
    end

    function system:HandleInput()
        for key, _ in pairs(self.abilities_by_key) do
            if Input ~= nil and Input.GetKeyDown ~= nil and Input.GetKeyDown(key) then
                self:TryActivateByKey(key)
            end
        end
    end

    return system
end

return AbilitySystem
