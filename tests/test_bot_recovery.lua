if not WoweeBotStatus then dofile('addons/WoweeBotStatus/Status.lua') end
if type(rawget(_G,'WoweeBotRecovery'))~='table' then dofile('addons/WoweeBotStatus/Recovery.lua') end
if not WoweeBotInbox then dofile('addons/WoweeBotStatus/Inbox.lua') end
local R=WoweeBotRecovery
local function body(phase,stationary,notes)
    local h={'1','1000_1',tostring(phase or 6),'1000','10','200','0','52','104','50','133','4','999',tostring(stationary or 130),'0',tostring(#(notes or {}))}
    return table.concat(h,'\t')..(#(notes or {})>0 and ('\n'..table.concat(notes,'\n')) or '')
end
local text=body(6,130,{'1\t900\t1\t10\t0','2\t1000\t2\t10\t130'})
local row=assert(R.decode(text));assert(row.phase==6 and row.notes[2].seconds==130)
assert(not R.decode(text..'\n'))
assert(not R.decode(text:gsub('1000_1','bad|name')))
assert(not R.decode(text:gsub('52\t104','-1\t104')))
assert(not R.decode(body(11,0,{})))
assert(not R.decode(body(6,0,{'1\t900\t2\t10\t119'})))
assert(not R.decode(body(6,0,{'2\t900\t1\t10\t0','1\t901\t1\t10\t0'})))
assert(not R.decode(body(6,0,{'1\t900\t1\t9\t0'})))
local c=R.new();local control={ackId='ack',generation=7}
c:capability('WIDLERCAP','1\tack\t7\trecovery_1','Other','Jizzle',control);assert(not c.supported)
c:capability('WIDLERCAP','1\told\t7\trecovery_1','Jizzle','Jizzle',control);assert(not c.supported)
c:capability('WIDLERCAP','1\tack\t6\trecovery_1','Jizzle','Jizzle',control);assert(not c.supported)
c:capability('WIDLERCAP','1\tack\t7\trecovery_1','Jizzle','Jizzle',control);assert(c.supported)
local function deliver(text,id,generation,sender,now)
    local count=math.ceil(#text/160);local accepted
    for i=count,1,-1 do
        accepted=c:receive('WIDLEREC','1\t'..(id or 'snap')..'\t'..(generation or 7)..'\t'..i..'\t'..count..'\t'..text:sub((i-1)*160+1,i*160),sender or 'Jizzle','Jizzle',7,now or 1) or accepted
    end
    return accepted
end
c:expect('snap',7,0);assert(not deliver(text,'wrong'));assert(not deliver(text,'snap',6));assert(not deliver(text,'snap',7,'Other'))
assert(deliver(text));assert(c.snapshot and c:currentWork(1):find('Spirit%-healer'))
assert(c:summary(1):find('death counter 10',1,true) and c:summary(1):find('see Notes',1,true))
assert(not c:currentWork(22) and c:summary(22):find('stale',1,true))
c:expect('later',7,23);c:expire(28,7);assert(c.failure and not c.pending)
c:expect('snap',7,30);assert(not deliver(text,'snap',7,'Jizzle',29));c:expire(31,8);assert(c.failure and not c.pending)
c:expect('snap',7,32)
assert(not c:receive('WIDLEREC','1\tsnap\t7\t1\t2\ta','Jizzle','Jizzle',7,33))
assert(c:receive('WIDLEREC','1\tsnap\t7\t1\t2\tb','Jizzle','Jizzle',7,33) and c.failure)
local inbox=WoweeBotInbox.new({},'recovery realm','Jizzle')
assert(inbox:recovery(row,1000));assert(not inbox:recovery(row,1001))
assert(inbox:find('recovery:1000_1:2').kind=='blocked')
local manual=assert(R.decode(body(1,0,{'1\t900\t1\t10\t0','2\t1000\t2\t10\t130'})))
inbox:recovery(manual,1002);assert(inbox:find('recovery:1000_1:2').kind=='blocked')
manual.phase=3;inbox:recovery(manual,1003);assert(inbox:find('recovery:1000_1:2').kind=='blocked')
local alive=assert(R.decode(body(9,0,{'2\t1000\t2\t10\t130','3\t1001\t4\t10\t0'})))
assert(inbox:recovery(alive,1004));assert(inbox:find('recovery:1000_1:2').kind=='note')
assert(not inbox:recovery({epoch='bad',notes={false}},1005))

-- The model checks above also run standalone. When real frames are loaded,
-- verify wire -> callback -> visible primary recovery line and durable Notes.
if WoweeBotController and WoweeBotNotesDriver then
    local old={clock=GetTime,wall=GetServerTime,unit=UnitName,send=SendChatMessage,
        identity=GetWoweeRealmIdentity,realm=GetRealmName,data=WoweeBotNotesData,points=UnitCharacterPoints,shown=WoweeBotNotesFrame:IsShown()}
    local clock,sent=0,{}
    GetTime=function() return clock end;GetServerTime=function() return 1000+clock end
    UnitName=function(u) if u=='player' then return 'Jizzle' end return old.unit(u) end
    GetWoweeRealmIdentity=function() return 'recovery-ui-realm' end;GetRealmName=function() return 'Recovery UI' end
    WoweeBotNotesData={};UnitCharacterPoints=function() return 0 end
    SendChatMessage=function(command) sent[#sent+1]=command end
    local event=WoweeBotController:GetScript('OnEvent');local tick=function() WoweeBotController:GetScript('OnUpdate')() end
    local function receive(prefix,body,sender) event(WoweeBotController,'CHAT_MSG_ADDON',prefix,body,'WHISPER',sender or 'Jizzle') end
    event(WoweeBotController,'PLAYER_ENTERING_WORLD');WoweeBotNotesDriver:GetScript('OnEvent')(WoweeBotNotesDriver,'PLAYER_ENTERING_WORLD');tick()
    SlashCmdList.WOWEEBOTNOTES('')
    local id=assert(sent[#sent]:match('^%.bot idle ([%w_-]+) status$'))
    receive('WIDLE','1\t'..id..'\tok\tbot\t7\t0\tnone')
    -- The server advertises recovery first: native observers may request a
    -- snapshot synchronously inside the legacy capability callback.
    receive('WIDLERCAP','1\t'..id..'\t7\trecovery_1')
    receive('WIDLECAP','1\t'..id..'\t7\tactivity_1');clock=1;tick()
    local snap
    for _,command in ipairs(sent) do snap=command:match('^%.bot idle ([%w_-]+) snapshot 7$') or snap end
    assert(snap)
    local function frame(prefix,content,sender)
        local count=math.ceil(#content/160)
        for i=count,1,-1 do receive(prefix,'1\t'..snap..'\t7\t'..i..'\t'..count..'\t'..content:sub((i-1)*160+1,i*160),sender) end
    end
    local activity={'bot','dead','trainer','traveling','0','-1','none','123','900','4c65652042726f776e','-','-','none','0','-','-','-','0','0'}
    frame('WIDLEACT',table.concat(activity,'\t'))
    frame('WIDLEREC',text,'Other');clock=1.3;tick();assert(not WoweeBotStatusInteraction:GetText():find('Spirit%-healer'))
    frame('WIDLEREC',text);clock=1.6;tick()
    assert(WoweeBotStatusInteraction:GetText():find('Spirit%-healer'))
    assert(WoweeBotStatusGoal:GetText():find('Lee Brown',1,true))
    assert(WoweeBotStatusTravel:GetText():find('death counter 10',1,true))
    assert(WoweeBotNotesSummary:GetText():find('1 blocked',1,true))
    event(WoweeBotController,'PLAYER_LEAVING_WORLD')
    GetTime,GetServerTime,UnitName,SendChatMessage=old.clock,old.wall,old.unit,old.send
    GetWoweeRealmIdentity,GetRealmName,WoweeBotNotesData,UnitCharacterPoints=old.identity,old.realm,old.data,old.points
    if not old.shown then WoweeBotNotesFrame:Hide() end
end
print('Recovery decoder, authority, freshness, historical Notes and optional real-frame checks passed')
