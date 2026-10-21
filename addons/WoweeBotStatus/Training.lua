WoweeBotTraining = {}
local Training = WoweeBotTraining
Training.__index = Training
function Training.new() return setmetatable({}, Training) end

local function integer(s, minimum, maximum)
    if type(s) ~= 'string' or not s:match('^%d+$') then return end
    local n = tonumber(s)
    if n and n >= minimum and n <= maximum then return n end
end
local function unhex(s)
    if type(s) ~= 'string' or s == '' or #s > 96 or #s % 2 ~= 0 or s:find('[^0-9a-f]') then return end
    local text = s:gsub('..', function(pair) return string.char(tonumber(pair,16)) end)
    if text:find('[%z\1-\31\127]') then return end
    return text
end
local states = {coins=true, budget=true, ready=true, learned=true, ineligible=true,
    unavailable=true, unknown_budget=true, coins_unknown_budget=true, free=true}
function Training.decode(body)
    if type(body) ~= 'string' or #body > 16384 then return end
    local lines = {}
    for line in (body .. '\n'):gmatch('(.-)\n') do lines[#lines+1] = line end
    local status, count = (lines[1] or ''):match('^1\t(%l+)\t(%d+)$')
    count = integer(count,0,32)
    if (status ~= 'ok' and status ~= 'unavailable') or not count or #lines ~= count+1 or
        (status == 'unavailable' and count ~= 0) then return end
    local rows, seen = {}, {}
    for i = 2,#lines do
        local f = {}
        for field in (lines[i] .. '\t'):gmatch('(.-)\t') do f[#f+1] = field end
        if #f ~= 11 then return end
        local row = {lesson=integer(f[1],1,4294967295), npc=integer(f[2],1,4294967295),
            budget=integer(f[3],0,12), observed=integer(f[4],1,4294967295),
            display=integer(f[5],1,4294967295), state=f[6], cost=integer(f[7],0,4294967295),
            money=integer(f[8],0,4294967295), spendable=integer(f[9],0,4294967295),
            title=unhex(f[10]), trainer=unhex(f[11])}
        if not row.lesson or not row.npc or not row.budget or not row.observed or not row.display or
            not states[row.state] or not row.cost or not row.money or not row.spendable or
            not row.title or not row.trainer or seen[row.lesson] or row.spendable > row.money then return end
        if ((row.state == 'coins' or row.state == 'coins_unknown_budget') and row.cost <= row.money) or
            (row.state == 'budget' and (row.cost > row.money or row.cost <= row.spendable)) or
            (row.state == 'ready' and row.cost > row.spendable) then return end
        if row.budget == 0 and ((row.state == 'coins' or row.state == 'ready') and row.spendable ~= row.money or
            row.state == 'budget' or row.state == 'free' or row.state == 'unknown_budget' or row.state == 'coins_unknown_budget') then return end
        seen[row.lesson], rows[#rows+1] = true, row
    end
    return {status=status, rows=rows}
end

function Training:capability(prefix, text, sender, player, control)
    if prefix ~= 'WIDLECAP' or not WoweeBotStatus.samePlayer(sender,player) or
        type(text) ~= 'string' or #text > WoweeBotStatus.CAPABILITY_MAX_BYTES then return end
    local id, generation, features = text:match('^1\t([%w_-]+)\t(%d+)\t([%w_,]+)$')
    if not id or id ~= control.ackId or tonumber(generation) ~= control.generation then return end
    self.supported = (',' .. features .. ','):find(',training_1,',1,true) ~= nil
    if not self.supported then self.pending = nil end
end

function Training:expect(id, generation, now)
    if self.supported then
        self.pending = {id=id, generation=generation, sentAt=now, parts={}}
        self.failure = nil
    end
end

function Training:expire(now, generation)
    if self.pending and (now < self.pending.sentAt or now-self.pending.sentAt > 4 or
        generation ~= self.pending.generation) then
        self.pending, self.failure = nil, 'Training reply missing or incomplete; previous records retained.'
    end
end

function Training:receive(prefix, text, sender, player, generation, now)
    local p = self.pending
    if prefix ~= 'WIDLETRAIN' or not self.supported or not p or
        not WoweeBotStatus.samePlayer(sender,player) or type(text) ~= 'string' or #text > 240 or
        now < p.sentAt or now-p.sentAt > 4 or generation ~= p.generation then return end
    local id, epoch, part, count, chunk = text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(.*)$')
    part, count = integer(part,1,128), integer(count,1,128)
    if id ~= p.id or tonumber(epoch) ~= generation or not part or not count or part > count or
        not chunk or #chunk > 160 then return end
    if (p.count and p.count ~= count) or (p.parts[part] and p.parts[part] ~= chunk) then
        self.pending, self.failure = nil, 'Conflicting training fragments; previous records retained.'
        return true
    end
    p.count, p.parts[part] = count, chunk
    for i = 1,count do if not p.parts[i] then return end end
    local snapshot = Training.decode(table.concat(p.parts))
    self.pending = nil
    if not snapshot then self.failure = 'Invalid training reply; previous records retained.'; return true end
    self.failure = nil
    self.snapshot, self.completedAt = snapshot, now
    return true
end
