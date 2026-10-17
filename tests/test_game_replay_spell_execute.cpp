// SMSG_SPELLLOGEXECUTE through GameHandler's own dispatch, built as
// AzerothCore's Spell::SendLogExecute (WotLK) and cmangos's SpellLog (classic
// and TBC) write it: the caster packed everywhere, a target packed on TBC and
// WotLK and whole on classic. See game_replay.hpp.
#include <catch_amalgamated.hpp>

#include "game_replay.hpp"
#include "addons/addon_manager.hpp"
#include "game/spell_handler.hpp"

#include <algorithm>
#include <chrono>
#include <string>

using replay::Session;

namespace {

using wowee::game::CombatTextEntry;
using wowee::game::Opcode;
using wowee::game::wireOpcode;
using wowee::network::Packet;

constexpr uint64_t kPlayer = Session::kPlayerGuid;
constexpr uint64_t kMob = 0xF130000000000123ull;

// The servers' SpellEffects numbers.
constexpr uint32_t kPowerDrain = 8;
constexpr uint32_t kPowerBurn = 62;
constexpr uint32_t kInterruptCast = 68;
constexpr uint32_t kFeedPet = 101;

Packet executeLog(uint64_t caster, uint32_t spellId, uint32_t effect) {
    Packet p(wireOpcode(Opcode::SMSG_SPELLLOGEXECUTE));
    p.writePackedGuid(caster);
    p.writeUInt32(spellId);
    p.writeUInt32(1);
    p.writeUInt32(effect);
    p.writeUInt32(1);
    return p;
}

void writeTarget(Packet& p, bool classic, uint64_t guid) {
    if (classic) p.writeUInt64(guid);
    else p.writePackedGuid(guid);
}

// Spell::ExecuteLogEffectTakeTargetPower.
Packet takePower(bool classic, uint32_t effect) {
    Packet p = executeLog(kPlayer, 5138, effect);
    writeTarget(p, classic, kMob);
    p.writeUInt32(250);
    p.writeUInt32(0);
    p.writeFloat(effect == kPowerDrain ? 1.0f : 0.0f);
    return p;
}

bool hasText(Session& s, CombatTextEntry::Type type, uint32_t spellId) {
    const auto& texts = s.handler().getCombatText();
    return std::any_of(texts.begin(), texts.end(), [&](const CombatTextEntry& e) {
        return e.type == type && e.spellId == spellId;
    });
}

bool chatHas(Session& s, const std::string& text) {
    const auto& history = s.handler().getChatHistory();
    return std::any_of(history.begin(), history.end(), [&](const auto& m) {
        return m.message.find(text) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("an interrupt in the execute log is effect 68", "[replay][execute]") {
    for (const char* expansion : {"wotlk", "tbc", "classic"}) {
        CAPTURE(expansion);
        Session s(expansion);
        // On the target, where the panel shows spell mechanics by default.
        s.handler().setTargetGuidRaw(kMob);
        Packet p = executeLog(kPlayer, 2139, kInterruptCast);
        writeTarget(p, std::string(expansion) == "classic", kMob);
        p.writeUInt32(133);
        s.receive(p);
        CHECK(hasText(s, CombatTextEntry::INTERRUPT, 133));
    }
}

TEST_CASE("a drain is effect 8 and a burn is effect 62", "[replay][execute]") {
    for (const char* expansion : {"wotlk", "tbc", "classic"}) {
        for (uint32_t effect : {kPowerDrain, kPowerBurn}) {
            CAPTURE(expansion, effect);
            Session s(expansion);
            s.receive(takePower(std::string(expansion) == "classic", effect));
            const auto& texts = s.handler().getCombatText();
            const auto drain = std::find_if(texts.begin(), texts.end(), [](const auto& e) {
                return e.type == CombatTextEntry::POWER_DRAIN;
            });
            REQUIRE(drain != texts.end());
            CHECK(drain->spellId == 5138);
            CHECK(drain->amount == 250);
        }
    }
}

TEST_CASE("feeding a pet is effect 101, with a packed caster on TBC too", "[replay][execute]") {
    for (const char* expansion : {"wotlk", "tbc"}) {
        CAPTURE(expansion);
        Session s(expansion);
        Packet p = executeLog(kPlayer, 6991, kFeedPet);
        p.writeUInt32(4540);
        s.receive(p);
        CHECK(chatHas(s, "You feed your pet"));
    }
}

TEST_CASE("TBC buff slots hide empty effects and retain real aura identities", "[replay][buffframe]") {
    Session s("tbc");
    auto* spells = s.handler().getSpellHandler();
    REQUIRE(spells != nullptr);
    spells->resetPlayerAuras(24);
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua != nullptr);
    std::vector<std::string> errors;
    lua->setLuaErrorCallback([&](const auto& error) { errors.push_back(error); });
    REQUIRE(lua->executeString("__WoweeInterfaceVersion=20400"));
    // TBC BuffFrame walks one-based indices and draws every nonzero result.
    CHECK(lua->evaluateBoolean("(function() for i=1,32 do if GetPlayerBuff(i,'HELPFUL') ~= 0 then return false end end for i=1,16 do if GetPlayerBuff(i,'HARMFUL') ~= 0 then return false end end return true end)()"));
    CHECK(lua->evaluateBoolean("GetPlayerBuffTimeLeft(0) == 0 and GetPlayerBuffTexture(0) == nil"));

    auto& helpful = spells->getPlayerAuraSlotRef(0);
    helpful.spellId = 133;
    helpful.charges = 2;
    helpful.durationMs = helpful.maxDurationMs = 60000;
    helpful.receivedAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    auto& harmful = spells->getPlayerAuraSlotRef(7);
    harmful.spellId = 116;
    harmful.flags = 0x80;
    harmful.charges = 3;
    CHECK(lua->evaluateBoolean("GetPlayerBuff(1,'HELPFUL') == 1 and GetPlayerBuff(2,'HELPFUL') == 0"));
    CHECK(lua->evaluateBoolean("GetPlayerBuff(1,'HARMFUL') == 8 and GetPlayerBuff(2,'HARMFUL') == 0"));
    CHECK(lua->evaluateBoolean("select(2,GetPlayerBuff(1,'HELPFUL')) == 0 and select(2,GetPlayerBuff(1,'HARMFUL')) == 1"));
    CHECK(lua->evaluateBoolean("GetPlayerBuff(0,'HELPFUL') == 0"));
    CHECK(lua->evaluateBoolean("GetPlayerBuffApplications(1) == 2 and GetPlayerBuffApplications(8) == 3 and GetPlayerBuffApplications(0) == 0"));
    CHECK(lua->evaluateBoolean("GetPlayerBuffTimeLeft(1) > 0 and GetPlayerBuffTimeLeft(1) <= 60"));
    REQUIRE(lua->executeString("buffTip=CreateFrame('GameTooltip','BuffIndexTooltip',UIParent); buffTip:SetPlayerBuff(GetPlayerBuff(1,'HELPFUL'))"));
    CHECK(lua->evaluateBoolean("buffTip.__spellId == 133"));
    REQUIRE(lua->executeString("buffTip:SetPlayerBuff(GetPlayerBuff(1,'HARMFUL'))"));
    CHECK(lua->evaluateBoolean("buffTip.__spellId == 116"));
    REQUIRE(lua->executeString("buffTip:SetPlayerBuff(0); CancelPlayerBuff(0)"));
    CHECK(lua->evaluateBoolean("buffTip.__spellId == nil"));
    CHECK(s.sent().of(Opcode::CMSG_CANCEL_AURA).empty());
    REQUIRE(lua->executeString("CancelPlayerBuff(1)"));
    auto cancelled = s.sent().of(Opcode::CMSG_CANCEL_AURA);
    REQUIRE(cancelled.size() == 1);
    CHECK(cancelled.front().readUInt32() == 133);

    // The same client still supports a vanilla interface's zero/-1 contract.
    REQUIRE(lua->executeString("__WoweeInterfaceVersion=11200"));
    CHECK(lua->evaluateBoolean("GetPlayerBuff(0,'HELPFUL') == 0 and GetPlayerBuff(1,'HELPFUL') == -1"));
    CHECK(lua->evaluateBoolean("GetPlayerBuff(0,'HARMFUL') == 7 and GetPlayerBuff(1,'HARMFUL') == -1"));
    CHECK(lua->evaluateBoolean("GetPlayerBuffApplications(0) == 2 and GetPlayerBuffApplications(7) == 3"));
    REQUIRE(lua->executeString("buffTip:SetPlayerBuff(GetPlayerBuff(0,'HELPFUL'))"));
    CHECK(lua->evaluateBoolean("buffTip.__spellId == 133"));
    helpful = {};
    harmful = {};
    REQUIRE(lua->executeString("__WoweeInterfaceVersion=20400"));
    CHECK(lua->evaluateBoolean("GetPlayerBuff(1,'HELPFUL') == 0 and GetPlayerBuff(1,'HARMFUL') == 0"));
    CAPTURE(errors);
    CHECK(errors.empty());
}
