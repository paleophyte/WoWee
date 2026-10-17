if not WoweeBotInbox then dofile('addons/WoweeBotStatus/Inbox.lua') end
local Inbox = WoweeBotInbox
local root = {}
local a = assert(Inbox.new(root, 'realm-a', 'Jizzle'))
assert(not Inbox.new(root, '', 'Jizzle'))
assert(not Inbox.new(root, 'realm-a', 'Unknown'))
assert(a:talents(1, 100))
assert(not a:talents(1, 101))
assert(a:counts().note == 1 and a:counts().unread == 1)
assert(a:dismiss('talents'))
assert(not a:talents(1, 102)) -- dismissal survives polling/relog until the count changes
assert(a:counts().note == 0)
a = Inbox.new(root, 'realm-a', 'Jizzle')
assert(not a:talents(1, 103) and a:counts().note == 0)
assert(a:talents(2, 104) and a:counts().note == 1)
assert(a:talents(0, 105) and not a:find('talents'))
assert(a:learn(133, 'Fireball', 'Rank 1', 110))
assert(a:learn(116, 'Frostbolt', 'Rank 1', 111))
assert(not a:learn(116, 'Frostbolt', 'Rank 1', 112))
local learned = a:visible()[1]
assert(#learned.spells == 2 and learned.title == 'New abilities x2')
assert(a:learn(143, 'Fireball', 'Rank 2', 140))
assert(a:counts().note == 2) -- a later training visit forms a new group
assert(a:put('control', 'blocked', 'Control missing', 'Refresh', 150))
assert(not a:put('control', 'blocked', 'Control missing', 'Refresh', 151))
assert(a:find('control').updated == 150)
assert(a:put('reward:6', 'decision', 'Choose reward', 'Waiting', 152))
assert(a:visible()[1].kind == 'decision')
assert(not a:dismiss('reward:6') and not a:dismiss('control'))
assert(a:clearNotes())
assert(a:counts().note == 0 and a:counts().decision == 1 and a:counts().blocked == 1)
assert(a:resolve('control', 155, 'Confirmed again'))
assert(not a:resolve('reward:6', 156, 'Unsafe'))
assert(not a:resolve('control', 156, 'Duplicate resolution'))
assert(a:counts().blocked == 0 and a:counts().note == 1)
assert(a:find('control').text == 'Confirmed again')
local b = Inbox.new(root, 'realm-b', 'Jizzle')
assert(b:counts().decision == 0 and #b:visible() == 0)
local alt = Inbox.new(root, 'realm-a', 'Other')
assert(#alt:visible() == 0)
-- Opening the same scope retains unresolved work and dismissal state. The
-- native replay test separately exercises real SavedVariables serialization.
assert(Inbox.new(root, 'realm-a', 'Jizzle'):counts().decision == 1)
for i = 1, 120 do a:put('fyi:' .. i, 'note', 'Note', tostring(i), 200 + i) end
assert(#a.data.entries == Inbox.LIMIT and a:find('reward:6'))
assert(a:put('blocked', 'blocked', 'Blocked', 'Waiting', 500))
for i = 1, 120 do a:put('later:' .. i, 'note', 'Note', tostring(i), 600 + i) end
assert(#a.data.entries == Inbox.LIMIT and a:find('blocked') and a:find('reward:6'))
-- Clock correction must not merge an older training event into a future group.
local c = Inbox.new({}, 'realm', 'Clock')
c:learn(1, 'First', '', 1000); c:learn(2, 'Earlier', '', 900)
assert(#c:visible() == 2)
print('Bot inbox scenarios passed')

-- Count actual supported failure text only, with a fresh control generation.
local e = Inbox.new({}, 'errors', 'Jizzle')
for i = 1, 4 do assert(not e:observeError('Item not ready', i, 1000+i, nil)) end
assert(not e:observeError('Item not ready', 5, 1005, 1))
assert(not e:observeError('Need more items', 6, 1006, 1)) -- separate causes
assert(not e:observeError('Item not ready', 7, 1007, 1))
assert(e:observeError('Item not ready', 8, 1008, 1))
assert(e:counts().note == 1 and e:counts().blocked == 0)
for i = 9, 80 do assert(not e:observeError('Item not ready', i, 1000+i, 1)) end
assert(e:counts().note == 1) -- repeated events don't spam the inbox
assert(not e:observeError('A fish is on your line! Right-click to reel it in.', 81, 1081, 1))
assert(not e:observeError('Need more items', 82, 1082, 1)) -- first observation aged out
assert(not e:observeError('Need more items', 83, 1083, 2)) -- new handoff clears old evidence
assert(not e:observeError('Need more items', 84, 1084, 2))
assert(e:observeError('Need more items', 85, 1085, 2))
assert(e:counts().note == 2)
assert(not e:observeError('Need more items', 86, 1086, nil))
assert(not e:observeError('Need more items', 87, 1087, 2))
assert(not e:observeError('Need more items', 88, 1088, 2))
assert(not e:observeError('Need more items', 1, 1089, 2)) -- monotonic clock reset
assert(not e:observeError('Need more items', 2, 1090, 2))
assert(not e:observeError('Need more items', 3, 1091, 2)) -- same five-minute bucket
assert(not e:observeError('Need more items', 200, 1300, 2))
assert(not e:observeError('Need more items', 201, 1301, 2))
assert(e:observeError('Need more items', 202, 1302, 2)) -- later recurrence can notify
assert(e:counts().note == 3)
local persisted = e.data
local scoped = {scopes = {['6:errors:Jizzle'] = persisted}}
local relog = Inbox.new(scoped, 'errors', 'Jizzle')
assert(relog:counts().note == 3)
for i = 1, 3 do assert(not relog:observeError('Need more items', i, 1303+i, 1)) end
assert(Inbox.new(scoped, 'another realm', 'Jizzle'):counts().note == 0)
print('Repeated bot-control error note scenarios passed')
local prefixed = Inbox.new({}, 'errors', 'Other')
assert(not prefixed:observeError('Fishing: You can\'t fish in this area.', 1, 1001, 1))
assert(not prefixed:observeError('Fishing: You can\'t fish in this area.', 2, 1002, 1))
assert(prefixed:observeError('Fishing: You can\'t fish in this area.', 3, 1003, 1))
assert(prefixed:visible()[1].text:find('Fishing:',1,true))
assert(prefixed:visible()[1].text:find('fishing spot and facing',1,true))
for i=4,6 do assert(not prefixed:observeError('Another Fishing Rank: You can\'t fish in this area.', i, 1000+i, 1)) end
assert(prefixed:counts().note == 1)
for i=7,9 do assert(not prefixed:observeError('Unrelated: unknown reason', i, 1000+i, 1)) end
assert(not prefixed:observeError('Fishing:\nNeed more items', 10, 1010, 1))

-- Exercise the real Notes frame handler as well as the standalone model. The
-- replay harness has already loaded NotesUI; restore globals before its tests.
if WoweeBotNotesDriver then
    local old = {clock=GetTime, wall=GetServerTime, identity=GetWoweeRealmIdentity,
        realm=GetRealmName, confirmed=WoweeBotConfirmedControlGeneration, data=WoweeBotNotesData,
        open=SlashCmdList.WOWEEBOTSTATUS, points=UnitCharacterPoints, shown=WoweeBotNotesFrame:IsShown()}
    local clock, generation, opened = 0, nil, 0
    GetTime=function() return clock end
    GetServerTime=function() return 1000+clock end
    GetWoweeRealmIdentity=function() return 'error-note-test' end
    GetRealmName=function() return 'Error Notes Test' end
    WoweeBotConfirmedControlGeneration=function() return generation end
    WoweeBotNotesData={}
    UnitCharacterPoints=function() return 0 end
    SlashCmdList.WOWEEBOTSTATUS=function() opened=opened+1 end
    local event = WoweeBotNotesDriver:GetScript('OnEvent')
    event(WoweeBotNotesDriver, 'PLAYER_ENTERING_WORLD')
    SlashCmdList.WOWEEBOTNOTES('')
    for i=1,3 do event(WoweeBotNotesDriver,'UI_ERROR_MESSAGE','Item not ready') end
    assert(WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  0 notes', WoweeBotNotesSummary:GetText())
    generation=11
    for i=1,3 do clock=i; event(WoweeBotNotesDriver,'UI_ERROR_MESSAGE','Item not ready') end
    assert(WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  1 notes')
    for i=4,10 do clock=i; event(WoweeBotNotesDriver,'UI_ERROR_MESSAGE',45,'Item not ready') end
    assert(WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  1 notes')
    WoweeBotNotesRow1:GetScript('OnClick')()
    assert(WoweeBotNotesDetail:GetText():find('at least 3 times within 30 seconds',1,true))
    assert(WoweeBotNotesOpen:GetText() == 'Open Bot activity')
    WoweeBotNotesOpen:GetScript('OnClick')(); assert(opened==1)
    event(WoweeBotNotesDriver,'PLAYER_LEAVING_WORLD')
    for i=1,3 do event(WoweeBotNotesDriver,'UI_ERROR_MESSAGE','Need more items') end
    GetTime,GetServerTime,GetWoweeRealmIdentity,GetRealmName=old.clock,old.wall,old.identity,old.realm
    WoweeBotConfirmedControlGeneration,WoweeBotNotesData=old.confirmed,old.data
    SlashCmdList.WOWEEBOTSTATUS=old.open
    UnitCharacterPoints=old.points
    if not old.shown then WoweeBotNotesFrame:Hide() end
    print('Real Notes UI error-event and action scenarios passed')
end
