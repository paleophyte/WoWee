-- Run with the bundled Lua 5.1 interpreter from the wow-idle workspace.
do
local model=WoweeBotGatherTools.new()
local control={ackId='hello',generation=2}
model:capability('WIDLETCAP','1\thello\t2\tgather_tools_1','Other','Jizzle',control)
assert(not model.supported)
model:capability('WIDLETCAP','1\thello\t1\tgather_tools_1','Jizzle','Jizzle',control)
assert(not model.supported)
model:capability('WIDLETCAP','1\thello\t2\tgather_tools_1','Jizzle','Jizzle',control)
assert(model.supported)
local body='1\t1700000000_1\t3\n1\t100\t7005\t1\n2\t100\t7005\t0\n3\t100\t7005\t1'
local decoded=WoweeBotGatherTools.decode(body)
assert(decoded and #decoded.rows==3)
for _,bad in ipairs({body..'\n',body:gsub('7005','42'),body:gsub('3\t100','4\t100'),body:gsub('7005\t1','7005\t2'),body:gsub('1700000000_1','none'),body:gsub('\t3\n','\t17\n')}) do
 assert(not WoweeBotGatherTools.decode(bad))
end
model:expect('snap',2,10)
assert(not model:receive('WIDLETOOLS','1\tsnap\t2\t1\t1\t'..body,'Other','Jizzle',2,11))
assert(not model:receive('WIDLETOOLS','1\tsnap\t1\t1\t1\t'..body,'Jizzle','Jizzle',2,11))
local split=math.floor(#body/2)
assert(not model:receive('WIDLETOOLS','1\tsnap\t2\t2\t2\t'..body:sub(split+1),'Jizzle','Jizzle',2,11))
assert(model:receive('WIDLETOOLS','1\tsnap\t2\t1\t2\t'..body:sub(1,split),'Jizzle','Jizzle',2,11))
assert(model.snapshot and not model.failure)
local root={};local inbox=WoweeBotInbox.new(root,'TBC','Jizzle')
assert(inbox:gatherTools(model.snapshot,11))
assert(#inbox.data.entries==3)
assert(inbox.data.entries[1].title=='Gathering skipped: missing Skinning Knife')
assert(inbox.data.entries[2].title=='Gathering tool available: Skinning Knife')
assert(inbox.data.entries[2].text:find('does not mean any gathering has completed',1,true))
assert(not inbox:gatherTools(model.snapshot,12))
inbox.data.entries={} -- User cleared the FYI notes; replay must not resurrect them.
inbox=WoweeBotInbox.new(root,'TBC','Jizzle')
assert(not inbox:gatherTools(model.snapshot,13) and #inbox.data.entries==0)
local other=WoweeBotInbox.new(root,'Classic','Jizzle')
assert(other:gatherTools(model.snapshot,13) and #other.data.entries==3)
local accepted=model.snapshot
model:expect('next',2,20)
assert(not model:receive('WIDLETOOLS','1\tnext\t2\t1\t2\tfirst','Jizzle','Jizzle',2,21))
assert(model:receive('WIDLETOOLS','1\tnext\t2\t1\t2\tconflict','Jizzle','Jizzle',2,21))
assert(model.failure and model.snapshot==accepted) -- Snapshot retained, never replaced by malformed input.
assert(model.snapshot.epoch=='1700000000_1')
model:expect('timeout',2,30);model:expire(35,2)
assert(not model.pending and model.failure and model.snapshot.epoch=='1700000000_1')
model:expect('stale',2,40);model:expire(41,3)
assert(not model.pending and model.failure)
end
do
    local oldClock,oldSend=GetTime,SendChatMessage
    local clock,sent=oldClock()+450,{}
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
    local id=latest('status');receive('WIDLE','1\t'..id..'\tok\tbot\t81\t0\tnone')
    receive('WIDLETCAP','1\t'..id..'\t81\tgather_tools_1')
    receive('WIDLECAP','1\t'..id..'\t81\tactivity_1')
    clock=clock+0.3;tick();local request=latest('snapshot')
    local wire='1\t1792450000_8\t1\n1\t1000\t7005\t1'
    local packet='1\t'..request..'\t81\t1\t1\t'..wire
    receive('WIDLETOOLS',packet,'Other')
    SlashCmdList.WOWEEBOTNOTES('')
    assert(not WoweeBotNotesRow1.entry or WoweeBotNotesRow1.entry.title~='Gathering skipped: missing Skinning Knife')
    receive('WIDLETOOLS',packet)
    assert(WoweeBotNotesRow1.entry.title=='Gathering skipped: missing Skinning Knife')
    WoweeBotNotesRow1:GetScript('OnClick')()
    assert(WoweeBotNotesDetail:GetText():find('no Skinning Knife was carried',1,true))
    assert(WoweeBotNotesDetail:GetText():find('Other eligible work can continue',1,true))
    assert(WoweeBotStatusMode:GetText()=='Bot is playing (server confirmed)')
    for _,command in ipairs(sent)do assert(not command:find('grind ',1,true) and not command:find(' control ',1,true)) end
    clock=clock+5;tick();assert(type(WoweeBotController:GetScript('OnUpdate'))=='function')
    GetTime,SendChatMessage=oldClock,oldSend
    event(WoweeBotController,'PLAYER_ENTERING_WORLD')
end
print('Gathering prerequisite Notes and native controller/frame integration passed')
