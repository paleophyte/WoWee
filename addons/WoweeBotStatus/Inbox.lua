-- Durable, realm-scoped facts. This model never executes game actions.
WoweeBotInbox = {}
local Inbox = WoweeBotInbox
Inbox.__index = Inbox
Inbox.LIMIT = 100

function Inbox.new(root, realm, character)
    if type(realm) ~= 'string' or realm == '' or type(character) ~= 'string' or
        character == '' or character == 'Unknown' then return end
    if type(root.scopes) ~= 'table' then root.scopes = {} end
    local key = #realm .. ':' .. realm .. ':' .. character
    local data = root.scopes[key]
    if type(data) ~= 'table' or data.version ~= 1 or type(data.entries) ~= 'table' then
        data = {version = 1, entries = {}, nextId = 0}
        root.scopes[key] = data
    end
    return setmetatable({data = data}, Inbox)
end

function Inbox:find(key)
    for _, e in ipairs(self.data.entries) do if e.key == key then return e end end
end

function Inbox:trim()
    -- Unresolved decisions and blockers are never discarded to make room for FYIs.
    local entries = self.data.entries
    while #entries > self.LIMIT do
        local index
        for i, e in ipairs(entries) do
            if e.kind == 'note' and (not index or e.updated < entries[index].updated) then index = i end
        end
        if not index then break end
        table.remove(entries, index)
    end
end

