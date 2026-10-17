if not WoweeBotStatus then dofile('addons/WoweeBotStatus/Status.lua') end
if not WoweeBotControl then dofile('addons/WoweeBotStatus/Control.lua') end
local Control = WoweeBotControl
local c = Control.new('test')
local function ack(id, mode, generation, grind, now, result, reason)
    return c:receive('WIDLE', '1\t' .. id .. '\t' .. (result or 'ok') .. '\t' .. mode ..
        '\t' .. generation .. '\t' .. grind .. '\t' .. (reason or 'none'), 'Jizzle', 'Jizzle', now)
end
assert(not c:isBot(0))
assert(c:update(0) == '.bot idle test_1 status')
assert(not c:update(1))
assert(not c:receive('WIDLE', '1\ttest_1\tok\tbot\t1\t0\tnone', 'Other', 'Jizzle', 1))
assert(not ack('wrong', 'bot', 1, 0, 1))
assert(not ack('test_1', 'bot', 0, 0, 1))
assert(not c:isBot(1))
assert(ack('test_1', 'bot', 1, 0, 1))
assert(c:isBot(1))
assert(not ack('test_1', 'manual', 2, 0, 1)) -- duplicate cannot overwrite state
c:planGrind(true)
assert(c:update(1) == '.bot idle test_2 grind 1 on')
assert(c:isBot(1))
c:want('manual') -- takeover supersedes an in-flight grind request
assert(not c:isBot(1))
assert(c:update(1) == '.bot idle test_3 control 1 manual')
assert(not ack('test_2', 'bot', 1, 1, 2))
assert(not c:update(3))
assert(c:update(4) == '.bot idle test_3 control 1 manual')
assert(ack('test_3', 'manual', 2, 0, 5))
assert(not c:isBot(5))
assert(c:label(5) == 'I am playing (server confirmed)')
c:want('bot')
assert(c:update(6) == '.bot idle test_4 control 2 bot')
assert(not c:isBot(6))
assert(ack('test_4', 'bot', 3, 0, 7))
assert(c:isBot(7))
assert(not c:isBot(33)) -- stale confirmation suspends automation
assert(c:update(33) == '.bot idle test_5 status')
assert(not ack('test_5', 'bot', 2, 1, 33)) -- older epoch is rejected
assert(ack('test_5', 'manual', 4, 0, 34)) -- external takeover is observed
assert(not c:isBot(34))
c:want('bot')
assert(c:update(35) == '.bot idle test_6 control 4 bot')
assert(ack('test_6', 'manual', 5, 0, 36, 'error', 'stale_generation'))
assert(not c:isBot(36))
assert(c.failure:find('stale_generation', 1, true))
assert(not c:update(40)) -- errors never trigger an unbounded command loop
c:refresh()
assert(c:update(40) == '.bot idle test_7 control 5 bot')
assert(c:update(43) == '.bot idle test_7 control 5 bot')
assert(c:update(46) == '.bot idle test_7 control 5 bot')
assert(not c:update(49))
assert(c.failure)
assert(not ack('test_7', 'bot', 6, 0, 50)) -- timed-out reply cannot resume automation
assert(not c:isBot(50))
c:want('manual')
assert(c:update(51) == '.bot idle test_8 status') -- refresh stale state before a change
assert(ack('test_8', 'manual', 6, 0, 52))
assert(not c.intent) -- already in requested state; no blind toggle
assert(not c:update(52))
print('Bot control protocol scenarios passed')
