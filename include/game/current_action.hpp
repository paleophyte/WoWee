#pragma once

#include "game/protocol_constants.hpp"

#include <cstdint>

namespace wowee {
namespace game {

// Spell.dbc Attributes bit for an ability that waits for the next melee swing
// (Heroic Strike, Raptor Strike, Maul). The server starts it with a zero cast
// time and only reports it gone when the swing lands.
constexpr uint32_t SPELL_ATTR0_ON_NEXT_SWING = 0x00000004;

[[nodiscard]] constexpr bool isOnNextSwingSpell(uint32_t attributes) {
    return (attributes & SPELL_ATTR0_ON_NEXT_SWING) != 0;
}

// IsCurrentAction for a spell on the action bar: the spell the player is
// casting, channelling or has queued for the next swing, or Auto Attack while
// the swing is on. activeSpellId is 0 when nothing is cast or queued.
[[nodiscard]] constexpr bool isCurrentSpellAction(uint32_t slotSpellId, uint32_t activeSpellId,
                                                  bool autoAttacking) {
    if (slotSpellId == 0) return false;
    if (slotSpellId == SPELL_ID_ATTACK) return autoAttacking;
    return slotSpellId == activeSpellId;
}

}  // namespace game
}  // namespace wowee
