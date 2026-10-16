#pragma once

namespace wowee::addons {

/// WoW's `bit` library, which Lua 5.1 does not have, on 32 bits.
///
/// band, bor and bxor take any number of arguments, as WoW's do. FrameXML
/// builds its combat log masks that way: COMBATLOG_FILTER_MINE is bit.bor of
/// five flags, and a two-argument bor kept only the first two, so the mask
/// came out as AFFILIATION_MINE + REACTION_FRIENDLY and every filter built
/// from these constants asked a different question from the one it states.
inline constexpr const char* kBitLibLua = R"LUA(
bit = bit or {}
local floor = math.floor
local function band2(a, b) local r,m=0,1 for i=0,31 do if a%2==1 and b%2==1 then r=r+m end a=floor(a/2) b=floor(b/2) m=m*2 end return r end
local function bor2(a, b) local r,m=0,1 for i=0,31 do if a%2==1 or b%2==1 then r=r+m end a=floor(a/2) b=floor(b/2) m=m*2 end return r end
local function bxor2(a, b) local r,m=0,1 for i=0,31 do if (a%2==1)~=(b%2==1) then r=r+m end a=floor(a/2) b=floor(b/2) m=m*2 end return r end
-- Folds the two-argument form over every argument. A lone argument meets the
-- operation's identity, so it comes back in 32 bits like any other result,
-- and a nil anywhere raises, as it did with two.
local function variadic(op2, identity)
    local f
    f = function(a, ...)
        local n = select('#', ...)
        if n == 0 then return op2(identity, a) end
        local r = op2(a, (...))
        if n == 1 then return r end
        return f(r, select(2, ...))
    end
    return f
end
bit.band = bit.band or variadic(band2, 4294967295)
bit.bor = bit.bor or variadic(bor2, 0)
bit.bxor = bit.bxor or variadic(bxor2, 0)
bit.bnot = bit.bnot or function(a) return 4294967295 - a end
bit.lshift = bit.lshift or function(a, n) return a * (2^n) end
bit.rshift = bit.rshift or function(a, n) return math.floor(a / (2^n)) end
)LUA";

} // namespace wowee::addons
