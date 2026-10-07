#ifdef GEODE_IS_WINDOWS
#include <winsock.h>
#endif

#include "StreamOverlayServer.hpp"

#include "TwitchLevelBriefCache.hpp"
#include "TwitchLevelOpen.hpp"
#include "../TwitchRequestFilters.hpp"
#include "../TwitchRequestManager.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>

#ifdef GEODE_IS_MACOS
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr char const* kModuleID = "paimbnails.streamoverlay.menu";
constexpr char const* kConfigKey = "twitch-requests-obs-config";
constexpr uint16_t kPort = 21680;
constexpr float kRefreshSeconds = .25f;
constexpr float kRestartRetrySeconds = 2.f;
// gallery opens 19 previews at once.
constexpr int kListenBacklog = 32;
constexpr int kMaxNextCount = 8;

std::string urlFor(char const* path) {
    return fmt::format("http://localhost:{}{}", kPort, path);
}

struct OptionInfo {
    char const* key;
    char const* name;
};

constexpr OptionInfo kStyles[] = {
    {"glass", "Cristal"},
    {"gd", "Geometry Dash"},
    {"neon", "Neon"},
    {"synthwave", "Synthwave"},
    {"arcade", "Arcade 8-bit"},
    {"terminal", "Terminal"},
    {"minimal", "Minimalista"},
    {"comic", "Comic"},
    {"glitch", "Glitch"},
    {"holo", "Holografico"},
    {"aurora", "Aurora"},
    {"inferno", "Infierno"},
    {"frost", "Hielo"},
    {"galaxy", "Galaxia"},
    {"royal", "Real dorado"},
    {"pastel", "Pastel kawaii"},
    {"brutal", "Brutalista"},
    {"cozy", "Lo-fi calido"},
    {"esports", "Esports HUD"},
};
static_assert(std::size(kStyles) == kStreamOverlayStyleCount);

constexpr OptionInfo kLayouts[] = {
    {"cards", "Tarjetas"},
    {"compact", "Compacto"},
    {"ticker", "Cinta inferior"},
    {"sidebar", "Lateral derecho"},
    {"spotlight", "Solo nivel actual"},
    {"corner", "Esquina mini"},
    {"banner", "Banner superior"},
};
static_assert(std::size(kLayouts) == kStreamOverlayLayoutCount);

constexpr OptionInfo kAnimations[] = {
    {"flow", "Flotante"},
    {"slide", "Deslizar"},
    {"pulse", "Pulso"},
    {"none", "Sin animacion"},
    {"bounce", "Rebote"},
    {"flip", "Giro 3D"},
    {"zoom", "Zoom"},
    {"glitch", "Glitch"},
    {"drop", "Caida"},
    {"blur", "Desenfoque"},
    {"typewriter", "Maquina de escribir"},
};
static_assert(std::size(kAnimations) == kStreamOverlayAnimationCount);

// overlay files ship in resources/; the installed .geode flattens them.
struct OverlayRoute {
    char const* path;
    char const* file;
};

constexpr OverlayRoute kOverlayRoutes[] = {
    {"/overlay.css", "stream-overlay.css"},
    {"/overlay.js", "stream-overlay.js"},
    {"/gallery", "stream-overlay-gallery.html"},
};
constexpr char const* kOverlayIndex = "stream-overlay.html";

StreamOverlayConfig g_config;

template <size_t N>
std::vector<std::string> optionNames(OptionInfo const (&options)[N]) {
    std::vector<std::string> names;
    names.reserve(N);
    for (auto const& option : options) names.emplace_back(option.name);
    return names;
}

template <size_t N>
matjson::Value optionJson(OptionInfo const (&options)[N]) {
    auto list = matjson::Value::array();
    for (auto const& option : options) {
        list.push(matjson::makeObject({{"key", option.key}, {"name", option.name}}));
    }
    return list;
}

int64_t packColor(ccColor3B color) {
    return (static_cast<int64_t>(color.r) << 16)
        | (static_cast<int64_t>(color.g) << 8)
        | static_cast<int64_t>(color.b);
}

ccColor3B unpackColor(int64_t value, ccColor3B fallback) {
    if (value < 0 || value > 0xffffff) return fallback;
    return {
        static_cast<GLubyte>((value >> 16) & 0xff),
        static_cast<GLubyte>((value >> 8) & 0xff),
        static_cast<GLubyte>(value & 0xff),
    };
}

