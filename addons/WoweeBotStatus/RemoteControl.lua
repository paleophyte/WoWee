-- Local automation reuses the same controller as the buttons. No game execution here.
WoweeBotRemote = {}
local Remote = WoweeBotRemote
Remote.__index = Remote
function Remote.new(context)
    return setmetatable({context=context}, Remote)
end
function Remote:cancel(id, outcome)
    local r=self.request
    if not r or r.id~=id or r.outcome~='requested' then return end
    local c=self.context()
    if c==r.controller and c.intentRevision==r.revision then
        -- Discard an unconfirmed mutation. Read-only recovery must not replay it.
        c:refresh()
    end
    r.outcome=outcome or 'cancelled'
end
function Remote:requestControl(command,id)
    local c,inWorld=self.context()
    if not inWorld then return 'disconnected' end
    if command~='manual' and command~='bot' and command~='stop' then return 'refused' end
    if self.request and self.request.id==id then return 'refused' end
    if self.request and self.request.outcome=='requested' then
        if command=='bot' then return 'busy' end
        self:cancel(self.request.id,'superseded') -- Manual takeover always wins.
    end
    local mode=command=='bot' and 'bot' or 'manual'
    -- Force a fresh matching reply even when the last observation already has this mode.
    c:refresh()
    c:want(mode)
    self.request={id=id,mode=mode,controller=c,revision=c.intentRevision,
        sequence=c.sequence,started=GetTime(),outcome='requested'}
    return 'requested'
end
function Remote:snapshot()
    local c,inWorld=self.context()
    local now=GetTime()
    local r=self.request
    if r and r.outcome=='requested' then
        if not inWorld or c~=r.controller then r.outcome='disconnected'
        elseif c.intentRevision~=r.revision then r.outcome='superseded'
        elseif c.failure then
            r.outcome=c.failure:find('Server rejected command:',1,true) and 'refused' or 'timed_out'
        elseif c:fresh(now) and c.sequence>r.sequence and c.ackId and
            c.confirmedAt>=r.started and c.mode==r.mode and not c.intent and
            not (c.pending and c.pending.verb=='control') then r.outcome='confirmed'
        end
    end
    if r and r.outcome=='confirmed' then
        if not inWorld or c~=r.controller then r.outcome='disconnected'
        elseif c.intentRevision~=r.revision or not c:fresh(now) or c.failure or
            c.mode~=r.mode or c.intent or (c.pending and c.pending.verb=='control') then
            r.outcome='superseded'
        end
    end
    local fresh=inWorld and c:fresh(now) and not c.failure and true or false
    return {in_world=inWorld and true or false, fresh=fresh,
        mode=c.mode or 'unknown', generation=c.generation or 0,
        acknowledgement=c.ackId or '', age_seconds=c.confirmedAt and math.max(0,now-c.confirmedAt) or -1,
        pending_mode=c.intent or (c.pending and c.pending.verb=='control' and c.pending.desired) or '',
        failure=c.failure or '', request_id=r and r.id or '',
        outcome=r and r.outcome or 'none',
        control_may_still_be_active=not (fresh and c.mode=='manual' and not c.intent and
            not (c.pending and c.pending.verb=='control'))}
end
