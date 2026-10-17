-- Real frame + correlated snapshots: local maintenance may change without
-- changing the retained planner destination. Selection is never a receipt.
do
    local oldClock, oldUnit, oldSend = GetTime, UnitName, SendChatMessage
    local clock, sent = oldClock(), {}
    GetTime = function() return clock end
    UnitName = function(unit) if unit == 'player' then return 'Jizzle' end return oldUnit(unit) end
    SendChatMessage = function(command) sent[#sent + 1] = command end
    local event = WoweeBotController:GetScript('OnEvent')
    local tick = function() WoweeBotController:GetScript('OnUpdate')() end
    local function receive(prefix, text)
        event(WoweeBotController, 'CHAT_MSG_ADDON', prefix, text, 'WHISPER', 'Jizzle')
    end
    local function hex(text)
        return text == '' and '-' or text:gsub('.', function(c) return string.format('%02x', c:byte()) end)
    end
    local function lastId(verb)
        for i = #sent, 1, -1 do
            local id = sent[i]:match('^%.bot idle ([%w_-]+) ' .. verb)
            if id then return id end
        end
        error('Missing ' .. verb .. ' request')
    end
    event(WoweeBotController, 'PLAYER_ENTERING_WORLD')
    tick()
    local id = lastId('status')
    receive('WIDLE', '1\t' .. id .. '\tok\tbot\t13\t0\tnone')
    receive('WIDLECAP', '1\t' .. id .. '\t13\tactivity_1')
    local function snapshot(npc, task, executed, state)
        clock = clock + 8
        tick()
        receive('WIDLE', '1\t' .. lastId('status') .. '\tok\tbot\t13\t0\tnone')
        tick()
        local request = lastId('snapshot')
        local body = table.concat({'bot', state or 'non_combat', 'trainer', 'traveling', '0', '-1',
            'none', '1651', '80', hex('Lee Brown'), '-', hex('Goldshire'), 'npc', '789',
            hex(npc), hex(task), hex(executed), '0', '0'}, '\t')
        local count = math.ceil(#body / 160)
        for i = 1, count do
            receive('WIDLEACT', '1\t' .. request .. '\t13\t' .. i .. '\t' .. count .. '\t' ..
                body:sub((i - 1) * 160 + 1, i * 160))
        end
        assert(WoweeBotStatusGoal:GetText() == 'Travel plan: Train at Lee Brown')
    end
    snapshot('Bag vendor', 'rpg buy', '')
    assert(WoweeBotStatusInteraction:GetText() == 'Selected task: Buy items - Bag vendor')
    assert(WoweeBotStatusAction:GetText() == '') -- intent is not a completed purchase
    snapshot('Quest giver', 'rpg end quest', 'rpg buy')
    assert(WoweeBotStatusInteraction:GetText() == 'Selected task: Turn in quests - Quest giver')
    assert(WoweeBotStatusAction:GetText() == 'Last reported action: Buy items')
    snapshot('Warlock trainer', 'rpg', '')
    assert(WoweeBotStatusInteraction:GetText() == 'Selected target: Warlock trainer (task not reported)')
    snapshot('Armor vendor', 'rpg buy', 'rpg buy')
    assert(WoweeBotStatusInteraction:GetText() == 'Selected task: Buy items - Armor vendor')
    snapshot('Armor vendor', 'rpg buy', '', 'combat')
    assert(WoweeBotStatusInteraction:GetText() == 'Now: In combat')
    for i = 1, 8 do tick() end
    assert(type(WoweeBotController:GetScript('OnUpdate')) == 'function')
    GetTime, UnitName, SendChatMessage = oldClock, oldUnit, oldSend
    event(WoweeBotController, 'PLAYER_ENTERING_WORLD')
end
print('Retained travel plan and changing maintenance UI scenarios passed')
