-- Browsing is local UI state. It never changes the server's quest focus.
local frame=CreateFrame('Frame','WoweeBotQuestsFrame',UIParent)
frame:SetSize(660,700);frame:SetPoint('CENTER',UIParent,'CENTER',0,0)
frame:SetFrameStrata('DIALOG');frame:SetClampedToScreen(true);frame:EnableMouse(true)
frame:SetBackdrop({bgFile='Interface\\Tooltips\\UI-Tooltip-Background',
    edgeFile='Interface\\Tooltips\\UI-Tooltip-Border',tile=true,tileSize=16,edgeSize=16,
    insets={left=4,right=4,top=4,bottom=4}})
frame:SetBackdropColor(0.035,0.04,0.025,0.97)
local function label(name,font,y,height)
    local text=frame:CreateFontString(name,'OVERLAY',font)
    text:SetPoint('TOPLEFT',frame,'TOPLEFT',18,y);text:SetSize(620,height)
    text:SetJustifyH('LEFT');return text
end
label(nil,'GameFontNormalLarge',-16,25):SetText('Accepted quests')
local scope=label('WoweeBotQuestsScope','GameFontHighlightSmall',-47,22)
local status=label('WoweeBotQuestsStatus','GameFontHighlightSmall',-72,28)
local detail=label('WoweeBotQuestsDetail','GameFontHighlight',-365,215)
detail:SetJustifyV('TOP')
local selected=label('WoweeBotQuestsSelected','GameFontNormal',-585,42)
label(nil,'GameFontHighlightSmall',-669,22):SetText('Browse rows, then choose Work on this quest. Manual/Bot control stays unchanged.')
local page,viewing,online,dirty,nextDraw=1,nil,false,true,0
local rows,entries={},{}
local render
local function button(name,title,x,y,width,callback)
    local b=CreateFrame('Button',name,frame,'UIPanelButtonTemplate')
    b:SetSize(width,23);b:SetPoint('TOPLEFT',frame,'TOPLEFT',x,y)
    b:SetText(title);b:SetScript('OnClick',callback);return b
end
button(nil,'x',616,-12,26,function() frame:Hide() end)
for i=1,5 do
    local rowIndex=i
    local row=button('WoweeBotQuestRow'..i,'',18,-108-(i-1)*43,620,function()
        local quest=entries[(page-1)*5+rowIndex]
        viewing=quest and quest.questId or nil
        render()
    end)
    row:SetHeight(40);rows[i]=row
end
local previous=button('WoweeBotQuestsPrevious','< Previous',18,-330,110,function() page=math.max(1,page-1);viewing=nil;render() end)
local following=button('WoweeBotQuestsNext','Next >',138,-330,110,function() page=page+1;viewing=nil;render() end)
local work=button('WoweeBotQuestWork','Work on this quest',18,-634,190,function()
    if type(WoweeBotSelectQuest)=='function' and viewing then WoweeBotSelectQuest(viewing) end
    render()
end)
local clear=button('WoweeBotQuestClear','Clear quest selection',220,-634,190,function()
    if type(WoweeBotSelectQuest)=='function' then WoweeBotSelectQuest(0) end
    render()
end)
local function questState(quest)
    if quest.failed then return 'Failed' end
    if quest.complete then
        for _,objective in ipairs(quest.objectives or {}) do
            if not objective.finished then return 'Server marked complete; checking objective progress' end
        end
        return 'Ready to turn in'
    end
    return 'In progress'
end
local function title(quest)
    return quest.title~='' and quest.title or ('Quest #'..quest.questId)
end
render=function()
    local available=online and type(WoweeGetAcceptedQuests)=='function'
    entries=available and WoweeGetAcceptedQuests() or nil
    local valid=type(entries)=='table'
    entries=valid and entries or {}
    local pages=math.max(1,math.ceil(#entries/5));page=math.min(page,pages)
    scope:SetText((UnitName('player') or 'Character')..' on '..(GetRealmName() or 'realm'))
    status:SetText(not online and 'Log in to view accepted quests.' or not valid and
        'Quest data unavailable. This view requires an updated native WoWee build.' or
        (#entries..' accepted - page '..page..'/'..pages))
    local viewed
    for _,quest in ipairs(entries) do if quest.questId==viewing then viewed=quest end end
    if not viewed then viewed=entries[(page-1)*5+1];viewing=viewed and viewed.questId or nil end
    for i,row in ipairs(rows) do
        local quest=entries[(page-1)*5+i]
        if quest then
            row:SetText((quest.questId==viewing and '> ' or '')..'['..quest.level..'] '..title(quest)..'\n'..questState(quest))
            row:Show();row:Enable()
        else row:Hide();row:Disable() end
    end
    local lines={}
    if viewed then
        lines={'Viewing: '..title(viewed),questState(viewed)}
        for _,objective in ipairs(viewed.objectives or {}) do lines[#lines+1]=objective.text end
        if #(viewed.objectives or {})==0 then
            lines[#lines+1]=viewed.summary~='' and viewed.summary or 'No counted objectives reported.'
        end
    elseif valid then lines={'No accepted quests in this character\'s quest log.'} end
    detail:SetText(table.concat(lines,'\n'))
    local view=rawget(_G,'WoweeBotQuestFocusView')
    local focusText,canChoose,focused
    if online and type(view)=='function' then focusText,canChoose,focused=view() end
    selected:SetText(focusText or 'Selected quest: waiting for server capability')
    if canChoose and viewed and not viewed.failed and focused~=viewed.questId then work:Enable() else work:Disable() end
    if canChoose then clear:Enable() else clear:Disable() end
    if page>1 then previous:Enable() else previous:Disable() end
    if page<pages then following:Enable() else following:Disable() end
    dirty=false
end
function WoweeBotQuestsToggle()
    if frame:IsShown() then frame:Hide() else frame:Show();render() end
end
local launcher=CreateFrame('Button','WoweeBotQuestsLauncher',UIParent,'UIPanelButtonTemplate')
launcher:SetSize(90,22);launcher:SetPoint('BOTTOM',UIParent,'BOTTOM',-102,110)
launcher:SetText('Quests');launcher:SetScript('OnClick',WoweeBotQuestsToggle)
-- Own frame and event/update handlers: a popup error cannot disable the
-- manual/bot controller. Only redraw while visible, including item-name replies.
frame:SetScript('OnEvent',function(self,event)
    if event=='PLAYER_ENTERING_WORLD' then online=true;page=1;viewing=nil end
    if event=='PLAYER_LEAVING_WORLD' then online=false;entries={};viewing=nil end
    dirty=true
end)
for _,event in ipairs({'PLAYER_ENTERING_WORLD','PLAYER_LEAVING_WORLD','QUEST_LOG_UPDATE','BAG_UPDATE'}) do frame:RegisterEvent(event) end
frame:SetScript('OnUpdate',function()
    local now=GetTime()
    if dirty or now>=nextDraw then render();nextDraw=now+1 end
end)
SLASH_WOWEEBOTQUESTS1='/botquests'
SlashCmdList.WOWEEBOTQUESTS=function() frame:Show();render() end
frame:Hide()
