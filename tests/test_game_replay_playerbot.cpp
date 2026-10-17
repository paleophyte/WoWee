#include "game_replay.hpp"
#include "core/coordinates.hpp"
#include "game/entity.hpp"
#include "game/movement_handler.hpp"
#include "rendering/camera_controller.hpp"
#include "addons/addon_manager.hpp"
#include <imgui.h>
#include <cmath>

using replay::Session;
using wowee::game::Opcode;
using wowee::network::Packet;

namespace {

Packet tbcSpline(uint64_t guid, bool stop = false, bool corner = false) {
    Packet p(wowee::game::wireOpcode(Opcode::SMSG_MONSTER_MOVE));
    p.writePackedGuid(guid);
    p.writeFloat(100.0f); p.writeFloat(200.0f); p.writeFloat(30.0f);
    p.writeUInt32(1); // spline ID
    p.writeUInt8(stop ? 1 : 0);
    if (!stop) {
        p.writeUInt32(0); // compressed spline, one destination
        p.writeUInt32(1000); // milliseconds
        p.writeUInt32(corner ? 2 : 1);
        p.writeFloat(110.0f); p.writeFloat(200.0f); p.writeFloat(30.0f);
        // Midpoint (105,200,30) minus the decoded delta gives (105,205,30).
        if (corner) p.writeUInt32(0x7ECu << 11); // Y delta = -20 quarters
    }
    return p;
}


void addPlayer(Session& s) {
    s.handler().getEntityManager().addEntity(Session::kPlayerGuid,
        std::make_shared<wowee::game::Player>(Session::kPlayerGuid));
}

Packet botChat(const std::string& text, bool prefixed = false) {
    Packet p(wowee::game::wireOpcode(Opcode::SMSG_MESSAGECHAT));
    // CMaNGOS PlayerbotAI::HandleCommand sends raw debug replies as type 0xFF;
    // TellPlayerNoFacing sends #a replies as PARTY + LANG_ADDON + BOT prefix.
    p.writeUInt8(prefixed ? 2 : 0xFF);
    p.writeUInt32(0xFFFFFFFF);
    p.writeUInt64(Session::kPlayerGuid);
    p.writeUInt32(0);
    p.writeUInt64(0);
    const auto payload = prefixed ? "BOT\t" + text : text;
    p.writeUInt32(static_cast<uint32_t>(payload.size() + 1));
    p.writeString(payload);
    p.writeUInt8(0);
    return p;
}

size_t botCommandCount(Session& session, const std::string& command) {
    size_t count = 0;
    for (auto packet : session.sent().of(Opcode::CMSG_MESSAGECHAT)) {
        const auto type = packet.readUInt32();
        const auto language = packet.readUInt32();
        if (type != static_cast<uint32_t>(wowee::game::ChatType::WHISPER) ||
            language != static_cast<uint32_t>(wowee::game::ChatLanguage::ADDON)) continue;
        const auto target = packet.readString();
        if (target == "Jizzle" && packet.readString() == "BOT\t" + command) ++count;
    }
    return count;
}

} // namespace

TEST_CASE("own TBC splines follow intermediate path corners",
          "[replay][movement][playerbot]") {
    Session s("tbc");
    addPlayer(s);
    s.receive(tbcSpline(Session::kPlayerGuid, false, true));
    s.handler().update(0.5f);
    const auto corner = wowee::core::coords::serverToCanonical(glm::vec3(105, 205, 30));
    CHECK(std::abs(s.handler().getMovementInfo().x - corner.x) < 0.01f);
    CHECK(std::abs(s.handler().getMovementInfo().y - corner.y) < 0.01f);
}


