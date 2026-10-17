#include "addons/lua_handler_globals.hpp"

#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace wowee::addons {

namespace {

bool gLegacyHandlerGlobals = false;

// Names are cached as longer event payloads arrive. TBC chat reads arg11/12,
// and combat events can carry more; publish the actual payload without truncation.
std::vector<std::string> gHandlerArgNames;
char gHandlerSaveKey;

/// Read a global without the missing-API fallback answering for it.
///
/// lua_getglobal goes through _G's __index, which with that fallback installed
/// hands back a callable stand-in for a name nothing has defined - and what is
/// wanted here is the value to put back afterwards, which for an undefined name
/// is nothing at all.
void rawGetGlobal(lua_State* L, const char* name) {
    lua_pushstring(L, name);
    lua_rawget(L, LUA_GLOBALSINDEX);
}

// Registry ownership is per Lua state, including when an allocator reuses a
// destroyed state's address. Rows/depth belong to that state too.
void pushSaveTable(lua_State* L) {
    lua_pushlightuserdata(L, &gHandlerSaveKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    if (lua_istable(L, -1)) return;
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushlightuserdata(L, &gHandlerSaveKey);
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

// Each nested handler has a reusable row, with no fixed-width slot overlap
// when an inner event carries more arguments than the outer event.
int pushSaveRow(lua_State* L, bool entering, int depth = -1) {
    pushSaveTable(L);
    const int table = lua_gettop(L);
    if (depth < 0) {
        lua_rawgeti(L, table, 0);
        depth = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    lua_pushinteger(L, entering ? depth + 1 : depth);
    lua_rawseti(L, table, 0);
    lua_rawgeti(L, table, depth + 1);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawseti(L, table, depth + 1);
    }
    lua_remove(L, table);
    return depth;
}

void clearSaved(lua_State* L, int row, int slot) {
    lua_pushnil(L);
    lua_rawseti(L, row, slot);
}

}  // namespace

void setLegacyHandlerGlobals(bool on) { gLegacyHandlerGlobals = on; }
bool legacyHandlerGlobals() { return gLegacyHandlerGlobals; }

int pcallScript(lua_State* L, const char* script, int nargs, int errfunc) {
    if (!gLegacyHandlerGlobals || nargs < 1)
        return lua_pcall(L, nargs, 0, errfunc);

    // The caller reserves its argument payload. Our registry bookkeeping also
    // needs scratch slots before lua_pcall gets a chance to grow the stack.
    if (!lua_checkstack(L, 8)) {
        lua_pop(L, nargs + 1);
        lua_pushliteral(L, "not enough Lua stack for handler globals");
        return LUA_ERRMEM;
    }

    const int selfIdx = lua_gettop(L) - nargs + 1;
    // OnEvent alone puts the event name before the payload; every other
    // handler's own arguments start at arg1.
    //
    // And only if it was given one. Every dispatch of an OnEvent here passes
    // the name, but reading a stack slot that was never pushed is not an error
    // Lua reports - it hands back whatever is there - so the count decides
    // rather than the handler's name alone.
    const bool isEvent = script && std::strcmp(script, "OnEvent") == 0 && nargs >= 2;
    const int firstArg = selfIdx + (isEvent ? 2 : 1);
    int argCount = lua_gettop(L) - firstArg + 1;
    if (argCount < 0) argCount = 0;
    while (static_cast<int>(gHandlerArgNames.size()) < argCount)
        gHandlerArgNames.push_back("arg" + std::to_string(gHandlerArgNames.size() + 1));

    // Only the names about to be overwritten are saved, and only those are put
    // back. A client before 3.0 left the higher argN wherever the last event
    // had left them, and a body written against one reads no further than the
    // count its own event carries.
    const int depth = pushSaveRow(L, true);
    const int save = lua_gettop(L);
    rawGetGlobal(L, "this");
    lua_rawseti(L, save, 1);
    if (isEvent) {
        rawGetGlobal(L, "event");
        lua_rawseti(L, save, 2);
    }
    for (int i = 0; i < argCount; ++i) {
        rawGetGlobal(L, gHandlerArgNames[i].c_str());
        lua_rawseti(L, save, 3 + i);
    }
    lua_pop(L, 1);   // the save table - the call's own stack is left as it was

    lua_pushvalue(L, selfIdx);
    lua_setglobal(L, "this");
    if (isEvent) {
        lua_pushvalue(L, selfIdx + 1);
        lua_setglobal(L, "event");
    }
    for (int i = 0; i < argCount; ++i) {
        lua_pushvalue(L, firstArg + i);
        lua_setglobal(L, gHandlerArgNames[i].c_str());
    }

    const int rc = lua_pcall(L, nargs, 0, errfunc);

    // Above whatever pcall left - nothing on success, the error on failure -
    // and popped again, so a caller still finds its error at the top.
    pushSaveRow(L, false, depth);
    const int back = lua_gettop(L);
    lua_rawgeti(L, back, 1);
    lua_setglobal(L, "this");
    clearSaved(L, back, 1);
    if (isEvent) {
        lua_rawgeti(L, back, 2);
        lua_setglobal(L, "event");
        clearSaved(L, back, 2);
    }
    for (int i = 0; i < argCount; ++i) {
        lua_rawgeti(L, back, 3 + i);
        lua_setglobal(L, gHandlerArgNames[i].c_str());
        clearSaved(L, back, 3 + i);
    }
    lua_pop(L, 1);
    return rc;
}

int lua_WoweeFireOnLoad(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    lua_getfield(L, 1, "__scripts");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    lua_getfield(L, -1, "OnLoad");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return 0; }
    lua_remove(L, -2);                    // __scripts, leaving the handler
    lua_pushvalue(L, 1);                  // self
    if (!gLegacyHandlerGlobals) {
        lua_call(L, 1, 0);
        return 0;
    }
    // The saved value rides on the stack below the call rather than anywhere it
    // would have to be cleaned up, because a raise here does not return.
    rawGetGlobal(L, "this");
    lua_insert(L, -3);                    // beneath the handler and its argument
    lua_pushvalue(L, 1);
    lua_setglobal(L, "this");
    lua_call(L, 1, 0);
    lua_setglobal(L, "this");             // what was saved, still on the stack
    return 0;
}

}  // namespace wowee::addons
