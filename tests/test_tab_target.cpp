// Which units Tab and TargetNearestEnemy may pick.
//
// The wolves at the Dun Morogh start carry a faction template with no
// hostility in it, so they are neutral rather than hostile. Asking for hostile
// units alone found none of them, and Tab cleared the target instead.
#include <catch_amalgamated.hpp>

#include "game/tab_target.hpp"

using namespace wowee::game;

namespace {
constexpr uint32_t kBeast = 1;
}

TEST_CASE("a hostile unit is a tab target", "[tab-target]") {
    CHECK(isTabTargetable(true, false, false, kBeast, 0));
}

TEST_CASE("a neutral unit is a tab target", "[tab-target]") {
    CHECK(isTabTargetable(false, false, false, kBeast, 0));
}

TEST_CASE("a friendly unit is not a tab target", "[tab-target]") {
    CHECK_FALSE(isTabTargetable(false, true, false, kBeast, 0));
}

TEST_CASE("a friendly unit attacking the player is a tab target", "[tab-target]") {
    CHECK(isTabTargetable(false, true, true, kBeast, 0));
}

TEST_CASE("a neutral critter is not a tab target", "[tab-target]") {
    CHECK_FALSE(isTabTargetable(false, false, false, CREATURE_TYPE_CRITTER, 0));
}

TEST_CASE("a hostile critter is a tab target", "[tab-target]") {
    CHECK(isTabTargetable(true, false, false, CREATURE_TYPE_CRITTER, 0));
}

TEST_CASE("a unit whose creature type has not arrived is a tab target", "[tab-target]") {
    CHECK(isTabTargetable(false, false, false, 0, 0));
}

TEST_CASE("a unit that cannot be selected or attacked is never a tab target", "[tab-target]") {
    for (const uint32_t flag : {UNIT_FLAG_NOT_SELECTABLE, UNIT_FLAG_NON_ATTACKABLE,
                                UNIT_FLAG_IMMUNE_TO_PC}) {
        CHECK_FALSE(isTabTargetable(true, false, true, kBeast, flag));
        CHECK_FALSE(isTabTargetable(false, false, false, kBeast, flag));
    }
}
