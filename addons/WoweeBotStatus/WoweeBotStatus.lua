local Status = WoweeBotStatus
local model = Status.new()
local controller = WoweeBotControl.new()
local activity = WoweeBotActivity.new()
local driver = CreateFrame('Frame', 'WoweeBotController', UIParent)
local panel = CreateFrame('Frame', 'WoweeBotStatusFrame', UIParent)
panel:SetSize(690, 234)
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
local goal = label('Goal', 'GameFontNormalLarge', 16, -38, 655)
goal:SetTextColor(1, 0.84, 0.25)
local details = label('Details', 'GameFontHighlight', 16, -64, 655)
local progress = label('Progress', 'GameFontHighlightSmall', 16, -88, 655)
local action = label('Action', 'GameFontHighlightSmall', 16, -113, 655)
action:SetTextColor(0.65, 0.7, 0.7)
local control = label('Control', 'GameFontHighlightSmall', 16, -162, 655)
control:SetTextColor(0.72, 0.8, 0.62)
local modeLabel = label('Mode', 'GameFontHighlight', 16, -138, 500)
local interaction = label('Interaction', 'GameFontHighlightSmall', 16, -186, 655)

local settings, nextPoll, nextDraw, lastRequest = nil, 0, 0, -100
local inWorld = UnitExists('player') and true or false
local autoButton, playButton
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
    if not inWorld or not controller:isBot(now) or now - lastRequest < 2 then return end
    local player = UnitName('player')
    if not player or player == '' or player == 'Unknown' then return end
    if activity.supported and activity.pending then return end
    lastRequest, nextPoll = now, now + 8
    model:request(now)
    if activity.supported then
        SendChatMessage(activity:request(controller.generation, now), 'SAY')
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
    progress:SetText('')
    action:SetText('')
    control:SetText('')
    interaction:SetText('')
    modeLabel:SetText(controller:label(now))
    if playButton then
        playButton:SetText(controller.failure and 'Retry control' or controller.intent and 'Switching...' or
            (controller.mode == 'bot' and 'Let me play' or 'Bot, you play'))
    end
    if autoButton then autoButton:SetText(automatic() and 'Auto grind: On' or 'Auto grind: Off') end
    if controller:fresh(now) and controller.mode == 'manual' then
        freshness:SetText('Manual control')
        goal:SetText('Your character is under your control')
        details:SetText('Bot actions are paused. Play normally, then resume when ready.')
        control:SetText('Auto grind paused until bot control is confirmed')
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
            details:SetText('No current status received. Player AI may be off.')
        else
            details:SetText('Asking your Playerbot what it is doing...')
        end
        return
    end
    freshness:SetText('Updated ' .. math.floor(now - model.travelAt) .. 's ago')
    local questId, questTitle = model:currentQuest(now)
    goal:SetText(questTitle and ('Quest: ' .. questTitle) or ('Goal: ' .. (model.goal or 'No destination')))
    local parts = {model.status or (model.kind == 'idle' and 'Idle' or 'No travel status reported')}
    if model.typed then
        parts[1] = (model.aiState or 'unknown'):gsub('_', ' ') .. ' - ' .. parts[1]
        if questTitle then parts[#parts + 1] = 'Destination: ' .. model.goal end
    end
    if model.distance and model.kind ~= 'idle' then parts[#parts + 1] = model.distance .. ' yd' end
    if model.location then parts[#parts + 1] = Status.clean(model.location) end
    details:SetText(table.concat(parts, '  -  '))
    if model.typed and model.interactionName then
        interaction:SetText('Interaction target: ' .. model.interactionName ..
            (model.interactionTask and (' - selected task: ' .. model.interactionTask) or ''))
    elseif model.typed then
        interaction:SetText('No NPC interaction selected')
    end
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
    if model.actionAt and now - model.actionAt <= Status.STALE_AFTER then
        action:SetText((model.typed and 'Last successful action: ' or 'Last action: ') .. model.action)
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
button('Refresh', 65, -32, function()
    model.disabled = nil; activity.pending, activity.failure = nil, nil
    controller:refresh(); nextPoll = 0; draw()
end)
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
    if inWorld then
        if activity:expire(now, controller.generation) and activity.supported then model.travelAt = nil end
        local desired, reason
        if automatic() and controller:isBot(now) and not grouped() then
            desired, reason = model:grindDecision(now, questState(model:currentQuest(now)))
        end
        grindReason = reason or ''
        controller:planGrind(desired)
        local command = controller:update(now)
        if command then SendChatMessage(command, 'SAY') end
        if not model.disabled and now >= nextPoll then request() end
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
    elseif event == 'PLAYER_ENTERING_WORLD' then
        model, controller, activity, inWorld, nextPoll = Status.new(), WoweeBotControl.new(), WoweeBotActivity.new(), true, 0
    elseif event == 'PLAYER_LEAVING_WORLD' then
        model, controller, activity, inWorld = Status.new(), WoweeBotControl.new(), WoweeBotActivity.new(), false
    elseif event == 'CHAT_MSG_ADDON' then
        local prefix, text, _, sender = ...
        local now, player = GetTime(), UnitName('player')
        local previousMode = controller.mode
        local received = controller:receive(prefix, text, sender, player, now)
        if received and previousMode ~= controller.mode then
            model, nextPoll, lastRequest = Status.new(), 0, -100
            activity.pending = nil
        end
        local supported = activity.supported
        if activity:capability(prefix, text, sender, player, controller) and not supported then nextPoll = 0 end
        local snapshot, activityReply = activity:receive(prefix, text, sender, player, controller.generation, now)
        if activityReply then
            if snapshot and controller:isBot(now) and snapshot.mode == 'bot' then
                WoweeBotActivity.apply(model, snapshot, now)
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
    'CHAT_MSG_ADDON', 'CHAT_MSG_SYSTEM', 'QUEST_LOG_UPDATE'}) do driver:RegisterEvent(event) end

SLASH_WOWEEBOTSTATUS1 = '/botstatus'
SlashCmdList.WOWEEBOTSTATUS = function(command)
    if command and command:lower() == 'hide' then panel:Hide(); return end
    panel:Show(); model.disabled = nil; request(); draw()
end
SLASH_WOWEEBOTPLAY1 = '/botplay'
SlashCmdList.WOWEEBOTPLAY = function(command)
    local mode = command and command:lower()
    if mode == 'manual' or mode == 'bot' then controller:want(mode); draw()
    else WoweeBotToggleControl() end
end
draw()
