#include "auth/auth_handler.hpp"
#include "auth/auth_packets.hpp"
#include "game/game_handler.hpp"
#include "game/game_services.hpp"
#include "game/packet_parsers.hpp"
#include "game/world_packets.hpp"
#include "network/net_platform.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_layout.hpp"
#include "rendering/animation/emote_registry.hpp"
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
#include <thread>
#include <ctime>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#endif

using json = nlohmann::json;
using namespace wowee;

namespace {

std::atomic<bool> g_running{true};

constexpr int kEscKey = 27;
constexpr auto kLogoutSilentExitTimeout = std::chrono::seconds(12);

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
};

struct PendingChat {
    game::ChatType type = game::ChatType::SAY;
    std::string message;
    std::string target;
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

        if (!auth_.connect(settings_.authHost, settings_.authPort)) {
            fail("Could not connect to auth server");
            return false;
        }
        auth_.authenticate(settings_.account, settings_.password);
        return true;
    }

    void update(float deltaSeconds) {
        auth_.update(deltaSeconds);
        const auto beforeWorldState = game_.getState();
        game_.update(deltaSeconds);
        const auto afterWorldState = game_.getState();
        if (afterWorldState != beforeWorldState) {
            std::cout << "World state changed: "
                      << static_cast<int>(beforeWorldState) << " -> "
                      << static_cast<int>(afterWorldState) << "\n";
        }
        inWorldForApi_ = (game_.getState() == game::WorldState::IN_WORLD);

        if (!selectedCharacter_ && game_.getState() == game::WorldState::CHAR_LIST_RECEIVED) {
            selectConfiguredCharacter();
        }

        if (!enteredWorld_ && game_.getState() == game::WorldState::IN_WORLD) {
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
            drainScheduledCommands(deltaSeconds);
            drainPendingChat();
        }
        syncChatSnapshot();
        updateLogoutExit();
    }

    bool isFailed() const { return failed_.load(); }
    bool isInWorld() const { return inWorldForApi_.load(); }
    bool isLogoutRequested() const { return logoutRequested_.load(); }
    std::string status() const {
        if (failed_) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            return "failed: " + failureReason_;
        }
        if (logoutRequested_) return "logging_out";
        if (isInWorld()) return "in_world";
        return "connecting";
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
            rendering::EmoteRegistry::instance().loadFromDbc(&assetManager_);
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

    std::mutex pendingMutex_;
    std::deque<PendingChat> pendingChat_;
    std::deque<std::string> scheduledCommands_;
    float nextScheduledCommandDelay_ = 0.0f;

    std::mutex chatMutex_;
    std::deque<game::MessageChatData> chatMirror_;
    size_t chatBaseId_ = 1;
    size_t nextChatId_ = 1;
    size_t lastGameHistorySize_ = 0;
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

void sendHttp(socket_t client, int status, const json& body) {
    const std::string payload = body.dump();
    std::ostringstream out;
    out << "HTTP/1.1 " << status << (status == 200 ? " OK" : " Error") << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Content-Length: " << payload.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << payload;
    const std::string response = out.str();
    wowee::net::portableSend(client, reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

void handleHttpClient(socket_t client, HeadlessSession& session) {
    std::string req;
    std::array<uint8_t, 8192> buf{};
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
        ssize_t n = wowee::net::portableRecv(client, buf.data(), buf.size());
        if (n <= 0) break;
        req.append(reinterpret_cast<const char*>(buf.data()), static_cast<size_t>(n));
    }

    const size_t headerEnd = req.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        sendHttp(client, 400, {{"error", "bad request"}});
        wowee::net::closeSocket(client);
        return;
    }

    const std::string headers = req.substr(0, headerEnd);
    std::istringstream hs(headers);
    std::string method;
    std::string target;
    std::string version;
    hs >> method >> target >> version;

    size_t contentLength = 0;
    std::string line;
    std::getline(hs, line);
    while (std::getline(hs, line)) {
        line = trim(line);
        const std::string lower = lowerAscii(line);
        if (lower.rfind("content-length:", 0) == 0) {
            contentLength = static_cast<size_t>(std::stoul(trim(line.substr(15))));
        }
    }

    std::string body = req.substr(headerEnd + 4);
    while (body.size() < contentLength) {
        ssize_t n = wowee::net::portableRecv(client, buf.data(), buf.size());
        if (n <= 0) break;
        body.append(reinterpret_cast<const char*>(buf.data()), static_cast<size_t>(n));
    }

    const size_t qpos = target.find('?');
    const std::string path = qpos == std::string::npos ? target : target.substr(0, qpos);
    const std::string query = qpos == std::string::npos ? "" : target.substr(qpos + 1);

    try {
        if (method == "GET" && path == "/status") {
            sendHttp(client, 200, {{"status", session.status()}, {"inWorld", session.isInWorld()}});
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
        } else {
            sendHttp(client, 404, {{"error", "not found"}});
        }
    } catch (const std::exception& ex) {
        sendHttp(client, 400, {{"error", ex.what()}});
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
