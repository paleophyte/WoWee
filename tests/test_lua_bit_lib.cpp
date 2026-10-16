// WoW's `bit` library as this client supplies it, run in plain Lua.
#include <catch_amalgamated.hpp>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <string>

#include "addons/lua_bit_lib.hpp"

namespace {

struct Fixture {
    lua_State* L = luaL_newstate();

    Fixture() {
        for (const lua_CFunction open : {luaopen_base, luaopen_math}) {
            lua_pushcfunction(L, open);
            lua_call(L, 0, 0);
        }
        REQUIRE(luaL_dostring(L, wowee::addons::kBitLibLua) == 0);
    }
    ~Fixture() { lua_close(L); }

    double eval(const std::string& expr) {
        const std::string chunk = "return " + expr;
        INFO(chunk);
        REQUIRE(luaL_dostring(L, chunk.c_str()) == 0);
        const double v = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return v;
    }
};

}  // namespace

TEST_CASE("bor, band and bxor take every argument they are given", "[lua][bit]") {
    Fixture f;
    // COMBATLOG_FILTER_MINE, as constants.lua builds it.
    CHECK(f.eval("bit.bor(0x1, 0x10, 0x100, 0x400, 0x4000)") == 0x4511);
    CHECK(f.eval("bit.band(0xFF, 0x0F, 0x3)") == 0x3);
    CHECK(f.eval("bit.bxor(1, 2, 4, 1)") == 6);
}

TEST_CASE("two arguments behave as before", "[lua][bit]") {
    Fixture f;
    CHECK(f.eval("bit.bor(0x80000000, 1)") == 2147483649.0);
    CHECK(f.eval("bit.band(0xF0, 0x3C)") == 0x30);
    CHECK(f.eval("bit.bxor(0xFF, 0x0F)") == 0xF0);
    CHECK(f.eval("bit.band(-1, -1)") == 4294967295.0);
    CHECK(f.eval("bit.bor(2^32 + 1, 0)") == 1);
}

TEST_CASE("one argument comes back in 32 bits", "[lua][bit]") {
    Fixture f;
    CHECK(f.eval("bit.bor(5)") == 5);
    CHECK(f.eval("bit.band(5)") == 5);
    CHECK(f.eval("bit.bor(-1)") == 4294967295.0);
    CHECK(f.eval("bit.bor(2^33 + 1)") == 1);
}

// An undefined constant must fail loudly, not turn into a plausible mask.
TEST_CASE("a nil argument raises wherever it is", "[lua][bit]") {
    Fixture f;
    for (const char* call : {"bit.band(6, nil)", "bit.bor(1, nil, 4)", "bit.bxor(1, 2, nil)", "bit.bor(nil)"}) {
        INFO(call);
        CHECK(luaL_dostring(f.L, (std::string("return ") + call).c_str()) != 0);
        lua_settop(f.L, 0);
    }
}
