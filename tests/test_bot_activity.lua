if not WoweeBotStatus then dofile('addons/WoweeBotStatus/Status.lua') end
if not WoweeBotActivity then dofile('addons/WoweeBotStatus/Activity.lua') end
local Activity = WoweeBotActivity
local function hex(s) return s == '' and '-' or (s:gsub('.', function(c) return string.format('%02x', c:byte()) end)) end
local fields = {'bot','non_combat','quest_objective','working','33','0','creature_loot','80','3',
    hex('Candle from Kobold'),hex('Quest: café'),hex('Elwynn'),'npc','789',hex('Dane Winslow'),hex('rpg ai chat'),hex('move to rpg target'),'0','1'}
local body = table.concat(fields, '\t')
local c = Activity.new('test')
local control = {ackId='control_1', generation=2}
assert(not c:capability('WIDLECAP','1\tcontrol_1\t2\tactivity_1','Other','Jizzle',control))
assert(not c:capability('WIDLECAP','1\told\t2\tactivity_1','Jizzle','Jizzle',control))
assert(not c:capability('WIDLECAP','1\tcontrol_1\t1\tactivity_1','Jizzle','Jizzle',control))
assert(c:capability('WIDLECAP','1\tcontrol_1\t2\tactivity_1','Jizzle','Jizzle',control))
assert(c:capability('WIDLECAP','1\tcontrol_1\t2\tfuture_2,activity_1','Jizzle','Jizzle',control))
assert(c.supported)
assert(c:request(2,0) == '.bot idle test_a1 snapshot 2')
local function packet(id, generation, part, total, chunk)
    return '1\t'..id..'\t'..generation..'\t'..part..'\t'..total..'\t'..chunk
end
local function receive(text, now, sender, epoch)
    return c:receive('WIDLEACT',text,sender or 'Jizzle','Jizzle',epoch or 2,now or 1)
end
assert(not receive(packet('wrong',2,1,1,body:sub(1,160))))
assert(not receive(packet('test_a1',1,1,1,body:sub(1,160))))
local total = math.ceil(#body/160)
local result
for i=total,1,-1 do -- reversed delivery still commits only the complete snapshot
    local text = packet('test_a1',2,i,total,body:sub((i-1)*160+1,i*160))
    if i == total then assert(not receive(text,1,'Other')); assert(not receive(text)) end
    result = receive(text)
    if i > 1 then assert(not result) end
end
assert(result and result.questTitle == 'Quest: café' and result.interactionName == 'Dane Winslow')
assert(not c.pending and not receive(packet('test_a1',2,1,total,body:sub(1,160))))
local model = WoweeBotStatus.new()
Activity.apply(model,result,1)
assert(model:currentQuest(1) == 33)
assert(model:grindDecision(1,{complete=false}))
assert(not model:grindDecision(1,{complete=true}))
for _, kind in ipairs({'vendor_item','object_loot','spell','interact'}) do
    result.objectiveType=kind; Activity.apply(model,result,1)
    assert(not model:grindDecision(1,{complete=false}))
end
fields[5]='not-a-number'; assert(not Activity.decode(table.concat(fields,'\t'))); fields[5]='33'
fields[6]='4'; assert(not Activity.decode(table.concat(fields,'\t'))); fields[6]='0'
fields[15]='0a'; assert(not Activity.decode(table.concat(fields,'\t'))); fields[15]=hex('Dane Winslow')
fields[15]='zz'; assert(not Activity.decode(table.concat(fields,'\t'))); fields[15]=hex('Dane Winslow')
assert(not Activity.decode(body..'\textra'))
c:request(2,2)
assert(not receive(packet('test_a2',2,1,2,'abc'),3))
local _, changed = receive(packet('test_a2',2,1,2,'def'),3)
assert(changed and c.failure and not c.pending)
c:request(2,4); assert(c:expire(9,2) and c.failure)
c:request(2,10); assert(c:expire(10,3))
c:request(2,11)
local _, changed = receive('1\ttest_a5\t3\terror\tstale_generation',12)
assert(changed and c.failure:find('stale_generation',1,true))
print('Typed activity snapshot scenarios passed')
