-- Read-only, self-correlated death/recovery observations. No game commands.
WoweeBotRecovery = {}
local Recovery=WoweeBotRecovery
Recovery.__index=Recovery
function Recovery.new() return setmetatable({},Recovery) end
local function number(s,lo,hi)
    if type(s)~='string' or not s:match('^%-?%d+$') then return end
    local n=tonumber(s)
    if n and n>=lo and n<=hi then return n end
end
local function fields(s)
    local out={};for v in (s..'\t'):gmatch('(.-)\t') do out[#out+1]=v end;return out
end
function Recovery.decode(body)
    if type(body)~='string' or #body>1024 then return end
    local lines={};for line in (body..'\n'):gmatch('(.-)\n') do lines[#lines+1]=line end
    local h=fields(lines[1] or '')
    if #h~=16 or h[1]~='1' or not (h[2]=='none' or h[2]:match('^%d+_%d+$')) or #h[2]>40 then return end
    local names={'phase','observed','deaths','distance','reclaim','health','maxHealth','mana','maxMana','attempt','attemptAt','stationary','combat','count'}
    local row={epoch=h[2],notes={}}
    for i,key in ipairs(names) do
        local lo=key=='distance' and -1 or 0
        local hi=key=='phase' and 10 or key=='attempt' and 5 or key=='combat' and 1 or key=='count' and 6 or
            key=='distance' and 20000000 or (key=='reclaim' or key=='stationary') and 604800 or 4294967295
        row[key]=number(h[i+2],lo,hi)
        if row[key]==nil then return end
    end
    if #lines~=row.count+1 or row.attempt==0 and row.attemptAt~=0 or row.attempt~=0 and row.attemptAt==0 then return end
    if row.epoch=='none' and (row.phase~=1 or row.count~=0) then return end
    local sequence=0
    for i=2,#lines do
        local f=fields(lines[i]);if #f~=5 then return end
        local note={sequence=number(f[1],1,4294967295),at=number(f[2],1,4294967295),kind=number(f[3],1,4),
                    deaths=number(f[4],0,4294967295),seconds=number(f[5],0,604800)}
        if not note.sequence or not note.at or not note.kind or not note.deaths or not note.seconds or note.sequence<=sequence then return end
        if note.kind==1 and (note.deaths<10 or note.seconds~=0) or note.kind==2 and note.seconds<120 or
            note.kind==3 and note.seconds<10 or note.kind==4 and note.seconds~=0 then return end
        sequence=note.sequence;row.notes[#row.notes+1]=note
    end
    return row
end
function Recovery:capability(prefix,text,sender,player,control)
    if prefix~='WIDLERCAP' or not WoweeBotStatus.samePlayer(sender,player) or type(text)~='string' or #text>100 then return end
    local id,generation=text:match('^1\t([%w_-]+)\t(%d+)\trecovery_1$')
    if id and id==control.ackId and tonumber(generation)==control.generation then self.supported=true end
end
function Recovery:expect(id,generation,now)
    if self.supported then self.pending={id=id,generation=generation,sentAt=now,parts={}};self.failure=nil end
end
function Recovery:expire(now,generation)
    local p=self.pending
    if p and (now<p.sentAt or now-p.sentAt>4 or generation~=p.generation) then
        self.pending,self.failure=nil,'Recovery reply missing or incomplete'
    end
end
function Recovery:receive(prefix,text,sender,player,generation,now)
    local p=self.pending
    if prefix~='WIDLEREC' or not self.supported or not p or not WoweeBotStatus.samePlayer(sender,player) or
        type(text)~='string' or #text>240 or generation~=p.generation or now<p.sentAt or now-p.sentAt>4 then return end
    local id,epoch,part,count,chunk=text:match('^1\t([%w_-]+)\t(%d+)\t(%d+)\t(%d+)\t(.*)$')
    part,count=number(part,1,8),number(count,1,8)
    if id~=p.id or tonumber(epoch)~=generation or not part or not count or part>count or not chunk or #chunk>160 then return end
    if p.count and p.count~=count or p.parts[part] and p.parts[part]~=chunk then
        self.pending,self.failure=nil,'Conflicting recovery fragments';return true
    end
    p.count,p.parts[part]=count,chunk
    for i=1,count do if not p.parts[i] then return end end
    local row=Recovery.decode(table.concat(p.parts));self.pending=nil
    if not row then self.failure='Invalid recovery report';return true end
    self.snapshot,self.completedAt,self.failure=row,now,nil
    return true
end
function Recovery:fresh(now)
    return self.snapshot and not self.failure and self.completedAt and now>=self.completedAt and now-self.completedAt<=20
end
local phases={
    [2]='Dead; awaiting release or resurrection',[3]='Ghost; choosing a recovery action',
    [4]='Waiting for the corpse reclaim delay',[5]='Within corpse range; awaiting a reclaim attempt',
    [6]='Spirit-healer recovery attempt',[7]='Ghost; corpse is unavailable',[8]='Corpse is on another map or instance',
}
function Recovery:currentWork(now)
    if not self:fresh(now) or not phases[self.snapshot.phase] then return end
    local s=self.snapshot
    if s.phase~=7 and s.phase~=8 then
        if s.attempt==5 then return 'Recovery: Resurrection acceptance sent; awaiting alive state' end
        if s.attempt==3 then return 'Recovery: Corpse reclaim attempted; awaiting alive state' end
        if s.phase==3 and s.attempt==2 then return 'Recovery: Attempting to reach the corpse' end
    end
    return 'Recovery: '..phases[s.phase]
end
function Recovery:summary(now)
    if not self.supported then return end
    if self.failure then return 'Recovery status unavailable: '..self.failure end
    if not self:fresh(now) then
        if self.snapshot then return 'Recovery status is stale; querying again' end
        return
    end
    local s=self.snapshot
    if s.phase==0 or s.phase==1 then return end
    if s.phase==9 or s.phase==10 then
        return (s.combat==1 and 'Alive again; in combat' or 'Alive again')..' - HP '..s.health..'/'..s.maxHealth..
            (s.maxMana>0 and ('; mana '..s.mana..'/'..s.maxMana) or '')
    end
    local parts={}
    if s.distance>=0 then parts[#parts+1]='Corpse '..s.distance..' yd' end
    if s.reclaim>0 then parts[#parts+1]='Reclaim delay '..s.reclaim..'s at last update' end
    parts[#parts+1]='Bot death counter '..s.deaths
    if s.stationary>=120 then parts[#parts+1]='No recovery movement observed for '..s.stationary..'s; see Notes' end
    return table.concat(parts,' - ')
end
