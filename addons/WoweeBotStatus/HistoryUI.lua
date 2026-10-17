local frame = CreateFrame('Frame','WoweeBotHistoryFrame',UIParent)
frame:SetSize(660,440); frame:SetPoint('CENTER',UIParent,'CENTER',0,40)
frame:SetFrameStrata('DIALOG'); frame:SetClampedToScreen(true)
frame:SetBackdrop({bgFile='Interface\\Tooltips\\UI-Tooltip-Background',
    edgeFile='Interface\\Tooltips\\UI-Tooltip-Border',tile=true,tileSize=16,edgeSize=16,
    insets={left=4,right=4,top=4,bottom=4}})
frame:SetBackdropColor(0.035,0.04,0.025,0.97)
local function label(name,font,y,height)
    local text=frame:CreateFontString(name,'OVERLAY',font)
    text:SetPoint('TOPLEFT',frame,'TOPLEFT',18,y);text:SetSize(620,height)
    text:SetJustifyH('LEFT'); return text
end
label(nil,'GameFontNormalLarge',-16,25):SetText('Recent bot activity')
local scope=label('WoweeBotHistoryScope','GameFontHighlightSmall',-47,22)
local status=label('WoweeBotHistoryStatus','GameFontHighlightSmall',-72,36)
local rows={}
for i=1,5 do rows[i]=label('WoweeBotHistoryRow'..i,'GameFontHighlight',-115-(i-1)*49,44) end
local footer=label(nil,'GameFontHighlightSmall',-401,25)
footer:SetText('Seen shows when the update arrived. Current task stays in Bot activity.')
local model, online, page, dirty, nextDraw = nil,false,1,true,0
local render
local function button(name,title,x,y,width,callback)
    local b=CreateFrame('Button',name,frame,'UIPanelButtonTemplate')
    b:SetSize(width,23);b:SetPoint('TOPLEFT',frame,'TOPLEFT',x,y)
    b:SetText(title);b:SetScript('OnClick',callback);return b
end
button(nil,'x',616,-12,26,function() frame:Hide() end)
local previous=button('WoweeBotHistoryPrevious','< Previous',18,-371,110,function() page=math.max(1,page-1);render() end)
local following=button('WoweeBotHistoryNext','Next >',138,-371,110,function() page=page+1;render() end)
local function age(at)
    local seconds=math.max(0,GetTime()-at)
    if seconds<60 then return math.floor(seconds)..'s ago' end
    if seconds<3600 then return math.floor(seconds/60)..'m ago' end
    return math.floor(seconds/3600)..'h ago'
end
render=function()
    local count=model and #model.entries or 0
    local pages=math.max(1,math.ceil(count/5));page=math.min(page,pages)
    scope:SetText((UnitName('player') or 'Character')..' on '..(GetRealmName() or 'realm')..' - this world session')
    local message
    if not online then message='Offline; previous observations are historical.'
    elseif not model or not model.supported then message='Server activity history is unavailable.'
    elseif model.failure then message=model.failure
    elseif not model.completedAt then message='Waiting for a complete history reply.'
    elseif count==0 then message='No verified purchases, learned spells or quest turn-ins reported yet.'
    else message=count..' outcomes - page '..page..'/'..pages end
    if model and model.missed>0 then message=message..' '..model.missed..' older records expired before receipt.' end
    status:SetText(message)
    for i,row in ipairs(rows) do
        local entry=model and model.entries[count-(page-1)*5-i+1]
        row:SetText(entry and ('Seen '..age(entry.receivedAt)..' - '..entry.text) or '')
    end
    if page>1 then previous:Enable() else previous:Disable() end
    if page<pages then following:Enable() else following:Disable() end
    dirty=false
end
-- Receiving data only marks this popup dirty. Rendering never runs inside the
-- control driver's OnUpdate, so a popup error cannot stop control/status polls.
function WoweeBotHistoryChanged(history,inWorld)
    if model~=history then page=1 end
    model,online,dirty=history,inWorld,true
end
local function toggle()
    if frame:IsShown() then frame:Hide() else frame:Show();render() end
end
local launcher=CreateFrame('Button','WoweeBotHistoryLauncher',UIParent,'UIPanelButtonTemplate')
launcher:SetSize(130,22);launcher:SetPoint('BOTTOM',UIParent,'BOTTOM',258,110)
launcher:SetText('Recent activity');launcher:SetScript('OnClick',toggle)
frame:SetScript('OnUpdate',function()
    local now=GetTime()
    if dirty or now>=nextDraw then render();nextDraw=now+1 end
end)
SLASH_WOWEEBOTHISTORY1='/bothistory'
SlashCmdList.WOWEEBOTHISTORY=function() frame:Show();render() end
frame:Hide()