bool sameColor(ccColor3B a, ccColor3B b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

std::string cssColor(ccColor3B color) {
    return fmt::format("#{:02x}{:02x}{:02x}", color.r, color.g, color.b);
}

template <typename T>
T clampOption(int value, int count) {
    return static_cast<T>(std::clamp(value, 0, count - 1));
}

void clampConfig(StreamOverlayConfig& config) {
    config.style = clampOption<StreamOverlayStyle>(static_cast<int>(config.style), kStreamOverlayStyleCount);
    config.layout = clampOption<StreamOverlayLayout>(static_cast<int>(config.layout), kStreamOverlayLayoutCount);
    config.animation = clampOption<StreamOverlayAnimation>(
        static_cast<int>(config.animation), kStreamOverlayAnimationCount);
    config.nextCount = std::clamp(config.nextCount, 1, kMaxNextCount);
    config.scale = std::clamp(config.scale, .5f, 1.6f);
    config.opacity = std::clamp(config.opacity, .15f, 1.f);
    config.roundness = std::clamp(config.roundness, 0.f, 34.f);
}

void loadConfig() {
    auto saved = Mod::get()->getSavedValue<matjson::Value>(
        kConfigKey, matjson::makeObject({}));

    StreamOverlayConfig config;
    auto readBool = [&saved](char const* key, bool& value) {
        value = saved[key].asBool().unwrapOr(value);
    };
    config.style = static_cast<StreamOverlayStyle>(
        saved["style"].asInt().unwrapOr(static_cast<int>(config.style)));
    config.layout = static_cast<StreamOverlayLayout>(
        saved["layout"].asInt().unwrapOr(static_cast<int>(config.layout)));
    config.animation = static_cast<StreamOverlayAnimation>(
        saved["animation"].asInt().unwrapOr(static_cast<int>(config.animation)));
    config.nextCount = static_cast<int>(
        saved["nextCount"].asInt().unwrapOr(config.nextCount));
    config.scale = static_cast<float>(saved["scale"].asDouble().unwrapOr(config.scale));
    config.opacity = static_cast<float>(
        saved["opacity"].asDouble().unwrapOr(config.opacity));
    config.roundness = static_cast<float>(
        saved["roundness"].asDouble().unwrapOr(config.roundness));

    StreamOverlayConfig const defaults;
    config.accent = unpackColor(
        saved["accent"].asInt().unwrapOr(packColor(config.accent)), config.accent);
    config.background = unpackColor(
        saved["background"].asInt().unwrapOr(packColor(config.background)), config.background);
    config.text = unpackColor(
        saved["text"].asInt().unwrapOr(packColor(config.text)), config.text);
    // configs saved before styles existed keep whatever palette the user picked.
    bool const legacyCustom = !sameColor(config.accent, defaults.accent)
        || !sameColor(config.background, defaults.background)
        || !sameColor(config.text, defaults.text);
    config.customColors = saved["customColors"].asBool().unwrapOr(legacyCustom);

    readBool("showLevelID", config.showLevelID);
    readBool("showAuthor", config.showAuthor);
    readBool("showRequester", config.showRequester);
    readBool("showProgress", config.showProgress);
    readBool("showQueueCount", config.showQueueCount);
    readBool("showDifficulty", config.showDifficulty);
    readBool("showPlatform", config.showPlatform);
    readBool("showAttempts", config.showAttempts);
    readBool("showStats", config.showStats);
    readBool("showAlerts", config.showAlerts);
    readBool("alertSound", config.alertSound);
    readBool("showCelebration", config.showCelebration);
    readBool("showParticles", config.showParticles);
    readBool("hideWhenIdle", config.hideWhenIdle);
    clampConfig(config);
    g_config = config;
}

matjson::Value toggleJson(StreamOverlayConfig const& config) {
    return matjson::makeObject({
        {"customColors", config.customColors},
        {"showLevelID", config.showLevelID},
        {"showAuthor", config.showAuthor},
        {"showRequester", config.showRequester},
        {"showProgress", config.showProgress},
        {"showQueueCount", config.showQueueCount},
        {"showDifficulty", config.showDifficulty},
        {"showPlatform", config.showPlatform},
        {"showAttempts", config.showAttempts},
        {"showStats", config.showStats},
        {"showAlerts", config.showAlerts},
        {"alertSound", config.alertSound},
        {"showCelebration", config.showCelebration},
        {"showParticles", config.showParticles},
        {"hideWhenIdle", config.hideWhenIdle},
    });
}

matjson::Value configJson(StreamOverlayConfig const& config) {
    auto json = toggleJson(config);
    json["style"] = kStyles[static_cast<int>(config.style)].key;
    json["layout"] = kLayouts[static_cast<int>(config.layout)].key;
    json["animation"] = kAnimations[static_cast<int>(config.animation)].key;
    json["nextCount"] = config.nextCount;
    json["scale"] = config.scale;
    json["opacity"] = config.opacity;
    json["roundness"] = config.roundness;
    json["accent"] = cssColor(config.accent);
    json["background"] = cssColor(config.background);
    json["text"] = cssColor(config.text);
    return json;
}

matjson::Value briefJson(LevelBrief const* brief) {
    bool const found = brief && brief->found;
    return matjson::makeObject({
        {"name", found ? brief->name : ""},
        {"author", found ? brief->author : ""},
        {"stars", found ? brief->stars : 0},
        {"difficulty", found ? brief->difficulty : 0},
        {"length", found ? brief->length : 0},
        {"platformer", found && brief->platformer},
        {"known", found},
    });
}

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string readFileRaw(std::filesystem::path const& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec) return {};
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::filesystem::path resolveGameFile(char const* name, bool skipSuffix) {
    auto* utils = CCFileUtils::sharedFileUtils();
    if (!utils) return {};
    std::string path = utils->fullPathForFilename(name, skipSuffix);
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return {};
    return path;
}

std::filesystem::path withExtension(std::filesystem::path path, char const* extension) {
    if (path.empty()) return {};
    path.replace_extension(extension);
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) ? path : std::filesystem::path{};
}

