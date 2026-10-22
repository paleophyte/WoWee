#pragma once
#include <csignal>
#include <cstdlib>
namespace wowee::core {
// Only signal-safe operations here. A second signal forces a non-successful exit.
inline volatile std::sig_atomic_t sessionStopSignal = 0;
inline void handleSessionStopSignal(int signal) {
    if (sessionStopSignal) std::_Exit(2);
    sessionStopSignal = signal;
}
}