TEST_CASE("own TBC splines drive movement state and hold their endpoint",
          "[replay][movement][playerbot]") {
    Session s("tbc");
    addPlayer(s);
    auto* movement = s.handler().getMovementHandler();
    s.receive(tbcSpline(Session::kPlayerGuid));
    REQUIRE(movement->hasServerDrivenMovement());

    s.handler().update(0.5f);
    const auto midpoint = wowee::core::coords::serverToCanonical(glm::vec3(105, 200, 30));
    CHECK(std::abs(s.handler().getMovementInfo().x - midpoint.x) < 0.01f);
    CHECK(std::abs(s.handler().getMovementInfo().y - midpoint.y) < 0.01f);

    const auto heartbeats = s.sent().of(Opcode::MSG_MOVE_HEARTBEAT).size();
    s.handler().sendMovement(Opcode::MSG_MOVE_HEARTBEAT);
    CHECK(s.sent().of(Opcode::MSG_MOVE_HEARTBEAT).size() == heartbeats);

    s.handler().update(0.75f); // past arrival, including remote-unit dead reckoning
    const auto end = wowee::core::coords::serverToCanonical(glm::vec3(110, 200, 30));
    CHECK(std::abs(s.handler().getMovementInfo().x - end.x) < 0.01f);
    CHECK(std::abs(s.handler().getMovementInfo().y - end.y) < 0.01f);
    CHECK_FALSE(movement->isServerDrivenMoving());

    s.handler().sendMovement(Opcode::MSG_MOVE_START_FORWARD);
    CHECK_FALSE(movement->hasServerDrivenMovement());
    CHECK(s.sent().of(Opcode::MSG_MOVE_START_FORWARD).size() == 1);
}

TEST_CASE("an own spline stop holds the server position but remote splines do not take control",
          "[replay][movement][playerbot]") {
    Session s("tbc");
    addPlayer(s);
    constexpr uint64_t other = 0xF130000001000002ull;
    s.handler().getEntityManager().addEntity(other, std::make_shared<wowee::game::Unit>(other));
    s.receive(tbcSpline(other));
    CHECK_FALSE(s.handler().getMovementHandler()->hasServerDrivenMovement());

    s.receive(tbcSpline(Session::kPlayerGuid));
    s.receive(tbcSpline(Session::kPlayerGuid, true));
    s.handler().update(0.1f);
    const auto stopped = wowee::core::coords::serverToCanonical(glm::vec3(100, 200, 30));
    CHECK(std::abs(s.handler().getMovementInfo().x - stopped.x) < 0.01f);
    CHECK(std::abs(s.handler().getMovementInfo().y - stopped.y) < 0.01f);
    CHECK_FALSE(s.handler().getMovementHandler()->isServerDrivenMoving());
}

TEST_CASE("server control restrictions still prevent manual spline interruption",
          "[replay][movement][playerbot]") {
    Session s("tbc");
    addPlayer(s);
    Packet control(wowee::game::wireOpcode(Opcode::SMSG_CLIENT_CONTROL_UPDATE));
    control.writePackedGuid(Session::kPlayerGuid);
    control.writeUInt8(0);
    s.receive(control);
    s.receive(tbcSpline(Session::kPlayerGuid));
    s.handler().sendMovement(Opcode::MSG_MOVE_START_FORWARD);
    CHECK(s.handler().getMovementHandler()->hasServerDrivenMovement());
    CHECK(s.sent().of(Opcode::MSG_MOVE_START_FORWARD).empty());

    s.handler().forceClearTaxiAndMovementState();
    CHECK_FALSE(s.handler().getMovementHandler()->hasServerDrivenMovement());
}

TEST_CASE("TBC keepalive pings do not trigger CMaNGOS overspeed detection",
          "[replay][network][playerbot]") {
    Session s("tbc");
    s.handler().update(10.0f);
    CHECK(s.sent().of(Opcode::CMSG_PING).empty());
    s.handler().update(20.0f);
    CHECK(s.sent().of(Opcode::CMSG_PING).size() == 1);
}

TEST_CASE("movement keys interrupt playerbot camera follow but respect chat and taxis",
          "[replay][movement][playerbot]") {
    auto* context = ImGui::CreateContext();
    auto& input = wowee::core::Input::getInstance();
    struct Cleanup {
        ImGuiContext* context;
        ~Cleanup() {
            auto& keys = wowee::core::Input::getInstance();
            keys.clearVirtualKeys();
            keys.update();
            ImGui::DestroyContext(context);
        }
    } cleanup{context};
    wowee::rendering::Camera camera;
    wowee::rendering::CameraController controller(&camera);
    std::vector<Opcode> sent;
    controller.setMovementCallback([&](uint32_t opcode) {
        sent.push_back(static_cast<Opcode>(opcode));
    });
    input.setVirtualKey(SDL_SCANCODE_W, true);
    input.update();

    controller.setExternalFollow(true); // Taxi mode cannot be interrupted.
    controller.update(0.016f);
    CHECK(sent.empty());
    controller.setExternalFollow(true, true);
    ImGui::GetIO().WantTextInput = true;
    controller.update(0.016f);
    CHECK(sent.empty());
    ImGui::GetIO().WantTextInput = false;
    controller.update(0.016f);
    REQUIRE(sent.size() == 1);
    CHECK(sent[0] == Opcode::MSG_MOVE_START_FORWARD);
}