// fixed keys only: the browser never names a file on disk.
std::unordered_map<std::string, std::filesystem::path> resolveGdAssets() {
    std::unordered_map<std::string, std::filesystem::path> assets;
    auto add = [&assets](char const* key, std::filesystem::path path) {
        if (!path.empty()) assets.emplace(key, std::move(path));
    };
    auto preferHd = [](char const* hdName, char const* anyName) {
        auto path = resolveGameFile(hdName, true);
        return path.empty() ? resolveGameFile(anyName, false) : path;
    };

    auto addFont = [&add](char const* key, char const* name) {
        auto path = resolveGameFile(name, false);
        add(fmt::format("{}.fnt", key).c_str(), path);
        add(fmt::format("{}.png", key).c_str(), withExtension(path, ".png"));
    };
    addFont("gold", "goldFont.fnt");
    addFont("big", "bigFont.fnt");
    // the uhd sheet decodes to 64 mb in the browser; hd is plenty for 60px icons.
    auto sheet = preferHd("GJ_GameSheet03-hd.plist", "GJ_GameSheet03.plist");
    add("sheet.plist", sheet);
    add("sheet.png", withExtension(sheet, ".png"));
    add("square01.png", resolveGameFile("GJ_square01.png", false));
    add("square02.png", resolveGameFile("GJ_square02.png", false));
    add("bg.png", preferHd("game_bg_01_001-hd.png", "game_bg_01_001.png"));
    add("ground.png", resolveGameFile("groundSquare_01_001.png", false));
    return assets;
}

std::string_view contentTypeFor(std::string_view name) {
    if (name.ends_with(".html")) return "text/html; charset=utf-8";
    if (name.ends_with(".css")) return "text/css; charset=utf-8";
    if (name.ends_with(".js")) return "application/javascript; charset=utf-8";
    if (name.ends_with(".png")) return "image/png";
    if (name.ends_with(".plist")) return "application/xml; charset=utf-8";
    return "text/plain; charset=utf-8";
}

constexpr char const* kMissingFilesHtml = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><title>Paimbnails overlay</title></head>
<body style="font-family:system-ui,sans-serif;color:#fff;background:#151826;padding:32px">
<h2>Faltan los archivos del overlay</h2>
<p>No se encontro stream-overlay.html en los recursos del mod. Reinstala Paimbnails.</p>
</body></html>)HTML";

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)

