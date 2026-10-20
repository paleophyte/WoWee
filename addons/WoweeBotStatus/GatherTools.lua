-- Independent observations of autonomous gathering skipped for a missing carried tool.
-- Receiving a report never changes control, inventory or the planner.
WoweeBotGatherTools = {}
local GatherTools = WoweeBotGatherTools
GatherTools.__index = GatherTools
function GatherTools.new() return setmetatable({}, GatherTools) end
local function number(s, lo, hi)
    if type(s) ~= 'string' or not s:match('^%d+$') then return end
    local n=tonumber(s);if n and n>=lo and n<=hi then return n end
end
local function fields(s)
    local out={};for v in (s..'\t'):gmatch('(.-)\t') do out[#out+1]=v end;return out
end
function GatherTools.decode(body)
    if type(body)~='string' or #body>4096 then return end
    local lines={};for line in (body..'\n'):gmatch('(.-)\n') do lines[#lines+1]=line end
    local h=fields(lines[1] or '')
    if #h~=3 or h[1]~='1' or #h[2]>32 or
        not (h[2]=='none' or h[2]:match('^%d+_%d+$')) then return end
    local count=number(h[3],0,16)
    if not count or #lines~=count+1 or h[2]=='none' and count~=0 then return end
    local rows,previous={},nil
    for i=2,#lines do
        local f=fields(lines[i]);if #f~=4 then return end
        local row={sequence=number(f[1],1,4294967295),at=number(f[2],1,4294967295),
            item=number(f[3],1,4294967295),missing=number(f[4],0,1)}
        if not row.sequence or not row.at or not (row.item==7005 or row.item==2901) or
            row.missing==nil or previous and row.sequence~=previous+1 then return end
        rows[#rows+1],previous=row,row.sequence
    end
    return {epoch=h[2],rows=rows}
end
function GatherTools:capability(prefix,text,sender,player,control)
    if prefix~='WIDLETCAP' or not WoweeBotStatus.samePlayer(sender,player) or type(text)~='string' or #text>100 then return end
    local id,generation=text:match('^1\t([%w_-]+)\t(%d+)\tgather_tools_1$')
    if id and id==control.ackId and tonumber(generation)==control.generation then self.supported=true end
end
function GatherTools:expect(id,generation,now)
    if self.supported then self.pending={id=id,generation=generation,sentAt=now,parts={}};self.failure=nil end
end
function GatherTools:expire(now,generation)
    local p=self.pending
    if p and (now<p.sentAt or now-p.sentAt>4 or generation~=p.generation) then
        self.pending,self.failure=nil,'Gathering-tool report missing or incomplete'
    end
end
function GatherTools:receive(prefix,text,sender,player,generation,now)
    local p=self.pending
    if prefix~='WIDLETOOLS' or not self.supported or not p or not WoweeBotStatus.samePlayer(sender,player) or
        type(text)~='string' or #text>240 or generation~=p.generation or now<p.sentAt or now-p.sentAt>4 then return end
    local id,epoch,part,count,chunk=text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(.*)$')
    part,count=number(part,1,32),number(count,1,32)
    if id~=p.id or tonumber(epoch)~=generation or not part or not count or part>count or not chunk or #chunk>160 then return end
    if p.count and p.count~=count or p.parts[part] and p.parts[part]~=chunk then
        self.pending,self.failure=nil,'Conflicting gathering-tool fragments';return true
    end
    p.count,p.parts[part]=count,chunk
    for i=1,count do if not p.parts[i] then return end end
    local row=GatherTools.decode(table.concat(p.parts));self.pending=nil
    if not row then self.failure='Invalid gathering-tool report';return true end
    self.snapshot,self.completedAt,self.failure=row,now,nil
    return true
end
