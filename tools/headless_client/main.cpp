#include "auth/auth_handler.hpp"
#include "auth/auth_packets.hpp"
#include "game/game_handler.hpp"
#include "game/game_services.hpp"
#include "game/packet_parsers.hpp"
#include "game/transport_manager.hpp"
#include "game/world_packets.hpp"
#include "network/net_platform.hpp"
#include "network/packet.hpp"
#include "network/world_socket.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_layout.hpp"
#include "rendering/animation/emote_registry.hpp"
#include "core/crash_diagnostics.hpp"
#include "core/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <thread>
#include <ctime>
#include <vector>
#include <cmath>

#ifdef _WIN32
#include <conio.h>
#endif

using json = nlohmann::json;
using namespace wowee;

namespace {

std::atomic<bool> g_running{true};

constexpr int kEscKey = 27;
constexpr auto kLogoutSilentExitTimeout = std::chrono::seconds(12);

const char* objectTypeName(game::ObjectType type) {
    switch (type) {
        case game::ObjectType::OBJECT: return "OBJECT";
        case game::ObjectType::ITEM: return "ITEM";
        case game::ObjectType::CONTAINER: return "CONTAINER";
        case game::ObjectType::UNIT: return "UNIT";
        case game::ObjectType::PLAYER: return "PLAYER";
        case game::ObjectType::GAMEOBJECT: return "GAMEOBJECT";
        case game::ObjectType::DYNAMICOBJECT: return "DYNAMICOBJECT";
        case game::ObjectType::CORPSE: return "CORPSE";
    }
    return "UNKNOWN";
}

bool consumeEscKeypress() {
#ifdef _WIN32
    while (_kbhit()) {
        const int ch = _getch();
        if (ch == kEscKey) return true;
        if (ch == 0 || ch == 0xE0) {
            if (_kbhit()) (void)_getch();
        }
    }
#endif
    return false;
}

void setDefaultEnv(const char* key, const char* value) {
    if (std::getenv(key)) return;
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 0);
#endif
}

struct Settings {
    std::string authHost = "127.0.0.1";
    uint16_t authPort = 3724;
    std::string account;
    std::string password;

    auth::ClientInfo clientInfo;
    std::string expansion = "wotlk";

    std::string realmName;
    size_t realmIndex = 0;
    uint32_t realmId = 0;
    std::string characterName;

    bool autoJoinDefaultChannels = false;
    bool apiEnabled = true;
    std::string apiBind = "127.0.0.1";
    uint16_t apiPort = 8787;
    size_t apiMaxMessages = 200;

    std::vector<std::string> onEnterWorldCommands;
    float onEnterCommandDelaySeconds = 0.25f;

    std::optional<game::CharCreateData> createCharacter;
    bool exitAfterCreateCharacter = false;
};

struct PendingChat {
    game::ChatType type = game::ChatType::SAY;
    std::string message;
    std::string target;
};

struct Waypoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float arrivalRadius = 0.0f;
};

struct MovementTask {
    bool active = false;
    bool started = false;
    uint32_t mapId = 0;
    std::vector<Waypoint> waypoints;
    size_t currentWaypoint = 0;
    float arrivalRadius = 3.0f;
    std::string status = "idle";
    std::string error;
    float lastSetX = 0.0f;
    float lastSetY = 0.0f;
    float lastSetZ = 0.0f;
    bool lastSetValid = false;

    float targetX() const {
        return currentWaypoint < waypoints.size() ? waypoints[currentWaypoint].x : 0.0f;
    }
    float targetY() const {
        return currentWaypoint < waypoints.size() ? waypoints[currentWaypoint].y : 0.0f;
    }
    float targetZ() const {
        return currentWaypoint < waypoints.size() ? waypoints[currentWaypoint].z : 0.0f;
    }
    float targetRadius() const {
        if (currentWaypoint < waypoints.size() && waypoints[currentWaypoint].arrivalRadius > 0.0f)
            return waypoints[currentWaypoint].arrivalRadius;
        return arrivalRadius;
    }
};

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) { return !isSpace(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) { return !isSpace(c); }).base(), value.end());
    return value;
}

std::string detectExpansion(uint32_t build) {
    if (build <= 5875) return "classic";
    if (build <= 8606) return "tbc";
    return "wotlk";
}

std::optional<game::Race> raceFromString(std::string value);
std::optional<game::Class> classFromString(std::string value);
std::optional<game::Gender> genderFromString(std::string value);

template <typename T>
T jsonValue(const json& obj, const char* key, T fallback) {
    if (!obj.is_object() || !obj.contains(key) || obj.at(key).is_null()) return fallback;
    return obj.at(key).get<T>();
}

Settings loadSettings(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open settings file: " + path);
    }

    json doc = json::parse(in);
    Settings s;

    const auto authObj = doc.value("auth", json::object());
    s.authHost = jsonValue<std::string>(authObj, "host", s.authHost);
    s.authPort = static_cast<uint16_t>(jsonValue<int>(authObj, "port", s.authPort));
    s.account = jsonValue<std::string>(authObj, "account", s.account);
    s.password = jsonValue<std::string>(authObj, "password", s.password);

    const auto clientObj = doc.value("client", json::object());
    s.clientInfo.majorVersion = static_cast<uint8_t>(jsonValue<int>(clientObj, "major", 3));
    s.clientInfo.minorVersion = static_cast<uint8_t>(jsonValue<int>(clientObj, "minor", 3));
    s.clientInfo.patchVersion = static_cast<uint8_t>(jsonValue<int>(clientObj, "patch", 5));
    s.clientInfo.build = static_cast<uint16_t>(jsonValue<int>(clientObj, "build", 12340));
    s.clientInfo.protocolVersion = static_cast<uint8_t>(jsonValue<int>(clientObj, "protocol", 8));
    s.clientInfo.locale = jsonValue<std::string>(clientObj, "locale", "enUS");
    s.clientInfo.platform = jsonValue<std::string>(clientObj, "platform", "x86");
    s.clientInfo.os = jsonValue<std::string>(clientObj, "os", "Win");
    s.expansion = jsonValue<std::string>(clientObj, "expansion", detectExpansion(s.clientInfo.build));

    const auto realmObj = doc.value("realm", json::object());
    s.realmName = jsonValue<std::string>(realmObj, "name", "");
    s.realmIndex = static_cast<size_t>(std::max(0, jsonValue<int>(realmObj, "index", 0)));
    s.realmId = static_cast<uint32_t>(std::max(0, jsonValue<int>(realmObj, "id", 0)));

    const auto characterObj = doc.value("character", json::object());
    s.characterName = jsonValue<std::string>(characterObj, "name", "");

    const auto chatObj = doc.value("chat", json::object());
    s.autoJoinDefaultChannels = jsonValue<bool>(chatObj, "autoJoinDefaultChannels", false);

    const auto apiObj = doc.value("api", json::object());
    s.apiEnabled = jsonValue<bool>(apiObj, "enabled", true);
    s.apiBind = jsonValue<std::string>(apiObj, "bind", "127.0.0.1");
    s.apiPort = static_cast<uint16_t>(jsonValue<int>(apiObj, "port", 8787));
    s.apiMaxMessages = static_cast<size_t>(std::max(1, jsonValue<int>(apiObj, "maxMessages", 200)));

    if (s.account.empty()) throw std::runtime_error("settings auth.account is required");
    if (s.password.empty()) throw std::runtime_error("settings auth.password is required");

    const auto botsObj = doc.value("bots", json::object());
    if (botsObj.is_object() && botsObj.value("enabled", false)) {
        const auto names = botsObj.value("names", json::array());
        if (!names.is_array()) {
            throw std::runtime_error("settings bots.names must be an array");
        }
        for (const auto& nameValue : names) {
            if (!nameValue.is_string()) continue;
            const std::string name = trim(nameValue.get<std::string>());
            if (!name.empty()) {
                s.onEnterWorldCommands.push_back(".bot add " + name);
            }
        }
    }

    const auto automationObj = doc.value("automation", json::object());
    if (automationObj.is_object()) {
        s.onEnterCommandDelaySeconds = std::max(0.0f,
            jsonValue<float>(automationObj, "commandDelaySeconds", s.onEnterCommandDelaySeconds));
        const auto commands = automationObj.value("onEnterWorldCommands", json::array());
        if (!commands.is_array()) {
            throw std::runtime_error("settings automation.onEnterWorldCommands must be an array");
        }
        for (const auto& commandValue : commands) {
            if (!commandValue.is_string()) continue;
            const std::string command = trim(commandValue.get<std::string>());
            if (!command.empty()) {
                s.onEnterWorldCommands.push_back(command);
            }
        }
    }

    const auto provisionObj = doc.value("provision", json::object());
    if (provisionObj.is_object()) {
        const auto createObj = provisionObj.value("createCharacter", json::object());
        if (createObj.is_object() && jsonValue<bool>(createObj, "enabled", false)) {
            game::CharCreateData data;
            data.name = trim(jsonValue<std::string>(createObj, "name", ""));
            const auto race = raceFromString(jsonValue<std::string>(createObj, "race", ""));
            const auto cls = classFromString(jsonValue<std::string>(createObj, "class", ""));
            const auto gender = genderFromString(jsonValue<std::string>(createObj, "gender", "male"));
            if (data.name.empty()) throw std::runtime_error("provision.createCharacter.name is required");
            if (!race) throw std::runtime_error("provision.createCharacter.race is invalid");
            if (!cls) throw std::runtime_error("provision.createCharacter.class is invalid");
            if (!gender) throw std::runtime_error("provision.createCharacter.gender is invalid");
            data.race = *race;
            data.characterClass = *cls;
            data.gender = *gender;
            data.skin = static_cast<uint8_t>(std::clamp(jsonValue<int>(createObj, "skin", 0), 0, 255));
            data.face = static_cast<uint8_t>(std::clamp(jsonValue<int>(createObj, "face", 0), 0, 255));
            data.hairStyle = static_cast<uint8_t>(std::clamp(jsonValue<int>(createObj, "hairStyle", 0), 0, 255));
            data.hairColor = static_cast<uint8_t>(std::clamp(jsonValue<int>(createObj, "hairColor", 0), 0, 255));
            data.facialHair = static_cast<uint8_t>(std::clamp(jsonValue<int>(createObj, "facialHair", 0), 0, 255));
            s.createCharacter = data;
            s.exitAfterCreateCharacter = jsonValue<bool>(createObj, "exitAfterCreate", true);
        }
    }

    return s;
}

