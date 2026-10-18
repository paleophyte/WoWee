-- Actual transport, durable Notes, and native frame delivery. No gameplay setter.
do
    local M=WoweeBotItemUse
    local function hex(s)return(s:gsub('.',function(c)return string.format('%02x',c:byte())end))end
    local function body(at,count,blocked)
        return '1\t1792359900_4\t1\n'..at..'\t186234\t32971\t42144\t'..count..'\t1\t'..blocked..'\t'..hex('Water Barrel')..'\t'..hex('Water Bucket')
    end
    local wire=body(1000,1,1)
    assert(M.decode(wire).rows[1].item==32971)
    for _,bad in ipairs({wire..'\n',wire:gsub('\t1\t1\t'..hex('Water Barrel'),'\t0\t1\t'..hex('Water Barrel')),
        wire:gsub('32971','0'),wire:gsub(hex('Water Bucket'),'00'),wire:gsub('1\t179','2\t179'),wire:gsub('\t1\n','\t9\n')}) do
        assert(not M.decode(bad))
    end
    local m=M.new();local control={ackId='ctl',generation=7}
    m:capability('WIDLEICAP','1\tctl\t7\titem_use_1','Other','Jizzle',control);assert(not m.supported)
    m:capability('WIDLEICAP','1\tctl\t7\titem_use_1','Jizzle','Jizzle',control);assert(m.supported)
    local function packet(id,generation,part,count,chunk)return table.concat({'1',id,generation,part,count,chunk},'\t')end
    m:expect('snap',7,10)
    assert(not m:receive('WIDLEITEM',packet('snap',7,1,1,wire),'Other','Jizzle',7,10))
    assert(not m:receive('WIDLEITEM',packet('old',7,1,1,wire),'Jizzle','Jizzle',7,10))
    assert(not m:receive('WIDLEITEM',packet('snap',8,1,1,wire),'Jizzle','Jizzle',7,10))
    assert(m:receive('WIDLEITEM',packet('snap',7,1,1,wire),'Jizzle','Jizzle',7,10));assert(m.snapshot.rows[1].blocked==1)
    m:expect('parts',7,20)
    m:receive('WIDLEITEM',packet('parts',7,1,2,'abc'),'Jizzle','Jizzle',7,20)
    assert(m:receive('WIDLEITEM',packet('parts',7,1,2,'xyz'),'Jizzle','Jizzle',7,20));assert(m.failure and not m.pending)
    m:expect('timeout',7,30);m:expire(35,7);assert(m.failure and not m.pending and m.snapshot.rows[1].item==32971)
    local root={};local inbox=WoweeBotInbox.new(root,'TBC QA','Jizzle')
    assert(inbox:itemUse(M.decode(wire),1001));assert(#inbox.data.entries==1)
    assert(inbox.data.entries[1].kind=='note') -- continue useful work; not a character-wide blocker
    assert(inbox:dismiss(inbox.data.entries[1].key));assert(not inbox:itemUse(M.decode(wire),1010))
    assert(inbox.data.entries[1].dismissed)
    assert(inbox:itemUse(M.decode(body(1011,0,0)),1012));assert(not inbox.data.entries[1].dismissed)
    assert(inbox.data.entries[1].title=='Pickup limit cleared: Water Bucket')
    assert(not inbox:itemUse(M.decode(wire),1013)) -- older facts cannot resurrect the warning
    local saved=WoweeBotInbox.new(root,'TBC QA','Jizzle');assert(#saved.data.entries==1)
    assert(#WoweeBotInbox.new(root,'Other realm','Jizzle').data.entries==0)

    local oldClock,oldSend=GetTime,SendChatMessage
    local clock,sent=oldClock()+400,{}
    GetTime=function()return clock end
    SendChatMessage=function(command)sent[#sent+1]=command end
    local event=WoweeBotController:GetScript('OnEvent')
    local notes=WoweeBotNotesDriver:GetScript('OnEvent')
    local tick=function()WoweeBotController:GetScript('OnUpdate')()end
    local function receive(prefix,text,sender)event(WoweeBotController,'CHAT_MSG_ADDON',prefix,text,'WHISPER',sender or 'Jizzle')end
    local function latest(verb)
        for i=#sent,1,-1 do local id=sent[i]:match('^%.bot idle ([%w_-]+) '..verb);if id then return id end end
        error('Missing '..verb..' request')
    end
    notes(WoweeBotNotesDriver,'PLAYER_LEAVING_WORLD');WoweeBotNotesData={};notes(WoweeBotNotesDriver,'PLAYER_ENTERING_WORLD')
    event(WoweeBotController,'PLAYER_ENTERING_WORLD');tick()
    local id=latest('status');receive('WIDLE','1\t'..id..'\tok\tbot\t71\t0\tnone')
    receive('WIDLEICAP','1\t'..id..'\t71\titem_use_1')
    receive('WIDLECAP','1\t'..id..'\t71\tactivity_1')
    clock=clock+0.3;tick();local request=latest('snapshot')
    receive('WIDLEITEM',packet(request,71,1,1,wire),'Other')
    SlashCmdList.WOWEEBOTNOTES('');assert(not WoweeBotNotesRow1.entry or WoweeBotNotesRow1.entry.title~='Pickup skipped: Water Bucket')
    receive('WIDLEITEM',packet(request,71,1,1,wire))
    assert(WoweeBotNotesRow1.entry.title=='Pickup skipped: Water Bucket')
    WoweeBotNotesRow1:GetScript('OnClick')()
    assert(WoweeBotNotesDetail:GetText():find('Skipped Water Barrel',1,true))
    assert(WoweeBotNotesDetail:GetText():find('continue other work',1,true))
    assert(WoweeBotStatusMode:GetText()=='Bot is playing (server confirmed)')
    -- A separate report's bad fragments/timeout leave the control driver alive.
    clock=clock+5;tick();assert(type(WoweeBotController:GetScript('OnUpdate'))=='function')
    GetTime,SendChatMessage=oldClock,oldSend
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
end
print('Unique-item skip transport, Notes dedup/dismissal/resolution/realm scope and native UI passed')
