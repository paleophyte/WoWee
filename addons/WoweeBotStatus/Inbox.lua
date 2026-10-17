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

local function coins(value)
    return math.floor(value/10000) .. 'g ' .. math.floor(value/100)%100 .. 's ' .. value%100 .. 'c'
end

-- These are observed aggregate planner decisions, not individual trainer
-- quotes. They inform the player without blocking other automatic work.
function Inbox:trainingPlan(snapshot, now)
    if type(snapshot) ~= 'table' or snapshot.status ~= 'ok' then return false end
    local names={[0]='Class',[1]='Mount',[2]='Profession and secondary',[3]='Pet'}
    local changed,present=false,{}
    for _,row in ipairs(snapshot.rows) do
        local key='training-plan:' .. row.type
        present[key]=true
        local previous=self:find(key)
        do
            if row.state~='none' or previous then
                local title,text
                if row.state=='none' then
                    title='Training plan cleared: ' .. names[row.type]
                    text='The automatic planner last found no remaining lessons in its current list. This is not confirmation that every possible lesson has been learned.'
                else
                    local cached=row.state=='cached_eligible' or row.state=='cached_deferred'
                    title=(cached and 'Training planner awaiting recheck: ' or row.state=='deferred' and 'Training trip deferred: ' or 'Training budget check passed: ') .. names[row.type]
                    text='Last automatic planner check for ' .. names[row.type]:lower() .. ' training.\n' ..
                        'Combined lesson estimate before trainer discounts: ' .. coins(row.estimate) .. '.\n' ..
                        'Wallet at that check: ' .. coins(row.money) .. '.\n' ..
                        'Required by the planner budget gate: ' .. coins(row.required) .. '.'
                    if row.reservations==1 then text=text .. '\nThis gate includes money reserved for earlier priorities.'
                    else text=text .. '\nThis gate excludes earlier budget reservations.' end
                    if cached then
                        text=text .. '\nThe planner returned a cached ' .. (row.state=='cached_eligible' and 'allowed' or 'deferred') ..
                            ' decision that differs from the current funds check. Its normal budget refresh is pending.'
                    elseif row.state=='deferred' then
                        text=text .. '\nMissing for this combined gate: ' .. coins(row.required-row.money) ..
                            '. This budget gate declined training travel. Other work, including nearby or quest-related NPC interactions, can continue.'
                    elseif row.cheat==1 then text=text .. '\nThe server gold-cheat policy bypassed this budget gate.'
                    else text=text .. '\nThe budget gate permits a trip. A destination or purchase has not been confirmed.' end
                    text=text .. '\nThis is a combined planning check, not a discounted price or shortfall for a specific lesson.'
                end
                changed=self:put(key,'note',title,text,now,'spellbook') or changed
                local entry=self:find(key)
                entry.planObserved,entry.planCurrent=row.observed,true
            end
        end
    end
    for _,entry in ipairs(self.data.entries) do
        if entry.key:match('^training%-plan:%d+$') and entry.planCurrent and not present[entry.key] then
            changed=self:put(entry.key,'note','Earlier automatic training plan',
                'The server has no retained planner check for this category in this session. This earlier observation has not been rechecked.\n' .. entry.text,now,'spellbook') or changed
            entry.planCurrent=false
        end
    end
    return changed
end

