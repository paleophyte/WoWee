// Quest progress through GameHandler's own dispatch. See game_replay.hpp.
#include <catch_amalgamated.hpp>

#include "game_replay.hpp"
#include "game/quest_handler.hpp"

#include <algorithm>
#include <string>
#include <vector>

using replay::Session;

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
