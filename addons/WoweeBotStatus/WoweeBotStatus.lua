local Status = WoweeBotStatus
local model = Status.new()
local grind = WoweeBotGrind.new()
local panel = CreateFrame('Frame', 'WoweeBotStatusFrame', UIParent)
panel:SetSize(590, 166)
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

local title = label('Title', 'GameFontNormal', 16, -13, 180)
title:SetText('BOT ACTIVITY')
title:SetTextColor(0.86, 0.72, 0.35)
local freshness = label('Freshness', 'GameFontHighlightSmall', 170, -15, 110)
freshness:SetTextColor(0.65, 0.68, 0.63)
local goal = label('Goal', 'GameFontNormalLarge', 16, -38, 555)
goal:SetTextColor(1, 0.84, 0.25)
local details = label('Details', 'GameFontHighlight', 16, -64, 555)
local progress = label('Progress', 'GameFontHighlightSmall', 16, -88, 555)
local action = label('Action', 'GameFontHighlightSmall', 16, -113, 555)
action:SetTextColor(0.65, 0.7, 0.7)
local control = label('Control', 'GameFontHighlightSmall', 16, -137, 555)
control:SetTextColor(0.72, 0.8, 0.62)

local settings, nextPoll, nextDraw, lastRequest = nil, 0, 0, -100
local inWorld = UnitExists('player') and true or false
local autoButton

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
    if not inWorld or now - lastRequest < 2 then return end
    local player = UnitName('player')
    if not player or player == '' or player == 'Unknown' then return end
    lastRequest, nextPoll = now, now + 8
    model:request(now)
    SendAddonMessage('BOT', 'debug traveldetail', 'WHISPER', player)
    SendAddonMessage('BOT', 'debug action', 'WHISPER', player)
    if automatic() then SendAddonMessage('BOT', '#a nc ?', 'WHISPER', player) end
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
    if autoButton then autoButton:SetText(automatic() and 'Auto grind: On' or 'Auto grind: Off') end
    if model.disabled then
        freshness:SetText('Player AI is off')
        goal:SetText('Player AI disabled')
        details:SetText('Enable player AI to see its current activity.')
        return
    end
    if automatic() and inWorld and panel:IsShown() then
        local command = grind:update(model, questState(model:currentQuest(now)), now, grouped())
        if command then
            SendAddonMessage('BOT', command, 'WHISPER', UnitName('player'))
        end
        control:SetText(grind:label(now))
    elseif not automatic() then
        control:SetText('Grind: manual control (auto grind is off)')
    else
        control:SetText('Auto grind paused')
    end
    if not model:isFresh(now) then
        freshness:SetText(model.travelAt and ('Last update ' .. math.floor(now - model.travelAt) .. 's ago') or '')
        goal:SetText(model.unsupported and 'Bot status unavailable' or 'Waiting for bot status')
        if model.unsupported then
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
    if model.distance and model.kind ~= 'idle' then parts[#parts + 1] = model.distance .. ' yd' end
    if model.location then parts[#parts + 1] = Status.clean(model.location) end
    details:SetText(table.concat(parts, '  -  '))
    local objective = questProgress(questId)
    if objective == '' and model.kind and model.kind:find('quest', 1, true) and not questId then
        objective = grouped() and 'Quest name unavailable while grouped; showing the destination.' or
            'Waiting for the bot to report its quest name.'
    elseif objective == '' then
        objective = model.reportAt and now - model.reportAt <= Status.STALE_AFTER and model.report or ''
    end
    progress:SetText(objective)
    if model.actionAt and now - model.actionAt <= Status.STALE_AFTER then
        action:SetText('Last action: ' .. model.action)
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
button('Refresh', 65, -32, function() model.disabled = nil; grind:retry(); request(); draw() end)
button('x', 20, -9, function() panel:Hide() end)
autoButton = button('Auto grind: On', 115, -185, function()
    settings.autoGrind = not automatic()
    grind = WoweeBotGrind.new()
    save(); nextPoll = 0; draw()
end, 'WoweeBotStatusAutoGrind')

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
panel:SetScript('OnUpdate', function()
    local now = GetTime()
    if inWorld and not model.disabled and now >= nextPoll then request() end
    if now >= nextDraw then draw(); nextDraw = now + 0.25 end
end)
panel:SetScript('OnEvent', function(self, event, ...)
    if event == 'ADDON_LOADED' then
        local name = ...
        if name ~= 'WoweeBotStatus' then return end
        if type(WoweeBotStatusSettings) ~= 'table' then WoweeBotStatusSettings = {} end
        settings = WoweeBotStatusSettings
        if settings.autoGrind == nil then settings.autoGrind = true end
        local p = settings.position
        if type(p) == 'table' and type(p[1]) == 'string' and type(p[2]) == 'string' and
            type(p[3]) == 'number' and type(p[4]) == 'number' then
            self:ClearAllPoints(); self:SetPoint(p[1], UIParent, p[2], p[3], p[4])
        end
        if settings.enabled == false then self:Hide() end
    elseif event == 'PLAYER_ENTERING_WORLD' then
        model, grind, inWorld, nextPoll = Status.new(), WoweeBotGrind.new(), true, 0
    elseif event == 'PLAYER_LEAVING_WORLD' then
        model, grind, inWorld = Status.new(), WoweeBotGrind.new(), false
    elseif event == 'CHAT_MSG_ADDON' then
        local prefix, text, _, sender = ...
        local now, player = GetTime(), UnitName('player')
        local received = grind:receive(prefix, text, sender, player, now)
        if model:receive(prefix, text, sender, player, now) or received then draw() end
    elseif event == 'CHAT_MSG_SYSTEM' then
        local text = ...
        if text and text:find('Enable player ai', 1, true) then
            model, grind, nextPoll = Status.new(), WoweeBotGrind.new(), 0
        elseif text and text:find('Disable player ai', 1, true) then
            model, grind = Status.new(), WoweeBotGrind.new(); model.disabled = true
        end
    end
    draw()
end)
for _, event in ipairs({'ADDON_LOADED', 'PLAYER_ENTERING_WORLD', 'PLAYER_LEAVING_WORLD',
    'CHAT_MSG_ADDON', 'CHAT_MSG_SYSTEM', 'QUEST_LOG_UPDATE'}) do panel:RegisterEvent(event) end

SLASH_WOWEEBOTSTATUS1 = '/botstatus'
SlashCmdList.WOWEEBOTSTATUS = function(command)
    if command and command:lower() == 'hide' then panel:Hide(); return end
    panel:Show(); model.disabled = nil; request(); draw()
end
draw()
