// Server packets through GameHandler's own dispatch. See game_replay.hpp.
#include <catch_amalgamated.hpp>

#include "game_replay.hpp"
#include "addons/lua_api_registrations.hpp"
#include "addons/addon_manager.hpp"
#include "game/entity.hpp"
#include "game/game_utils.hpp"
#include "game/packed_time.hpp"
#include "game/packet_parsers.hpp"
#include "game/spell_handler.hpp"

#include <algorithm>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

using replay::Session;
using replay::UpdateObject;
using wowee::game::UF;

namespace {

std::shared_ptr<wowee::game::Unit> playerUnit(Session& s) {
    return std::dynamic_pointer_cast<wowee::game::Unit>(
        s.handler().getEntityManager().getEntity(Session::kPlayerGuid));
}

// A field-only unit block: no movement flags carry a position or spline.
// Pre-WotLK has a transport byte in the packet and eight-bit create flags;
// WotLK uses sixteen-bit flags. The values mask has the same wire shape.
wowee::network::Packet unitFields(const std::map<uint16_t, uint32_t>& fields,
                                 bool create, bool preWotlk,
                                 uint64_t guid = Session::kPlayerGuid,
                                 wowee::game::ObjectType type = wowee::game::ObjectType::PLAYER) {
    using namespace wowee::game;
    wowee::network::Packet packet(wireOpcode(Opcode::SMSG_UPDATE_OBJECT));
    packet.writeUInt32(1);
    if (preWotlk) packet.writeUInt8(0);
    packet.writeUInt8(static_cast<uint8_t>(create ? UpdateType::CREATE_OBJECT2 : UpdateType::VALUES));
    packet.writePackedGuid(guid);
    if (create) {
        packet.writeUInt8(static_cast<uint8_t>(type));
        const uint16_t flags = type == ObjectType::PLAYER && guid == Session::kPlayerGuid ? 1 : 0;
        if (preWotlk) packet.writeUInt8(static_cast<uint8_t>(flags)); // UPDATEFLAG_SELF
        else packet.writeUInt16(flags);
    }
    const auto blocks = static_cast<uint8_t>(fields.rbegin()->first / 32 + 1);
    packet.writeUInt8(blocks);
    for (uint8_t block = 0; block < blocks; ++block) {
        uint32_t mask = 0;
        for (const auto& [index, value] : fields) {
            if (index / 32 == block) mask |= 1u << (index % 32);
        }
        packet.writeUInt32(mask);
    }
    for (const auto& [index, value] : fields) packet.writeUInt32(value);
    return packet;
}

constexpr uint32_t kAutoShot = 75;
constexpr uint64_t kMob = 0xF130000000000123ull;
constexpr uint64_t kOtherHunter = 0x0000000000000077ull;

// A hunter in the world with a mob to shoot.
void huntMob(Session& s) {
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 181}, {UF::UNIT_FIELD_LEVEL, 9}});
    s.receive(UpdateObject().create(kMob, wowee::game::ObjectType::UNIT,
                                    {{UF::UNIT_FIELD_HEALTH, 100}, {UF::UNIT_FIELD_LEVEL, 8}}));
}

// The castCount CMSG_CAST_SPELL carried on the latest press, its first byte.
uint8_t lastCastCount(const Session& s) {
    auto casts = s.sent().of(wowee::game::Opcode::CMSG_CAST_SPELL);
    REQUIRE_FALSE(casts.empty());
    return casts.back().getData().at(0);
}

// Spell::SendCastResult: castCount, spellId, result.
wowee::network::Packet castFailed(uint8_t castCount, uint32_t spellId, uint8_t result) {
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_CAST_FAILED));
    p.writeUInt8(castCount);
    p.writeUInt32(spellId);
    p.writeUInt8(result);
    return p;
}

// Player::SendAutoRepeatCancel: the shooter's packed guid, to everyone nearby.
wowee::network::Packet autoRepeatCancelled(uint64_t shooter) {
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_CANCEL_AUTO_REPEAT));
    p.writePackedGuid(shooter);
    return p;
}

constexpr uint8_t kNotReady = 67;
constexpr uint8_t kOutOfRange = 97;

}  // namespace

TEST_CASE("the session speaks WotLK", "[replay]") {
    Session s;
    CHECK(wowee::game::isActiveExpansion("wotlk"));
    CHECK_FALSE(wowee::game::isPreWotlk());
}

TEST_CASE("TBC cast failures retain their item and fishing meanings", "[replay][tbc-cast-errors]") {
    using namespace wowee::game;
    Session s("tbc");
    struct Failure { uint8_t raw, normalized; const char* text; };
    // Deployed CMaNGOS TBC SpellDefines.h values, not the shared UI table.
    const Failure cases[] = {{0x2a,45,"Item not ready"}, {0x2c,47,"Target not in line of sight"},
        {0x36,58,"You can't fish in this area."}, {0x89,55,"Need more items"},
        {0x5f,100,"Reagents required"}, {0x3f,67,"Not ready"}};
    std::vector<std::string> messages;
    s.handler().setAddonEventCallback([&](const std::string& event, const auto& args) {
        if (event == "UI_ERROR_MESSAGE" && !args.empty()) messages.push_back(args.front());
    });
    for (const auto& failure : cases) {
        CAPTURE(failure.raw, failure.normalized, failure.text);
        // Spell::SendCastResult: spellId, result, cast_count. The separate
        // SMSG_SPELL_FAILURE from SendInterrupted uses a packed caster first.
        // The core calls 0x130 SMSG_CAST_RESULT; WoWee's TBC opcode table
        // canonically names that same wire packet SMSG_CAST_FAILED.
        for (const auto opcode : {Opcode::SMSG_CAST_FAILED, Opcode::SMSG_SPELL_FAILURE}) {
            const auto before = messages.size();
            wowee::network::Packet packet(wireOpcode(opcode));
            if (opcode == Opcode::SMSG_SPELL_FAILURE) packet.writePackedGuid(Session::kPlayerGuid);
            packet.writeUInt32(7620); // apprentice Fishing
            packet.writeUInt8(failure.raw);
            if (opcode == Opcode::SMSG_CAST_FAILED) packet.writeUInt8(0);
            s.receive(packet);
            REQUIRE(messages.size() == before + 1);
            CHECK(messages.back().find(failure.text) != std::string::npos);
        }
        wowee::network::Packet packet(0);
        packet.writeUInt32(7620); packet.writeUInt8(failure.raw); packet.writeUInt8(0);
        CastFailedData data;
        REQUIRE(s.handler().getPacketParsers()->parseCastFailed(packet, data));
        CHECK(data.result == failure.normalized);
        wowee::network::Packet resultPacket(0);
        resultPacket.writeUInt32(7620); resultPacket.writeUInt8(failure.raw); resultPacket.writeUInt8(0);
        uint32_t spell = 0, misc = 0, misc2 = 0;
        uint8_t result = 0;
        REQUIRE(s.handler().getPacketParsers()->parseCastResult(resultPacket, spell, result, misc, misc2));
        CHECK(result == failure.normalized);
    }
    CHECK(createPacketParsers("classic")->castResult(44) == 45);
    CHECK(createPacketParsers("wotlk")->castResult(45) == 45);
}