bool splitHostPort(const std::string& address, std::string& host, uint16_t& port) {
    const size_t pos = address.rfind(':');
    if (pos == std::string::npos) return false;
    host = address.substr(0, pos);
    port = static_cast<uint16_t>(std::stoi(address.substr(pos + 1)));
    return !host.empty() && port != 0;
}

std::optional<game::ChatType> chatTypeFromString(const std::string& value) {
    const std::string t = lowerAscii(value);
    if (t == "say") return game::ChatType::SAY;
    if (t == "yell") return game::ChatType::YELL;
    if (t == "whisper" || t == "w") return game::ChatType::WHISPER;
    if (t == "channel") return game::ChatType::CHANNEL;
    if (t == "party") return game::ChatType::PARTY;
    if (t == "guild") return game::ChatType::GUILD;
    if (t == "raid") return game::ChatType::RAID;
    if (t == "officer") return game::ChatType::OFFICER;
    return std::nullopt;
}

std::string chatTypeName(game::ChatType type) {
    return game::getChatTypeString(type);
}

std::optional<game::Race> raceFromString(std::string value) {
    value = lowerAscii(trim(std::move(value)));
    if (value == "human" || value == "1") return game::Race::HUMAN;
    if (value == "orc" || value == "2") return game::Race::ORC;
    if (value == "dwarf" || value == "3") return game::Race::DWARF;
    if (value == "night_elf" || value == "nightelf" || value == "night elf" || value == "4") return game::Race::NIGHT_ELF;
    if (value == "undead" || value == "5") return game::Race::UNDEAD;
    if (value == "tauren" || value == "6") return game::Race::TAUREN;
    if (value == "gnome" || value == "7") return game::Race::GNOME;
    if (value == "troll" || value == "8") return game::Race::TROLL;
    if (value == "blood_elf" || value == "bloodelf" || value == "blood elf" || value == "10") return game::Race::BLOOD_ELF;
    if (value == "draenei" || value == "11") return game::Race::DRAENEI;
    return std::nullopt;
}

std::optional<game::Class> classFromString(std::string value) {
    value = lowerAscii(trim(std::move(value)));
    if (value == "warrior" || value == "1") return game::Class::WARRIOR;
    if (value == "paladin" || value == "2") return game::Class::PALADIN;
    if (value == "hunter" || value == "3") return game::Class::HUNTER;
    if (value == "rogue" || value == "4") return game::Class::ROGUE;
    if (value == "priest" || value == "5") return game::Class::PRIEST;
    if (value == "shaman" || value == "7") return game::Class::SHAMAN;
    if (value == "mage" || value == "8") return game::Class::MAGE;
    if (value == "warlock" || value == "9") return game::Class::WARLOCK;
    if (value == "druid" || value == "11") return game::Class::DRUID;
    return std::nullopt;
}

std::optional<game::Gender> genderFromString(std::string value) {
    value = lowerAscii(trim(std::move(value)));
    if (value == "male" || value == "m" || value == "0") return game::Gender::MALE;
    if (value == "female" || value == "f" || value == "1") return game::Gender::FEMALE;
    return std::nullopt;
}

std::string movementTaskStateName(const MovementTask& task) {
    if (task.active) return "moving";
    return task.status.empty() ? "idle" : task.status;
}

