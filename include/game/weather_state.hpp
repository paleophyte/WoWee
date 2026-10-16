#pragma once

#include <cstdint>

namespace wowee::game {

// What SMSG_WEATHER's first field asks for. It is the server's WeatherState,
// numbered the same from 1.12 to 3.3.5: 0 fine, 1 fog, 2 drizzle (TrinityCore
// only), 3-5 rain, 6-8 snow, 22/41/42 sandstorm, 86 thunderstorm, 90 black
// rain, 106 black snow. Read as 1 rain, 2 snow, 3 storm, light rain came out a
// storm, fog came out rain, and heavier rain and every snow came out nothing.
enum class WeatherKind : uint8_t { NONE, FOG, RAIN, SNOW, SANDSTORM, THUNDERSTORM };

constexpr WeatherKind weatherKind(uint32_t state) {
    switch (state) {
        case 1:                     return WeatherKind::FOG;
        case 2: case 3: case 4: case 5:
        case 90:                    return WeatherKind::RAIN;
        case 6: case 7: case 8:
        case 106:                   return WeatherKind::SNOW;
        case 22: case 41: case 42:  return WeatherKind::SANDSTORM;
        case 86:                    return WeatherKind::THUNDERSTORM;
        default:                    return WeatherKind::NONE;
    }
}

} // namespace wowee::game
