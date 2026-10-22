#pragma once
#include <cstdint>
namespace wowee::game {
// Facts from server packets, distinct from local countdowns and UI exit callbacks.
struct LogoutObservation {
    uint64_t responses = 0, completions = 0;
    uint32_t result = 0;
};
}
