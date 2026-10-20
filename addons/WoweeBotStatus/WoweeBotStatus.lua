local Status = WoweeBotStatus
local model = Status.new()
local controller = WoweeBotControl.new()
local function newFocus()
    local module=rawget(_G,'WoweeBotQuestFocus')
    if type(module)=='table' and type(module.new)=='function' then return module.new() end
end
local focus = newFocus()
local activity = WoweeBotActivity.new()
local function newHistory()
    local module=rawget(_G,'WoweeBotHistory')
    if type(module)=='table' and type(module.new)=='function' then return module.new() end
end
local history = newHistory()
local function newTraining()
    local module=rawget(_G,'WoweeBotTraining')
    if type(module)=='table' and type(module.new)=='function' then return module.new() end
end
local training = newTraining()
local function newTrainingPlan()
    local module=rawget(_G,'WoweeBotTrainingPlan')
    if type(module)=='table' and type(module.new)=='function' then return module.new() end
end
local trainingPlan = newTrainingPlan()
local function newTravel()
    local module=rawget(_G,'WoweeBotTravel')
    if type(module)=='table' and type(module.new)=='function' then return module.new() end
end
local travel = newTravel()
local function newRecovery()
    local module=rawget(_G,'WoweeBotRecovery')
    if type(module)=='table' and type(module.new)=='function' then return module.new() end
end
local recovery=newRecovery()
local function newItemUse()
    local module=rawget(_G,'WoweeBotItemUse')
    if type(module)=='table' and type(module.new)=='function' then return module.new() end
end
local itemUse=newItemUse()
local function newGatherTools()
    local module=rawget(_G,'WoweeBotGatherTools')
    if type(module)=='table' and type(module.new)=='function' then return module.new() end
end
local gatherTools=newGatherTools()
function WoweeBotConfirmedControlGeneration()
    if controller:isBot(GetTime()) then return controller.generation end
end
local driver = CreateFrame('Frame', 'WoweeBotController', UIParent)
local rejectedPopup
local panel = CreateFrame('Frame', 'WoweeBotStatusFrame', UIParent)
panel:SetSize(690, 284)
panel:SetPoint('BOTTOM', UIParent, 'BOTTOM', 0, 138)
panel:SetFrameStrata('MEDIUM')
panel:SetMovable(true)
panel:EnableMouse(true)
panel:SetClampedToScreen(true)
panel:RegisterForDrag('LeftButton')
panel:SetBackdrop({
    bgFile = 'Interface\\Tooltips\\UI-Tooltip-Background',
    edgeFile = 'Interface\\Tooltips\\UI-Tooltip-Border',
    tile = true, tileSize = 16, edgeSize = 16,
    insets = {left = 4, right = 4, top = 4, bottom = 4},
})
panel:SetBackdropColor(0.035, 0.04, 0.025, 0.94)
panel:SetBackdropBorderColor(0.65, 0.53, 0.25, 1)

local function label(name, font, x, y, width)
    local text = panel:CreateFontString('WoweeBotStatus' .. name, 'OVERLAY', font)
    text:SetPoint('TOPLEFT', panel, 'TOPLEFT', x, y)
    text:SetWidth(width)
    text:SetHeight(20)
    text:SetJustifyH('LEFT')
    text:SetWordWrap(false)
    return text
end

local title = label('Title', 'GameFontNormal', 16, -13, 150)
title:SetText('BOT ACTIVITY')
title:SetTextColor(0.86, 0.72, 0.35)
local freshness = label('Freshness', 'GameFontHighlightSmall', 175, -15, 170)
freshness:SetTextColor(0.65, 0.68, 0.63)
local selectedQuest = label('SelectedQuest', 'GameFontNormal', 16, -38, 655)
local goal = label('Goal', 'GameFontNormal', 16, -90, 655)
goal:SetTextColor(1, 0.84, 0.25)
local details = label('Details', 'GameFontHighlight', 16, -114, 655)
local progress = label('Progress', 'GameFontHighlightSmall', 16, -139, 655)
local action = label('Action', 'GameFontHighlightSmall', 16, -164, 655)
action:SetTextColor(0.65, 0.7, 0.7)
local control = label('Control', 'GameFontHighlightSmall', 16, -236, 655)
control:SetTextColor(0.72, 0.8, 0.62)
local modeLabel = label('Mode', 'GameFontHighlight', 16, -212, 500)
local interaction = label('Interaction', 'GameFontNormalLarge', 16, -64, 655)
local travelDetail = label('Travel', 'GameFontHighlightSmall', 16, -188, 655)
travelDetail:SetTextColor(0.9,0.76,0.4)

local settings, nextPoll, nextDraw, lastRequest = nil, 0, 0, -100
local inWorld = UnitExists('player') and true or false
local lastDriverTick
local function notifyHistory()
    local changed=rawget(_G,'WoweeBotHistoryChanged')
    if type(changed)=='function' then changed(history,inWorld) end
end
local autoButton, playButton, drawPolicy
local grindReason = ''

