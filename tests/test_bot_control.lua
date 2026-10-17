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

-- Correlated server warning facts are read-only and accepted in manual mode.
local w = Control.new('warn')
assert(w:update(0) == '.bot idle warn_1 status')
assert(w:receive('WIDLE','1\twarn_1\tok\tmanual\t7\t0\tnone','Jizzle','Jizzle',0))
local warning = '1\twarn_1\t7\t1000\t1\t1\t1300'
assert(not w:warning('WIDLEWARN',warning,'Jizzle','Jizzle',1)) -- capability required
assert(w:capability('WIDLECAP','1\twarn_1\t7\tactivity_1,warning_1','Jizzle','Jizzle'))
local fact = assert(w:warning('WIDLEWARN',warning,'Jizzle','Jizzle',1))
assert(fact.at==1000 and fact.sequence==1 and fact.reason==1 and fact.untilAt==1300)
assert(w.mode=='manual' and not w.intent and not w.pending)
assert(not w:warning('WIDLEWARN',warning,'Other','Jizzle',1))
assert(not w:warning('OTHER',warning,'Jizzle','Jizzle',1))
assert(not w:warning('WIDLEWARN',warning:gsub('warn_1','warn_0'),'Jizzle','Jizzle',1))
assert(not w:warning('WIDLEWARN',warning:gsub('\t7\t','\t6\t'),'Jizzle','Jizzle',1))
for _, bad in ipairs({'1\twarn_1\t7\t1000\t1\t0\t1300','1\twarn_1\t7\t1000\t1\t5\t1300',
    '1\twarn_1\t7\t1000\t0\t1\t1300','1\twarn_1\t7\t0\t1\t1\t300',
    '1\twarn_1\t7\t1000\t1\t1\t1301','1\twarn_1\t7\t1000\t4294967296\t1\t1300',
    warning..'\textra', warning..'\n', string.rep('x',181)}) do
    assert(not w:warning('WIDLEWARN',bad,'Jizzle','Jizzle',1))
end
assert(not w:warning('WIDLEWARN',warning,'Jizzle','Jizzle',26)) -- stale
w:want('bot')
assert(not w:warning('WIDLEWARN',warning,'Jizzle','Jizzle',1))
assert(w:update(1)=='.bot idle warn_2 control 7 bot')
assert(not w:warning('WIDLEWARN',warning,'Jizzle','Jizzle',1))
assert(w:receive('WIDLE','1\twarn_2\tok\tbot\t8\t0\tnone','Jizzle','Jizzle',2))
assert(not w:warning('WIDLEWARN',warning,'Jizzle','Jizzle',2))
assert(w:capability('WIDLECAP','1\twarn_2\t8\tactivity_1','Jizzle','Jizzle'))
assert(not w.warningSupported)
assert(not w:warning('WIDLEWARN','1\twarn_2\t8\t1000\t1\t1\t1300','Jizzle','Jizzle',2))
print('Correlated fishing warning scenarios passed')

-- Fishing has an independent acknowledged policy; manual control stays manual.
local f = Control.new('fish')
assert(f:update(0)=='.bot idle fish_1 status')
assert(f:receive('WIDLE','1\tfish_1\tok\tmanual\t5\t0\tnone','Jizzle','Jizzle',0))
assert(f:capability('WIDLECAP','1\tfish_1\t5\tactivity_1,fishing_1','Jizzle','Jizzle'))
assert(f:update(1)=='.bot idle fish_2 fishing 5 status')
local function fishReply(id,enabled,now,result,reason,epoch,sender,storage)
    return f:receive('WIDLEFISH','1\t'..id..'\t'..(epoch or 5)..'\t'..(result or 'ok')..'\t'..
        enabled..'\t'..(storage or 'stored')..'\t'..(reason or 'none'),sender or 'Jizzle','Jizzle',now)
