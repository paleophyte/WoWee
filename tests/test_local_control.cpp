#include <catch_amalgamated.hpp>
#include "core/local_control.hpp"
#include "addons/local_control_bridge.hpp"
#include <filesystem>
#include <fstream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
using Json=nlohmann::json;
namespace {
struct LuaFixture {
    lua_State* L=luaL_newstate();
    LuaFixture() {
        luaopen_base(L); lua_settop(L,0);
        luaopen_string(L); lua_settop(L,0);
        luaopen_math(L); lua_settop(L,0);
        luaopen_table(L); lua_settop(L,0);
        eval("testNow=0; GetTime=function() return testNow end");
        for (auto file:{"Status.lua","Control.lua","RemoteControl.lua"}) {
            std::string path=std::string(WOWEE_SOURCE_DIR)+"/addons/WoweeBotStatus/"+file;
            REQUIRE(luaL_dofile(L,path.c_str())==0);
        }
        eval("c=WoweeBotControl.new('local'); connected=true; WoweeBotRemoteSession=WoweeBotRemote.new(function() return c,connected end)");
    }
    ~LuaFixture(){lua_close(L);}
    void eval(const char* code) { REQUIRE(luaL_dostring(L,code)==0); }
    Json call(const std::string& cmd,const std::string& id="r1") {
        const int top=lua_gettop(L);
        auto result=wowee::addons::dispatchLocalControl(L,true,cmd,id);
        CHECK(lua_gettop(L)==top);
        return result;
    }
    void ack(const char* mode="manual",const char* result="ok") {
        const auto script=std::string("assert(c.pending); testNow=testNow+1; assert(c:receive('WIDLE','1\t'..c.pending.id..'\t")+result+"\t"+mode+"\t1\t0\tnone','Jizzle','Jizzle',testNow))";
        eval(script.c_str());
    }
};
}
TEST_CASE("Local bridge confirms only a fresh controller acknowledgement", "[local-control]") {
    LuaFixture f;
    CHECK(f.call("status")["mode"]=="unknown");
    CHECK(f.call("bot")["outcome"]=="requested");
    CHECK(f.call("poll")["outcome"]=="requested");
    f.eval("assert(c:update(testNow)=='.bot idle local_1 status')");
    // Wrong sender and request id cannot establish control.
    f.eval("assert(not c:receive('WIDLE','1\twrong\tok\tbot\t1\t0\tnone','Other','Jizzle',testNow))");
    CHECK(f.call("poll")["outcome"]=="requested");
    f.ack();
    f.eval("assert(c:update(testNow)=='.bot idle local_2 control 1 bot')");
    f.ack("bot");
    CHECK(f.call("poll")["outcome"]=="confirmed");
    CHECK(f.call("status")["fresh"]==true);
    CHECK(f.call("status")["control_may_still_be_active"]==true);
    CHECK(f.call("bot")["outcome"]=="refused"); // Never replay the same external id.
}
TEST_CASE("Local stop confirms Manual without logout or accepting an old cached mode", "[local-control]") {
    LuaFixture f;
    f.eval("c.mode='manual'; c.confirmedAt=0; c.generation=1; c.ackId='old'");
    CHECK(f.call("stop")["outcome"]=="requested");
    CHECK(f.call("poll")["outcome"]=="requested");
    f.eval("c:update(testNow)"); f.ack(); f.eval("c:update(testNow)");
    auto s=f.call("poll");CHECK(s["outcome"]=="confirmed");
    CHECK(s["mode"]=="manual");CHECK(s["control_may_still_be_active"]==false);
}
TEST_CASE("Local control reports refusal, supersession, stale status and disconnect", "[local-control]") {
    LuaFixture f;
    CHECK(f.call("bot")["outcome"]=="requested");
    CHECK(f.call("bot","r2")["outcome"]=="busy");
    f.eval("c:update(testNow)");f.ack("manual","error");
    CHECK(f.call("poll")["outcome"]=="refused");
    CHECK(f.call("bot","r3")["outcome"]=="requested");
    f.eval("c:want('manual')");CHECK(f.call("poll","r3")["outcome"]=="superseded");
    CHECK(f.call("bot","r4")["outcome"]=="requested");
    f.eval("connected=false");CHECK(f.call("poll","r4")["outcome"]=="disconnected");
    f.eval("connected=true;c.mode='bot';c.confirmedAt=0;testNow=30");
    CHECK(f.call("status")["fresh"]==false);
    CHECK(f.call("status")["control_may_still_be_active"]==true);
}
TEST_CASE("Remote Manual supersedes a pending remote Bot request", "[local-control]") {
    LuaFixture f;
    CHECK(f.call("bot","old")["outcome"]=="requested");
    CHECK(f.call("stop","new")["outcome"]=="requested");
    CHECK(f.call("poll","new")["request_id"]=="new");
    f.call("cancel","old"); // A disconnected old caller cannot cancel the takeover.
    f.eval("assert(c.intent=='manual');c:update(testNow)");
    f.ack(); f.eval("c:update(testNow)");
    CHECK(f.call("poll","new")["outcome"]=="confirmed");
    CHECK(f.call("status")["mode"]=="manual");
}
TEST_CASE("Timeout and cancellation discard mutation instead of replaying it", "[local-control]") {
    LuaFixture f;f.call("bot");f.eval("c:update(0);c:update(3);c:update(6);c:update(9)");
    CHECK(f.call("poll")["outcome"]=="timed_out");
    f.eval("assert(not c.intent);assert(not c.pending);assert(c:update(100):find(' status',1,true))");
    f.call("bot","r2");f.call("cancel","r2");
    CHECK(f.call("poll","r2")["outcome"]=="cancelled");
    f.eval("assert(not c.intent);assert(not c.pending);assert(c:update(101):find(' status',1,true))");
    CHECK(wowee::addons::dispatchLocalControl(f.L,false,"bot","x")["outcome"]=="disconnected");
    CHECK(wowee::addons::dispatchLocalControl(nullptr,true,"bot","x")["outcome"]=="unavailable");
}
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
namespace {
struct SocketFixture {
    std::string directory, path;
    std::vector<int> clients;
    SocketFixture() {
        char name[]="/tmp/wowee-control-XXXXXX";
        REQUIRE(mkdtemp(name)!=nullptr);
        directory=std::filesystem::canonical(name).string();path=directory+"/control.sock";
    }
    ~SocketFixture(){for(int fd:clients)close(fd);std::filesystem::remove_all(directory);}
    int connect() {
        int fd=socket(AF_UNIX,SOCK_STREAM,0);REQUIRE(fd>=0);
        sockaddr_un address{};address.sun_family=AF_UNIX;std::strcpy(address.sun_path,path.c_str());
        REQUIRE(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
        fcntl(fd,F_SETFL,O_NONBLOCK);clients.push_back(fd);return fd;
    }
    void send(int fd,const std::string& text){REQUIRE(::send(fd,text.data(),text.size(),0)==static_cast<ssize_t>(text.size()));}
    Json read(int fd){char text[16385];auto count=recv(fd,text,sizeof(text),0);REQUIRE(count>0);return Json::parse(std::string(text,static_cast<size_t>(count)));}
};
}
TEST_CASE("Local socket is private, refuses replacement and never blocks on partial input", "[local-control]") {
    SocketFixture f;int calls=0;
    {
        wowee::core::LocalControlServer server(f.path,[&](auto,auto){++calls;return Json{{"mode","manual"}};});
        struct stat info{};REQUIRE(lstat(f.path.c_str(),&info)==0);CHECK((info.st_mode&0777)==0600);
        CHECK_THROWS(wowee::core::LocalControlServer(f.path,[](auto,auto){return Json{};}));
        int fd=f.connect();f.send(fd,"{\"id\":\"a\",");server.tick();CHECK(calls==0);
        f.send(fd,"\"command\":\"status\"}\n");server.tick();
        auto reply=f.read(fd);CHECK(reply["id"]=="a");CHECK(reply["outcome"]=="observed");CHECK(calls==1);
    }
    CHECK_FALSE(std::filesystem::exists(f.path));
    chmod(f.directory.c_str(),0755);
    CHECK_THROWS(wowee::core::LocalControlServer(f.path,[](auto,auto){return Json{};}));
}
TEST_CASE("Local socket bounds commands and mutation wait, and permits status while waiting", "[local-control]") {
    SocketFixture f;int mutations=0,cancels=0;
    wowee::core::LocalControlServer server(f.path,[&](const std::string& command,const std::string& id) {
        if(command=="cancel")++cancels;
        if(command=="bot")++mutations;
        return Json{{"request_id",id},{"outcome","requested"}};
    });
    const auto start=wowee::core::LocalControlServer::Clock::now();
    int bad=f.connect();f.send(bad,"{\"id\":\"bad\",\"command\":\"execute\"}\n");server.tick(start);
    CHECK(f.read(bad)["outcome"]=="invalid_request");CHECK(mutations==0);
    int bot=f.connect();f.send(bot,"{\"id\":\"b\",\"command\":\"bot\",\"timeout_seconds\":1}\n");server.tick(start);CHECK(mutations==1);
    int status=f.connect();f.send(status,"{\"id\":\"s\",\"command\":\"status\"}\n");server.tick(start);
    CHECK(f.read(status)["outcome"]=="observed");
    server.tick(start+std::chrono::seconds(2));CHECK(f.read(bot)["outcome"]=="timed_out");CHECK(cancels==1);
}
TEST_CASE("A lost local caller cancels the pending intent", "[local-control]") {
    SocketFixture f;int cancels=0;
    wowee::core::LocalControlServer server(f.path,[&](const std::string& command,const std::string& id){if(command=="cancel")++cancels;return Json{{"request_id",id},{"outcome","requested"}};});
    int fd=f.connect();f.send(fd,"{\"id\":\"b\",\"command\":\"bot\"}\n");server.tick();
    shutdown(fd,SHUT_RDWR);server.tick();CHECK(cancels==1);
}

#include <sys/wait.h>
#include <signal.h>
#include <thread>
TEST_CASE("Python CLI, real socket and addon bridge complete an isolated Bot then Stop round trip", "[local-control]") {
    SocketFixture socket;
    LuaFixture lua;
    wowee::core::LocalControlServer server(socket.path,[&](const std::string& command,const std::string& id){return lua.call(command,id);});
    for(const char* command:{"bot","stop"}) {
        int output[2];REQUIRE(pipe(output)==0);
        const auto child=fork();REQUIRE(child>=0);
        if(child==0) {
            close(output[0]);dup2(output[1],STDOUT_FILENO);close(output[1]);
            const std::string tool=std::string(WOWEE_SOURCE_DIR)+"/tools/wowee_control.py";
            execlp("python3","python3",tool.c_str(),"--socket",socket.path.c_str(),"--timeout","2",command,nullptr);
            _exit(127);
        }
        close(output[1]);int status=0;bool finished=false;
        for(int n=0;n<300;++n) {
            server.tick();
            lua.eval("testNow=testNow+0.01; local command=c:update(testNow); if command and c.pending then local p=c.pending; assert(c:receive('WIDLE','1\t'..p.id..'\tok\t'..(p.verb=='control' and p.desired or c.mode or 'manual')..'\t1\t0\tnone','Jizzle','Jizzle',testNow)) end");
            if(waitpid(child,&status,WNOHANG)==child){finished=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if(!finished){kill(child,SIGKILL);waitpid(child,&status,0);}
        REQUIRE(finished);REQUIRE(WIFEXITED(status));REQUIRE(WEXITSTATUS(status)==0);
        char buffer[16385];const auto count=read(output[0],buffer,sizeof(buffer));close(output[0]);REQUIRE(count>0);
        const auto reply=Json::parse(std::string(buffer,static_cast<size_t>(count)));
        CHECK(reply["outcome"]=="confirmed");
        CHECK(reply["control"]["mode"]==(std::string(command)=="bot"?"bot":"manual"));
    }
}
#endif
