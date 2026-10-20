// A one-millisecond period difference becomes seconds of phase error after
// many cycles. Keep arc-length arithmetic reproducible on Apple Silicon/x86.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#elif defined(_MSC_VER)
#pragma float_control(precise, on)
#pragma fp_contract(off)
#endif

#include "game/legacy_transport_route.hpp"
#include "game/transport_path_repository.hpp"
#include <algorithm>
#include <cmath>

namespace wowee::game {
namespace {
struct CurveSample { glm::vec3 position, tangent; };
CurveSample curve(const glm::vec3* p, float t) {
    // Evaluate weights before combining coordinates. This preserves the float
    // arithmetic used by the legacy path's arc-length table.
    const float t2 = t*t, t3 = t2*t;
    return {
        p[0]*(-0.5f*t3+t2-0.5f*t) + p[1]*(1.5f*t3-2.5f*t2+1) +
        p[2]*(-1.5f*t3+2*t2+0.5f*t) + p[3]*(0.5f*t3-0.5f*t2),
        p[0]*(-1.5f*t2+2*t-0.5f) + p[1]*(4.5f*t2-5*t) +
        p[2]*(-4.5f*t2+4*t+0.5f) + p[3]*(1.5f*t2-t)};
}
uint32_t milliseconds(float seconds) {
    if (!std::isfinite(seconds) || seconds > 86400) return 86400001;
    return static_cast<uint32_t>(std::floor(seconds*1000.0f+0.5f));
}
}

std::shared_ptr<const LegacyTransportRoute> LegacyTransportRoute::build(
        const TaxiRoute& route, float speed, float acceleration) {
    if (!std::isfinite(speed) || speed < 0.01f || speed > 500 ||
        !std::isfinite(acceleration) || acceleration < 0.01f || acceleration > 500 ||
        route.nodes.size() < 4 || route.nodes.size() > 4096) return {};
    auto result = std::make_shared<LegacyTransportRoute>();
    result->speed_ = speed;
    result->acceleration_ = acceleration;
    const float rampTime = speed/acceleration;
    const float rampDistance = 0.5f*speed*rampTime;
    const auto endTime = [&](float distance) {
        return distance > rampDistance ? (distance-rampDistance)/speed+rampTime :
            std::sqrt(2*distance/acceleration);
    };
    const auto middleTime = [&](float distance) {
        return distance > 2*rampDistance ? distance/speed+rampTime :
            2*std::sqrt(distance/acceleration);
    };
    size_t first = 0;
    while (first < route.nodes.size()) {
        size_t end = first+1;
        while (end < route.nodes.size() && route.nodes[end].mapId == route.nodes[first].mapId &&
               !(route.nodes[end-1].actionFlags & 1u)) ++end;
        if (end-first < 4) return {}; // Never invent missing endpoint controls.
        Leg leg{};
        leg.mapId = route.nodes[first].mapId;
        leg.beginMs = result->periodMs_;
        for (size_t i=first; i<end; ++i) {
            const auto& point = route.nodes[i].position;
            for (int axis=0; axis<3; ++axis)
                if (!std::isfinite(point[axis]) || std::abs(point[axis]) > 100000) return {};
            if (route.nodes[i].dwellMs > 3600000) return {};
            leg.controls.push_back(point);
        }
        // The first and last rows are tangent controls, not extra travel legs.
        for (size_t i=0; i+3<leg.controls.size(); ++i) {
            glm::vec3 previous = leg.controls[i+1];
            float length = 0;
            for (int step=1; step<=20; ++step) {
                const auto point = curve(&leg.controls[i], static_cast<float>(step)/20).position;
                length += glm::length(point-previous);
                previous = point;
            }
            if (!std::isfinite(length)) return {};
            leg.lengths.push_back(length);
            leg.distance += length;
        }
        if (leg.distance <= 0) return {};
        float covered = 0;
        for (size_t i=1; i+1<leg.controls.size(); ++i) {
            if (i>1) covered += leg.lengths[i-2];
            const auto& node = route.nodes[first+i];
            if (!(node.actionFlags & 2u) || !node.dwellMs) continue;
            const float previous = leg.stops.empty() ? 0 : leg.stops.back().distance;
            const uint32_t departure = leg.stops.empty() ? leg.beginMs : leg.stops.back().departMs;
            const float seconds = leg.stops.empty() ? endTime(covered) : middleTime(covered-previous);
            const uint32_t arrival = departure + milliseconds(seconds);
            if (arrival > 86400000 || arrival+node.dwellMs > 86400000) return {};
            leg.stops.push_back({covered, arrival, arrival+node.dwellMs});
        }
        leg.endMs = leg.stops.empty() ? leg.beginMs+milliseconds(leg.distance/speed) :
            leg.stops.back().departMs+milliseconds(endTime(leg.distance-leg.stops.back().distance));
        if (leg.endMs <= leg.beginMs || leg.endMs > 86400000) return {};
        result->periodMs_ = leg.endMs;
        result->legs_.push_back(std::move(leg));
        first = end;
    }
    return result;
}

LegacyTransportPose LegacyTransportRoute::evaluate(uint32_t timeMs, uint32_t mapId) const {
    LegacyTransportPose result;
    if (!periodMs_) return result;
    const uint32_t now = timeMs%periodMs_;
    for (const auto& leg : legs_) {
        if (now < leg.beginMs || now >= leg.endMs) continue;
        if (leg.mapId != mapId) return result;
        result.present = true;
        const auto traveled = [&](float seconds) {
            const float ramp = speed_/acceleration_;
            return seconds < ramp ? 0.5f*acceleration_*seconds*seconds :
                speed_*(seconds-0.5f*ramp);
        };
        float distance = (now-leg.beginMs)*0.001f*speed_;
        bool located = false;
        uint32_t depart = leg.beginMs;
        float previous = 0;
        bool hasPreviousStop = false;
        for (const auto& stop : leg.stops) {
            if (now < stop.arriveMs) {
                const float left = (stop.arriveMs-now)*0.001f;
                const float elapsed = (now-depart)*0.001f;
                distance = hasPreviousStop && elapsed < left ? previous+traveled(elapsed) :
                    stop.distance-traveled(left);
                located = true;
                break;
            }
            if (now < stop.departMs) {
                distance = stop.distance;
                result.stopped = true;
                located = true;
                break;
            }
            depart = stop.departMs;
            previous = stop.distance;
            hasPreviousStop = true;
        }
        if (!located && hasPreviousStop) distance = previous+traveled((now-depart)*0.001f);
        distance = std::clamp(distance, 0.0f, leg.distance);
        size_t segment = 0;
        while (segment+1<leg.lengths.size() && distance>leg.lengths[segment]) {
            distance -= leg.lengths[segment];
            ++segment;
        }
        const float parameter = leg.lengths[segment] > 0 ? distance/leg.lengths[segment] : 0;
        const auto sample = curve(&leg.controls[segment], parameter);
        result.position = sample.position;
        result.tangent = sample.tangent;
        return result;
    }
    return result;
}
} // namespace wowee::game
