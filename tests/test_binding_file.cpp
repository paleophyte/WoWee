// The saved key bindings file, one `COMMAND=KEY1,KEY2` per line.
#include <catch_amalgamated.hpp>

#include "addons/binding_file.hpp"

#include <sstream>

using wowee::addons::parseSavedBindings;

TEST_CASE("both keys of a binding are read", "[binding-file]") {
    std::istringstream in("TARGETSELF=F1,SHIFT-F1\n");
    const auto saved = parseSavedBindings(in);
    REQUIRE(saved.count("TARGETSELF") == 1);
    CHECK(saved.at("TARGETSELF")[0] == "F1");
    CHECK(saved.at("TARGETSELF")[1] == "SHIFT-F1");
}

TEST_CASE("an unbound slot reads as empty", "[binding-file]") {
    std::istringstream in("TOGGLEBACKPACK=,\nJUMP=SPACE\n");
    const auto saved = parseSavedBindings(in);
    CHECK(saved.at("TOGGLEBACKPACK")[0].empty());
    CHECK(saved.at("TOGGLEBACKPACK")[1].empty());
    CHECK(saved.at("JUMP")[0] == "SPACE");
    CHECK(saved.at("JUMP")[1].empty());
}

TEST_CASE("the equals key is a key, not the separator", "[binding-file]") {
    std::istringstream in("ACTIONBUTTON12==,\n");
    CHECK(parseSavedBindings(in).at("ACTIONBUTTON12")[0] == "=");
}

TEST_CASE("a carriage return is not part of a key", "[binding-file]") {
    std::istringstream in("MULTIACTIONBAR1BUTTON1=U,H\r\n");
    CHECK(parseSavedBindings(in).at("MULTIACTIONBAR1BUTTON1")[1] == "H");
}

TEST_CASE("lines without a command are skipped", "[binding-file]") {
    std::istringstream in("\nnot a binding\n=F2,\n");
    CHECK(parseSavedBindings(in).empty());
}
