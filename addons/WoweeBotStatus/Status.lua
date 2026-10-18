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

function Status.actionText(text)
    local lastAttempt
    for attempt in text:gmatch('|A:([^|]+)') do
        if not attempt:match(' %- PREREQ$') then lastAttempt = attempt end
    end
    if not lastAttempt and text:find('PUSH:', 1, true) then return 'Waiting for next action' end
    local summary = Status.clean(lastAttempt or text)
    if #summary > 100 then summary = summary:sub(1, 97) .. '...' end
    return summary
end

function Status.samePlayer(a, b)
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
    if not Status.samePlayer(sender, player) or type(text) ~= 'string' then return false end
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
        kind = kind:gsub('%s+', '')
        self.objectiveEntry = tonumber(goal:match('|Hentry:([%-]?%d+):'))
        goal = Status.clean(goal)
        -- Switching to maintenance or another NPC must not leave a former
        -- quest displayed as if the bot were still pursuing it.
        local matchesRecentReport = self.reportAt and now - self.reportAt <= 2 and
            self.report and self.report:find(goal, 1, true)
        if not kind:find('quest', 1, true) or
            ((self.kind ~= kind or self.goal ~= goal) and not matchesRecentReport) then
            self.questId, self.questTitle, self.questAt = nil, nil, nil
        end
        self.grindActivity = nil -- A legacy description is not negotiated grind authority.
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
        self.action, self.actionAt = Status.actionText(text), now
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

-- Quest identity must come from the bot, and completion from that exact live
-- quest-log entry. A ready travel target alone does not mean a finished quest.
function Status:grindDecision(now, quest)
    if not self:isFresh(now) then return nil, 'waiting for bot status' end
    if self.kind == 'grind' then
        if not self.typed or not self.grindActivity then return false, 'grind activity requires typed confirmation' end
        if self.aiState ~= 'non_combat' and self.aiState ~= 'combat' then return nil, 'waiting for normal activity' end
        return self.status == 'working', self.status == 'working' and 'grinding for XP or gold' or 'travelling to grind area'
    end
    if self.kind and self.kind:gsub('%s+', '') == 'questobjective' then
        local id = self:currentQuest(now)
        if not id or not quest then return nil, 'waiting for quest details' end
        if quest.complete then return false, 'quest ready to turn in' end
        if quest.failed then return false, 'quest failed' end
        if self.typed then
            if self.objectiveType ~= 'kill' and self.objectiveType ~= 'creature_loot' then
                return false, 'non-combat objective'
            end
            return self.status == 'working', self.status == 'working' and 'quest objective' or 'travelling to objective'
        end
        local kill = self.goal and self.goal:match('^kill ')
        -- Legacy entry links use abs(entry), including for game objects. A
        -- positive display link cannot establish that loot comes from a mob.
        -- Only typed creature_loot can enable grinding for a loot objective.
        if self.goal and self.goal:match('^loot ') then return false, 'loot source needs typed status' end
        if not kill then return false, 'non-combat objective' end
        if not self.status or not self.status:match('^working') then
            return false, 'travelling to objective'
        end
        return true, 'quest objective'
    end
    return false, 'travel or errands'
end
