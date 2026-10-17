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
assert(not c.intent) -- Refresh is read-only even after a rejected mutation
assert(c:update(40) == '.bot idle test_7 status')
assert(c:update(43) == '.bot idle test_7 status')
assert(c:update(46) == '.bot idle test_7 status')
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

-- Policy operations have their own typed replies and never confirm AI control.
local p = Control.new('policy')
assert(p:update(0) == '.bot idle policy_1 status')
assert(p:receive('WIDLE','1\tpolicy_1\tok\tmanual\t1\t0\tnone','Jizzle','Jizzle',0))
assert(not p:capability('WIDLECAP','1\told\t1\tsocial_1','Jizzle','Jizzle'))
assert(not p:capability('WIDLECAP','1\tpolicy_1\t1\tsocial_1','Other','Jizzle'))
assert(p:capability('WIDLECAP','1\tpolicy_1\t1\tactivity_1,social_1','Jizzle','Jizzle'))
assert(p:update(1) == '.bot idle policy_2 social 1 status')
local function policy(result, enabled, storage, reason, now, generation, id, sender)
    return p:receive('WIDLEPOL','1\t' .. (id or p.pending.id) .. '\t' .. (generation or p.generation) ..
        '\t' .. result .. '\t' .. enabled .. '\t' .. storage .. '\t' .. reason,
        sender or 'Jizzle','Jizzle',now)
end
assert(not p:receive('WIDLE','1\tpolicy_2\tok\tbot\t1\t0\tnone','Jizzle','Jizzle',1))
assert(not policy('ok',0,'stored','none',1,nil,nil,'Other'))
assert(policy('ok',0,'stored','none',1))
assert(p:socialFresh(1) and p.socialEnabled == false)
assert(p.mode == 'manual' and p.generation == 1 and p.confirmedAt == 0)
p:wantSocial(true)
assert(p:update(2) == '.bot idle policy_3 social 1 on')
assert(policy('error',0,'unavailable','persistence_failed',2))
assert(p.socialFailure and not p.failure and p.mode == 'manual')
p:refreshSocial()
assert(p:update(3) == '.bot idle policy_4 social 1 status')
assert(policy('ok',0,'stored','none',3))
p:wantSocial(true)
assert(p:update(4) == '.bot idle policy_5 social 1 on')
p:want('bot') -- takeover supersedes any unconfirmed policy mutation
assert(not policy('ok',1,'stored','none',4,1,'policy_5'))
assert(p:update(4) == '.bot idle policy_6 control 1 bot')
assert(p:receive('WIDLE','1\tpolicy_6\tok\tbot\t2\t0\tnone','Jizzle','Jizzle',4))
assert(p:capability('WIDLECAP','1\tpolicy_6\t2\tsocial_1','Jizzle','Jizzle'))
assert(p:update(5) == '.bot idle policy_7 social 2 status')
assert(policy('ok',0,'stored','none',5))
p:wantSocial(true)
assert(p:update(6) == '.bot idle policy_8 social 2 on')
assert(not policy('ok',0,'stored','none',6)) -- contradictory success isn't confirmation
assert(not policy('ok',1,'unavailable','none',6))
assert(policy('ok',1,'stored','none',6))
assert(p.socialEnabled and p.mode == 'bot' and p.confirmedAt == 4)
assert(p:update(40) == '.bot idle policy_9 status') -- control freshness has priority
assert(p:receive('WIDLE','1\tpolicy_9\tok\tbot\t2\t0\tnone','Jizzle','Jizzle',40))
assert(p:update(40) == '.bot idle policy_10 social 2 status')
assert(p:update(43) == '.bot idle policy_10 social 2 status')
assert(p:update(46) == '.bot idle policy_10 social 2 status')
assert(not p:update(49))
assert(p.socialFailure and not p.failure and p:isBot(49))
p:want('manual') -- failed policy query must not prevent taking control
assert(p:update(49) == '.bot idle policy_11 control 2 manual')
assert(p:receive('WIDLE','1\tpolicy_11\tok\tmanual\t3\t0\tnone','Jizzle','Jizzle',49))
assert(p:capability('WIDLECAP','1\tpolicy_11\t3\tsocial_1','Jizzle','Jizzle'))
assert(p:update(50) == '.bot idle policy_12 social 3 status')
assert(policy('error',0,'stored','stale_generation',50,4))
assert(not p:fresh(50) and p.generation == 3)
assert(p:update(51) == '.bot idle policy_13 status')
assert(p:receive('WIDLE','1\tpolicy_13\tok\tmanual\t4\t0\tnone','Jizzle','Jizzle',51))
assert(p:capability('WIDLECAP','1\tpolicy_13\t4\tactivity_1','Jizzle','Jizzle'))
assert(not p.socialSupported and not p:update(52)) -- unsupported realms get no policy commands
print('Social policy protocol scenarios passed')

