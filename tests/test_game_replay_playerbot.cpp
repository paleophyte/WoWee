#include "game_replay.hpp"
#include "core/coordinates.hpp"
#include "game/entity.hpp"
#include "game/movement_handler.hpp"
#include "rendering/camera_controller.hpp"
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