TEST_CASE("every TBC cast failure matches the actual core enum", "[replay][tbc-cast-table]") {
    using namespace wowee::game;
    Session s("tbc");
    auto* parser = s.handler().getPacketParsers();
    // Literal names/numbers from the installed CMaNGOS SpellDefines.h and
    // AzerothCore SharedDefines.h. Four TBC-only meanings use shared UI codes
    // 188-191, outside the WotLK wire range. No production lookup generates this.
    struct Result { uint8_t raw, normalized; const char* name; };
    const Result cases[] = {
        {0x00, 1, "SPELL_FAILED_AFFECTING_COMBAT"},
        {0x01, 2, "SPELL_FAILED_ALREADY_AT_FULL_HEALTH"},
        {0x02, 3, "SPELL_FAILED_ALREADY_AT_FULL_MANA"},
        {0x03, 4, "SPELL_FAILED_ALREADY_AT_FULL_POWER"},
        {0x04, 5, "SPELL_FAILED_ALREADY_BEING_TAMED"},
        {0x05, 6, "SPELL_FAILED_ALREADY_HAVE_CHARM"},
        {0x06, 7, "SPELL_FAILED_ALREADY_HAVE_SUMMON"},
        {0x07, 8, "SPELL_FAILED_ALREADY_OPEN"},
        {0x08, 9, "SPELL_FAILED_AURA_BOUNCED"},
        {0x09, 10, "SPELL_FAILED_AUTOTRACK_INTERRUPTED"},
        {0x0a, 11, "SPELL_FAILED_BAD_IMPLICIT_TARGETS"},
        {0x0b, 12, "SPELL_FAILED_BAD_TARGETS"},
        {0x0c, 13, "SPELL_FAILED_CANT_BE_CHARMED"},
        {0x0d, 14, "SPELL_FAILED_CANT_BE_DISENCHANTED"},
        {0x0e, 15, "SPELL_FAILED_CANT_BE_DISENCHANTED_SKILL"},
        {0x0f, 17, "SPELL_FAILED_CANT_BE_PROSPECTED"},
        {0x10, 18, "SPELL_FAILED_CANT_CAST_ON_TAPPED"},
        {0x11, 19, "SPELL_FAILED_CANT_DUEL_WHILE_INVISIBLE"},
        {0x12, 20, "SPELL_FAILED_CANT_DUEL_WHILE_STEALTHED"},
        {0x13, 21, "SPELL_FAILED_CANT_STEALTH"},
        {0x14, 22, "SPELL_FAILED_CASTER_AURASTATE"},
        {0x15, 23, "SPELL_FAILED_CASTER_DEAD"},
        {0x16, 24, "SPELL_FAILED_CHARMED"},
        {0x17, 25, "SPELL_FAILED_CHEST_IN_USE"},
        {0x18, 26, "SPELL_FAILED_CONFUSED"},
        {0x19, 27, "SPELL_FAILED_DONT_REPORT"},
        {0x1a, 28, "SPELL_FAILED_EQUIPPED_ITEM"},
        {0x1b, 29, "SPELL_FAILED_EQUIPPED_ITEM_CLASS"},
        {0x1c, 30, "SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND"},
        {0x1d, 31, "SPELL_FAILED_EQUIPPED_ITEM_CLASS_OFFHAND"},
        {0x1e, 32, "SPELL_FAILED_ERROR"},
        {0x1f, 33, "SPELL_FAILED_FIZZLE"},
        {0x20, 34, "SPELL_FAILED_FLEEING"},
        {0x21, 35, "SPELL_FAILED_FOOD_LOWLEVEL"},
        {0x22, 36, "SPELL_FAILED_HIGHLEVEL"},
        {0x23, 37, "SPELL_FAILED_HUNGER_SATIATED"},
        {0x24, 38, "SPELL_FAILED_IMMUNE"},
        {0x25, 40, "SPELL_FAILED_INTERRUPTED"},
        {0x26, 41, "SPELL_FAILED_INTERRUPTED_COMBAT"},
        {0x27, 42, "SPELL_FAILED_ITEM_ALREADY_ENCHANTED"},
        {0x28, 43, "SPELL_FAILED_ITEM_GONE"},
        {0x29, 44, "SPELL_FAILED_ITEM_NOT_FOUND"},
        {0x2a, 45, "SPELL_FAILED_ITEM_NOT_READY"},
        {0x2b, 46, "SPELL_FAILED_LEVEL_REQUIREMENT"},
        {0x2c, 47, "SPELL_FAILED_LINE_OF_SIGHT"},
        {0x2d, 48, "SPELL_FAILED_LOWLEVEL"},
        {0x2e, 49, "SPELL_FAILED_LOW_CASTLEVEL"},
        {0x2f, 50, "SPELL_FAILED_MAINHAND_EMPTY"},
        {0x30, 51, "SPELL_FAILED_MOVING"},
        {0x31, 52, "SPELL_FAILED_NEED_AMMO"},
        {0x32, 53, "SPELL_FAILED_NEED_AMMO_POUCH"},
        {0x33, 54, "SPELL_FAILED_NEED_EXOTIC_AMMO"},
        {0x34, 56, "SPELL_FAILED_NOPATH"},
        {0x35, 57, "SPELL_FAILED_NOT_BEHIND"},
        {0x36, 58, "SPELL_FAILED_NOT_FISHABLE"},
        {0x37, 59, "SPELL_FAILED_NOT_FLYING"},
        {0x38, 60, "SPELL_FAILED_NOT_HERE"},
        {0x39, 61, "SPELL_FAILED_NOT_INFRONT"},
        {0x3a, 62, "SPELL_FAILED_NOT_IN_CONTROL"},
        {0x3b, 63, "SPELL_FAILED_NOT_KNOWN"},
        {0x3c, 64, "SPELL_FAILED_NOT_MOUNTED"},
        {0x3d, 65, "SPELL_FAILED_NOT_ON_TAXI"},
        {0x3e, 66, "SPELL_FAILED_NOT_ON_TRANSPORT"},
        {0x3f, 67, "SPELL_FAILED_NOT_READY"},
        {0x40, 68, "SPELL_FAILED_NOT_SHAPESHIFT"},
        {0x41, 69, "SPELL_FAILED_NOT_STANDING"},
        {0x42, 70, "SPELL_FAILED_NOT_TRADEABLE"},
        {0x43, 71, "SPELL_FAILED_NOT_TRADING"},
        {0x44, 72, "SPELL_FAILED_NOT_UNSHEATHED"},
        {0x45, 73, "SPELL_FAILED_NOT_WHILE_GHOST"},
        {0x46, 75, "SPELL_FAILED_NO_AMMO"},
        {0x47, 76, "SPELL_FAILED_NO_CHARGES_REMAIN"},
        {0x48, 77, "SPELL_FAILED_NO_CHAMPION"},
        {0x49, 78, "SPELL_FAILED_NO_COMBO_POINTS"},
        {0x4a, 79, "SPELL_FAILED_NO_DUELING"},
        {0x4b, 80, "SPELL_FAILED_NO_ENDURANCE"},
        {0x4c, 81, "SPELL_FAILED_NO_FISH"},
        {0x4d, 82, "SPELL_FAILED_NO_ITEMS_WHILE_SHAPESHIFTED"},
        {0x4e, 83, "SPELL_FAILED_NO_MOUNTS_ALLOWED"},
        {0x4f, 84, "SPELL_FAILED_NO_PET"},
        {0x50, 85, "SPELL_FAILED_NO_POWER"},
        {0x51, 86, "SPELL_FAILED_NOTHING_TO_DISPEL"},
        {0x52, 87, "SPELL_FAILED_NOTHING_TO_STEAL"},
        {0x53, 88, "SPELL_FAILED_ONLY_ABOVEWATER"},
        {0x54, 89, "SPELL_FAILED_ONLY_DAYTIME"},
        {0x55, 90, "SPELL_FAILED_ONLY_INDOORS"},
        {0x56, 91, "SPELL_FAILED_ONLY_MOUNTED"},
        {0x57, 92, "SPELL_FAILED_ONLY_NIGHTTIME"},
        {0x58, 93, "SPELL_FAILED_ONLY_OUTDOORS"},
        {0x59, 94, "SPELL_FAILED_ONLY_SHAPESHIFT"},
        {0x5a, 95, "SPELL_FAILED_ONLY_STEALTHED"},
        {0x5b, 96, "SPELL_FAILED_ONLY_UNDERWATER"},
        {0x5c, 97, "SPELL_FAILED_OUT_OF_RANGE"},
        {0x5d, 98, "SPELL_FAILED_PACIFIED"},
        {0x5e, 99, "SPELL_FAILED_POSSESSED"},
        {0x5f, 100, "SPELL_FAILED_REAGENTS"},
        {0x60, 101, "SPELL_FAILED_REQUIRES_AREA"},
        {0x61, 102, "SPELL_FAILED_REQUIRES_SPELL_FOCUS"},
        {0x62, 103, "SPELL_FAILED_ROOTED"},
        {0x63, 104, "SPELL_FAILED_SILENCED"},
        {0x64, 105, "SPELL_FAILED_SPELL_IN_PROGRESS"},
        {0x65, 106, "SPELL_FAILED_SPELL_LEARNED"},
        {0x66, 107, "SPELL_FAILED_SPELL_UNAVAILABLE"},
        {0x67, 108, "SPELL_FAILED_STUNNED"},
        {0x68, 109, "SPELL_FAILED_TARGETS_DEAD"},
        {0x69, 110, "SPELL_FAILED_TARGET_AFFECTING_COMBAT"},
        {0x6a, 111, "SPELL_FAILED_TARGET_AURASTATE"},
        {0x6b, 112, "SPELL_FAILED_TARGET_DUELING"},
        {0x6c, 113, "SPELL_FAILED_TARGET_ENEMY"},
        {0x6d, 114, "SPELL_FAILED_TARGET_ENRAGED"},
        {0x6e, 115, "SPELL_FAILED_TARGET_FRIENDLY"},
        {0x6f, 116, "SPELL_FAILED_TARGET_IN_COMBAT"},
        {0x70, 117, "SPELL_FAILED_TARGET_IS_PLAYER"},
        {0x71, 118, "SPELL_FAILED_TARGET_IS_PLAYER_CONTROLLED"},
        {0x72, 119, "SPELL_FAILED_TARGET_NOT_DEAD"},
        {0x73, 120, "SPELL_FAILED_TARGET_NOT_IN_PARTY"},
        {0x74, 121, "SPELL_FAILED_TARGET_NOT_LOOTED"},
        {0x75, 122, "SPELL_FAILED_TARGET_NOT_PLAYER"},
        {0x76, 123, "SPELL_FAILED_TARGET_NO_POCKETS"},
        {0x77, 124, "SPELL_FAILED_TARGET_NO_WEAPONS"},
        {0x78, 126, "SPELL_FAILED_TARGET_UNSKINNABLE"},
        {0x79, 127, "SPELL_FAILED_THIRST_SATIATED"},
        {0x7a, 128, "SPELL_FAILED_TOO_CLOSE"},
        {0x7b, 129, "SPELL_FAILED_TOO_MANY_OF_ITEM"},
        {0x7c, 130, "SPELL_FAILED_TOTEM_CATEGORY"},
        {0x7d, 131, "SPELL_FAILED_TOTEMS"},
        {0x7e, 188, "SPELL_FAILED_TRAINING_POINTS"},
        {0x7f, 132, "SPELL_FAILED_TRY_AGAIN"},
        {0x80, 133, "SPELL_FAILED_UNIT_NOT_BEHIND"},
        {0x81, 134, "SPELL_FAILED_UNIT_NOT_INFRONT"},
        {0x82, 135, "SPELL_FAILED_WRONG_PET_FOOD"},
        {0x83, 136, "SPELL_FAILED_NOT_WHILE_FATIGUED"},
        {0x84, 137, "SPELL_FAILED_TARGET_NOT_IN_INSTANCE"},
        {0x85, 138, "SPELL_FAILED_NOT_WHILE_TRADING"},
        {0x86, 139, "SPELL_FAILED_TARGET_NOT_IN_RAID"},
        {0x87, 189, "SPELL_FAILED_DISENCHANT_WHILE_LOOTING"},
        {0x88, 190, "SPELL_FAILED_PROSPECT_WHILE_LOOTING"},
        {0x89, 55, "SPELL_FAILED_NEED_MORE_ITEMS"},
        {0x8a, 140, "SPELL_FAILED_TARGET_FREEFORALL"},
        {0x8b, 141, "SPELL_FAILED_NO_EDIBLE_CORPSES"},
        {0x8c, 142, "SPELL_FAILED_ONLY_BATTLEGROUNDS"},
        {0x8d, 143, "SPELL_FAILED_TARGET_NOT_GHOST"},
        {0x8e, 191, "SPELL_FAILED_TOO_MANY_SKILLS"},
        {0x8f, 144, "SPELL_FAILED_TRANSFORM_UNUSABLE"},
        {0x90, 145, "SPELL_FAILED_WRONG_WEATHER"},
        {0x91, 146, "SPELL_FAILED_DAMAGE_IMMUNE"},
        {0x92, 147, "SPELL_FAILED_PREVENTED_BY_MECHANIC"},
        {0x93, 148, "SPELL_FAILED_PLAY_TIME"},
        {0x94, 149, "SPELL_FAILED_REPUTATION"},
        {0x95, 150, "SPELL_FAILED_MIN_SKILL"},
        {0x96, 151, "SPELL_FAILED_NOT_IN_ARENA"},
        {0x97, 152, "SPELL_FAILED_NOT_ON_SHAPESHIFT"},
        {0x98, 153, "SPELL_FAILED_NOT_ON_STEALTHED"},
        {0x99, 154, "SPELL_FAILED_NOT_ON_DAMAGE_IMMUNE"},
        {0x9a, 155, "SPELL_FAILED_NOT_ON_MOUNTED"},
        {0x9b, 156, "SPELL_FAILED_TOO_SHALLOW"},
        {0x9c, 157, "SPELL_FAILED_TARGET_NOT_IN_SANCTUARY"},
        {0x9d, 158, "SPELL_FAILED_TARGET_IS_TRIVIAL"},
        {0x9e, 159, "SPELL_FAILED_BM_OR_INVISGOD"},
        {0x9f, 160, "SPELL_FAILED_EXPERT_RIDING_REQUIREMENT"},
        {0xa0, 161, "SPELL_FAILED_ARTISAN_RIDING_REQUIREMENT"},
        {0xa1, 162, "SPELL_FAILED_NOT_IDLE"},
        {0xa2, 163, "SPELL_FAILED_NOT_INACTIVE"},
        {0xa3, 164, "SPELL_FAILED_PARTIAL_PLAYTIME"},
        {0xa4, 165, "SPELL_FAILED_NO_PLAYTIME"},
        {0xa5, 166, "SPELL_FAILED_NOT_IN_BATTLEGROUND"},
        {0xa6, 168, "SPELL_FAILED_ONLY_IN_ARENA"},
        {0xa7, 169, "SPELL_FAILED_TARGET_LOCKED_TO_RAID_INSTANCE"},
        {0xa8, 187, "SPELL_FAILED_UNKNOWN"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.raw, c.name);
        CHECK(parser->castResult(c.raw) == c.normalized);
    }
    for (unsigned raw = 0xa9; raw < 0xff; ++raw) {
        CAPTURE(raw);
        CHECK(parser->castResult(static_cast<uint8_t>(raw)) == 187); // unknown, never a guessed reason
    }
    CHECK(parser->castResult(0xff) == 0); // server-only OK, same shared success convention
    CHECK(createPacketParsers("wotlk")->castResult(97) == 97);
    CHECK(createPacketParsers("classic")->castResult(44) == 45);
}

