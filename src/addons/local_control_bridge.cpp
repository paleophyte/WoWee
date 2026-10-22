#include "addons/local_control_bridge.hpp"
extern "C" {
#include <lua.h>
}
namespace wowee::addons {
nlohmann::json dispatchLocalControl(lua_State* L,bool inWorld,const std::string& command,const std::string& id) {
    using Json=nlohmann::json;
    const auto unavailable=[&](const char* outcome) { return Json{{"outcome",outcome},{"request_id",id},{"in_world",inWorld},{"fresh",false},{"control_may_still_be_active",true}}; };
    if (!inWorld && command!="cancel") return unavailable("disconnected");
    if (!L) return unavailable("unavailable");
    struct Stack { lua_State* L; int top;~Stack(){lua_settop(L,top);} } guard{L,lua_gettop(L)};
    auto call=[&](int nargs) {
        // Table is below its function and self/arguments. Retain it for the snapshot.
        return lua_pcall(L,nargs,1,0)==0;
    };
    lua_getglobal(L,"WoweeBotRemoteSession");
    if (!lua_istable(L,-1)) return unavailable("unavailable");
    const int table=lua_gettop(L);
    const bool mutation=command=="bot" || command=="manual" || command=="stop";
    std::string result;
    if (mutation || command=="cancel") {
        lua_getfield(L,table,mutation?"requestControl":"cancel");
        if (!lua_isfunction(L,-1)) return unavailable("unavailable");
        lua_pushvalue(L,table);
        if (mutation) { lua_pushlstring(L,command.data(),command.size()); }
        lua_pushlstring(L,id.data(),id.size());
        if (!call(mutation?3:2)) return unavailable("addon_error");
        if (mutation && lua_isstring(L,-1)) result=lua_tostring(L,-1);
        lua_pop(L,1);
    }
    lua_getfield(L,table,"snapshot");
    if (!lua_isfunction(L,-1)) return unavailable("unavailable");
    lua_pushvalue(L,table);
    if (!call(1) || !lua_istable(L,-1)) return unavailable("addon_error");
    Json snapshot=Json::object();
    for (const char* key:{"in_world","fresh","mode","generation","acknowledgement","age_seconds","pending_mode","failure","request_id","outcome","control_may_still_be_active"}) {
        lua_getfield(L,-1,key);
        if (lua_isboolean(L,-1)) snapshot[key]=lua_toboolean(L,-1)!=0;
        else if (lua_type(L,-1)==LUA_TNUMBER) snapshot[key]=lua_tonumber(L,-1);
        else if (lua_type(L,-1)==LUA_TSTRING) {
            size_t size=0;const char* text=lua_tolstring(L,-1,&size);
            snapshot[key]=std::string(text,size>1024?1024:size);
        }
        lua_pop(L,1);
    }
    if (mutation) snapshot["outcome"]=result.empty()?"addon_error":result;
    return snapshot;
}
}
