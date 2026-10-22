#pragma once
#include "game/logout_observation.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
namespace wowee::core {
class ManagedSession {
public:
    using Clock=std::chrono::steady_clock;
    using Json=nlohmann::json;
    enum class Action { Manual, Logout, Disconnect, Cancel };
    struct Observation {
        bool inWorld=false, connected=false, loggingOut=false;
        game::LogoutObservation logout;
        Json control=Json::object();
    };
    ManagedSession(unsigned durationSeconds=0,unsigned stopTimeoutSeconds=45);
    bool beginStop(const std::string& id,const std::string& reason,Clock::time_point now);
    std::vector<Action> tick(Clock::time_point now,const Observation& observation);
    void interrupt(const std::string& reason);
    bool stopping() const;
    bool terminal() const;
    bool completedLogout(uint64_t sequence) const;
    const std::string& id() const { return id_; }
    Json snapshot() const;
    int exitCode() const { return phase_=="finished"?0:2; }
private:
    std::string phase_="waiting",id_,reason_,failure_;
    unsigned duration_,stopTimeout_;
    bool manualIssued_=false,manualConfirmed_=false,logoutConfirmed_=false,disconnected_=false;
    Clock::time_point entered_{},stopStarted_{};
    bool enteredOnce_=false;
    uint64_t logoutBaseline_=0,responseBaseline_=0;
    Json manualEvidence_=Json::object();
};
// Private, exclusive per-run receipt files; atomic current state and append-only transitions.
class SessionJournal {
public:
    explicit SessionJournal(const std::string& directory);
    ~SessionJournal();
    void record(const ManagedSession::Json& status,bool shutdownComplete=false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
