-- The server owns control. A click expresses intent; only a matching reply
-- confirms it. Epochs reject commands issued before a manual takeover.
WoweeBotControl = {}
local Control = WoweeBotControl
Control.__index = Control
Control.FRESH_FOR, Control.RETRY_AFTER, Control.MAX_ATTEMPTS = 25, 3, 3

function Control.new(nonce)
    return setmetatable({nonce = nonce or tostring(math.floor(GetTime() * 1000)), sequence = 0,
        nextStatus = 0}, Control)
end

function Control:fresh(now)
    return self.confirmedAt and now - self.confirmedAt <= self.FRESH_FOR
end

function Control:isBot(now)
    return self:fresh(now) and self.mode == 'bot' and not self.failure and
        not self.intent and not (self.pending and self.pending.verb == 'control')
end

function Control:want(mode)
    if mode ~= 'manual' and mode ~= 'bot' then return end
    self.intent, self.failure, self.pending = mode, nil, nil
    self.desiredGrind = nil
    self.socialIntent, self.socialAt = nil, nil
end

function Control:refresh()
    self.failure, self.pending, self.nextStatus = nil, nil, 0
    self:refreshSocial()
end

function Control:planGrind(desired)
    self.desiredGrind = desired
end

function Control:start(verb, now, desired)
    self.sequence = self.sequence + 1
    local id = self.nonce .. '_' .. self.sequence
    local command = '.bot idle ' .. id .. ' ' .. verb
    if verb ~= 'status' then command = command .. ' ' .. self.generation .. ' ' .. desired end
    self.pending = {id = id, verb = verb, command = command, desired = desired, attempts = 1, sentAt = now}
    return command
end

function Control:update(now)
    local p = self.pending
    if p then
        if now - p.sentAt < self.RETRY_AFTER then return end
        if p.attempts >= self.MAX_ATTEMPTS then
            if p.verb == 'social' then
                self.pending, self.socialIntent, self.socialAt = nil, nil, nil
                self.socialFailure, self.nextSocial = 'No policy confirmation', now + 30
                return
            end
            self.pending, self.failure = nil, 'No confirmation; Refresh to retry'
            return
        end
        p.attempts, p.sentAt = p.attempts + 1, now
        return p.command -- retry the same request, never a blind toggle
    end
    if self.failure then return end
    if self.intent and self:fresh(now) then
        if self.mode ~= self.intent then return self:start('control', now, self.intent) end
        self.intent = nil
    end
    if self:fresh(now) and self.socialSupported and self.socialIntent ~= nil then
        return self:start('social', now, self.socialIntent and 'on' or 'off')
    end
    if self:isBot(now) and self.desiredGrind ~= nil and self.desiredGrind ~= self.grind then
        return self:start('grind', now, self.desiredGrind and 'on' or 'off')
    end
    if now >= self.nextStatus or not self:fresh(now) then return self:start('status', now) end
    if self.socialSupported and now >= (self.nextSocial or 0) and not self:socialFresh(now) then
        return self:start('social', now, 'status')
    end
end

function Control:receive(prefix, text, sender, player, now)
    if prefix == 'WIDLEPOL' then return self:receiveSocial(text, sender, player, now) end
    if prefix ~= 'WIDLE' or not WoweeBotStatus.samePlayer(sender, player) or
        type(text) ~= 'string' or #text > 240 then return false end
    local version, id, result, mode, generation, grind, reason =
        text:match('^(%d+)\t([%w_-]+)\t(%a+)\t(%a+)\t(%d+)\t([01])\t([%w_]+)$')
    generation = tonumber(generation)
    if version ~= '1' or not self.pending or self.pending.verb == 'social' or id ~= self.pending.id or
        (result ~= 'ok' and result ~= 'error') or (mode ~= 'manual' and mode ~= 'bot') or
        not generation or generation < 1 or generation > 4294967295 or
        (self.generation and generation < self.generation) then return false end
    if self.generation ~= generation then self:refreshSocial() end
    self.mode, self.generation, self.grind = mode, generation, grind == '1'
    self.ackId = id
    self.confirmedAt, self.nextStatus, self.pending = now, now + 8, nil
    if result == 'error' then self.failure = 'Server rejected command: ' .. reason
    elseif self.intent == mode then self.intent = nil end
    return true
end

function Control:capability(prefix, text, sender, player)
    if prefix ~= 'WIDLECAP' or not WoweeBotStatus.samePlayer(sender, player) or
        type(text) ~= 'string' or #text > 120 then return false end
    local id, generation, features = text:match('^1\t([%w_-]+)\t(%d+)\t([%w_,]+)$')
    if not id or id ~= self.ackId or tonumber(generation) ~= self.generation then return false end
    self.socialSupported = (',' .. features .. ','):find(',social_1,', 1, true) ~= nil
    if not self.socialSupported then self.socialAt, self.socialIntent, self.socialFailure = nil, nil, nil end
    return true
end

function Control:socialFresh(now)
    return self.socialSupported and self.socialAt and now - self.socialAt <= self.FRESH_FOR and not self.socialFailure
end

function Control:wantSocial(enabled)
    if type(enabled) ~= 'boolean' or not self.socialSupported then return end
    self.socialIntent, self.socialFailure = enabled, nil
    if self.pending and self.pending.verb == 'social' then self.pending = nil end
end

function Control:refreshSocial()
    self.socialAt, self.socialIntent, self.socialFailure, self.nextSocial = nil, nil, nil, 0
    if self.pending and self.pending.verb == 'social' then self.pending = nil end
end

function Control:receiveSocial(text, sender, player, now)
    if not WoweeBotStatus.samePlayer(sender, player) or type(text) ~= 'string' or #text > 240 then return false end
    local id, generation, result, enabled, storage, reason =
        text:match('^1\t([%w_-]+)\t(%d+)\t(%a+)\t([01])\t(%a+)\t([%w_]+)$')
    local p = self.pending
    generation = tonumber(generation)
    if not id or not p or p.verb ~= 'social' or id ~= p.id or
        (result ~= 'ok' and result ~= 'error') or (storage ~= 'stored' and storage ~= 'unavailable') or
        not generation or generation < 1 or generation > 4294967295 then return false end
    if generation ~= self.generation then
        if result ~= 'error' or reason ~= 'stale_generation' or generation < self.generation then return false end
        self.pending, self.socialIntent, self.socialAt = nil, nil, nil
        self.confirmedAt, self.nextStatus = nil, 0
        self.socialFailure = 'Control changed; refreshing before policy changes'
        return true
    end
    if result == 'ok' and (storage ~= 'stored' or reason ~= 'none' or
        (p.desired ~= 'status' and (enabled == '1') ~= (p.desired == 'on'))) then return false end
    self.pending, self.socialIntent = nil, nil
    self.nextSocial = now + 30
    if result == 'error' then
        self.socialFailure, self.socialAt = 'Policy rejected: ' .. reason, nil
    else
        self.socialEnabled, self.socialAt, self.socialFailure = enabled == '1', now, nil
    end
    return true
end

function Control:label(now)
    if self.failure then return self.failure end
    if self.intent or (self.pending and self.pending.verb == 'control') then return 'Switching control...' end
    if not self:fresh(now) then return 'Control unconfirmed; automation paused' end
    return self.mode == 'manual' and 'I am playing (server confirmed)' or 'Bot is playing (server confirmed)'
end