std::string timeToIso(std::chrono::system_clock::time_point tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

json chatToJson(const game::MessageChatData& msg, size_t id) {
    return {
        {"id", id},
        {"type", chatTypeName(msg.type)},
        {"from", msg.senderName},
        {"fromGuid", msg.senderGuid},
        {"to", msg.receiverName},
        {"toGuid", msg.receiverGuid},
        {"channel", msg.channelName},
        {"message", msg.message},
        {"timestamp", timeToIso(msg.timestamp)}
    };
}

class HeadlessSession {
public:
    explicit HeadlessSession(Settings settings)
        : settings_(std::move(settings)), game_(services_) {
        configureProtocol();
    }

    bool start() {
        auth_.setClientInfo(settings_.clientInfo);
        auth_.setOnSuccess([this](const std::vector<uint8_t>&) {
            std::cout << "Auth succeeded; requesting realms\n";
            auth_.requestRealmList();
        });
        auth_.setOnFailure([this](const std::string& reason) {
            fail("Auth failed: " + reason);
        });
        auth_.setOnRealmList([this](const std::vector<auth::Realm>& realms) {
            onRealmList(realms);
        });

        game_.setOnFailure([this](const std::string& reason) {
            fail("World failed: " + reason);
        });
        game_.setCharCreateCallback([this](bool success, const std::string& message) {
            std::cout << "Character create result: " << message << "\n";
            if (success) {
                charCreateDone_ = true;
                if (settings_.exitAfterCreateCharacter) {
                    g_running = false;
                }
            } else {
                fail("Character create failed: " + message);
            }
        });

        // Headless has no renderer, so it never learns whether a transport's model
        // resolved to a WMO or M2 asset (see EntitySpawner::spawnOnlineGameObject).
        // Known M2 transports (Deeprun Tram cars, Thunder Bluff lifts) are hardcoded
        // here since that's the whole population this harness needs to track; anything
        // else defaults to isM2=false (WMO), matching ships/zeppelins.
        auto registerOrUpdateTransport = [this](uint64_t guid, uint32_t entry, uint32_t displayId,
                                                float x, float y, float z, float orientation) {
            auto* tm = game_.getTransportManager();
            if (!tm) return;
            const bool isM2 =
                displayId == 3831u ||
                (entry >= 176080u && entry <= 176085u) ||
                (entry >= 20649u && entry <= 20657u);
            const glm::vec3 canonicalPos(x, y, z);
            if (!tm->getTransport(guid)) {
                tm->resolveAndRegisterSpawn(guid, entry, displayId, canonicalPos,
                                            /*wmoInstanceId=*/0, isM2,
                                            game_.hasServerTransportUpdate(guid));
            }
            tm->updateServerTransport(guid, canonicalPos, orientation);
        };
        game_.setGameObjectSpawnCallback([this, registerOrUpdateTransport](
                uint64_t guid, uint32_t entry, uint32_t displayId,
                float x, float y, float z, float orientation, float /*scale*/) {
            if (!game_.isTransportGuid(guid)) return;
            registerOrUpdateTransport(guid, entry, displayId, x, y, z, orientation);
        });
        game_.setTransportMoveCallback([this, registerOrUpdateTransport](
                uint64_t guid, float x, float y, float z, float orientation) {
            uint32_t entry = 0, displayId = 0;
            auto entity = game_.getEntityManager().getEntity(guid);
            if (entity && entity->getType() == game::ObjectType::GAMEOBJECT) {
                auto go = std::static_pointer_cast<game::GameObject>(entity);
                entry = go->getEntry();
                displayId = go->getDisplayId();
            }
            registerOrUpdateTransport(guid, entry, displayId, x, y, z, orientation);
        });

        if (!auth_.connect(settings_.authHost, settings_.authPort)) {
            fail("Could not connect to auth server");
            return false;
        }
        auth_.authenticate(settings_.account, settings_.password);
        return true;
    }

    void update(float deltaSeconds) {
        wowee::core::setCrashNote("headless update: auth");
        auth_.update(deltaSeconds);
        wowee::core::setCrashNote("headless update: game");
        const auto beforeWorldState = game_.getState();
        game_.update(deltaSeconds);
        wowee::core::setCrashNote("headless update: state transition");
        const auto afterWorldState = game_.getState();
        if (afterWorldState != beforeWorldState) {
            std::cout << "World state changed: "
                      << static_cast<int>(beforeWorldState) << " -> "
                      << static_cast<int>(afterWorldState) << "\n";
        }
        inWorldForApi_ = (game_.getState() == game::WorldState::IN_WORLD);

        if (!selectedCharacter_ && game_.getState() == game::WorldState::CHAR_LIST_RECEIVED) {
            wowee::core::setCrashNote("headless update: character selection");
            if (settings_.createCharacter) {
                provisionConfiguredCharacter();
                return;
            }
            selectConfiguredCharacter();
        }

        if (!enteredWorld_ && game_.getState() == game::WorldState::IN_WORLD) {
            wowee::core::setCrashNote("headless update: enter world");
            enteredWorld_ = true;
            inWorldForApi_ = true;
            std::cout << "Entered world";
            if (!settings_.characterName.empty()) std::cout << " as " << settings_.characterName;
            std::cout << "\n";
            if (settings_.autoJoinDefaultChannels) {
                game_.autoJoinDefaultChannels();
            }
            queueOnEnterWorldCommands();
        }

        if (!logoutRequested_) {
            wowee::core::setCrashNote("headless update: scheduled commands");
            drainScheduledCommands(deltaSeconds);
            wowee::core::setCrashNote("headless update: pending chat");
            drainPendingChat();
            wowee::core::setCrashNote("headless update: movement");
            updateMovementTask(deltaSeconds);
            wowee::core::setCrashNote("headless update: transport boarding");
            if (inWorldForApi_) {
                // While riding with no active goto, movementInfo doesn't move on its own here
                // (unlike the GUI client's per-frame render loop, which relocks the player to
                // the deck each frame) - keep it following the transport so /world/self and
                // the disembark distance check both see the real, moving position instead of
                // wherever the player happened to be standing at the moment they boarded. If a
                // goto is active (e.g. walking off at the destination), let
                // updateMovementTask() drive movementInfo instead so that walk can actually
                // carry the player off the deck rather than being overwritten back onto it.
                glm::vec3 playerCanonical;
                if (game_.isOnTransport() && !movementTask_.active) {
                    playerCanonical = game_.getComposedWorldPosition();
                    game_.setPosition(playerCanonical.x, playerCanonical.y, playerCanonical.z);
                } else {
                    const auto& move = game_.getMovementInfo();
                    playerCanonical = glm::vec3(move.x, move.y, move.z);
                }
                game_.updateM2TransportBoarding(playerCanonical);
            }
        }
        wowee::core::setCrashNote("headless update: chat snapshot");
        syncChatSnapshot();
        wowee::core::setCrashNote("headless update: logout");
        updateLogoutExit();
        wowee::core::setCrashNote("headless update: idle");
    }

    bool isFailed() const { return failed_.load(); }
    bool isInWorld() const { return inWorldForApi_.load(); }
    bool isLogoutRequested() const { return logoutRequested_.load(); }
    json statusJson() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        bool inCombat = false, isDead = false, isPlayerDead = false;
        uint32_t hp = 0, maxHp = 0;
        if (isInWorld()) {
            auto playerEntity = game_.getEntityManager().getEntity(game_.getPlayerGuid());
            if (playerEntity) {
                if (auto* unit = dynamic_cast<game::Unit*>(playerEntity.get())) {
                    hp = unit->getHealth();
                    maxHp = unit->getMaxHealth();
                }
            }
            inCombat = game_.isInCombat();
            isDead = game_.isDead();
            isPlayerDead = game_.isPlayerDead();
        }
        return {
            {"status", statusUnlocked()},
            {"inWorld", isInWorld()},
            {"movement", movementTaskToJsonLocked()},
            {"combat", {{"inCombat", inCombat}}},
            {"health", {{"current", hp}, {"max", maxHp}, {"isDead", isDead}, {"isPlayerDead", isPlayerDead}}}
        };
    }
    std::string status() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return statusUnlocked();
    }

    json worldSelf() const {
        const auto& move = game_.getMovementInfo();
        json character = json::object();
        if (const auto* ch = game_.getActiveCharacter()) {
            character = {
                {"name", ch->name},
                {"guid", ch->guid},
                {"race", static_cast<uint8_t>(ch->race)},
                {"class", static_cast<uint8_t>(ch->characterClass)},
                {"level", ch->level}
            };
        }

uint32_t hp = 0, maxHp = 0;
        {
            auto playerEntity = game_.getEntityManager().getEntity(game_.getPlayerGuid());
            if (playerEntity) {
                if (auto* unit = dynamic_cast<game::Unit*>(playerEntity.get())) {
                    hp = unit->getHealth();
                    maxHp = unit->getMaxHealth();
                }
            }
        }

        std::lock_guard<std::mutex> lock(stateMutex_);
        return {
            {"status", statusUnlocked()},
            {"inWorld", isInWorld()},
            {"character", character},
            {"playerGuid", game_.getPlayerGuid()},
            {"mapId", game_.getCurrentMapId()},
            {"position", {{"x", move.x}, {"y", move.y}, {"z", move.z}}},
            {"orientation", move.orientation},
            {"movementFlags", move.flags},
            {"movementFlags2", move.flags2},
            {"runSpeed", game_.getServerRunSpeed()},
            {"transport", {
                {"onTransport", game_.isOnTransport()},
                {"guid", game_.getPlayerTransportGuid()},
                {"offset", {
                    {"x", game_.getPlayerTransportOffset().x},
                    {"y", game_.getPlayerTransportOffset().y},
                    {"z", game_.getPlayerTransportOffset().z}
                }}
            }},
            {"movement", movementTaskToJsonLocked()},
            {"combat", {{"inCombat", game_.isInCombat()}}},
            {"health", {{"current", hp}, {"max", maxHp}, {"isDead", game_.isDead()}, {"isPlayerDead", game_.isPlayerDead()}}}
        };
    }

    json worldEntities(float radius = 120.0f, bool onlyTransports = false) const {
        const auto& move = game_.getMovementInfo();
        json entities = json::array();
        size_t total = 0;
        size_t included = 0;

        // entity->getX/Y/Z() below is the raw GameObject spawn/presence-echo field -
        // for transports it never moves, since CMaNGOS only sends a static echo and all
        // actual motion is simulated client-side by TransportManager. Snapshot the live
        // simulated state once (thread-safe, mutex-guarded copy) so transport entities
        // can additionally report where they actually are, without disturbing the
        // existing GameObject-sourced fields other callers may depend on.
        std::unordered_map<uint64_t, game::ActiveTransport> liveTransports;
        if (const auto* tm = game_.getTransportManager()) {
            for (auto& t : tm->snapshotTransports()) {
                const uint64_t guid = t.guid;
                liveTransports.emplace(guid, std::move(t));
            }
        }

        for (const auto& entity : game_.getEntityManager().snapshotEntities()) {
            if (!entity) continue;
            const uint64_t guid = entity->getGuid();
            ++total;

            const bool isTransport = game_.isTransportGuid(guid);
            if (onlyTransports && !isTransport) continue;

            // Transports report a static presence-echo position (see the class
            // comment above) - the live simulated position, when available, is
            // what callers actually need for proximity/boarding checks. Using
            // the stale static field here made every distance/radius decision
            // for a moving tram car wrong regardless of where it actually was.
            const auto liveIt = isTransport ? liveTransports.find(guid) : liveTransports.end();
            const bool hasLive = liveIt != liveTransports.end();
            const float refX = hasLive ? liveIt->second.position.x : entity->getX();
            const float refY = hasLive ? liveIt->second.position.y : entity->getY();
            const float refZ = hasLive ? liveIt->second.position.z : entity->getZ();
            const float dx = refX - move.x;
            const float dy = refY - move.y;
            const float dz = refZ - move.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (radius >= 0.0f && distance > radius) continue;

            json item = {
                {"guid", guid},
                {"type", objectTypeName(entity->getType())},
                {"isTransport", isTransport},
                {"hasServerTransportUpdate", isTransport ? game_.hasServerTransportUpdate(guid) : false},
                {"position", {{"x", entity->getX()}, {"y", entity->getY()}, {"z", entity->getZ()}}},
                {"orientation", entity->getOrientation()},
                {"distance", distance}
            };

            if (isTransport && hasLive) {
                const auto& t = liveIt->second;
                item["livePosition"] = {{"x", t.position.x}, {"y", t.position.y}, {"z", t.position.z}};
                item["localPathTimeMs"] = t.localClockMs;
                item["playerOnBoard"] = t.playerOnBoard;
            }

            if (entity->isUnit()) {
                const auto* unit = static_cast<const game::Unit*>(entity.get());
                item["name"] = unit->getName();
                item["entry"] = unit->getEntry();
                item["displayId"] = unit->getDisplayId();
                item["level"] = unit->getLevel();
            } else if (entity->getType() == game::ObjectType::GAMEOBJECT) {
                const auto* go = static_cast<const game::GameObject*>(entity.get());
                item["name"] = go->getName();
                item["entry"] = go->getEntry();
                item["displayId"] = go->getDisplayId();
                if (const auto* info = game_.getCachedGameObjectInfo(go->getEntry())) {
                    item["template"] = {
                        {"entry", info->entry},
                        {"name", info->name},
                        {"type", info->type},
                        {"displayId", info->displayId}
                    };
                }
            }

            entities.push_back(std::move(item));
            ++included;
        }

        return {
            {"status", status()},
            {"inWorld", isInWorld()},
            {"mapId", game_.getCurrentMapId()},
            {"position", {{"x", move.x}, {"y", move.y}, {"z", move.z}}},
            {"radius", radius},
            {"onlyTransports", onlyTransports},
            {"entityCount", total},
            {"included", included},
            {"entities", entities}
        };
    }

    json partyJson() const {
        const auto& party = game_.getPartyData();
        json members = json::array();
        for (const auto& member : party.members) {
            members.push_back({
                {"name", member.name},
                {"guid", member.guid},
                {"isOnline", member.isOnline != 0},
                {"subGroup", member.subGroup},
                {"flags", member.flags},
                {"roles", member.roles},
                {"level", member.level},
                {"zoneId", member.zoneId},
                {"hasPartyStats", member.hasPartyStats}
            });
        }
        return {
            {"status", status()},
            {"inGroup", !party.isEmpty()},
            {"groupType", party.groupType},
            {"memberCount", party.memberCount},
            {"leaderGuid", party.leaderGuid},
            {"members", members}
        };
    }

    json queueCommand(const std::string& command) {
        const std::string trimmed = trim(command);
        if (trimmed.empty()) {
            return {{"ok", false}, {"error", "command is required"}};
        }
        PendingChat msg;
        msg.type = game::ChatType::SAY;
        msg.message = trimmed;
        enqueueChat(std::move(msg));
        return {{"ok", true}};
    }

    json startGoto(uint32_t mapId, float x, float y, float z, float arrivalRadius) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!isInWorld()) {
            return {{"ok", false}, {"error", "not in world"}};
        }
        if (mapId != 0 && mapId != game_.getCurrentMapId()) {
            return {{"ok", false}, {"error", "map mismatch"}, {"currentMapId", game_.getCurrentMapId()}};
        }
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            return {{"ok", false}, {"error", "coordinates must be finite"}};
        }
        if (!std::isfinite(arrivalRadius) || arrivalRadius <= 0.0f) {
            arrivalRadius = 3.0f;
        }
        if (game_.isPlayerRooted()) {
            return {{"ok", false}, {"error", "movement_locked: player is rooted"}};
        }
        if (!game_.isServerMovementAllowed()) {
            return {{"ok", false}, {"error", "movement_locked: server movement not allowed"}};
        }
        if (game_.isOnTaxiFlight()) {
            return {{"ok", false}, {"error", "movement_locked: on taxi flight"}};
        }

        Waypoint wp;
        wp.x = x;
        wp.y = y;
        wp.z = z;
        wp.arrivalRadius = std::max(0.5f, arrivalRadius);

        movementTask_ = MovementTask{};
        movementTask_.active = true;
        movementTask_.mapId = mapId == 0 ? game_.getCurrentMapId() : mapId;
        movementTask_.arrivalRadius = std::max(0.5f, arrivalRadius);
        movementTask_.waypoints.push_back(wp);
        movementTask_.status = "moving";
        return {{"ok", true}, {"movement", movementTaskToJsonLocked()}};
    }

    json startGotoWaypoints(uint32_t mapId, const std::vector<Waypoint>& waypoints, float defaultRadius) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!isInWorld()) {
            return {{"ok", false}, {"error", "not in world"}};
        }
        if (waypoints.empty()) {
            return {{"ok", false}, {"error", "waypoints list is empty"}};
        }
        if (!std::isfinite(defaultRadius) || defaultRadius <= 0.0f) {
            defaultRadius = 3.0f;
        }
        if (game_.isPlayerRooted()) {
            return {{"ok", false}, {"error", "movement_locked: player is rooted"}};
        }
        if (!game_.isServerMovementAllowed()) {
            return {{"ok", false}, {"error", "movement_locked: server movement not allowed"}};
        }
        if (game_.isOnTaxiFlight()) {
            return {{"ok", false}, {"error", "movement_locked: on taxi flight"}};
        }

        movementTask_ = MovementTask{};
        movementTask_.active = true;
        movementTask_.mapId = mapId == 0 ? game_.getCurrentMapId() : mapId;
        movementTask_.arrivalRadius = std::max(0.5f, defaultRadius);
        movementTask_.waypoints = waypoints;
        movementTask_.status = "moving";
        return {{"ok", true}, {"movement", movementTaskToJsonLocked()}};
    }

    json stopMovementTask(const std::string& reason = "stopped") {
        std::lock_guard<std::mutex> lock(stateMutex_);
        stopMovementTaskLocked(reason, "");
        return {{"ok", true}, {"movement", movementTaskToJsonLocked()}};
    }

    json fireAreaTrigger(uint32_t triggerId) {
        if (!isInWorld()) {
            return {{"ok", false}, {"error", "not in world"}};
        }
        if (triggerId == 0) {
            return {{"ok", false}, {"error", "trigger id is required"}};
        }
        auto* socket = game_.getSocket();
        if (!socket || !socket->isConnected()) {
            return {{"ok", false}, {"error", "world socket is not connected"}};
        }

        network::Packet pkt(game::wireOpcode(game::Opcode::CMSG_AREATRIGGER));
        pkt.writeUInt32(triggerId);
        socket->send(pkt);
        std::cout << "Sent CMSG_AREATRIGGER id=" << triggerId << "\n";
        return {
            {"ok", true},
            {"triggerId", triggerId},
            {"mapId", game_.getCurrentMapId()},
            {"position", {{"x", game_.getMovementInfo().x}, {"y", game_.getMovementInfo().y}, {"z", game_.getMovementInfo().z}}}
        };
    }

    void requestGracefulLogout() {
        if (logoutRequested_.exchange(true)) return;
        if (game_.getState() == game::WorldState::IN_WORLD) {
            std::cout << "Esc pressed; requesting logout. Press Ctrl-C to quit immediately.\n";
            game_.requestLogout();
            logoutRequestedAt_ = std::chrono::steady_clock::now();
        } else {
            std::cout << "Esc pressed before entering world; exiting.\n";
            g_running = false;
        }
    }

    void enqueueChat(PendingChat msg) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingChat_.push_back(std::move(msg));
    }

    json readChat(size_t afterId, size_t limit) {
        std::lock_guard<std::mutex> lock(chatMutex_);
        json arr = json::array();
        for (size_t i = 0; i < chatMirror_.size(); ++i) {
            const size_t id = chatBaseId_ + i;
            if (id <= afterId) continue;
            arr.push_back(chatToJson(resolveChatSender(chatMirror_[i]), id));
            if (arr.size() >= limit) break;
        }
        return {
            {"status", status()},
            {"inWorld", isInWorld()},
            {"nextId", chatBaseId_ + chatMirror_.size()},
            {"messages", arr}
        };
    }