#ifdef GEODE_IS_WINDOWS
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) return;
#ifdef GEODE_IS_WINDOWS
    closesocket(socket);
#else
    close(socket);
#endif
}

void setClientTimeouts(SocketHandle socket) {
#ifdef GEODE_IS_WINDOWS
    DWORD timeout = 2000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<char const*>(&timeout), sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<char const*>(&timeout), sizeof(timeout));
#else
    timeval timeout{2, 0};
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    int noSigPipe = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
#endif
}

bool sendAll(SocketHandle socket, std::string const& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int count = send(socket, data.data() + sent,
            static_cast<int>(std::min<size_t>(data.size() - sent, 16 * 1024)), 0);
        if (count <= 0) return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

std::string httpResponse(
    int code,
    std::string_view status,
    std::string_view contentType,
    std::string const& body,
    int cacheSeconds = 0
) {
    auto cache = cacheSeconds > 0
        ? fmt::format("public, max-age={}", cacheSeconds)
        : std::string("no-store, no-cache, must-revalidate");
    return fmt::format(
        "HTTP/1.1 {} {}\r\n"
        "Content-Type: {}\r\n"
        "Content-Length: {}\r\n"
        "Cache-Control: {}\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n{}",
        code, status, contentType, body.size(), cache, body);
}

std::string notFound() {
    return httpResponse(404, "Not Found", "text/plain; charset=utf-8", "Not found");
}

#endif

class StreamOverlayTicker final : public CCNode {
public:
    static StreamOverlayTicker* create() {
        auto* ret = new StreamOverlayTicker();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void update(float dt) override {
        StreamOverlayServer::get().tick(dt);
    }
};

Ref<StreamOverlayTicker> g_ticker;

} // namespace

struct StreamOverlayServer::Impl {
    std::atomic_bool stopping = false;
    std::atomic_bool running = false;
    // a finished thread stays joinable: completion sits apart from running so
    // a failed start is reaped without ever blocking.
    std::atomic_bool finished = true;
    std::thread worker;
    mutable std::mutex mutex;
    std::string payload = "{}";
    std::string status = "Apagado";
    // written on the main thread before the worker starts, read-only afterwards.
    std::filesystem::path resourcesDir;
    std::unordered_map<std::string, std::filesystem::path> gdAssets;
    std::string catalog = "{}";

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    std::atomic<SocketHandle> listener = kInvalidSocket;

    void setStatus(std::string text) {
        std::lock_guard lock(mutex);
        status = std::move(text);
    }

    // read on every request so an edited file shows up after refreshing obs.
    std::string overlayFile(std::string_view name) const {
        auto contents = readFileRaw(resourcesDir / std::string(name));
        if (contents.empty()) contents = readFileRaw(resourcesDir / "overlay" / std::string(name));
        return contents;
    }

    std::string serveOverlayFile(std::string_view name) const {
        auto body = overlayFile(name);
        if (body.empty()) {
            if (name.ends_with(".html")) {
                return httpResponse(500, "Internal Server Error",
                    contentTypeFor(".html"), kMissingFilesHtml);
            }
            return notFound();
        }
        return httpResponse(200, "OK", contentTypeFor(name), body);
    }

    std::string responseFor(std::string path) const {
        auto query = path.find('?');
        if (query != std::string::npos) path.resize(query);

        if (path == "/" || path == "/overlay" || path == "/preview") {
            return serveOverlayFile(kOverlayIndex);
        }
        for (auto const& route : kOverlayRoutes) {
            if (path == route.path) return serveOverlayFile(route.file);
        }
        if (path == "/api/state") {
            std::lock_guard lock(mutex);
            return httpResponse(200, "OK", "application/json; charset=utf-8", payload);
        }
        if (path == "/api/styles") {
            return httpResponse(200, "OK", "application/json; charset=utf-8", catalog);
        }
        if (path.starts_with("/gd/")) {
            auto it = gdAssets.find(path.substr(4));
            if (it == gdAssets.end()) {
                return notFound();
            }
            auto body = readFileRaw(it->second);
            if (body.empty()) {
                return notFound();
            }
            return httpResponse(200, "OK", contentTypeFor(it->first), body, 3600);
        }
        if (path == "/health") {
            return httpResponse(200, "OK", "text/plain; charset=utf-8", "Paimbnails OBS overlay OK");
        }
        if (path == "/favicon.ico") {
            return httpResponse(204, "No Content", "image/x-icon", "");
        }
        return notFound();
    }

    void handleClient(SocketHandle client) const {
        setClientTimeouts(client);
        std::string request;
        request.reserve(2048);
        char buffer[2048];
        while (request.size() < 16 * 1024) {
            int count = recv(client, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            request.append(buffer, static_cast<size_t>(count));
            if (request.find("\r\n\r\n") != std::string::npos) break;
        }

        auto lineEnd = request.find("\r\n");
        auto firstLine = request.substr(0, lineEnd);
        auto firstSpace = firstLine.find(' ');
        auto secondSpace = firstSpace == std::string::npos
            ? std::string::npos : firstLine.find(' ', firstSpace + 1);
        if (firstLine.compare(0, firstSpace, "GET") != 0
            || firstSpace == std::string::npos || secondSpace == std::string::npos) {
            sendAll(client, httpResponse(405, "Method Not Allowed",
                "text/plain; charset=utf-8", "GET only"));
            return;
        }
        sendAll(client, responseFor(
            firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1)));
    }

    void closeListener() {
        auto socket = listener.exchange(kInvalidSocket);
        if (socket == kInvalidSocket) return;
#ifdef GEODE_IS_WINDOWS
        ::shutdown(socket, 2);
#else
        ::shutdown(socket, SHUT_RDWR);
#endif
        closeSocket(socket);
    }

    void run() {
        struct FinishRun {
            Impl* impl;
            ~FinishRun() {
                impl->running = false;
                impl->finished = true;
            }
        } finish{this};

#ifdef GEODE_IS_WINDOWS
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            setStatus("Windows no pudo iniciar la red local");
            return;
        }
#endif

        auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == kInvalidSocket) {
            setStatus("No se pudo crear el servidor local");
#ifdef GEODE_IS_WINDOWS
            WSACleanup();
#endif
            return;
        }
        listener = socket;

#ifdef GEODE_IS_WINDOWS
        int reuse = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<char const*>(&reuse), sizeof(reuse));
