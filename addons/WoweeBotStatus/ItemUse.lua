-- Independent observations of pickups skipped at a known unique-item cap.
-- Receiving a report never changes control, inventory or the planner.
WoweeBotItemUse = {}
local ItemUse = WoweeBotItemUse
ItemUse.__index = ItemUse
function ItemUse.new() return setmetatable({}, ItemUse) end
local function number(s, lo, hi)
    if type(s) ~= 'string' or not s:match('^%d+$') then return end
    local n=tonumber(s);if n and n>=lo and n<=hi then return n end
end
local function fields(s)
    local out={};for v in (s..'\t'):gmatch('(.-)\t') do out[#out+1]=v end;return out
end
local function unhex(s)
    if not s or s=='' or #s>160 or #s%2~=0 or s:find('[^0-9a-f]') then return end
    local value=s:gsub('..',function(pair)return string.char(tonumber(pair,16))end)
    if value:find('[%z\1-\31\127]') then return end
    return value
end
function ItemUse.decode(body)
    if type(body)~='string' or #body>4096 then return end
    local lines={};for line in (body..'\n'):gmatch('(.-)\n') do lines[#lines+1]=line end
    local h=fields(lines[1] or '')
    if #h~=3 or h[1]~='1' or #h[2]>32 or
        not (h[2]=='none' or h[2]:match('^%d+_%d+$')) then return end
    local count=number(h[3],0,8)
    if not count or #lines~=count+1 or h[2]=='none' and count~=0 then return end
    local rows,seen={},{}
    for i=2,#lines do
        local f=fields(lines[i]);if #f~=9 then return end
        local row={at=number(f[1],1,4294967295),object=number(f[2],1,4294967295),
            item=number(f[3],1,4294967295),spell=number(f[4],1,4294967295),
            count=number(f[5],0,4294967295),limit=number(f[6],1,2147483647),
            blocked=number(f[7],0,1),objectName=unhex(f[8]),itemName=unhex(f[9])}
        for _,key in ipairs({'at','object','item','spell','count','limit','blocked','objectName','itemName'}) do
            if row[key]==nil then return end
        end
        if seen[row.item] or (row.blocked==1 and row.count<row.limit) or
            (row.blocked==0 and row.count>=row.limit) then return end
        seen[row.item]=true;rows[#rows+1]=row
    end
    return {epoch=h[2],rows=rows}
end
function ItemUse:capability(prefix,text,sender,player,control)
    if prefix~='WIDLEICAP' or not WoweeBotStatus.samePlayer(sender,player) or type(text)~='string' or #text>100 then return end
    local id,generation=text:match('^1\t([%w_-]+)\t(%d+)\titem_use_1$')
    if id and id==control.ackId and tonumber(generation)==control.generation then self.supported=true end
end
function ItemUse:expect(id,generation,now)
    if self.supported then self.pending={id=id,generation=generation,sentAt=now,parts={}};self.failure=nil end
end
function ItemUse:expire(now,generation)
    local p=self.pending
    if p and (now<p.sentAt or now-p.sentAt>4 or generation~=p.generation) then
        self.pending,self.failure=nil,'Pickup-limit report missing or incomplete'
    end
end
function ItemUse:receive(prefix,text,sender,player,generation,now)
    local p=self.pending
    if prefix~='WIDLEITEM' or not self.supported or not p or not WoweeBotStatus.samePlayer(sender,player) or
        type(text)~='string' or #text>240 or generation~=p.generation or now<p.sentAt or now-p.sentAt>4 then return end
    local id,epoch,part,count,chunk=text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(.*)$')
    part,count=number(part,1,32),number(count,1,32)
    if id~=p.id or tonumber(epoch)~=generation or not part or not count or part>count or not chunk or #chunk>160 then return end
    if p.count and p.count~=count or p.parts[part] and p.parts[part]~=chunk then
        self.pending,self.failure=nil,'Conflicting pickup-limit fragments';return true
    end
    p.count,p.parts[part]=count,chunk
    for i=1,count do if not p.parts[i] then return end end
    local row=ItemUse.decode(table.concat(p.parts));self.pending=nil
    if not row then self.failure='Invalid pickup-limit report';return true end
    self.snapshot,self.completedAt,self.failure=row,now,nil
    return true
end
