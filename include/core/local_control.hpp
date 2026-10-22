#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
namespace wowee::core {
// Main-thread, nonblocking, bounded local transport. The callback owns control semantics.
class LocalControlServer {
public:
    using Json = nlohmann::json;
    using Handler = std::function<Json(const std::string&, const std::string&)>;
    using Clock = std::chrono::steady_clock;
    LocalControlServer(const std::string& socketPath, Handler handler);
    ~LocalControlServer();
    LocalControlServer(const LocalControlServer&) = delete;
    LocalControlServer& operator=(const LocalControlServer&) = delete;
    void tick(Clock::time_point now = Clock::now());
    bool hasClients() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
