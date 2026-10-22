#include <catch_amalgamated.hpp>
#include "core/managed_session.hpp"
#include <filesystem>
#include <fstream>
using Session=wowee::core::ManagedSession;
using Action=Session::Action;
using Json=nlohmann::json;
namespace {
const auto start=Session::Clock::time_point{};
Session::Observation world() {
    Session::Observation o;o.inWorld=o.connected=true;return o;
}
void manual(Session::Observation& o,const std::string& id="q") {
    o.control={{"request_id",id},{"outcome","confirmed"},{"fresh",true},{"mode","manual"},{"pending_mode",""},{"failure",""},{"acknowledgement","new"}};
}
void beginLogout(Session& session,Session::Observation& o) {
    REQUIRE(session.beginStop("q","test",start));
    CHECK(session.tick(start,o)==std::vector{Action::Manual});
    manual(o);CHECK(session.tick(start+std::chrono::seconds(1),o)==std::vector{Action::Logout});
    o.loggingOut=true;
}
}
TEST_CASE("Managed duration starts at world entry and never reissues Manual", "[managed-session]") {
    Session session(10,45);auto o=world();o.inWorld=false;
    CHECK(session.tick(start,o).empty());CHECK(session.tick(start+std::chrono::seconds(99),o).empty());
    o.inWorld=true;CHECK(session.tick(start+std::chrono::seconds(100),o).empty());
    CHECK(session.tick(start+std::chrono::seconds(109),o).empty());
    CHECK(session.tick(start+std::chrono::seconds(110),o)==std::vector{Action::Manual});
    o.control={{"request_id","session-deadline"},{"outcome","requested"}};
    CHECK(session.tick(start+std::chrono::seconds(111),o).empty());
    CHECK(session.snapshot()["reason"]=="duration");
    CHECK_FALSE(session.beginStop("other","retry",start));
}
TEST_CASE("Only fresh Manual, new server logout completion and disconnect finish a managed session", "[managed-session]") {
    Session session;auto o=world();o.logout.completions=5;
    beginLogout(session,o);
    CHECK_FALSE(session.completedLogout(5));CHECK(session.completedLogout(6));
    o.logout.responses=1;o.logout.result=0;
    CHECK(session.tick(start+std::chrono::seconds(22),o).empty()); // Countdown alone does not finish.
    CHECK(session.snapshot()["logout_confirmed"]==false);
    o.logout.completions=6;
    CHECK(session.tick(start+std::chrono::seconds(23),o)==std::vector{Action::Disconnect});
    CHECK_FALSE(session.terminal());
    o.inWorld=o.connected=false;session.tick(start+std::chrono::seconds(24),o);
    CHECK(session.terminal());CHECK(session.exitCode()==0);
    CHECK(session.snapshot()["manual_confirmed"]==true);
    CHECK(session.snapshot()["logout_confirmed"]==true);
    CHECK(session.snapshot()["disconnected"]==true);
    CHECK(session.snapshot()["control_may_still_be_active"]==false);
}
TEST_CASE("Managed session fails on unconfirmed control instead of logging out", "[managed-session]") {
    for(const char* outcome:{"refused","timed_out","superseded","unavailable","disconnected"}) {
        Session session;auto o=world();session.beginStop("q","test",start);
        session.tick(start,o);o.control={{"request_id","q"},{"outcome",outcome}};
        CHECK(session.tick(start+std::chrono::seconds(1),o)==std::vector{Action::Cancel});
        CHECK(session.exitCode()==2);CHECK(session.snapshot()["logout_confirmed"]==false);
    }
}
TEST_CASE("Managed shutdown distinguishes refusal, cancellation, timeout and lost connection", "[managed-session]") {
    SECTION("refused") {
        Session s;auto o=world();beginLogout(s,o);o.logout.responses=1;o.logout.result=1;o.loggingOut=false;
        s.tick(start+std::chrono::seconds(2),o);CHECK(s.snapshot()["failure"]=="logout_refused");
    }
    SECTION("cancelled") {
        Session s;auto o=world();beginLogout(s,o);o.loggingOut=false;
        s.tick(start+std::chrono::seconds(2),o);CHECK(s.snapshot()["failure"]=="logout_cancelled_or_not_started");
    }
    SECTION("deadline") {
        Session s(0,30);auto o=world();beginLogout(s,o);
        s.tick(start+std::chrono::seconds(31),o);CHECK(s.snapshot()["failure"]=="shutdown_deadline_exceeded");CHECK(s.exitCode()==2);
    }
    SECTION("disconnect before server acknowledgement") {
        Session s;auto o=world();beginLogout(s,o);o.connected=false;
        s.tick(start+std::chrono::seconds(2),o);CHECK(s.snapshot()["logout_confirmed"]==false);CHECK(s.exitCode()==2);
    }
    SECTION("control changed on same frame as logout packet") {
        Session s;auto o=world();beginLogout(s,o);o.control["mode"]="bot";o.logout.completions=1;
        s.tick(start+std::chrono::seconds(2),o);CHECK(s.exitCode()==2);CHECK(s.snapshot()["logout_confirmed"]==false);
    }
}
TEST_CASE("Unexpected disconnection and interrupted process cannot manufacture success", "[managed-session]") {
    Session s(10);auto o=world();s.tick(start,o);o.connected=false;
    s.tick(start+std::chrono::seconds(1),o);CHECK(s.exitCode()==2);
    CHECK(s.snapshot()["failure"]=="unexpected_disconnect");
    Session unbounded;unbounded.interrupt("forced_close");CHECK(unbounded.exitCode()==2);
    CHECK(unbounded.snapshot()["control_may_still_be_active"]==true);
}
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#include <sys/stat.h>
#include <unistd.h>
TEST_CASE("Session receipts are private, atomic, exclusive and retain transition order", "[managed-session]") {
    char dir[]="/tmp/wowee-session-XXXXXX";REQUIRE(mkdtemp(dir)!=nullptr);
    const auto path=std::filesystem::canonical(dir);
    {
        wowee::core::SessionJournal journal(path.string());Session session;
        journal.record(session.snapshot());session.interrupt("test");journal.record(session.snapshot());
        journal.record(session.snapshot(),true);journal.record(session.snapshot(),true);
        Json result;std::ifstream(path/"session.json")>>result;
        CHECK(result["phase"]=="failed");CHECK(result["exit_code"]==2);
        CHECK(result["client_shutdown_complete"]==true);
        CHECK(result["sequence"]==3);
        struct stat info{};REQUIRE(stat((path/"session.json").c_str(),&info)==0);CHECK((info.st_mode&0777)==0600);
        CHECK_THROWS(wowee::core::SessionJournal(path.string()));
        Json still;std::ifstream(path/"session.json")>>still;CHECK(still==result);
        std::ifstream events(path/"session-events.jsonl");std::string line;unsigned n=0;
        while(std::getline(events,line)){++n;CHECK(Json::parse(line)["sequence"]==n);}CHECK(n==3);
    }
    std::filesystem::remove_all(path);
}

#include "core/session_signal.hpp"
#include <sys/wait.h>
TEST_CASE("Managed signals request a graceful stop then allow a forced non-successful exit", "[managed-session]") {
    for(int signals:{1,2}) {
        auto child=fork();REQUIRE(child>=0);
        if(child==0) {
            wowee::core::handleSessionStopSignal(SIGTERM);
            if(wowee::core::sessionStopSignal!=SIGTERM)_exit(1);
            if(signals==2)wowee::core::handleSessionStopSignal(SIGTERM);
            _exit(0);
        }
        int status=0;REQUIRE(waitpid(child,&status,0)==child);
        REQUIRE(WIFEXITED(status));CHECK(WEXITSTATUS(status)==(signals==1?0:2));
    }
}
#endif
