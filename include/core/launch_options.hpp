#pragma once
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wowee::core {
struct LaunchOptions {
    std::string profile, realm, character, controlSocket;
    unsigned startupTimeoutSeconds = 180;
    bool help = false;
    bool enabled() const { return !profile.empty(); }
};
inline LaunchOptions parseLaunchOptions(int argc, char* const argv[]) {
    LaunchOptions out;
    bool timeoutSeen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") { out.help = true; continue; }
        auto value = [&]() -> std::string {
            if (++i >= argc || !*argv[i] || std::string_view(argv[i]).starts_with("--"))
                throw std::invalid_argument("Missing launch argument value");
            return argv[i];
        };
        auto assign = [&](std::string& field) {
            if (!field.empty()) throw std::invalid_argument("Duplicate launch argument");
            field = value();
        };
        if (arg == "--control-socket") assign(out.controlSocket);
        else if (arg == "--login-profile") assign(out.profile);
        else if (arg == "--realm") assign(out.realm);
        else if (arg == "--character") assign(out.character);
        else if (arg == "--startup-timeout") {
            if (timeoutSeen) throw std::invalid_argument("Duplicate startup timeout");
            timeoutSeen = true;
            const auto text = value(); unsigned seconds = 0;
            const auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), seconds);
            if (ec != std::errc{} || end != text.data()+text.size() || seconds < 1 || seconds > 600)
                throw std::invalid_argument("Startup timeout must be 1..600 seconds");
            out.startupTimeoutSeconds = seconds;
        } else throw std::invalid_argument("Unknown launch argument (use --help)");
    }
    if (!out.help && (out.enabled() != !out.character.empty() ||
        (!out.enabled() && (!out.realm.empty() || timeoutSeen))))
        throw std::invalid_argument("--login-profile and --character are required together");
    return out;
}
// Exact matching only: duplicates are errors, never select the first match.
inline int uniqueLaunchMatch(const std::vector<std::vector<std::string>>& aliases,
                             const std::string& wanted) {
    int match = -1;
    for (size_t i = 0; i < aliases.size(); ++i) {
        bool found = false;
        for (const auto& name : aliases[i]) if (name == wanted) found = true;
        if (!found) continue;
        if (match >= 0) throw std::invalid_argument("Launch selection is ambiguous");
        match = static_cast<int>(i);
    }
    if (match < 0) throw std::invalid_argument("Launch selection was not found");
    return match;
}
inline constexpr const char* launchHelp =
    "Usage: wowee [--login-profile LABEL_OR_HOST:PORT --character NAME]\n"
    "             [--realm NAME] [--startup-timeout SECONDS]\n"
    "             [--control-socket ABSOLUTE_PATH_IN_PRIVATE_DIRECTORY]\n"
    "Uses saved credentials; exact names only. Multiple realms require --realm.\n"
    "Startup timeout: 1..600 seconds (default 180). No automatic reconnect.\n"
    "Does not change bot control or impose a gameplay time limit.\n";
}
