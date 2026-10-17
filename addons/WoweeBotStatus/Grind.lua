-- Commands are changes to the server's non-combat strategy, not attack orders.
-- Never pretend a queued command succeeded: wait for its strategy-list reply.
WoweeBotGrind = {}
local Grind = WoweeBotGrind
Grind.__index = Grind
Grind.RETRY_AFTER = 8
Grind.MAX_ATTEMPTS = 3

function Grind.new()
    return setmetatable({attempts = 0}, Grind)
end

function Grind:receive(prefix, text, sender, player, now)
    if prefix ~= 'BOT' or not WoweeBotStatus.samePlayer(sender, player) or
        type(text) ~= 'string' then return false end
    local list = WoweeBotStatus.clean(text):match('^Non Combat Strategies:%s*(.*)$')
    if not list then return false end
    self.observed = false
    for strategy in list:gmatch('[^,]+') do
        if strategy:match('^%s*(.-)%s*$') == 'grind' then self.observed = true end
    end
    self.observedAt = now
    if self.observed == self.desired then self.pending, self.attempts = nil, 0 end
    return true
end

function Grind:retry()
    self.attempts, self.pending = 0, nil
end

function Grind:update(status, quest, now, grouped)
    self.paused = nil
    if grouped then self.reason, self.paused = 'paused while grouped', true; return end
    local desired, reason = status:grindDecision(now, quest)
    self.reason = reason
    if desired == nil then self.paused = true; return end
    if desired ~= self.desired then self.attempts, self.sentAt = 0, nil end
    self.desired = desired
    if not self.observedAt or now - self.observedAt > WoweeBotStatus.STALE_AFTER then return end
    if self.observed == desired and not self.pending then self.attempts = 0; return end
    if self.attempts >= self.MAX_ATTEMPTS or
        (self.sentAt and now - self.sentAt < self.RETRY_AFTER) then return end
    self.attempts = self.attempts + 1
    self.sentAt, self.pending = now, true
    -- The question mark asks the server to report the resulting strategies.
    return '#a nc ' .. (desired and '+' or '-') .. 'grind,?'
end

function Grind:label(now)
    if self.paused then return 'Auto grind: ' .. self.reason end
    if self.pending then
        if self.attempts >= self.MAX_ATTEMPTS and now - self.sentAt >= self.RETRY_AFTER then
            return 'Grind change unconfirmed; Refresh to retry'
        end
        return 'Grind: waiting for server to turn ' .. (self.desired and 'on' or 'off')
    end
    if not self.observedAt or now - self.observedAt > WoweeBotStatus.STALE_AFTER then
        return 'Grind: waiting for server strategy status'
    end
    return 'Grind: ' .. (self.observed and 'on' or 'off') .. ' (confirmed) - ' .. (self.reason or '')
end
