-- Run by the real Lua engine in test_game_replay, or by Lua 5.1 from the repo.
if not WoweeBotStatus then dofile('addons/WoweeBotStatus/Status.lua') end
local Status = WoweeBotStatus
assert(Status.actionText('|A:seal of blood - PREREQ|A:seal of blood - IMPOSSIBLE|' ..
    'PUSH:seal of blood - 4715.32 (alt)|no actions executed') == 'seal of blood - IMPOSSIBLE')
assert(#Status.actionText(string.rep('x', 300)) == 100)
local state = Status.new()
local travel = 'quest objective travel target\nTarget: kill Young Wolf (objective 1)' ..
    '\nDistance 45y\nLocation: Northshire Valley (45y)' ..
    '\nStatus: traveling [for 120s](retry 1/0)'
local report = 'Traveling 45y to Northshire Valley for ' ..
    '|cffffff00|Hquest:33:2|h[Wolves Across the Border]|h|r to kill Young Wolf (objective 1)'

assert(not state:receive('', travel, 'SomeoneElse', 'Jizzle', 0))
assert(not state:receive('', travel, '', 'Jizzle', 0))
assert(not state:receive('OtherAddon', travel, 'Jizzle', 'Jizzle', 0))
assert(not state:receive('', 'move to travel target', 'Jizzle', 'Jizzle', 0))
state:request(0)
assert(state:receive('', travel, 'jizzle-Realm', 'Jizzle', 0))
assert(state:isFresh(0))
assert(state.distance == 45)
assert(state.status == 'traveling(retry 1/0)') -- lifetime is not an ETA; retries survive
assert(not state:currentQuest(0)) -- an NPC name is not enough to identify a quest
assert(state:receive('BOT', report, 'Jizzle', 'Jizzle', 0))
local id, title = state:currentQuest(0)
assert(id == 33 and title == 'Wolves Across the Border')
assert(state:receive('', 'move to travel target', 'Jizzle', 'Jizzle', 0))
assert(state.action == 'move to travel target')
assert(not state:receive('', 'another action', 'Jizzle', 'Jizzle', 1))
assert(not state:isFresh(26))
assert(not state:currentQuest(26))

-- Replies can arrive out of order without throwing away the matching quest.
state = Status.new()
state:receive('BOT', report, 'Jizzle', 'Jizzle', 1)
state:receive('', travel, 'Jizzle', 'Jizzle', 2)
assert(state:currentQuest(2) == 33)
state:receive('', 'rpg travel target\nTarget: sell items to Brother Danil\nDistance 8y\nStatus: working [for 30s]', 'Jizzle', 'Jizzle', 3)
assert(not state:currentQuest(3))
assert(state.goal == 'sell items to Brother Danil')
assert(state.status == 'working')

-- Ordinary quest links from other BOT output do not select an active quest.
assert(not state:receive('BOT', 'Quest list: |Hquest:33:2|h[Wolves Across the Border]|h', 'Jizzle', 'Jizzle', 3))
state:request(4)
assert(not state:receive('', 'late action', 'Jizzle', 'Jizzle', 9))
state:receive('', 'invalid command: traveldetail', 'Jizzle', 'Jizzle', 10)
assert(state.unsupported)
state.disabled = true
assert(not state:isFresh(10))
print('Bot status model scenarios passed')

if not WoweeBotGrind then dofile('addons/WoweeBotStatus/Grind.lua') end
local Grind = WoweeBotGrind
local controller = Grind.new()
state = Status.new()
state:request(0)
state:receive('', travel, 'Jizzle', 'Jizzle', 0)
state:receive('BOT', report, 'Jizzle', 'Jizzle', 0)
local incomplete, complete = {complete = false}, {complete = true}
assert(state:grindDecision(0, incomplete) == false) -- travel is not grinding
assert(state:grindDecision(0, complete) == false)
assert(state:grindDecision(0, nil) == nil) -- no matching quest, no guessing
local working = travel:gsub('traveling', 'working')
state:receive('', working, 'Jizzle', 'Jizzle', 0)
assert(state:grindDecision(0, incomplete) == true)
assert(controller:update(state, incomplete, 0, false) == nil) -- wait for actual strategies
assert(not controller:receive('BOT', 'Non Combat Strategies: grind', 'Other', 'Jizzle', 0))
assert(not controller:receive('', 'Non Combat Strategies: grind', 'Jizzle', 'Jizzle', 0))
assert(controller:receive('BOT', 'Non Combat Strategies: travel, rpg', 'Jizzle', 'Jizzle', 0))
assert(controller:update(state, incomplete, 0, false) == '#a nc +grind,?')
assert(controller:label(0):find('waiting for server', 1, true))
assert(controller:update(state, incomplete, 1, false) == nil)
assert(controller:receive('BOT', 'Non Combat Strategies: grind, rpg, travel', 'jizzle-Realm', 'Jizzle', 1))
assert(controller:update(state, incomplete, 1, false) == nil)
assert(controller:label(1):find('on (confirmed)', 1, true))
assert(controller:update(state, complete, 1, false) == '#a nc -grind,?')
assert(controller:receive('BOT', 'Non Combat Strategies: travel, rpg', 'Jizzle', 'Jizzle', 2))
assert(controller:update(state, complete, 2, false) == nil)
assert(controller:label(2):find('off (confirmed)', 1, true))
assert(controller:update(state, incomplete, 2, true) == nil)
assert(controller:label(2) == 'Auto grind: paused while grouped')
assert(controller:update(state, incomplete, 26, false) == nil)
assert(controller:label(26):find('waiting for bot status', 1, true))

-- A completed quest supersedes an enable command still queued on the server.
controller = Grind.new()
controller:receive('BOT', 'Non Combat Strategies: travel', 'Jizzle', 'Jizzle', 0)
assert(controller:update(state, incomplete, 0, false) == '#a nc +grind,?')
assert(controller:update(state, complete, 0, false) == '#a nc -grind,?')
assert(not controller:receive('BOT', 'Combat Strategies: grind', 'Jizzle', 'Jizzle', 0))
controller = Grind.new()
controller:receive('BOT', 'Non Combat Strategies: travel', 'Jizzle', 'Jizzle', 0)
assert(controller:update(state, incomplete, 26, false) == nil)

-- Loot from creatures needs combat; collecting from objects does not.
local loot = 'questobjective travel target\nTarget: loot |Hitem:750|h[Tough Wolf Meat]|h from ' ..
    '|Hentry:69:|h[Timber Wolf]|h (objective 1)\nDistance 1y\nStatus: working'
state = Status.new()
state:receive('', loot, 'Jizzle', 'Jizzle', 0)
state:receive('BOT', report, 'Jizzle', 'Jizzle', 0)
assert(state:grindDecision(0, incomplete) == true)
state:receive('', loot:gsub('entry:69:', 'entry:-69:'), 'Jizzle', 'Jizzle', 0)
assert(state:grindDecision(0, incomplete) == false)
state:receive('', 'trainer travel target\nTarget: train a skill\nStatus: working', 'Jizzle', 'Jizzle', 1)
assert(state:grindDecision(1, nil) == false)

-- A jammed server gets bounded retries, not four commands per second forever.
controller = Grind.new()
controller:receive('BOT', 'Non Combat Strategies: grind', 'Jizzle', 'Jizzle', 0)
assert(controller:update(state, nil, 1, false) == '#a nc -grind,?')
assert(controller:update(state, nil, 2, false) == nil)
assert(controller:update(state, nil, 9, false) == '#a nc -grind,?')
assert(controller:update(state, nil, 17, false) == '#a nc -grind,?')
controller:receive('BOT', 'Non Combat Strategies: grind', 'Jizzle', 'Jizzle', 24)
state.travelAt = 24
assert(controller:update(state, nil, 25, false) == nil)
assert(controller:label(25):find('unconfirmed', 1, true))
controller:retry()
assert(controller:update(state, nil, 25, false) == '#a nc -grind,?')
print('Quest grind control scenarios passed')
