-- Aggregate checks must never masquerade as individual discounted prices or
-- a confirmed journey/purchase. Exercise the independent correlated channel.
do
    local Plan=WoweeBotTrainingPlan
    assert(type(Plan)=='table')
    local function body(state,required,money,estimate,cheat)
        return '1\tok\t1\n0\t1792259000\t'..state..'\t'..(estimate or 100)..'\t'..
            required..'\t'..money..'\t3\t1\t'..(cheat or 0)
    end
    local deferred=assert(Plan.decode(body('deferred',125,25)))
    assert(not Plan.decode(body('eligible',125,25)))
    assert(not Plan.decode(body('deferred',25,25)))
    assert(not Plan.decode(body('deferred',125,25,100,1)))
    assert(Plan.decode(body('eligible',125,25,100,1)))
    assert(Plan.decode(body('cached_eligible',125,25)))
    assert(Plan.decode(body('cached_deferred',25,125)))
    assert(not Plan.decode(body('cached_deferred',125,25)))
    assert(not Plan.decode(body('none',1,0,0)))
    assert(Plan.decode(body('none',0,0,0)))
    assert(not Plan.decode(body('coins',125,25)))
    assert(not Plan.decode(body('deferred',4294967296,25)))
    assert(not Plan.decode(body('deferred',125,25)..'\t0'))
    assert(not Plan.decode('1\tok\t2\n'..body('deferred',125,25):match('\n(.*)')..'\n'..body('deferred',125,25):match('\n(.*)')))
    assert(not Plan.decode('1\tunavailable\t1\n0'))
    local plan=Plan.new()
    plan:capability('WIDLECAP','1\tack\t2\ttraining_plan_1','Other','Jizzle',{ackId='ack',generation=2})
    assert(not plan.supported)
    plan:capability('WIDLECAP','1\tack\t2\ttraining_plan_1','Jizzle','Jizzle',{ackId='ack',generation=2})
    assert(plan.supported)
    plan:expect('snap',2,10)
    local frame='1\tsnap\t2\t1\t1\t'..body('deferred',125,25)
    assert(not plan:receive('WIDLEPLAN',frame,'Other','Jizzle',2,11))
    assert(not plan:receive('WIDLEPLAN',frame,'Jizzle','Jizzle',3,11))
    assert(not plan:receive('WIDLEPLAN',frame,'Jizzle','Jizzle',2,15))
    assert(plan:receive('WIDLEPLAN',frame,'Jizzle','Jizzle',2,11))
    assert(plan.snapshot.rows[1].required==125)
    local root={};local inbox=WoweeBotInbox.new(root,'Plan test','Jizzle')
    assert(inbox:trainingPlan(plan.snapshot,100))
    local entry=inbox:find('training-plan:0')
    assert(entry.kind=='note' and entry.title:find('deferred',1,true))
    assert(entry.text:find('before trainer discounts',1,true))
    assert(entry.text:find('Missing for this combined gate: 0g 1s 0c',1,true))
    assert(entry.text:find('nearby or quest-related NPC interactions',1,true))
    assert(not entry.text:find('Discounted price:',1,true))
    entry.dismissed=true
    assert(not inbox:trainingPlan(plan.snapshot,101) and entry.dismissed)
    assert(inbox:trainingPlan(assert(Plan.decode(body('eligible',125,200))),102))
    assert(entry.text:find('purchase has not been confirmed',1,true))
    assert(inbox:trainingPlan(assert(Plan.decode(body('cached_deferred',25,125))),102))
    assert(entry.text:find('cached deferred decision',1,true) and not entry.text:find('Missing for this combined gate',1,true))
    assert(inbox:trainingPlan(assert(Plan.decode(body('none',0,200,0))),103))
    assert(entry.title:find('cleared',1,true))
    local relog=WoweeBotInbox.new(root,'Plan test','Jizzle')
    assert(relog:find('training-plan:0')==entry)
    assert(not relog:trainingPlan({status='unavailable'},104))
    assert(relog:trainingPlan({status='ok',rows={}},105))
    assert(entry.text:find('has not been rechecked',1,true))
    assert(not relog:trainingPlan({status='ok',rows={}},106))
    plan:expect('missing',2,20);plan:expire(25,2)
    assert(plan.failure and plan.snapshot.rows[1].required==125)
    local retained=plan.snapshot
    plan:expect('multi',2,30)
    local payload=body('deferred',125,25)
    local function fragment(id,part,count,chunk)
        return '1\t'..id..'\t2\t'..part..'\t'..count..'\t'..chunk
    end
    assert(not plan:receive('WIDLEPLAN',fragment('wrong',1,1,payload),'Jizzle','Jizzle',2,31))
    assert(not plan:receive('WIDLEPLAN',fragment('multi',1,9,payload),'Jizzle','Jizzle',2,31))
    assert(not plan:receive('WIDLEPLAN',fragment('multi',1,1,string.rep('x',161)),'Jizzle','Jizzle',2,31))
    assert(not plan:receive('WIDLEPLAN',fragment('multi',2,2,payload:sub(20)),'Jizzle','Jizzle',2,31))
    assert(not plan:receive('WIDLEPLAN',fragment('multi',2,2,payload:sub(20)),'Jizzle','Jizzle',2,31))
    assert(plan:receive('WIDLEPLAN',fragment('multi',1,2,payload:sub(1,19)),'Jizzle','Jizzle',2,31))
    assert(plan.snapshot.rows[1].required==retained.rows[1].required)
    retained=plan.snapshot
    plan:expect('conflict',2,40)
    plan:receive('WIDLEPLAN',fragment('conflict',1,2,'first'),'Jizzle','Jizzle',2,41)
    assert(plan:receive('WIDLEPLAN',fragment('conflict',1,2,'different'),'Jizzle','Jizzle',2,41))
    assert(plan.failure and plan.snapshot==retained and not plan.pending)
