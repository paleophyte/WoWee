#pragma once
#include <string>
#include <nlohmann/json.hpp>
struct lua_State;
namespace wowee::addons {
nlohmann::json dispatchLocalControl(lua_State* state, bool inWorld,
                                   const std::string& command, const std::string& id);
}
