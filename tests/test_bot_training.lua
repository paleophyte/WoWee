if not WoweeBotStatus then dofile('addons/WoweeBotStatus/Status.lua') end
if not WoweeBotTraining then dofile('addons/WoweeBotStatus/Training.lua') end
if not WoweeBotInbox then dofile('addons/WoweeBotStatus/Inbox.lua') end
local T=WoweeBotTraining
local function hex(s) return (s:gsub('.',function(c) return string.format('%02x',c:byte()) end)) end
local function row(state,cost,money,spendable,id)
    return table.concat({id or 202,7,8,1792255000,201,state or 'coins',cost or 100,money or 25,
        spendable or 10,hex('Polearms'),hex('Weapon Master')},'\t')
end
local function body(...) local rows={...};return '1\tok\t'..#rows..(#rows>0 and ('\n'..table.concat(rows,'\n')) or '') end
local snapshot=T.decode(body(row()))
assert(snapshot and snapshot.rows[1].cost==100 and snapshot.rows[1].title=='Polearms')
assert(T.decode('1\tunavailable\t0') and T.decode('1\tok\t0'))
assert(not T.decode(body(row(),row())) and not T.decode(body(row('pretend'))))
assert(not T.decode(body(row('coins',10,25,10))))
assert(not T.decode(body(row('budget',100,25,10))))
assert(not T.decode(body(row('ready',100,100,90))))
assert(not T.decode(body(row('coins',100,25,30))))
assert(not T.decode(body(row()):gsub(hex('Polearms'),'0a')))
assert(not T.decode('1\tok\t33') and not T.decode(body(row())..'\n'))
local model=T.new()
local control={ackId='ack',generation=7}
model:capability('WIDLECAP','1\tack\t7\tactivity_1,training_1','Other','Jizzle',control)
assert(not model.supported)
model:capability('WIDLECAP','1\told\t7\ttraining_1','Jizzle','Jizzle',control)
assert(not model.supported)
model:capability('WIDLECAP','1\tack\t7\ttraining_1','Jizzle','Jizzle',control)
assert(model.supported)
local function packet(id,part,count,chunk,generation)
    return '1\t'..id..'\t'..(generation or 7)..'\t'..part..'\t'..count..'\t'..chunk
end
local function deliver(data,id,now)
    model:expect(id,7,now)
    local count=math.ceil(#data/160)
    for i=count,1,-1 do
        local text=packet(id,i,count,data:sub((i-1)*160+1,i*160))
        assert(not model:receive('WIDLETRAIN',text,'Other','Jizzle',7,now))
        model:receive('WIDLETRAIN',text,'Jizzle','Jizzle',7,now)
    end
    assert(not model.pending and not model.failure)
end
deliver(body(row()),'req',1)
assert(model.snapshot.rows[1].lesson==202)
model:expect('conflict',7,2)
model:receive('WIDLETRAIN',packet('conflict',1,2,'a'),'Jizzle','Jizzle',7,2)
model:receive('WIDLETRAIN',packet('conflict',1,2,'b'),'Jizzle','Jizzle',7,2)
assert(model.failure and not model.pending and model.snapshot.rows[1].lesson==202)
model:expect('late',7,3);model:expire(8,7)
assert(not model.pending and model.failure and model.snapshot.rows[1].lesson==202)
local many={}
for i=1,32 do many[i]=row('coins',4294967295,25,10,i) end
deliver(body(unpack(many)),'max',9)
assert(#model.snapshot.rows==32)
local root={}
local inbox=WoweeBotInbox.new(root,'realm1','Jizzle')
assert(inbox:training(snapshot,10))
local entry=inbox:find('training:202')
assert(entry.kind=='blocked' and entry.text:find('75c',1,true))
assert(not inbox:training(snapshot,11) and #inbox.data.entries==1)
inbox=WoweeBotInbox.new(root,'realm1','Jizzle')
assert(not inbox:training(snapshot,12)) -- persisted identity/option, no duplicates
assert(inbox:training(T.decode(body(row('budget',100,200,50))),13))
assert(entry.kind=='blocked' and entry.text:find('reserved',1,true) and entry.text:find('50c',1,true))
assert(inbox:training(T.decode(body(row('ready',100,200,150))),14))
assert(entry.kind=='note' and entry.text:find('not been learned',1,true))
assert(inbox:training(T.decode(body(row('learned',100,200,0))),15))
assert(entry.text:find('Confirmed learned',1,true))
assert(not inbox:training(T.decode('1\tunavailable\t0'),16))
assert(entry.text:find('Confirmed learned',1,true))
assert(inbox:training(snapshot,17) and inbox:training(T.decode('1\tok\t0'),18))
assert(entry.kind=='note' and entry.text:find('has not been rechecked',1,true))
assert(#WoweeBotInbox.new(root,'realm2','Jizzle').data.entries==0)
assert(#WoweeBotInbox.new(root,'realm1','Other').data.entries==0)
print('Training decode, framing, shortfalls, reconciliation, durability and scope passed')
if arg and arg[1]=='--server-fixture' then
    local file=assert(io.open(arg[2],'rb'));local data=file:read('*a');file:close()
    local decoded=assert(T.decode(data))
    assert(#decoded.rows==32 and decoded.rows[32].cost==4294967295)
    deliver(data,'server',20)
    assert(#model.snapshot.rows==32)
end