private:
    std::string statusUnlocked() const {
        if (failed_) return "failed: " + failureReason_;
        if (logoutRequested_) return "logging_out";
        if (isInWorld()) return "in_world";
        return "connecting";
    }

    json movementTaskToJsonLocked() const {
        const size_t total = movementTask_.waypoints.size();
        const size_t index = movementTask_.currentWaypoint;
        return {
            {"state", movementTaskStateName(movementTask_)},
            {"active", movementTask_.active},
            {"mapId", movementTask_.mapId},
            {"x", movementTask_.targetX()},
            {"y", movementTask_.targetY()},
            {"z", movementTask_.targetZ()},
            {"arrivalRadius", movementTask_.targetRadius()},
            {"error", movementTask_.error},
            {"waypointIndex", total > 0 ? static_cast<uint64_t>(index + 1) : 0ull},
            {"waypointCount", static_cast<uint64_t>(total)}
        };
    }

    game::MessageChatData resolveChatSender(const game::MessageChatData& msg) const {
        if (!msg.senderName.empty() || msg.senderGuid == 0) return msg;

        game::MessageChatData resolved = msg;
        const auto& cache = game_.getPlayerNameCache();
        auto it = cache.find(msg.senderGuid);
        if (it != cache.end() && !it->second.empty()) {
            resolved.senderName = it->second;
        }
        return resolved;
    }

    void configureProtocol() {
        const std::filesystem::path dataPath = std::getenv("WOW_DATA_PATH") ? std::getenv("WOW_DATA_PATH") : "Data";
        const auto expansionPath = dataPath / "expansions" / settings_.expansion;

        if (!game_.getOpcodeTable().loadFromJson((expansionPath / "opcodes.json").string())) {
            std::cerr << "Warning: failed to load opcode table from " << (expansionPath / "opcodes.json").string() << "\n";
        }
        game::setActiveOpcodeTable(&game_.getOpcodeTable());

        if (!game_.getUpdateFieldTable().loadFromJson((expansionPath / "update_fields.json").string())) {
            std::cerr << "Warning: failed to load update field table from " << (expansionPath / "update_fields.json").string() << "\n";
        }
        game::setActiveUpdateFieldTable(&game_.getUpdateFieldTable());
        game_.setPacketParsers(game::createPacketParsers(settings_.expansion));

        if (dbcLayout_.loadFromJson((expansionPath / "dbc_layouts.json").string())) {
            pipeline::setActiveDBCLayout(&dbcLayout_);
        }

        if (assetManager_.initialize(dataPath.string()) || assetManager_.initializeDbcOnly(dataPath.string())) {
            assetManager_.setExpansionDataPath(expansionPath.string());
            // Without this, GameHandler::services().assetManager stays null for the
            // whole process, and anything gated on it (e.g. MovementHandler's
            // AreaTrigger.dbc load) silently never runs - headless has its own
            // assetManager_ member but nothing previously wired it into the shared
            // GameServices struct GameHandler was constructed with.
            services_.assetManager = &assetManager_;
            rendering::EmoteRegistry::instance().loadFromDbc(&assetManager_);
            // Without this, TransportManager::hasPathForEntry() never finds a real
            // moving path for any transport (Deeprun Tram, boats, elevators) - they
            // register but sit stationary at their spawn point forever, since nothing
            // else in this harness ever loads the animation curves.
            if (auto* tm = game_.getTransportManager()) {
                tm->loadTransportAnimationDBC(&assetManager_);
                tm->loadTaxiPathNodeDBC(&assetManager_);
            }
        } else {
            std::cerr << "Warning: asset manager not initialized; headless emotes will use fallback text\n";
        }
    }

    void onRealmList(const std::vector<auth::Realm>& realms) {
        if (realms.empty()) {
            fail("Realm list is empty");
            return;
        }

        size_t index = settings_.realmIndex;
        if (!settings_.realmName.empty()) {
            const std::string want = lowerAscii(settings_.realmName);
            auto it = std::find_if(realms.begin(), realms.end(), [&](const auth::Realm& realm) {
                return lowerAscii(realm.name) == want;
            });
            if (it == realms.end()) {
                fail("Configured realm not found: " + settings_.realmName);
                return;
            }
            index = static_cast<size_t>(std::distance(realms.begin(), it));
        }
        if (index >= realms.size()) {
            fail("Configured realm index is out of range");
            return;
        }

        const auto& realm = realms[index];
        std::string worldHost;
        uint16_t worldPort = 0;
        if (!splitHostPort(realm.address, worldHost, worldPort)) {
            fail("Realm address is not host:port: " + realm.address);
            return;
        }

        const uint32_t realmId = settings_.realmId != 0 ? settings_.realmId : realm.id;
        std::cout << "Connecting to realm " << realm.name << " at " << worldHost << ":" << worldPort << "\n";
        game_.connect(worldHost, worldPort, auth_.getSessionKey(), settings_.account,
            settings_.clientInfo.build, realmId);
    }

    void selectConfiguredCharacter() {
        const auto& characters = game_.getCharacters();
        if (characters.empty()) {
            fail("No characters found on selected realm");
            return;
        }

        const game::Character* chosen = nullptr;
        if (!settings_.characterName.empty()) {
            const std::string want = lowerAscii(settings_.characterName);
            for (const auto& ch : characters) {
                if (lowerAscii(ch.name) == want) {
                    chosen = &ch;
                    break;
                }
            }
            if (!chosen) {
                fail("Configured character not found: " + settings_.characterName);
                return;
            }
        } else {
            chosen = &characters.front();
            settings_.characterName = chosen->name;
        }

        selectedCharacter_ = true;
        std::cout << "Selecting character " << chosen->name << "\n";
        game_.selectCharacter(chosen->guid);
    }

    void provisionConfiguredCharacter() {
        if (charCreateRequested_ || charCreateDone_) return;
        const auto& data = *settings_.createCharacter;
        const std::string want = lowerAscii(data.name);
        for (const auto& ch : game_.getCharacters()) {
            if (lowerAscii(ch.name) == want) {
                std::cout << "Character already exists: " << data.name << "\n";
                charCreateDone_ = true;
                if (settings_.exitAfterCreateCharacter) {
                    g_running = false;
                }
                return;
            }
        }

        charCreateRequested_ = true;
        std::cout << "Creating character " << data.name << "\n";
        game_.createCharacter(data);
    }

    void drainPendingChat() {
        if (!isInWorld()) return;

        std::deque<PendingChat> local;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            local.swap(pendingChat_);
        }

        for (const auto& msg : local) {
            game_.sendChatMessage(msg.type, msg.message, msg.target);
        }
    }

    void queueOnEnterWorldCommands() {
        for (const auto& command : settings_.onEnterWorldCommands) {
            scheduledCommands_.push_back(command);
        }
        if (!scheduledCommands_.empty()) {
            nextScheduledCommandDelay_ = 0.0f;
            std::cout << "Queued " << scheduledCommands_.size() << " on-enter-world command(s)\n";
        }
    }

    void drainScheduledCommands(float deltaSeconds) {
        if (!isInWorld() || scheduledCommands_.empty()) return;

        if (nextScheduledCommandDelay_ > 0.0f) {
            nextScheduledCommandDelay_ -= deltaSeconds;
            if (nextScheduledCommandDelay_ > 0.0f) return;
        }

        PendingChat msg;
        msg.type = game::ChatType::SAY;
        msg.message = scheduledCommands_.front();
        scheduledCommands_.pop_front();
        enqueueChat(std::move(msg));
        nextScheduledCommandDelay_ = settings_.onEnterCommandDelaySeconds;
    }

    void stopMovementTaskLocked(const std::string& status, const std::string& error) {
        if (movementTask_.active || movementTask_.started) {
            game_.sendMovement(game::Opcode::MSG_MOVE_STOP);
            game_.sendMovement(game::Opcode::MSG_MOVE_HEARTBEAT);
        }
        movementTask_.active = false;
        movementTask_.started = false;
        movementTask_.status = status;
        movementTask_.error = error;
        movementTask_.lastSetValid = false;
    }

    void advanceToNextWaypointLocked() {
        if (movementTask_.currentWaypoint + 1 < movementTask_.waypoints.size()) {
            ++movementTask_.currentWaypoint;
            movementTask_.lastSetValid = false;
            if (movementTask_.currentWaypoint + 1 == movementTask_.waypoints.size()) {
                std::cout << "Waypoint " << (movementTask_.currentWaypoint + 1)
                          << "/" << movementTask_.waypoints.size() << " (final)\n";
            } else {
                std::cout << "Waypoint " << (movementTask_.currentWaypoint + 1)
                          << "/" << movementTask_.waypoints.size() << "\n";
            }
        } else {
            stopMovementTaskLocked("arrived", "");
        }
    }

    void updateMovementTask(float deltaSeconds) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!movementTask_.active) return;

        if (!isInWorld()) {
            stopMovementTaskLocked("failed", "not in world");
            return;
        }
        if (movementTask_.mapId != 0 && movementTask_.mapId != game_.getCurrentMapId()) {
            stopMovementTaskLocked("failed", "map changed");
            return;
        }
        if (game_.isPlayerRooted()) {
            stopMovementTaskLocked("movement_locked", "player is rooted");
            return;
        }
        if (!game_.isServerMovementAllowed()) {
            stopMovementTaskLocked("movement_locked", "server movement not allowed");
            return;
        }
        if (game_.isOnTaxiFlight()) {
            stopMovementTaskLocked("movement_locked", "on taxi flight");
            return;
        }

        if (movementTask_.waypoints.empty()) {
            stopMovementTaskLocked("failed", "no waypoints");
            return;
        }

        const auto& move = game_.getMovementInfo();

        if (movementTask_.lastSetValid) {
            const float resetDx = move.x - movementTask_.lastSetX;
            const float resetDy = move.y - movementTask_.lastSetY;
            const float resetDz = move.z - movementTask_.lastSetZ;
            const float resetDist = std::sqrt(resetDx * resetDx + resetDy * resetDy + resetDz * resetDz);
            if (resetDist > 1.0f) {
                stopMovementTaskLocked("movement_locked", "server reset position");
                return;
            }
        }

        const float tx = movementTask_.targetX();
        const float ty = movementTask_.targetY();
        const float tz = movementTask_.targetZ();
        const float radius = movementTask_.targetRadius();

        const float dx = tx - move.x;
        const float dy = ty - move.y;
        const float dz = tz - move.z;
        const float horizontalDist = std::sqrt(dx * dx + dy * dy);
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (distance <= radius) {
            // Treat waypoint radius as "close enough", not as permission to
            // snap forward. Snapping at every intermediate waypoint makes the
            // headless client visibly outrun a normal WoW client on dense paths.
            if (movementTask_.currentWaypoint + 1 >= movementTask_.waypoints.size() && distance <= 0.5f) {
                game_.setPosition(tx, ty, tz);
            }
            advanceToNextWaypointLocked();
            return;
        }

        if (horizontalDist > 0.001f) {
            game_.setOrientation(std::atan2(-dy, dx));
            game_.sendMovement(game::Opcode::MSG_MOVE_SET_FACING);
        }
        if (!movementTask_.started) {
            game_.sendMovement(game::Opcode::MSG_MOVE_START_FORWARD);
            movementTask_.started = true;
        }

        const float speed = std::max(0.1f, game_.getServerRunSpeed());
        const float step = std::min(distance, speed * std::max(0.0f, deltaSeconds));
        const float t = distance > 0.001f ? (step / distance) : 1.0f;
        const float newX = move.x + dx * t;
        const float newY = move.y + dy * t;
        const float newZ = move.z + dz * t;
        game_.setPosition(newX, newY, newZ);
        movementTask_.lastSetX = newX;
        movementTask_.lastSetY = newY;
        movementTask_.lastSetZ = newZ;
        movementTask_.lastSetValid = true;
        game_.sendMovement(game::Opcode::MSG_MOVE_HEARTBEAT);
    }

    void syncChatSnapshot() {
        std::lock_guard<std::mutex> lock(chatMutex_);
        syncChatSnapshotLocked();
    }

    void syncChatSnapshotLocked() {
        const auto& history = game_.getChatHistory();
        if (history.size() < lastGameHistorySize_) {
            chatMirror_.clear();
            chatBaseId_ = nextChatId_;
            lastGameHistorySize_ = 0;
        }

        for (size_t i = lastGameHistorySize_; i < history.size(); ++i) {
            if (history[i].message.empty()) continue;
            chatMirror_.push_back(history[i]);
            ++nextChatId_;
            while (chatMirror_.size() > settings_.apiMaxMessages) {
                chatMirror_.pop_front();
                ++chatBaseId_;
            }
        }
        lastGameHistorySize_ = history.size();
    }

    void updateLogoutExit() {
        if (!logoutRequested_) return;
        const auto elapsed = std::chrono::steady_clock::now() - logoutRequestedAt_;
        const bool inWorld = game_.getState() == game::WorldState::IN_WORLD;
        if (!inWorld && !game_.isLoggingOut()) {
            g_running = false;
            return;
        }

        if (!game_.isLoggingOut() && elapsed >= std::chrono::seconds(2)) {
            std::cout << "Logout complete; exiting process.\n";
            g_running = false;
            return;
        }

        if (elapsed >= kLogoutSilentExitTimeout) {
            std::cout << "Logout wait reached "
                      << std::chrono::duration_cast<std::chrono::seconds>(kLogoutSilentExitTimeout).count()
                      << "s; exiting process.\n";
            g_running = false;
        }
    }

    void fail(const std::string& reason) {
        if (!failed_) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            failed_ = true;
            failureReason_ = reason;
            std::cerr << reason << "\n";
        }
    }

    Settings settings_;
    game::GameServices services_;
    auth::AuthHandler auth_;
    game::GameHandler game_;
    pipeline::DBCLayout dbcLayout_;
    pipeline::AssetManager assetManager_;

    std::atomic<bool> failed_{false};
    std::atomic<bool> inWorldForApi_{false};
    mutable std::mutex stateMutex_;
    std::string failureReason_;
    bool selectedCharacter_ = false;
    bool enteredWorld_ = false;
    bool charCreateRequested_ = false;
    bool charCreateDone_ = false;

    std::mutex pendingMutex_;
    std::deque<PendingChat> pendingChat_;
    std::deque<std::string> scheduledCommands_;
    float nextScheduledCommandDelay_ = 0.0f;

    std::mutex chatMutex_;
    std::deque<game::MessageChatData> chatMirror_;
    size_t chatBaseId_ = 1;
    size_t nextChatId_ = 1;
    size_t lastGameHistorySize_ = 0;
    MovementTask movementTask_;
    std::atomic<bool> logoutRequested_{false};
    std::chrono::steady_clock::time_point logoutRequestedAt_{};
};

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const std::string hex = s.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string queryParam(const std::string& query, const std::string& key) {
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = part.find('=');
        std::string k = urlDecode(eq == std::string::npos ? part : part.substr(0, eq));
        if (k == key) return urlDecode(eq == std::string::npos ? "" : part.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

bool apiDebugEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WOWEE_API_DEBUG");
        return value && value[0] != '\0' && std::string(value) != "0";
    }();
    return enabled;
}

