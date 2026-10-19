-- Real frame + correlated snapshots: local maintenance may change without
-- changing the retained planner destination. Selection is never a receipt.
do
    local oldClock, oldUnit, oldSend = GetTime, UnitName, SendChatMessage
    local clock, sent = oldClock(), {}
    GetTime = function() return clock end
    UnitName = function(unit) if unit == 'player' then return 'Jizzle' end return oldUnit(unit) end
    SendChatMessage = function(command) sent[#sent + 1] = command end
    local event = WoweeBotController:GetScript('OnEvent')
    local tick = function() WoweeBotController:GetScript('OnUpdate')() end
    local function receive(prefix, text)
        event(WoweeBotController, 'CHAT_MSG_ADDON', prefix, text, 'WHISPER', 'Jizzle')
    end
    local function hex(text)
        return text == '' and '-' or text:gsub('.', function(c) return string.format('%02x', c:byte()) end)
    end
    local function lastId(verb)
        for i = #sent, 1, -1 do
            local id = sent[i]:match('^%.bot idle ([%w_-]+) ' .. verb)
            if id then return id end
        end
        error('Missing ' .. verb .. ' request')
    end
    event(WoweeBotController, 'PLAYER_ENTERING_WORLD')
    tick()
    local id = lastId('status')
    receive('WIDLE', '1\t' .. id .. '\tok\tbot\t13\t0\tnone')
    receive('WIDLECAP', '1\t' .. id .. '\t13\tactivity_1')
    local function snapshot(npc, task, executed, state)
        clock = clock + 8
        tick()
        receive('WIDLE', '1\t' .. lastId('status') .. '\tok\tbot\t13\t0\tnone')
        tick()
        local request = lastId('snapshot')
        local body = table.concat({'bot', state or 'non_combat', 'trainer', 'traveling', '0', '-1',
            'none', '1651', '80', hex('Lee Brown'), '-', hex('Goldshire'), 'npc', '789',
            hex(npc), hex(task), hex(executed), '0', '0'}, '\t')
        local count = math.ceil(#body / 160)
        for i = 1, count do
            receive('WIDLEACT', '1\t' .. request .. '\t13\t' .. i .. '\t' .. count .. '\t' ..
                body:sub((i - 1) * 160 + 1, i * 160))
        end
        assert(WoweeBotStatusGoal:GetText() == 'Travel plan: Train at Lee Brown')
    end
    snapshot('Bag vendor', 'rpg buy', '')
    assert(WoweeBotStatusInteraction:GetText() == 'Selected task: Buy items - Bag vendor')
    assert(WoweeBotStatusAction:GetText() == '') -- intent is not a completed purchase
    snapshot('Quest giver', 'rpg end quest', 'rpg buy')
    assert(WoweeBotStatusInteraction:GetText() == 'Selected task: Turn in quests - Quest giver')
    assert(WoweeBotStatusAction:GetText() == 'Last reported action: Buy items')
    snapshot('Warlock trainer', 'rpg', '')
    assert(WoweeBotStatusInteraction:GetText() == 'Selected target: Warlock trainer (task not reported)')
    snapshot('Armor vendor', 'rpg buy', 'rpg buy')
    assert(WoweeBotStatusInteraction:GetText() == 'Selected task: Buy items - Armor vendor')
    snapshot('Armor vendor', 'rpg buy', '', 'combat')
    assert(WoweeBotStatusInteraction:GetText() == 'Now: In combat')
    for i = 1, 8 do tick() end
    assert(type(WoweeBotController:GetScript('OnUpdate')) == 'function')
    GetTime, UnitName, SendChatMessage = oldClock, oldUnit, oldSend
    event(WoweeBotController, 'PLAYER_ENTERING_WORLD')
end
print('Retained travel plan and changing maintenance UI scenarios passed')

-- Negotiated grind activity: actual models and native frames.
do
local A,S,C = WoweeBotActivity,WoweeBotStatus,WoweeBotControl
local function hex(s) return s == '' and '-' or (s:gsub('.',function(c)return string.format('%02x',c:byte())end)) end
local function body(kind,status)
    return table.concat({'bot','non_combat',kind or 'grind',status or 'working','0','-1','none','257','3',
        hex('Kobold Worker'),'-',hex('Elwynn Forest'),'none','0','-','-','-','0','0'},'\t')
end
local function packet(id,gen,part,count,chunk) return table.concat({'1',id,gen,part,count,chunk},'\t') end
local function deliver(a,id,wire,now,generation)
    local result,changed
    local count=math.ceil(#wire/160)
    for i=count,1,-1 do result,changed=a:receive('WIDLEACT',packet(id,2,i,count,wire:sub((i-1)*160+1,i*160)),
        'Idlelabqfix','Idlelabqfix',generation or 2,now or 1) end
    return result,changed
end
local ctl={ackId='control_1',generation=2}
local a=A.new('old')
assert(a:capability('WIDLECAP','1\tcontrol_1\t2\tactivity_1','Idlelabqfix','Idlelabqfix',ctl))
assert(a:request(2,0)=='.bot idle old_a1 snapshot 2')
local old=assert(deliver(a,'old_a1',body('other'),1))
local m=S.new();A.apply(m,old,1);assert(m:grindDecision(2,nil)==false)
-- A new kind without explicit negotiation must never start grinding.
a:request(2,2);local unrequested,changed=deliver(a,'old_a2',body(),3)
assert(not unrequested and changed and a.failure=='Unrequested grind activity')
assert(not a:capability('WIDLEGCAP','1\tcontrol_1\t2\tgrind_activity_1','Other','Idlelabqfix',ctl))
assert(not a:capability('WIDLEGCAP','1\told\t2\tgrind_activity_1','Idlelabqfix','Idlelabqfix',ctl))
assert(not a:capability('WIDLEGCAP','1\tcontrol_1\t3\tgrind_activity_1','Idlelabqfix','Idlelabqfix',ctl))
assert(not a:capability('WIDLEGCAP','1\tcontrol_1\t2\tunknown_1','Idlelabqfix','Idlelabqfix',ctl))
-- Both delivery orders are supported, but the ordinary capability remains required.
local n=A.new('new')
assert(n:capability('WIDLEGCAP','1\tcontrol_1\t2\tgrind_activity_1','Idlelabqfix','Idlelabqfix',ctl))
assert(not n.supported and not n.grindSupported)
assert(n:capability('WIDLECAP','1\tcontrol_1\t2\tactivity_1','Idlelabqfix','Idlelabqfix',ctl))
assert(n:request(2,0)=='.bot idle new_a1 snapshot 2 grind_1')
local explicit=assert(deliver(n,'new_a1',body(),1));assert(explicit.grindActivity)
A.apply(m,explicit,1);local desired,reason=m:grindDecision(2,nil)
assert(desired==true and reason=='grinding for XP or gold')
assert(m.goal=='Grind for XP or gold: Kobold Worker' and not m:currentQuest(2))
for _,status in ipairs({'preparing','ready','traveling','cooldown','expired','none'}) do
    local s=assert(A.decode(body('grind',status)));s.grindActivity=true;A.apply(m,s,1)
    assert(m:grindDecision(2,nil)==false)
end
for _,state in ipairs({'dead','reaction','unknown'}) do
    local s=assert(A.decode(body()));s.grindActivity=true;s.state=state;A.apply(m,s,1)
    assert(m:grindDecision(2,nil)==nil)
end
-- Merely decoding a body does not supply negotiated authority.
local raw=assert(A.decode(body()));A.apply(m,raw,1);assert(m:grindDecision(2,nil)==false)
local invalid=body():gsub('\t0\t%-1\tnone\t257\t','\t0\t-1\tnone\t-161557\t')
assert(not A.decode(invalid))
invalid=body():gsub('\t0\t%-1\tnone\t257\t','\t33\t-1\tnone\t257\t');assert(not A.decode(invalid))
-- Existing quest classification must stay distinct from a selected grind activity.
for _,kind in ipairs({'object_loot','vendor_item','spell','interact','kill','creature_loot'}) do
    local s={mode='bot',state='non_combat',kind='quest_objective',status='working',questId=3904,objective=0,
        objectiveType=kind,entry=kind=='object_loot' and -161557 or 38,distance=3,goal='Quest',questTitle='Quest',
        location='',interactionName='',interactionType='none',task='',executed='',moveRetries=0,workRetries=0,grindActivity=true}
    A.apply(m,s,1);assert(m:grindDecision(2,{complete=false,failed=false})==(kind=='kill' or kind=='creature_loot'))
    assert(m:grindDecision(2,{complete=true})==false)
end
A.apply(m,explicit,1);assert(m:grindDecision(30,nil)==nil)
m:receive('', 'grind travel target\nTarget: get xp or gold from killing Kobold Worker\nStatus: working', 'Idlelabqfix','Idlelabqfix',2)
assert(m:grindDecision(3,nil)==false)
-- A newly accepted ordinary capability without the matching extension revokes it.
ctl.ackId='control_2';assert(n:capability('WIDLECAP','1\tcontrol_2\t2\tactivity_1','Idlelabqfix','Idlelabqfix',ctl))
assert(not n.grindSupported and n:request(2,4)=='.bot idle new_a2 snapshot 2')
assert(n:capability('WIDLEGCAP','1\tcontrol_2\t2\tgrind_activity_1','Idlelabqfix','Idlelabqfix',ctl))
assert(n.grindSupported)
n:request(2,5);assert(not deliver(n,'new_a3',body(),10))
n:request(2,11);assert(not deliver(n,'new_a4',body(),12,3))
-- Real Control owns mutations: taking Manual immediately supersedes grind.
local c=C.new('proof');assert(c:update(0)=='.bot idle proof_1 status')
assert(c:receive('WIDLE','1\tproof_1\tok\tbot\t2\t0\tnone','Idlelabqfix','Idlelabqfix',0.1))
assert(c:isBot(.2));c:planGrind(true);assert(c:update(.2)=='.bot idle proof_2 grind 2 on')
c:want('manual');assert(not c:isBot(.3));assert(c:update(.3)=='.bot idle proof_3 control 2 manual')
assert(not c:receive('WIDLE','1\tproof_2\tok\tbot\t2\t1\tnone','Idlelabqfix','Idlelabqfix',.4))
assert(c:receive('WIDLE','1\tproof_3\tok\tmanual\t3\t0\tnone','Idlelabqfix','Idlelabqfix',.5))
assert(not c:isBot(.6) and c.desiredGrind==nil and c:update(.6)==nil)
print('Grind negotiation, legacy compatibility, lifecycle, quest/container separation and real control handoff pass')

end
-- Run after the normal native activity-frame scenarios. No mocked frames.
do
    local oldClock,oldUnit,oldSend=GetTime,UnitName,SendChatMessage
    local oldCount,oldTitle,oldBoards=GetNumQuestLogEntries,GetQuestLogTitle,GetNumQuestLeaderBoards
    local oldParty,oldRaid=GetNumPartyMembers,GetNumRaidMembers
    local clock,sent=oldClock()+100,{}
    GetTime=function()return clock end
    UnitName=function(unit)if unit=='player'then return 'Jizzle'end;return oldUnit(unit)end
    SendChatMessage=function(command)sent[#sent+1]=command end
    GetNumQuestLogEntries=function()return 1 end
    GetQuestLogTitle=function()return "Milly's Harvest",4,0,false,false,false,0,0,3904 end
    GetNumQuestLeaderBoards=function()return 0 end
    GetNumPartyMembers=function()return 0 end
    GetNumRaidMembers=function()return 0 end
    local event=WoweeBotController:GetScript('OnEvent')
    local tick=function()WoweeBotController:GetScript('OnUpdate')()end
    local function receive(prefix,text)event(WoweeBotController,'CHAT_MSG_ADDON',prefix,text,'WHISPER','Jizzle')end
    local function last(verb)
        for i=#sent,1,-1 do
            local id=sent[i]:match('^%.bot idle ([%w_-]+) '..verb)
            if id then return id,sent[i]end
        end
        error('Missing '..verb..' request; control='..tostring(WoweeBotStatusControl:GetText())..'; sent='..table.concat(sent,' | '))
    end
    local function ack(id,grind,mode,generation)
        generation=generation or 21
        receive('WIDLE','1\t'..id..'\tok\t'..(mode or 'bot')..'\t'..generation..'\t'..grind..'\tnone')
        receive('WIDLEGCAP','1\t'..id..'\t'..generation..'\tgrind_activity_1')
        receive('WIDLECAP','1\t'..id..'\t'..generation..'\tactivity_1')
    end
    local function hex(s)return s=='' and '-' or s:gsub('.',function(c)return string.format('%02x',c:byte())end)end
    local function snapshot(kind,status)
        clock=clock+8;tick();ack(last('status'),'0');tick()
        local id,command=last('snapshot');assert(command:match(' snapshot 21 grind_1$'))
        local object=kind=='quest_objective'
        local body=table.concat({'bot','non_combat',kind,status,object and '3904' or '0',object and '0' or '-1',
            object and 'object_loot' or 'none',object and '-161557' or '257','3',hex(object and 'Milly\'s Harvest' or 'Kobold Worker'),
            object and hex("Milly's Harvest")or '-',hex('Elwynn Forest'),'none','0','-','-','-','0','0'},'\t')
        local count=math.ceil(#body/160)
        for i=1,count do receive('WIDLEACT','1\t'..id..'\t21\t'..i..'\t'..count..'\t'..body:sub((i-1)*160+1,i*160))end
        clock=clock+.3;tick()
    end
    WoweeBotStatusSettings.autoGrind=true
    event(WoweeBotController,'PLAYER_ENTERING_WORLD');tick();ack(last('status'),'0')
    snapshot('grind','working')
    assert(WoweeBotStatusGoal:GetText()=='Travel plan: Grind for XP or gold: Kobold Worker')
    local on,onCommand=last('grind');assert(onCommand:match(' grind 21 on$'));ack(on,'1')
    -- The next confirmed task replaces the grind activity completely.
    clock=clock+8;tick();ack(last('status'),'1');tick()
    local id,command=last('snapshot');assert(command:match(' snapshot 21 grind_1$'))
    local body=table.concat({'bot','non_combat','quest_objective','working','3904','0','object_loot','-161557','2',
        hex("Milly's Harvest"),hex("Milly's Harvest"),hex('Elwynn Forest'),'none','0','-','-','-','0','0'},'\t')
    local count=math.ceil(#body/160)
    for i=1,count do receive('WIDLEACT','1\t'..id..'\t21\t'..i..'\t'..count..'\t'..body:sub((i-1)*160+1,i*160))end
    clock=clock+.3;tick()
    local off,offCommand=last('grind');assert(off~=on and offCommand:match(' grind 21 off$'));ack(off,'0')
    assert(WoweeBotStatusGoal:GetText()=="Quest plan: Milly's Harvest")
    SlashCmdList.WOWEEBOTPLAY('manual');tick()
    local manual,manualCommand=last('control');assert(manualCommand:match(' control 21 manual$'));ack(manual,'0','manual',22)
    clock=clock+.3;tick()
    assert(WoweeBotStatusMode:GetText():find('I am playing',1,true))
    assert(type(WoweeBotController:GetScript('OnUpdate'))=='function')
    GetTime,UnitName,SendChatMessage=oldClock,oldUnit,oldSend
    GetNumQuestLogEntries,GetQuestLogTitle,GetNumQuestLeaderBoards=oldCount,oldTitle,oldBoards
    GetNumPartyMembers,GetNumRaidMembers=oldParty,oldRaid
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
end
print('Native frames: negotiated grind label/On, container Off and Manual handoff passed')

-- Real policy frames and request events: no implicit control toggle or manual rejection.
do
 local oldTime,oldName,oldSend=GetTime,UnitName,SendChatMessage
 local oldGroup,oldGuild,oldTrade,oldHide=DeclineGroup,DeclineGuild,CloseTrade,StaticPopup_Hide
 local clock,sent,declined=0,{}, {group=0,guild=0,trade=0}
 GetTime=function()return clock end
 UnitName=function(u)if u=='player'then return 'Jizzle'end return oldName(u)end
 SendChatMessage=function(text)sent[#sent+1]=text end
 DeclineGroup=function()declined.group=declined.group+1 end
 DeclineGuild=function()declined.guild=declined.guild+1 end
 CloseTrade=function()declined.trade=declined.trade+1 end
 StaticPopup_Hide=function()end
 local event=WoweeBotController:GetScript('OnEvent')
 local function tick()WoweeBotController:GetScript('OnUpdate')()end
 local function receive(prefix,text)event(WoweeBotController,'CHAT_MSG_ADDON',prefix,text,'WHISPER','Jizzle')end
 local function id(verb)
  for i=#sent,1,-1 do local v=sent[i]:match('^%.bot idle ([%w_-]+) '..verb..' ');if v then return v end end
  error('No '..verb..' request')
 end
 -- status has no trailing argument, so identify it separately.
 local function statusId()
  for i=#sent,1,-1 do local v=sent[i]:match('^%.bot idle ([%w_-]+) status$');if v then return v end end
  error('No status request')
 end
 event(WoweeBotController,'PLAYER_ENTERING_WORLD');tick()
 receive('WIDLE','1\t'..statusId()..'\tok\tmanual\t44\t0\tnone')
 receive('WIDLECAP','1\t'..statusId()..'\t44\trequests_1')
 WoweeBotPoliciesFrame:Show();tick()
 receive('WIDLEREQ','1\t'..id('requests')..'\t44\tok\t7\tstored\tnone')
 assert(WoweeBotRequestguildState:GetText()=='Guild invites: Reject (server confirmed)')
 assert(WoweeBotRequestgroupToggle:IsEnabled()==1)
 WoweeBotRequestgroupToggle:GetScript('OnClick')();tick()
 assert(sent[#sent]:find('group_off',1,true))
 receive('WIDLEREQ','1\t'..id('requests')..'\t44\tok\t5\tstored\tnone')
 assert(WoweeBotRequestgroupState:GetText()=='Group invites: Allow (server confirmed)')
 for _,v in ipairs({'PARTY_INVITE_REQUEST','GUILD_INVITE_REQUEST','TRADE_REQUEST'})do event(WoweeBotController,v,'Other')end
 assert(declined.group==0 and declined.guild==0 and declined.trade==0)
 for _,v in ipairs(sent)do assert(not v:find(' control ',1,true))end
 SlashCmdList.WOWEEBOTPLAY('bot');tick()
 receive('WIDLE','1\t'..id('control')..'\tok\tbot\t45\t0\tnone');tick()
 receive('WIDLEREQ','1\t'..id('requests')..'\t45\tok\t5\tstored\tnone')
 for _,v in ipairs({'PARTY_INVITE_REQUEST','GUILD_INVITE_REQUEST','TRADE_REQUEST'})do event(WoweeBotController,v,'Other')end
 assert(declined.group==0 and declined.guild==1 and declined.trade==1)
 clock=30 -- stale bot confirmation cannot reject on the addon's behalf
 event(WoweeBotController,'GUILD_INVITE_REQUEST','Other');assert(declined.guild==1)
 GetTime,UnitName,SendChatMessage=oldTime,oldName,oldSend
 DeclineGroup,DeclineGuild,CloseTrade,StaticPopup_Hide=oldGroup,oldGuild,oldTrade,oldHide
 WoweeBotPoliciesFrame:Hide();event(WoweeBotController,'PLAYER_ENTERING_WORLD')
end
print('Incoming request policy UI, independent toggles, Manual preservation and bot event rejection passed')
