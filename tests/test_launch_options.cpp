#include <catch_amalgamated.hpp>
#include "core/launch_options.hpp"
using namespace wowee::core;
namespace {
LaunchOptions parse(std::initializer_list<const char*> values) {
    std::vector<std::string> strings;
    for (auto value : values) strings.emplace_back(value);
    std::vector<char*> argv;
    for (auto& value : strings) argv.push_back(value.data());
    return parseLaunchOptions(static_cast<int>(argv.size()), argv.data());
}
}
TEST_CASE("Launch identity is explicit and bounded", "[launch]") {
    CHECK_FALSE(parse({"wowee"}).enabled());
    CHECK(parse({"wowee", "--help"}).help);
    CHECK(parse({"wowee", "--control-socket", "/private/control.sock"}).controlSocket == "/private/control.sock");
    CHECK_THROWS(parse({"wowee", "--control-socket"}));
    const auto options = parse({"wowee", "--login-profile", "My TBC", "--character", "Jizzle", "--realm", "My Realm", "--startup-timeout", "60"});
    CHECK(options.profile == "My TBC"); CHECK(options.character == "Jizzle");
    CHECK(options.realm == "My Realm"); CHECK(options.startupTimeoutSeconds == 60);
    CHECK_THROWS(parse({"wowee", "--login-profile", "My TBC"}));
    CHECK_THROWS(parse({"wowee", "--character", "Jizzle"}));
    CHECK_THROWS(parse({"wowee", "--realm", "My Realm"}));
    CHECK_THROWS(parse({"wowee", "--login-profile", "--character", "Jizzle"}));
    CHECK_THROWS(parse({"wowee", "--password", "secret"}));
    for (auto value : {"0", "601", "-1", "12s", "999999999999999999999"})
        CHECK_THROWS(parse({"wowee", "--login-profile", "My TBC", "--character", "Jizzle", "--startup-timeout", value}));
    CHECK_THROWS(parse({"wowee", "--login-profile", "a", "--login-profile", "b", "--character", "Jizzle"}));
}
TEST_CASE("Launch selection never falls back to an unintended identity", "[launch]") {
    const std::vector<std::vector<std::string>> profiles{{"TBC", "host:3724"}, {"Other", "other:3724"}};
    CHECK(uniqueLaunchMatch(profiles, "TBC") == 0);
    CHECK(uniqueLaunchMatch(profiles, "host:3724") == 0);
    CHECK(uniqueLaunchMatch(profiles, "Other") == 1);
    CHECK_THROWS(uniqueLaunchMatch(profiles, "tbc"));
    CHECK_THROWS(uniqueLaunchMatch(profiles, "Missing"));
    CHECK_THROWS(uniqueLaunchMatch({}, "Missing"));
    CHECK_THROWS(uniqueLaunchMatch({{"TBC"}, {"TBC"}}, "TBC"));
    CHECK(uniqueLaunchMatch({{"TBC", "TBC"}}, "TBC") == 0);
}

TEST_CASE("Managed duration and shutdown arguments require explicit session wiring", "[launch]") {
    auto options=parse({"wowee","--login-profile","TBC","--character","Jizzle","--control-socket","/private/session/control.sock","--session-seconds","1200","--stop-timeout","45"});
    CHECK(options.sessionSeconds==1200);CHECK(options.stopTimeoutSeconds==45);
    CHECK_THROWS(parse({"wowee","--session-seconds","1200"}));
    CHECK_THROWS(parse({"wowee","--login-profile","TBC","--character","Jizzle","--session-seconds","1200"}));
    CHECK_THROWS(parse({"wowee","--stop-timeout","45"}));
    for(auto value:{"0","86401","1s","-1"})
        CHECK_THROWS(parse({"wowee","--login-profile","TBC","--character","Jizzle","--control-socket","/private/control.sock","--session-seconds",value}));
    CHECK_THROWS(parse({"wowee","--control-socket","/private/control.sock","--stop-timeout","121"}));
}
