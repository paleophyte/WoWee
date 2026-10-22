#include "game_replay.hpp"
#include "core/managed_session.hpp"
#include "core/local_control.hpp"
#include "addons/addon_manager.hpp"
#include <fstream>
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
namespace wowee::core {
struct ManagedSessionTestAccess {
    static void install(Application& app,const std::string& path) {
        app.launchOptions_.controlSocket=path;app.initializeLocalControl();
        app.state=AppState::IN_GAME;
        app.addonManager_=std::make_unique<addons::AddonManager>();
        REQUIRE(app.addonManager_->initialize(app.gameHandler.get()));
    }
    static addons::LuaEngine& lua(Application& app){return *app.addonManager_->getLuaEngine();}
    static void tick(Application& app){app.updateManagedSession();app.localControl_->tick();}
    static auto snapshot(Application& app){return app.managedSession_->snapshot();}
    static void installStoppedRenderer(Application& app) {
        app.renderer=std::make_unique<rendering::Renderer>();app.running=false;
    }
    static void render(Application& app){app.render();}
    static bool renderStagesEmpty(const Application& app){return app.stageStats_.empty();}

};
}
TEST_CASE("Application managed quit requests real logout only after Manual and records a verified disconnect", "[managed-session]") {
    using namespace wowee;
    char name[]="/tmp/wowee-managed-app-XXXXXX";REQUIRE(mkdtemp(name)!=nullptr);
    const auto dir=std::filesystem::canonical(name);
    {
        core::Application app;game::GameServices services;
        auto registry=std::make_unique<game::ExpansionRegistry>();registry->initialize(std::string(WOWEE_SOURCE_DIR)+"/Data");REQUIRE(registry->setActive("tbc"));
        const auto data=registry->getActive()->dataPath;
        game::GameHandlerTestAccess::setExpansions(app,std::move(registry));
        auto& gh=game::GameHandlerTestAccess::installFrameGame(app,services);
        REQUIRE(gh.getOpcodeTable().loadFromJson(data+"/opcodes.json"));
        REQUIRE(gh.getUpdateFieldTable().loadFromJson(data+"/update_fields.json"));
        gh.setPacketParsers(game::createPacketParsers("tbc"));
        auto socket=std::make_unique<replay::SentPackets>();auto* sent=socket.get();
        game::GameHandlerTestAccess::setSocket(gh,std::move(socket));game::GameHandlerTestAccess::enterWorld(gh);
        core::ManagedSessionTestAccess::install(app,(dir/"control.sock").string());
        auto& lua=core::ManagedSessionTestAccess::lua(app);
        REQUIRE(lua.executeString("testNow=0;GetTime=function() return testNow end"));
        for(auto file:{"Status.lua","Control.lua","RemoteControl.lua"})REQUIRE(lua.executeFile(std::string(WOWEE_SOURCE_DIR)+"/addons/WoweeBotStatus/"+file));
        REQUIRE(lua.executeString("c=WoweeBotControl.new('managed');WoweeBotRemoteSession=WoweeBotRemote.new(function() return c,true end)"));
        int client=::socket(AF_UNIX,SOCK_STREAM,0);REQUIRE(client>=0);
        sockaddr_un address{};address.sun_family=AF_UNIX;std::strcpy(address.sun_path,(dir/"control.sock").c_str());
        REQUIRE(connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);fcntl(client,F_SETFL,O_NONBLOCK);
        std::string request="{\"id\":\"quit1\",\"command\":\"quit\"}\n";
        REQUIRE(send(client,request.data(),request.size(),0)==static_cast<ssize_t>(request.size()));
        core::ManagedSessionTestAccess::tick(app);core::ManagedSessionTestAccess::tick(app);
        CHECK(sent->of(game::Opcode::CMSG_LOGOUT_REQUEST).empty());
        REQUIRE(lua.executeString("assert(c:update(0)=='.bot idle managed_1 status');testNow=1;assert(c:receive('WIDLE','1\tmanaged_1\tok\tmanual\t1\t0\tnone','Jizzle','Jizzle',testNow));c:update(testNow)"));
        core::ManagedSessionTestAccess::tick(app);
        REQUIRE(sent->of(game::Opcode::CMSG_LOGOUT_REQUEST).size()==1);
        network::Packet accepted(game::wireOpcode(game::Opcode::SMSG_LOGOUT_RESPONSE));accepted.writeUInt32(0);accepted.writeUInt8(1);
        game::GameHandlerTestAccess::receive(gh,accepted);core::ManagedSessionTestAccess::tick(app);
        CHECK(core::ManagedSessionTestAccess::snapshot(app)["logout_confirmed"]==false);
        network::Packet complete(game::wireOpcode(game::Opcode::SMSG_LOGOUT_COMPLETE));game::GameHandlerTestAccess::receive(gh,complete);
        core::ManagedSessionTestAccess::tick(app);CHECK(gh.getState()==game::WorldState::DISCONNECTED);
        CHECK(app.getState()==core::AppState::DISCONNECTED);
        core::ManagedSessionTestAccess::tick(app);
        const auto state=core::ManagedSessionTestAccess::snapshot(app);CHECK(state["phase"]=="finished");
        char reply[16385];const auto count=recv(client,reply,sizeof(reply),0);REQUIRE(count>0);close(client);
        const auto result=nlohmann::json::parse(std::string(reply,static_cast<size_t>(count)));
        CHECK(result["id"]=="quit1");CHECK(result["outcome"]=="confirmed");CHECK(result["control"]["logout_confirmed"]==true);
        app.shutdown();
        nlohmann::json receipt;std::ifstream(dir/"session.json")>>receipt;
        CHECK(receipt["client_shutdown_complete"]==true);CHECK(receipt["exit_code"]==0);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("A stopped application never submits another render frame", "[managed-session]") {
    wowee::core::Application app;
    wowee::core::ManagedSessionTestAccess::installStoppedRenderer(app);
    wowee::core::ManagedSessionTestAccess::render(app);
    CHECK(wowee::core::ManagedSessionTestAccess::renderStagesEmpty(app));
}
#endif
