// tab_target.hpp - which units Tab and TargetNearestEnemy may pick.
//
// "Enemy" here means anything the player can attack, not only what is hostile.
// Neutral wildlife counts: the wolves around Coldridge Valley carry a faction
// template with no hostility at all, so asking for hostile units alone left Tab
// with nothing to cycle and it cleared the target instead. Critters are the
// exception the real client makes, and a unit the server marks as not
// selectable, not attackable or immune to players is skipped whatever its
// faction says.
#pragma once

#include "game/protocol_constants.hpp"

#include <cstdint>

namespace wowee {
namespace game {

/// Creature type 8 in CreatureType.dbc.
constexpr uint32_t CREATURE_TYPE_CRITTER = 8;

[[nodiscard]] constexpr bool isTabTargetable(bool hostile, bool friendly,
                                             bool attackingPlayer,
                                             uint32_t creatureType,
                                             uint32_t unitFlags) {
    constexpr uint32_t kUntargetable =
        UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC;
    if (unitFlags & kUntargetable) return false;
    if (hostile || attackingPlayer) return true;
    if (friendly) return false;
    return creatureType != CREATURE_TYPE_CRITTER;
}

} // namespace game
} // namespace wowee
