// src/game/transport_animator.cpp
// Path evaluation, Z clamping, and orientation for transports.
// Extracted from TransportManager::updateTransportMovement (Phase 3b of spline refactoring).
#include "game/transport_animator.hpp"
#include "game/transport_manager.hpp"
#include "game/transport_path_repository.hpp"
#include "math/spline.hpp"
#include "core/logger.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace wowee::game {

void TransportAnimator::evaluateAndApply(
    ActiveTransport& transport,
    const PathEntry& pathEntry,
    uint32_t pathTimeMs) const
{
    const auto& spline = pathEntry.spline;

    // Evaluate position from time via CatmullRomSpline (path is local offsets, add base position)
    glm::vec3 pathOffset = spline.evaluatePosition(pathTimeMs);

    // Catmull-Rom splines aren't constrained to the convex hull of their control
    // points. TransportAnimation.dbc uses sparse keyframes near each Deeprun Tram
    // station, and live position polling showed the evaluated path visibly
    // overshooting past the authored stop keyframe before correcting back to it
    // (observed ~12 units of dip-and-recover on final approach) - reported live
    // as cars not quite lining up with the platform ramps. Clamp X/Y to the
    // authored keyframe extents (in raw, pre-mirror spline space) to remove the
    // overshoot without touching the keyframe values themselves. Z has its own
    // clampZOffset() below for the same underlying spline behavior.
    bool xyClamped = false;
    // Populated for tram entries below; also decides the 176085 mirror.
    glm::vec3 keyMin(std::numeric_limits<float>::max());
    glm::vec3 keyMax(std::numeric_limits<float>::lowest());
    if (TransportManager::isDeeprunTramTransport(transport) && !spline.keys().empty()) {
        for (const auto& key : spline.keys()) {
            keyMin = glm::min(keyMin, key.position);
            keyMax = glm::max(keyMax, key.position);
        }
        const float clampedX = std::clamp(pathOffset.x, keyMin.x, keyMax.x);
        const float clampedY = std::clamp(pathOffset.y, keyMin.y, keyMax.y);
        xyClamped = (clampedX != pathOffset.x) || (clampedY != pathOffset.y);
        pathOffset.x = clampedX;
        pathOffset.y = clampedY;
    }

    // Entry 176085's TransportAnimation.dbc path data is mirrored relative to its real
    // train siblings (176080, 176081): diagnostic dump of all six entries' raw key
    // extents showed 176080/176081 spanning local X=[0,+2482] (real-world X correctly
    // increasing toward Stormwind, matching their spawn near Ironforge), while 176085
    // spans local X=[-2482,0] - the same negative-direction convention as the genuinely
    // Stormwind-side cars (176082-176084, correct for THEM since their real-world X
    // needs to decrease toward Ironforge). 176085 spawns at real-world X=-11 (Ironforge
    // side) but its path pulls it further NEGATIVE instead of toward Stormwind's
    // positive coordinates, driving it off the edge of the modeled tunnel entirely -
    // "took me outside of map bounds and back" reported live, on the one car
    // consistently observed going the wrong way. Y is negligible for all six paths
    // (confirmed ~0 in the same dump), so a straight X negation is enough to mirror
    // this one entry's local path back into the same real-world direction as its
    // siblings, without needing a general per-transport reverse/mirror flag for what's
    // so far a single-entry data quirk.
    // Only mirror when this entry's data actually uses the negative-X
    // convention (vanilla/TBC). WotLK's re-export gives 176085 the same
    // positive-X frame as its siblings (after the loader's Y-major rotation),
    // so an unconditional entry check would drive it off-tunnel there.
    const bool tramMirroredData = keyMin.x < -100.0f && keyMax.x < 100.0f;
    if (transport.entry == 176085u && tramMirroredData) {
        pathOffset.x = -pathOffset.x;
    }

    pathOffset.z = clampZOffset(
        pathOffset.z,
        pathEntry.worldCoords,
        transport.useClientAnimation,
        transport.serverUpdateCount,
        transport.hasServerClock);

    transport.position = transport.basePosition + pathOffset;

    // The affected ship routes need entry-specific berth headings at their
    // repeated-position TaxiPath dwell nodes. Blend during the final/first five
    // seconds and hold the exact authored position for the 60-second stop.
    float shipDockBlend = 0.0f;
    glm::vec3 shipApproach(0.0f);
    bool shipAtDockDwell = false;
    glm::vec3 shipDockPosition(0.0f);
    // Keep this entry-scoped: display 7087 is shared by other night-elf ships
    // across expansions, and those routes must retain their existing docking
    // orientation unless their own geometry proves they need the maneuver.
    const bool needsSideOnDock = transport.entry == 176310u ||
                                 transport.entry == 176244u ||
                                 transport.entry == 181646u;
    if (needsSideOnDock && pathEntry.worldCoords) {
        constexpr uint32_t kDockTurnMs = 5000u;
        const auto& keys = spline.keys();
        for (size_t i = 1; i + 1 < keys.size(); ++i) {
            const glm::vec3 dwellDelta = keys[i].position - keys[i + 1].position;
            if (glm::dot(dwellDelta, dwellDelta) > 0.01f) continue;
            const uint32_t dwellStart = keys[i].timeMs;
            const uint32_t dwellEnd = keys[i + 1].timeMs;
            const uint32_t turnStart = dwellStart > kDockTurnMs
                ? dwellStart - kDockTurnMs : 0u;
            if (pathTimeMs < turnStart || pathTimeMs > dwellEnd + kDockTurnMs) {
                continue;
            }

            shipApproach = keys[i].position - keys[i - 1].position;
            shipApproach.z = 0.0f;
            const float approachLen = glm::length(shipApproach);
            if (approachLen <= 0.001f) break;
            shipApproach /= approachLen;

            if (pathTimeMs < dwellStart) {
                shipDockBlend = static_cast<float>(
                    pathTimeMs - turnStart) / static_cast<float>(kDockTurnMs);
            } else if (pathTimeMs <= dwellEnd) {
                shipDockBlend = 1.0f;
                shipAtDockDwell = true;
                shipDockPosition = keys[i].position;
            } else {
                shipDockBlend = 1.0f - static_cast<float>(
                    pathTimeMs - dwellEnd) / static_cast<float>(kDockTurnMs);
            }
            shipDockBlend = std::clamp(shipDockBlend, 0.0f, 1.0f);
            shipDockBlend = shipDockBlend * shipDockBlend *
                            (3.0f - 2.0f * shipDockBlend);
            break;
        }
    }
    if (shipAtDockDwell) {
        // Catmull-Rom evaluation can sit slightly off a repeated-position key
        // even during its authored hold. Pin the actual dwell to the TaxiPath
        // node so the gangway does not retain a visible one-unit gap.
        transport.position = transport.basePosition + shipDockPosition;
    }

    // Use server yaw if available (authoritative), otherwise compute from spline tangent
    if (transport.hasServerYaw) {
        float effectiveYaw = transport.serverYaw +
            (transport.serverYawFlipped180 ? glm::pi<float>() : 0.0f);
        transport.rotation = glm::angleAxis(effectiveYaw, glm::vec3(0.0f, 0.0f, 1.0f));
    } else if (xyClamped) {
        // The tangent below comes from a separate, unclamped evaluation of the same
        // spline - it doesn't know the position is currently pinned at the keyframe
        // boundary above. Near the station, the raw (unclamped) path loops through a
        // little overshoot-and-recover S-curve, and its tangent sweeps through whatever
        // instantaneous directions that loop produces - including briefly near-
        // perpendicular to the direction of travel - while the rendered position sits
        // still. Reported live as cars "turning sideways for a brief period before
        // leaving the station." Leave transport.rotation at its last computed value
        // while the position is clamped; there's no real facing change to reflect since
        // the car isn't actually moving from the rider's point of view during that
        // window.
    } else {
        auto result = spline.evaluate(pathTimeMs);
        glm::vec3 tangent = result.tangent;
        // Mirror the tangent's X to match the position mirror above, so facing
        // direction stays consistent with this entry's (corrected) direction of travel.
        if (transport.entry == 176085u && tramMirroredData) {
            tangent.x = -tangent.x;
        }
        // orientationFromTangent orients along the full 3D tangent, pitching/banking to
        // match vertical slope - correct for something like a boat cresting swells, but
        // a subway car shouldn't nose-dive on a downhill grade the way this made it look
        // ("angling downwards instead of staying flat" reported live). Flattening
        // tangent.z to 0 fixed that, but a subsequent live test reported the level car
        // visually clipping into the sloped tunnel floor on grade changes, so that was
        // changed to a clamped partial pitch instead of a full flatten. Confirmed via a
        // later live comparison against the real game client that this was the wrong
        // trade-off: in the real client, tram cars stay level (parallel to the ground)
        // through elevation changes - any clamped tilt is visibly wrong regardless of
        // whether it also reduces clipping. Reverted to a full flatten to match the
        // real client's confirmed behavior; other transports keep full tangent-based
        // orientation.
        const bool isDeeprunTram =
            transport.displayId == 3831u ||
            (transport.entry >= 176080u && transport.entry <= 176085u) ||
            (transport.pathId >= 176080u && transport.pathId <= 176085u);
        if (isDeeprunTram) {
            tangent.z = 0.0f;
        }
        const float tangentLenSq = glm::dot(tangent, tangent);
        if (tangentLenSq <= 1e-6f && shipDockBlend > 0.0f) {
            tangent = shipApproach;
        }
        const float effectiveTangentLenSq = glm::dot(tangent, tangent);
        if (effectiveTangentLenSq > 1e-6f) {
            if (pathEntry.worldCoords && !transport.isM2) {
                // TaxiPathNode coordinates were converted server -> canonical by
                // swapping X/Y. WMO transport models face server-space +X, so derive
                // the same yaw the server would send from the canonical tangent.
                // The generic spline helper uses a different local-forward convention
                // and mirrored ship yaw, producing sideways/backwards sailing.
                // Transport WMO hulls are authored with their bow opposite the
                // model-space +X axis used by the raw route yaw.
                float routeYaw = std::atan2(tangent.x, tangent.y);
                const bool reversedShipHull = transport.entry == 176310u ||
                                              transport.entry == 176244u ||
                                              transport.entry == 181646u ||
                                              transport.entry == 190536u;
                if (reversedShipHull) {
                    // These WMO hulls are authored with their visible bow opposite
                    // the route-local forward axis. Without this correction they
                    // translate stern-first even though the spline tangent is right.
                    routeYaw += glm::pi<float>();
                }
                float effectiveYaw = routeYaw;
                if (shipDockBlend > 0.0f && needsSideOnDock) {
                    // A GO query reports the transport's orientation at the instant
                    // it is received, not a persistent heading for every berth. Using
                    // that snapshot as dockYaw made Bravery's result depend on where
                    // she happened to be when the player logged in. These TaxiPath
                    // routes run parallel to their berths, so their corrected route
                    // heading is also the stable broadside dock heading.
                    float dockTargetYaw = routeYaw;
                    if (transport.entry == 176310u ||
                        transport.entry == 176244u ||
                        transport.entry == 181646u) {
                        // These Auberdine TaxiPath segments run parallel to their
                        // berths. The server spawn yaw points the hull diagonally or
                        // bow-first at the dock; keep the corrected route heading for
                        // a broadside stop. This remains entry-scoped so ships in
                        // other expansions retain their established orientation.
                        dockTargetYaw = routeYaw;
                    }
                    const float dockDelta = std::remainder(
                        dockTargetYaw - routeYaw, glm::two_pi<float>());
                    effectiveYaw += shipDockBlend * dockDelta;
                }
                transport.rotation = glm::angleAxis(
                    effectiveYaw, glm::vec3(0.0f, 0.0f, 1.0f));
                if (shipDockBlend > 0.999f && needsSideOnDock) {
                    static std::unordered_set<uint64_t> loggedDockGuids;
                    if (loggedDockGuids.insert(transport.guid).second) {
                        LOG_WARNING("SHIP DOCK DIAG entry=", transport.entry,
                                    " guid=0x", std::hex, transport.guid, std::dec,
                                    " pathTime=", pathTimeMs,
                                    " position=(", transport.position.x, ",",
                                    transport.position.y, ",", transport.position.z, ")",
                                    " tangent=(", tangent.x, ",", tangent.y, ")",
                                    " routeYaw=", routeYaw,
                                    " hasDockYaw=", transport.hasDockYaw,
                                    " dockYaw=", transport.dockYaw,
                                    " effectiveYaw=", effectiveYaw);
                    }
                }
            } else {
                transport.rotation = math::CatmullRomSpline::orientationFromTangent(tangent);
            }
        } else if (pathEntry.worldCoords && !transport.isM2 && transport.hasDockYaw) {
            // TaxiPathNode route builders encode a dock wait with repeated
            // positions. With no movement tangent, restore the GO's authored
            // spawn orientation so the ship lies alongside the dock rather
            // than retaining its bow-first approach yaw throughout the dwell.
            transport.rotation = glm::angleAxis(
                transport.dockYaw, glm::vec3(0.0f, 0.0f, 1.0f));
        }
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
