// Quest progress through GameHandler's own dispatch. See game_replay.hpp.
#include <catch_amalgamated.hpp>

#include "game_replay.hpp"
#include "game/quest_handler.hpp"
#include "game/quest_objectives.hpp"
#include "game/entity.hpp"
#include "addons/addon_manager.hpp"

#include <algorithm>
#include <string>
#include <vector>

using replay::Session;

TEST_CASE("accepted quest copilot view preserves stock selection and reconciles objectives",
          "[replay][quest][accepted-quests][addon]") {
    // Synthetic local cache entries test the UI/API, not earned gameplay.
    Session s("tbc");
    auto* quests = s.handler().getQuestHandler();
    wowee::game::QuestHandler::QuestLogEntry kill;
    kill.questId = 15; kill.title = "Kill test"; kill.level = 3; kill.zoneOrSort = 12;
    kill.killObjectives[0] = {299, 10}; kill.killCounts[299] = {3, 10};
    wowee::game::QuestHandler::QuestLogEntry collection;
    collection.questId = 5545; collection.title = "Collection test"; collection.level = 9;
    collection.zoneOrSort = 12; collection.complete = true;
    collection.itemObjectives[0] = {49001, 8}; collection.itemCounts[49001] = 7;
    quests->questLogRef() = {kill, collection};
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua);
    REQUIRE(lua->executeString(R"(
        CollapseQuestHeader(0)
        oldSelection=GetQuestLogSelection()
        accepted=WoweeGetAcceptedQuests()
    )"));
    CHECK(lua->evaluateBoolean("select(1,GetNumQuestLogEntries())==1 and #accepted==2"));
    CHECK(lua->evaluateBoolean("accepted[1].questId==15 and accepted[1].objectives[1].text:find('3/10',1,true)~=nil"));
    CHECK(lua->evaluateBoolean("accepted[2].complete and not accepted[2].failed and not accepted[2].objectives[1].finished"));
    CHECK(lua->evaluateBoolean("GetQuestLogSelection()==oldSelection"));

    lua->widgets().noteScreenSize(1920.0f, 1080.0f);
    std::vector<std::string> errors;
    lua->setLuaErrorCallback([&](const auto& message) { errors.push_back(message); });
    REQUIRE(lua->executeString("testNow=0; GetTime=function() return testNow end"));
    REQUIRE(lua->executeFile(std::string(WOWEE_SOURCE_DIR)+"/addons/WoweeBotStatus/QuestsUI.lua"));
    lua->fireEvent("PLAYER_ENTERING_WORLD");
    REQUIRE(lua->executeString("SlashCmdList.WOWEEBOTQUESTS()"));
    lua->widgets().layout(1920.0f, 1080.0f); // Resolve the shown popup's visibility before OnUpdate.
    CHECK(lua->evaluateBoolean("WoweeBotQuestsFrame:IsShown() and WoweeBotQuestsStatus:GetText()=='2 accepted - page 1/1'"));
    REQUIRE(lua->executeString("WoweeBotQuestRow2:GetScript('OnClick')()"));
    CHECK(lua->evaluateBoolean("WoweeBotQuestsDetail:GetText():find('Server marked complete; checking objective progress',1,true)~=nil"));
    quests->questLogRef()[1].itemCounts[49001] = 8;
    lua->fireEvent("QUEST_LOG_UPDATE"); lua->dispatchOnUpdate(0.25f);
    CHECK(lua->evaluateBoolean("WoweeBotQuestsDetail:GetText():find('Ready to turn in',1,true)~=nil"));
    CHECK(lua->evaluateBoolean("WoweeBotQuestsDetail:GetText():find('8/8',1,true)~=nil"));
    CHECK(lua->evaluateBoolean("not accepted[2].objectives[1].finished")); // prior snapshot is immutable
    quests->questLogRef()[1].failed = true;
    lua->fireEvent("QUEST_LOG_UPDATE"); lua->dispatchOnUpdate(0.25f);
    CHECK(lua->evaluateBoolean("WoweeBotQuestsDetail:GetText():find('Failed',1,true)~=nil"));
    quests->questLogRef().erase(quests->questLogRef().begin()+1);
    lua->fireEvent("QUEST_LOG_UPDATE"); lua->dispatchOnUpdate(0.25f);
    CHECK(lua->evaluateBoolean("WoweeBotQuestsDetail:GetText():find('Kill test',1,true)~=nil and not WoweeBotQuestRow2:IsShown()"));
    CHECK(lua->evaluateBoolean("GetQuestLogSelection()==oldSelection and select(1,GetNumQuestLogEntries())==1"));
    lua->fireEvent("PLAYER_LEAVING_WORLD"); lua->dispatchOnUpdate(0.25f);
    CHECK(lua->evaluateBoolean("WoweeBotQuestsStatus:GetText()=='Log in to view accepted quests.' and WoweeBotQuestsDetail:GetText()==''"));
    for (uint32_t i = 1; i <= 5; ++i) {
        auto extra = kill; extra.questId = 1000+i; extra.title = "Extra "+std::to_string(i);
        quests->questLogRef().push_back(extra);
    }
    lua->fireEvent("PLAYER_ENTERING_WORLD"); lua->dispatchOnUpdate(0.25f);
    REQUIRE(lua->executeString("WoweeBotQuestsNext:GetScript('OnClick')()"));
    CHECK(lua->evaluateBoolean("WoweeBotQuestsStatus:GetText()=='6 accepted - page 2/2' and WoweeBotQuestsDetail:GetText():find('Extra 5',1,true)~=nil"));
    quests->questLogRef().resize(1);
    lua->fireEvent("QUEST_LOG_UPDATE"); lua->dispatchOnUpdate(0.25f);
    CHECK(lua->evaluateBoolean("WoweeBotQuestsStatus:GetText()=='1 accepted - page 1/1' and WoweeBotQuestsDetail:GetText():find('Kill test',1,true)~=nil"));
    REQUIRE(lua->executeString("savedQuestReader=WoweeGetAcceptedQuests; WoweeGetAcceptedQuests=nil"));
    lua->fireEvent("QUEST_LOG_UPDATE"); lua->dispatchOnUpdate(0.25f);
    CHECK(lua->evaluateBoolean("WoweeBotQuestsStatus:GetText():find('Quest data unavailable',1,true)~=nil and WoweeBotQuestsDetail:GetText()==''"));
    REQUIRE(lua->executeString("WoweeGetAcceptedQuests=savedQuestReader"));
    CHECK(s.sent().of(wowee::game::Opcode::CMSG_MESSAGECHAT).empty());
    CHECK(s.sent().of(wowee::game::Opcode::CMSG_QUESTGIVER_ACCEPT_QUEST).empty());
    CHECK(errors.empty());
    lua->setLuaErrorCallback({});
}

