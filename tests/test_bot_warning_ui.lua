-- End-to-end real controller frame -> correlated fact -> real Notes frame.
assert(WoweeBotController and WoweeBotNotesDriver, "real addon frames must be loaded")
do
    local old = {clock=GetTime, wall=GetServerTime, unit=UnitName, send=SendChatMessage,
        identity=GetWoweeRealmIdentity, realm=GetRealmName, data=WoweeBotNotesData,
        points=UnitCharacterPoints, shown=WoweeBotNotesFrame:IsShown()}
    local clock, sent = 0, {}
    GetTime=function() return clock end
    GetServerTime=function() return 1000+clock end
    UnitName=function(unit) if unit=='player' then return 'Jizzle' end return old.unit(unit) end
    GetWoweeRealmIdentity=function() return 'server-warning-ui-test' end
    GetRealmName=function() return 'Warning UI Test' end
    WoweeBotNotesData={}
    UnitCharacterPoints=function() return 0 end
    SendChatMessage=function(command) sent[#sent+1]=command end
    local controlEvent=WoweeBotController:GetScript('OnEvent')
    local notesEvent=WoweeBotNotesDriver:GetScript('OnEvent')
    controlEvent(WoweeBotController,'PLAYER_ENTERING_WORLD')
    notesEvent(WoweeBotNotesDriver,'PLAYER_ENTERING_WORLD')
    WoweeBotController:GetScript('OnUpdate')()
    local id
    for _, command in ipairs(sent) do id=id or command:match('^%.bot idle ([%w_-]+) status$') end
    assert(id, 'controller must request actual status')
    controlEvent(WoweeBotController,'CHAT_MSG_ADDON','WIDLE','1\t'..id..'\tok\tmanual\t9\t0\tnone','WHISPER','Jizzle')
    controlEvent(WoweeBotController,'CHAT_MSG_ADDON','WIDLECAP','1\t'..id..'\t9\tactivity_1,warning_1','WHISPER','Jizzle')
    SlashCmdList.WOWEEBOTNOTES('')
    local warning='1\t'..id..'\t9\t1000\t1\t2\t1300'
    controlEvent(WoweeBotController,'CHAT_MSG_ADDON','WIDLEWARN',warning,'WHISPER','Other')
    assert(WoweeBotNotesSummary:GetText()=='0 choices  |  0 blocked  |  0 notes')
    controlEvent(WoweeBotController,'CHAT_MSG_ADDON','WIDLEWARN',warning,'WHISPER','Jizzle')
    assert(WoweeBotNotesSummary:GetText()=='0 choices  |  0 blocked  |  1 notes')
    WoweeBotNotesRow1:GetScript('OnClick')()
    assert(WoweeBotNotesDetail:GetText():find('did not start a fishing channel',1,true))
    assert(WoweeBotNotesOpen:GetText()=='Open Bot activity')
    controlEvent(WoweeBotController,'CHAT_MSG_ADDON','WIDLEWARN',warning,'WHISPER','Jizzle')
    assert(WoweeBotNotesSummary:GetText()=='0 choices  |  0 blocked  |  1 notes')
    assert(#sent==1) -- a historical fact in Manual cannot enable AI or send any command
    controlEvent(WoweeBotController,'PLAYER_LEAVING_WORLD')
    notesEvent(WoweeBotNotesDriver,'PLAYER_LEAVING_WORLD')
    GetTime,GetServerTime,UnitName,SendChatMessage=old.clock,old.wall,old.unit,old.send
    GetWoweeRealmIdentity,GetRealmName,WoweeBotNotesData=old.identity,old.realm,old.data
    UnitCharacterPoints=old.points
    if not old.shown then WoweeBotNotesFrame:Hide() end
    print('Real controller-to-Notes fishing warning scenarios passed')
end

-- The real Fishing buttons query and mutate only their own policy in Manual.
do
    local old={clock=GetTime,unit=UnitName,send=SendChatMessage}
    local clock,sent=0,{}
    GetTime=function() return clock end
    UnitName=function(unit) if unit=='player' then return 'Jizzle' end return old.unit(unit) end
    SendChatMessage=function(command) sent[#sent+1]=command end
    local event=WoweeBotController:GetScript('OnEvent')
    local update=WoweeBotController:GetScript('OnUpdate')
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
    SlashCmdList.WOWEEBOTPOLICY('')
    update()
    local id=assert(sent[#sent]:match('^%.bot idle ([%w_-]+) status$'))
    local function reply(prefix,body) event(WoweeBotController,'CHAT_MSG_ADDON',prefix,body,'WHISPER','Jizzle') end
    reply('WIDLE','1\t'..id..'\tok\tmanual\t5\t0\tnone')
    reply('WIDLECAP','1\t'..id..'\t5\tactivity_1,social_1,fishing_1')
    update()
    id=assert(sent[#sent]:match('^%.bot idle ([%w_-]+) social 5 status$'))
    reply('WIDLEPOL','1\t'..id..'\t5\tok\t0\tstored\tnone')
    update()
    id=assert(sent[#sent]:match('^%.bot idle ([%w_-]+) fishing 5 status$'))
    reply('WIDLEFISH','1\t'..id..'\t5\tok\t0\tstored\tnone')
    SlashCmdList.WOWEEBOTPOLICY('')
    assert(WoweeBotFishingState:GetText()=='Fishing: Off (server confirmed)')
    assert(WoweeBotFishingToggle:IsEnabled() and WoweeBotFishingToggle:GetText()=='Allow fishing')
    assert(WoweeBotSocialState:GetText()=='Social Detours: Off (server confirmed)')
    WoweeBotFishingToggle:GetScript('OnClick')()
    assert(WoweeBotFishingState:GetText()=='Fishing: waiting for confirmation')
    update()
    id=assert(sent[#sent]:match('^%.bot idle ([%w_-]+) fishing 5 on$'))
    reply('WIDLEFISH','1\t'..id..'\t5\tok\t1\tstored\tnone')
    assert(WoweeBotFishingState:GetText()=='Fishing: On (server confirmed)')
    assert(WoweeBotFishingToggle:GetText()=='Disable fishing')
    assert(WoweeBotSocialState:GetText()=='Social Detours: Off (server confirmed)')
    WoweeBotFishingToggle:GetScript('OnClick')();update()
    id=assert(sent[#sent]:match('^%.bot idle ([%w_-]+) fishing 5 off$'))
    reply('WIDLEFISH','1\t'..id..'\t5\tok\t0\tstored\tnone')
    assert(WoweeBotFishingState:GetText()=='Fishing: Off (server confirmed)')
    for _,command in ipairs(sent) do assert(not command:find(' control ',1,true) and not command:find(' grind ',1,true)) end
    event(WoweeBotController,'PLAYER_LEAVING_WORLD')
    GetTime,UnitName,SendChatMessage=old.clock,old.unit,old.send
    WoweeBotPoliciesFrame:Hide()
    print('Real Fishing policy UI and manual-control independence scenarios passed')
end
