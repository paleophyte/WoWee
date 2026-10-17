-- Strict additive telemetry, durable historical Notes and the actual panel.
do
    local Travel=WoweeBotTravel
    assert(type(Travel)=='table')
    local function hex(s) return (s:gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
    local function body(state,reason,seconds,rebuilds,remaining,warning)
        local head=table.concat({'1',state,'1000',reason,seconds,rebuilds,remaining,remaining>0 and 1120 or 0},'\t')
        return head..'\n'..(warning or '0\t0\t0\t0\t0\t0\t-')
    end
    local fact='1000\t1\t1\t0\t90\t1120\t'..hex('Auctioneer Chilton')
    local paused=body('paused',0,90,1,120,fact)
    local decoded=assert(Travel.decode(paused))
    assert(decoded.warning.goal=='Auctioneer Chilton' and decoded.warning.kind==1)
    for reason=1,11 do assert(Travel.decode(body('waiting',reason,121,0,0,
        '1000\t1\t2\t'..reason..'\t120\t0\t'..hex('NPC')))) end
    for _,invalid in ipairs({
        body('paused',0,90,1,0,fact),body('paused',0,90,0,120,fact),
        body('waiting',0,120,0,0),body('stalled',1,90,0,0),body('stalled',0,4,0,0),
        body('traveling',0,8,0,0),body('rebuilding',0,45,0,0),body('manual',0,1,0,0),
        body('idle',0,0,0,0,'1000\t1\t1\t0\t89\t1120\t-'),
        body('idle',0,0,0,0,'1000\t1\t2\t0\t120\t0\t-'),
        body('idle',0,0,0,0,'1000\t1\t1\t0\t90\t1121\t-'),
        body('idle',0,0,0,0,'0\t1\t0\t0\t0\t0\t-'),
        paused..'\nextra',paused:gsub(hex('Auctioneer Chilton'),'0a'),
        paused:gsub(hex('Auctioneer Chilton'),string.rep('61',41))
    }) do assert(not Travel.decode(invalid),invalid) end
    local travel=Travel.new()
    travel:capability('WIDLECAP','1\tack\t7\tnav_1','Other','Jizzle',{ackId='ack',generation=7})
    assert(not travel.supported)
    travel:capability('WIDLECAP','1\tack\t7\tnav_1','Jizzle','Jizzle',{ackId='ack',generation=7})
    assert(travel.supported);travel:expect('snapshot',7,10)
    local frames={}
    for i=1,math.ceil(#paused/160) do frames[i]='1\tsnapshot\t7\t'..i..'\t'..math.ceil(#paused/160)..'\t'..paused:sub((i-1)*160+1,i*160) end
    assert(not travel:receive('WIDLETRAVEL',frames[1],'Other','Jizzle',7,11))
    assert(not travel:receive('WIDLETRAVEL',frames[1],'Jizzle','Jizzle',8,11))
    assert(not travel:receive('WIDLETRAVEL',frames[1],'Jizzle','Jizzle',7,15))
    for i=#frames,1,-1 do travel:receive('WIDLETRAVEL',frames[i],'Jizzle','Jizzle',7,11) end
    assert(travel.snapshot.warning.kind==1 and travel:summary(12):find('119s',1,true))
    assert(travel:summary(32):find('stale',1,true))
    travel:expect('missing',7,12);travel:expire(17,7);assert(travel.failure and travel:summary(17):find('unavailable',1,true))
    travel:expect('conflict',7,20)
    travel:receive('WIDLETRAVEL','1\tconflict\t7\t1\t2\tx','Jizzle','Jizzle',7,21)
    travel:receive('WIDLETRAVEL','1\tconflict\t7\t1\t2\ty','Jizzle','Jizzle',7,21)
    assert(travel.failure and not travel.pending)
    local root={};local inbox=WoweeBotInbox.new(root,'Travel realm','Jizzle')
    local warning=decoded.warning
    assert(inbox:travelWarning(warning,100));assert(not inbox:travelWarning(warning,101))
    local note=inbox:visible()[1];assert(note.kind=='note' and note.action=='botstatus')
    assert(note.text:find('kept the selected activity',1,true))
    assert(inbox:dismiss(note.key))
    assert(not WoweeBotInbox.new(root,'Travel realm','Jizzle'):travelWarning(warning,102))
    assert(WoweeBotInbox.new(root,'Other realm','Jizzle'):travelWarning(warning,102))
    warning.at,warning.untilAt,warning.sequence=1100,1220,2
    assert(inbox:travelWarning(warning,103)) -- genuinely later fact is not hidden
    assert(not inbox:travelWarning({at=1},104))
end
do
    local oldClock,oldWall,oldSend,oldData=GetTime,GetServerTime,SendChatMessage,WoweeBotNotesData
    local clock,sent=oldClock()+400,{}
    GetTime=function() return clock end
    GetServerTime=function() return 1792265000+clock end
    SendChatMessage=function(command) sent[#sent+1]=command end
    local event=WoweeBotController:GetScript('OnEvent')
    local notes=WoweeBotNotesDriver:GetScript('OnEvent')
    local tick=function() WoweeBotController:GetScript('OnUpdate')() end
    local function receive(prefix,text,sender) event(WoweeBotController,'CHAT_MSG_ADDON',prefix,text,'WHISPER',sender or 'Jizzle') end
    local function latest(verb)
        for i=#sent,1,-1 do local id=sent[i]:match('^%.bot idle ([%w_-]+) '..verb);if id then return id end end
        error('Missing '..verb..' request')
    end
    local function hex(s) return (s:gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
    notes(WoweeBotNotesDriver,'PLAYER_LEAVING_WORLD');WoweeBotNotesData={}
    notes(WoweeBotNotesDriver,'PLAYER_ENTERING_WORLD')
    event(WoweeBotController,'PLAYER_ENTERING_WORLD');tick()
    local id=latest('status')
    receive('WIDLE','1\t'..id..'\tok\tbot\t70\t0\tnone')
    receive('WIDLECAP','1\t'..id..'\t70\tactivity_1,nav_1')
    clock=clock+.3;tick();local request=latest('snapshot')
    local function deliver(prefix,body,generation,sender)
        local count=math.ceil(#body/160)
        for i=count,1,-1 do receive(prefix,'1\t'..request..'\t'..generation..'\t'..i..'\t'..count..'\t'..body:sub((i-1)*160+1,i*160),sender) end
    end
    local act=table.concat({'bot','non_combat','other','traveling','0','-1','none','8670','500',
        hex('Auctioneer Chilton'),'-',hex('Stormwind City'),'none','0','-','-','-','0','0'},'\t')
    deliver('WIDLEACT',act,70)
    local body='1\tpaused\t1000\t0\t90\t1\t120\t1120\n1000\t1\t1\t0\t90\t1120\t'..hex('Auctioneer Chilton')
    deliver('WIDLETRAVEL',body,70,'Other')
    assert(WoweeBotStatusTravel:GetText()=='')
    deliver('WIDLETRAVEL',body,70)
    assert(WoweeBotStatusTravel:GetText():find('Travel paused',1,true))
    assert(WoweeBotStatusGoal:GetText():find('Auctioneer Chilton',1,true))
    assert(WoweeBotStatusMode:GetText()=='Bot is playing (server confirmed)')
    SlashCmdList.WOWEEBOTNOTES('')
    assert(WoweeBotNotesSummary:GetText():find('1 notes',1,true))
    WoweeBotNotesRow1:GetScript('OnClick')()
    assert(WoweeBotNotesDetail:GetText():find('route rebuild',1,true))
    assert(WoweeBotNotesDetail:GetText():find('may already have ended',1,true))
    -- Normal manual control remains independent of the travel safeguard.
    WoweeBotStatusPlay:GetScript('OnClick')();clock=clock+.3;tick();id=latest('control')
    receive('WIDLE','1\t'..id..'\tok\tmanual\t71\t0\tnone')
    assert(WoweeBotStatusTravel:GetText()=='' and WoweeBotStatusMode:GetText()=='I am playing (server confirmed)')
    assert(type(WoweeBotController:GetScript('OnUpdate'))=='function')
    for _,command in ipairs(sent) do assert(not command:find(' grind ',1,true)) end
    notes(WoweeBotNotesDriver,'PLAYER_LEAVING_WORLD');WoweeBotNotesData=oldData
    GetTime,GetServerTime,SendChatMessage=oldClock,oldWall,oldSend
    notes(WoweeBotNotesDriver,'PLAYER_ENTERING_WORLD');WoweeBotNotesFrame:Hide()
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
end
print('Travel report validation, correlation/freshness, historical Notes, actual panel and Manual independence passed')
