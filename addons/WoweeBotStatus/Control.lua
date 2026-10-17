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
    self.fishingIntent, self.fishingAt = nil, nil
    self.recoverAt, self.recoveryDelay = nil, nil
end

function Control:refresh()
    self.failure, self.pending, self.nextStatus = nil, nil, 0
    self.confirmedAt = nil
    -- Refresh only observes. It must not replay an old, unconfirmed mutation.
    self.intent, self.desiredGrind, self.recoverAt, self.recoveryDelay = nil, nil, nil, nil
    self:refreshSocial(); self:refreshFishing()
end

function Control:fail(message, now)
    self.failure, self.pending = message, nil
    self.intent, self.desiredGrind, self.socialIntent, self.fishingIntent = nil, nil, nil, nil
    self.recoveryDelay = math.min((self.recoveryDelay or 15) * 2, 120)
    self.recoverAt = now + self.recoveryDelay
end

function Control:reject(reason, now)
    self.lastRejected, self.rejectedAt = reason, now
    return false
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
    self.lastSent, self.sentAt = command, now
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
            if p.verb == 'fishing' then
                self.pending, self.fishingIntent, self.fishingAt = nil, nil, nil
                self.fishingFailure, self.nextFishing = 'No policy confirmation', now + 30
                return
            end
            self:fail('No server confirmation; retrying status', now)
            return
        end
        p.attempts, p.sentAt = p.attempts + 1, now
        self.lastSent, self.sentAt = p.command .. ' (retry ' .. p.attempts .. ')', now
        return p.command -- retry the same request, never a blind toggle
    end
    if self.failure then
        if self.recoverAt and now >= self.recoverAt then return self:start('status', now) end
        return
    end
    if self.intent and self:fresh(now) then
        if self.mode ~= self.intent then return self:start('control', now, self.intent) end
        self.intent = nil
    end
    if self:fresh(now) and self.socialSupported and self.socialIntent ~= nil then
        return self:start('social', now, self.socialIntent and 'on' or 'off')
    end
    if self:fresh(now) and self.fishingSupported and self.fishingIntent ~= nil then
        return self:start('fishing', now, self.fishingIntent and 'on' or 'off')
    end
    if self:isBot(now) and self.desiredGrind ~= nil and self.desiredGrind ~= self.grind then
        return self:start('grind', now, self.desiredGrind and 'on' or 'off')
    end
    if now >= self.nextStatus or not self:fresh(now) then return self:start('status', now) end
    if self.socialSupported and now >= (self.nextSocial or 0) and not self:socialFresh(now) then
        return self:start('social', now, 'status')
    end
    if self.fishingSupported and now >= (self.nextFishing or 0) and not self:fishingFresh(now) then
        return self:start('fishing', now, 'status')
    end
end

function Control:receive(prefix, text, sender, player, now)
    if prefix == 'WIDLEPOL' then return self:receiveSocial(text, sender, player, now) end
    if prefix == 'WIDLEFISH' then return self:receiveFishing(text, sender, player, now) end
    if prefix ~= 'WIDLE' then return false end
    if not WoweeBotStatus.samePlayer(sender, player) then return self:reject('control sender mismatch', now) end
    if type(text) ~= 'string' or #text > 240 then return self:reject('invalid control length', now) end
    local version, id, result, mode, generation, grind, reason =
        text:match('^(%d+)\t([%w_-]+)\t(%a+)\t(%a+)\t(%d+)\t([01])\t([%w_]+)$')
    generation = tonumber(generation)
    if version ~= '1' or not id or #id > 32 or
        (result ~= 'ok' and result ~= 'error') or (mode ~= 'manual' and mode ~= 'bot') or
        not generation or generation < 1 or generation > 4294967295 then return self:reject('invalid control fields', now) end
    if not self.pending then return self:reject('no pending control request', now) end
    if self.pending.verb == 'social' or self.pending.verb == 'fishing' then return self:reject('control reply while awaiting policy', now) end
    if id ~= self.pending.id then return self:reject('control request ' .. id .. ', expected ' .. self.pending.id, now) end
    if self.generation and generation < self.generation then
        return self:reject('control generation ' .. generation .. ', expected at least ' .. self.generation, now)
    end
    if self.generation ~= generation then self:refreshSocial(); self:refreshFishing() end
    self.mode, self.generation, self.grind = mode, generation, grind == '1'
    self.ackId = id
    self.confirmedAt, self.nextStatus, self.pending = now, now + 8, nil
    self.observedAt = now
    self.lastAccepted, self.acceptedAt = id .. ' ' .. result .. ' ' .. mode .. ' generation ' .. generation, now
    if result == 'error' then self:fail('Server rejected command: ' .. reason, now)
    else
        self.failure, self.recoverAt, self.recoveryDelay = nil, nil, nil
        if self.intent == mode then self.intent = nil end
    end
    return true