local function automatic()
    return settings and settings.autoGrind ~= false
end

local function grouped()
    return GetNumPartyMembers() > 0 or GetNumRaidMembers() > 0
end

local function save()
    if WoweeSaveVariables then WoweeSaveVariables() end
end

local function request()
    local now = GetTime()
    local observeManual = training and training.supported and activity.supported and
        controller.mode == 'manual' and controller:fresh(now) and not controller.failure and not controller.intent and
        not (controller.pending and controller.pending.verb == 'control')
    if not inWorld or (not controller:isBot(now) and not observeManual) or now - lastRequest < 2 then return end
    local player = UnitName('player')
    if not player or player == '' or player == 'Unknown' then return end
    if activity.supported and activity.pending then return end
    lastRequest, nextPoll = now, now + 8
    model:request(now)
    if activity.supported then
        local command=activity:request(controller.generation, now)
        if history then history:expect(activity.pending.id,controller.generation,now) end
        if training then training:expect(activity.pending.id,controller.generation,now) end
        if trainingPlan then trainingPlan:expect(activity.pending.id,controller.generation,now) end
        if travel then travel:expect(activity.pending.id,controller.generation,now) end
        if recovery then recovery:expect(activity.pending.id,controller.generation,now) end
        if itemUse then itemUse:expect(activity.pending.id,controller.generation,now) end
        if gatherTools then gatherTools:expect(activity.pending.id,controller.generation,now) end
        SendChatMessage(command, 'SAY')
        return
    end
    SendAddonMessage('BOT', 'debug traveldetail', 'WHISPER', player)
    SendAddonMessage('BOT', 'debug action', 'WHISPER', player)
    -- Playerbots may send the human-readable where report to the whole party
    -- even when the request is a private addon whisper. Never poll it in a
    -- group; the two raw debug queries above are always sent only to us.
    if not grouped() then SendAddonMessage('BOT', '#a where', 'WHISPER', player) end
end

local function questState(id)
    if not id then return end
    for i = 1, GetNumQuestLogEntries() do
        local _, _, _, _, header, _, complete, _, questId = GetQuestLogTitle(i)
        if not header and questId == id then
            return {complete = complete == 1, failed = complete == -1}
        end
    end
end

