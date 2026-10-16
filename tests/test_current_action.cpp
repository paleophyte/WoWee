#include <catch_amalgamated.hpp>

#include "game/current_action.hpp"

using wowee::game::isCurrentSpellAction;
using wowee::game::isOnNextSwingSpell;
using wowee::game::SPELL_ID_ATTACK;

// IsCurrentAction answered false for every slot, so a button never showed its
// checked state while its spell was being cast.

TEST_CASE("the slot holding the spell being cast is current", "[current_action]") {
    REQUIRE(isCurrentSpellAction(133, 133, false));
}

TEST_CASE("other slots are not current while a spell is cast", "[current_action]") {
    REQUIRE_FALSE(isCurrentSpellAction(116, 133, false));
}

TEST_CASE("nothing is current when nothing is cast", "[current_action]") {
    REQUIRE_FALSE(isCurrentSpellAction(133, 0, false));
}

TEST_CASE("an empty spell id never matches an idle cast", "[current_action]") {
    REQUIRE_FALSE(isCurrentSpellAction(0, 0, false));
}

TEST_CASE("auto attack is current while the swing is on", "[current_action]") {
    REQUIRE(isCurrentSpellAction(SPELL_ID_ATTACK, 0, true));
    REQUIRE(isCurrentSpellAction(SPELL_ID_ATTACK, 133, true));
}

TEST_CASE("auto attack is not current once the swing stops", "[current_action]") {
    REQUIRE_FALSE(isCurrentSpellAction(SPELL_ID_ATTACK, 0, false));
}

TEST_CASE("the swing does not make a cast spell current", "[current_action]") {
    REQUIRE_FALSE(isCurrentSpellAction(133, 0, true));
}

// Raptor Strike and Heroic Strike start with a zero cast time and wait for the
// next swing; their buttons stayed unlit for that wait. Attribute values are
// 3.3.5a Spell.dbc's.
TEST_CASE("on-next-swing abilities are recognised from Spell.dbc attributes", "[current_action]") {
    REQUIRE(isOnNextSwingSpell(0x00050404));  // Raptor Strike (2973)
    REQUIRE(isOnNextSwingSpell(0x00050014));  // Heroic Strike (78)
}

TEST_CASE("ordinary abilities are not on-next-swing", "[current_action]") {
    REQUIRE_FALSE(isOnNextSwingSpell(0x00000010));  // Attack (6603)
    REQUIRE_FALSE(isOnNextSwingSpell(0x00040100));  // Stoneform (20594)
    REQUIRE_FALSE(isOnNextSwingSpell(0x29040010));  // Find Treasure (2481)
}

TEST_CASE("an ability queued for the next swing is current", "[current_action]") {
    REQUIRE(isCurrentSpellAction(2973, 2973, true));
    REQUIRE_FALSE(isCurrentSpellAction(78, 2973, true));
}
