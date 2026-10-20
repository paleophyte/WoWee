-- Versioned, bounded snapshots. A complete reply replaces the whole activity;
-- individual fragments never update the displayed goal or grind policy.
WoweeBotActivity = {}
local Activity = WoweeBotActivity
Activity.__index = Activity

function Activity.new(nonce)
    return setmetatable({nonce = nonce or tostring(math.floor(GetTime() * 1000)), sequence = 0}, Activity)
end

local function integer(text, minimum, maximum)
    if not text or not text:match('^%-?%d+$') then return end
    local n = tonumber(text)
    if n and n >= minimum and n <= maximum then return n end
end

local function unhex(text)
    if text == '-' then return '' end
    if not text or #text > 192 or #text % 2 ~= 0 or text:find('[^0-9a-f]') then return end
    local value = text:gsub('..', function(pair) return string.char(tonumber(pair, 16)) end)
    if value:find('[%z\1-\31\127]') then return end
    return value
end

local function refreshExtensions(self)
    self.grindSupported = self.capabilityRequest == self.grindCapabilityRequest and
        self.capabilityGeneration == self.grindCapabilityGeneration and self.capabilityRequest ~= nil
    self.skinningSupported = self.grindSupported and self.capabilityRequest == self.skinningCapabilityRequest and
        self.capabilityGeneration == self.skinningCapabilityGeneration
end

function Activity:capability(prefix, text, sender, player, control)
    if prefix == 'WIDLEHCAP' then
        if not WoweeBotStatus.samePlayer(sender, player) or type(text) ~= 'string' or #text > 100 then return false end
        local id, generation = text:match('^1\t([%w_-]+)\t(%d+)\tskinning_hunt_1$')
        if not id or id ~= control.ackId or tonumber(generation) ~= control.generation then
            self.capabilityRejected = 'skinning capability does not match accepted control reply'
            return false
        end
        self.skinningCapabilityRequest, self.skinningCapabilityGeneration = id, tonumber(generation)
        refreshExtensions(self)
        return true
    end
    if prefix == 'WIDLEGCAP' then
        if not WoweeBotStatus.samePlayer(sender, player) or type(text) ~= 'string' or #text > 100 then return false end
        local id, generation = text:match('^1\t([%w_-]+)\t(%d+)\tgrind_activity_1$')
        if not id or id ~= control.ackId or tonumber(generation) ~= control.generation then
            self.capabilityRejected = 'grind capability does not match accepted control reply'
            return false
        end
        self.grindCapabilityRequest, self.grindCapabilityGeneration = id, tonumber(generation)
        refreshExtensions(self)
        return true
    end
    if prefix ~= 'WIDLECAP' or not WoweeBotStatus.samePlayer(sender, player) or
        type(text) ~= 'string' or #text > 160 then return false end
    local id, generation, features = text:match('^1\t([%w_-]+)\t(%d+)\t([%w_,]+)$')
    if not id then self.capabilityRejected = 'invalid capability fields'; return false end
    if id ~= control.ackId then self.capabilityRejected = 'capability request does not match accepted control reply'; return false end
    if tonumber(generation) ~= control.generation then self.capabilityRejected = 'capability generation mismatch'; return false end
    if not (',' .. features .. ','):find(',activity_1,', 1, true) then
        self.capabilityRejected = 'activity_1 not advertised'; return false
    end
    self.supported = true
    self.capabilityRequest, self.capabilityGeneration = id, tonumber(generation)
    refreshExtensions(self)
    return true
end

function Activity:request(generation, now)
    self.sequence = self.sequence + 1
    local id = self.nonce .. '_a' .. self.sequence
    local grindActivity = self.grindSupported and self.grindCapabilityGeneration == generation
    local skinningHunt = grindActivity and self.skinningSupported and self.skinningCapabilityGeneration == generation
    self.pending = {id = id, generation = generation, sentAt = now, parts = {}, grindActivity = grindActivity, skinningHunt = skinningHunt}
    self.lastRequest, self.requestedAt = id .. ' generation ' .. generation, now
    self.failure = nil
    return '.bot idle ' .. id .. ' snapshot ' .. generation .. (skinningHunt and ' grind_2' or grindActivity and ' grind_1' or '')
end

function Activity:reject(reason, now)
    self.lastRejected, self.rejectedAt = reason, now
end