end
print('Aggregate training-plan distinction, validation, sender/correlation, dedup and historical reconciliation passed')

-- Exercise the real controller -> framed reply -> Notes UI path. Missing or
-- malformed diagnostic replies must never send gameplay/control commands.
if WoweeBotController then
    local oldClock,oldWall,oldSend=GetTime,GetServerTime,SendChatMessage
    local clock,sent=oldClock()+200,{}
    GetTime=function() return clock end
    GetServerTime=function() return 1792259000 end
    SendChatMessage=function(command) sent[#sent+1]=command end
    local event=WoweeBotController:GetScript('OnEvent')
    local notes=WoweeBotNotesDriver:GetScript('OnEvent')
    local function tick() WoweeBotController:GetScript('OnUpdate')() end
    local function receive(prefix,text,sender)
        event(WoweeBotController,'CHAT_MSG_ADDON',prefix,text,'WHISPER',sender or 'Jizzle')
    end
    local function latest(verb)
        for i=#sent,1,-1 do
            local id=sent[i]:match('^%.bot idle ([%w_-]+) '..verb)
            if id then return id end
        end
        error('Missing '..verb..' request')
    end
    notes(WoweeBotNotesDriver,'PLAYER_LEAVING_WORLD');WoweeBotNotesData={}
    notes(WoweeBotNotesDriver,'PLAYER_ENTERING_WORLD')
    event(WoweeBotController,'PLAYER_ENTERING_WORLD');tick()
    local id=latest('status')
    receive('WIDLE','1\t'..id..'\tok\tmanual\t60\t0\tnone')
    receive('WIDLECAP','1\t'..id..'\t60\tactivity_1,training_1,training_plan_1')
    clock=clock+0.3;tick()
    local request=latest('snapshot')
    local data='1\tok\t1\n0\t1792259000\tdeferred\t100\t125\t25\t3\t1\t0'
    local frame='1\t'..request..'\t60\t1\t1\t'..data
    receive('WIDLEPLAN',frame,'Other')
    SlashCmdList.WOWEEBOTNOTES('')
    assert(WoweeBotNotesSummary:GetText():find('0 notes',1,true))
    receive('WIDLEPLAN',frame)
    assert(WoweeBotNotesSummary:GetText():find('1 note',1,true))
    WoweeBotNotesRow1:GetScript('OnClick')()
    assert(WoweeBotNotesDetail:GetText():find('Combined lesson estimate before trainer discounts',1,true))
    assert(WoweeBotNotesDetail:GetText():find('Missing for this combined gate: 0g 1s 0c',1,true))
    assert(WoweeBotStatusMode:GetText()=='I am playing (server confirmed)')
    clock=clock+8;tick() -- expire any missing companion diagnostics safely
    assert(type(WoweeBotController:GetScript('OnUpdate'))=='function')
    for _,command in ipairs(sent) do
        assert(not command:find(' control ',1,true) and not command:find(' grind ',1,true))
    end
    WoweeBotNotesFrame:Hide()
    GetTime,GetServerTime,SendChatMessage=oldClock,oldWall,oldSend
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
    print('Aggregate planner reply reaches actual Notes UI in Manual without controller mutation')
end