TEST_CASE("TBC resource, range and tool failures explain the actual blocker", "[replay][tbc-cast-table]") {
    using namespace wowee::game;
    Session s("tbc");
    std::vector<std::string> messages;
    s.handler().setAddonEventCallback([&](const std::string& event, const auto& args) {
        if (event == "UI_ERROR_MESSAGE" && !args.empty()) messages.push_back(args.front());
    });
    struct Failure { uint8_t raw, normalized; const char* text; };
    const Failure cases[] = {
        {0x1b, 29, "Required item class is not equipped"},
        {0x1c, 30, "mainhand"}, {0x1d, 31, "offhand"},
        {0x50, 85, "Not enough mana"}, {0x5c, 97, "Out of range"},
        {0x62, 103, "rooted"}, {0x63, 104, "silenced"}, {0x64, 105, "Spell in progress"},
        {0x6a, 111, "required state"}, {0x73, 120, "Target not in party"},
        {0x7e, 188, "Training points required"}, {0x87, 189, "Cannot disenchant while looting"},
        {0x88, 190, "Cannot prospect while looting"}, {0x8e, 191, "Too many skills"},
        {0xa6, 168, "Only in arena"}, {0xa7, 169, "Target locked to raid instance"},
        {0xa8, 187, "Unknown error"}
    };
    for (const auto& c : cases) {
        CAPTURE(c.raw, c.normalized, c.text);
        for (auto opcode : {Opcode::SMSG_CAST_FAILED, Opcode::SMSG_SPELL_FAILURE}) {
            const auto before=messages.size();
            wowee::network::Packet packet(wireOpcode(opcode));
            if (opcode == Opcode::SMSG_SPELL_FAILURE) packet.writePackedGuid(Session::kPlayerGuid);
            packet.writeUInt32(585); packet.writeUInt8(c.raw);
            if (opcode == Opcode::SMSG_CAST_FAILED) packet.writeUInt8(0);
            s.receive(packet);
            REQUIRE(messages.size() == before+1);
            CHECK(messages.back().find(c.text) != std::string::npos);
        }
    }
    // Spell::SendCastResult appends a focus id or two tool/category ids after
    // spell, raw result and cast count. Mapping must happen before tail parsing.
    for (const auto raw : {0x61, 0x7c, 0x7d}) {
        CAPTURE(raw);
        wowee::network::Packet packet(0);
        packet.writeUInt32(585); packet.writeUInt8(static_cast<uint8_t>(raw)); packet.writeUInt8(7);
        packet.writeUInt32(123); if (raw != 0x61) packet.writeUInt32(456);
        CastFailedData data;
        REQUIRE(s.handler().getPacketParsers()->parseCastFailed(packet, data));
        CHECK(data.spellId == 585); CHECK(data.castCount == 7);
        CHECK(data.result == (raw == 0x61 ? 102 : raw == 0x7c ? 130 : 131));
        CHECK(data.miscArg == 123); CHECK(data.miscArg2 == (raw == 0x61 ? 0 : 456));
        CHECK(packet.getRemainingSize() == 0);
        wowee::network::Packet resultPacket(0);
        resultPacket.writeUInt32(585); resultPacket.writeUInt8(static_cast<uint8_t>(raw)); resultPacket.writeUInt8(7);
        resultPacket.writeUInt32(123); if (raw != 0x61) resultPacket.writeUInt32(456);
        uint32_t spell=0, misc=0, misc2=0; uint8_t result=0;
        REQUIRE(s.handler().getPacketParsers()->parseCastResult(resultPacket,spell,result,misc,misc2));
        CHECK(result == data.result); CHECK(misc == 123); CHECK(misc2 == data.miscArg2);
        CHECK(resultPacket.getRemainingSize() == 0);
    }
}

TEST_CASE("the player's create block makes the player's unit", "[replay]") {
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 181},
                   {UF::UNIT_FIELD_MAXHEALTH, 181},
                   {UF::UNIT_FIELD_LEVEL, 9}});

    auto unit = playerUnit(s);
    REQUIRE(unit);
    CHECK(unit->getHealth() == 181);
    CHECK(unit->getMaxHealth() == 181);
    CHECK(unit->getLevel() == 9);
}

TEST_CASE("TBC coinage follows the server's field on login and values updates", "[replay][tbc-wallet]") {
    // CMaNGOS TBC UpdateFields.h: UNIT_END234 +0x04CB =1461.
    // Use literal server indices: building this packet with the client's own
    // field table would make a wrong table pass by agreeing with itself.
    // 1441, the former coinage mapping, lies inside the explored-zone bitset.
    Session s("tbc");
    CHECK(wowee::game::fieldIndex(UF::PLAYER_FIELD_COINAGE) == 1461);
    std::vector<std::string> events;
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua);
    s.handler().setAddonEventCallback([&](const auto& event, const auto&) { events.push_back(event); });
    const auto moneyEvents = [&] { return std::count(events.begin(), events.end(), "PLAYER_MONEY"); };
    s.receive(unitFields({{22,180},{28,180},{34,7},{1441,0},{1461,600}},true,true));
    CHECK(s.handler().getMoneyCopper() == 600);
    CHECK(lua->evaluateBoolean("GetMoney() == 600"));
    CHECK(moneyEvents() == 1);

    s.receive(unitFields({{1461,650}},false,true));
    CHECK(s.handler().getMoneyCopper() == 650);
    CHECK(lua->evaluateBoolean("GetMoney() == 650"));
    CHECK(moneyEvents() == 2);
    s.receive(unitFields({{1461,650}},false,true));
    CHECK(moneyEvents() == 2); // Repeating a balance is not another change.

    // Exploration and rested XP cannot become a wallet balance or money event.
    s.receive(unitFields({{1441,0x80000000u},{1460,321}},false,true));
    CHECK(s.handler().getMoneyCopper() == 650);
    CHECK(lua->evaluateBoolean("GetMoney() == 650"));
    CHECK(moneyEvents() == 2);
    s.receive(unitFields({{1461,0}},false,true));
    CHECK(s.handler().getMoneyCopper() == 0);
    CHECK(lua->evaluateBoolean("GetMoney() == 0"));
    CHECK(moneyEvents() == 3);
}

