local Inbox = WoweeBotInbox
local driver = CreateFrame('Frame', 'WoweeBotNotesDriver', UIParent)
local frame = CreateFrame('Frame', 'WoweeBotNotesFrame', UIParent)
frame:SetSize(640, 550)
frame:SetPoint('CENTER', UIParent, 'CENTER', 0, 0)
frame:SetFrameStrata('DIALOG')
frame:SetMovable(true); frame:EnableMouse(true); frame:SetClampedToScreen(true)
frame:RegisterForDrag('LeftButton')
frame:SetScript('OnDragStart', function(self) self:StartMoving() end)
frame:SetScript('OnDragStop', function(self) self:StopMovingOrSizing() end)
frame:SetBackdrop({bgFile = 'Interface\\Tooltips\\UI-Tooltip-Background',
    edgeFile = 'Interface\\Tooltips\\UI-Tooltip-Border', tile = true, tileSize = 16, edgeSize = 16,
    insets = {left = 4, right = 4, top = 4, bottom = 4}})
frame:SetBackdropColor(0.035, 0.04, 0.025, 0.97)
frame:SetBackdropBorderColor(0.65, 0.53, 0.25, 1)

local function text(parent, name, font, x, y, width, height)
    local label = parent:CreateFontString(name, 'OVERLAY', font)
    label:SetPoint('TOPLEFT', parent, 'TOPLEFT', x, y)
    label:SetSize(width, height); label:SetJustifyH('LEFT'); label:SetJustifyV('TOP')
    label:SetWordWrap(true)
    return label
end
local function button(parent, name, title, x, y, width, callback)
    local b = CreateFrame('Button', name, parent, 'UIPanelButtonTemplate')
    b:SetSize(width, 23); b:SetPoint('TOPLEFT', parent, 'TOPLEFT', x, y)
    b:SetText(title); b:SetScript('OnClick', callback)
    return b
end
local function shown(widget, value)
    if value then widget:Show() else widget:Hide() end
end
text(frame, nil, 'GameFontNormalLarge', 18, -17, 400, 25):SetText('Decisions & notes')
local summary = text(frame, 'WoweeBotNotesSummary', 'GameFontHighlight', 18, -51, 600, 24)
local storage = text(frame, 'WoweeBotNotesStorage', 'GameFontHighlightSmall', 18, -80, 600, 32)
local footer = text(frame, nil, 'GameFontHighlightSmall', 18, -510, 600, 30)
local detail = text(frame, 'WoweeBotNotesDetail', 'GameFontHighlight', 22, -155, 590, 305)
local heading = text(frame, 'WoweeBotNotesHeading', 'GameFontNormalLarge', 22, -118, 590, 32)
local inbox, persistent, online, page, selected, detailPage = nil, false, false, 1, nil, 1
local scopeIdentity, scopeCharacter
local rows, render, nextCheck = {}, nil, 0

local function save() if persistent and WoweeSaveVariables then WoweeSaveVariables() end end
local function now() return GetServerTime and GetServerTime() or time() end
local function age(stamp)
    local seconds = math.max(0, now() - stamp)
    if seconds < 60 then return 'just now' end
    if seconds < 3600 then return math.floor(seconds / 60) .. 'm ago' end
    if seconds < 86400 then return math.floor(seconds / 3600) .. 'h ago' end
    return math.floor(seconds / 86400) .. 'd ago'
end

local launcher = CreateFrame('Button', 'WoweeBotNotesLauncher', UIParent, 'UIPanelButtonTemplate')
launcher:SetSize(130, 22); launcher:SetPoint('BOTTOM', UIParent, 'BOTTOM', 116, 110)
local function toggle()
    if frame:IsShown() then frame:Hide() else frame:Show(); render() end