end

function Control:capability(prefix, text, sender, player)
    if prefix ~= 'WIDLECAP' or not WoweeBotStatus.samePlayer(sender, player) or
        type(text) ~= 'string' or #text > 120 then return false end
    local id, generation, features = text:match('^1\t([%w_-]+)\t(%d+)\t([%w_,]+)$')
    if not id or id ~= self.ackId or tonumber(generation) ~= self.generation then return false end
    self.warningSupported = (',' .. features .. ','):find(',warning_1,', 1, true) ~= nil
    self.collectionSupported = (',' .. features .. ','):find(',collection_1,', 1, true) ~= nil
    self.fishingSupported = (',' .. features .. ','):find(',fishing_1,', 1, true) ~= nil
    if not self.fishingSupported then self.fishingAt, self.fishingIntent, self.fishingFailure = nil, nil, nil end
    self.socialSupported = (',' .. features .. ','):find(',social_1,', 1, true) ~= nil
    if not self.socialSupported then self.socialAt, self.socialIntent, self.socialFailure = nil, nil, nil end
    return true
end

-- Read-only historical warning facts share the last acknowledged request/epoch.
-- They may arrive in manual mode; they never request a control change.
function Control:warning(prefix, text, sender, player, now)
    if prefix ~= 'WIDLEWARN' or not self.warningSupported or not self:fresh(now) or
        self.failure or self.intent or (self.pending and self.pending.verb == 'control') or
        not WoweeBotStatus.samePlayer(sender, player) or type(text) ~= 'string' or #text > 180 then return end
    local id, generation, at, sequence, reason, untilAt =
        text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(%d+)\t(%d+)$')
    generation, at, sequence, reason, untilAt = tonumber(generation), tonumber(at),
        tonumber(sequence), tonumber(reason), tonumber(untilAt)
    if not id or id ~= self.ackId or generation ~= self.generation or not at or
        at < 1 or at > 4294966995 or sequence < 1 or sequence > 4294967295 or
        reason < 1 or reason > 4 or untilAt ~= at + 300 then return end
    return {at = at, sequence = sequence, reason = reason, untilAt = untilAt}
end

function Control:collectionWarning(prefix, text, sender, player, now)
    if prefix ~= 'WIDLECOL' or not self.collectionSupported or not self:fresh(now) or
        self.failure or self.intent or (self.pending and self.pending.verb == 'control') or
        not WoweeBotStatus.samePlayer(sender, player) or type(text) ~= 'string' or #text > 240 then return end
    local id, generation, at, sequence, quest, item, current, required, reason, untilAt, title =
        text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(%d+)\t(%d+)\t(%d+)\t(%d+)\t(%d+)\t(%d+)\t([%x%-]+)$')
    generation, at, sequence, quest, item, current, required, reason, untilAt = tonumber(generation),
        tonumber(at), tonumber(sequence), tonumber(quest), tonumber(item), tonumber(current),
        tonumber(required), tonumber(reason), tonumber(untilAt)
    if not id or id ~= self.ackId or generation ~= self.generation or
        at < 1 or at > 4294967175 or sequence < 1 or sequence > 4294967295 or
        quest < 1 or quest > 4294967295 or item < 1 or item > 4294967295 or
        current < 0 or required < 1 or required > 65535 or current >= required or
        reason < 1 or reason > 4 or untilAt ~= at + 120 then return end
    if title == '-' then title = nil
    else
        if #title > 80 or #title % 2 ~= 0 or title:find('[^%x]') then return end
        title = title:gsub('..', function(pair) return string.char(tonumber(pair, 16)) end)
        if title:find('[%c]') then return end
    end
    return {at=at,sequence=sequence,quest=quest,item=item,current=current,required=required,
        reason=reason,untilAt=untilAt,title=title}
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