function Inbox:put(key, kind, title, text, now, action)
    local e = self:find(key)
    if e and e.kind == kind and e.title == title and e.text == text then return false end
    if not e then
        self.data.nextId = self.data.nextId + 1
        e = {id = self.data.nextId, key = key, created = now}
        self.data.entries[#self.data.entries + 1] = e
    end
    e.kind, e.title, e.text, e.updated, e.action = kind, title, text, now, action
    e.read, e.dismissed = false, false
    self:trim()
    return true
end

function Inbox:resolve(key, now, text)
    local e = self:find(key)
    if not e or e.kind ~= 'blocked' then return false end
    e.kind, e.text, e.updated, e.read = 'note', text, now, false
    e.title = e.title .. ' (resolved)'
    self:trim()
    return true
end

function Inbox:remove(key)
    for i, e in ipairs(self.data.entries) do
        if e.key == key then table.remove(self.data.entries, i); return true end
    end
    return false
end

function Inbox:talents(points, now)
    points = tonumber(points) or 0
    if points <= 0 then return self:remove('talents') end
    return self:put('talents', 'note', 'Unspent talent points',
        points .. ' unspent talent ' .. (points == 1 and 'point' or 'points') ..
        '. Open Talents when you are ready to choose.', now, 'talents')
end

function Inbox:learn(id, name, rank, now)
    if not id or not name or name == '' then return false end
    local e
    for _, candidate in ipairs(self.data.entries) do
        if candidate.spells and now >= candidate.updated and now - candidate.updated <= 15 and
            #candidate.spells < 30 and (not e or candidate.updated > e.updated) then e = candidate end
    end
    if e then
        for _, spell in ipairs(e.spells) do if spell.id == id then return false end end
    else
        self.data.nextId = self.data.nextId + 1
        e = {id = self.data.nextId, key = 'learned:' .. self.data.nextId,
            kind = 'note', created = now, spells = {}, action = 'spellbook'}
        self.data.entries[#self.data.entries + 1] = e
    end
    e.spells[#e.spells + 1] = {id = id, name = name, rank = rank or ''}
    e.title = #e.spells == 1 and ('New ability: ' .. name) or ('New abilities x' .. #e.spells)
    e.text = 'Learned while connected. Open Spellbook to review.'
    e.updated, e.read, e.dismissed = now, false, false
    self:trim()
    return true
end

-- These are WoWee's actual spell-failure strings. UI_ERROR_MESSAGE also carries
-- fishing-bite notifications, so never treat every event as a failure.
local observedErrors = {['Item not ready'] = 'item-not-ready', ['Need more items'] = 'need-more-items',
    ['Target not in line of sight'] = 'line-of-sight', ["You can't fish in this area."] = 'not-fishable',
    ['Face open water and try casting again.'] = 'face-open-water'}
function Inbox:observeError(message, clock, now, generation)
    if type(generation) ~= 'number' or generation < 1 then self.errorWindow = nil; return false end
    if type(clock) ~= 'number' or type(now) ~= 'number' then return false end
    local window = self.errorWindow
    if not window or window.generation ~= generation or clock < window.clock then
        window = {generation = generation, recent = {}}
        self.errorWindow = window
    end
    window.clock = clock
    if type(message) ~= 'string' or #message > 200 or message:find('[%z\1-\31\127]') then return false end
    local reason = message
    -- SMSG_SPELL_FAILURE prefixes the same reason with the spell's name when
    -- it is cached; SMSG_CAST_RESULT supplies the plain reason.
    if not observedErrors[reason] then reason = message:match('^.+: ([^:]+)$') end
    local code = observedErrors[reason]
    if not code then return false end
    local recent = {}
    for _, at in ipairs(window.recent[code] or {}) do
        if clock - at <= 30 then recent[#recent + 1] = at end
    end
    recent[#recent + 1] = clock
    while #recent > 3 do table.remove(recent, 1) end
    window.recent[code] = recent
    if #recent < 3 then return false end
    -- One FYI per error per five-minute bucket, including across relogs. Normal
    -- note dismissal/retention applies; this never blocks play or chooses items.
    local key = 'game-error:' .. code .. ':' .. math.floor(now / 300)
    if self:find(key) then return false end
    local inspect = code == 'line-of-sight' and 'the target and line of sight' or
        (code == 'not-fishable' or code == 'face-open-water') and 'the fishing spot and facing' or
        'equipment and supplies'
    return self:put(key, 'note', 'Repeated game error: ' .. reason,
        'The game reported "' .. reason .. '" at least 3 times within 30 seconds while bot control was confirmed. ' ..
        'Most recent message: "' .. message .. '". ' ..
        'Check Bot activity for the current action, or take manual control to inspect ' .. inspect .. '.',
        now, 'botstatus')
end

-- Server observations, separate from client red error-message heuristics.
local fishingReasons = {'the fishing cast action failed',
    'an accepted cast did not start a fishing channel',
    'no fishing pole was found when the action executed',
    'the fishing pole could not be equipped'}
function Inbox:fishingWarning(fact, now)
    if type(fact) ~= 'table' or type(now) ~= 'number' then return false end
    for _, key in ipairs({'at', 'sequence', 'reason', 'untilAt'}) do
        if type(fact[key]) ~= 'number' or fact[key] ~= math.floor(fact[key]) then return false end
    end
    local reason = fishingReasons[fact.reason]
    if not reason or fact.at < 1 or fact.at > 4294966995 or fact.sequence < 1 or
        fact.sequence > 4294967295 or fact.untilAt ~= fact.at + 300 then return false end
    local last = self.data.lastFishingWarning
    if type(last) == 'table' and type(last.at) == 'number' and type(last.sequence) == 'number' and
        (fact.at < last.at or (fact.at == last.at and fact.sequence <= last.sequence)) then return false end
    -- Persist the high-water mark too: retained status must not reappear after
    -- a dismissed entry eventually leaves the bounded note history.
    self.data.lastFishingWarning = {at = fact.at, sequence = fact.sequence}
    return self:put('fishing-warning:' .. fact.at .. ':' .. fact.sequence, 'note',
        'Fishing retry limit reached',
        'The server paused fishing for five minutes after 3 failed attempts. Last recorded failure: ' ..
        reason .. '. Other bot activities and manual control remain available. ' ..
        'This is a record of that pause; it may already have ended.', now, 'botstatus')
end

local collectionReasons = {
    'The containers could not be looted; the server did not identify why.',
    'The server still reported full bags after trying to make room.',
    "The quest item or its container was on the bot's skip-loot list.",
    'The last checked container was in use or not ready.',
    'No spawned quest container was found nearby in three fresh checks at a collection stop.'}
function Inbox:collectionWarning(fact, now)
    if type(fact) ~= 'table' or type(now) ~= 'number' then return false end
    for _, key in ipairs({'at','sequence','quest','item','current','required','reason','untilAt'}) do
        if type(fact[key]) ~= 'number' or fact[key] ~= math.floor(fact[key]) then return false end
    end
    local reason=collectionReasons[fact.reason]
    local emptyStop = fact.source == 'empty_stop'
    if not reason or (emptyStop and fact.reason ~= 5) or (not emptyStop and fact.reason == 5) or
        fact.at < 1 or fact.at > 4294967175 or fact.sequence < 1 or fact.sequence > 4294967295 or
        fact.quest < 1 or fact.quest > 4294967295 or fact.item < 1 or fact.item > 4294967295 or
        fact.current < 0 or fact.required < 1 or fact.required > 65535 or fact.current >= fact.required or
        fact.untilAt ~= fact.at + 120 or (fact.title ~= nil and
        (type(fact.title) ~= 'string' or #fact.title > 40 or fact.title:find('[%c]'))) then return false end
    local watermark = emptyStop and 'lastCollectionStopWarning' or 'lastCollectionWarning'
    local last=self.data[watermark]
    if type(last)=='table' and type(last.at)=='number' and type(last.sequence)=='number' and
        (fact.at < last.at or (fact.at==last.at and fact.sequence<=last.sequence)) then return false end
    self.data[watermark]={at=fact.at,sequence=fact.sequence}
    local title=fact.title and fact.title~='' and fact.title or ('Quest #'..fact.quest)
    local item=type(GetItemInfo)=='function' and GetItemInfo(fact.item)
    item=item or ('item #'..fact.item)
    if emptyStop then
        return self:put('collection-stop-warning:'..fact.at..':'..fact.sequence,'note',
            'No quest containers nearby: '..title,
            reason..' The server skipped that stop for two minutes so the planner could choose another. '..
            'At that time, '..item..' was '..fact.current..'/'..fact.required..'. The selected quest was kept. '..
            'This does not establish why the containers were absent, or their availability elsewhere. '..
            'This records a past pause; it may already have ended.',now,'botstatus')
    end
    return self:put('collection-warning:'..fact.at..':'..fact.sequence,'note',
        'Could not collect: '..title,
        'The server skipped the checked containers for two minutes after 3 rejected loot checks. '..reason..
        ' At that time, '..item..' was '..fact.current..'/'..fact.required..'. The selected quest was kept. '..
        'This records a past backoff; it may already have ended.',now,'botstatus')
end

function Inbox:visible()
    local result = {}
    for _, e in ipairs(self.data.entries) do if not e.dismissed then result[#result + 1] = e end end
    local order = {decision = 1, blocked = 2, note = 3}
    table.sort(result, function(a, b)
        if order[a.kind] ~= order[b.kind] then return order[a.kind] < order[b.kind] end
        if a.updated ~= b.updated then return a.updated > b.updated end
        return a.id > b.id
    end)
    return result
end

function Inbox:counts()
    local counts = {decision = 0, blocked = 0, note = 0, unread = 0}
    for _, e in ipairs(self:visible()) do
        counts[e.kind] = counts[e.kind] + 1
        if not e.read then counts.unread = counts.unread + 1 end
    end
    return counts
end

function Inbox:dismiss(key)
    local e = self:find(key)
    if not e or e.kind ~= 'note' or e.dismissed then return false end
    e.dismissed, e.read = true, true
    return true
end

function Inbox:clearNotes()
    local changed = false
    for _, e in ipairs(self.data.entries) do
        if e.kind == 'note' and not e.dismissed then e.dismissed, e.read, changed = true, true, true end
    end
    return changed
end
