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
end

function Control:refresh()
    self.failure, self.pending, self.nextStatus = nil, nil, 0
end

function Control:planGrind(desired)
    self.desiredGrind = desired
end

function Control:start(verb, now, desired)
    self.sequence = self.sequence + 1
    local id = self.nonce .. '_' .. self.sequence
    local command = '.bot idle ' .. id .. ' ' .. verb
    if verb ~= 'status' then command = command .. ' ' .. self.generation .. ' ' .. desired end
    self.pending = {id = id, verb = verb, command = command, attempts = 1, sentAt = now}
    return command
end

function Control:update(now)
    local p = self.pending
    if p then
        if now - p.sentAt < self.RETRY_AFTER then return end
        if p.attempts >= self.MAX_ATTEMPTS then
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
    if self:isBot(now) and self.desiredGrind ~= nil and self.desiredGrind ~= self.grind then
        return self:start('grind', now, self.desiredGrind and 'on' or 'off')
    end
    if now >= self.nextStatus or not self:fresh(now) then return self:start('status', now) end
end

function Control:receive(prefix, text, sender, player, now)
    if prefix ~= 'WIDLE' or not WoweeBotStatus.samePlayer(sender, player) or
        type(text) ~= 'string' or #text > 240 then return false end
    local version, id, result, mode, generation, grind, reason =
        text:match('^(%d+)\t([%w_-]+)\t(%a+)\t(%a+)\t(%d+)\t([01])\t([%w_]+)$')
    generation = tonumber(generation)
    if version ~= '1' or not self.pending or id ~= self.pending.id or
        (result ~= 'ok' and result ~= 'error') or (mode ~= 'manual' and mode ~= 'bot') or
        not generation or generation < 1 or generation > 4294967295 or
        (self.generation and generation < self.generation) then return false end
    self.mode, self.generation, self.grind = mode, generation, grind == '1'
    self.confirmedAt, self.nextStatus, self.pending = now, now + 8, nil
    if result == 'error' then self.failure = 'Server rejected command: ' .. reason
    elseif self.intent == mode then self.intent = nil end
    return true
end

function Control:label(now)
    if self.failure then return self.failure end
    if self.intent or (self.pending and self.pending.verb == 'control') then return 'Switching control...' end
    if not self:fresh(now) then return 'Control unconfirmed; automation paused' end
    return self.mode == 'manual' and 'I am playing (server confirmed)' or 'Bot is playing (server confirmed)'
end
