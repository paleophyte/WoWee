#pragma once

#include <cstddef>
#include <cstdint>

namespace wowee::core {

void installCrashDiagnostics(const char* processLabel);
void setCrashBreadcrumb(const char* phase,
                        uint16_t wireOpcode,
                        const char* logicalOpcode,
                        std::size_t packetSize,
                        std::size_t readPos,
                        int worldState);
void setCrashNote(const char* note);

} // namespace wowee::core
