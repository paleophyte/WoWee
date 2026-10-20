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
assert(c.completedAt == 1 and table.concat(c:diagnostic(2),'\n'):find('1s ago',1,true))
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
assert(c.lastRejected and #table.concat(c:diagnostic(12),'\n') < 1000)
-- A valid server snapshot can have no successful action yet. Do not attach
-- a fresh timestamp to nil: the display must safely omit that optional line.
fields[17]='-'
local emptyAction = Activity.decode(table.concat(fields, '\t'))
assert(emptyAction and emptyAction.executed == '')
Activity.apply(model, emptyAction, 42)
assert(model.action == nil and model.actionAt == nil)
-- A retained training destination must not masquerade as the selected local
-- task. Changing that task must leave the planner/objective data untouched.
model.aiState, model.goal, model.kind, model.status = 'non_combat', 'Train at Lee Brown', 'trainer', 'traveling'
model.interactionName, model.interactionTask = 'Vendor', 'rpg buy'
assert(Activity.currentWork(model) == 'Selected task: Buy items - Vendor')
assert(model.goal == 'Train at Lee Brown' and model.kind == 'trainer')
model.interactionName, model.interactionTask = 'Quest giver', 'rpg end quest'
assert(Activity.currentWork(model) == 'Selected task: Turn in quests - Quest giver')
model.interactionName, model.interactionTask = 'Warlock trainer', 'rpg train'
assert(Activity.currentWork(model) == 'Selected task: Train skills - Warlock trainer')
model.interactionTask = 'rpg'
assert(Activity.currentWork(model):find('task not reported', 1, true))
model.interactionTask = nil
assert(Activity.currentWork(model):find('task not reported', 1, true))
model.aiState = 'combat'
assert(Activity.currentWork(model) == 'Now: In combat')
model.aiState = 'dead'
assert(Activity.currentWork(model) == 'Now: Dead')
model.aiState, model.interactionName = 'non_combat', nil
assert(Activity.currentWork(model) == 'Now: Traveling along the travel plan')
model.status = 'cooldown'
assert(Activity.currentWork(model) == 'Now: Waiting before choosing the next activity')
assert(Activity.actionName('rpg buy') == 'Buy items')
assert(Activity.actionName('new unknown action') == 'new unknown action')
assert(Activity.actionName(nil) == nil and Activity.actionName('') == nil)
print('Typed activity snapshot scenarios passed')

-- Negotiated selected-skinning work must never infer authority from goal text.
do
local A,S = WoweeBotActivity,WoweeBotStatus
local ctl = {ackId='control_1',generation=2}
local caps = {
    {'WIDLECAP','activity_1'}, {'WIDLEGCAP','grind_activity_1'}, {'WIDLEHCAP','skinning_hunt_1'}
}
local function cap(a,which,id,gen,sender)
    local c=caps[which]
    return a:capability(c[1],table.concat({'1',id or ctl.ackId,gen or ctl.generation,c[2]},'\t'),
        sender or 'Idlelabqfix','Idlelabqfix',ctl)
end
local function body(kind,status)
    return table.concat({'bot','non_combat',kind or 'skinning',status or 'working','0','-1','none',
        '721','2','536b696e20526162626974','-','-','none','0','-','-','-','0','0'},'\t')
end
local function receive(a,wire,now,gen,sender,id)
    local packet=table.concat({'1',id or a.pending.id,gen or 2,'1','1',wire},'\t')
    return a:receive('WIDLEACT',packet,sender or 'Idlelabqfix','Idlelabqfix',gen or 2,now or 1)
end
for _,order in ipairs({{1,2,3},{1,3,2},{2,1,3},{2,3,1},{3,1,2},{3,2,1}}) do
    local a=A.new('all')
    for i,which in ipairs(order) do
        assert(cap(a,which));assert((a.skinningSupported == true)==(i==3))
    end
    assert(a:request(2,0)=='.bot idle all_a1 snapshot 2 grind_2')
    local snap=assert(receive(a,body()));assert(snap.skinningHunt and snap.grindActivity)
    local m=S.new();A.apply(m,snap,1)
    local desired,reason=m:grindDecision(2,nil)
    assert(desired==true and reason=='hunting selected skinning prey')
    assert(not m:currentQuest(2) and m.goal=='Skin Rabbit')
    for _,status in ipairs({'none','preparing','ready','traveling','cooldown','expired'}) do
        snap.status=status;A.apply(m,snap,1);assert(m:grindDecision(2,nil)==false)
    end
    snap.status='working'
    for _,state in ipairs({'dead','reaction','unknown'}) do
        snap.state=state;A.apply(m,snap,1);assert(m:grindDecision(2,nil)==nil)
    end
    snap.state='combat';A.apply(m,snap,1);assert(m:grindDecision(2,nil)==true)
    assert(m:grindDecision(30,nil)==nil)
    -- A newer accepted control request cannot reuse the previous extension.
    ctl.ackId='control_2';assert(cap(a,1));assert(not a.skinningSupported)
    assert(a:request(2,3)=='.bot idle all_a2 snapshot 2')
    assert(cap(a,2));assert(a:request(2,4)=='.bot idle all_a3 snapshot 2 grind_1')
    assert(cap(a,3));assert(a:request(2,5)=='.bot idle all_a4 snapshot 2 grind_2')
    assert(a:request(3,6)=='.bot idle all_a5 snapshot 3')
    ctl.ackId='control_1'
end
-- Every missing capability falls back without granting skinning authority.
for missing=1,3 do
    local a=A.new('missing')
    for i=1,3 do if i~=missing then assert(cap(a,i)) end end
    assert(not a.skinningSupported)
    a:request(2,0)
    local snap,changed=receive(a,body())
    assert(not snap and changed and a.failure=='Unrequested skinning activity')
end
local a=A.new('strict')
for i=1,3 do assert(cap(a,i)) end
assert(not cap(a,3,'stale'));assert(not cap(a,3,nil,3));assert(not cap(a,3,nil,nil,'Other'))
a:request(2,0)
assert(not receive(a,body(),1,2,'Other'))
assert(not receive(a,body(),1,2,nil,'wrong_request'))
assert(not receive(a,body(),1,3))
assert(not receive(a,body(),5))
assert(a:expire(5,2) and not a.pending)
-- Decoding valid bytes alone does not authorize a strategy change.
local raw=assert(A.decode(body()));local m=S.new();A.apply(m,raw,1)
assert(m:grindDecision(2,nil)==false)
for _,mutation in ipairs({
    {'\t0\t%-1\tnone\t721\t','\t0\t-1\tnone\t-721\t'},
    {'\t0\t%-1\tnone\t721\t','\t33\t-1\tnone\t721\t'},
    {'\t0\t%-1\tnone\t721\t','\t0\t0\tnone\t721\t'},
    {'\t0\t%-1\tnone\t721\t','\t0\t-1\tkill\t721\t'},
    {'^bot','manual'}
}) do assert(not A.decode((body():gsub(mutation[1],mutation[2])))) end
-- Conflicting fragments cannot update the model atomically.
a:request(2,6)
local id=a.pending.id
assert(not a:receive('WIDLEACT','1\t'..id..'\t2\t1\t2\tbot','Idlelabqfix','Idlelabqfix',2,7))
local _,changed=a:receive('WIDLEACT','1\t'..id..'\t2\t1\t2\tbad','Idlelabqfix','Idlelabqfix',2,7)
assert(changed and a.failure=='Conflicting activity fragments')
print('Skinning capability orders, downgrade, selected work, lifecycle and frame authority pass')

end