#else
        int reuse = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kPort);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
            || listen(socket, kListenBacklog) != 0) {
            setStatus("El puerto 21680 ya esta ocupado");
            closeListener();
#ifdef GEODE_IS_WINDOWS
            WSACleanup();
#endif
            return;
        }

        running = true;
        setStatus("Activo en localhost:21680");
        while (!stopping) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socket, &readSet);
            timeval timeout{0, 250000};
#ifdef GEODE_IS_WINDOWS
            int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
            int ready = select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
            if (ready <= 0 || stopping) continue;

            auto client = accept(socket, nullptr, nullptr);
            if (client == kInvalidSocket) continue;
            handleClient(client);
            closeSocket(client);
        }

        closeListener();
#ifdef GEODE_IS_WINDOWS
        WSACleanup();
#endif
    }
#endif
};

std::vector<std::string> streamOverlayStyleNames() {
    return optionNames(kStyles);
}

std::vector<std::string> streamOverlayLayoutNames() {
    return optionNames(kLayouts);
}

std::vector<std::string> streamOverlayAnimationNames() {
    return optionNames(kAnimations);
}

StreamOverlayConfig const& streamOverlayConfig() {
    return g_config;
}

void setStreamOverlayConfig(StreamOverlayConfig config) {
    clampConfig(config);
    g_config = config;
    auto saved = toggleJson(config);
    saved["style"] = static_cast<int>(config.style);
    saved["layout"] = static_cast<int>(config.layout);
    saved["animation"] = static_cast<int>(config.animation);
    saved["nextCount"] = config.nextCount;
    saved["scale"] = config.scale;
    saved["opacity"] = config.opacity;
    saved["roundness"] = config.roundness;
    saved["accent"] = packColor(config.accent);
    saved["background"] = packColor(config.background);
    saved["text"] = packColor(config.text);
    Mod::get()->setSavedValue<matjson::Value>(kConfigKey, saved);
    paimon::requestDeferredModSave();
    StreamOverlayServer::get().refreshSnapshot();
}

