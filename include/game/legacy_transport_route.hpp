#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace wowee::game {
struct TaxiRoute;

struct LegacyTransportPose {
    glm::vec3 position{}; // canonical coordinates
    glm::vec3 tangent{};
    bool present = false;
    bool stopped = false;
};

// Classic-era ShipPath timing: authored endpoint controls, twenty samples per
// curve segment, acceleration/braking and absolute dock delays. TBC's ordinary
// update trailer supplies the running clock against this derived period.
class LegacyTransportRoute {
public:
    static std::shared_ptr<const LegacyTransportRoute> build(
        const TaxiRoute& route, float speed, float acceleration);
    [[nodiscard]] uint32_t periodMs() const { return periodMs_; }
    [[nodiscard]] LegacyTransportPose evaluate(uint32_t timeMs, uint32_t mapId) const;
private:
    struct Stop { float distance; uint32_t arriveMs, departMs; };
    struct Leg {
        uint32_t mapId, beginMs, endMs;
        std::vector<glm::vec3> controls;
        std::vector<float> lengths;
        std::vector<Stop> stops;
        float distance = 0;
    };
    std::vector<Leg> legs_;
    float speed_ = 0, acceleration_ = 0;
    uint32_t periodMs_ = 0;
};
} // namespace wowee::game