const char* httpReason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "Error";
    }
}

void configureApiClientSocket(socket_t client) {
// Accepted sockets can inherit non-blocking mode from the listening socket on
// Windows. The per-client handler expects blocking reads with a short timeout.
#ifdef _WIN32
    u_long blockingMode = 0;
    ioctlsocket(client, FIONBIO, &blockingMode);
#else
    int flags = fcntl(client, F_GETFL, 0);
    if (flags >= 0) fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
#endif

    int one = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef _WIN32
    DWORD timeoutMs = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

bool sendAll(socket_t client, const std::string& response) {
    size_t offset = 0;
    while (offset < response.size()) {
        const size_t remaining = response.size() - offset;
        ssize_t sent = wowee::net::portableSend(
            client,
            reinterpret_cast<const uint8_t*>(response.data() + offset),
            remaining
        );
        if (sent <= 0) {
            if (apiDebugEnabled()) {
                std::cerr << "API send failed: "
                          << wowee::net::errorString(wowee::net::lastError()) << "\n";
            }
            return false;
        }
        offset += static_cast<size_t>(sent);
    }
    return true;
}

bool parseContentLengthLine(const std::string& line, size_t& contentLength) {
    const std::string lower = lowerAscii(line);
    if (lower.rfind("content-length:", 0) != 0) return true;
    const std::string raw = trim(line.substr(15));
    if (raw.empty() || raw.size() > 10) return false;
    size_t value = 0;
    for (char ch : raw) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        value = value * 10 + static_cast<size_t>(ch - '0');
        if (value > 1024 * 1024) return false;
    }
    contentLength = value;
    return true;
}