end
assert(not fishReply('fish_2',0,1,nil,nil,nil,'Other'))
assert(not fishReply('old',0,1))
assert(not fishReply('fish_2',0,1,nil,nil,4))
assert(fishReply('fish_2',0,1))
assert(f:fishingFresh(1) and not f.fishingEnabled and f.mode=='manual')
assert(not f.socialSupported and not f.intent and not f:update(1))
f:wantFishing(true)
assert(f:update(2)=='.bot idle fish_3 fishing 5 on')
assert(not fishReply('fish_3',0,2)) -- a different value cannot confirm a write
assert(not fishReply('fish_3',1,2,nil,nil,nil,nil,'unavailable'))
assert(fishReply('fish_3',1,2))
assert(f.fishingEnabled and not f.socialEnabled and f.mode=='manual' and not f.intent)
f:want('bot')
assert(not f:fishingFresh(3) and not f.fishingIntent)
assert(f:update(3)=='.bot idle fish_4 control 5 bot')
assert(f:receive('WIDLE','1\tfish_4\tok\tbot\t6\t0\tnone','Jizzle','Jizzle',3))
assert(f:update(4)=='.bot idle fish_5 fishing 6 status')
assert(fishReply('fish_5',1,4,nil,nil,6))
f:wantFishing(false)
assert(f:update(5)=='.bot idle fish_6 fishing 6 off')
assert(f:update(8)=='.bot idle fish_6 fishing 6 off')
assert(f:update(11)=='.bot idle fish_6 fishing 6 off')
assert(not f:update(14))
assert(f.fishingFailure and not f.failure and f:isBot(14)) -- policy timeout never captures control
assert(f.nextFishing==44 and f.nextSocial==0) -- retry clocks are independent too
f:want('manual')
assert(f:update(14)=='.bot idle fish_7 control 6 manual')
assert(f:receive('WIDLE','1\tfish_7\tok\tmanual\t7\t0\tnone','Jizzle','Jizzle',14))
assert(f:update(15)=='.bot idle fish_8 fishing 7 status')
assert(fishReply('fish_8',0,15,'error','stale_generation',8))
assert(not f:fresh(15) and not f.fishingIntent)
assert(f:update(16)=='.bot idle fish_9 status')
assert(f:receive('WIDLE','1\tfish_9\tok\tmanual\t8\t0\tnone','Jizzle','Jizzle',16))
assert(f:capability('WIDLECAP','1\tfish_9\t8\tactivity_1,social_1','Jizzle','Jizzle'))
assert(not f.fishingSupported and not f.fishingIntent and not f.fishingAt and not f.fishingFailure)
assert(not fishReply('fish_9',1,16,nil,nil,8))
print('Independent Fishing policy protocol scenarios passed')

-- Correlated, historical container failures cannot acquire control.
local col=Control.new('collection')
assert(col:update(0)=='.bot idle collection_1 status')
assert(col:receive('WIDLE','1\tcollection_1\tok\tmanual\t9\t0\tnone','Jizzle','Jizzle',0))
assert(col:capability('WIDLECAP','1\tcollection_1\t9\tactivity_1,collection_1','Jizzle','Jizzle'))
local title='412042756e646c65206f662054726f75626c65'
local cw='1\tcollection_1\t9\t1000\t1\t5545\t13872\t4\t8\t3\t1120\t'..title
local cf=assert(col:collectionWarning('WIDLECOL',cw,'Jizzle','Jizzle',1))
assert(cf.quest==5545 and cf.item==13872 and cf.current==4 and cf.required==8)
assert(cf.title=='A Bundle of Trouble' and cf.reason==3 and cf.untilAt==1120)
assert(col.mode=='manual' and not col.pending and not col.intent)
assert(not col:collectionWarning('WIDLECOL',cw,'Other','Jizzle',1))
assert(not col:collectionWarning('OTHER',cw,'Jizzle','Jizzle',1))
for _,bad in ipairs({cw:gsub('collection_1','old'),cw:gsub('\t9\t','\t8\t'),
    cw:gsub('\t1000\t','\t0\t'),cw:gsub('\t1\t5545','\t0\t5545'),
    cw:gsub('\t5545\t','\t0\t'),cw:gsub('\t13872\t','\t4294967296\t'),
    cw:gsub('\t4\t8\t','\t8\t8\t'),cw:gsub('\t4\t8\t','\t4\t65536\t'),
    cw:gsub('\t3\t1120','\t5\t1120'),cw:gsub('\t1120\t','\t1121\t'),
    cw:gsub(title,'0a'),cw:gsub(title,'abc'),cw:gsub(title,'----'),
    cw:gsub(title,string.rep('61',41)),cw..'\textra',cw..'\n',string.rep('x',241)}) do
    assert(not col:collectionWarning('WIDLECOL',bad,'Jizzle','Jizzle',1))
end
assert(col:collectionWarning('WIDLECOL',cw:gsub(title,'-'),'Jizzle','Jizzle',1).title==nil)
assert(not col:collectionWarning('WIDLECOL',cw,'Jizzle','Jizzle',26))
col:want('bot');assert(not col:collectionWarning('WIDLECOL',cw,'Jizzle','Jizzle',1))
assert(col:update(1)=='.bot idle collection_2 control 9 bot')
assert(not col:collectionWarning('WIDLECOL',cw,'Jizzle','Jizzle',1))
assert(col:receive('WIDLE','1\tcollection_2\tok\tbot\t10\t0\tnone','Jizzle','Jizzle',2))
assert(not col:collectionWarning('WIDLECOL',cw,'Jizzle','Jizzle',2))
assert(col:capability('WIDLECAP','1\tcollection_2\t10\tactivity_1','Jizzle','Jizzle'))
assert(not col.collectionSupported)
assert(not col:collectionWarning('WIDLECOL',cw:gsub('collection_1','collection_2'):gsub('\t9\t','\t10\t'),'Jizzle','Jizzle',2))
print('Correlated collection failure protocol scenarios passed')