function Activity:diagnostic(now)
    local p, count = self.pending, 0
    if p then for _ in pairs(p.parts) do count = count + 1 end end
    return {'Activity capability: ' .. (self.supported and ('typed, ' .. (self.capabilityRequest or '?') ..
        ', generation ' .. (self.capabilityGeneration or '?')) or 'legacy / not advertised'),
        'Snapshot pending: ' .. (p and (p.id .. ', generation ' .. p.generation .. ', fragments ' .. count .. '/' ..
            (p.count or '?') .. ', age ' .. math.max(0, math.floor(now - p.sentAt)) .. 's') or 'none'),
        'Last snapshot request: ' .. (self.lastRequest or 'none'),
        'Last complete snapshot: ' .. (self.completedAt and (math.max(0, math.floor(now - self.completedAt)) .. 's ago') or 'never'),
        'Snapshot failure: ' .. (self.failure or 'none'),
        'Capability last rejected: ' .. (self.capabilityRejected or 'none'),
        'Snapshot last rejected: ' .. (self.lastRejected or 'none')}
end

function Activity:expire(now, generation)
    if self.pending and (now - self.pending.sentAt > 4 or self.pending.generation ~= generation) then
        self.pending, self.failure = nil, 'Activity reply incomplete or missing'
        return true
    end
end

local states = {off=true, manual=true, combat=true, non_combat=true, dead=true, reaction=true, unknown=true}
local kinds = {none=true, quest_offer=true, quest_turnin=true, quest_objective=true,
    trainer=true, vendor=true, repair=true, social=true, other=true, grind=true, skinning=true}
local statuses = {none=true, preparing=true, ready=true, traveling=true, working=true, cooldown=true, expired=true}
local objectives = {none=true, kill=true, creature_loot=true, object_loot=true, vendor_item=true, spell=true, interact=true}
local interactions = {none=true, npc=true, object=true, player=true}