bool recvAppend(socket_t client, std::string& out, size_t maxBytes) {
    std::array<uint8_t, 8192> buf{};
    ssize_t n = wowee::net::portableRecv(client, buf.data(), buf.size());
    if (n <= 0) {
        if (n < 0 && apiDebugEnabled()) {
            const int err = wowee::net::lastError();
            if (!wowee::net::isConnectionClosed(err)) {
                std::cerr << "API recv failed: " << wowee::net::errorString(err) << "\n";
            }
        }
        return false;
    }
    const size_t available = maxBytes > out.size() ? maxBytes - out.size() : 0;
    const size_t toCopy = std::min(available, static_cast<size_t>(n));
    out.append(reinterpret_cast<const char*>(buf.data()), toCopy);
    return toCopy == static_cast<size_t>(n);
}

void sendHttp(socket_t client, int status, const json& body) {
    const std::string payload = body.dump();
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << httpReason(status) << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Content-Length: " << payload.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << payload;
    const std::string response = out.str();
    bool sent = sendAll(client, response);
    (void)sent; // client may have disconnected — ignore
}

void handleHttpClient(socket_t client, HeadlessSession& session) {
    configureApiClientSocket(client);
    std::string req;
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
        if (!recvAppend(client, req, 65536)) break;
    }

    const size_t headerEnd = req.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        sendHttp(client, req.empty() ? 408 : 400, {{"ok", false}, {"error", req.empty() ? "request timeout" : "bad request: missing header terminator"}});
        wowee::net::closeSocket(client);
        return;
    }

    const std::string headers = req.substr(0, headerEnd);
    std::istringstream hs(headers);
    std::string method;
    std::string target;
    std::string version;
    hs >> method >> target >> version;
    if (method.empty() || target.empty() || version.rfind("HTTP/", 0) != 0) {
        sendHttp(client, 400, {{"ok", false}, {"error", "bad request line"}});
        wowee::net::closeSocket(client);
        return;
    }

    size_t contentLength = 0;
    std::string line;
    std::getline(hs, line);
    while (std::getline(hs, line)) {
        line = trim(line);
        if (!parseContentLengthLine(line, contentLength)) {
            sendHttp(client, 400, {{"ok", false}, {"error", "invalid Content-Length"}});
            wowee::net::closeSocket(client);
            return;
        }
    }
    if (contentLength > 1024 * 1024) {
        sendHttp(client, 413, {{"ok", false}, {"error", "request body too large"}});
        wowee::net::closeSocket(client);
        return;
    }

    std::string body = req.substr(headerEnd + 4);
    while (body.size() < contentLength) {
        if (!recvAppend(client, body, contentLength)) break;
    }
    if (body.size() < contentLength) {
        sendHttp(client, 400, {{"ok", false}, {"error", "incomplete request body"}});
        wowee::net::closeSocket(client);
        return;
    }

    const size_t qpos = target.find('?');
    const std::string path = qpos == std::string::npos ? target : target.substr(0, qpos);
    const std::string query = qpos == std::string::npos ? "" : target.substr(qpos + 1);
    if (apiDebugEnabled()) {
        std::cerr << "API " << method << " " << path << " body=" << body.size() << "\n";
    }

    try {
        if (method == "OPTIONS") {
            sendHttp(client, 200, {{"ok", true}});
        } else if (method == "GET" && path == "/status") {
            sendHttp(client, 200, session.statusJson());
        } else if (method == "GET" && path == "/world/self") {
            sendHttp(client, 200, session.worldSelf());
        } else if (method == "GET" && path == "/world/entities") {
            const std::string radiusParam = queryParam(query, "radius");
            const float radius = radiusParam.empty() ? 120.0f : std::stof(radiusParam);
            const std::string transportsParam = queryParam(query, "transports");
            const bool onlyTransports = transportsParam == "1" || transportsParam == "true" || transportsParam == "yes";
            sendHttp(client, 200, session.worldEntities(radius, onlyTransports));
        } else if (method == "GET" && path == "/party") {
            sendHttp(client, 200, session.partyJson());
        } else if (method == "GET" && path == "/chat") {
            const size_t after = queryParam(query, "after").empty() ? 0 : static_cast<size_t>(std::stoull(queryParam(query, "after")));
            const size_t limit = queryParam(query, "limit").empty() ? 100 : static_cast<size_t>(std::stoull(queryParam(query, "limit")));
            sendHttp(client, 200, session.readChat(after, limit));
        } else if (method == "POST" && path == "/chat") {
            json payload = json::parse(body.empty() ? "{}" : body);
            auto type = chatTypeFromString(payload.value("type", "say"));
            if (!type) {
                sendHttp(client, 400, {{"error", "unsupported chat type"}});
            } else {
                PendingChat msg;
                msg.type = *type;
                msg.message = payload.value("message", "");
                msg.target = payload.value("target", payload.value("channel", ""));
                if (msg.message.empty()) {
                    sendHttp(client, 400, {{"error", "message is required"}});
                } else if ((msg.type == game::ChatType::WHISPER || msg.type == game::ChatType::CHANNEL) && msg.target.empty()) {
                    sendHttp(client, 400, {{"error", "target/channel is required for this chat type"}});
                } else {
                    session.enqueueChat(std::move(msg));
                    sendHttp(client, 200, {{"ok", true}});
                }
            }
        } else if (method == "POST" && path == "/commands") {
            json payload = json::parse(body.empty() ? "{}" : body);
            if (payload.contains("commands") && payload["commands"].is_array()) {
                json results = json::array();
                for (const auto& item : payload["commands"]) {
                    results.push_back(session.queueCommand(item.is_string() ? item.get<std::string>() : ""));
                }
                sendHttp(client, 200, {{"ok", true}, {"results", results}});
            } else {
                const auto result = session.queueCommand(payload.value("command", payload.value("message", "")));
                sendHttp(client, result.value("ok", false) ? 200 : 400, result);
            }
        } else if (method == "POST" && path == "/movement/goto/waypoints") {
            json payload = json::parse(body.empty() ? "{}" : body);
            const uint32_t mapId = static_cast<uint32_t>(std::max(0, payload.value("mapId", 0)));
            const float defaultRadius = payload.value("arrivalRadius", 3.0f);
            std::vector<Waypoint> waypoints;
            for (const auto& wpJson : payload.value("waypoints", json::array())) {
                if (!wpJson.is_object()) continue;
                Waypoint wp;
                wp.x = wpJson.value("x", 0.0f);
                wp.y = wpJson.value("y", 0.0f);
                wp.z = wpJson.value("z", 0.0f);
                wp.arrivalRadius = wpJson.value("arrivalRadius", 0.0f);
                if (std::isfinite(wp.x) && std::isfinite(wp.y) && std::isfinite(wp.z)) {
                    waypoints.push_back(wp);
                }
            }
            const auto result = session.startGotoWaypoints(mapId, waypoints, defaultRadius);
            sendHttp(client, result.value("ok", false) ? 200 : 400, result);
        } else if (method == "POST" && path == "/movement/goto") {
            json payload = json::parse(body.empty() ? "{}" : body);
            const uint32_t mapId = static_cast<uint32_t>(std::max(0, payload.value("mapId", 0)));
            const float x = payload.value("x", 0.0f);
            const float y = payload.value("y", 0.0f);
            const float z = payload.value("z", 0.0f);
            const float arrivalRadius = payload.value("arrivalRadius", 3.0f);
            const auto result = session.startGoto(mapId, x, y, z, arrivalRadius);
            sendHttp(client, result.value("ok", false) ? 200 : 400, result);
        } else if (method == "POST" && path == "/movement/stop") {
            json payload = json::parse(body.empty() ? "{}" : body);
            sendHttp(client, 200, session.stopMovementTask(payload.value("reason", "stopped")));
        } else if (method == "POST" && path == "/area-trigger") {
            json payload = json::parse(body.empty() ? "{}" : body);
            const uint32_t triggerId = static_cast<uint32_t>(std::max(0, payload.value("id", payload.value("triggerId", 0))));
            const auto result = session.fireAreaTrigger(triggerId);
            sendHttp(client, result.value("ok", false) ? 200 : 400, result);
        } else {
            sendHttp(client, 404, {{"ok", false}, {"error", "not found"}});
        }
    } catch (const std::exception& ex) {
        std::cerr << "API handler exception for " << method << " " << path << ": " << ex.what() << "\n";
        sendHttp(client, 500, {{"ok", false}, {"error", ex.what()}, {"path", path}});
    } catch (...) {
        std::cerr << "API handler unknown exception for " << method << " " << path << "\n";
        sendHttp(client, 500, {{"ok", false}, {"error", "unknown API handler exception"}, {"path", path}});
    }

    wowee::net::closeSocket(client);
}

