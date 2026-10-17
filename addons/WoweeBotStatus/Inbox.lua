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
