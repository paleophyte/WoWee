// SMSG_ATTACKERSTATEUPDATE as classic and TBC send it.
//
// The head is the same in both: hit flags, two packed guids, the total, and a
// run of sub-damages. The tails are not, and the difference is not cosmetic:
//
//   classic  victimState, overkill, then blocked if the packet carries it
//   TBC      victimState, an unknown field, a spell id, blocked, no overkill
//
// So the head is worth sharing and the tails are not, and this pins both
// before either is touched. A field read one step out of place here does not
// raise; it shows up as a damage number that is wrong, which is the sort of
// thing that gets blamed on the server.
//
// This is a regression net rather than an outside oracle: the bytes are laid
// out from what these readers expect.
#include <catch_amalgamated.hpp>

#include <cstring>
#include <vector>

#include "core/application.hpp"
#include "game/packet_parsers.hpp"

namespace wowee {
namespace core {
Application* Application::instance = nullptr;
}
}

using namespace wowee::game;

namespace {

struct Writer {
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (i * 8)));
    }
    void f32(float v) {
        uint32_t raw;
        std::memcpy(&raw, &v, 4);
        u32(raw);
    }
    /// A guid in the packed form every expansion uses: a mask of the non-zero
    /// bytes, then those bytes.
    void packedGuid(uint64_t guid) {
        uint8_t mask = 0;
        uint8_t out[8];
        int n = 0;
        for (int i = 0; i < 8; ++i) {
            const uint8_t b = static_cast<uint8_t>((guid >> (i * 8)) & 0xFF);
            if (b) { mask |= static_cast<uint8_t>(1u << i); out[n++] = b; }
        }
        u8(mask);
        for (int i = 0; i < n; ++i) u8(out[i]);
    }
};

/// The head both expansions share, with two sub-damage entries.
void writeHead(Writer& w) {
    w.u32(0x00000002);            // hitInfo
    w.packedGuid(0x0102030405ull);  // attacker
    w.packedGuid(0x1112131415ull);  // target
    w.u32(1234);                  // totalDamage
    w.u8(2);                      // subDamageCount
    for (int i = 0; i < 2; ++i) {
        w.u32(static_cast<uint32_t>(1 << i));  // schoolMask
        w.f32(100.0f + i);                     // damage
        w.u32(600 + i);                        // intDamage
        w.u32(10 + i);                         // absorbed
        w.u32(20 + i);                         // resisted
    }
}

void checkHead(const AttackerStateUpdateData& data) {
    CHECK(data.hitInfo == 0x00000002u);
    CHECK(data.attackerGuid == 0x0102030405ull);
    CHECK(data.targetGuid == 0x1112131415ull);
    CHECK(data.totalDamage == 1234);
    REQUIRE(data.subDamages.size() == 2);
    CHECK(data.subDamages[0].schoolMask == 1u);
    CHECK(data.subDamages[0].intDamage == 600u);
    CHECK(data.subDamages[1].schoolMask == 2u);
    CHECK(data.subDamages[1].resisted == 21u);
    CHECK(data.subDamageCount == 2);
}

}  // namespace

TEST_CASE("classic attacker state: victim state then overkill", "[attackerstate]") {
    Writer w;
    writeHead(w);
    w.u32(1);      // victimState
    w.u32(77);     // overkill
    w.u32(55);     // blocked, which classic reads only when it is there
    wowee::network::Packet packet(0, w.bytes);

    ClassicPacketParsers parsers;
    AttackerStateUpdateData data;
    REQUIRE(parsers.parseAttackerStateUpdate(packet, data));
    checkHead(data);
    CHECK(data.victimState == 1u);
    CHECK(data.overkill == 77);
    CHECK(data.blocked == 55u);
}

TEST_CASE("classic attacker state: blocked is optional", "[attackerstate]") {
    Writer w;
    writeHead(w);
    w.u32(1);      // victimState
    w.u32(77);     // overkill
    wowee::network::Packet packet(0, w.bytes);

    ClassicPacketParsers parsers;
    AttackerStateUpdateData data;
    REQUIRE(parsers.parseAttackerStateUpdate(packet, data));
    CHECK(data.overkill == 77);
}

TEST_CASE("TBC attacker state: an unknown and a spell id, and no overkill",
          "[attackerstate]") {
    Writer w;
    writeHead(w);
    w.u32(1);      // victimState
    w.u32(0);      // unknown, commonly 0
    w.u32(0);      // spell id for some melee specials
    w.u32(55);     // blocked
    wowee::network::Packet packet(0, w.bytes);

    TbcPacketParsers parsers;
    AttackerStateUpdateData data;
    REQUIRE(parsers.parseAttackerStateUpdate(packet, data));
    checkHead(data);
    CHECK(data.victimState == 1u);
    CHECK(data.blocked == 55u);
    // No overkill on the wire here, and the reader says so rather than
    // leaving whatever the previous field held.
    CHECK(data.overkill == -1);
}

TEST_CASE("a truncated packet is refused and the read position restored",
          "[attackerstate]") {
    Writer w;
    writeHead(w);
    w.u32(1);      // victimState, then nothing
    wowee::network::Packet packet(0, w.bytes);
    const size_t before = packet.getReadPos();

    TbcPacketParsers parsers;
    AttackerStateUpdateData data;
    CHECK_FALSE(parsers.parseAttackerStateUpdate(packet, data));
    // Rewound, so the caller can try another reader against the same bytes.
    CHECK(packet.getReadPos() == before);
}

