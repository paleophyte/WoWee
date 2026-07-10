// src/game/transport_animator.cpp
// Path evaluation, Z clamping, and orientation for transports.
// Extracted from TransportManager::updateTransportMovement (Phase 3b of spline refactoring).
#include "game/transport_animator.hpp"
#include "game/transport_manager.hpp"
#include "game/transport_path_repository.hpp"
#include "math/spline.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace wowee::game {

void TransportAnimator::evaluateAndApply(
    ActiveTransport& transport,
    const PathEntry& pathEntry,
    uint32_t pathTimeMs) const
{
    const auto& spline = pathEntry.spline;

    // Evaluate position from time via CatmullRomSpline (path is local offsets, add base position)
    glm::vec3 pathOffset = spline.evaluatePosition(pathTimeMs);

    pathOffset.z = clampZOffset(
        pathOffset.z,
        pathEntry.worldCoords,
        transport.useClientAnimation,
        transport.serverUpdateCount,
        transport.hasServerClock);

    transport.position = transport.basePosition + pathOffset;

    // Use server yaw if available (authoritative), otherwise compute from spline tangent
    if (transport.hasServerYaw) {
        float effectiveYaw = transport.serverYaw +
            (transport.serverYawFlipped180 ? glm::pi<float>() : 0.0f);
        transport.rotation = glm::angleAxis(effectiveYaw, glm::vec3(0.0f, 0.0f, 1.0f));
    } else {
        auto result = spline.evaluate(pathTimeMs);
        glm::vec3 tangent = result.tangent;
        // orientationFromTangent orients along the full 3D tangent, pitching/banking to
        // match vertical slope - correct for something like a boat cresting swells, but
        // a subway car shouldn't nose-dive on a downhill grade the way this made it look
        // ("angling downwards instead of staying flat" reported live). Fully flattening
        // tangent.z to 0 fixed that but overcorrected: the car's position still follows
        // the real track elevation (unchanged below), so a level, unpitched model on a
        // real grade visually clips into the sloped tunnel floor ("cars clipping into
        // the ground when going up and/or down the hills" reported live). Clamp the
        // pitch instead of removing it, so the car banks gently with real grade changes
        // without the exaggerated nose-dive the raw Catmull-Rom tangent produced near
        // control points; other transports keep full tangent-based orientation.
        const bool isDeeprunTram =
            transport.displayId == 3831u ||
            (transport.entry >= 176080u && transport.entry <= 176085u) ||
            (transport.pathId >= 176080u && transport.pathId <= 176085u);
        if (isDeeprunTram) {
            const float horizLen = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
            if (horizLen > 1e-4f) {
                // ~0.3 rad (~17 degrees) max pitch either way.
                constexpr float kMaxSlope = 0.3f;
                const float maxAbsZ = horizLen * kMaxSlope;
                tangent.z = std::clamp(tangent.z, -maxAbsZ, maxAbsZ);
            }
        }
        transport.rotation = math::CatmullRomSpline::orientationFromTangent(tangent);
    }
}

float TransportAnimator::clampZOffset(float z, bool worldCoords, bool clientAnim,
                                       int serverUpdateCount, bool hasServerClock)
{
    // Skip Z clamping for world-coordinate paths (TaxiPathNode) where values are absolute positions.
    if (worldCoords) return z;

    constexpr float kMinFallbackZOffset = -2.0f;
    constexpr float kMaxFallbackZOffset =  8.0f;

    // Clamp fallback Z offsets for non-world-coordinate paths to prevent transport
    // models from sinking below sea level on paths derived only from spawn-time data
    // (notably icebreaker routes where the DBC path has steep vertical curves).
    if (clientAnim && serverUpdateCount <= 1) {
        z = std::max(z, kMinFallbackZOffset);
    }
    if (!clientAnim && !hasServerClock) {
        z = std::clamp(z, kMinFallbackZOffset, kMaxFallbackZOffset);
    }
    return z;
}

} // namespace wowee::game
