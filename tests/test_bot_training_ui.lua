-- Exercise the real Notes frame through correlated controller replies, then
-- recheck in Manual without sending a gameplay/control mutation.
do
    local oldClock,oldWall,oldSend=GetTime,GetServerTime,SendChatMessage
    local clock,sent=oldClock()+200,{}
    GetTime=function() return clock end
    GetServerTime=function() return 1792255000+clock end
    SendChatMessage=function(command) sent[#sent+1]=command end
    local event=WoweeBotController:GetScript('OnEvent')
    local notes=WoweeBotNotesDriver:GetScript('OnEvent')
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
    notes(WoweeBotNotesDriver,'PLAYER_LEAVING_WORLD')
    WoweeBotNotesData={}
    notes(WoweeBotNotesDriver,'PLAYER_ENTERING_WORLD')
    event(WoweeBotController,'PLAYER_ENTERING_WORLD');tick()
    local id=latest('status')
    receive('WIDLE','1\t'..id..'\tok\tbot\t40\t0\tnone')
    receive('WIDLECAP','1\t'..id..'\t40\tactivity_1,history_1,training_1')
    clock=clock+0.3;tick()
    local request=latest('snapshot')
    local function body(state,money,spendable)
        return '1\tok\t1\n202\t7\t8\t1792255000\t201\t'..state..'\t100\t'..money..'\t'..spendable..
            '\t'..hex('Polearms')..'\t'..hex('Weapon Master')
    end
    local function deliver(data,generation,sender)
        local count=math.ceil(#data/160)
        for i=count,1,-1 do receive('WIDLETRAIN','1\t'..request..'\t'..generation..'\t'..i..'\t'..count..'\t'..
            data:sub((i-1)*160+1,i*160),sender) end
    end
    deliver(body('coins',25,10),40,'Other')
    SlashCmdList.WOWEEBOTNOTES('')
    assert(WoweeBotNotesSummary:GetText():find('0 blocked',1,true))
    deliver(body('coins',25,10),40)
    assert(WoweeBotNotesSummary:GetText():find('1 blocked',1,true))
    WoweeBotNotesRow1:GetScript('OnClick')()
    assert(WoweeBotNotesDetail:GetText():find('Missing from wallet: 0g 0s 75c',1,true))
    assert(WoweeBotNotesDetail:GetText():find('Weapon Master',1,true))
    assert(WoweeBotStatusMode:GetText()=='Bot is playing (server confirmed)')
    -- Hand off normally; the additive training query is read-only in Manual.
    WoweeBotStatusPlay:GetScript('OnClick')();clock=clock+0.3;tick()
    id=latest('control')
    receive('WIDLE','1\t'..id..'\tok\tmanual\t41\t0\tnone')
    receive('WIDLECAP','1\t'..id..'\t41\tactivity_1,training_1')
    clock=clock+2.1;tick()
    request=latest('snapshot')
    deliver(body('learned',200,0),41)
    assert(WoweeBotNotesDetail:GetText():find('Confirmed learned',1,true))
    assert(WoweeBotNotesSummary:GetText():find('0 blocked',1,true))
    assert(WoweeBotStatusMode:GetText()=='I am playing (server confirmed)')
    for _,command in ipairs(sent) do assert(not command:find(' grind ',1,true)) end
    assert(type(WoweeBotController:GetScript('OnUpdate'))=='function')
    WoweeBotNotesFrame:Hide()
    GetTime,GetServerTime,SendChatMessage=oldClock,oldWall,oldSend
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
end
print('Training Notes detail, sender protection, manual reconciliation and control independence passed')