TEST_CASE("TBC aura fields use all 56 slots and packed metadata", "[replay][tbc-aura-fields]") {
    // Literal CMaNGOS TBC UpdateFields.h / SpellAuras.cpp wire values. Do not
    // construct these from the client's field table: that hid the old errors.
    Session s("tbc");
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua);
    REQUIRE(lua->executeString("__WoweeInterfaceVersion=20400"));
    std::vector<std::string> events;
    s.handler().setAddonEventCallback([&](const auto& event, const auto&) { events.push_back(event); });
    s.receive(unitFields({{22,180},{28,180},{34,7},
        {48,133},{87,116},{88,17},{103,1243},
        {104,0x12},{113,0x12000000},{114,0x21},{117,0x21000000},
        {118,60},{127,30u<<24},{128,70},{131,80u<<24},
        {132,1},{141,2u<<24},{142,0},{145,4u<<24}},true,true));
    const auto& auras = s.handler().getPlayerAuras();
    REQUIRE(auras.size() == 56);
    CHECK(std::count_if(auras.begin(),auras.end(),[](const auto& a){return !a.isEmpty();}) == 4);
    CHECK(auras[0].spellId == 133);
    CHECK(auras[39].spellId == 116);
    CHECK(auras[40].spellId == 17);
    CHECK(auras[55].spellId == 1243);
    CHECK((auras[0].flags & 0x02) != 0); // effect 1, not harmful
    CHECK((auras[0].flags & 0x80) == 0);
    CHECK((auras[39].flags & 0x80) == 0);
    CHECK((auras[40].flags & 0x80) != 0);
    CHECK((auras[55].flags & 0x80) != 0);
    CHECK(auras[0].level == 60);
    CHECK(auras[39].level == 30);
    CHECK(auras[40].level == 70);
    CHECK(auras[55].level == 80);
    CHECK(lua->evaluateBoolean("GetPlayerBuff(1,'HELPFUL') == 1 and GetPlayerBuff(2,'HELPFUL') == 40 and GetPlayerBuff(3,'HELPFUL') == 0"));
    CHECK(lua->evaluateBoolean("GetPlayerBuff(1,'HARMFUL') == 41 and GetPlayerBuff(2,'HARMFUL') == 56 and GetPlayerBuff(3,'HARMFUL') == 0"));
    CHECK(lua->evaluateBoolean("GetPlayerBuffApplications(1) == 2 and GetPlayerBuffApplications(40) == 3 and GetPlayerBuffApplications(41) == 1 and GetPlayerBuffApplications(56) == 5"));

    // Actual SMSG_UPDATE_AURA_DURATION payload: byte slot, uint32 remaining.
    wowee::network::Packet duration(0x137);
    duration.writeUInt8(55); duration.writeUInt32(45000);
    s.receive(duration);
    REQUIRE(auras[55].durationMs == 45000);
    const auto received = auras[55].receivedAtMs;
    events.clear();
    s.receive(unitFields({{1461,600},{926,123}},false,true));
    CHECK(auras[55].durationMs == 45000);
    CHECK(auras[55].receivedAtMs == received);
    CHECK(std::count(events.begin(),events.end(),"UNIT_AURA") == 0);
    s.receive(unitFields({{104,0x11},{145,254u<<24}},false,true));
    CHECK(auras[0].flags == 0x11);
    CHECK(auras[55].durationMs == 45000);
    CHECK(auras[55].receivedAtMs == received);
    CHECK(lua->evaluateBoolean("GetPlayerBuffApplications(56) == 255"));
    CHECK(std::count(events.begin(),events.end(),"UNIT_AURA") == 1);
    s.receive(unitFields({{103,133}},false,true));
    CHECK(auras[55].spellId == 133);
    CHECK(auras[55].durationMs == -1); // no inherited duration from previous identity
    s.receive(unitFields({{48,0},{87,0},{88,0},{103,0}},false,true));
    CHECK(std::all_of(auras.begin(),auras.end(),[](const auto& a){return a.isEmpty();}));
    CHECK(lua->evaluateBoolean("GetPlayerBuff(1,'HELPFUL') == 0 and GetPlayerBuff(1,'HARMFUL') == 0"));
}

TEST_CASE("TBC aura applications cannot act as unit visibility bytes", "[replay][tbc-aura-fields]") {
    Session s("tbc");
    s.receive(unitFields({{22,180},{28,180},{34,7}},true,true));
    auto unit = playerUnit(s);
    REQUIRE(unit);
    s.receive(unitFields({{159,0x00010000}},false,true));
    CHECK(unit->getVisibilityFlags() == 1);
    // Actual applications word5, formerly misidentified as UNIT_FIELD_BYTES_1.
    s.receive(unitFields({{137,0x01020000}},false,true));
    CHECK(unit->getVisibilityFlags() == 1);
}

TEST_CASE("TBC extra aura duration uses packed guid and one slot", "[replay][tbc-aura-fields]") {
    Session s("tbc");
    s.receive(unitFields({{22,180},{28,180},{34,7}},true,true));
    auto* spells = s.handler().getSpellHandler();
    REQUIRE(spells);
    spells->resetPlayerAuras(56);
    spells->getPlayerAuraSlotRef(55).spellId = 1243;
    spells->getPlayerAuraSlotRef(55).flags = 0x81;
    // SpellAuraHolder::SendAuraDurationToCaster[NeedUpdate]: packed target,
    // slot, spell id, max duration, remaining. No count/effect/flags bytes.
    for (const uint16_t opcode : {0x3a4,0x3a5}) {
        wowee::network::Packet packet(opcode);
        packet.writePackedGuid(Session::kPlayerGuid);
        packet.writeUInt8(55); packet.writeUInt32(1243);
        packet.writeUInt32(60000); packet.writeUInt32(42000);
        s.receive(packet);
        const auto& aura = s.handler().getPlayerAuras().at(55);
        CHECK(aura.spellId == 1243);
        CHECK(aura.flags == 0x81);
        CHECK(aura.maxDurationMs == 60000);
        CHECK(aura.durationMs == 42000);
    }
    // A truncated duration or invalid slot cannot mutate the current aura.
    wowee::network::Packet truncated(0x3a4);
    truncated.writePackedGuid(Session::kPlayerGuid);
    truncated.writeUInt8(55); truncated.writeUInt32(1243);
    truncated.writeUInt32(60000);
    s.receive(truncated);
    CHECK(s.handler().getPlayerAuras().at(55).durationMs == 42000);
    spells->getPlayerAuraSlotRef(0).spellId = 133;
    spells->getPlayerAuraSlotRef(0).flags = 0x12;
    // Player::SendExtraAuraDurationsOnLogin(visible): same entries until EOF,
    // including an empty list. Neither form clears update-field identities.
    wowee::network::Packet initial(0x3a3);
    initial.writePackedGuid(Session::kPlayerGuid);
    initial.writeUInt8(0); initial.writeUInt32(133);
    initial.writeUInt32(60000); initial.writeUInt32(50000);
    initial.writeUInt8(55); initial.writeUInt32(1243);
    initial.writeUInt32(0xffffffff); initial.writeUInt32(0);
    s.receive(initial);
    CHECK(s.handler().getPlayerAuras().at(0).spellId == 133);
    CHECK(s.handler().getPlayerAuras().at(0).flags == 0x12);
    CHECK(s.handler().getPlayerAuras().at(0).durationMs == 50000);
    CHECK(s.handler().getPlayerAuras().at(55).durationMs == -1);
    wowee::network::Packet empty(0x3a3);
    empty.writePackedGuid(Session::kPlayerGuid);
    s.receive(empty);
    CHECK(s.handler().getPlayerAuras().at(0).spellId == 133);
    // ClearExtraAuraInfo names the spell, rather than a raw slot. Preserve
    // another aura while clearing the matching identity.
    wowee::network::Packet clear(0x3a6);
    clear.writePackedGuid(Session::kPlayerGuid); clear.writeUInt32(1243);
    s.receive(clear);
    CHECK(s.handler().getPlayerAuras().at(55).isEmpty());
    CHECK(s.handler().getPlayerAuras().at(0).spellId == 133);
    wowee::network::Packet passive(0x3a4);
    passive.writePackedGuid(Session::kPlayerGuid);
    passive.writeUInt8(56); passive.writeUInt32(20597);
    passive.writeUInt32(0xffffffff); passive.writeUInt32(0);
    s.receive(passive);
    CHECK(s.handler().getPlayerAuras().size() == 56);
}