namespace {

using wowee::game::Opcode;
using wowee::game::wireOpcode;
using wowee::network::Packet;

constexpr uint32_t kQuest = 33;
constexpr uint32_t kYoungWolf = 299;
constexpr uint32_t kAltar = 1234;

struct InfoMessages {
    std::vector<std::string> lines;
    explicit InfoMessages(Session& s) {
        s.handler().setAddonEventCallback(
            [this](const std::string& name, const std::vector<std::string>& args) {
                if (name == "UI_INFO_MESSAGE" && !args.empty()) lines.push_back(args[0]);
            });
    }
};

void acceptQuest(Session& s) {
    s.handler().getQuestHandler()->addQuestToLocalLogIfMissing(kQuest, "Wolves Across the Border", "");
}

// As AzerothCore's QueryHandler writes it.
Packet creatureName(uint32_t entry, const std::string& name) {
    Packet p(wireOpcode(Opcode::SMSG_CREATURE_QUERY_RESPONSE));
    p.writeUInt32(entry);
    p.writeString(name);
    for (int i = 0; i < 5; ++i) p.writeString("");
    for (int i = 0; i < 4 + 2 + 4; ++i) p.writeUInt32(0);  // type to rank, kill credits, models
    p.writeFloat(1.0f);
    p.writeFloat(1.0f);
    p.writeUInt8(0);
    for (int i = 0; i < 6 + 1; ++i) p.writeUInt32(0);  // quest items, movement id
    return p;
}

Packet gameObjectName(uint32_t entry, const std::string& name) {
    Packet p(wireOpcode(Opcode::SMSG_GAMEOBJECT_QUERY_RESPONSE));
    p.writeUInt32(entry);
    p.writeUInt32(10);  // GAMEOBJECT_TYPE_GOOBER
    p.writeUInt32(1);
    p.writeString(name);
    for (int i = 0; i < 6; ++i) p.writeString("");
    for (int i = 0; i < 24; ++i) p.writeUInt32(0);
    p.writeFloat(1.0f);
    for (int i = 0; i < 6; ++i) p.writeUInt32(0);
    return p;
}

// Player::SendQuestUpdateAddCreatureOrGo: a game object's entry has the high
// bit set.
Packet addKill(uint32_t wireEntry, uint32_t count, uint32_t required) {
    Packet p(wireOpcode(Opcode::SMSG_QUESTUPDATE_ADD_KILL));
    p.writeUInt32(kQuest);
    p.writeUInt32(wireEntry);
    p.writeUInt32(count);
    p.writeUInt32(required);
    p.writeUInt64(0xF130000000000123ull);
    return p;
}

bool chatMentions(Session& s, const std::string& text) {
    const auto& history = s.handler().getChatHistory();
    return std::any_of(history.begin(), history.end(), [&](const auto& m) {
        return m.message.find(text) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("a quest kill is shown as ERR_QUEST_ADD_KILL_SII", "[replay][quest]") {
    Session s;
    InfoMessages info(s);
    acceptQuest(s);
    s.receive(creatureName(kYoungWolf, "Young Wolf"));
    s.receive(addKill(kYoungWolf, 3, 10));
    CHECK(info.lines == std::vector<std::string>{"Young Wolf slain: 3/10"});
    CHECK_FALSE(chatMentions(s, "3/10"));
}

TEST_CASE("a quest object is named from the object cache, as ERR_QUEST_ADD_FOUND_SII",
          "[replay][quest]") {
    Session s;
    InfoMessages info(s);
    acceptQuest(s);
    s.receive(creatureName(kAltar, "Kobold Vermin"));
    s.receive(gameObjectName(kAltar, "Altar of Zul"));
    s.receive(addKill(kAltar | 0x80000000u, 1, 1));
    CHECK(info.lines == std::vector<std::string>{"Altar of Zul: 1/1"});
}

TEST_CASE("a quest player kill is shown as ERR_QUEST_ADD_PLAYER_KILL_SII", "[replay][quest]") {
    Session s;
    InfoMessages info(s);
    acceptQuest(s);
    Packet p(wireOpcode(Opcode::SMSG_QUESTUPDATE_ADD_PVP_KILL));
    p.writeUInt32(kQuest);
    p.writeUInt32(2);
    p.writeUInt32(5);
    s.receive(p);
    CHECK(info.lines == std::vector<std::string>{"Players slain: 2/5"});
    CHECK_FALSE(chatMentions(s, "2/5"));
}

TEST_CASE("TBC server-assigned quests appear and disappear without local dialog clicks",
          "[replay][quest][playerbot]") {
    Session s("tbc");
    s.handler().getEntityManager().addEntity(Session::kPlayerGuid,
        std::make_shared<wowee::game::Player>(Session::kPlayerGuid));
    constexpr uint32_t questId = 783;
    s.receive(replay::UpdateObject().values(Session::kPlayerGuid,
        {{wowee::game::UF::PLAYER_QUEST_LOG_START, questId}}).packet(true));
    REQUIRE(s.handler().getQuestLog().size() == 1);
    CHECK(s.handler().getQuestLog()[0].questId == questId);
    const auto queries = s.sent().of(Opcode::CMSG_QUEST_QUERY).size();
    CHECK(queries == 1);

    // A partial health update must retain the quest and not repeatedly query it.
    s.receive(replay::UpdateObject().values(Session::kPlayerGuid,
        {{wowee::game::UF::UNIT_FIELD_HEALTH, 100}}).packet(true));
    CHECK(s.handler().getQuestLog().size() == 1);
    CHECK(s.sent().of(Opcode::CMSG_QUEST_QUERY).size() == queries);

    s.receive(replay::UpdateObject().values(Session::kPlayerGuid,
        {{wowee::game::UF::PLAYER_QUEST_LOG_START, 0}}).packet(true));
    CHECK(s.handler().getQuestLog().empty());
}

TEST_CASE("completed item quests reconcile later inventory updates", "[replay][quest][quest-item-order]") {
    Session s("tbc");
    auto* handler = s.handler().getQuestHandler();
    handler->addQuestToLocalLogIfMissing(6, "Bounty on Garrick Padfoot", "");
    REQUIRE(handler->getQuestLog().size() == 1);
    // Metadata fixture; completion itself comes through the real packet handler.
    auto& quest = const_cast<wowee::game::QuestHandler::QuestLogEntry&>(handler->getQuestLog()[0]);
    quest.itemObjectives[0] = {182, 1};
    quest.requiredItemCounts[182] = 1;
    quest.itemCounts[182] = 0;
    Packet complete(wireOpcode(Opcode::SMSG_QUESTUPDATE_COMPLETE));
    complete.writeUInt32(6);
    s.receive(complete);
    REQUIRE(quest.complete);
    CHECK(quest.itemCounts.at(182) == 0);
    wowee::game::ItemDef head;
    head.itemId = 182; head.name = "Garrick's Head"; head.stackCount = 1;
    REQUIRE(s.handler().getInventory().setBackpackSlot(0, head));
    const auto held = s.handler().getInventory().countItem(182);
    REQUIRE(held == 1);
    s.handler().reconcileQuestItemObjectives({{182, held}});
    CHECK(quest.complete);
    CHECK(quest.itemCounts.at(182) == 1);
    const auto lines = wowee::game::questObjectives(s.handler(), quest);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].finished);
    CHECK(lines[0].text.ends_with("1/1"));
    // A later removal is reflected too; a ready flag never invents possession.
    REQUIRE(s.handler().getInventory().clearBackpackSlot(0));
    s.handler().reconcileQuestItemObjectives({});
    CHECK(quest.itemCounts.at(182) == 0);
    CHECK_FALSE(wowee::game::questObjectives(s.handler(), quest)[0].finished);
}

TEST_CASE("inventory reconciliation clamps counts without completing unrelated objectives", "[replay][quest][quest-item-order]") {
    Session s("tbc");
    auto* handler = s.handler().getQuestHandler();
    handler->addQuestToLocalLogIfMissing(33, "Collection", "");
    auto& quest = const_cast<wowee::game::QuestHandler::QuestLogEntry&>(handler->getQuestLog()[0]);
    quest.itemObjectives[0] = {2934, 3};
    quest.itemObjectives[1] = {2318, 2};
    s.handler().reconcileQuestItemObjectives({{2934, 20}, {999, 5}});
    CHECK(quest.itemCounts.at(2934) == 3);
    CHECK(quest.itemCounts.at(2318) == 0);
    CHECK_FALSE(quest.complete);
    const auto lines = wowee::game::questObjectives(s.handler(), quest);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].finished);
    CHECK_FALSE(lines[1].finished);
}