// WotLK, laid out as AzerothCore's Unit::SendAttackStateUpdate writes it:
// sub-damages of school, float and integer damage only; then the absorbs and
// the resists as runs of their own when the hit flags say so; then the victim
// state as one byte, an unknown, a melee spell id, and blocked on a block.
namespace {

constexpr uint32_t kAffectsVictim = 0x2, kPartialAbsorb = 0x40, kPartialResist = 0x100;
constexpr uint32_t kFullAbsorb = 0x20, kBlock = 0x2000, kGlancing = 0x10000, kCrushing = 0x20000;
constexpr uint32_t kRageGain = 0x800000;

std::vector<uint8_t> wotlkHit(uint32_t hitInfo, uint8_t victimState, uint32_t blocked) {
    Writer w;
    w.u32(hitInfo);
    w.packedGuid(0x0102030405ull);
    w.packedGuid(0x1112131415ull);
    w.u32(1234);   // total
    w.u32(7);      // overkill
    w.u8(2);
    for (int i = 0; i < 2; ++i) {
        w.u32(static_cast<uint32_t>(1 << i));
        w.f32(100.0f + i);
        w.u32(600 + i);
    }
    if (hitInfo & (kFullAbsorb | kPartialAbsorb)) { w.u32(10); w.u32(11); }
    if (hitInfo & kPartialResist) { w.u32(20); w.u32(21); }
    w.u8(victimState);
    w.u32(0);
    w.u32(0);
    if (hitInfo & kBlock) w.u32(blocked);
    if (hitInfo & kRageGain) w.u32(0);
    return w.bytes;
}

}  // namespace

TEST_CASE("WotLK attacker state: a plain hit has no absorb and its victim state",
          "[attackerstate]") {
    wowee::network::Packet packet(0, wotlkHit(kAffectsVictim, 1, 0));
    WotlkPacketParsers parsers;
    AttackerStateUpdateData data;
    REQUIRE(parsers.parseAttackerStateUpdate(packet, data));
    CHECK(data.totalDamage == 1234);
    CHECK(data.overkill == 7);
    REQUIRE(data.subDamages.size() == 2);
    CHECK(data.subDamages[1].intDamage == 601u);
    CHECK(data.subDamages[0].absorbed == 0u);
    CHECK(data.subDamages[0].resisted == 0u);
    CHECK(data.victimState == 1u);
    CHECK(data.blocked == 0u);
}

TEST_CASE("WotLK attacker state: absorbs, resists and blocked when flagged",
          "[attackerstate]") {
    wowee::network::Packet packet(
        0, wotlkHit(kAffectsVictim | kPartialAbsorb | kPartialResist | kBlock | kGlancing, 1, 55));
    WotlkPacketParsers parsers;
    AttackerStateUpdateData data;
    REQUIRE(parsers.parseAttackerStateUpdate(packet, data));
    REQUIRE(data.subDamages.size() == 2);
    CHECK(data.subDamages[1].absorbed == 11u);
    CHECK(data.subDamages[1].resisted == 21u);
    CHECK(data.victimState == 1u);
    CHECK(data.blocked == 55u);
    CHECK(data.isGlancing());
    CHECK_FALSE(data.isCrushing());
}

TEST_CASE("WotLK attacker state: a dodge reads as one", "[attackerstate]") {
    wowee::network::Packet packet(0, wotlkHit(kAffectsVictim, 2, 0));
    WotlkPacketParsers parsers;
    AttackerStateUpdateData data;
    REQUIRE(parsers.parseAttackerStateUpdate(packet, data));
    CHECK(data.victimState == 2u);
}

TEST_CASE("WotLK attacker state: resists alone, and a full absorb alone", "[attackerstate]") {
    WotlkPacketParsers parsers;
    {
        wowee::network::Packet packet(0, wotlkHit(kAffectsVictim | kPartialResist, 1, 0));
        AttackerStateUpdateData data;
        REQUIRE(parsers.parseAttackerStateUpdate(packet, data));
        CHECK(data.subDamages[0].resisted == 20u);
        CHECK(data.subDamages[0].absorbed == 0u);
        CHECK(data.victimState == 1u);
    }
    {
        wowee::network::Packet packet(0, wotlkHit(kAffectsVictim | kFullAbsorb | kCrushing | kRageGain, 1, 0));
        AttackerStateUpdateData data;
        REQUIRE(parsers.parseAttackerStateUpdate(packet, data));
        CHECK(data.subDamages[1].absorbed == 11u);
        CHECK(data.subDamages[1].resisted == 0u);
        CHECK(data.isCrushing());
        CHECK_FALSE(data.isGlancing());
    }
}

TEST_CASE("WotLK attacker state: a short packet is refused and rewound", "[attackerstate]") {
    WotlkPacketParsers parsers;
    const auto refused = [&](std::vector<uint8_t> bytes) {
        wowee::network::Packet packet(0, std::move(bytes));
        AttackerStateUpdateData data;
        const bool ok = parsers.parseAttackerStateUpdate(packet, data);
        return !ok && packet.getReadPos() == 0;
    };
    auto full = wotlkHit(kAffectsVictim, 1, 0);
    // The tail cut short: victim state, unknown and spell id need nine bytes.
    CHECK(refused(std::vector<uint8_t>(full.begin(), full.end() - 1)));
    // The absorb flag with no absorbs behind it.
    auto noAbsorbs = wotlkHit(kAffectsVictim, 1, 0);
    noAbsorbs[0] = static_cast<uint8_t>(kAffectsVictim | kPartialAbsorb);
    noAbsorbs.resize(noAbsorbs.size() - 9);
    CHECK(refused(noAbsorbs));
    // A count no packet of this size could hold.
    Writer w;
    w.u32(kAffectsVictim);
    w.packedGuid(0x0102030405ull);
    w.packedGuid(0x1112131415ull);
    w.u32(0);
    w.u32(0);
    w.u8(200);
    for (int i = 0; i < 12; ++i) w.u8(0);
    CHECK(refused(w.bytes));
}