TEST_CASE("TBC primary stats and resistances follow literal owner fields", "[replay][tbc-field-families]") {
    Session s("tbc");
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua);
    std::vector<std::string> events;
    s.handler().setAddonEventCallback([&](const auto& event,const auto&) {events.push_back(event);});
    s.receive(unitFields({{22,180},{28,180},{34,8},{36,1|(5<<8)},
        {159,0},{160,0},{161,0},{162,0},{163,0},{185,0x42100000},
        {171,21},{172,22},{173,23},{174,24},{175,25},
        {186,80},{187,1},{188,2},{189,3},{190,4},{191,5},{192,6}},true,true));
    for (int i=0;i<5;++i) {
        CAPTURE(i);
        CHECK(s.handler().getPlayerStat(i)==21+i);
        CHECK(lua->evaluateBoolean("select(2,UnitStat('player',"+std::to_string(i+1)+")) == "+std::to_string(21+i)));
    }
    CHECK(s.handler().getArmorRating()==80);
    CHECK(lua->evaluateBoolean("select(2,UnitArmor('player')) == 80 and select(2,UnitResistance('player',0)) == 80"));
    for (int school=1;school<=6;++school) {
        CHECK(s.handler().getResistance(school)==school);
        CHECK(lua->evaluateBoolean("select(2,UnitResistance('player',"+std::to_string(school)+")) == "+std::to_string(school)));
    }
    events.clear();
    s.receive(unitFields({{171,31},{172,32},{173,33},{174,34},{175,35},
        {186,180},{187,11},{188,12},{189,13},{190,14},{191,15},{192,16}},false,true));
    CHECK(std::count(events.begin(),events.end(),"UNIT_STATS")==1);
    CHECK(std::count(events.begin(),events.end(),"UNIT_RESISTANCES")==1);
    for (int i=0;i<5;++i) CHECK(s.handler().getPlayerStat(i)==31+i);
    for (int school=1;school<=6;++school) CHECK(s.handler().getResistance(school)==10+school);
    CHECK(lua->evaluateBoolean("select(2,UnitStat('player',3)) == 33 and select(2,UnitArmor('player')) == 180"));
    events.clear();
    // Former mappings now name stand/visibility and pet metadata, and the
    // maximum offhand damage float. They cannot become character sheet stats.
    s.receive(unitFields({{159,0},{160,91},{161,92},{162,93},{163,94},{185,0x42400000}},false,true));
    for (int i=0;i<5;++i) CHECK(s.handler().getPlayerStat(i)==31+i);
    CHECK(s.handler().getArmorRating()==180);
    CHECK(std::count(events.begin(),events.end(),"UNIT_STATS")==0);
    CHECK(std::count(events.begin(),events.end(),"UNIT_RESISTANCES")==0);
    // Zero is a received value rather than an unknown stat or old resistance.
    s.receive(unitFields({{173,0},{186,0},{192,0}},false,true));
    CHECK(s.handler().getPlayerStat(2)==0);
    CHECK(s.handler().getArmorRating()==0);
    CHECK(s.handler().getResistance(6)==0);
}

TEST_CASE("TBC loot cannot reidentify XP as the verified coinage field", "[replay][tbc-field-families]") {
    Session s("tbc");
    s.receive(unitFields({{22,180},{28,180},{34,8},{926,100},{1461,600}},true,true));
    // CMaNGOS TBC Loot::SendGold sends group shares as one uint32, without a
    // sole-looter byte. Coinage can arrive separately from an equal XP increment.
    wowee::network::Packet notification(0x163);
    notification.writeUInt32(25);
    s.receive(notification);
    CHECK(s.handler().getMoneyCopper()==625);
    s.receive(unitFields({{926,125}},false,true));
    CHECK(wowee::game::fieldIndex(UF::PLAYER_FIELD_COINAGE)==1461);
    s.receive(unitFields({{1461,625}},false,true));
    CHECK(s.handler().getMoneyCopper()==625);
    s.receive(unitFields({{1461,630}},false,true));
    CHECK(s.handler().getMoneyCopper()==630);
}

TEST_CASE("TBC exploration rested XP and PvP currencies use distinct fields", "[replay][tbc-field-families]") {
    Session s("tbc");
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua=manager.getLuaEngine();
    REQUIRE(lua);
    std::vector<std::string> events;
    s.handler().setAddonEventCallback([&](const auto& event,const auto&) {events.push_back(event);});
    s.receive(unitFields({{22,180},{28,180},{34,8},{1312,8},{1332,2},
        {1440,4},{1459,0x80000000},{1460,900},{1461,600},
        {1505,0x3f800000},{1506,0x40000000},{1562,45},{1563,67}},true,true));
    REQUIRE(s.handler().getPlayerExploredZoneMasks().size()==128);
    CHECK(s.handler().hasPlayerExploredZoneMasks());
    CHECK(s.handler().getPlayerExploredZoneMasks()[0]==2);
    CHECK(s.handler().getPlayerExploredZoneMasks()[108]==4);
    CHECK(s.handler().getPlayerExploredZoneMasks()[127]==0x80000000u);
    CHECK(s.handler().getPlayerRestedXp()==900);
    CHECK(s.handler().getHonorPoints()==45);
    CHECK(s.handler().getArenaPoints()==67);
    CHECK(lua->evaluateBoolean("GetXPExhaustion() == 900 and GetHonorCurrency() == 45 and GetArenaCurrency() == 67"));
    events.clear();
    s.receive(unitFields({{1312,0x80000000},{1440,64},{1505,0x40400000},{1506,0x40800000}},false,true));
    CHECK(s.handler().getPlayerExploredZoneMasks()[0]==2);
    CHECK(s.handler().getPlayerExploredZoneMasks()[108]==64);
    CHECK(s.handler().getPlayerRestedXp()==900);
    CHECK(s.handler().getHonorPoints()==45);
    CHECK(s.handler().getArenaPoints()==67);
    CHECK(std::count(events.begin(),events.end(),"UPDATE_EXHAUSTION")==0);
    s.receive(unitFields({{1332,0},{1459,16},{1460,950},{1461,650},{1562,145},{1563,167}},false,true));
    CHECK(s.handler().getPlayerExploredZoneMasks()[0]==0);
    CHECK(s.handler().getPlayerExploredZoneMasks()[127]==16);
    CHECK(lua->evaluateBoolean("GetXPExhaustion() == 950 and GetHonorCurrency() == 145 and GetArenaCurrency() == 167 and GetMoney() == 650"));
    CHECK(std::count(events.begin(),events.end(),"UPDATE_EXHAUSTION")==1);
    s.receive(unitFields({{1460,0},{1562,0},{1563,0}},false,true));
    CHECK(lua->evaluateBoolean("GetXPExhaustion() == nil and GetHonorCurrency() == 0 and GetArenaCurrency() == 0"));
}

TEST_CASE("TBC appearance fields do not read guild metadata or invent missing bytes", "[replay][tbc-field-families]") {
    Session s("tbc");
    wowee::game::Character character{};
    character.guid=Session::kPlayerGuid;
    s.handler().charactersRef().push_back(character);
    int appearanceChanges=0, rebuilds=0;
    s.handler().setAppearanceChangedCallback([&] {++appearanceChanges;});
    s.handler().setPlayerModelRebuildCallback([&] {++rebuilds;});
    s.receive(unitFields({{22,180},{28,180},{34,8},{36,1|(5<<8)},
        {237,0x11223344},{238,0x55667788},{239,0x04030201},{240,0x02030005}},true,true));
    uint8_t race=0,gender=0,facial=0;uint32_t appearance=0;
    REQUIRE(s.handler().getPlayerAppearance(Session::kPlayerGuid,race,gender,appearance,facial));
    CHECK(race==1);CHECK(gender==0);CHECK(appearance==0x04030201u);CHECK(facial==5);
    CHECK(s.handler().getInventory().getPurchasedBankBagSlots()==3);
    s.receive(unitFields({{239,0x08070605},{240,0x02020009}},false,true));
    CHECK(s.handler().getCharacters()[0].appearanceBytes==0x08070605u);
    CHECK(s.handler().getCharacters()[0].facialFeatures==9);
    CHECK(s.handler().getInventory().getPurchasedBankBagSlots()==2);
    CHECK(appearanceChanges==2);CHECK(rebuilds==2);
    s.receive(unitFields({{237,1},{238,2}},false,true));
    CHECK(appearanceChanges==2);CHECK(rebuilds==2);
    CHECK(s.handler().getCharacters()[0].appearanceBytes==0x08070605u);
    CHECK(s.handler().getCharacters()[0].facialFeatures==9);
    CHECK(s.handler().getInventory().getPurchasedBankBagSlots()==2);
    s.receive(unitFields({{239,0},{240,0}},false,true));
    REQUIRE(s.handler().getPlayerAppearance(Session::kPlayerGuid,race,gender,appearance,facial));
    CHECK(appearance==0);CHECK(facial==0);
}

TEST_CASE("TBC omitted zero appearance fields cannot be guessed from guild metadata", "[replay][tbc-field-families]") {
    Session s("tbc");
    s.receive(unitFields({{22,180},{28,180},{34,8},{36,1|(5<<8)},{237,1},{238,2}},true,true));
    uint8_t race=0,gender=0,facial=0;uint32_t appearance=0;
    // Object::_SetCreateBits omits zero fields, including valid all-zero
    // appearance. Known public fields default to zero, not nearby guild data.
    REQUIRE(s.handler().getPlayerAppearance(Session::kPlayerGuid,race,gender,appearance,facial));
    CHECK(appearance==0);CHECK(facial==0);
    s.receive(unitFields({{239,0x04030201}},false,true));
    REQUIRE(s.handler().getPlayerAppearance(Session::kPlayerGuid,race,gender,appearance,facial));
    CHECK(appearance==0x04030201u);CHECK(facial==0);
    s.handler().updateFieldTableRef().setIndex(UF::PLAYER_BYTES,0xffff);
    CHECK_FALSE(s.handler().getPlayerAppearance(Session::kPlayerGuid,race,gender,appearance,facial));
}

