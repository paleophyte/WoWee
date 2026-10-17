-- Supported history travels with real correlated snapshot requests. Missing
-- frames cannot erase past results or change manual/bot control.
do
    local oldClock,oldWall,oldSend=GetTime,GetServerTime,SendChatMessage
    local clock,sent=oldClock()+100,{}
    GetTime=function() return clock end
    GetServerTime=function() return 1792234010+clock end
    SendChatMessage=function(command) sent[#sent+1]=command end
    local event=WoweeBotController:GetScript('OnEvent')
    local tick=function() WoweeBotController:GetScript('OnUpdate')() end
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
    local function hex(s) return (s:gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
    event(WoweeBotController,'PLAYER_ENTERING_WORLD');tick()
    local id=latest('status')
    receive('WIDLE','1\t'..id..'\tok\tbot\t20\t0\tnone')
    receive('WIDLECAP','1\t'..id..'\t20\tactivity_1,history_1')
    clock=clock+8;tick()
    receive('WIDLE','1\t'..latest('status')..'\tok\tbot\t20\t0\tnone')
    local request=latest('snapshot')
    local body='1792234000_1\t3\n1\t1792234000\tpurchase\t4496\t1\t789\t'..hex('Bought Brown Linen Bag x1')..
        '\n2\t1792234001\tlearned\t635\t1\t927\t'..hex('Learned Holy Light at Brother Wilhelm')..
        '\n3\t1792234002\tturnin\t5545\t1\t10616\t'..hex('Turned in A Bundle of Trouble')
    local parts=math.ceil(#body/160)
    local function history(sender)
        for i=parts,1,-1 do receive('WIDLEHIST','1\t'..request..'\t20\t'..i..'\t'..parts..'\t'..
            body:sub((i-1)*160+1,i*160),sender) end
    end
    history('Other')
    SlashCmdList.WOWEEBOTHISTORY('')
    assert(WoweeBotHistoryStatus:GetText():find('Waiting for a complete',1,true))
    history('Jizzle');WoweeBotHistoryFrame:GetScript('OnUpdate')()
    assert(WoweeBotHistoryStatus:GetText():find('3 outcomes',1,true))
    assert(WoweeBotHistoryRow1:GetText():find('Turned in A Bundle of Trouble',1,true))
    assert(WoweeBotHistoryRow2:GetText():find('Learned Holy Light',1,true))
    assert(WoweeBotHistoryRow3:GetText():find('Bought Brown Linen Bag',1,true))
    history('Jizzle');WoweeBotHistoryFrame:GetScript('OnUpdate')()
    assert(WoweeBotHistoryStatus:GetText():find('3 outcomes',1,true))
    assert(not WoweeBotHistoryRow4:GetText():find('Bought',1,true))
    assert(WoweeBotStatusMode:GetText()=='Bot is playing (server confirmed)')
    -- A later incomplete history reply preserves results and control.
    clock=clock+8;tick()
    receive('WIDLE','1\t'..latest('status')..'\tok\tbot\t20\t0\tnone')
    clock=clock+5;tick();WoweeBotHistoryFrame:GetScript('OnUpdate')()
    assert(WoweeBotHistoryStatus:GetText():find('previous records retained',1,true))
    assert(WoweeBotHistoryRow1:GetText():find('Turned in',1,true))
    assert(WoweeBotStatusMode:GetText()=='Bot is playing (server confirmed)')
    assert(type(WoweeBotController:GetScript('OnUpdate'))=='function')
    WoweeBotStatusPlay:GetScript('OnClick')();clock=clock+0.3;tick()
    receive('WIDLE','1\t'..latest('control')..'\tok\tmanual\t21\t0\tnone')
    assert(WoweeBotStatusMode:GetText()=='I am playing (server confirmed)')
    history('Jizzle');WoweeBotHistoryFrame:GetScript('OnUpdate')()
    assert(WoweeBotHistoryRow1:GetText():find('Turned in',1,true))
    assert(WoweeBotHistoryStatus:GetText():find('3 outcomes',1,true))
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
    SlashCmdList.WOWEEBOTHISTORY('')
    assert(WoweeBotHistoryRow1:GetText()=='') -- a new character/world scope clears the view
    assert(WoweeBotHistoryStatus:GetText():find('unavailable',1,true))
    WoweeBotHistoryFrame:Hide()
    GetTime,GetServerTime,SendChatMessage=oldClock,oldWall,oldSend
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
end
print('Real history frames, correlated packets, deduplication, expiry and world-scope scenarios passed')

-- Actual TBC replies captured October17: quest109, two purchases, seven
-- learned spells and quest40. Only outer request/generation/sender are adapted
-- to this isolated controller. Epochs/sequence/time/IDs/quantities/text are raw.
-- Separate captured AI lifetimes are replayed in one scope to test epoch
-- deduplication; this does not claim history persists across a real logout.
do
local capturedBatches={
[[1792242841_1	1
1	1792243361	turnin	109	1	234	5475726e656420696e205265706f727420746f20477279616e2053746f75746d616e746c6520617420477279616e2053746f75746d616e746c65]],
[[1792244118_3	2
1	1792244344	purchase	159	5	151	426f756768742052656672657368696e6720537072696e672057617465722078352066726f6d2042726f672048616d66697374
2	1792244344	purchase	159	5	465	426f756768742052656672657368696e6720537072696e672057617465722078352066726f6d204261726b65657020446f6262696e73]],
[[1792244582_4	8
1	1792244600	learned	498	1	927	4c6561726e656420446976696e652050726f74656374696f6e2061742042726f746865722057696c68656c6d
2	1792244600	learned	633	1	927	4c6561726e6564204c6179206f6e2048616e64732061742042726f746865722057696c68656c6d
3	1792244600	learned	31789	1	927	4c6561726e6564205269676874656f757320446566656e73652061742042726f746865722057696c68656c6d
4	1792244600	learned	853	1	927	4c6561726e65642048616d6d6572206f66204a7573746963652061742042726f746865722057696c68656c6d
5	1792244600	learned	639	1	927	4c6561726e656420486f6c79204c696768742061742042726f746865722057696c68656c6d
6	1792244600	learned	1022	1	927	4c6561726e656420426c657373696e67206f662050726f74656374696f6e2061742042726f746865722057696c68656c6d
7	1792244600	learned	465	1	927	4c6561726e6564204465766f74696f6e20417572612061742042726f746865722057696c68656c6d
8	1792244704	turnin	40	1	240	5475726e656420696e204120466973687920506572696c206174204d61727368616c2044756768616e]]
}

    local oldClock,oldUnit,oldSend=GetTime,UnitName,SendChatMessage
    local clock,sent=oldClock()+1000,{}
    GetTime=function() return clock end
    UnitName=function(unit) if unit=='player' then return 'Idlelabwood' end return oldUnit(unit) end
    SendChatMessage=function(command) sent[#sent+1]=command end
    local event=WoweeBotController:GetScript('OnEvent')
    local tick=function() WoweeBotController:GetScript('OnUpdate')() end
    local function latest(verb)
        for i=#sent,1,-1 do
            local id=sent[i]:match('^%.bot idle ([%w_-]+) '..verb)
            if id then return id end
        end
        error('Missing captured replay '..verb..' request')
    end
    local function receive(prefix,text)
        event(WoweeBotController,'CHAT_MSG_ADDON',prefix,text,'WHISPER','Idlelabwood')
    end
    event(WoweeBotController,'PLAYER_ENTERING_WORLD');tick()
    local id=latest('status')
    receive('WIDLE','1\t'..id..'\tok\tbot\t31\t0\tnone')
    receive('WIDLECAP','1\t'..id..'\t31\tactivity_1,history_1')
    local function deliver(body)
        clock=clock+8;tick()
        receive('WIDLE','1\t'..latest('status')..'\tok\tbot\t31\t0\tnone');tick()
        local request=latest('snapshot')
        local count=math.ceil(#body/160)
        for i=count,1,-1 do
            receive('WIDLEHIST','1\t'..request..'\t31\t'..i..'\t'..count..'\t'..body:sub((i-1)*160+1,i*160))
        end
    end
    local total=0
    for _,body in ipairs(capturedBatches) do
        total=total+#assert(WoweeBotHistory.decode(body)).entries
        deliver(body)
    end
    assert(total==11)
    SlashCmdList.WOWEEBOTHISTORY('')
    assert(WoweeBotHistoryStatus:GetText():find('11 outcomes',1,true))
    deliver(capturedBatches[3]);WoweeBotHistoryFrame:GetScript('OnUpdate')()
    assert(WoweeBotHistoryStatus:GetText():find('11 outcomes',1,true))
    local found={}
    for page=1,3 do
        for i=1,5 do
            local text=_G['WoweeBotHistoryRow'..i]:GetText()
            if text~='' then assert(text:find('Seen ',1,true)==1) end
            for _,value in ipairs({'Bought Refreshing Spring Water x5','Learned Hammer of Justice',
                'Turned in Report to Gryan Stoutmantle','Turned in A Fishy Peril'}) do
                if text:find(value,1,true) then found[value]=true end
            end
        end
        if page<3 then WoweeBotHistoryNext:GetScript('OnClick')() end
    end
    for _,value in ipairs({'Bought Refreshing Spring Water x5','Learned Hammer of Justice',
        'Turned in Report to Gryan Stoutmantle','Turned in A Fishy Peril'}) do assert(found[value],value) end
    assert(WoweeBotStatusMode:GetText()=='Bot is playing (server confirmed)')
    assert(type(WoweeBotController:GetScript('OnUpdate'))=='function')
    WoweeBotHistoryFrame:Hide()
    GetTime,UnitName,SendChatMessage=oldClock,oldUnit,oldSend
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
end
print('Actual captured TBC purchases/training/turn-ins render once through the real controller and history UI')
