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