TEST_CASE("game object state reads TBC STATE and retains WotLK packed bytes", "[replay][tbc-field-families]") {
    using wowee::game::ObjectType;
    constexpr uint64_t guid=0xf110000000000124ull;
    for (const bool tbc : {true,false}) {
        const char* profile=tbc ? "tbc" : "wotlk";
        DYNAMIC_SECTION(profile) {
            Session s(tbc ? "tbc" : "wotlk");
            std::vector<uint8_t> states;
            s.handler().setGameObjectStateCallback([&](uint64_t id,uint8_t state) {CHECK(id==guid);states.push_back(state);});
            // On TBC17 is a position float. On WotLK17 is packed bytes.
            s.receive(unitFields({{3,31},{8,1},{14,0},{17,tbc ? 0x429b002au : 0x64ab0300u}},true,tbc,guid,ObjectType::GAMEOBJECT));
            REQUIRE(states.size()==1);CHECK(states.back()==0);
            s.receive(unitFields({{static_cast<uint16_t>(tbc ? 17 : 14),0x42b00001}},false,tbc,guid,ObjectType::GAMEOBJECT));
            CHECK(states.size()==1);
            s.receive(unitFields({{static_cast<uint16_t>(tbc ? 14 : 17),1}},false,tbc,guid,ObjectType::GAMEOBJECT));
            REQUIRE(states.size()==2);CHECK(states.back()==1);
            s.receive(unitFields({{14,2},{17,tbc ? 0x42b00000u : 0x64ab0302u}},false,tbc,guid,ObjectType::GAMEOBJECT));
            REQUIRE(states.size()==3);CHECK(states.back()==2);
            s.receive(unitFields({{static_cast<uint16_t>(tbc ? 14 : 17),0}},false,tbc,guid,ObjectType::GAMEOBJECT));
            REQUIRE(states.size()==4);CHECK(states.back()==0);
            // A second TBC CREATE can omit STATE entirely when it is zero.
            if (tbc) {
                s.receive(unitFields({{3,32},{8,1},{17,0x429b002a}},true,true,guid,ObjectType::GAMEOBJECT));
                REQUIRE(states.size()==5);CHECK(states.back()==0);
            }
        }
    }
}

TEST_CASE("unit power arrays respect each expansion's field boundaries", "[replay][power-layout]") {
    struct Profile { const char* name; uint8_t powers; bool preWotlk; };
    for (const auto profile : {Profile{"classic",5,true}, Profile{"tbc",5,true},
                               Profile{"turtle",5,true}, Profile{"wotlk",7,false}}) {
        DYNAMIC_SECTION(profile.name) {
            Session s(profile.name);
            using wowee::game::fieldIndex;
            const auto power = fieldIndex(UF::UNIT_FIELD_POWER1);
            const auto maximum = fieldIndex(UF::UNIT_FIELD_MAXPOWER1);
            REQUIRE(power + profile.powers == fieldIndex(UF::UNIT_FIELD_MAXHEALTH));
            std::map<uint16_t,uint32_t> fields{
                {fieldIndex(UF::UNIT_FIELD_HEALTH),180},
                {fieldIndex(UF::UNIT_FIELD_MAXHEALTH),300},
                {fieldIndex(UF::UNIT_FIELD_LEVEL),9},
                {fieldIndex(UF::UNIT_FIELD_FACTIONTEMPLATE),12},
                {fieldIndex(UF::UNIT_FIELD_BYTES_0),1 | (2 << 8)}}; // human paladin, mana
            for (uint8_t type=0; type<profile.powers; ++type) {
                fields[power+type]=80+type;
                fields[maximum+type]=280+type;
            }
            std::vector<std::string> events;
            s.handler().setAddonEventCallback([&](const auto& event, const auto&) { events.push_back(event); });
            s.receive(unitFields(fields,true,profile.preWotlk));
            auto unit=playerUnit(s);
            REQUIRE(unit);
            CHECK(unit->getHealth()==180);
            CHECK(unit->getMaxHealth()==300);
            CHECK(unit->getLevel()==9);
            CHECK(unit->getFactionTemplate()==12);
            for (uint8_t type=0; type<7; ++type) {
                CAPTURE(type);
                CHECK(unit->getPowerByType(type)==(type<profile.powers ? 80+type : 0));
                CHECK(unit->getMaxPowerByType(type)==(type<profile.powers ? 280+type : 0));
            }
            CHECK(std::find(events.begin(),events.end(),"UNIT_MAXMANA")!=events.end());
            wowee::addons::AddonManager manager;
            REQUIRE(manager.initialize(&s.handler()));
            auto* lua=manager.getLuaEngine();
            REQUIRE(lua);
            CHECK(lua->evaluateBoolean("UnitMana('player') == 80 and UnitManaMax('player') == 280"));

            // A maximum-only update must not land in a non-existent sixth
            // or seventh current-power slot on Classic/TBC.
            events.clear();
            std::map<uint16_t,uint32_t> maxima;
            for (uint8_t type=0; type<profile.powers; ++type) maxima[maximum+type]=400+type;
            s.receive(unitFields(maxima,false,profile.preWotlk));
            for (uint8_t type=0; type<7; ++type) {
                CAPTURE(type);
                CHECK(unit->getPowerByType(type)==(type<profile.powers ? 80+type : 0));
                CHECK(unit->getMaxPowerByType(type)==(type<profile.powers ? 400+type : 0));
            }
            CHECK(std::find(events.begin(),events.end(),"UNIT_MAXMANA")!=events.end());
            CHECK(std::find(events.begin(),events.end(),"UNIT_MAXRAGE")!=events.end());
            CHECK(std::find(events.begin(),events.end(),"UNIT_MANA")==events.end());
            CHECK(lua->evaluateBoolean("UnitManaMax('player') == 400"));
            events.clear();
            std::map<uint16_t,uint32_t> currents;
            for (uint8_t type=0; type<profile.powers; ++type) currents[power+type]=40+type;
            s.receive(unitFields(currents,false,profile.preWotlk));
            CHECK(std::find(events.begin(),events.end(),"UNIT_MANA")!=events.end());
            CHECK(std::find(events.begin(),events.end(),"UNIT_RAGE")!=events.end());
            CHECK(std::find(events.begin(),events.end(),"UNIT_MAXMANA")==events.end());
            CHECK(lua->evaluateBoolean("UnitMana('player') == 40 and UnitManaMax('player') == 400"));
            s.receive(unitFields({{power,0}},false,profile.preWotlk));
            CHECK(lua->evaluateBoolean("UnitMana('player') == 0 and UnitManaMax('player') == 400"));
            CHECK(unit->getHealth()==180);
            CHECK(unit->getMaxHealth()==300);
            CHECK(unit->getLevel()==9);
            CHECK(unit->getFactionTemplate()==12);
        }
    }
}

TEST_CASE("a values block changes only the fields it carries", "[replay]") {
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 181},
                   {UF::UNIT_FIELD_MAXHEALTH, 181},
                   {UF::UNIT_FIELD_LEVEL, 9}});
    s.receive(UpdateObject().values(Session::kPlayerGuid, {{UF::UNIT_FIELD_HEALTH, 65}}));

    auto unit = playerUnit(s);
    REQUIRE(unit);
    CHECK(unit->getHealth() == 65);
    CHECK(unit->getMaxHealth() == 181);
}

// AzerothCore's Player::SetRestState and RemoveRestState set and clear
// PLAYER_FLAGS_RESTING (0x20).
TEST_CASE("resting follows the resting player flag", "[replay]") {
    Session s;
    s.spawnPlayer({{UF::PLAYER_FLAGS, 0x20}});
    CHECK(s.handler().isPlayerResting());

    s.receive(UpdateObject().values(Session::kPlayerGuid, {{UF::PLAYER_FLAGS, 0}}));
    CHECK_FALSE(s.handler().isPlayerResting());
}

// Player::SendInitialPacketsBeforeAddToMap: the packed game time, the speed,
// and a zero added in 3.1.2.
TEST_CASE("the login time sets the game clock", "[replay]") {
    Session s;
    wowee::game::WowDate now;
    now.hour = 13;
    now.minute = 30;
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_LOGIN_SETTIMESPEED));
    p.writeUInt32(wowee::game::packWowPackedTime(now));
    p.writeFloat(0.01666667f);
    p.writeUInt32(0);
    s.receive(p);

    CHECK(s.handler().getGameTime() == Catch::Approx(13.5f).margin(0.01f));
}

TEST_CASE("a refused Auto Shot press stops the repeat it started", "[replay]") {
    Session s;
    huntMob(s);
    s.handler().castSpell(kAutoShot, kMob);
    REQUIRE(s.handler().getAutoRepeatSpellId() == kAutoShot);

    s.receive(castFailed(lastCastCount(s), kAutoShot, kOutOfRange));
    CHECK(s.handler().getAutoRepeatSpellId() == 0);
}

TEST_CASE("a refused second Auto Shot press leaves the first one repeating", "[replay]") {
    Session s;
    huntMob(s);
    s.handler().castSpell(kAutoShot, kMob);
    s.handler().castSpell(kAutoShot, kMob);

    s.receive(castFailed(lastCastCount(s), kAutoShot, kNotReady));
    CHECK(s.handler().getAutoRepeatSpellId() == kAutoShot);
}

// Unit::InterruptSpell sends it for whoever stopped shooting.
TEST_CASE("only the player's own auto repeat cancel stops Auto Shot", "[replay]") {
    Session s;
    huntMob(s);
    s.handler().castSpell(kAutoShot, kMob);

    s.receive(autoRepeatCancelled(kOtherHunter));
    CHECK(s.handler().getAutoRepeatSpellId() == kAutoShot);

    s.receive(autoRepeatCancelled(Session::kPlayerGuid));
    CHECK(s.handler().getAutoRepeatSpellId() == 0);
}

constexpr uint32_t kArcaneShot = 3044;

TEST_CASE("a refused cast takes back the GCD it started", "[replay]") {
    Session s;
    huntMob(s);
    s.handler().castSpell(kArcaneShot, kMob);
    REQUIRE(s.handler().isGCDActive());

    s.receive(castFailed(lastCastCount(s), kArcaneShot, kOutOfRange));
    CHECK_FALSE(s.handler().isGCDActive());
}