StreamOverlayServer& StreamOverlayServer::get() {
    static StreamOverlayServer instance;
    return instance;
}

StreamOverlayServer::StreamOverlayServer() : m_impl(std::make_unique<Impl>()) {}

StreamOverlayServer::~StreamOverlayServer() {
    stop();
}

void StreamOverlayServer::init() {
    if (m_initialized) return;
    m_initialized = true;
    loadConfig();

    auto* director = CCDirector::get();
    if (director && director->getScheduler()) {
        g_ticker = StreamOverlayTicker::create();
        if (g_ticker) {
            director->getScheduler()->scheduleUpdateForTarget(g_ticker.data(), 0, false);
        }
    }
    restart();
}

void StreamOverlayServer::shutdown() {
    if (!m_initialized) return;
    m_initialized = false;
    if (g_ticker) {
        if (auto* director = CCDirector::get(); director && director->getScheduler()) {
            director->getScheduler()->unscheduleUpdateForTarget(g_ticker.data());
        }
        (void)g_ticker.take();
    }
    stop();
}

bool StreamOverlayServer::supported() const {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    return true;
#else
    return false;
#endif
}

void StreamOverlayServer::start() {
    if (!supported() || m_impl->worker.joinable()) return;
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    m_impl->stopping = false;
    m_impl->running = false;
    m_impl->finished = false;
    m_restartClock = 0.f;
    m_impl->setStatus("Iniciando servidor local...");
    // cocos file lookups are main-thread only; resolve everything before the worker runs.
    m_impl->resourcesDir = Mod::get()->getResourcesDir();
    m_impl->gdAssets = resolveGdAssets();
    m_impl->catalog = matjson::makeObject({
        {"styles", optionJson(kStyles)},
        {"layouts", optionJson(kLayouts)},
        {"animations", optionJson(kAnimations)},
    }).dump(matjson::NO_INDENTATION);
    refreshSnapshot();
    try {
        m_impl->worker = std::thread([impl = m_impl.get()] { impl->run(); });
    } catch (...) {
        m_impl->finished = true;
        m_impl->setStatus("No se pudo iniciar el hilo del servidor");
    }
#endif
}

void StreamOverlayServer::stop() {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    m_impl->stopping = true;
    m_impl->closeListener();
    if (m_impl->worker.joinable()) m_impl->worker.join();
    m_impl->running = false;
    m_impl->finished = true;
    m_restartClock = 0.f;
    m_impl->setStatus("Apagado");
#endif
}

void StreamOverlayServer::restart() {
    stop();
    if (!m_initialized || !paimon::modules::isEnabled(kModuleID)) return;
    start();
}

void StreamOverlayServer::tick(float dt) {
    if (!m_initialized) return;
    if (!paimon::modules::isEnabled(kModuleID)) {
        if (m_impl->worker.joinable()) stop();
        return;
    }

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    // the port may still be held by a previous process: reap the finished
    // thread and retry calmly.
    if (!m_impl->running) {
        if (m_impl->finished && m_impl->worker.joinable()) {
            m_impl->worker.join();
        }
        if (!m_impl->worker.joinable()) {
            m_restartClock += std::max(dt, 0.f);
            if (m_restartClock >= kRestartRetrySeconds) start();
        }
    } else {
        m_restartClock = 0.f;
    }
#endif

    m_refreshClock += dt;
    if (m_refreshClock < kRefreshSeconds) return;
    m_refreshClock = 0.f;
    refreshSnapshot();
}

