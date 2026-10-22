#include "core/managed_session.hpp"
#include <stdexcept>
#include <cerrno>
#include <cstdio>
#include <utility>
namespace wowee::core {
ManagedSession::ManagedSession(unsigned seconds,unsigned timeout):duration_(seconds),stopTimeout_(timeout) {}
bool ManagedSession::beginStop(const std::string& id,const std::string& reason,Clock::time_point now) {
    if(stopping() || terminal())return false;
    id_=id;reason_=reason;stopStarted_=now;phase_="manual";return true;
}
bool ManagedSession::stopping() const { return phase_=="manual" || phase_=="logout" || phase_=="disconnect"; }
bool ManagedSession::terminal() const { return phase_=="finished" || phase_=="failed"; }
bool ManagedSession::completedLogout(uint64_t sequence) const { return phase_=="logout" && sequence>logoutBaseline_; }
void ManagedSession::interrupt(const std::string& reason) { if(!terminal()){failure_=reason;phase_="failed";} }
std::vector<ManagedSession::Action> ManagedSession::tick(Clock::time_point now,const Observation& o) {
    if(terminal())return {};
    if(!stopping()) {
        if(o.inWorld && !enteredOnce_){entered_=now;enteredOnce_=true;phase_="running";}
        if(duration_ && enteredOnce_ && !o.connected){interrupt("unexpected_disconnect");return {Action::Cancel};}
        if(duration_ && enteredOnce_ && now-entered_>=std::chrono::seconds(duration_))
            beginStop("session-deadline","duration",now);
        else return {};
    }
    if(phase_=="disconnect" && !o.connected){disconnected_=true;phase_="finished";return {};}
    if(now-stopStarted_>=std::chrono::seconds(stopTimeout_)) {interrupt("shutdown_deadline_exceeded");return {Action::Cancel};}
    if(!o.connected || !o.inWorld){interrupt("disconnected_before_logout_confirmation");return {Action::Cancel};}
    if(phase_=="manual") {
        if(!manualIssued_){manualIssued_=true;return {Action::Manual};}
        const auto outcome=o.control.value("outcome","");
        if(o.control.value("request_id","")!=id_){interrupt("manual_request_superseded");return {Action::Cancel};}
        if(outcome!="requested" && outcome!="confirmed"){interrupt("manual_"+outcome);return {Action::Cancel};}
        if(outcome=="confirmed" && o.control.value("fresh",false) && o.control.value("mode","")=="manual" &&
            o.control.value("pending_mode","").empty() && o.control.value("failure","").empty()) {
            manualConfirmed_=true;manualEvidence_=o.control;
            logoutBaseline_=o.logout.completions;responseBaseline_=o.logout.responses;
            phase_="logout";return {Action::Logout};
        }
    } else if(phase_=="logout") {
        // Completion is a server packet fact, never a countdown or UI callback.
        if(o.control.value("mode","")!="manual" || !o.control.value("fresh",false) ||
            o.control.value("outcome","")!="confirmed") {interrupt("control_changed_during_logout");return {Action::Cancel};}
        if(o.logout.completions>logoutBaseline_){logoutConfirmed_=true;phase_="disconnect";return {Action::Disconnect};}
        if(o.logout.responses>responseBaseline_ && o.logout.result!=0){interrupt("logout_refused");return {Action::Cancel};}
        if(!o.loggingOut){interrupt("logout_cancelled_or_not_started");return {Action::Cancel};}

    }
    return {};
}
ManagedSession::Json ManagedSession::snapshot() const {
    return {{"phase",phase_},{"request_id",id_},{"reason",reason_},{"failure",failure_},
        {"duration_seconds",duration_},{"stop_timeout_seconds",stopTimeout_},
        {"manual_confirmed",manualConfirmed_},{"logout_confirmed",logoutConfirmed_},{"disconnected",disconnected_},
        {"manual_evidence",manualEvidence_},{"exit_code",terminal()?exitCode():-1},
        {"outcome",phase_=="finished"?"confirmed":phase_=="failed"?"unconfirmed":"requested"},
        {"control_may_still_be_active",phase_!="finished"}};
}
}
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <filesystem>
#include <ctime>
namespace wowee::core {
struct SessionJournal::Impl {
    std::string directory,last;int events=-1;uint64_t sequence=0;
    ~Impl(){if(events>=0)close(events);}
};
static int exclusive(const std::string& path){
    int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)throw std::runtime_error("Session evidence path already exists or cannot be created");return fd;
}
static void writeAll(int fd,const std::string& data){
    size_t n=0;while(n<data.size()){const auto count=write(fd,data.data()+n,data.size()-n);if(count<0 && errno==EINTR)continue;if(count<=0)throw std::runtime_error("Cannot write session evidence");n+=static_cast<size_t>(count);}
    if(fsync(fd)!=0)throw std::runtime_error("Cannot flush session evidence");
}
SessionJournal::SessionJournal(const std::string& directory):impl_(std::make_unique<Impl>()) {
    struct stat info{};
    if(!std::filesystem::path(directory).is_absolute() || std::filesystem::canonical(directory)!=directory ||
        lstat(directory.c_str(),&info)!=0 || !S_ISDIR(info.st_mode) || info.st_uid!=getuid() || (info.st_mode&077)!=0)
        throw std::runtime_error("Session evidence requires an owned private directory");
    impl_->directory=directory;
    int reserved=exclusive(directory+"/session.json");close(reserved);
    impl_->events=exclusive(directory+"/session-events.jsonl");
}
SessionJournal::~SessionJournal()=default;
void SessionJournal::record(const ManagedSession::Json& state,bool shutdownComplete){
    auto stable=state;stable["client_shutdown_complete"]=shutdownComplete;
    const auto fingerprint=stable.dump();if(fingerprint==impl_->last)return;
    auto event=stable;event["pid"]=getpid();event["sequence"]=++impl_->sequence;event["unix_time"]=std::time(nullptr);
    writeAll(impl_->events,event.dump()+"\n");
    const auto temp=impl_->directory+"/session.json.tmp";
    int fd=exclusive(temp);
    try {writeAll(fd,event.dump(2)+"\n");close(fd);fd=-1;
        if(rename(temp.c_str(),(impl_->directory+"/session.json").c_str())!=0)throw std::runtime_error("Cannot publish session receipt");
        int parent=open(impl_->directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(parent<0)throw std::runtime_error("Cannot flush session directory");const int result=fsync(parent);close(parent);if(result!=0)throw std::runtime_error("Cannot flush session directory");
    } catch(...){if(fd>=0)close(fd);unlink(temp.c_str());throw;}
    impl_->last=fingerprint;
}
}
#else
namespace wowee::core {
struct SessionJournal::Impl {};
SessionJournal::SessionJournal(const std::string&){throw std::runtime_error("Session journal currently requires desktop macOS/Linux");}
SessionJournal::~SessionJournal()=default;
void SessionJournal::record(const ManagedSession::Json&,bool){}
}
#endif
