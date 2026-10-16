-- Playerbots answers "debug traveldetail" without an addon prefix, while
-- "#a where" reports the actual quest link with the BOT prefix. The travel
-- destination alone often names an NPC, not the quest: never infer a quest
-- from whichever quest happens to be first in the player's log.
WoweeBotStatus = {}
local Status = WoweeBotStatus
Status.__index = Status
Status.STALE_AFTER = 25

function Status.clean(text)
    text = (text or ''):gsub('|H.-|h(.-)|h', '%1')
    return (text:gsub('|c%x%x%x%x%x%x%x%x', ''):gsub('|r', ''))
end

local function samePlayer(a, b)
    return type(a) == 'string' and type(b) == 'string' and a ~= '' and b ~= '' and
        a:match('^[^-]+') ~= nil and b:match('^[^-]+') ~= nil and
        a:match('^[^-]+'):lower() == b:match('^[^-]+'):lower()
end

function Status.new()
    return setmetatable({}, Status)
end

function Status:request(now)
    self.requestedAt = now
    self.firstRequestAt = self.firstRequestAt or now
    self.actionDeadline = now + 4
end

function Status:receive(prefix, text, sender, player, now)
    if not samePlayer(sender, player) or type(text) ~= 'string' then return false end
    if prefix == 'BOT' then
        local plain = Status.clean(text)
        local intent = plain:match('^Traveling') or plain:match('^Currently') or
            plain:match('^Continuing') or plain:match('^Taking ') or
            plain:match('^I want to travel') or plain:match('^%(Forced%)')
        if intent then
            local id, title = text:match('|Hquest:(%d+)[^|]*|h(.-)|h')
            self.questId = tonumber(id)
            self.questTitle = title and Status.clean(title):gsub('^%[(.*)%]$', '%1') or nil
            self.questAt = now
            self.report = plain
            self.reportAt = now
            return true
        end
        if plain:find('Nowhere to travel', 1, true) then
            self.questId, self.questTitle = nil, nil
            self.report, self.reportAt = plain, now
            return true
        end
        return false
    end
    if prefix ~= '' then return false end
    if text:find('invalid command:', 1, true) then
        self.unsupported = true
        return true
    end
    if text:find('travel target\n', 1, true) then
        local kind = text:match('^([^\n]+) travel target')
        local goal = text:match('\nTarget: ([^\n]+)')
        if not kind or not goal then return false end
        goal = Status.clean(goal)
        -- Switching to maintenance or another NPC must not leave a former
        -- quest displayed as if the bot were still pursuing it.
        local matchesRecentReport = self.reportAt and now - self.reportAt <= 2 and
            self.report and self.report:find(goal, 1, true)
        if not kind:find('quest', 1, true) or
            ((self.kind ~= kind or self.goal ~= goal) and not matchesRecentReport) then
            self.questId, self.questTitle, self.questAt = nil, nil, nil
        end
        self.kind, self.goal = kind, goal
        self.distance = tonumber(text:match('\nDistance (%d+)y'))
        self.location = text:match('\nLocation: ([^\n]+)')
        self.status = text:match('\nStatus: ([^\n]+)')
        -- The server's "[for Ns]" is target lifetime, not arrival time.
        -- Show the status but never present that lifetime as an ETA.
        if self.status then self.status = self.status:gsub(' %[%s*for [^%]]*%]', '') end
        self.travelAt, self.unsupported = now, nil
        return true
    end
    -- Only one query has an unstructured single-line reply: debug action.
    -- Travel replies and BOT reports are identified independently, so their
    -- arrival order cannot turn a travel report into a last-action label.
    if self.actionDeadline and now <= self.actionDeadline and
        not text:find('\n', 1, true) and text ~= '' then
        self.action, self.actionAt = Status.clean(text), now
        self.actionDeadline = nil
        return true
    end
    return false
end

function Status:isFresh(now)
    return not self.disabled and self.travelAt ~= nil and now - self.travelAt <= self.STALE_AFTER
end

function Status:currentQuest(now)
    if self:isFresh(now) and self.questAt and now - self.questAt <= self.STALE_AFTER then
        return self.questId, self.questTitle
    end
end
