#include "game_replay.hpp"
#include "core/coordinates.hpp"
#include "game/entity.hpp"
#include "game/movement_handler.hpp"
#include "game/spell_handler.hpp"
#include "rendering/camera_controller.hpp"
#include "rendering/renderer.hpp"
#include "addons/addon_manager.hpp"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
extern "C" {
#include <lua.h>
}

using replay::Session;
using wowee::game::Opcode;
using wowee::network::Packet;

namespace wowee::rendering {
struct RendererTestAccess {
    static void createCamera(Renderer& renderer) {
        renderer.camera = std::make_unique<Camera>();
        renderer.cameraController = std::make_unique<CameraController>(renderer.camera.get());
        renderer.cameraController->setFollowTarget(&renderer.getCharacterPosition());
    }
    static void updateCharacterFacing(Renderer& renderer, float dt) {
        renderer.updateCharacterFacing(dt);
    }
};
}

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

Packet tbcReturningSpline(uint8_t facingType = 0, uint64_t target = 0, bool smooth = false) {
    Packet p(wowee::game::wireOpcode(Opcode::SMSG_MONSTER_MOVE));
    p.writePackedGuid(Session::kPlayerGuid);
    p.writeFloat(100); p.writeFloat(200); p.writeFloat(30);
    p.writeUInt32(2);
    p.writeUInt8(facingType);
    if (facingType == 4) p.writeFloat(wowee::core::coords::PI);
    if (facingType == 3) p.writeUInt64(target);
    if (facingType == 2) {
        p.writeFloat(110); p.writeFloat(190); p.writeFloat(30);
    }
    p.writeUInt32(smooth ? 0x300 : 0x100); // TBC Flying enables smooth/uncompressed paths
    p.writeUInt32(5000);
    p.writeUInt32(4);
    if (!smooth) { p.writeFloat(110); p.writeFloat(200); p.writeFloat(30); }
    // Three intermediate points relative to midpoint (105,200,30).
    for (const auto& point : std::vector<glm::vec2>{{90,200}, {90,210}, {110,210}}) {
        if (smooth) {
            p.writeFloat(point.x); p.writeFloat(point.y); p.writeFloat(30);
            continue;
        }
        const auto dx = static_cast<int32_t>((105 - point.x) * 4);
        const auto dy = static_cast<int32_t>((200 - point.y) * 4);
        p.writeUInt32((static_cast<uint32_t>(dx) & 0x7ff) |
                      ((static_cast<uint32_t>(dy) & 0x7ff) << 11));
    }
    if (smooth) { p.writeFloat(110); p.writeFloat(200); p.writeFloat(30); }
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

TEST_CASE("own TBC returning paths face each segment and apply final facing only at arrival",
          "[replay][movement][playerbot][spline-facing]") {
    for (uint8_t facingType : {0, 2, 3, 4}) {
        CAPTURE(facingType);
        Session s("tbc");
        addPlayer(s);
        constexpr uint64_t targetGuid = 0xf130000001000003ull;
        auto target = std::make_shared<wowee::game::Unit>(targetGuid);
        const auto targetPosition = wowee::core::coords::serverToCanonical(glm::vec3(120,200,30));
        target->setPosition(targetPosition.x, targetPosition.y, targetPosition.z, 0);
        s.handler().getEntityManager().addEntity(targetGuid, target);
        s.receive(tbcReturningSpline(facingType, targetGuid));
        auto checkSample = [&](float dt, glm::vec3 serverPosition, glm::vec3 serverDirection) {
            s.handler().update(dt);
            const auto& move = s.handler().getMovementInfo();
            const auto pos = wowee::core::coords::serverToCanonical(serverPosition);
            CHECK(move.x == Catch::Approx(pos.x).margin(0.02f));
            CHECK(move.y == Catch::Approx(pos.y).margin(0.02f));
            const auto direction = wowee::core::coords::serverToCanonical(serverDirection);
            CHECK(std::cos(move.orientation) * direction.x - std::sin(move.orientation) * direction.y
                  == Catch::Approx(1).margin(0.001f));
            const auto renderDirection = wowee::core::coords::canonicalToRender(direction);
            const float yaw = wowee::core::coords::canonicalToCharacterYawDeg(move.orientation) *
                              wowee::core::coords::PI / 180;
            CHECK(std::cos(yaw) * renderDirection.x + std::sin(yaw) * renderDirection.y
                  == Catch::Approx(1).margin(0.001f));
        };
        checkSample(0.5f, {95,200,30}, {-1,0,0});
        checkSample(1.0f, {90,205,30}, {0,1,0});
        checkSample(1.5f, {100,210,30}, {1,0,0});
        checkSample(1.5f, {110,205,30}, {0,-1,0});
        s.handler().update(1.0f); // cross arrival in one frame
        const auto& end = s.handler().getMovementInfo();
        CHECK_FALSE(s.handler().getMovementHandler()->isServerDrivenMoving());
        CHECK(end.x == Catch::Approx(200));
        CHECK(end.y == Catch::Approx(110));
        const float expectedServerAngle = facingType == 4 ? wowee::core::coords::PI :
            facingType == 3 ? 0.0f : -wowee::core::coords::PI * 0.5f;
        CHECK(std::cos(wowee::core::coords::canonicalToServerYaw(end.orientation) - expectedServerAngle)
              == Catch::Approx(1).margin(0.001f));
        const auto orientation = end.orientation;
        s.handler().update(2.0f);
        CHECK(s.handler().getMovementInfo().orientation == Catch::Approx(orientation));
        CHECK(s.sent().of(Opcode::CMSG_MESSAGECHAT).empty());
    }
}

TEST_CASE("TBC smooth self paths face their curve and manual takeover discards final facing",
          "[replay][movement][playerbot][spline-facing]") {
    Session s("tbc");
    addPlayer(s);
    s.receive(tbcReturningSpline(4, 0, true));
    s.handler().update(0.25f);
    for (int sample = 0; sample < 30; ++sample) {
        const auto before = s.handler().getMovementInfo();
        s.handler().update(0.01f);
        const auto after = s.handler().getMovementInfo();
        const float dx = after.x - before.x, dy = after.y - before.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        REQUIRE(distance > 0);
        CHECK((std::cos(after.orientation) * dx - std::sin(after.orientation) * dy) / distance > 0.995f);
        s.handler().update(0.13f);
    }
    s.handler().sendMovement(Opcode::MSG_MOVE_START_FORWARD);
    REQUIRE_FALSE(s.handler().getMovementHandler()->hasServerDrivenMovement());
    const auto manual = s.handler().getMovementInfo();
    s.handler().update(8);
    CHECK(s.handler().getMovementInfo().orientation == Catch::Approx(manual.orientation));
    CHECK(s.sent().of(Opcode::MSG_MOVE_START_FORWARD).size() == 1);
}

TEST_CASE("server spline facing survives application and renderer frame synchronization",
          "[replay][movement][playerbot][frame-facing]") {
    using Access = wowee::game::GameHandlerTestAccess;
    using RenderAccess = wowee::rendering::RendererTestAccess;
    wowee::game::GameServices services;
    wowee::core::Application app;
    auto registry = std::make_unique<wowee::game::ExpansionRegistry>();
    registry->initialize(std::string(WOWEE_SOURCE_DIR) + "/Data");
    REQUIRE(registry->setActive("tbc"));
    const auto path = registry->getActive()->dataPath;
    services.expansionRegistry = registry.get();
    Access::setExpansions(app, std::move(registry));
    auto& game = Access::installFrameGame(app, services);
    REQUIRE(game.getOpcodeTable().loadFromJson(path + "/opcodes.json"));
    REQUIRE(game.getUpdateFieldTable().loadFromJson(path + "/update_fields.json"));
    game.setPacketParsers(wowee::game::createPacketParsers("tbc"));
    game.setPlayerGuid(Session::kPlayerGuid);
    game.getEntityManager().addEntity(Session::kPlayerGuid,
        std::make_shared<wowee::game::Player>(Session::kPlayerGuid));
    Access::setSocket(game, std::make_unique<replay::SentPackets>());
    Access::enterWorld(game);
    auto renderer = std::make_unique<wowee::rendering::Renderer>();
    RenderAccess::createCamera(*renderer);
    auto& render = Access::installFrameRenderer(app, std::move(renderer));
    services.renderer = &render;
    auto* camera = render.getCameraController();
    auto* context = ImGui::CreateContext();
    struct InputCleanup {
        ImGuiContext* context;
        ~InputCleanup() {
            auto& input = wowee::core::Input::getInstance();
            input.clearVirtualKeys(); input.update();
            ImGui::DestroyContext(context);
        }
    } cleanup{context};
    auto& input = wowee::core::Input::getInstance();
    camera->setMovementCallback([&](uint32_t opcode) { game.sendMovement(static_cast<Opcode>(opcode)); });
    render.getCharacterPosition() = {100, 200, 30};
    game.setPosition(200, 100, 30);
    // First manual frame: no earlier bot spline is required to permit input.
    Access::applyServerMovement(app, 0.016f);
    const auto startPosition = render.getCharacterPosition();
    input.setVirtualKey(SDL_SCANCODE_W, true); input.update();
    camera->update(0.016f);
    CHECK_FALSE(game.getMovementHandler()->hasServerDrivenMovement());
    CHECK(glm::distance(glm::vec2(startPosition), glm::vec2(render.getCharacterPosition())) > 0.01f);
    input.setVirtualKey(SDL_SCANCODE_W, false); input.update();
    camera->update(0.016f);
    Access::receive(game, tbcReturningSpline());
    for (float dt : {0.5f, 1.0f, 1.5f, 1.5f}) {
        game.update(dt);
        // Simulate a camera heading left over from manual play/orbit.
        camera->setFacingYaw(37.0f);
        const auto orbit = camera->getYaw();
        Access::applyServerMovement(app, dt);
        const float expected = wowee::core::coords::canonicalToCharacterYawDeg(
            game.getMovementInfo().orientation);
        CHECK(render.getCharacterYaw() == Catch::Approx(expected));
        CHECK(camera->getFacingYaw() == Catch::Approx(expected));
        // Even a later camera-facing change must not replace a server-owned
        // model pose. Orbit remains independent of both facing values.
        camera->setFacingYaw(expected + 73.0f);
        RenderAccess::updateCharacterFacing(render, dt);
        CHECK(render.getCharacterYaw() == Catch::Approx(expected));
        CHECK(camera->getYaw() == Catch::Approx(orbit));
        camera->setFacingYaw(expected);
    }
    // Actual local input must still interrupt external following and permit
    // the renderer to use manual facing again.
    input.setVirtualKey(SDL_SCANCODE_W, true); input.update();
    camera->update(0.016f);
    CHECK_FALSE(game.getMovementHandler()->hasServerDrivenMovement());
    CHECK_FALSE(camera->isExternallyFollowing());
    RenderAccess::updateCharacterFacing(render, 0.016f);
    CHECK(render.getCharacterYaw() == Catch::Approx(camera->getFacingYaw()));
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

TEST_CASE("Lua boolean evaluation returns the expression and preserves the stack",
          "[replay][addon][playerbot]") {
    Session s("tbc");
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua != nullptr);
    auto* state = lua->getState();
    const int base = lua_gettop(state);
    lua_pushstring(state, "unrelated truthy stack value");
    for (int i = 0; i < 3; ++i) {
        CHECK(lua->evaluateBoolean("true"));
        CHECK_FALSE(lua->evaluateBoolean("false"));
        CHECK_FALSE(lua->evaluateBoolean("nil"));
        CHECK(lua->evaluateBoolean("0")); // Lua treats zero as true.
        CHECK_FALSE(lua->evaluateBoolean("1 == 2"));
        CHECK(lua_gettop(state) == base + 1);
        CHECK(std::string(lua_tostring(state, -1)) == "unrelated truthy stack value");
    }
    CHECK_FALSE(lua->evaluateBoolean("error('evaluation failure')"));
    CHECK_FALSE(lua->evaluateBoolean("not valid syntax !"));
    CHECK(lua_gettop(state) == base + 1);
    lua_settop(state, base);
}

TEST_CASE("bot control is acknowledged and runs independently of the activity panel",
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
    for (const auto* file : {"Status.lua", "Grind.lua", "Control.lua", "Activity.lua", "Inbox.lua", "NotesUI.lua"})
        REQUIRE(lua->executeFile(root + file));
    REQUIRE(lua->executeFile(std::string(WOWEE_SOURCE_DIR) + "/tests/test_bot_status.lua"));
    REQUIRE(lua->executeFile(std::string(WOWEE_SOURCE_DIR) + "/tests/test_bot_control.lua"));
    REQUIRE(lua->executeFile(std::string(WOWEE_SOURCE_DIR) + "/tests/test_bot_activity.lua"));
    REQUIRE(lua->executeFile(std::string(WOWEE_SOURCE_DIR) + "/tests/test_bot_inbox.lua"));
    REQUIRE(lua->executeFile(root + "WoweeBotStatus.lua"));
    REQUIRE(lua->executeFile(std::string(WOWEE_SOURCE_DIR) + "/tests/test_bot_warning_ui.lua"));
    lua->fireEvent("ADDON_LOADED", {"WoweeBotStatus"});
    lua->fireEvent("PLAYER_ENTERING_WORLD");
    lua->widgets().layout(1920.0f, 1080.0f);
    s.handler().setAddonEventCallback([lua](const std::string& event, const auto& args) {
        lua->fireEvent(event, args);
    });
    struct ClearCallback {
        wowee::game::GameHandler& handler;
        ~ClearCallback() { handler.setAddonEventCallback({}); }
    } cleanup{s.handler()};
    auto tick = [&] { lua->dispatchOnUpdate(0.25f); };
    auto ack = [&](const std::string& id, const std::string& mode, int generation, bool grind) {
        s.receive(botChat("WIDLE\t1\t0_" + id + "\tok\t" + mode + "\t" +
            std::to_string(generation) + "\t" + (grind ? "1" : "0") + "\tnone"));
    };
    auto count = [&] { return s.sent().of(Opcode::CMSG_MESSAGECHAT).size(); };
    auto lastCommand = [&] {
        auto packets = s.sent().of(Opcode::CMSG_MESSAGECHAT);
        REQUIRE_FALSE(packets.empty());
        auto p = packets.back();
        p.readUInt32(); p.readUInt32();
        return p.readString();
    };
    tick();
    CHECK(count() == 1);
    CHECK(lastCommand() == ".bot idle 0_1 status");
    CHECK(botCommandCount(s, "#a nc ?") == 0); // no legacy automation before confirmation
    ack("1", "bot", 1, false);
    tick();
    CHECK(count() == 4); // three private status queries, no strategy mutation
    s.receive(replay::UpdateObject().values(Session::kPlayerGuid,
        {{wowee::game::UF::PLAYER_QUEST_LOG_START, 33}}).packet(true));
    s.receive(botChat("questobjective travel target\nTarget: kill Young Wolf (objective 1)\nDistance 1y\nStatus: working"));
    s.receive(botChat("Traveling 1y for |Hquest:33:2|h[Wolves Across the Border]|h to kill Young Wolf (objective 1)", true));
    tick();
    CHECK(lastCommand() == ".bot idle 0_2 grind 1 on");
    ack("2", "bot", 1, true);
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Quest: Wolves Across the Border'"));

    REQUIRE(lua->executeString("WoweeBotStatusPlay:GetScript('OnClick')()"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'Switching control...'"));
    tick();
    CHECK(lastCommand() == ".bot idle 0_3 control 1 manual");
    const auto pendingCount = count();
    tick();
    CHECK(count() == pendingCount); // no competing grind/status actions during handoff
    ack("3", "manual", 2, true);
    CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'I am playing (server confirmed)'"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Your character is under your control'"));
    REQUIRE(lua->executeString("WoweeBotStatusFrame:Hide(); testNow=8"));
    lua->widgets().layout(1920.0f, 1080.0f);
    tick();
    CHECK(count() == pendingCount + 1); // hidden panel still checks server control
    CHECK(lastCommand() == ".bot idle 0_4 status");
    CHECK(lua->evaluateBoolean("WoweeBotLauncher:IsShown()"));

    CHECK(lua->dispatchSlashCommand("/botplay", "bot"));
    tick();
    CHECK(lastCommand() == ".bot idle 0_5 control 2 bot");
    ack("4", "bot", 1, true); // stale, superseded request must not restore control
    CHECK_FALSE(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'Bot is playing (server confirmed)'"));
    ack("5", "bot", 3, false);
    CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'Bot is playing (server confirmed)'"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Waiting for bot status'"));
    const auto beforeHidden = count();
    tick();
    CHECK(count() == beforeHidden + 3); // private status continues while hidden
    CHECK(lua->dispatchSlashCommand("/botstatus", ""));
    CHECK(lua->evaluateBoolean("WoweeBotStatusFrame:IsShown()"));

    // Quest completion must still disable grinding through the guarded bridge.
    s.receive(botChat("questobjective travel target\nTarget: kill Young Wolf (objective 1)\nDistance 1y\nStatus: working"));
    s.receive(botChat("Traveling 1y for |Hquest:33:2|h[Wolves Across the Border]|h to kill Young Wolf (objective 1)", true));
    tick();
    CHECK(lastCommand() == ".bot idle 0_6 grind 3 on");
    ack("6", "bot", 3, true);
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
    tick();
    CHECK(lua->evaluateBoolean("WoweeBotStatusProgress:GetText() == 'Ready to turn in'"));
    CHECK(lastCommand() == ".bot idle 0_7 grind 3 off");
    ack("7", "bot", 3, false);

    const auto beforeGroup = count();
    REQUIRE(lua->executeString("GetNumPartyMembers=function() return 1 end; testNow=16"));
    tick();
    CHECK(count() == beforeGroup + 3); // control status + private queries; no party where report
    ack("8", "bot", 3, false);
    CHECK(lua->evaluateBoolean("WoweeBotStatusControl:GetText():find('paused while grouped',1,true) ~= nil"));
    REQUIRE(lua->executeString("testNow=50"));
    tick();
    CHECK(lastCommand() == ".bot idle 0_9 status");
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Waiting for bot status'"));
    ack("9", "bot", 3, false);
    s.receive(botChat("WIDLECAP\t1\t0_9\t3\tactivity_1"));
    tick();
    CHECK(lastCommand() == ".bot idle 0_a1 snapshot 3");
    auto hex = [](const std::string& text) {
        std::string out;
        for (unsigned char c : text) { out += "0123456789abcdef"[c >> 4]; out += "0123456789abcdef"[c & 15]; }
        return out.empty() ? std::string("-") : out;
    };
    const std::string body = "bot\tnon_combat\tquest_turnin\tworking\t33\t-1\tnone\t196\t3\t" +
        hex("Eagan Peltskinner") + "\t" + hex("Wolves Across the Border") + "\t" + hex("Northshire Abbey") +
        "\tnpc\t789\t" + hex("Dane Winslow") + "\t" + hex("rpg ai chat") + "\t" + hex("rpg ai chat") + "\t0\t1";
    const auto fragments = (body.size() + 159) / 160;
    for (size_t i = 0; i < fragments; ++i) {
        s.receive(botChat("WIDLEACT\t1\t0_a1\t3\t" + std::to_string(i + 1) + "\t" +
            std::to_string(fragments) + "\t" + body.substr(i * 160, 160)));
        if (i + 1 < fragments)
            CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Waiting for bot status'"));
    }
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Quest: Wolves Across the Border'"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusDetails:GetText():find('Eagan Peltskinner',1,true) ~= nil"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusInteraction:GetText():find('Dane Winslow',1,true) ~= nil"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusAction:GetText() == 'Last successful action: rpg ai chat'"));
    s.receive(botChat("rpg travel target\nTarget: train at Someone Else\nDistance 1y\nStatus: working"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Quest: Wolves Across the Border'"));

    // Exercise the reported trigger repeatedly: typed Bot -> manual -> Bot,
    // with new snapshot IDs/generations and late fragments from the old mode.
    auto snapshot = [&](int id, int generation, const std::string& title) {
        std::string current = body;
        const auto oldTitle = hex("Wolves Across the Border");
        current.replace(current.find(oldTitle), oldTitle.size(), hex(title));
        const auto parts = (current.size() + 159) / 160;
        for (size_t i = 0; i < parts; ++i)
            s.receive(botChat("WIDLEACT\t1\t0_a" + std::to_string(id) + "\t" + std::to_string(generation) + "\t" +
                std::to_string(i + 1) + "\t" + std::to_string(parts) + "\t" + current.substr(i * 160, 160)));
    };
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(lua->executeString("testNow=" + std::to_string(51 + cycle * 2)));
        CHECK(lua->dispatchSlashCommand("/botplay", "manual"));
        tick();
        const int requestId = 10 + cycle * 2;
        const int generation = 3 + cycle * 2;
        CHECK(lastCommand() == ".bot idle 0_" + std::to_string(requestId) + " control " + std::to_string(generation) + " manual");
        ack(std::to_string(requestId), "manual", generation + 1, false);
        CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'I am playing (server confirmed)'"));
        REQUIRE(lua->executeString("testNow=" + std::to_string(52 + cycle * 2)));
        CHECK(lua->dispatchSlashCommand("/botplay", "bot"));
        tick();
        CHECK(lastCommand() == ".bot idle 0_" + std::to_string(requestId + 1) + " control " + std::to_string(generation + 1) + " bot");
        ack(std::to_string(requestId + 1), "bot", generation + 2, false);
        s.receive(botChat("WIDLECAP\t1\t0_" + std::to_string(requestId + 1) + "\t" + std::to_string(generation + 2) + "\tactivity_1"));
        tick();
        CHECK(lastCommand() == ".bot idle 0_a" + std::to_string(cycle + 2) + " snapshot " + std::to_string(generation + 2));
        snapshot(cycle + 1, generation, "Obsolete snapshot");
        CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Waiting for bot status'"));
        snapshot(cycle + 2, generation + 2, "Replay cycle " + std::to_string(cycle + 1));
        CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Quest: Replay cycle " + std::to_string(cycle + 1) + "'"));
    }
    // Lose all replies for one status request. Recover via observation only,
    // even while the panel is closed; the independent server may still play.
    REQUIRE(lua->executeString("testNow=64")); tick();
    CHECK(lastCommand() == ".bot idle 0_a5 snapshot 9");
    REQUIRE(lua->executeString("testNow=67")); tick();
    CHECK(lastCommand() == ".bot idle 0_16 status");
    REQUIRE(lua->executeString("testNow=70")); tick();
    CHECK(lastCommand() == ".bot idle 0_16 status");
    REQUIRE(lua->executeString("testNow=73")); tick();
    CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText():find('No server confirmation',1,true) ~= nil"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusDetails:GetText():find('Last confirmed: Bot, 17s ago.',1,true) ~= nil"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusPlay:GetText() == 'Refresh status'"));
    REQUIRE(lua->executeString("WoweeBotStatusFrame:Hide(); testNow=103"));
    lua->widgets().layout(1920.0f, 1080.0f);
    tick();
    CHECK(lastCommand() == ".bot idle 0_17 status");
    ack("16", "manual", 10, false); // late old request cannot confirm control
    CHECK_FALSE(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'I am playing (server confirmed)'"));
    ack("17", "bot", 9, false);
    tick();
    CHECK(lastCommand() == ".bot idle 0_a6 snapshot 9");
    snapshot(6, 9, "Recovered without relog");
    CHECK(lua->evaluateBoolean("WoweeBotStatusGoal:GetText() == 'Quest: Recovered without relog'"));
    const auto beforeDebug = count();
    CHECK(lua->dispatchSlashCommand("/botstatus", "debug"));
    CHECK(count() == beforeDebug); // diagnostics are local, including while hidden
    CHECK(lua->dispatchSlashCommand("/botstatus", "refresh"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'Control unconfirmed; addon automation paused'"));
    tick();
    CHECK(lastCommand() == ".bot idle 0_18 status");
    ack("18", "manual", 10, false);
    const auto afterRefresh = count();
    tick();
    CHECK(count() == afterRefresh); // read-only refresh preserves server manual mode
    CHECK(botCommandCount(s, "#a nc +grind,?") == 0); // all mutations use the guarded protocol
    CAPTURE(errors);
    CHECK(errors.empty());
}
TEST_CASE("social policy buttons require server confirmation and preserve manual control",
          "[replay][addon][playerbot]") {
    Session s("tbc");
    addPlayer(s);
    std::dynamic_pointer_cast<wowee::game::Player>(
        s.handler().getEntityManager().getEntity(Session::kPlayerGuid))->setName("Jizzle");
    s.handler().setRealmIdentity("TBC - Home", "realm.example:8085", 1);
    std::vector<std::string> errors;
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua != nullptr);
    lua->setLuaErrorCallback([&](const auto& error) { errors.push_back(error); });
    lua->widgets().noteScreenSize(1920.0f, 1080.0f);
    REQUIRE(lua->executeString("testNow=0; GetTime=function() return testNow end; GetServerTime=function() return 1000+testNow end; UnitCharacterPoints=function() return 0 end; WoweeSaveVariables=function() end; WoweeBotStatusSettings={}"));
    const std::string root = std::string(WOWEE_SOURCE_DIR) + "/addons/WoweeBotStatus/";
    for (const auto* file : {"Status.lua", "Grind.lua", "Control.lua", "Activity.lua", "Inbox.lua", "NotesUI.lua", "WoweeBotStatus.lua"})
        REQUIRE(lua->executeFile(root + file));
    lua->fireEvent("ADDON_LOADED", {"WoweeBotStatus"});
    lua->fireEvent("PLAYER_ENTERING_WORLD");
    s.handler().setAddonEventCallback([lua](const std::string& event, const auto& args) {
        lua->fireEvent(event, args);
    });
    struct ClearCallback {
        wowee::game::GameHandler& handler;
        ~ClearCallback() { handler.setAddonEventCallback({}); }
    } cleanup{s.handler()};
    auto tick = [&] { lua->dispatchOnUpdate(0.25f); };
    auto sent = [&](const std::string& command) {
        size_t count = 0;
        for (auto packet : s.sent().of(Opcode::CMSG_MESSAGECHAT)) {
            packet.readUInt32(); packet.readUInt32();
            if (packet.readString() == command) ++count;
        }
        return count;
    };
    auto policy = [&](const std::string& id, const std::string& result, bool enabled,
                      const std::string& storage, const std::string& reason) {
        s.receive(botChat("WIDLEPOL\t1\t0_" + id + "\t1\t" + result + "\t" +
            (enabled ? "1" : "0") + "\t" + storage + "\t" + reason));
    };
    CHECK(lua->dispatchSlashCommand("/botpolicy", ""));
    lua->widgets().layout(1920.0f, 1080.0f);
    CHECK(lua->evaluateBoolean("WoweeBotSocialState:GetText() == 'Waiting for control confirmation'"));
    CHECK(lua->evaluateBoolean("WoweeBotSocialToggle:IsEnabled() == 0"));
    tick();
    CHECK(sent(".bot idle 0_1 status") == 1);
    s.receive(botChat("WIDLE\t1\t0_1\tok\tmanual\t1\t0\tnone"));
    CHECK(lua->evaluateBoolean("WoweeBotSocialState:GetText() == 'Social Detours: unavailable'"));
    s.receive(botChat("WIDLECAP\t1\t0_1\t1\tactivity_1,social_1"));
    tick();
    CHECK(sent(".bot idle 0_2 social 1 status") == 1);
    lua->fireEvent("CHAT_MSG_ADDON", {"WIDLEPOL", "1\t0_2\t1\tok\t1\tstored\tnone", "WHISPER", "SomeoneElse"});
    CHECK_FALSE(lua->evaluateBoolean("WoweeBotSocialState:GetText() == 'Social Detours: On (server confirmed)'"));
    policy("2", "ok", false, "stored", "none");
    CHECK(lua->evaluateBoolean("WoweeBotSocialState:GetText() == 'Social Detours: Off (server confirmed)'"));
    CHECK(lua->evaluateBoolean("WoweeBotSocialToggle:IsEnabled() == 1"));
    REQUIRE(lua->executeString("WoweeBotSocialToggle:GetScript('OnClick')()"));
    CHECK(lua->evaluateBoolean("WoweeBotSocialState:GetText() == 'Social Detours: waiting for confirmation'"));
    CHECK(lua->evaluateBoolean("WoweeBotSocialToggle:IsEnabled() == 0"));
    tick();
    CHECK(sent(".bot idle 0_3 social 1 on") == 1);
    policy("3", "error", false, "unavailable", "persistence_failed");
    CHECK(lua->evaluateBoolean("WoweeBotSocialState:GetText() == 'Social Detours: unconfirmed'"));
    tick();
    CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'I am playing (server confirmed)'"));
    CHECK(lua->dispatchSlashCommand("/botnotes", ""));
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  1 blocked  |  0 notes'"));
    CHECK(lua->evaluateBoolean("WoweeBotNotesRow1.title:GetText():find('Social Detours policy needs attention',1,true) ~= nil"));
    REQUIRE(lua->executeString("WoweeBotSocialRefresh:GetScript('OnClick')()"));
    tick();
    CHECK(sent(".bot idle 0_4 social 1 status") == 1);
    policy("4", "ok", false, "stored", "none");
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  1 notes'"));
    // The policy driver continues even when the activity panel is closed.
    REQUIRE(lua->executeString("WoweeBotStatusFrame:Hide(); WoweeBotSocialToggle:GetScript('OnClick')()"));
    tick();
    CHECK(sent(".bot idle 0_5 social 1 on") == 1);
    policy("5", "ok", true, "stored", "none");
    CHECK(lua->evaluateBoolean("WoweeBotSocialState:GetText() == 'Social Detours: On (server confirmed)'"));
    REQUIRE(lua->executeString("WoweeBotSocialToggle:GetScript('OnClick')()"));
    tick();
    CHECK(sent(".bot idle 0_6 social 1 off") == 1);
    for (auto packet : s.sent().of(Opcode::CMSG_MESSAGECHAT)) {
        packet.readUInt32(); packet.readUInt32();
        CHECK(packet.readString().find(" control ") == std::string::npos);
    }
    // An unconfirmed policy edit must never delay a request to change control.
    CHECK(lua->dispatchSlashCommand("/botplay", "bot"));
    tick();
    CHECK(sent(".bot idle 0_7 control 1 bot") == 1);
    policy("6", "ok", false, "stored", "none");
    CHECK(lua->evaluateBoolean("WoweeBotSocialState:GetText() == 'Social Detours: asking the server'"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'Switching control...'"));
    s.receive(botChat("WIDLE\t1\t0_7\tok\tbot\t2\t0\tnone"));
    CHECK(lua->evaluateBoolean("WoweeBotStatusMode:GetText() == 'Bot is playing (server confirmed)'"));
    CAPTURE(errors);
    CHECK(errors.empty());
}

TEST_CASE("bot notes use real realm identity and actual learned-spell events",
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
    CHECK(lua->evaluateBoolean("GetWoweeRealmIdentity() == nil and GetRealmName() == 'Unknown'"));
    s.handler().setRealmIdentity("TBC - Home", "realm.example:8085", 1);
    CHECK(lua->evaluateBoolean("GetRealmName() == 'TBC - Home' and GetNormalizedRealmName() == 'TBCHome'"));
    REQUIRE(lua->executeString("firstRealm=GetWoweeRealmIdentity(); testNow=0; testPoints=0; saved=0; GetTime=function() return testNow end; GetServerTime=function() return 1000+testNow end; UnitCharacterPoints=function() return testPoints end; WoweeSaveVariables=function() saved=saved+1 end; GetSpellInfo=function(id) if id==133 then return 'Fireball','Rank 1' elseif id==116 then return 'Frostbolt','Rank 1' end end"));
    const std::string root = std::string(WOWEE_SOURCE_DIR) + "/addons/WoweeBotStatus/";
    REQUIRE(lua->executeFile(root + "Inbox.lua"));
    REQUIRE(lua->executeFile(root + "NotesUI.lua"));
    lua->fireEvent("ADDON_LOADED", {"WoweeBotStatus"});
    lua->fireEvent("PLAYER_ENTERING_WORLD");
    CHECK(lua->dispatchSlashCommand("/botnotes", ""));
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  0 notes'"));
    lua->fireEvent("SPELLS_CHANGED"); // initial spellbook loading must not look like training
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  0 notes'"));
    s.handler().setAddonEventCallback([lua](const std::string& event, const auto& args) {
        lua->fireEvent(event, args);
    });
    struct ClearCallback {
        wowee::game::GameHandler& handler;
        ~ClearCallback() { handler.setAddonEventCallback({}); }
    } cleanup{s.handler()};
    for (uint32_t id : {133u, 116u, 116u}) {
        Packet learned(wowee::game::wireOpcode(Opcode::SMSG_LEARNED_SPELL));
        learned.writeUInt32(id);
        s.receive(learned);
    }
    CHECK(lua->evaluateBoolean("WoweeBotNotesLauncher:GetText() == 'Notes (1)'"));
    CHECK(lua->evaluateBoolean("WoweeBotNotesRow1.title:GetText() == '* New abilities x2'"));
    REQUIRE(lua->executeString("WoweeBotNotesRow1:GetScript('OnClick')()"));
    CHECK(lua->evaluateBoolean("WoweeBotNotesDetail:GetText():find('Frostbolt (Rank 1)',1,true) ~= nil"));
    CHECK(lua->evaluateBoolean("WoweeBotNotesLauncher:GetText() == 'Notes'"));
    REQUIRE(lua->executeString("WoweeBotNotesBack:GetScript('OnClick')(); testPoints=2"));
    lua->fireEvent("CHARACTER_POINTS_CHANGED");
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  2 notes'"));
    REQUIRE(lua->executeString("WoweeBotReportStatusProblem('control','No server confirmation'); WoweeBotNotesClear:GetScript('OnClick')()"));
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  1 blocked  |  0 notes'"));
    lua->fireEvent("PLAYER_LEAVING_WORLD");
    lua->fireEvent("PLAYER_ENTERING_WORLD");
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  1 blocked  |  0 notes'"));
    REQUIRE(lua->executeString("WoweeBotReportStatusProblem('control',nil)"));
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  1 notes'"));
    const auto savedPath = std::filesystem::temp_directory_path() /
        ("wowee-inbox-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lua");
    struct RemoveSavedFile {
        std::filesystem::path path;
        ~RemoveSavedFile() { std::error_code ec; std::filesystem::remove(path, ec); }
    } removeSaved{savedPath};
    REQUIRE(lua->saveSavedVariables(savedPath.string(), {"WoweeBotNotesData"}));
    REQUIRE(lua->executeString("WoweeBotNotesData=nil"));
    REQUIRE(lua->loadSavedVariables(savedPath.string()));
    lua->fireEvent("PLAYER_LEAVING_WORLD");
    // Identical realm names/IDs at different endpoints must not share an inbox.
    s.handler().setRealmIdentity("TBC - Home", "realm.example:8086", 1);
    CHECK(lua->evaluateBoolean("GetWoweeRealmIdentity() ~= firstRealm"));
    REQUIRE(lua->executeString("testPoints=0"));
    lua->fireEvent("PLAYER_ENTERING_WORLD");
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  0 notes'"));
    lua->fireEvent("PLAYER_LEAVING_WORLD");
    s.handler().setRealmIdentity("TBC - Home", "realm.example:8085", 1);
    lua->fireEvent("PLAYER_ENTERING_WORLD");
    CHECK(lua->evaluateBoolean("WoweeBotNotesSummary:GetText() == '0 choices  |  0 blocked  |  1 notes'"));
    CHECK(lua->evaluateBoolean("saved > 0"));
    CHECK(s.sent().of(Opcode::CMSG_MESSAGECHAT).empty()); // reading notes never enables AI
    lua->widgets().layout(1920.0f, 1080.0f);
    CAPTURE(errors);
    CHECK(errors.empty());
}

TEST_CASE("legacy aura tooltips use raw slots and money tooltips tolerate older global strings",
          "[replay][addon][compatibility]") {
    Session s("tbc");
    addPlayer(s);
    auto* spells = s.handler().getSpellHandler();
    REQUIRE(spells != nullptr);
    spells->resetPlayerAuras(24);
    auto& helpful = spells->getPlayerAuraSlotRef(3);
    helpful.spellId = 133;
    auto& harmful = spells->getPlayerAuraSlotRef(7);
    harmful.spellId = 116;
    harmful.flags = 0x80;
    harmful.charges = 3;
    harmful.durationMs = harmful.maxDurationMs = 60000;
    harmful.receivedAtMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    auto& secondHarmful = spells->getPlayerAuraSlotRef(15);
    secondHarmful.spellId = 118;
    secondHarmful.flags = 0x80;
    std::vector<std::string> errors;
    wowee::addons::AddonManager manager;
    REQUIRE(manager.initialize(&s.handler()));
    auto* lua = manager.getLuaEngine();
    REQUIRE(lua != nullptr);
    lua->setLuaErrorCallback([&](const auto& error) { errors.push_back(error); });
    REQUIRE(lua->executeString("compatTip=CreateFrame('GameTooltip','CompatAuraTooltip',UIParent); compatTip:SetPlayerBuff(GetPlayerBuff(0,'HELPFUL'))"));
    CHECK(lua->evaluateBoolean("compatTip.__spellId == 133"));
    auto* tooltip = lua->widgets().findByName("CompatAuraTooltip");
    REQUIRE(tooltip != nullptr);
    REQUIRE_FALSE(tooltip->tooltipLines.empty());
    CHECK(tooltip->tooltipLines[0].lc[1] == 1.0f);
    REQUIRE(lua->executeString("compatTip:SetPlayerBuff(GetPlayerBuff(0,'HARMFUL'))"));
    CHECK(lua->evaluateBoolean("compatTip.__spellId == 116"));
    REQUIRE_FALSE(tooltip->tooltipLines.empty());
    CHECK(tooltip->tooltipLines[0].lc[1] == 0.0f); // harmful title is red
    CHECK(std::any_of(tooltip->tooltipLines.begin(), tooltip->tooltipLines.end(), [](const auto& line) {
        return line.left == "3 stacks";
    }));
    CHECK(std::any_of(tooltip->tooltipLines.begin(), tooltip->tooltipLines.end(), [](const auto& line) {
        return line.left.find("sec remaining") != std::string::npos;
    }));
    REQUIRE(lua->executeString("compatTip:SetPlayerBuff(GetPlayerBuff(1,'HARMFUL'))"));
    CHECK(lua->evaluateBoolean("compatTip.__spellId == 118")); // second debuff is raw slot 15, not index 2
    for (const auto* slot : {"0", "-1", "24", "1.5", "1e100"}) {
        REQUIRE(lua->executeString(std::string("compatTip:SetPlayerBuff(") + slot + ")"));
        CHECK(lua->evaluateBoolean("compatTip.__spellId == nil"));
        CHECK(tooltip->tooltipLines.empty());
    }
    REQUIRE(lua->executeString("GOLD_AMOUNT_TEXTURE=nil; SILVER_AMOUNT_TEXTURE=nil; COPPER_AMOUNT_TEXTURE=nil"));
    CHECK(lua->evaluateBoolean(R"(GetCoinTextureString(198156) == '19|TInterface\\MoneyFrame\\UI-GoldIcon:0:0:2:0|t 81|TInterface\\MoneyFrame\\UI-SilverIcon:0:0:2:0|t 56|TInterface\\MoneyFrame\\UI-CopperIcon:0:0:2:0|t')"));
    CHECK(lua->evaluateBoolean(R"(GetCoinTextureString(0) == '0|TInterface\\MoneyFrame\\UI-CopperIcon:0:0:2:0|t')"));
    REQUIRE(lua->executeString("GOLD_AMOUNT_TEXTURE=function() end; SILVER_AMOUNT_TEXTURE=4; COPPER_AMOUNT_TEXTURE={}"));
    CHECK(lua->evaluateBoolean("GetCoinTextureString(10101):find('UI-GoldIcon',1,true) ~= nil"));
    REQUIRE(lua->executeString("GOLD_AMOUNT_TEXTURE='%d gold'; SILVER_AMOUNT_TEXTURE='%d silver'; COPPER_AMOUNT_TEXTURE='%d copper'"));
    CHECK(lua->evaluateBoolean("GetCoinTextureString(198156) == '19 gold 81 silver 56 copper'"));
    CHECK(s.sent().of(Opcode::CMSG_MESSAGECHAT).empty());
    CAPTURE(errors);
    CHECK(errors.empty());
}
