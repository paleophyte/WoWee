-- Run by the real Lua engine in test_game_replay, or by Lua 5.1 from the repo.
if not WoweeBotStatus then dofile('addons/WoweeBotStatus/Status.lua') end
local Status = WoweeBotStatus
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