-- Recovery observes the server and never revives a timed-out change of control.
local r = Control.new('recover')
assert(r:update(0) == '.bot idle recover_1 status')
assert(r:receive('WIDLE','1\trecover_1\tok\tmanual\t1\t0\tnone','Jizzle','Jizzle',0))
r:want('bot')
assert(r:update(1) == '.bot idle recover_2 control 1 bot')
assert(r:update(4) == '.bot idle recover_2 control 1 bot')
assert(r:update(7) == '.bot idle recover_2 control 1 bot')
assert(not r:update(10))
assert(r.failure and not r.intent and not r.pending and r.recoverAt == 40)
assert(not r:update(39))
assert(r:update(40) == '.bot idle recover_3 status')
assert(not r:receive('WIDLE','1\trecover_2\tok\tbot\t2\t0\tnone','Jizzle','Jizzle',40))
assert(not r:isBot(40))
assert(r:update(43) == '.bot idle recover_3 status')
assert(r:update(46) == '.bot idle recover_3 status')
assert(not r:update(49))
assert(r.recoverAt == 109) -- repeated loss backs off rather than querying every frame
assert(not r:update(108))
assert(r:update(109) == '.bot idle recover_4 status')
assert(r:receive('WIDLE','1\trecover_4\tok\tmanual\t2\t0\tnone','Jizzle','Jizzle',110))
assert(not r.failure and not r.intent and not r:isBot(110) and not r:update(110))
assert(r.recoveryDelay == nil and r.recoverAt == nil)
assert(table.concat(r:diagnostic(110),'\n'):find('recover_4 status',1,true))
assert(r.lastRejected and r.acceptedAt == 110)

-- Losing an ordinary observation recovers bot visibility, not a gameplay mutation.
assert(r:update(118) == '.bot idle recover_5 status')
assert(r:update(121) == '.bot idle recover_5 status')
assert(r:update(124) == '.bot idle recover_5 status')
assert(not r:update(127))
assert(r:update(157) == '.bot idle recover_6 status')
assert(r:receive('WIDLE','1\trecover_6\tok\tbot\t3\t0\tnone','Jizzle','Jizzle',157))
assert(r:isBot(157) and not r:update(157))

-- Explicit Refresh abandons an in-flight intent even before it times out.
r:want('manual')
assert(r:update(158) == '.bot idle recover_7 control 3 manual')
r:refresh()
assert(not r.intent and r:update(158) == '.bot idle recover_8 status')
assert(not r:receive('WIDLE','1\trecover_7\tok\tmanual\t4\t0\tnone','Jizzle','Jizzle',158))
assert(r:receive('WIDLE','1\trecover_8\tok\tmanual\t4\t0\tnone','Jizzle','Jizzle',159))
assert(not r:update(159) and not r:isBot(159))
assert(not r:receive('WIDLE','1\trecover_8\tok\tmanual\t4\t0\tnone','Jizzle','Jizzle',159))
assert(r.lastRejected == 'no pending control request')
local b = Control.new('backoff')
local now = 0
for cycle = 1, 5 do
    assert(b:update(now) == '.bot idle backoff_' .. cycle .. ' status')
    assert(b:update(now+3) == '.bot idle backoff_' .. cycle .. ' status')
    assert(b:update(now+6) == '.bot idle backoff_' .. cycle .. ' status')
    assert(not b:update(now+9))
    assert(b.recoveryDelay == math.min(15*2^cycle,120))
    assert(not b:update(b.recoverAt-1))
    now = b.recoverAt
end
print('Read-only control recovery scenarios passed')