function Control:fishingFresh(now)
    return self.fishingSupported and self.fishingAt and now - self.fishingAt <= self.FRESH_FOR and not self.fishingFailure
end

function Control:wantFishing(enabled)
    if type(enabled) ~= 'boolean' or not self.fishingSupported then return end
    self.fishingIntent, self.fishingFailure = enabled, nil
    if self.pending and self.pending.verb == 'fishing' then self.pending = nil end
end

function Control:refreshFishing()
    self.fishingAt, self.fishingIntent, self.fishingFailure, self.nextFishing = nil, nil, nil, 0
    if self.pending and self.pending.verb == 'fishing' then self.pending = nil end
end

function Control:receiveFishing(text, sender, player, now)
    if not WoweeBotStatus.samePlayer(sender, player) or type(text) ~= 'string' or #text > 240 then return false end
    local id, generation, result, enabled, storage, reason =
        text:match('^1\t([%w_-]+)\t(%d+)\t(%a+)\t([01])\t(%a+)\t([%w_]+)$')
    local p = self.pending
    generation = tonumber(generation)
    if not id or not p or p.verb ~= 'fishing' or id ~= p.id or
        (result ~= 'ok' and result ~= 'error') or (storage ~= 'stored' and storage ~= 'unavailable') or
        not generation or generation < 1 or generation > 4294967295 then return false end
    if generation ~= self.generation then
        if result ~= 'error' or reason ~= 'stale_generation' or generation < self.generation then return false end
        self.pending, self.fishingIntent, self.fishingAt = nil, nil, nil
        self.confirmedAt, self.nextStatus = nil, 0
        self.fishingFailure = 'Control changed; refreshing before policy changes'
        return true
    end
    if result == 'ok' and (storage ~= 'stored' or reason ~= 'none' or
        (p.desired ~= 'status' and (enabled == '1') ~= (p.desired == 'on'))) then return false end
    self.pending, self.fishingIntent = nil, nil
    self.nextFishing = now + 30
    if result == 'error' then
        self.fishingFailure, self.fishingAt = 'Policy rejected: ' .. reason, nil
    else
        self.fishingEnabled, self.fishingAt, self.fishingFailure = enabled == '1', now, nil
    end
    return true
end

function Control:label(now)
    if self.failure then return self.failure end
    if self.intent or (self.pending and self.pending.verb == 'control') then return 'Switching control...' end
    if not self:fresh(now) then return 'Control unconfirmed; addon automation paused' end
    return self.mode == 'manual' and 'I am playing (server confirmed)' or 'Bot is playing (server confirmed)'
end

function Control:diagnostic(now)
    local function age(at) return at and (math.max(0, math.floor(now - at)) .. 's ago') or 'never' end
    return {'Control: ' .. self:label(now),
        'Last confirmed: ' .. (self.mode or 'unknown') .. ', generation ' .. (self.generation or '?') .. ', ' .. age(self.observedAt),
        'Pending: ' .. (self.pending and (self.pending.id .. ' ' .. self.pending.verb .. ' attempt ' .. self.pending.attempts) or 'none'),
        'Last sent: ' .. (self.lastSent or 'none') .. ', ' .. age(self.sentAt),
        'Last accepted: ' .. (self.lastAccepted or 'none') .. ', ' .. age(self.acceptedAt),
        'Last rejected: ' .. (self.lastRejected or 'none') .. ', ' .. age(self.rejectedAt),
        'Read-only recovery: ' .. (self.recoverAt and (math.max(0, math.ceil(self.recoverAt - now)) .. 's') or 'not waiting')}
end