end
launcher:SetScript('OnClick', toggle)
button(frame, nil, 'x', 596, -12, 24, function() frame:Hide() end)
local clear = button(frame, 'WoweeBotNotesClear', 'Clear notes', 486, -48, 130, function()
    if inbox and inbox:clearNotes() then selected = nil; save(); render() end
end)
local back = button(frame, 'WoweeBotNotesBack', 'Back', 18, -473, 80, function()
    selected = nil; render()
end)
local open = button(frame, 'WoweeBotNotesOpen', 'Open', 108, -473, 130, function()
    if not selected or not online then return end
    if selected.action == 'talents' and ToggleTalentFrame then ToggleTalentFrame()
    elseif selected.action == 'spellbook' and ToggleSpellBook then ToggleSpellBook(BOOKTYPE_SPELL or 'spell')
    elseif selected.action == 'botstatus' and SlashCmdList.WOWEEBOTSTATUS then SlashCmdList.WOWEEBOTSTATUS('') end
end)
local prev = button(frame, nil, '< Previous', 330, -473, 100, function()
    if selected then detailPage = math.max(1, detailPage - 1) else page = math.max(1, page - 1) end
    render()
end)
local following = button(frame, nil, 'Next >', 440, -473, 100, function()
    if selected then detailPage = detailPage + 1 else page = page + 1 end
    render()
end)

for i = 1, 5 do
    local row = CreateFrame('Button', 'WoweeBotNotesRow' .. i, frame)
    row:SetSize(600, 66); row:SetPoint('TOPLEFT', frame, 'TOPLEFT', 18, -116 - (i - 1) * 70)
    row.title = text(row, nil, 'GameFontNormal', 5, -3, 475, 20)
    row.age = text(row, nil, 'GameFontHighlightSmall', 490, -3, 105, 20)
    row.body = text(row, nil, 'GameFontHighlightSmall', 5, -27, 490, 35)
    row:SetScript('OnClick', function()
        selected, detailPage = row.entry, 1
        if selected then selected.read = true; save() end
        render()
    end)
    row.dismiss = button(row, nil, 'Dismiss', 510, -28, 85, function()
        if inbox and row.entry and inbox:dismiss(row.entry.key) then save(); render() end
    end)
    rows[i] = row
end