TEST_CASE("a press refused during the GCD leaves the GCD running", "[replay]") {
    Session s;
    huntMob(s);
    s.handler().castSpell(kArcaneShot, kMob);
    s.handler().castSpell(kArcaneShot, kMob);

    s.receive(castFailed(lastCastCount(s), kArcaneShot, kNotReady));
    CHECK(s.handler().isGCDActive());
}

namespace {

constexpr uint64_t kPet = 0xF140000000000051ull;

// Player::PetSpellInitialize up to the spell count: guid, family, duration,
// react and command states, flags, the ten action bar slots.
wowee::network::Packet petSpells(uint64_t pet) {
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_PET_SPELLS));
    p.writeUInt64(pet);
    p.writeUInt16(0);
    p.writeUInt32(0);
    p.writeUInt8(1);
    p.writeUInt8(1);
    p.writeUInt16(0);
    for (int i = 0; i < 10; ++i) p.writeUInt32(0);
    p.writeUInt8(0);
    return p;
}

bool petCanBeAbandoned(Session& s) {
    lua_State* L = luaL_newstate();
    lua_pushlightuserdata(L, &s.handler());
    lua_setfield(L, LUA_REGISTRYINDEX, "wowee_game_handler");
    wowee::addons::registerUnitLuaAPI(L);
    REQUIRE(luaL_dostring(L, "return PetCanBeAbandoned()") == 0);
    const bool result = lua_toboolean(L, -1) != 0;
    lua_close(L);
    return result;
}

}  // namespace

namespace {

constexpr uint64_t kWolf = 0xF130000000000321ull;

// Unit::SendAttackStateUpdate for one swing of a single physical sub-damage.
wowee::network::Packet swing(uint64_t attacker, uint64_t victim, uint32_t damage, uint8_t victimState,
                             uint32_t hitInfo = 0x2 /* HITINFO_AFFECTS_VICTIM */, uint32_t blocked = 0) {
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_ATTACKERSTATEUPDATE));
    p.writeUInt32(hitInfo);
    p.writePackedGuid(attacker);
    p.writePackedGuid(victim);
    p.writeUInt32(damage);
    p.writeUInt32(0);
    p.writeUInt8(1);
    p.writeUInt32(1);  // physical
    p.writeFloat(static_cast<float>(damage));
    p.writeUInt32(damage);
    p.writeUInt8(victimState);
    p.writeUInt32(0);
    p.writeUInt32(0);
    if (hitInfo & 0x2000) p.writeUInt32(blocked);  // HITINFO_BLOCK
    return p;
}

std::vector<wowee::game::CombatTextEntry::Type> combatTextTypes(Session& s) {
    std::vector<wowee::game::CombatTextEntry::Type> types;
    for (const auto& e : s.handler().getCombatText()) types.push_back(e.type);
    return types;
}

}  // namespace

// Pet::CreateBaseAtTamed and Pet::LoadPetFromDB set UNIT_CAN_BE_ABANDONED
// (0x02) in UNIT_FIELD_BYTES_2 byte 2 on a hunter's beast, and nothing else
// sets it.
TEST_CASE("PetCanBeAbandoned is true for a tamed pet only", "[replay]") {
    for (const bool tamed : {true, false}) {
        Session s;
        s.spawnPlayer({{UF::UNIT_FIELD_LEVEL, 10}});
        s.receive(UpdateObject().create(
            kPet, wowee::game::ObjectType::UNIT,
            {{UF::UNIT_FIELD_HEALTH, 100}, {UF::UNIT_FIELD_BYTES_2, tamed ? 0x03u << 16 : 0u}}));
        s.receive(petSpells(kPet));
        REQUIRE(s.handler().getPetGuid() == kPet);

        CHECK(petCanBeAbandoned(s) == tamed);
    }
}

// WorldPackets::Misc::Weather: the WeatherState, its intensity, and whether
// it changes at once. Weather::GetWeatherState picks the state.
TEST_CASE("weather states read as what the server means", "[replay]") {
    using wowee::game::WeatherKind;
    struct Case { uint32_t state; WeatherKind kind; bool raining; bool snowing; };
    const Case cases[] = {
        {0, WeatherKind::NONE, false, false},
        {1, WeatherKind::FOG, false, false},
        {3, WeatherKind::RAIN, true, false},
        {4, WeatherKind::RAIN, true, false},
        {5, WeatherKind::RAIN, true, false},
        {6, WeatherKind::SNOW, false, true},
        {7, WeatherKind::SNOW, false, true},
        {8, WeatherKind::SNOW, false, true},
        {22, WeatherKind::SANDSTORM, false, false},
        {41, WeatherKind::SANDSTORM, false, false},
        {42, WeatherKind::SANDSTORM, false, false},
        {86, WeatherKind::THUNDERSTORM, true, false},
        {90, WeatherKind::RAIN, true, false},
        {106, WeatherKind::SNOW, false, true},
    };
    for (const Case& c : cases) {
        CAPTURE(c.state);
        Session s;
        wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_WEATHER));
        p.writeUInt32(c.state);
        p.writeFloat(0.5f);
        p.writeUInt8(0);
        s.receive(p);

        CHECK(s.handler().getWeatherKind() == c.kind);
        CHECK(s.handler().isRaining() == c.raining);
        CHECK(s.handler().isSnowing() == c.snowing);
    }
}

namespace {

wowee::network::Packet weather(uint32_t state, float intensity) {
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_WEATHER));
    p.writeUInt32(state);
    p.writeFloat(intensity);
    p.writeUInt8(0);
    return p;
}

std::vector<std::string> chatLines(Session& s) {
    std::vector<std::string> lines;
    for (const auto& m : s.handler().getChatHistory()) lines.push_back(m.message);
    return lines;
}

}  // namespace

TEST_CASE("weather is announced once per kind, and clears only after weather", "[replay]") {
    Session s;
    s.receive(weather(3, 0.3f));
    s.receive(weather(5, 0.9f));
    s.receive(weather(0, 0.0f));
    s.receive(weather(1, 0.5f));
    s.receive(weather(0, 0.0f));
    CHECK(chatLines(s) == std::vector<std::string>{"It begins to rain.", "The weather clears."});
}

TEST_CASE("faint rain is not rain", "[replay]") {
    Session s;
    s.receive(weather(4, 0.01f));
    CHECK_FALSE(s.handler().isRaining());
}

// AuraEffect::HandleFeignDeath sets UNIT_FLAG2_FEIGN_DEATH and
// UNIT_DYNFLAG_DEAD together, and removes them together.
TEST_CASE("Feign Death is not a death", "[replay]") {
    Session s;
    std::vector<std::string> events;
    s.handler().setAddonEventCallback(
        [&events](const std::string& name, const std::vector<std::string>&) { events.push_back(name); });
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});

    s.receive(UpdateObject().values(Session::kPlayerGuid,
                                    {{UF::UNIT_FIELD_FLAGS_2, 0x1}, {UF::UNIT_DYNAMIC_FLAGS, 0x20}}));
    CHECK_FALSE(s.handler().isPlayerDead());

    s.receive(UpdateObject().values(Session::kPlayerGuid,
                                    {{UF::UNIT_FIELD_FLAGS_2, 0}, {UF::UNIT_DYNAMIC_FLAGS, 0}}));
    CHECK_FALSE(s.handler().isPlayerDead());
    CHECK(std::count(events.begin(), events.end(), "PLAYER_DEAD") == 0);
    CHECK(std::count(events.begin(), events.end(), "PLAYER_ALIVE") == 0);
}

TEST_CASE("the dead flag at zero health is still a death", "[replay]") {
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(UpdateObject().values(Session::kPlayerGuid,
                                    {{UF::UNIT_FIELD_HEALTH, 0}, {UF::UNIT_DYNAMIC_FLAGS, 0x20}}));
    CHECK(s.handler().isPlayerDead());
}

// vmangos's Unit::SetFeignDeath sets the dead flag alone.
TEST_CASE("the dead flag at full health is Feign Death without its flags2 bit", "[replay]") {
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(UpdateObject().values(Session::kPlayerGuid, {{UF::UNIT_DYNAMIC_FLAGS, 0x20}}));
    CHECK_FALSE(s.handler().isPlayerDead());
}

TEST_CASE("killed while feigning stays dead when the feign ends", "[replay]") {
    Session s;
    std::vector<std::string> events;
    s.handler().setAddonEventCallback(
        [&events](const std::string& name, const std::vector<std::string>&) { events.push_back(name); });
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(UpdateObject().values(Session::kPlayerGuid,
                                    {{UF::UNIT_FIELD_FLAGS_2, 0x1}, {UF::UNIT_DYNAMIC_FLAGS, 0x20}}));
    s.receive(UpdateObject().values(Session::kPlayerGuid, {{UF::UNIT_FIELD_HEALTH, 0}}));
    s.receive(UpdateObject().values(Session::kPlayerGuid,
                                    {{UF::UNIT_FIELD_FLAGS_2, 0}, {UF::UNIT_DYNAMIC_FLAGS, 0}}));
    CHECK(s.handler().isPlayerDead());
    CHECK(std::count(events.begin(), events.end(), "PLAYER_ALIVE") == 0);
}

// A player created while feigning, as after a relog or a teleport mid-feign.
TEST_CASE("a player created feigning is alive, and stays so when it ends", "[replay]") {
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300},
                   {UF::UNIT_FIELD_MAXHEALTH, 300},
                   {UF::UNIT_FIELD_FLAGS_2, 0x1},
                   {UF::UNIT_DYNAMIC_FLAGS, 0x20}});
    CHECK_FALSE(s.handler().isPlayerDead());
    s.receive(UpdateObject().values(Session::kPlayerGuid,
                                    {{UF::UNIT_FIELD_FLAGS_2, 0}, {UF::UNIT_DYNAMIC_FLAGS, 0}}));
    CHECK_FALSE(s.handler().isPlayerDead());
}