TEST_CASE("self Playerbot replies reach addons without appearing in chat",
          "[replay][chat][addon][playerbot]") {
    Session s("tbc");
    addPlayer(s);
    auto player = std::dynamic_pointer_cast<wowee::game::Player>(
        s.handler().getEntityManager().getEntity(Session::kPlayerGuid));
    player->setName("Jizzle");
    std::vector<std::vector<std::string>> messages;
    s.handler().setAddonEventCallback([&](const std::string& event, const auto& args) {
        if (event == "CHAT_MSG_ADDON") messages.push_back(args);
    });
    s.receive(botChat("move to travel target"));
    REQUIRE(messages.size() == 1);
    REQUIRE(messages[0].size() == 4);
    CHECK(messages[0][0].empty());
    CHECK(messages[0][1] == "move to travel target");
    CHECK(messages[0][3] == "Jizzle");
    s.receive(botChat("Traveling to Northshire Abbey", true));
    REQUIRE(messages.size() == 2);
    CHECK(messages[1][0] == "BOT");
    CHECK(messages[1][1] == "Traveling to Northshire Abbey");
    CHECK(s.handler().getChatHistory().empty());
    s.handler().setAddonEventCallback({});
}

TEST_CASE("bot activity addon displays the reported quest and expires stale status",
          "[replay][addon][playerbot]") {
    Session s("tbc");
    addPlayer(s);
    std::dynamic_pointer_cast<wowee::game::Player>(
        s.handler().getEntityManager().getEntity(Session::kPlayerGuid))->setName("Jizzle");
    std::vector<std::string> errors;
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua != nullptr);
    lua->setLuaErrorCallback([&](const auto& error) { errors.push_back(error); });
    lua->widgets().noteScreenSize(1920.0f, 1080.0f);
    REQUIRE(lua->executeString("testNow=0; GetTime=function() return testNow end; WoweeSaveVariables=function() end"));
    const std::string root = std::string(WOWEE_SOURCE_DIR) + "/addons/WoweeBotStatus/";
    REQUIRE(lua->executeFile(root + "Status.lua"));
    REQUIRE(lua->executeFile(root + "Grind.lua"));
    REQUIRE(lua->executeFile(std::string(WOWEE_SOURCE_DIR) + "/tests/test_bot_status.lua"));
    REQUIRE(lua->executeFile(root + "WoweeBotStatus.lua"));
    lua->fireEvent("ADDON_LOADED", {"WoweeBotStatus"});
    lua->fireEvent("PLAYER_ENTERING_WORLD");
    lua->widgets().layout(1920.0f, 1080.0f);
    lua->dispatchOnUpdate(0.25f);
    CHECK(s.sent().of(Opcode::CMSG_MESSAGECHAT).size() == 4);
    CHECK(botCommandCount(s, "#a nc ?") == 1);
    s.handler().setAddonEventCallback([lua](const std::string& event, const auto& args) {
        lua->fireEvent(event, args);
    });
    struct ClearCallback {
        wowee::game::GameHandler& handler;
        ~ClearCallback() { handler.setAddonEventCallback({}); }
    } cleanup{s.handler()};
    s.receive(botChat("quest objective travel target\nTarget: kill Young Wolf (objective 1)\nDistance 45y\nLocation: Northshire Valley (45y)\nStatus: traveling [for 120s]"));
    s.receive(botChat("move to travel target"));
    s.receive(botChat("Traveling 45y to Northshire Valley for |cffffff00|Hquest:33:2|h[Wolves Across the Border]|h|r to kill Young Wolf (objective 1)", true));
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Quest: Wolves Across the Border'"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusDetails:GetText():find('45 yd',1,true) ~= nil"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusDetails:GetText():find('120',1,true) == nil"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusAction:GetText() == 'Last action: move to travel target'"));

    // Match the reported quest ID to the live quest log, including completion.
    s.receive(replay::UpdateObject().values(Session::kPlayerGuid,
        {{wowee::game::UF::PLAYER_QUEST_LOG_START, 33}}).packet(true));
    // On the way to a quest objective, prevent incidental grinding. At the
    // objective, enable it and await server confirmation rather than guessing.
    s.receive(botChat("Non Combat Strategies: grind, travel", true));
    CHECK(botCommandCount(s, "#a nc -grind,?") == 1);
    s.receive(botChat("Non Combat Strategies: travel", true));
    s.receive(botChat("questobjective travel target\nTarget: kill Young Wolf (objective 1)\nDistance 1y\nStatus: working"));
    CHECK(botCommandCount(s, "#a nc +grind,?") == 1);
    CHECK(lua->evaluateBoolean("WoweeBotStatusControl:GetText():find('waiting for server',1,true) ~= nil"));
    s.receive(botChat("Non Combat Strategies: grind, travel", true));
    CHECK(lua->evaluateBoolean("WoweeBotStatusControl:GetText():find('on (confirmed)',1,true) ~= nil"));
    Packet complete(wowee::game::wireOpcode(Opcode::SMSG_UPDATE_OBJECT));
    complete.writeUInt32(1);
    complete.writeUInt8(0); // TBC hasTransport
    complete.writeUInt8(0); // VALUES
    complete.writePackedGuid(Session::kPlayerGuid);
    const auto stateField = wowee::game::fieldIndex(wowee::game::UF::PLAYER_QUEST_LOG_START) + 1;
    complete.writeUInt8(static_cast<uint8_t>(stateField / 32 + 1));
    for (int block = 0; block <= stateField / 32; ++block)
        complete.writeUInt32(block == stateField / 32 ? 1u << (stateField % 32) : 0u);
    complete.writeUInt32(1); // QUEST_STATE_COMPLETE
    s.receive(complete);
    CHECK(lua->evaluateBoolean("WoweeBotStatusProgress:GetText() == 'Ready to turn in'"));
    CHECK(botCommandCount(s, "#a nc -grind,?") == 2);
    s.receive(botChat("Non Combat Strategies: travel", true));
    CHECK(lua->evaluateBoolean("WoweeBotStatusControl:GetText():find('off (confirmed)',1,true) ~= nil"));

    s.receive(botChat("rpg travel target\nTarget: sell items to Brother Danil\nDistance 8y\nStatus: working [for 30s]"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Goal: sell items to Brother Danil'"));
    REQUIRE(lua->executeString("testNow=30"));
    lua->dispatchOnUpdate(0.25f);
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Waiting for bot status'"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusAction:GetText() == ''"));
    const auto beforeGroup = s.sent().of(Opcode::CMSG_MESSAGECHAT).size();
    REQUIRE(lua->executeString("GetNumPartyMembers=function() return 1 end; testNow=38"));
    lua->dispatchOnUpdate(0.25f);
    // Private raw queries continue, while where must not produce party chat.
    CHECK(s.sent().of(Opcode::CMSG_MESSAGECHAT).size() == beforeGroup + 3);
    CHECK(lua->evaluateBoolean("WoweeBotStatusControl:GetText():find('paused while grouped',1,true) ~= nil"));
    // The manual switch stops strategy commands and strategy polling.
    REQUIRE(lua->executeString("WoweeBotStatusAutoGrind:GetScript('OnClick')()"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusSettings.autoGrind == false"));
    const auto manualCount = s.sent().of(Opcode::CMSG_MESSAGECHAT).size();
    REQUIRE(lua->executeString("testNow=46"));
    lua->dispatchOnUpdate(0.25f);
    CHECK(s.sent().of(Opcode::CMSG_MESSAGECHAT).size() == manualCount + 2);
    REQUIRE(lua->executeString("WoweeBotStatusFrame:Hide()"));
    lua->widgets().layout(1920.0f, 1080.0f);
    const auto count = s.sent().of(Opcode::CMSG_MESSAGECHAT).size();
    REQUIRE(lua->executeString("testNow=60"));
    lua->dispatchOnUpdate(0.25f);
    CHECK(s.sent().of(Opcode::CMSG_MESSAGECHAT).size() == count);
    CHECK(lua->dispatchSlashCommand("/botstatus", ""));
    CHECK(lua->evaluateBoolean("WoweeBotStatusFrame:IsShown()"));
    lua->fireEvent("CHAT_MSG_SYSTEM", {"Disable player ai"});
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Player AI disabled'"));
    const auto disabledCount = s.sent().of(Opcode::CMSG_MESSAGECHAT).size();
    REQUIRE(lua->executeString("testNow=100"));
    lua->widgets().layout(1920.0f, 1080.0f);
    lua->dispatchOnUpdate(0.25f);
    CHECK(s.sent().of(Opcode::CMSG_MESSAGECHAT).size() == disabledCount);
    CAPTURE(errors);
    CHECK(errors.empty());
}
