// binding_file.hpp - the saved key bindings, one `COMMAND=KEY1,KEY2` per line.
#pragma once

#include <array>
#include <istream>
#include <map>
#include <string>

namespace wowee::addons {

using SavedBindings = std::map<std::string, std::array<std::string, 2>>;

/// Every command line in the file. The second key is empty when there is none,
/// a line without `=` is skipped, and a trailing carriage return from a file
/// edited on Windows is not part of the key.
inline SavedBindings parseSavedBindings(std::istream& in) {
    SavedBindings out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const std::string rest = line.substr(eq + 1);
        const size_t comma = rest.find(',');
        out[line.substr(0, eq)] = {
            rest.substr(0, comma),
            comma == std::string::npos ? "" : rest.substr(comma + 1)
        };
    }
    return out;
}

} // namespace wowee::addons