TEST_CASE("a wolf's hit shows its damage", "[replay]") {
    using T = wowee::game::CombatTextEntry;
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(swing(kWolf, Session::kPlayerGuid, 12, 1));
    REQUIRE(s.handler().getCombatText().size() == 1);
    CHECK(s.handler().getCombatText()[0].type == T::MELEE_DAMAGE);
    CHECK(s.handler().getCombatText()[0].amount == 12);
}

TEST_CASE("a dodged swing shows a dodge", "[replay]") {
    using T = wowee::game::CombatTextEntry;
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(swing(Session::kPlayerGuid, kWolf, 0, 2));
    CHECK(combatTextTypes(s) == std::vector<T::Type>{T::DODGE});
}

TEST_CASE("a parried wolf shows a parry", "[replay]") {
    using T = wowee::game::CombatTextEntry;
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(swing(kWolf, Session::kPlayerGuid, 0, 3));
    CHECK(combatTextTypes(s) == std::vector<T::Type>{T::PARRY});
}

// Unit::CalcDamageInfo sends an evade as a miss with VICTIMSTATE_EVADES.
TEST_CASE("an evading wolf shows an evade, not a miss", "[replay]") {
    using T = wowee::game::CombatTextEntry;
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(swing(Session::kPlayerGuid, kWolf, 0, 6, 0x2 | 0x10 | 0x200000));
    CHECK(combatTextTypes(s) == std::vector<T::Type>{T::EVADE});
}

TEST_CASE("a partly blocked hit shows its damage and the block", "[replay]") {
    using T = wowee::game::CombatTextEntry;
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(swing(kWolf, Session::kPlayerGuid, 7, 1, 0x2 | 0x2000, 5));
    CHECK(combatTextTypes(s) == std::vector<T::Type>{T::MELEE_DAMAGE, T::BLOCK});
}

TEST_CASE("a fully blocked hit shows only the block", "[replay]") {
    using T = wowee::game::CombatTextEntry;
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_HEALTH, 300}, {UF::UNIT_FIELD_MAXHEALTH, 300}});
    s.receive(swing(kWolf, Session::kPlayerGuid, 0, 5, 0x2 | 0x2000, 12));
    CHECK(combatTextTypes(s) == std::vector<T::Type>{T::BLOCK});
}

namespace {

constexpr uint32_t kRaptorStrike = 2973;

// Spell::SendSpellStart for the player's own spell on no particular target.
wowee::network::Packet spellStart(uint8_t castCount, uint32_t spellId, uint32_t castTimeMs) {
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_SPELL_START));
    p.writePackedGuid(Session::kPlayerGuid);
    p.writePackedGuid(Session::kPlayerGuid);
    p.writeUInt8(castCount);
    p.writeUInt32(spellId);
    p.writeUInt32(0);  // cast flags
    p.writeUInt32(castTimeMs);
    p.writeUInt32(0);  // target flags: none
    return p;
}

}  // namespace

// Spell::SendCastResult writes the press's castCount; the server's own casts,
// such as each Auto Shot after the first, have none and send 0.
TEST_CASE("a press never goes out with castCount 0", "[replay]") {
    Session s;
    huntMob(s);
    for (int i = 0; i < 255; ++i) s.handler().castSpell(kArcaneShot, kMob);
    s.handler().castSpell(kAutoShot, kMob);
    CHECK(lastCastCount(s) != 0);

    s.receive(castFailed(0, kAutoShot, kOutOfRange));
    CHECK(s.handler().getAutoRepeatSpellId() == kAutoShot);
}

TEST_CASE("a refused second press leaves the queued swing ability lit", "[replay]") {
    Session s;
    huntMob(s);
    auto& cache = s.handler().spellNameCacheRef();
    cache[kRaptorStrike].attr = 0x4;  // SPELL_ATTR0_ON_NEXT_SWING
    s.handler().spellNameCacheLoadedRef() = true;

    s.handler().castSpell(kRaptorStrike, kMob);
    const uint8_t queued = lastCastCount(s);
    s.receive(spellStart(queued, kRaptorStrike, 0));
    REQUIRE(s.handler().getActiveActionSpellId() == kRaptorStrike);

    s.handler().castSpell(kRaptorStrike, kMob);
    s.receive(castFailed(lastCastCount(s), kRaptorStrike, kNotReady));
    CHECK(s.handler().getActiveActionSpellId() == kRaptorStrike);

    s.receive(castFailed(queued, kRaptorStrike, kOutOfRange));
    CHECK(s.handler().getActiveActionSpellId() == 0);
}

namespace {

bool petCanBeRenamed(Session& s) {
    lua_State* L = luaL_newstate();
    lua_pushlightuserdata(L, &s.handler());
    lua_setfield(L, LUA_REGISTRYINDEX, "wowee_game_handler");
    wowee::addons::registerSocialLuaAPI(L);
    REQUIRE(luaL_dostring(L, "return PetCanBeRenamed()") == 0);
    const bool result = lua_toboolean(L, -1) != 0;
    lua_close(L);
    return result;
}

}  // namespace

// Pet::LoadPetFromDB sets UNIT_CAN_BE_RENAMED (0x01) beside
// UNIT_CAN_BE_ABANDONED only on a pet never named, and
// WorldSession::HandlePetRename clears it.
TEST_CASE("PetCanBeRenamed follows the pet's rename flag", "[replay]") {
    Session s;
    s.spawnPlayer({{UF::UNIT_FIELD_LEVEL, 10}});
    s.receive(UpdateObject().create(kPet, wowee::game::ObjectType::UNIT,
                                    {{UF::UNIT_FIELD_HEALTH, 100}, {UF::UNIT_FIELD_BYTES_2, 0x03u << 16}}));
    s.receive(petSpells(kPet));
    CHECK(petCanBeRenamed(s));

    s.receive(UpdateObject().values(kPet, {{UF::UNIT_FIELD_BYTES_2, 0x02u << 16}}));
    CHECK_FALSE(petCanBeRenamed(s));
}

namespace {

// WorldObject::SendSpellMiss: the spell, the caster's guid whole, an unknown
// byte, one target, its guid whole, and how it missed (SpellMissInfo).
wowee::network::Packet spellMiss(uint64_t caster, uint64_t victim, uint32_t spellId, uint8_t missInfo) {
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_SPELLLOGMISS));
    p.writeUInt32(spellId);
    p.writeUInt64(caster);
    p.writeUInt8(0);
    p.writeUInt32(1);
    p.writeUInt64(victim);
    p.writeUInt8(missInfo);
    return p;
}

std::vector<wowee::game::CombatTextEntry::Type> spellMissText(uint64_t caster, uint64_t victim,
                                                               uint8_t missInfo) {
    Session s;
    huntMob(s);
    // Resists and immunities show only on the target (fctSpellMechanics).
    s.handler().setTarget(kMob);
    s.receive(spellMiss(caster, victim, kArcaneShot, missInfo));
    std::vector<wowee::game::CombatTextEntry::Type> types;
    for (const auto& e : s.handler().getCombatText()) types.push_back(e.type);
    return types;
}

}  // namespace

namespace {

// SpellMissInfo, as every server family numbers it.
const std::vector<std::pair<uint8_t, wowee::game::CombatTextEntry::Type>>& missTypes() {
    using T = wowee::game::CombatTextEntry;
    static const std::vector<std::pair<uint8_t, T::Type>> table = {
        {1, T::MISS},   {2, T::RESIST}, {3, T::DODGE},   {4, T::PARRY},  {5, T::BLOCK},
        {6, T::EVADE},  {7, T::IMMUNE}, {8, T::IMMUNE},  {9, T::DEFLECT}, {10, T::ABSORB},
    };
    return table;
}

}  // namespace

TEST_CASE("a spell miss shows what the server says happened", "[replay]") {
    for (const auto& [missInfo, type] : missTypes()) {
        CAPTURE(missInfo);
        // The player's own misses, against the target, where every kind shows.
        CHECK(spellMissText(Session::kPlayerGuid, kMob, missInfo) ==
              std::vector<wowee::game::CombatTextEntry::Type>{type});
    }
}

namespace {

// Spell::SendSpellGo with no hits and one miss: caster and unit packed, the
// cast count, spell, flags and time, no hit targets, one miss target whole
// with its SpellMissInfo, and an empty target mask.
wowee::network::Packet spellGoMiss(uint64_t victim, uint32_t spellId, uint8_t missInfo) {
    wowee::network::Packet p(wowee::game::wireOpcode(wowee::game::Opcode::SMSG_SPELL_GO));
    p.writePackedGuid(Session::kPlayerGuid);
    p.writePackedGuid(Session::kPlayerGuid);
    p.writeUInt8(1);
    p.writeUInt32(spellId);
    p.writeUInt32(0);
    p.writeUInt32(0);
    p.writeUInt8(0);
    p.writeUInt8(1);
    p.writeUInt64(victim);
    p.writeUInt8(missInfo);
    p.writeUInt32(0);
    return p;
}

}  // namespace

TEST_CASE("a spell go miss shows what the server says happened", "[replay]") {
    for (const auto& [missInfo, type] : missTypes()) {
        CAPTURE(missInfo);
        Session s;
        huntMob(s);
        s.handler().setTarget(kMob);
        s.receive(spellGoMiss(kMob, kArcaneShot, missInfo));
        std::vector<wowee::game::CombatTextEntry::Type> types;
        for (const auto& e : s.handler().getCombatText()) types.push_back(e.type);
        CHECK(types == std::vector<wowee::game::CombatTextEntry::Type>{type});
    }
}