local function questProgress(id)
    if not id then return '' end
    for i = 1, GetNumQuestLogEntries() do
        local _, _, _, _, isHeader, _, complete, _, questId = GetQuestLogTitle(i)
        if not isHeader and questId == id then
            if complete == 1 then return 'Ready to turn in' end
            if complete == -1 then return 'Quest failed' end
            local lines = {}
            for j = 1, GetNumQuestLeaderBoards(i) do
                local text = GetQuestLogLeaderBoard(j, i)
                if text and text ~= '' then lines[#lines + 1] = text end
            end
            return table.concat(lines, '  |  ')
        end
    end
    return ''
end

local function draw()
    local now = GetTime()
    selectedQuest:SetText(focus and focus:label(controller,now) or 'Selected quest: unavailable')
    if drawPolicy then drawPolicy() end
    progress:SetText('')
    action:SetText('')
    travelDetail:SetText('')
    control:SetText('')
    interaction:SetText('')
    modeLabel:SetText(controller:label(now))
    if playButton then
        playButton:SetText(controller.failure and 'Refresh status' or controller.intent and 'Switching...' or
            (controller.mode == 'bot' and 'Let me play' or 'Bot, you play'))
    end
    if autoButton then autoButton:SetText(automatic() and 'Auto grind: On' or 'Auto grind: Off') end
    if controller:fresh(now) and not controller.failure and controller.mode == 'manual' then
        freshness:SetText('Manual control')
        goal:SetText('Your character is under your control')
        details:SetText('Bot actions are paused. Play normally, then resume when ready.')
        control:SetText('Auto grind paused until bot control is confirmed')
        return
    end
    if not controller:fresh(now) or controller.failure then
        freshness:SetText('Control status unavailable')
        goal:SetText('Waiting for bot status')
        local observed = controller.observedAt and ('Last confirmed: ' .. (controller.mode == 'bot' and 'Bot' or 'Manual') ..
            ', ' .. math.max(0, math.floor(now - controller.observedAt)) .. 's ago.') or 'No server confirmation received yet.'
        details:SetText(observed .. ' The server may still be playing.')
        control:SetText('Addon automation paused; querying status. Refresh queries without changing control.')
        return
    end
    if model.disabled then
        freshness:SetText('Player AI is off')
        goal:SetText('Player AI disabled')
        details:SetText('Enable player AI to see its current activity.')
        return
    end
    if automatic() and controller:isBot(now) then
        control:SetText(controller.pending and controller.pending.verb == 'grind' and 'Grind: waiting for server confirmation' or
            grouped() and 'Auto grind: paused while grouped' or
            ('Grind: ' .. (controller.grind and 'on' or 'off') .. ' (confirmed) - ' .. grindReason))
    elseif not automatic() then
        control:SetText('Grind: manual control (auto grind is off)')
    else
        control:SetText('Auto grind paused until bot control is confirmed')
    end
    if not model:isFresh(now) then
        freshness:SetText(model.travelAt and ('Last update ' .. math.floor(now - model.travelAt) .. 's ago') or '')
        goal:SetText(model.unsupported and 'Bot status unavailable' or 'Waiting for bot status')
        if activity.failure then
            details:SetText(activity.failure .. '. Retrying the activity query...')
        elseif model.unsupported then
            details:SetText('This server does not expose Playerbots status queries.')
        elseif model.firstRequestAt and now - model.firstRequestAt > 4 then
            details:SetText('No current activity received. Querying again; server AI may still be playing.')
        else
            details:SetText('Asking your Playerbot what it is doing...')
        end
        return
    end
    if travel and controller:isBot(now) then travelDetail:SetText(travel:summary(now) or '') end
    if recovery and controller:isBot(now) and recovery:summary(now) then travelDetail:SetText(recovery:summary(now)) end
    freshness:SetText('Updated ' .. math.floor(now - model.travelAt) .. 's ago')
    local questId, questTitle = model:currentQuest(now)
    goal:SetText(questTitle and ((model.typed and 'Quest destination: ' or 'Quest: ') .. questTitle) or
        ((model.typed and 'Travel destination: ' or 'Goal: ') .. (model.goal or 'No destination')))
    local parts = {model.status or (model.kind == 'idle' and 'Idle' or 'No travel status reported')}
    if model.typed then
        parts[1] = (model.aiState or 'unknown'):gsub('_', ' ') .. ' - ' .. parts[1]
        if questTitle then parts[#parts + 1] = 'Destination: ' .. model.goal end
    end
    if model.distance and model.kind ~= 'idle' then parts[#parts + 1] = model.distance .. ' yd' end
    if model.location then parts[#parts + 1] = Status.clean(model.location) end
    details:SetText(table.concat(parts, '  -  '))
    if model.typed then interaction:SetText(WoweeBotActivity.currentWork(model)) end
    if recovery and recovery:currentWork(now) then interaction:SetText(recovery:currentWork(now)) end
    local objective = questProgress(questId)
    if objective == '' and model.kind and model.kind:find('quest', 1, true) and not questId then
        objective = grouped() and 'Quest name unavailable while grouped; showing the destination.' or
            'Waiting for the bot to report its quest name.'
    elseif objective == '' then
        if model.status and model.status:match('^cooldown') then
            objective = 'Waiting before choosing the next activity'
        else
            objective = model.reportAt and now - model.reportAt <= Status.STALE_AFTER and model.report or ''
        end
    end
    progress:SetText(objective)
    if type(model.action) == 'string' and model.action ~= '' and model.actionAt and
        now - model.actionAt <= Status.STALE_AFTER then
        action:SetText((model.typed and 'Last reported action: ' or 'Last action: ') ..
            (model.typed and WoweeBotActivity.actionName(model.action) or model.action))
    end
end

local function button(text, width, x, callback, name)
    local b = CreateFrame('Button', name, panel, 'UIPanelButtonTemplate')
    b:SetSize(width, 21)
    b:SetPoint('TOPRIGHT', panel, 'TOPRIGHT', x, -9)
    b:SetText(text)
    b:SetScript('OnClick', callback)
    return b
end
button('Quest log', 78, -102, function() if ToggleQuestLog then ToggleQuestLog() end end)
local function refreshStatus()
    model.disabled = nil; activity.pending, activity.failure = nil, nil
    controller:refresh(); nextPoll = 0; draw()
end
button('Refresh', 65, -32, refreshStatus, 'WoweeBotStatusRefresh')
button('x', 20, -9, function() panel:Hide() end)
autoButton = button('Auto grind: On', 115, -185, function()
    settings.autoGrind = not automatic()
    controller:planGrind(nil)
    save(); nextPoll = 0; draw()
end, 'WoweeBotStatusAutoGrind')

function WoweeBotToggleControl()
    if controller.failure then controller:refresh(); draw(); return end
    local current = controller.intent or controller.mode
    controller:want(current == 'bot' and 'manual' or 'bot')
    draw()
end
playButton = button('Bot, you play', 135, -310, WoweeBotToggleControl, 'WoweeBotStatusPlay')
playButton:ClearAllPoints()
playButton:SetPoint('TOPRIGHT', panel, 'TOPRIGHT', -16, -135)
BINDING_HEADER_WOWEEBOTSTATUS = 'WoWee Bot'
BINDING_NAME_WOWEEBOTCONTROL = 'Switch manual / bot control'

-- An always-visible entry point survives closing the activity panel.
local launcher = CreateFrame('Button', 'WoweeBotLauncher', UIParent, 'UIPanelButtonTemplate')
launcher:SetSize(90, 22)
launcher:SetPoint('BOTTOM', UIParent, 'BOTTOM', 0, 110)
launcher:SetText('Bot activity')
launcher:SetScript('OnClick', function() if panel:IsShown() then panel:Hide() else panel:Show() end end)

local function shortcut(text, x, callback)
    local b = CreateFrame('Button', nil, panel, 'UIPanelButtonTemplate')
    b:SetSize(90, 21); b:SetPoint('BOTTOMLEFT', panel, 'BOTTOMLEFT', x, 8)
    b:SetText(text); b:SetScript('OnClick', callback)
end
shortcut('Character', 16, function() if ToggleCharacter then ToggleCharacter('PaperDollFrame') end end)
shortcut('Bags', 112, function() if ToggleAllBags then ToggleAllBags() end end)
shortcut('Talents', 208, function() if ToggleTalentFrame then ToggleTalentFrame() end end)
shortcut('Skills', 304, function() if ToggleCharacter then ToggleCharacter('SkillFrame') end end)
shortcut('Spellbook', 400, function() if ToggleSpellBook then ToggleSpellBook(BOOKTYPE_SPELL or 'spell') end end)
shortcut('Map', 496, function() if ToggleWorldMap then ToggleWorldMap() end end)

local policies = CreateFrame('Frame', 'WoweeBotPoliciesFrame', UIParent)
policies:SetSize(430, 650); policies:SetPoint('CENTER', UIParent, 'CENTER', 0, 0)
policies:SetFrameStrata('DIALOG'); policies:EnableMouse(true)
policies:SetBackdrop({bgFile='Interface\\Tooltips\\UI-Tooltip-Background',
    edgeFile='Interface\\Tooltips\\UI-Tooltip-Border',tile=true,tileSize=16,edgeSize=16,
    insets={left=4,right=4,top=4,bottom=4}})
policies:SetBackdropColor(0.035,0.04,0.025,0.97)
local function policyText(name, font, y, height)
    local text = policies:CreateFontString(name,'OVERLAY',font)
    text:SetPoint('TOPLEFT',policies,'TOPLEFT',18,y); text:SetSize(394,height)
    text:SetJustifyH('LEFT'); text:SetJustifyV('TOP'); text:SetWordWrap(true)
    return text
end
policyText(nil,'GameFontNormalLarge',-17,25):SetText('Hero policies')
policyText(nil,'GameFontHighlightSmall',-52,46):SetText('Social Detours allows optional NPC visits, conversations, and idle activities. Quest and maintenance work remain available with it Off.')
local policyState = policyText('WoweeBotSocialState','GameFontNormal',-108,22)
local policyDetail = policyText('WoweeBotSocialDetail','GameFontHighlightSmall',-137,40)
local function policyButton(name, title, x, y, width, callback)
    local b = CreateFrame('Button',name,policies,'UIPanelButtonTemplate')
    b:SetSize(width,23); b:SetPoint('TOPLEFT',policies,'TOPLEFT',x,y)
    b:SetText(title); b:SetScript('OnClick',callback); return b
end
policyButton(nil,'x',394,-12,22,function() policies:Hide() end)
local policyToggle = policyButton('WoweeBotSocialToggle','Waiting...',18,-188,245,function()
    local now = GetTime()
    if inWorld and controller:fresh(now) and controller:socialFresh(now) and not controller.intent and not controller.failure then
        controller:wantSocial(not controller.socialEnabled)
        drawPolicy()
    end
end)
policyButton('WoweeBotSocialRefresh','Refresh',278,-188,130,function()
    controller:refreshSocial()
    if not controller:fresh(GetTime()) or controller.failure then controller:refresh() end
    drawPolicy()
end)
policyText(nil,'GameFontHighlightSmall',-222,42):SetText('Fishing permits autonomous casts and fishing trips. Off keeps manual fishing available.')
local fishingState = policyText('WoweeBotFishingState','GameFontNormal',-273,22)
local fishingDetail = policyText('WoweeBotFishingDetail','GameFontHighlightSmall',-301,40)
local fishingToggle = policyButton('WoweeBotFishingToggle','Waiting...',18,-350,245,function()
    local now = GetTime()
    if inWorld and controller:fresh(now) and controller:fishingFresh(now) and not controller.intent and not controller.failure then
        controller:wantFishing(not controller.fishingEnabled)
        drawPolicy()
    end
end)
policyButton('WoweeBotFishingRefresh','Refresh',278,-350,130,function()
    controller:refreshFishing()
    if not controller:fresh(GetTime()) or controller.failure then controller:refresh() end
    drawPolicy()
end)

policyText(nil,'GameFontNormal',-396,24):SetText('Incoming requests — bot control only')
policyText(nil,'GameFontHighlightSmall',-424,32):SetText('Each policy defaults to Reject. Manual control leaves incoming requests for you to handle.')
local requestRows = {}
for index, entry in ipairs({{'guild','Guild invites'}, {'group','Group invites'}, {'trade','Trade requests'}}) do
    local kind, title, y = entry[1], entry[2], -468 - (index-1)*44
    local label = policyText('WoweeBotRequest' .. kind .. 'State','GameFontNormal',y,38)
    label:SetSize(250,38)
    local button = policyButton('WoweeBotRequest' .. kind .. 'Toggle','Waiting...',278,y,130,function()
        local now = GetTime()
        if inWorld and controller:fresh(now) and controller:requestsFresh(now) and not controller.intent and not controller.failure and not controller.requestsIntent then
            controller:wantRequest(kind,not controller:rejectsRequest(kind)); drawPolicy()
        end
    end)
    requestRows[#requestRows+1] = {kind=kind,title=title,label=label,button=button}
end
local requestsDetail=policyText('WoweeBotRequestsDetail','GameFontHighlightSmall',-604,32)
policyButton('WoweeBotRequestsRefresh','Refresh',278,-598,130,function()
    controller:refreshRequests()
    if not controller:fresh(GetTime()) or controller.failure then controller:refresh() end
    drawPolicy()
end)

drawPolicy = function()
    if not policies:IsShown() then return end
    local now = GetTime()
    local fresh = controller:socialFresh(now)
    local pending = controller.socialIntent ~= nil or (controller.pending and controller.pending.verb == 'social')
    policyToggle:Disable()
    if not inWorld then
        policyState:SetText('Log into a character to change policies')
        policyDetail:SetText('Policies do not enable bot control.')
    elseif not controller:fresh(now) or controller.failure then
        policyState:SetText('Waiting for control confirmation')
        policyDetail:SetText('Refresh to query the server. This does not enable AI.')
    elseif not controller.socialSupported then
        policyState:SetText('Social Detours: unavailable')
        policyDetail:SetText('This server has not advertised Social Detours controls.')
    elseif pending then
        policyState:SetText('Social Detours: waiting for confirmation')
        policyDetail:SetText('The server confirms and saves this preference.')
    elseif controller.socialFailure then
        policyState:SetText('Social Detours: unconfirmed')
        policyDetail:SetText(controller.socialFailure .. '. Refresh to retry.')
    elseif fresh then
        policyState:SetText('Social Detours: ' .. (controller.socialEnabled and 'On' or 'Off') .. ' (server confirmed)')
        policyDetail:SetText('Saved for this character. Changing it does not switch manual / bot control.')
        if not controller.intent then policyToggle:Enable() end
    else
        policyState:SetText('Social Detours: asking the server')
        policyDetail:SetText('No current policy confirmation. Default behavior on the patched server is Off.')
    end
    policyToggle:SetText(pending and 'Waiting...' or fresh and
        (controller.socialEnabled and 'Disable social detours' or 'Allow social detours') or 'Awaiting confirmed policy')
    local fishingFresh = controller:fishingFresh(now)
    local fishingPending = controller.fishingIntent ~= nil or (controller.pending and controller.pending.verb == 'fishing')
    fishingToggle:Disable()
    if not inWorld then
        fishingState:SetText('Log into a character to change Fishing')
        fishingDetail:SetText('Policies do not enable bot control.')
    elseif not controller:fresh(now) or controller.failure then
        fishingState:SetText('Fishing: waiting for control confirmation')
        fishingDetail:SetText('Refresh to query the server. This does not enable AI.')
    elseif not controller.fishingSupported then
        fishingState:SetText('Fishing: unavailable')
        fishingDetail:SetText('This server has not advertised Fishing controls.')
    elseif fishingPending then
        fishingState:SetText('Fishing: waiting for confirmation')
        fishingDetail:SetText('The server confirms and saves this preference.')
    elseif controller.fishingFailure then
        fishingState:SetText('Fishing: unconfirmed')
        fishingDetail:SetText(controller.fishingFailure .. '. Refresh to retry.')
    elseif fishingFresh then
        fishingState:SetText('Fishing: ' .. (controller.fishingEnabled and 'On' or 'Off') .. ' (server confirmed)')
        fishingDetail:SetText('Saved for this character. Manual fishing remains available. Turning On does not switch control or bypass the retry pause.')
        if not controller.intent then fishingToggle:Enable() end
    else
        fishingState:SetText('Fishing: asking the server')
        fishingDetail:SetText('No current policy confirmation. The default is Off.')
    end
    fishingToggle:SetText(fishingPending and 'Waiting...' or fishingFresh and
        (controller.fishingEnabled and 'Disable fishing' or 'Allow fishing') or 'Awaiting confirmed policy')
    local freshRequests = inWorld and controller:fresh(now) and not controller.failure and controller:requestsFresh(now)
    local pendingRequests = controller.requestsIntent or (controller.pending and controller.pending.verb == 'requests')
    for _, row in ipairs(requestRows) do
        row.button:Disable()
        local state = not inWorld and 'log in first' or not controller.requestsSupported and 'unavailable' or
            pendingRequests and 'waiting for confirmation' or controller.requestsFailure and 'unconfirmed' or
            freshRequests and (controller:rejectsRequest(row.kind) and 'Reject' or 'Allow') or 'asking the server'
        row.label:SetText(row.title .. ': ' .. state .. (freshRequests and not pendingRequests and ' (server confirmed)' or ''))
        row.button:SetText(freshRequests and not pendingRequests and (controller:rejectsRequest(row.kind) and 'Allow' or 'Reject') or 'Waiting...')
        if freshRequests and not pendingRequests and not controller.intent then row.button:Enable() end
    end
    requestsDetail:SetSize(250,38)
    requestsDetail:SetText(controller.requestsFailure or (controller.requestsSupported and 'Saved per character. Applies only in bot control.' or 'Server request-policy support required.'))

end
local policyLauncher = CreateFrame('Button','WoweeBotPoliciesLauncher',panel,'UIPanelButtonTemplate')
policyLauncher:SetSize(80,21); policyLauncher:SetPoint('BOTTOMLEFT',panel,'BOTTOMLEFT',592,8)
policyLauncher:SetText('Policies'); policyLauncher:SetScript('OnClick',function() policies:Show(); drawPolicy() end)
policies:Hide()

panel:SetScript('OnDragStart', function(self) self:StartMoving() end)
panel:SetScript('OnDragStop', function(self)
    self:StopMovingOrSizing()
    if settings then
        local point, _, relative, x, y = self:GetPoint(1)
        settings.position = {point, relative, x, y}
        save()
    end
end)
panel:SetScript('OnHide', function()
    if settings then settings.enabled = false; save() end
end)
panel:SetScript('OnShow', function()
    if settings then settings.enabled = true; save() end
    nextPoll = 0
end)
driver:SetScript('OnUpdate', function()
    local now = GetTime()
    lastDriverTick = now
    -- FrameXML may create its popup after our event handler. Hide that same
    -- rejected request once more on the following tick, only in the same bot epoch.
    if rejectedPopup then
        if inWorld and controller:isBot(now) and rejectedPopup.generation == controller.generation and StaticPopup_Hide then
            StaticPopup_Hide(rejectedPopup.name)
        end
        rejectedPopup = nil
    end
    if inWorld then
        if training then training:expire(now,controller.generation) end
        if trainingPlan then trainingPlan:expire(now,controller.generation) end
        if travel then travel:expire(now,controller.generation) end
        if recovery then recovery:expire(now,controller.generation) end
        if itemUse then itemUse:expire(now,controller.generation) end
        if gatherTools then gatherTools:expire(now,controller.generation) end
        if history then
            local pending=history.pending
            history:expire(now,controller.generation)
            if pending and not history.pending then notifyHistory() end
        end
        if activity:expire(now, controller.generation) and activity.supported then model.travelAt = nil end
        local desired, reason
        if automatic() and controller:isBot(now) and not grouped() then
            desired, reason = model:grindDecision(now, questState(model:currentQuest(now)))
        end
        grindReason = reason or ''
        controller:planGrind(desired)
        local command = controller:update(now)
        if command then SendChatMessage(command, 'SAY') end
        if focus then
            local focusCommand=focus:update(controller,now)
            if focusCommand then SendChatMessage(focusCommand,'SAY') end
            if WoweeBotReportStatusProblem then
                if focus.failure then WoweeBotReportStatusProblem('focus',focus.failure)
                elseif focus:fresh(controller,now) then WoweeBotReportStatusProblem('focus',nil,'Quest selection confirmed by the server.') end
            end
        end
        if not model.disabled and now >= nextPoll then request() end
        if WoweeBotReportStatusProblem then
            if controller.failure then WoweeBotReportStatusProblem('control', controller.failure) end
            if controller:isBot(now) and activity.failure then WoweeBotReportStatusProblem('activity', activity.failure) end
            if controller.socialFailure then WoweeBotReportStatusProblem('social', controller.socialFailure) end
            if controller.fishingFailure then WoweeBotReportStatusProblem('fishing', controller.fishingFailure) end
            if controller.requestsFailure then WoweeBotReportStatusProblem('requests', controller.requestsFailure) end
            if recovery and recovery.supported and recovery.failure then WoweeBotReportStatusProblem('recovery',recovery.failure) end
        end
    end
    if now >= nextDraw then draw(); nextDraw = now + 0.25 end
end)
driver:SetScript('OnEvent', function(self, event, ...)
    if event == 'ADDON_LOADED' then
        local name = ...
        if name ~= 'WoweeBotStatus' then return end
        if type(WoweeBotStatusSettings) ~= 'table' then WoweeBotStatusSettings = {} end
        settings = WoweeBotStatusSettings
        if settings.autoGrind == nil then settings.autoGrind = true end
        local p = settings.position
        if type(p) == 'table' and type(p[1]) == 'string' and type(p[2]) == 'string' and
            type(p[3]) == 'number' and type(p[4]) == 'number' then
            panel:ClearAllPoints(); panel:SetPoint(p[1], UIParent, p[2], p[3], p[4])
        end
        if settings.enabled == false then panel:Hide() end
    elseif event == 'PARTY_INVITE_REQUEST' or event == 'GUILD_INVITE_REQUEST' or event == 'TRADE_REQUEST' then
        local kind = event == 'PARTY_INVITE_REQUEST' and 'group' or event == 'GUILD_INVITE_REQUEST' and 'guild' or 'trade'
        local now = GetTime()
        if inWorld and controller:isBot(now) and controller:requestsFresh(now) and controller:rejectsRequest(kind) then
            local decline
            if kind == 'group' then decline = DeclineGroup
            elseif kind == 'guild' then decline = DeclineGuild
            else decline = CloseTrade or CancelTrade end
            if decline then decline() end
            local popup = kind == 'group' and 'PARTY_INVITE' or kind == 'guild' and 'GUILD_INVITE' or 'TRADE'
            if StaticPopup_Hide then StaticPopup_Hide(popup) end
            rejectedPopup = {name=popup,generation=controller.generation}
        end
    elseif event == 'PLAYER_ENTERING_WORLD' then
        focus=newFocus()
        rejectedPopup = nil
        model, controller, activity, inWorld, nextPoll = Status.new(), WoweeBotControl.new(), WoweeBotActivity.new(), true, 0
        history=newHistory();notifyHistory()
        training=newTraining();trainingPlan=newTrainingPlan();travel=newTravel();recovery=newRecovery();itemUse=newItemUse();gatherTools=newGatherTools()
    elseif event == 'PLAYER_LEAVING_WORLD' then
        focus=newFocus()
        model, controller, activity, inWorld = Status.new(), WoweeBotControl.new(), WoweeBotActivity.new(), false
        if history then history.pending=nil end
        if training then training.pending=nil end
        if trainingPlan then trainingPlan.pending=nil end
        if travel then travel.pending=nil end
        if recovery then recovery.pending=nil end
        if itemUse then itemUse.pending=nil end
        if gatherTools then gatherTools.pending=nil end
        notifyHistory()
    elseif event == 'CHAT_MSG_ADDON' then
        local prefix, text, _, sender = ...
        local now, player = GetTime(), UnitName('player')
        local previousMode = controller.mode
        local received = controller:receive(prefix, text, sender, player, now)
        if received and prefix == 'WIDLE' and WoweeBotReportStatusProblem and not controller.failure then
            WoweeBotReportStatusProblem('control', nil)
            if controller.mode == 'manual' then
                WoweeBotReportStatusProblem('activity', nil, 'Activity queries are paused while you play manually.')
                WoweeBotReportStatusProblem('recovery', nil, 'Recovery observation is paused while you play manually.')
            end
        end
        if received and prefix == 'WIDLEPOL' and not controller.socialFailure and WoweeBotReportStatusProblem then
            WoweeBotReportStatusProblem('social', nil, 'Social Detours preference confirmed by the server.')
        end
        if received and prefix == 'WIDLEREQ' and not controller.requestsFailure and WoweeBotReportStatusProblem then
            WoweeBotReportStatusProblem('requests', nil, 'Incoming request policies confirmed by the server.')
        end
        if received and prefix == 'WIDLEFISH' and not controller.fishingFailure and WoweeBotReportStatusProblem then
            WoweeBotReportStatusProblem('fishing', nil, 'Fishing preference confirmed by the server.')
        end
        if received and previousMode ~= controller.mode then
            if history then history.pending,history.failure=nil,nil end
            if training then training.pending,training.failure=nil,nil end
            if trainingPlan then trainingPlan.pending,trainingPlan.failure=nil,nil end
            if travel then travel.pending,travel.snapshot,travel.failure=nil,nil,nil end
            if recovery then recovery.pending,recovery.snapshot,recovery.failure=nil,nil,nil end
            if itemUse then itemUse.pending,itemUse.snapshot,itemUse.failure=nil,nil,nil end
            if gatherTools then gatherTools.pending,gatherTools.snapshot,gatherTools.failure=nil,nil,nil end
            model, nextPoll, lastRequest = Status.new(), 0, -100
            activity.pending = nil
        end
        local supported = activity.supported
        controller:capability(prefix, text, sender, player)
        if focus then
            focus:capability(prefix,text,sender,player,controller)
            focus:receive(prefix,text,sender,player,controller,now)
        end
        if gatherTools then
            gatherTools:capability(prefix,text,sender,player,controller)
            if gatherTools:receive(prefix,text,sender,player,controller.generation,now) and not gatherTools.failure and gatherTools.snapshot then
                if WoweeBotReportGatherTools then WoweeBotReportGatherTools(gatherTools.snapshot) end
            end
        end
        if itemUse then
            itemUse:capability(prefix,text,sender,player,controller)
            if itemUse:receive(prefix,text,sender,player,controller.generation,now) and not itemUse.failure and itemUse.snapshot then
                if WoweeBotReportItemUse then WoweeBotReportItemUse(itemUse.snapshot) end
            end
        end
        if recovery then
            recovery:capability(prefix,text,sender,player,controller)
            if recovery:receive(prefix,text,sender,player,controller.generation,now) and not recovery.failure and recovery.snapshot then
                if WoweeBotReportRecovery then WoweeBotReportRecovery(recovery.snapshot) end
                if WoweeBotReportStatusProblem then WoweeBotReportStatusProblem('recovery',nil,'Fresh recovery status received.') end
            end
        end
        if travel then
            travel:capability(prefix,text,sender,player,controller)
            if travel:receive(prefix,text,sender,player,controller.generation,now) and
                not travel.failure and travel.snapshot and travel.snapshot.warning and WoweeBotReportTravelWarning then
                WoweeBotReportTravelWarning(travel.snapshot.warning)
            end
        end
        if trainingPlan then
            trainingPlan:capability(prefix,text,sender,player,controller)
            if trainingPlan:receive(prefix,text,sender,player,controller.generation,now) and
                not trainingPlan.failure and trainingPlan.snapshot and WoweeBotReportTrainingPlan then
                WoweeBotReportTrainingPlan(trainingPlan.snapshot)
            end
        end
        if training then
            training:capability(prefix,text,sender,player,controller)
            if training:receive(prefix,text,sender,player,controller.generation,now) and
                not training.failure and training.snapshot and WoweeBotReportTraining then
                WoweeBotReportTraining(training.snapshot)
            end
        end
        if history then
            history:capability(prefix,text,sender,player,controller)
            history:receive(prefix,text,sender,player,controller.generation,now)
            notifyHistory()
        end
        local warning = controller:warning(prefix, text, sender, player, now)
        if warning and WoweeBotReportFishingWarning then WoweeBotReportFishingWarning(warning) end
        local collection = controller:collectionWarning(prefix, text, sender, player, now)
        if collection and WoweeBotReportCollectionWarning then WoweeBotReportCollectionWarning(collection) end
        if activity:capability(prefix, text, sender, player, controller) and not supported then nextPoll = 0 end
        local snapshot, activityReply = activity:receive(prefix, text, sender, player, controller.generation, now)
        if activityReply then
            if snapshot and controller:isBot(now) and snapshot.mode == 'bot' then
                WoweeBotActivity.apply(model, snapshot, now)
                if WoweeBotReportStatusProblem then WoweeBotReportStatusProblem('activity', nil) end
            else model.travelAt = nil end
        end
        -- Once typed activity is available, unsolicited legacy chat must not
        -- overwrite it or supply grind intent from an unrelated old request.
        if (not activity.supported and model:receive(prefix, text, sender, player, now)) or received or activityReply then draw() end
    elseif event == 'CHAT_MSG_SYSTEM' then
        local text = ...
        if text and text:find('Enable player ai', 1, true) then
            model, nextPoll = Status.new(), 0; controller:refresh()
        elseif text and text:find('Disable player ai', 1, true) then
            model = Status.new(); model.disabled = true; controller:refresh()
        end
    end
    draw()
end)
for _, event in ipairs({'ADDON_LOADED', 'PLAYER_ENTERING_WORLD', 'PLAYER_LEAVING_WORLD',
    'CHAT_MSG_ADDON', 'CHAT_MSG_SYSTEM', 'QUEST_LOG_UPDATE', 'PARTY_INVITE_REQUEST', 'GUILD_INVITE_REQUEST', 'TRADE_REQUEST'}) do driver:RegisterEvent(event) end

local function diagnostic()
    local now = GetTime()
    local lines = controller:diagnostic(now)
    for _, line in ipairs(activity:diagnostic(now)) do lines[#lines + 1] = line end
    lines[#lines + 1] = 'Controller callback: ' .. (lastDriverTick and (math.max(0, math.floor(now - lastDriverTick)) .. 's ago') or 'not run') ..
        '; in world: ' .. tostring(inWorld) .. '; panel shown: ' .. tostring(panel:IsShown()) .. '; grouped: ' .. tostring(grouped())
    for _, line in ipairs(lines) do print('Bot status: ' .. line) end
end
SLASH_WOWEEBOTSTATUS1 = '/botstatus'
SlashCmdList.WOWEEBOTSTATUS = function(command)
    if command and command:lower() == 'hide' then panel:Hide(); return end
    if command and command:lower() == 'debug' then diagnostic(); return end
    if command and command:lower() == 'refresh' then refreshStatus(); return end
    panel:Show(); model.disabled = nil; request(); draw()
end
SLASH_WOWEEBOTPLAY1 = '/botplay'
SlashCmdList.WOWEEBOTPLAY = function(command)
    local mode = command and command:lower()
    if mode == 'manual' or mode == 'bot' then controller:want(mode); draw()
    else WoweeBotToggleControl() end
end
SLASH_WOWEEBOTPOLICY1 = '/botpolicy'
SlashCmdList.WOWEEBOTPOLICY = function() policies:Show(); drawPolicy() end
draw()

-- The picker expresses intent; only the matched server reply changes the label.
function WoweeBotQuestFocusView()
    local now=GetTime()
    return focus and focus:label(controller,now) or 'Selected quest: unavailable',
        inWorld and focus and focus:fresh(controller,now) and not focus.pending and focus.state~='unavailable',
        focus and focus.questId
end
function WoweeBotSelectQuest(questId)
    return inWorld and focus and focus:choose(questId,controller,GetTime())
end