void runHttpServer(const Settings& settings, HeadlessSession& session) {
    wowee::net::ensureInit();
    socket_t server = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCK) {
        std::cerr << "API socket() failed\n";
        return;
    }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(settings.apiPort);
    if (inet_pton(AF_INET, settings.apiBind.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "API bind address must be an IPv4 address: " << settings.apiBind << "\n";
        wowee::net::closeSocket(server);
        return;
    }

    if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "API bind failed on " << settings.apiBind << ":" << settings.apiPort << "\n";
        wowee::net::closeSocket(server);
        return;
    }
    if (::listen(server, 16) != 0) {
        std::cerr << "API listen failed\n";
        wowee::net::closeSocket(server);
        return;
    }

    wowee::net::setNonBlocking(server);
    std::cout << "Chat API listening on http://" << settings.apiBind << ":" << settings.apiPort << "\n";

    while (g_running) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int len = sizeof(clientAddr);
#else
        socklen_t len = sizeof(clientAddr);
#endif
        socket_t client = ::accept(server, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (client == INVALID_SOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        handleHttpClient(client, session);
    }

    wowee::net::closeSocket(server);
}

void signalHandler(int) {
    g_running = false;
}

void usage() {
    std::cerr << "Usage: wowee_headless [settings.json]\n";
}

} // namespace