render = function()
    local counts = inbox and inbox:counts() or {decision=0, blocked=0, note=0, unread=0}
    launcher:SetText('Notes' .. (counts.unread > 0 and (' (' .. counts.unread .. ')') or ''))
    if not frame:IsShown() then return end
    summary:SetText(counts.decision .. ' choices  |  ' .. counts.blocked .. ' blocked  |  ' .. counts.note .. ' notes')
    storage:SetText(not online and 'Log into a character to see its notes.' or
        (persistent and ('Notes for ' .. UnitName('player') .. ' on ' .. GetRealmName()) or
        'Notes are temporary. Update WoWee to save them separately for each realm.'))
    shown(clear, inbox ~= nil and not selected)
    detail:SetText(''); heading:SetText('')
    for _, row in ipairs(rows) do row:Hide() end
    shown(back, selected ~= nil)
    open:Hide()
    local pages = 1
    if selected then
        heading:SetText(selected.title)
        if selected.spells then
            pages = math.max(1, math.ceil(#selected.spells / 10))
            detailPage = math.min(detailPage, pages)
            local lines = {selected.text, ''}
            for i = (detailPage - 1) * 10 + 1, math.min(detailPage * 10, #selected.spells) do
                local spell = selected.spells[i]
                lines[#lines + 1] = spell.name .. (spell.rank ~= '' and (' (' .. spell.rank .. ')') or '')
            end
            detail:SetText(table.concat(lines, '\n'))
        else detail:SetText(selected.text) end
        if selected.action then
            open:SetText(selected.action == 'talents' and 'Open Talents' or
                selected.action == 'botstatus' and 'Open Bot activity' or 'Open Spellbook')
            shown(open, online)
        end
        footer:SetText(age(selected.created) .. '  |  Page ' .. detailPage .. ' / ' .. pages)
    else
        local entries = inbox and inbox:visible() or {}
        pages = math.max(1, math.ceil(#entries / 5)); page = math.min(page, pages)
        if #entries == 0 then heading:SetText('Nothing needs your attention') end
        for i, row in ipairs(rows) do
            local e = entries[(page - 1) * 5 + i]
            if e then
                row.entry = e
                row.title:SetText((not e.read and '* ' or '') ..
                    (e.kind == 'blocked' and 'Blocked: ' or '') .. e.title)
                row.age:SetText(age(e.updated)); row.body:SetText(e.text)
                row:Show(); shown(row.dismiss, e.kind == 'note')
            end
        end
        footer:SetText('Page ' .. page .. ' / ' .. pages .. '  |  Click a note to read details. Clear notes keeps unresolved choices and blockers.')
    end
    shown(prev, (selected and detailPage or page) > 1)
    shown(following, (selected and detailPage or page) < pages)
end

local function initialize()
    local character = UnitName('player')
    if not character or character == '' or character == 'Unknown' then return end
    local getIdentity = rawget(_G, 'GetWoweeRealmIdentity')
    local identity = type(getIdentity) == 'function' and getIdentity() or nil
    persistent = type(identity) == 'string' and identity ~= ''
    if inbox and scopeIdentity == identity and scopeCharacter == character then return end
    scopeIdentity, scopeCharacter = identity, character
    if type(rawget(_G, 'WoweeBotNotesData')) ~= 'table' then WoweeBotNotesData = {} end
    inbox = Inbox.new(persistent and WoweeBotNotesData or {}, persistent and identity or 'temporary', character)
    page, selected, detailPage = 1, nil, 1
    if inbox:talents(UnitCharacterPoints('player'), now()) then save() end
end

-- Only verified controller/transport facts enter here. Travel cooldowns, NPC
-- proximity and red trainer rows cannot prove an unaffordable-training blocker.
function WoweeBotReportStatusProblem(key, message, resolution)
    if not online or not inbox then return end
    local changed
    if message then changed = inbox:put(key, 'blocked',
        key == 'control' and 'Control needs attention' or key == 'social' and 'Social Detours policy needs attention' or key == 'fishing' and 'Fishing policy needs attention' or 'Activity status unavailable',
        message .. ((key == 'social' or key == 'fishing') and '. Open Policies and press Refresh to retry.' or '. Open Bot activity and press Refresh to retry.'), now())
    else changed = inbox:resolve(key, now(), resolution or 'Fresh server confirmation received. This status problem is resolved.') end
    if changed then save(); render() end
end

-- Called only after the controller validates the server capability, sender,
-- acknowledgement ID, generation and freshness. No game actions run here.
function WoweeBotReportFishingWarning(fact)
    if online and inbox and inbox:fishingWarning(fact, now()) then save(); render() end
end

function WoweeBotReportCollectionWarning(fact)
    if online and inbox and inbox:collectionWarning(fact, now()) then save(); render() end
end

driver:SetScript('OnEvent', function(self, event, ...)
    if event == 'ADDON_LOADED' then
        if (...) ~= 'WoweeBotStatus' then return end
        online = UnitExists('player') and true or false
        if online then initialize() end
    elseif event == 'PLAYER_ENTERING_WORLD' then
        online = true; initialize()
    elseif event == 'PLAYER_LEAVING_WORLD' then
        save(); online, inbox, selected, persistent = false, nil, nil, false
    elseif event == 'UI_ERROR_MESSAGE' and online and inbox then
        local message, second = ...
        if type(message) ~= 'string' then message = second end
        local generation = WoweeBotConfirmedControlGeneration and WoweeBotConfirmedControlGeneration()
        if inbox:observeError(message, GetTime(), now(), generation) then save() end
    elseif event == 'LEARNED_SPELL_IN_TAB' and online and inbox then
        -- WoWee sends the actual spell ID from SMSG_LEARNED_SPELL, not a tab index.
        local id = tonumber((...))
        if id then
            local name, rank = GetSpellInfo(id)
            if inbox:learn(id, name, rank, now()) then save() end
        end
    elseif online and inbox then
        if inbox:talents(UnitCharacterPoints('player'), now()) then save() end
    end
    render()
end)
for _, event in ipairs({'ADDON_LOADED', 'PLAYER_ENTERING_WORLD', 'PLAYER_LEAVING_WORLD',
    'LEARNED_SPELL_IN_TAB', 'CHARACTER_POINTS_CHANGED', 'PLAYER_TALENT_UPDATE', 'UI_ERROR_MESSAGE'}) do driver:RegisterEvent(event) end
driver:SetScript('OnUpdate', function()
    if GetTime() < nextCheck then return end
    nextCheck = GetTime() + 3
    if online and not inbox then initialize() end
    if online and inbox and inbox:talents(UnitCharacterPoints('player'), now()) then save() end
    render()
end)
SLASH_WOWEEBOTNOTES1 = '/botnotes'
SlashCmdList.WOWEEBOTNOTES = function() frame:Show(); render() end
frame:Hide(); render()