function Activity.decode(body)
    local f = {}
    for field in (body .. '\t'):gmatch('(.-)\t') do f[#f+1] = field end
    if #f ~= 19 or (f[1] ~= 'manual' and f[1] ~= 'bot') or not states[f[2]] or
        not kinds[f[3]] or not statuses[f[4]] or not objectives[f[7]] or not interactions[f[13]] then return end
    local s = {mode=f[1], state=f[2], kind=f[3], status=f[4], objectiveType=f[7], interactionType=f[13]}
    s.questId = integer(f[5], 0, 4294967295)
    s.objective = integer(f[6], -1, 3)
    s.entry = integer(f[8], -2147483648, 2147483647)
    s.distance = integer(f[9], -1, 20000000)
    s.interactionEntry = integer(f[14], 0, 4294967295)
    s.moveRetries, s.workRetries = integer(f[18], 0, 4294967295), integer(f[19], 0, 4294967295)
    s.goal, s.questTitle, s.location = unhex(f[10]), unhex(f[11]), unhex(f[12])
    s.interactionName, s.task, s.executed = unhex(f[15]), unhex(f[16]), unhex(f[17])
    if not s.questId or not s.objective or not s.entry or not s.distance or not s.interactionEntry or
        not s.moveRetries or not s.workRetries or not s.goal or not s.questTitle or not s.location or
        not s.interactionName or not s.task or not s.executed then return end
    if s.kind:match('^quest_') and s.questId == 0 then return end
    if s.kind == 'quest_objective' and s.objective < 0 then return end
    if (s.kind == 'grind' or s.kind == 'skinning') and (s.mode ~= 'bot' or s.questId ~= 0 or s.objective ~= -1 or s.objectiveType ~= 'none' or s.entry <= 0) then return end
    if s.mode == 'bot' and (s.state == 'manual' or s.state == 'off') then return end
    return s
end

function Activity:receive(prefix, text, sender, player, generation, now)
    local p = self.pending
    if prefix ~= 'WIDLEACT' then return end
    if not WoweeBotStatus.samePlayer(sender, player) then return self:reject('snapshot sender mismatch', now) end
    if not p then return self:reject('no pending snapshot request', now) end
    if type(text) ~= 'string' or #text > 240 or now - p.sentAt > 4 or generation ~= p.generation then
        return self:reject('snapshot length, age or controller generation mismatch', now)
    end
    local id, epoch, reason = text:match('^1\t([%w_-]+)\t(%d+)\terror\t([%w_]+)$')
    if id == p.id and tonumber(epoch) and tonumber(epoch) >= generation then
        self.pending, self.failure = nil, 'Activity unavailable: ' .. reason
        return nil, true
    end
    local part, count, chunk
    id, epoch, part, count, chunk = text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(.*)$')
    part, count = integer(part, 1, 12), integer(count, 1, 12)
    if id ~= p.id then return self:reject('snapshot request mismatch', now) end
    if tonumber(epoch) ~= generation then return self:reject('snapshot generation mismatch', now) end
    if not part or not count or part > count or not chunk or #chunk > 160 then return self:reject('invalid snapshot fragment', now) end
    if (p.count and p.count ~= count) or (p.parts[part] and p.parts[part] ~= chunk) then
        self.pending, self.failure = nil, 'Conflicting activity fragments'
        return nil, true
    end
    p.count, p.parts[part] = count, chunk
    for i = 1, count do if not p.parts[i] then return end end
    local snapshot = Activity.decode(table.concat(p.parts))
    self.pending = nil
    if not snapshot then self.failure = 'Invalid activity reply'; return nil, true end
    if snapshot.kind == 'grind' and not p.grindActivity then self.failure = 'Unrequested grind activity'; return nil, true end
    if snapshot.kind == 'skinning' and not p.skinningHunt then self.failure = 'Unrequested skinning activity'; return nil, true end
    snapshot.skinningHunt = p.skinningHunt == true
    snapshot.grindActivity = p.grindActivity == true
    self.failure = nil
    self.completedAt = now
    return snapshot, true
end

-- A vendor destination permits several kinds of shopping, not just selling.
local goalVerbs = {grind='Grind for XP or gold: ', trainer='Train at ', vendor='Visit vendor: ', repair='Repair at ', social='Social visit: '}
local taskNames = {
    ['rpg buy']='Buy items', ['rpg sell']='Sell items', ['rpg repair']='Repair gear',
    ['rpg train']='Train skills', ['rpg start quest']='Accept quests', ['rpg end quest']='Turn in quests',
    ['rpg get mail']='Collect mail', ['rpg ah sell']='Post auctions', ['rpg ah buy']='Buy at auction',
    ['rpg discover']='Learn a flight path', ['rpg taxi']='Take a flight',
    ['rpg ai chat']='Talk to an NPC', ['rpg emote']='Emote', ['rpg craft']='Craft items',
    ['rpg bank deposit']='Deposit at bank', ['rpg bank withdraw']='Withdraw from bank',
    ['move to rpg target']='Approach interaction target', ['choose rpg target']='Choose an interaction target',
    ['travel']='Travel', ['loot']='Loot', ['equip upgrades']='Equip upgrades',
}

function Activity.actionName(name)
    if type(name) ~= 'string' or name == '' then return end
    return taskNames[name] or name
end

function Activity.currentWork(model)
    if model.aiState == 'combat' then return 'Now: In combat' end
    if model.aiState == 'dead' then return 'Now: Dead' end
    if model.aiState == 'reaction' then return 'Now: Reacting to an event' end
    if model.interactionName then
        -- Selection is intent, not proof a purchase, lesson or turn-in happened.
        if model.interactionTask and model.interactionTask ~= 'rpg' then
            return 'Selected task: ' .. Activity.actionName(model.interactionTask) .. ' - ' .. model.interactionName
        end
        return 'Selected target: ' .. model.interactionName .. ' (task not reported)'
    end
    if model.status == 'traveling' then return 'Now: Traveling along the travel plan' end
    if model.status == 'working' then return 'Travel planner: At destination; current task not reported' end
    if model.status == 'cooldown' then return 'Now: Waiting before choosing the next activity' end
    return 'Now: No specific task reported'
end

function Activity.apply(model, s, now)
    model.typed, model.aiState, model.objectiveType = true, s.state, s.objectiveType
    model.grindActivity = s.grindActivity == true
    model.skinningHunt = s.skinningHunt == true
    model.kind = s.kind == 'quest_objective' and 'questobjective' or s.kind
    model.goal = (goalVerbs[s.kind] or '') .. (s.goal ~= '' and s.goal or 'No destination')
    model.status, model.distance = s.status, s.distance >= 0 and s.distance or nil
    model.location = s.location ~= '' and s.location or nil
    model.questId = s.questId > 0 and s.questId or nil
    model.questTitle = s.questTitle ~= '' and s.questTitle or nil
    model.questAt, model.travelAt, model.unsupported = now, now, nil
    model.objectiveEntry = s.entry
    model.interactionName = s.interactionName ~= '' and s.interactionName or nil
    model.interactionType = s.interactionType
    model.interactionTask = s.task ~= '' and s.task or nil
    model.moveRetries, model.workRetries = s.moveRetries, s.workRetries
    model.action = s.executed ~= '' and s.executed or nil
    model.actionAt = model.action and now or nil
    model.report, model.reportAt, model.actionDeadline = nil, nil, nil
end