int main(int argc, char** argv) {
    wowee::core::installCrashDiagnostics("wowee_headless");
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    setDefaultEnv("WOWEE_HEADLESS", "1");
    setDefaultEnv("WOWEE_NET_ASYNC_PUMP", "0");
    wowee::net::ensureInit();

    const std::string settingsPath = argc >= 2 ? argv[1] : "tools/headless_client/settings.json";
    if (argc > 2) {
        usage();
        return 2;
    }

    try {
        Settings settings = loadSettings(settingsPath);
        setDefaultEnv("WOWEE_ACTIVE_EXPANSION", settings.expansion.c_str());
        std::cout << "WoWee headless client using " << settingsPath << "\n";
        std::cout << "Expansion profile: " << settings.expansion << "\n";

        HeadlessSession session(settings);
        if (!session.start()) return 1;

        std::thread apiThread;
        if (settings.apiEnabled) {
            apiThread = std::thread([&]() { runHttpServer(settings, session); });
        }

        auto last = std::chrono::steady_clock::now();
        while (g_running && !session.isFailed()) {
            if (consumeEscKeypress()) {
                session.requestGracefulLogout();
            }

            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            session.update(dt);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        g_running = false;
        if (apiThread.joinable()) apiThread.join();
        return session.isFailed() ? 1 : 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
