-- Historical verified outcomes, independent of the current plan/controller.
-- A missing/invalid history reply must never pause bot control or erase facts.
WoweeBotHistory = {}
local History = WoweeBotHistory
History.__index = History

function History.new()
    return setmetatable({entries={}, highwater={}, epochs={}, missed=0}, History)
end

local function integer(s, minimum, maximum)
    if type(s) ~= 'string' or not s:match('^%d+$') then return end
    local n = tonumber(s)
    if n and n >= minimum and n <= maximum then return n end
end
local function unhex(s)
    if not s or s == '' or #s > 160 or #s % 2 ~= 0 or s:find('[^0-9a-f]') then return end
    local text = s:gsub('..', function(pair) return string.char(tonumber(pair, 16)) end)
    if text:find('[%z\1-\31\127]') then return end
    return text
end
local kinds = {purchase=true, learned=true, turnin=true}

function History.decode(body)
    if type(body) ~= 'string' or #body > 4096 then return end
    local lines = {}
    for line in (body .. '\n'):gmatch('(.-)\n') do lines[#lines+1] = line end
    local epoch, count = (lines[1] or ''):match('^([%w_]+)\t(%d+)$')
    count = integer(count, 0, 16)
    if not epoch or #epoch > 32 or not count or #lines ~= count + 1 or
        (epoch ~= 'none' and not epoch:match('^%d+_%d+$')) or (epoch == 'none' and count ~= 0) then return end
    local entries, previous = {}, nil
    for i = 2, #lines do
        local f = {}
        for field in (lines[i] .. '\t'):gmatch('(.-)\t') do f[#f+1] = field end
        if #f ~= 7 then return end
        local e = {sequence=integer(f[1],1,4294967295), at=integer(f[2],1,4294967295),
            kind=f[3], subject=integer(f[4],1,4294967295), quantity=integer(f[5],1,4294967295),
            npc=integer(f[6],0,4294967295), text=unhex(f[7]), epoch=epoch}
        if not e.sequence or not e.at or not kinds[e.kind] or not e.subject or not e.quantity or
            not e.npc or not e.text or (previous and e.sequence ~= previous + 1) or
            (e.kind ~= 'purchase' and e.quantity ~= 1) then return end
        entries[#entries+1], previous = e, e.sequence
    end
    return {epoch=epoch, entries=entries}
end

function History:capability(prefix, text, sender, player, control)
    if prefix ~= 'WIDLECAP' or not WoweeBotStatus.samePlayer(sender,player) or
        type(text) ~= 'string' or #text > WoweeBotStatus.CAPABILITY_MAX_BYTES then return end
    local id, generation, features = text:match('^1\t([%w_-]+)\t(%d+)\t([%w_,]+)$')
    if not id or id ~= control.ackId or tonumber(generation) ~= control.generation then return end
    self.supported = (',' .. features .. ','):find(',history_1,',1,true) ~= nil
    if not self.supported then self.pending = nil end
end

function History:expect(id, generation, now)
    if self.supported then
        self.pending = {id=id, generation=generation, sentAt=now, parts={}}
        self.failure = nil
    end
end

function History:expire(now, generation)
    if self.pending and (now < self.pending.sentAt or now-self.pending.sentAt > 4 or
        generation ~= self.pending.generation) then
        self.pending, self.failure = nil, 'History reply missing or incomplete; previous records retained.'
    end
end

function History:apply(snapshot, now)
    local epoch = snapshot.epoch
    if self.highwater[epoch] == nil then
        self.epochs[#self.epochs+1] = epoch
        self.highwater[epoch] = 0
        if #self.epochs > 8 then self.highwater[table.remove(self.epochs,1)] = nil end
    end
    local last = self.highwater[epoch]
    if snapshot.entries[1] and snapshot.entries[1].sequence > last+1 then
        self.missed = self.missed + snapshot.entries[1].sequence-last-1
    end
    for _, entry in ipairs(snapshot.entries) do
        if entry.sequence > last then
            entry.receivedAt=now
            self.entries[#self.entries+1] = entry
            self.highwater[epoch] = entry.sequence
        end
    end
    while #self.entries > 64 do table.remove(self.entries,1) end
    self.completedAt = now
end

function History:receive(prefix, text, sender, player, generation, now)
    local p = self.pending
    if prefix ~= 'WIDLEHIST' or not self.supported or not p or
        not WoweeBotStatus.samePlayer(sender,player) or type(text) ~= 'string' or #text > 240 or
        now < p.sentAt or now-p.sentAt > 4 or generation ~= p.generation then return end
    local id, epoch, part, count, chunk = text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(.*)$')
    part, count = integer(part,1,32), integer(count,1,32)
    if id ~= p.id or tonumber(epoch) ~= generation or not part or not count or part > count or
        not chunk or #chunk > 160 then return end
    if (p.count and p.count ~= count) or (p.parts[part] and p.parts[part] ~= chunk) then
        self.pending, self.failure = nil, 'Conflicting history fragments; previous records retained.'
        return true
    end
    p.count, p.parts[part] = count, chunk
    for i = 1,count do if not p.parts[i] then return end end
    local snapshot = History.decode(table.concat(p.parts))
    self.pending = nil
    if not snapshot then self.failure = 'Invalid history reply; previous records retained.'; return true end
    self.failure = nil
    self:apply(snapshot,now)
    return true
end
