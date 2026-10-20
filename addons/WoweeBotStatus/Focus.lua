-- A quest plan is independent of character control and the immediate AI task.
WoweeBotQuestFocus={}
local Focus=WoweeBotQuestFocus
Focus.__index=Focus
local states={none=true,active=true,ready=true,rewarded=true,failed=true,missing=true,multiple=true,unavailable=true}
local rejection={
    stale_plan='The quest plan changed before this request arrived. Keeping the server plan and refreshing it.',
    not_accepted='The server cannot select that quest in its current quest-log state. Keeping the current plan; choose an active or ready accepted quest.',
    ai_unavailable='Quest selection is unavailable because no player AI is attached. Control has not changed.',
    revision_exhausted='The server quest-plan revision limit was reached. Relog before selecting a different quest.',
    syntax='The server rejected the quest-selection request format. Refreshing the current plan.',
}
function Focus.new(nonce)
    return setmetatable({nonce=nonce or tostring(math.floor(GetTime()*1000))..'_f',sequence=0,nextRead=0},Focus)
end
local function changing(control)
    return control.intent or (control.pending and control.pending.verb=='control')
end
function Focus:fresh(control,now)
    return self.supported and control:fresh(now) and not control.failure and not changing(control) and
        self.generation==control.generation and self.at and now-self.at<=25 and not self.failure
end
function Focus:capability(prefix,text,sender,player,control)
    if prefix~='WIDLEFCAP' or not WoweeBotStatus.samePlayer(sender,player) or type(text)~='string' or #text>100 then return false end
    local id,generation=text:match('^1\t([%w_-]+)\t(%d+)\tfocus_1$')
    if not id or id~=control.ackId or tonumber(generation)~=control.generation then return false end
    if self.generation~=control.generation then
        self.pending,self.intent,self.at,self.failure=nil,nil,nil,nil
        self.nextRead=0
    end
    self.supported,self.generation=true,control.generation
    return true
end
function Focus:choose(questId,control,now)
    if type(questId)~='number' or questId<0 or questId>4294967295 or questId~=math.floor(questId) or
        not self:fresh(control,now) or self.pending or self.state=='unavailable' then return false end
    self.intent=questId
    return true
end
function Focus:update(control,now)
    if not self.supported then return end
    if self.generation~=control.generation or not control:fresh(now) or control.failure or changing(control) then
        -- A handoff cancels an unconfirmed write. Observe the new epoch; never
        -- replay an old selection automatically when communication recovers.
        self.pending,self.intent,self.at=nil,nil,nil
        self.nextRead=0
        return
    end
    local pending=self.pending
    if pending then
        if now-pending.sentAt<3 then return end
        if pending.attempts>=3 then
            self.pending,self.intent,self.at=nil,nil,nil
            self.failure='Quest selection unconfirmed; refreshing the server plan.'
            self.nextRead=now+8
            return
        end
        pending.attempts,pending.sentAt=pending.attempts+1,now
        return pending.command
    end
    if self.intent==nil and now<self.nextRead then return end
    if self.intent~=nil and not self:fresh(control,now) then self.intent=nil end
    self.sequence=self.sequence+1
    local id=self.nonce..'_'..self.sequence
    local desired=self.intent
    local command='.bot idle '..id..' focus '..control.generation..' '..
        (desired~=nil and ('set '..self.revision..' '..desired) or 'status')
    self.pending={id=id,desired=desired,revision=self.revision,command=command,sentAt=now,attempts=1}
    self.intent=nil
    return command
end
function Focus:receive(prefix,text,sender,player,control,now)
    if prefix~='WIDLEFOCUS' or not WoweeBotStatus.samePlayer(sender,player) or type(text)~='string' or #text>180 then return false end
    local id,generation,result,revision,quest,state,reason=
        text:match('^1\t([%w_-]+)\t(%d+)\t(%a+)\t(%d+)\t(%d+)\t([%w_]+)\t([%w_]+)$')
    generation,revision,quest=tonumber(generation),tonumber(revision),tonumber(quest)
    local pending=self.pending
    if not id or not pending or id~=pending.id or changing(control) or not generation or generation<1 or generation>4294967295 or
        (result~='ok' and result~='error') or not states[state] or not revision or revision<1 or revision>4294967295 or
        not quest or quest>4294967295 or (quest==0 and state~='none' and state~='multiple' and state~='unavailable') or
        (quest>0 and (state=='none' or state=='multiple' or state=='unavailable')) then return false end
    if generation~=control.generation then
        if result~='error' or reason~='stale_generation' or not generation or generation<control.generation then return false end
        self.pending,self.intent,self.at=nil,nil,nil
        self.failure='Control changed; refreshing before quest selection.'
        control.confirmedAt,control.nextStatus=nil,0
        return true
    end
    if self.revision and revision<self.revision then return false end
    if result=='ok' and (reason~='none' or (pending.desired~=nil and quest~=pending.desired)) then return false end
    self.pending,self.intent=nil,nil
    self.nextRead=now+8
    if result=='error' then
        self.failure,self.at=rejection[reason] or ('Quest selection rejected: '..reason),nil
        self.lastRejected=reason
        return true
    end
    if self.questId~=quest then self.questTitle=nil end
    self.questId,self.state,self.revision,self.at,self.failure=quest,state,revision,now,nil
    self.observedAt=now
    return true
end
function Focus:label(control,now)
    if not self.supported then return 'Selected quest: unavailable on this server' end
    if self.pending and self.pending.desired~=nil then return 'Selected quest: waiting for server confirmation' end
    local title=self.questTitle or (self.questId and self.questId>0 and ('Quest #'..self.questId) or 'none')
    local reader=rawget(_G,'WoweeGetAcceptedQuests')
    local quests=type(reader)=='function' and reader() or nil
    if type(quests)=='table' then
        for _,quest in ipairs(quests) do if quest.questId==self.questId and quest.title~='' then title=quest.title;self.questTitle=title;break end end
    end
    if not self:fresh(control,now) then
        return (self.failure or 'Selected quest unconfirmed')..(self.observedAt and ('; last confirmed: '..title) or '')
    end
    if self.state=='none' then return 'Selected quest: none (server confirmed)' end
    if self.state=='multiple' then return 'Multiple quests selected on server; choose one or clear selection' end
    local suffix=self.state=='ready' and 'ready to turn in' or self.state=='rewarded' and 'turned in' or
        self.state=='failed' and 'failed' or self.state=='missing' and 'no longer in quest log' or 'server confirmed'
    return 'Selected quest: '..title..' ('..suffix..')'
end