void StreamOverlayServer::refreshSnapshot() {
    if (!m_initialized || !paimon::modules::isEnabled(kModuleID)) return;

    auto& manager = TwitchRequestManager::get();
    auto& cache = TwitchLevelBriefCache::get();
    cache.tick();
    auto const requests = manager.requests();

    matjson::Value playing = matjson::makeObject({
        {"active", false}, {"id", 0}, {"name", ""}, {"author", ""},
        {"requester", ""}, {"percent", 0}, {"platform", ""},
    });

    if (auto* play = PlayLayer::get(); play && play->m_level) {
        auto* level = play->m_level;
        int const currentID = level->m_levelID.value();
        std::string requester;
        std::string platform;
        if (auto index = indexOfRequest(currentID); index && *index < requests.size()) {
            requester = requests[*index].requester;
            platform = platformKey(requests[*index].platform);
        }
        LevelBrief const* brief = nullptr;
        if (currentID > 0) {
            brief = cache.peek(currentID);
            if (!brief) cache.request(currentID);
        }
        playing = briefJson(brief);
        playing["active"] = true;
        playing["id"] = currentID;
        playing["name"] = std::string(level->m_levelName);
        playing["author"] = std::string(level->m_creatorName);
        playing["requester"] = requester;
        playing["platform"] = platform;
        playing["percent"] = play->m_hasCompletedLevel ? 100 : std::clamp(
            static_cast<int>(std::floor(play->getCurrentPercent())), 0, 99);
        playing["best"] = std::clamp(level->m_normalPercent.value(), 0, 100);
        playing["attempts"] = play->m_attempts;
        playing["practice"] = play->m_isPracticeMode;
        playing["platformer"] = level->isPlatformer();
    }

    std::array<LevelRequest const*, kMaxNextCount> queueRequests{};
    size_t queueRequestCount = 0;
    size_t pendingCount = 0;
    LevelRequest const* latestRequest = nullptr;
    auto requestJson = [&cache](LevelRequest const& request) {
        auto const* brief = cache.peek(request.levelID);
        if (!brief) cache.request(request.levelID);
        auto item = briefJson(brief);
        item["entry"] = request.entryID;
        item["id"] = request.levelID;
        item["requester"] = request.requester;
        item["platform"] = platformKey(request.platform);
        item["queue"] = request.queue;
        item["description"] = requestNote(request);
        item["receivedAt"] = request.receivedAt;
        return item;
    };
    for (auto const& request : requests) {
        if (!manager.inSelectedQueue(request)) continue;
        if (request.played) continue;
        if (auto passes = requestPasses(request.levelID, !request.videoUrl.empty()); passes && !*passes) continue;
        ++pendingCount;
        if (queueRequestCount < static_cast<size_t>(g_config.nextCount)) {
            queueRequests[queueRequestCount++] = &request;
        }
        if (!latestRequest || request.entryID > latestRequest->entryID) {
            latestRequest = &request;
        }
    }

    auto queue = matjson::Value::array();
    matjson::Value latest = matjson::makeObject({{"entry", 0}});
    matjson::Value latestBuilt;
    bool haveLatest = false;
    for (size_t i = 0; i < queueRequestCount; ++i) {
        auto const* request = queueRequests[i];
        auto item = requestJson(*request);
        if (latestRequest && request->entryID == latestRequest->entryID) {
            latestBuilt = item;
            haveLatest = true;
        }
        queue.push(std::move(item));
    }
    if (latestRequest) latest = haveLatest ? std::move(latestBuilt) : requestJson(*latestRequest);

    auto const& session = manager.sessionStats();
    auto payload = matjson::makeObject({
        {"revision", static_cast<int64_t>(++m_revision)},
        {"serverTime", unixNow()},
        {"playing", playing},
        {"queue", queue},
        {"latest", latest},
        {"queueName", manager.selectedQueue()},
        {"pending", static_cast<int64_t>(pendingCount)},
        {"random", manager.isRandomOrder()},
        {"accepting", manager.isAccepting()},
        {"stats", matjson::makeObject({
            {"received", session.received},
            {"played", session.played},
            {"averageWait", static_cast<int64_t>(std::round(manager.averageWaitSeconds()))},
        })},
        {"config", configJson(g_config)},
    }).dump(matjson::NO_INDENTATION);

    std::lock_guard lock(m_impl->mutex);
    m_impl->payload = std::move(payload);
}

bool StreamOverlayServer::isRunning() const {
    return m_impl->running;
}

std::string StreamOverlayServer::statusText() const {
    if (!supported()) return "Solo disponible en Windows y macOS";
    std::lock_guard lock(m_impl->mutex);
    return m_impl->status;
}

std::string StreamOverlayServer::overlayUrl() const {
    return urlFor("/overlay");
}

std::string StreamOverlayServer::previewUrl() const {
    return urlFor("/preview");
}

std::string StreamOverlayServer::galleryUrl() const {
    return urlFor("/gallery");
}

} // namespace paimon::twitch
