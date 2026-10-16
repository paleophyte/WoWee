// Dispel, spellsteal and instakill logs through GameHandler's own dispatch,
// built as the servers write them: AzerothCore for WotLK, cmangos for classic
// and TBC. See game_replay.hpp.
#include <catch_amalgamated.hpp>

#include "game_replay.hpp"

#include <algorithm>
#include <string>

using replay::Session;

namespace {

using wowee::game::Opcode;
using wowee::game::wireOpcode;
using wowee::network::Packet;

constexpr uint64_t kPlayer = Session::kPlayerGuid;
constexpr uint64_t kMob = 0xF130000000000123ull;
constexpr uint32_t kDispelMagic = 527;
constexpr uint32_t kPowerWordFortitude = 1243;

bool chatHas(Session& s, const std::string& text) {
    const auto& history = s.handler().getChatHistory();
    return std::any_of(history.begin(), history.end(), [&](const auto& m) {
        return m.message.find(text) != std::string::npos;
    });
}

// Spell::EffectDispel: victim, then caster. Classic has neither the dispel
// spell nor the byte after each dispelled one.
Packet dispelLog(bool classic, uint64_t victim, uint64_t caster) {
    Packet p(wireOpcode(Opcode::SMSG_SPELLDISPELLOG));
    p.writePackedGuid(victim);
    p.writePackedGuid(caster);
    if (!classic) {
        p.writeUInt32(kDispelMagic);
        p.writeUInt8(0);
    }
    p.writeUInt32(1);
    p.writeUInt32(kPowerWordFortitude);
    if (!classic) p.writeUInt8(0);
    return p;
}

// Spell::EffectInstaKill. Classic sends the victim alone.
Packet instaKillLog(bool classic, uint64_t caster, uint64_t victim) {
    Packet p(wireOpcode(Opcode::SMSG_SPELLINSTAKILLLOG));
    if (!classic) p.writeUInt64(caster);
    p.writeUInt64(victim);
    p.writeUInt32(5);
    return p;
}

Packet dispelFailed(bool classic, uint64_t caster, uint64_t victim) {
    Packet p(wireOpcode(Opcode::SMSG_DISPEL_FAILED));
    p.writeUInt64(caster);
    p.writeUInt64(victim);
    if (!classic) p.writeUInt32(kDispelMagic);
    p.writeUInt32(kPowerWordFortitude);
    return p;
}

}  // namespace

TEST_CASE("a dispel log names its victim before its caster", "[replay][dispel]") {
    for (const char* expansion : {"wotlk", "tbc", "classic"}) {
        CAPTURE(expansion);
        const bool classic = std::string(expansion) == "classic";

        {
            Session s(expansion);
            s.receive(dispelLog(classic, kPlayer, kMob));
            CHECK(chatHas(s, "spell 1243 was dispelled."));
            CHECK_FALSE(chatHas(s, "You dispel"));
        }
        {
            Session s(expansion);
            s.receive(dispelLog(classic, kMob, kPlayer));
            CHECK(chatHas(s, "You dispel spell 1243."));
        }
    }
}

TEST_CASE("a spellsteal log has packed guids on TBC", "[replay][dispel]") {
    Session s("tbc");
    Packet p(wireOpcode(Opcode::SMSG_SPELLSTEALLOG));
    p.writePackedGuid(kMob);
    p.writePackedGuid(kPlayer);
    p.writeUInt32(30449);
    p.writeUInt8(0);
    p.writeUInt32(1);
    p.writeUInt32(kPowerWordFortitude);
    p.writeUInt8(0);
    s.receive(p);
    CHECK(chatHas(s, "You stole spell 1243."));
}

TEST_CASE("an instakill log has whole guids", "[replay][instakill]") {
    for (const char* expansion : {"wotlk", "tbc", "classic"}) {
        CAPTURE(expansion);
        Session s(expansion);
        s.receive(instaKillLog(std::string(expansion) == "classic", kMob, kPlayer));
        CHECK(chatHas(s, "You were killed by an instant-kill effect."));
    }
}

TEST_CASE("a failed dispel has whole guids, and classic has no dispel spell", "[replay][dispel]") {
    SECTION("wotlk") {
        Session s;
        s.receive(dispelFailed(false, kPlayer, kMob));
        CHECK(chatHas(s, "Dispel failed! (spell 527)"));
    }
    SECTION("classic") {
        Session s("classic");
        s.receive(dispelFailed(true, kPlayer, kMob));
        CHECK(chatHas(s, "Dispel failed!"));
        CHECK_FALSE(chatHas(s, "(spell"));
    }
}
