WoweeBotTrainingPlan = {}
local Plan = WoweeBotTrainingPlan
Plan.__index = Plan
function Plan.new() return setmetatable({}, Plan) end

local function integer(s, minimum, maximum)
    if type(s) ~= 'string' or not s:match('^%d+$') then return end
    local n = tonumber(s)
    if n and n >= minimum and n <= maximum then return n end
end

function Plan.decode(body)
    if type(body) ~= 'string' or #body > 1024 then return end
    local lines = {}
    for line in (body .. '\n'):gmatch('(.-)\n') do lines[#lines+1] = line end
    local status, count = (lines[1] or ''):match('^1\t(%l+)\t(%d+)$')
    count = integer(count,0,4)
    if (status ~= 'ok' and status ~= 'unavailable') or not count or #lines ~= count+1 or
        (status == 'unavailable' and count ~= 0) then return end
    local rows,seen = {},{}
    for i=2,#lines do
        local f={}
        for field in (lines[i] .. '\t'):gmatch('(.-)\t') do f[#f+1]=field end
        if #f~=9 then return end
        local row={type=integer(f[1],0,3),observed=integer(f[2],1,4294967295),state=f[3],
            estimate=integer(f[4],0,4294967295),required=integer(f[5],0,4294967295),
            money=integer(f[6],0,4294967295),budget=integer(f[7],1,12),
            reservations=integer(f[8],0,1),cheat=integer(f[9],0,1)}
        if not row.type or not row.observed or not row.estimate or not row.required or not row.money or
            not row.budget or not row.reservations or not row.cheat or seen[row.type] then return end
        if row.state=='none' then
            if row.estimate~=0 or row.required~=0 then return end
        elseif row.state=='eligible' or row.state=='deferred' then
            if row.estimate==0 or (row.state=='eligible')~=(row.cheat==1 or row.required<=row.money) then return end
        elseif row.state=='cached_eligible' or row.state=='cached_deferred' then
            if row.estimate==0 or (row.state=='cached_eligible')==(row.cheat==1 or row.required<=row.money) then return end
        else return end
        seen[row.type],rows[#rows+1]=true,row
    end
    return {status=status,rows=rows}
end

function Plan:capability(prefix, text, sender, player, control)
    if prefix ~= 'WIDLECAP' or not WoweeBotStatus.samePlayer(sender,player) or
        type(text) ~= 'string' or #text > WoweeBotStatus.CAPABILITY_MAX_BYTES then return end
    local id, generation, features = text:match('^1\t([%w_-]+)\t(%d+)\t([%w_,]+)$')
    if not id or id ~= control.ackId or tonumber(generation) ~= control.generation then return end
    self.supported = (',' .. features .. ','):find(',training_plan_1,',1,true) ~= nil
    if not self.supported then self.pending = nil end
end

function Plan:expect(id, generation, now)
    if self.supported then
        self.pending = {id=id, generation=generation, sentAt=now, parts={}}
        self.failure = nil
    end
end

function Plan:expire(now, generation)
    if self.pending and (now < self.pending.sentAt or now-self.pending.sentAt > 4 or
        generation ~= self.pending.generation) then
        self.pending, self.failure = nil, 'Plan reply missing or incomplete; previous records retained.'
    end
end

function Plan:receive(prefix, text, sender, player, generation, now)
    local p = self.pending
    if prefix ~= 'WIDLEPLAN' or not self.supported or not p or
        not WoweeBotStatus.samePlayer(sender,player) or type(text) ~= 'string' or #text > 240 or
        now < p.sentAt or now-p.sentAt > 4 or generation ~= p.generation then return end
    local id, epoch, part, count, chunk = text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(.*)$')
    part, count = integer(part,1,8), integer(count,1,8)
    if id ~= p.id or tonumber(epoch) ~= generation or not part or not count or part > count or
        not chunk or #chunk > 160 then return end
    if (p.count and p.count ~= count) or (p.parts[part] and p.parts[part] ~= chunk) then
        self.pending, self.failure = nil, 'Conflicting training fragments; previous records retained.'
        return true
    end
    p.count, p.parts[part] = count, chunk
    for i = 1,count do if not p.parts[i] then return end end
    local snapshot = Plan.decode(table.concat(p.parts))
    self.pending = nil
    if not snapshot then self.failure = 'Invalid training reply; previous records retained.'; return true end
    self.failure = nil
    self.snapshot, self.completedAt = snapshot, now
    return true
end
