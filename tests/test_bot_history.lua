if not WoweeBotStatus then dofile('addons/WoweeBotStatus/Status.lua') end
if not WoweeBotHistory then dofile('addons/WoweeBotStatus/History.lua') end
local H=WoweeBotHistory
local function hex(s) return (s:gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
local function entry(seq,kind,quantity,text)
    return table.concat({seq,1792234000,kind or 'purchase',4496,quantity or 1,789,hex(text or 'Bought Brown Linen Bag x1 from Dane Winslow')},'\t')
end
local body='1792234000_1\t2\n'..entry(1)..'\n'..entry(2,'learned',1,'Learned Holy Light at Brother Wilhelm')
assert(H.decode(body) and #H.decode(body).entries==2)
assert(H.decode('none\t0') and not H.decode('none\t1\n'..entry(1)))
assert(not H.decode(body..'\n') and not H.decode('x\t0') and not H.decode(string.rep('a',4097)))
assert(not H.decode('1_1\t2\n'..entry(1)..'\n'..entry(3)))
assert(not H.decode('1_1\t1\n'..entry(1,'learned',2)))
assert(not H.decode('1_1\t1\n'..entry(0)))
assert(not H.decode('1_1\t1\n'..entry(1,'invented')))
assert(not H.decode(('1_1\t1\n'..entry(1)):gsub(hex('Bought Brown Linen Bag x1 from Dane Winslow'),'0a')))
local model=H.new()
local control={ackId='ack',generation=7}
model:capability('WIDLECAP','1\tack\t7\tactivity_1,history_1','Other','Jizzle',control)
assert(not model.supported)
model:capability('WIDLECAP','1\told\t7\thistory_1','Jizzle','Jizzle',control)
assert(not model.supported)
model:capability('WIDLECAP','1\tack\t7\tactivity_1,history_1','Jizzle','Jizzle',control)
assert(model.supported)
local function packet(id,part,count,text,generation)
    return '1\t'..id..'\t'..(generation or 7)..'\t'..part..'\t'..count..'\t'..text
end
local function receive(text,now,sender,generation)
    return model:receive('WIDLEHIST',text,sender or 'Jizzle','Jizzle',generation or 7,now or 1)
end
local function deliver(data,id,at)
    model:expect(id,7,at)
    local count=math.ceil(#data/160)
    for i=count,1,-1 do
        local text=packet(id,i,count,data:sub((i-1)*160+1,i*160))
        assert(not receive(text,at,'Other'))
        receive(text,at)
        if i>1 then assert(model.pending) end
    end
    assert(not model.pending)
end
deliver(body,'req',1)
assert(#model.entries==2 and model.entries[1].kind=='purchase' and model.entries[2].kind=='learned')
deliver(body,'req2',2)
assert(#model.entries==2 and model.missed==0) -- repeated polling is not another purchase
assert(model.entries[1].receivedAt==1) -- polling must not make an old fact look newly received
model:expect('req3',7,3)
assert(not receive(packet('old',1,1,'none\t0'),3))
assert(not receive(packet('req3',1,1,'none\t0',6),3))
receive(packet('req3',1,2,'a'),3)
assert(receive(packet('req3',1,2,'b'),3) and model.failure and #model.entries==2)
model:expect('req4',7,4);model:expire(9,7)
assert(model.failure and not model.pending and #model.entries==2)
model:expect('req5',7,10);model:expire(10,8)
assert(not model.pending)
model:expect('req6',7,10);model:expire(9,7)
assert(not model.pending)
deliver('1792234000_1\t1\n'..entry(5,'turnin',1,'Turned in A Bundle of Trouble'),'req7',11)
assert(#model.entries==3 and model.missed==2 and model.entries[3].sequence==5)
deliver('1792234000_1\t1\n'..entry(5,'turnin',1,'Turned in A Bundle of Trouble'),'req8',12)
assert(#model.entries==3 and model.missed==2)
-- Use the current complete server feature list, not the older short fixture.
-- The server check also supplies the compiled IdleSend body from PlayerbotMgr.
local longId=string.rep('x',32)
control={ackId=longId,generation=4294967295}
local features='activity_1,social_1,requests_1,warning_1,fishing_1,collection_1,collection_stop_1,history_1,training_1,training_plan_1,nav_1'
local caps='1\t'..longId..'\t4294967295\t'..features
assert(#caps==170)
if arg and arg[1]=='--capability-fixture' then
    local file=assert(io.open(arg[2],'rb'));local actual=file:read('*a');file:close()
    assert(actual==caps,'compiled server capability must match the complete fixture')
    caps=actual
end
local readers={}
for _,name in ipairs({'Control','Activity','History','Training','TrainingPlan','Travel'}) do
    local key='WoweeBot'..name
    if not _G[key] then dofile('addons/WoweeBotStatus/'..name..'.lua') end
    readers[#readers+1]={name=name,model=_G[key]}
end
local function receives(reader,text,sender,authority,prefix)
    local fresh=reader.model.new('captest')
    if reader.name=='Control' then
        fresh.ackId,fresh.generation=control.ackId,control.generation
        fresh:capability(prefix or 'WIDLECAP',text,sender or 'Jizzle','Jizzle')
        return fresh.requestsSupported and fresh.socialSupported and fresh.fishingSupported
    end
    fresh:capability(prefix or 'WIDLECAP',text,sender or 'Jizzle','Jizzle',authority or control)
    return fresh.supported
end
local rejected={}
for _,reader in ipairs(readers) do
    if not receives(reader,caps) then rejected[#rejected+1]=reader.name end
end
assert(#rejected==0,'server capability frame rejected by: '..table.concat(rejected,', '))
for _,reader in ipairs(readers) do
    local maxFrame=caps..','..string.rep('x',69)
    assert(#maxFrame==240 and receives(reader,maxFrame),reader.name..' rejects bounded future features')
    assert(not receives(reader,maxFrame..'x'),reader.name..' accepts oversized capability')
    assert(not receives(reader,caps,'Other'),reader.name..' accepts another sender')
    assert(not receives(reader,caps:gsub(longId,string.rep('y',32))),reader.name..' accepts another request')
    assert(not receives(reader,caps:gsub('4294967295','4294967294')),reader.name..' accepts stale generation')
    assert(not receives(reader,caps,nil,nil,'OTHER'),reader.name..' accepts another prefix')
    assert(not receives(reader,caps..'\t'),reader.name..' accepts extra fields')
end
for epoch=2,10 do
    local records={}
    for i=1,16 do records[i]=entry(i) end
    model:apply(H.decode('1792234000_'..epoch..'\t16\n'..table.concat(records,'\n')),13)
end
assert(#model.entries==64 and #model.epochs==8)
if arg and arg[1]=='--server-fixture' then
    local file=assert(io.open(arg[2],'rb'));local actual=file:read('*a');file:close()
    local decoded=assert(H.decode(actual),'actual server serializer must decode')
    assert(#decoded.entries==16 and decoded.entries[1].sequence==5 and decoded.entries[16].sequence==20)
    assert(decoded.entries[1].quantity==4294967295 and #decoded.entries[1].text<=80)
    local actualModel=H.new();actualModel:apply(decoded,1)
    assert(#actualModel.entries==16 and actualModel.missed==4)
end
print('Bounded correlated history, deduplication, gaps, expiry and capability scenarios passed')