-- A complete, authenticated server snapshot updates one durable option. These
-- observations are advisory; they never purchase training or change control.
function Inbox:training(snapshot, now)
    if type(snapshot) ~= 'table' or snapshot.status ~= 'ok' then return false end
    local changed, present = false, {}
    for _, row in ipairs(snapshot.rows) do
        local key = 'training:' .. row.lesson
        present[key] = true
        local reason = ({coins='Not enough coins.', coins_unknown_budget='Not enough coins. The bot reservation budget is unavailable while AI is absent.', budget='Coins are reserved by the bot training budget.',
            ready='Funds now permit this lesson. It has not been learned yet.',
            learned='Confirmed learned.', ineligible='This lesson is no longer eligible under the current trainer prerequisites.',
            unavailable='This trainer or lesson is no longer available.',
            unknown_budget='Wallet funds are sufficient; the bot reservation budget is unavailable while AI is absent.',
            free='The server automatic-training policy currently bypasses payment. This does not imply manual training is free.'})[row.state]
        local text = row.title .. ' at ' .. row.trainer .. '.\n' .. reason
        if row.state == 'coins' or row.state == 'coins_unknown_budget' or row.state == 'budget' or row.state == 'ready' or row.state == 'unknown_budget' then
            text = text .. '\nDiscounted price: ' .. coins(row.cost) .. '. Wallet: ' .. coins(row.money) .. '.'
            if row.state ~= 'unknown_budget' and row.state ~= 'coins_unknown_budget' then
                text = text .. (row.budget == 0 and '\nAvailable for this manual purchase: ' or '\nSpendable under the observed budget: ') .. coins(row.spendable) .. '.'
            end
            if row.state == 'coins' or row.state == 'coins_unknown_budget' then text = text .. '\nMissing from wallet: ' .. coins(row.cost-row.money) .. '.'
            elseif row.state == 'budget' then text = text .. '\nMissing from spendable budget: ' .. coins(row.cost-row.spendable) .. '.' end
        end
        if row.state == 'coins' or row.state == 'coins_unknown_budget' or row.state == 'budget' then text = text .. '\nOther activities can continue.' end
        local blocked = row.state == 'coins' or row.state == 'coins_unknown_budget' or row.state == 'budget' or row.state == 'unknown_budget'
        changed = self:put(key, blocked and 'blocked' or 'note',
            (blocked and 'Training waits: ' or 'Training update: ') .. row.title, text, now, 'spellbook') or changed
    end
    -- The server retains32 observed options. Absence is not proof of learning.
    -- Keep an explicit historical record instead of a permanently stale blocker.
    local old = {}
    for _, entry in ipairs(self.data.entries) do
        if entry.key:match('^training:%d+$') and entry.kind == 'blocked' and not present[entry.key] then old[#old+1] = entry end
    end
    for _, entry in ipairs(old) do
        changed = self:put(entry.key, 'note', 'Earlier training observation',
            'This option is outside the server\'s current32-option watch list. Its availability has not been rechecked.\n' .. entry.text,
            now, 'spellbook') or changed
    end
    return changed
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


function Inbox:travelWarning(fact, now)
    if type(fact)~='table' or type(now)~='number' then return false end
    for _,key in ipairs({'at','sequence','kind','reason','seconds','untilAt'}) do
        if type(fact[key])~='number' or fact[key]~=math.floor(fact[key]) then return false end
    end
    if fact.at<1 or fact.at>4294967175 or fact.sequence<1 or fact.sequence>4294967295 or
        fact.reason<0 or fact.reason>11 or fact.seconds<0 or fact.seconds>604800 or
        type(fact.goal)~='string' or #fact.goal>40 or fact.goal:find('[%c]') then return false end
    local reason
    if fact.kind==1 then
        if fact.reason~=0 or fact.seconds<90 or fact.untilAt~=fact.at+120 then return false end
    elseif fact.kind==2 then
        reason=WoweeBotTravel and WoweeBotTravel.waitNames[fact.reason]
        if not reason or fact.seconds<120 or fact.untilAt~=0 then return false end
    else return false end
    local last=self.data.lastTravelWarning
    if type(last)=='table' and type(last.at)=='number' and type(last.sequence)=='number' and
        (fact.at<last.at or (fact.at==last.at and fact.sequence<=last.sequence)) then return false end
    self.data.lastTravelWarning={at=fact.at,sequence=fact.sequence}
    local goal=fact.goal~='' and fact.goal or 'the selected destination'
    local title,text
    if fact.kind==1 then
        title='Travel paused: '..goal
        text='The server observed at least '..fact.seconds..'s without 3 yards of displacement, including a route rebuild. '..
            'It paused travel attempts for two minutes and kept the selected activity. '..
            'Combat, death recovery, and manual control remain available. This records a past pause; it may already have ended.'
    else
        title='Travel waiting for '..reason
        text='While heading to '..goal..', the server observed at least '..fact.seconds..'s without 3 yards of displacement. '..
            'At that time it was waiting for '..reason..'. The travel safeguard did not interrupt that activity. '..
            'This is an observation, not proof of a missing item or a broken route. Open Bot activity for current status.'
    end
    return self:put('travel-warning:'..fact.at..':'..fact.sequence,'note',title,text,now,'botstatus')
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
