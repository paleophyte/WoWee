WoweeBotTravel = {}
local Travel = WoweeBotTravel
Travel.__index = Travel
function Travel.new() return setmetatable({}, Travel) end
local function integer(s, minimum, maximum)
    if type(s) ~= 'string' or not s:match('^%d+$') then return end
    local n=tonumber(s)
    if n and n>=minimum and n<=maximum then return n end
end
local function fields(line)
    local result={}
    for field in (line..'\t'):gmatch('(.-)\t') do result[#result+1]=field end
    return result
end
Travel.waitNames = {'combat','a spell or channel','a flight','a transport','the party',
    'an open loot transaction','death recovery','a teleport','movement to become available',
    'an open trade','the movement policy'}
function Travel.decode(body)
    if type(body)~='string' or #body>512 then return end
    local first,last=body:match('^([^\n]+)\n([^\n]+)$')
    if not first then return end
    local h,w=fields(first),fields(last)
    if #h~=8 or #w~=7 or h[1]~='1' then return end
    local row={state=h[2],observed=integer(h[3],0,4294967295),reason=integer(h[4],0,11),
        seconds=integer(h[5],0,604800),rebuilds=integer(h[6],0,1),
        remaining=integer(h[7],0,120),untilAt=integer(h[8],0,4294967295)}
    if not row.observed or not row.reason or not row.seconds or not row.rebuilds or
        not row.remaining or not row.untilAt then return end
    if row.state=='idle' or row.state=='manual' then
        if row.reason~=0 or row.seconds~=0 or row.rebuilds~=0 or row.remaining~=0 or row.untilAt~=0 then return end
    elseif row.state=='paused' then
        if row.remaining==0 or row.untilAt==0 or row.rebuilds~=1 then return end
    elseif row.state=='waiting' then
        if row.reason==0 or row.remaining~=0 or row.untilAt~=0 then return end
    elseif row.state=='rebuilding' then
        if row.reason~=0 or row.rebuilds~=1 or row.seconds<45 or row.remaining~=0 or row.untilAt~=0 then return end
    elseif row.state=='stalled' then
        if row.reason~=0 or row.seconds<5 or row.remaining~=0 or row.untilAt~=0 then return end
    elseif row.state=='traveling' then
        if row.reason~=0 or row.seconds>=5 or row.remaining~=0 or row.untilAt~=0 then return end
    else return end
    if row.observed==0 and row.state~='idle' and row.state~='manual' then return end
    local warning={at=integer(w[1],0,4294967175),sequence=integer(w[2],0,4294967295),
        kind=integer(w[3],0,2),reason=integer(w[4],0,11),seconds=integer(w[5],0,604800),
        untilAt=integer(w[6],0,4294967295)}
    for _,key in ipairs({'at','sequence','kind','reason','seconds','untilAt'}) do
        if not warning[key] then return end
    end
    if warning.kind==0 then
        if warning.at~=0 or warning.sequence~=0 or warning.reason~=0 or warning.seconds~=0 or
            warning.untilAt~=0 or w[7]~='-' then return end
    else
        if warning.at==0 or warning.sequence==0 then return end
        if warning.kind==1 then
            if warning.reason~=0 or warning.seconds<90 or warning.untilAt~=warning.at+120 then return end
        elseif warning.reason==0 or warning.seconds<120 or warning.untilAt~=0 then return end
        if w[7]=='-' then warning.goal='the selected destination'
        else
            if #w[7]>80 or #w[7]%2~=0 or w[7]:find('[^%x]') then return end
            warning.goal=w[7]:gsub('..',function(pair) return string.char(tonumber(pair,16)) end)
            if warning.goal:find('[%c]') then return end
        end
        row.warning=warning
    end
    return row
end
function Travel:capability(prefix,text,sender,player,control)
    if prefix~='WIDLECAP' or not WoweeBotStatus.samePlayer(sender,player) or
        type(text)~='string' or #text>160 then return end
    local id,generation,features=text:match('^1\t([%w_-]+)\t(%d+)\t([%w_,]+)$')
    if not id or id~=control.ackId or tonumber(generation)~=control.generation then return end
    self.supported=(','..features..','):find(',nav_1,',1,true)~=nil
    if not self.supported then self.pending,self.snapshot=nil,nil end
end
function Travel:expect(id,generation,now)
    if self.supported then
        self.pending={id=id,generation=generation,sentAt=now,parts={}}
        self.failure=nil
    end
end
function Travel:expire(now,generation)
    local p=self.pending
    if p and (now<p.sentAt or now-p.sentAt>4 or generation~=p.generation) then
        self.pending,self.failure=nil,'Travel reply missing or incomplete'
    end
end
function Travel:receive(prefix,text,sender,player,generation,now)
    local p=self.pending
    if prefix~='WIDLETRAVEL' or not self.supported or not p or
        not WoweeBotStatus.samePlayer(sender,player) or type(text)~='string' or #text>240 or
        now<p.sentAt or now-p.sentAt>4 or generation~=p.generation then return end
    local id,epoch,part,count,chunk=text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(.*)$')
    part,count=integer(part,1,4),integer(count,1,4)
    if id~=p.id or tonumber(epoch)~=generation or not part or not count or part>count or
        not chunk or #chunk>160 then return end
    if (p.count and p.count~=count) or (p.parts[part] and p.parts[part]~=chunk) then
        self.pending,self.failure=nil,'Conflicting travel fragments';return true
    end
    p.count,p.parts[part]=count,chunk
    for i=1,count do if not p.parts[i] then return end end
    local row=Travel.decode(table.concat(p.parts))
    self.pending=nil
    if not row then self.failure='Invalid travel report';return true end
    self.snapshot,self.completedAt,self.failure=row,now,nil
    return true
end
function Travel:summary(now)
    local row=self.snapshot
    if self.failure then return 'Travel status unavailable: '..self.failure end
    if not row or not self.completedAt then return end
    if now<self.completedAt or now-self.completedAt>20 then return 'Travel status is stale; querying again' end
    if row.state=='paused' then
        local remaining=math.max(0,row.remaining-math.floor(now-self.completedAt))
        return remaining>0 and ('Travel paused after a route rebuild made no progress ('..remaining..'s remaining)') or
            'Travel pause ending; waiting for server status'
    elseif row.state=='waiting' then
        return 'Travel waiting for '..Travel.waitNames[row.reason]..'; observed no movement for '..row.seconds..'s'
    elseif row.state=='rebuilding' then
        return 'No travel movement observed; rebuilding the route'
    elseif row.state=='stalled' then
        return (row.rebuilds==1 and 'Route rebuilt; ' or 'Travel: ')..'no movement observed for '..row.seconds..'s'
    end
end
