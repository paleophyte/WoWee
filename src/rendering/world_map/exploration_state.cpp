// exploration_state.cpp — Fog of war / exploration tracking implementation.
// Extracted from WorldMap::updateExploration, setServerExplorationMask
// (Phase 3 of refactoring plan).
#include "rendering/world_map/exploration_state.hpp"
#include "rendering/world_map/coordinate_projection.hpp"
#include "core/logger.hpp"

#include <cmath>

namespace wowee {
namespace rendering {
namespace world_map {

void ExplorationState::setServerMask(const std::vector<uint32_t>& masks, bool hasData) {
    if (!hasData || masks.empty()) {
        // New session or no data yet — reset both server mask and local accumulation
        if (hasServerMask_) {
            locallyExploredZones_.clear();
        }
        hasServerMask_ = false;
        serverMask_.clear();
        return;
    }
    hasServerMask_ = true;
    serverMask_ = masks;
}

bool ExplorationState::isBitSet(uint32_t bitIndex) const {
    if (!hasServerMask_ || serverMask_.empty()) return false;
    const size_t word = bitIndex / 32;
    if (word >= serverMask_.size()) return false;
    return (serverMask_[word] & (1u << (bitIndex % 32))) != 0;
}

void ExplorationState::update(const std::vector<Zone>& zones,
                               const glm::vec3& playerRenderPos,
                               int currentZoneIdx,
                               const std::unordered_map<uint32_t, uint32_t>& exploreFlagByAreaId) {
    overlaysChanged_ = false;

    if (hasServerMask_) {
        exploredZones_.clear();
        for (int i = 0; i < static_cast<int>(zones.size()); i++) {
            const auto& z = zones[i];
            if (z.areaID == 0 || z.exploreBits.empty()) continue;
            for (uint32_t bit : z.exploreBits) {
                if (isBitSet(bit)) {
                    exploredZones_.insert(i);
                    break;
                }
            }
        }
        // Also reveal the zone the player is currently standing in so the map isn't
        // pitch-black the moment they first enter a new zone.
        int curZone = findZoneForPlayer(zones, playerRenderPos);
        if (curZone >= 0) exploredZones_.insert(curZone);

        // Per-overlay exploration: check each overlay's areaIDs against the exploration mask
        std::unordered_set<int> newExploredOverlays;
        if (currentZoneIdx >= 0 && currentZoneIdx < static_cast<int>(zones.size())) {
            const auto& curZoneData = zones[currentZoneIdx];
            for (int oi = 0; oi < static_cast<int>(curZoneData.overlays.size()); oi++) {
                const auto& ov = curZoneData.overlays[oi];
                bool revealed = false;
                for (int a = 0; a < 4; a++) {
                    if (ov.areaIDs[a] == 0) continue;
                    auto flagIt = exploreFlagByAreaId.find(ov.areaIDs[a]);
                    if (flagIt != exploreFlagByAreaId.end() && isBitSet(flagIt->second)) {
                        revealed = true;
                        break;
                    }
                }
                if (revealed) newExploredOverlays.insert(oi);
            }
        }
        if (newExploredOverlays != exploredOverlays_) {
            exploredOverlays_ = std::move(newExploredOverlays);
            overlaysChanged_ = true;
        }

        // One-shot-per-zone diagnostic: when a zone with overlays reveals none of them
        // (the zone map draws full fog), dump why — how many mask bits are set overall,
        // the zone's own AreaBit state, and each overlay's first AreaID → ExploreFlag →
        // bit state. This distinguishes "server never set the bits" from a client mapping
        // bug. Logged only when the viewed zone changes, so it is not spammy.
        if (currentZoneIdx >= 0 && currentZoneIdx != diagLoggedZone_ &&
            currentZoneIdx < static_cast<int>(zones.size())) {
            const auto& cz = zones[currentZoneIdx];
            if (!cz.overlays.empty() && exploredOverlays_.empty()) {
                diagLoggedZone_ = currentZoneIdx;
                size_t setBits = 0;
                for (uint32_t w : serverMask_) setBits += static_cast<size_t>(__builtin_popcount(w));
                uint32_t zoneBit = 0;
                auto zbIt = exploreFlagByAreaId.find(cz.areaID);
                if (zbIt != exploreFlagByAreaId.end()) zoneBit = zbIt->second;
                LOG_INFO("[EXPLORE-DIAG] zone='", cz.areaName, "' areaID=", cz.areaID,
                         " zoneBit=", zoneBit, " zoneBitSet=", isBitSet(zoneBit),
                         " totalMaskBitsSet=", setBits, " maskWords=", serverMask_.size(),
                         " overlays=", cz.overlays.size(), " (none revealed)");
                int shown = 0;
                for (const auto& ov : cz.overlays) {
                    if (shown++ >= 6) break;
                    uint32_t aid = ov.areaIDs[0];
                    auto fIt = exploreFlagByAreaId.find(aid);
                    uint32_t flag = (fIt != exploreFlagByAreaId.end()) ? fIt->second : 0xFFFFFFFFu;
                    LOG_INFO("[EXPLORE-DIAG]   overlay areaID=", aid, " exploreFlag=", flag,
                             " bitSet=", (flag != 0xFFFFFFFFu && isBitSet(flag)));
                }
            } else {
                diagLoggedZone_ = currentZoneIdx;
            }
        }
        return;
    }

    // Server mask unavailable — fall back to locally-accumulated position tracking.
    float wowX = playerRenderPos.y;
    float wowY = playerRenderPos.x;

    bool foundPos = false;
    for (int i = 0; i < static_cast<int>(zones.size()); i++) {
        const auto& z = zones[i];
        if (z.areaID == 0) continue;
        float minX = std::min(z.bounds.locLeft, z.bounds.locRight);
        float maxX = std::max(z.bounds.locLeft, z.bounds.locRight);
        float minY = std::min(z.bounds.locTop, z.bounds.locBottom);
        float maxY = std::max(z.bounds.locTop, z.bounds.locBottom);
        if (maxX - minX < 0.001f || maxY - minY < 0.001f) continue;
        if (wowX >= minX && wowX <= maxX && wowY >= minY && wowY <= maxY) {
            locallyExploredZones_.insert(i);
            foundPos = true;
        }
    }

    if (!foundPos) {
        int zoneIdx = findZoneForPlayer(zones, playerRenderPos);
        if (zoneIdx >= 0) locallyExploredZones_.insert(zoneIdx);
    }

    // Display the accumulated local set
    exploredZones_ = locallyExploredZones_;

    // Without server mask, mark all overlays as explored (no fog of war)
    exploredOverlays_.clear();
    if (currentZoneIdx >= 0 && currentZoneIdx < static_cast<int>(zones.size())) {
        for (int oi = 0; oi < static_cast<int>(zones[currentZoneIdx].overlays.size()); oi++)
            exploredOverlays_.insert(oi);
    }
}

} // namespace world_map
} // namespace rendering
} // namespace wowee
