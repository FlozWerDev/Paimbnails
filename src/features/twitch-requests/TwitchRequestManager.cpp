#include "TwitchRequestManager.hpp"

#include "TwitchRequestFilters.hpp"
#include "TwitchRequestNotify.hpp"
#include "TwitchRequestParser.hpp"
#include "services/TwitchLevelBriefCache.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../utils/MainThreadDelay.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <random>
#include <ranges>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr char const* kQueueKey = "twitch-requests-queue";
constexpr char const* kAcceptingKey = "twitch-requests-accepting";
constexpr char const* kLiveKey = "twitch-requests-live";
constexpr char const* kRandomKey = "twitch-requests-random";
constexpr char const* kSelectedKey = "twitch-requests-selected";
constexpr char const* kRoutingKey = "twitch-requests-routing";
constexpr char const* kSelectedQueueKey = "twitch-requests-selected-queue";
constexpr char const* kRecentEventsKey = "twitch-requests-recent-events";
constexpr char const* kNextEntryKey = "twitch-requests-next-entry-id";
constexpr char const* kFilterModeKey = "twitch-requests-filter-mode";
constexpr char const* kFilterDifficultiesKey = "twitch-requests-filter-difficulties";
constexpr char const* kFilterLengthsKey = "twitch-requests-filter-lengths";
constexpr char const* kFilterVerifiedKey = "twitch-requests-filter-verified";
constexpr char const* kFilterDuplicatesKey = "twitch-requests-filter-duplicates";
constexpr char const* kFilterMaxPerUserKey = "twitch-requests-filter-max-per-user";
constexpr char const* kFilterCooldownKey = "twitch-requests-filter-cooldown";
constexpr char const* kFilterVideoRulesKey = "twitch-requests-filter-video-rules";
constexpr int kMaxPerUserLimit = 20;
constexpr int kMaxCooldownSeconds = 300;

// preserve twitch's original setting key.
char const* channelSettingKey(Platform platform) {
    switch (platform) {
        case Platform::YouTube: return "twitch-requests-youtube-channel";
        case Platform::Kick: return "twitch-requests-kick-channel";
        case Platform::TikTok: return "twitch-requests-tiktok-channel";
        default: return "twitch-requests-channel";
    }
}

int64_t unixTime() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string trimCopy(std::string value) {
    auto first = std::ranges::find_if(value, [](unsigned char ch) {
        return !std::isspace(ch);
    });
    value.erase(value.begin(), first);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

// trim utf-8 by code point so saved queue json stays valid.
void clampUtf8(std::string& text, size_t limit) {
    if (text.size() <= limit) return;
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    text.resize(cut);
}

// migrate legacy channel values into per-platform settings.
void migrateChannels() {
    for (int index = 0; index < kPlatformCount; ++index) {
        auto platform = platformFromIndex(index);
        auto const* key = channelSettingKey(platform);
        if (!Mod::get()->hasSetting(key)) continue;
        if (!trimCopy(Mod::get()->getSettingValue<std::string>(key)).empty()) continue;

        auto remembered = Mod::get()->getSavedValue<std::string>(
            std::string("requests-channel-") + platformKey(platform), std::string{});
        if (!remembered.empty()) Mod::get()->setSettingValue<std::string>(key, remembered);
    }
}

int savedFilter(char const* key, int count) {
    auto value = Mod::get()->getSavedValue<int64_t>(key, 0);
    return static_cast<int>(std::clamp<int64_t>(value, 0, count - 1));
}

uint32_t savedMask(char const* key, uint32_t allMask) {
    auto value = Mod::get()->getSavedValue<int64_t>(key, static_cast<int64_t>(allMask));
    uint32_t mask = static_cast<uint32_t>(value) & allMask;
    return mask == 0 ? allMask : mask;
}

int savedLimit(char const* key, int max) {
    auto value = Mod::get()->getSavedValue<int64_t>(key, 0);
    return static_cast<int>(std::clamp<int64_t>(value, 0, max));
}

std::string requesterKey(Platform platform, std::string const& requester) {
    std::string key = platformKey(platform);
    key += ':';
    for (unsigned char ch : requester) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        key += static_cast<char>(ch);
    }
    return key;
}

std::string userQueueKey(Platform platform, std::string const& requester,
    std::string const& userID, std::string const& queue) {
    if (!userID.empty()) return queue + '\n' + platformKey(platform) + ":id:" + userID;
    return queue + '\n' + requesterKey(platform, "name:" + requester);
}

std::string intakeStatus(std::string const& error, std::string const& queue) {
    if (error.empty()) return "Guardado en " + queue;
    if (error == "paused") return "Requests cerrados";
    if (error == "duplicate") return "Ese nivel ya esta en " + queue;
    if (error == "full") return "Almacenamiento de requests lleno";
    if (error == "user-limit") return "Limite de pedidos por usuario en " + queue;
    if (error == "cooldown") return "El usuario debe esperar para pedir en " + queue;
    if (error == "unverified") return "El filtro solo acepta usuarios GD verificados";
    return "El nivel no pasa los filtros";
}

// count only requests known to fail the filters.
bool filteredOut(LevelRequest const& request) {
    auto passes = requestPasses(request.levelID, !request.videoUrl.empty());
    return passes && !*passes;
}

}

std::string requestNote(LevelRequest const& request) {
    if (!request.description.empty()) return trimCopy(request.description);
    if (request.platform != Platform::Web) return {};
    if (request.message == fmt::format("Web request: {}", request.levelID)) return {};
    return trimCopy(request.message);
}

TwitchRequestManager& TwitchRequestManager::get() {
    static TwitchRequestManager instance;
    return instance;
}

// web has no channel or chatsource; clamp accidental web indexing.
TwitchRequestManager::Link& TwitchRequestManager::link(Platform platform) {
    return m_links[std::min<size_t>(static_cast<size_t>(platform), kPlatformCount - 1)];
}

TwitchRequestManager::Link const& TwitchRequestManager::link(Platform platform) const {
    return m_links[std::min<size_t>(static_cast<size_t>(platform), kPlatformCount - 1)];
}

void TwitchRequestManager::init() {
    if (m_initialized) return;
    // run before going live so setting listeners remain no-ops.
    migrateChannels();

    m_initialized = true;
    m_shuttingDown = false;
    m_accepting = Mod::get()->getSavedValue<bool>(kAcceptingKey, true);
    m_live = Mod::get()->getSavedValue<bool>(kLiveKey, true);
    m_randomOrder = Mod::get()->getSavedValue<bool>(kRandomKey, false);
    m_selected = platformFromKey(
        Mod::get()->getSavedValue<std::string>(kSelectedKey, std::string("twitch")));
    m_filters.mode = static_cast<ModeFilter>(savedFilter(kFilterModeKey, kModeFilterCount));
    m_filters.difficulties = savedMask(kFilterDifficultiesKey, kAllDifficulties);
    m_filters.lengths = savedMask(kFilterLengthsKey, kAllLengths);
    m_filters.verifiedOnly = Mod::get()->getSavedValue<bool>(kFilterVerifiedKey, false);
    m_filters.blockDuplicates = Mod::get()->getSavedValue<bool>(kFilterDuplicatesKey, true);
    m_filters.maxPerUser = savedLimit(kFilterMaxPerUserKey, kMaxPerUserLimit);
    m_filters.cooldownSeconds = savedLimit(kFilterCooldownKey, kMaxCooldownSeconds);

    auto videoRulesJson = Mod::get()->getSavedValue<matjson::Value>(
        kFilterVideoRulesKey, matjson::Value::array());
    m_filters.videoRules.clear();
    if (videoRulesJson.isArray()) {
        for (auto const& item : videoRulesJson.asArray().unwrap()) {
            if (!item.isObject()) continue;
            VideoRequirementRule rule;
            if (item.contains("mode") && item["mode"].isNumber()) {
                rule.mode = static_cast<ModeFilter>(
                    std::clamp(static_cast<int>(item["mode"].asInt().unwrapOr(0)), 0, kModeFilterCount - 1));
            }
            if (item.contains("difficulties") && item["difficulties"].isNumber()) {
                rule.difficulties = static_cast<uint32_t>(
                    item["difficulties"].asInt().unwrapOr(static_cast<int>(kAllDifficulties))) & kAllDifficulties;
            }
            if (rule.difficulties == 0) rule.difficulties = kAllDifficulties;
            m_filters.videoRules.push_back(rule);
        }
    }
    loadNotifyConfig();
    loadRouting();
    loadQueue();
    ++m_monitorGeneration;
    scheduleMonitor();
    restart();
}

void TwitchRequestManager::shutdown() {
    if (!m_initialized) return;
    m_shuttingDown = true;
    ++m_monitorGeneration;
    for (int index = 0; index < kPlatformCount; ++index) {
        stopLink(platformFromIndex(index));
    }
    stopWebRequests();
    saveQueue();
    // a later load in the same process must init everything again.
    m_initialized = false;
}

ConnectionState TwitchRequestManager::state(Platform platform) const {
    if (platform == Platform::Web) return m_webState;
    return link(platform).state;
}

std::string const& TwitchRequestManager::statusText(Platform platform) const {
    if (platform == Platform::Web) return m_webStatus;
    return link(platform).statusText;
}

std::string const& TwitchRequestManager::channel(Platform platform) const {
    if (platform == Platform::Web) return m_webUser;
    return link(platform).channel;
}

// web is active when enabled; it has no channel setting.
bool TwitchRequestManager::isActive(Platform platform) const {
    if (platform == Platform::Web) return webEnabled();
    return !link(platform).channel.empty();
}

size_t TwitchRequestManager::activeCount() const {
    auto count = static_cast<size_t>(std::ranges::count_if(m_links, [](Link const& entry) {
        return !entry.channel.empty();
    }));
    return count + (webEnabled() ? 1 : 0);
}

size_t TwitchRequestManager::connectedCount() const {
    auto count = static_cast<size_t>(std::ranges::count_if(m_links, [](Link const& entry) {
        return entry.state == ConnectionState::Connected;
    }));
    return count + (m_webState == ConnectionState::Connected ? 1 : 0);
}

bool TwitchRequestManager::webEnabled() const {
    return paimon::modules::isEnabled("paimbnails.webrequests.menu");
}

// writing the setting triggers its listener and restarts the source.
void TwitchRequestManager::setWebEnabled(bool enabled) {
    if (webEnabled() == enabled) return;
    Mod::get()->setSettingValue<bool>("twitch-requests-web-enabled", enabled);
}

std::string TwitchRequestManager::webUrl() const {
    if (m_webUser.empty()) return {};
    return "flozwer.org/request/" + m_webUser;
}

void TwitchRequestManager::setWebState(ConnectionState state, std::string text) {
    m_webState = state;
    m_webStatus = std::move(text);
}

void TwitchRequestManager::select(Platform platform) {
    if (m_selected == platform) return;
    m_selected = platform;
    Mod::get()->setSavedValue<std::string>(kSelectedKey, platformKey(platform));
    paimon::requestDeferredModSave();
}

void TwitchRequestManager::setState(
    Platform platform,
    ConnectionState state,
    std::string text
) {
    auto& entry = link(platform);
    entry.state = state;
    entry.statusText = std::move(text);
}

std::string TwitchRequestManager::channelSetting(Platform platform) const {
    // web's channel is the gd account and is not user-entered.
    if (platform == Platform::Web) return m_webUser;
    auto const* key = channelSettingKey(platform);
    if (!Mod::get()->hasSetting(key)) return {};
    return trimCopy(Mod::get()->getSettingValue<std::string>(key));
}

std::string TwitchRequestManager::commandsSetting() const {
    return trimCopy(Mod::get()->getSettingValue<std::string>("twitch-requests-commands"));
}

void TwitchRequestManager::setChannelSetting(Platform platform, std::string value) {
    if (platform == Platform::Web) return;
    value = trimCopy(std::move(value));
    if (value == channelSetting(platform)) return;
    // writing the setting restarts that platform through its listener.
    Mod::get()->setSettingValue<std::string>(channelSettingKey(platform), value);
}

void TwitchRequestManager::setCommandsSetting(std::string value) {
    value = trimCopy(std::move(value));
    if (value.empty()) value = "!req";
    if (value == commandsSetting()) return;
    Mod::get()->setSettingValue<std::string>("twitch-requests-commands", value);
}

std::string TwitchRequestManager::commandsSetting(Platform platform) const {
    return m_routing.platforms[static_cast<size_t>(platform)].commands;
}

void TwitchRequestManager::setCommandsSetting(Platform platform, std::string value) {
    value = trimCopy(std::move(value));
    clampUtf8(value, 400);
    auto& commands = m_routing.platforms[static_cast<size_t>(platform)].commands;
    if (commands == value) return;
    commands = std::move(value);
    saveRouting();
}

void TwitchRequestManager::setRouting(RequestRoutingConfig config) {
    for (auto& platform : config.platforms) {
        platform.commands = trimCopy(std::move(platform.commands));
        clampUtf8(platform.commands, 400);
        platform.queue = normalizeQueueName(platform.queue);
    }
    std::vector<RequestRoute> routes;
    for (auto route : config.routes) {
        if (!normalizeRoute(route)) continue;
        auto existing = std::ranges::find_if(routes, [&route](RequestRoute const& other) {
            return other.platform == route.platform && other.reward == route.reward && other.key == route.key;
        });
        if (existing != routes.end()) *existing = std::move(route);
        else if (routes.size() < 64) routes.push_back(std::move(route));
    }
    config.routes = std::move(routes);
    m_routing = std::move(config);
    saveRouting();
    ++m_queueRevision;
}

void TwitchRequestManager::loadRouting() {
    m_routing = {};
    auto saved = Mod::get()->getSavedValue<matjson::Value>(kRoutingKey, matjson::Value{});
    if (saved.isObject()) {
        m_routing.pointsEnabled = saved["pointsEnabled"].asBool().unwrapOr(false);
        for (int index = 0; index < kSelectableCount; ++index) {
            auto item = saved["platforms"][platformKey(platformFromIndex(index))];
            auto& config = m_routing.platforms[index];
            config.commandsEnabled = item["commandsEnabled"].asBool().unwrapOr(true);
            config.commands = item["commands"].asString().unwrapOr("");
            config.queue = normalizeQueueName(item["queue"].asString().unwrapOr("General"));
        }
        if (auto routes = saved["routes"].asArray(); routes) {
            for (auto const& item : routes.unwrap()) {
                RequestRoute route;
                route.platform = item["platform"].asString().unwrapOr("*");
                route.reward = item["reward"].asBool().unwrapOr(false);
                route.key = item["key"].asString().unwrapOr("");
                route.name = item["name"].asString().unwrapOr("");
                route.queue = item["queue"].asString().unwrapOr("General");
                if (normalizeRoute(route)) m_routing.routes.push_back(std::move(route));
                if (m_routing.routes.size() >= 64) break;
            }
        }
    }
    m_selectedQueue = Mod::get()->getSavedValue<std::string>(kSelectedQueueKey, "");
    if (!m_selectedQueue.empty()) m_selectedQueue = normalizeQueueName(m_selectedQueue);
    m_lastRewardID.clear();
    m_lastRewardText.clear();
    m_lastIntakeStatus.clear();
}

void TwitchRequestManager::saveRouting() {
    auto platforms = matjson::makeObject({});
    for (int index = 0; index < kSelectableCount; ++index) {
        auto const& config = m_routing.platforms[index];
        platforms[platformKey(platformFromIndex(index))] = matjson::makeObject({
            {"commandsEnabled", config.commandsEnabled}, {"commands", config.commands}, {"queue", config.queue},
        });
    }
    auto routes = matjson::Value::array();
    for (auto const& route : m_routing.routes) {
        routes.push(matjson::makeObject({
            {"platform", route.platform}, {"reward", route.reward}, {"key", route.key},
            {"name", route.name}, {"queue", route.queue},
        }));
    }
    Mod::get()->setSavedValue(kRoutingKey, matjson::makeObject({
        {"platforms", platforms}, {"pointsEnabled", m_routing.pointsEnabled}, {"routes", routes},
    }));
    paimon::requestDeferredModSave();
}

void TwitchRequestManager::selectQueue(std::string queue) {
    if (!queue.empty()) queue = normalizeQueueName(queue);
    if (m_selectedQueue == queue) return;
    m_selectedQueue = std::move(queue);
    Mod::get()->setSavedValue(kSelectedQueueKey, m_selectedQueue);
    paimon::requestDeferredModSave();
    ++m_queueRevision;
}

std::vector<std::string> TwitchRequestManager::queueNames() const {
    std::vector<std::string> names{"General"};
    auto add = [&names](std::string const& name) {
        if (std::ranges::find(names, name) == names.end()) names.push_back(name);
    };
    for (auto const& platform : m_routing.platforms) add(platform.queue);
    for (auto const& route : m_routing.routes) add(route.queue);
    for (auto const& request : m_requests) add(request.queue);
    if (!m_selectedQueue.empty()) add(m_selectedQueue);
    return names;
}

bool TwitchRequestManager::inSelectedQueue(LevelRequest const& request) const {
    return m_selectedQueue.empty() || request.queue == m_selectedQueue;
}

size_t TwitchRequestManager::selectedRequestCount() const {
    return static_cast<size_t>(std::ranges::count_if(m_requests,
        [this](LevelRequest const& request) { return inSelectedQueue(request); }));
}

bool TwitchRequestManager::rememberEvent(std::string const& key) {
    if (key.empty()) return true;
    if (!m_seenEvents.insert(key).second) return false;
    m_recentEvents.push_back(key);
    while (m_recentEvents.size() > 512) {
        m_seenEvents.erase(m_recentEvents.front());
        m_recentEvents.pop_front();
    }
    return true;
}

void TwitchRequestManager::setLive(bool live) {
    if (m_live == live) return;
    m_live = live;
    Mod::get()->setSavedValue<bool>(kLiveKey, live);
    paimon::requestDeferredModSave();
    restart();
}

void TwitchRequestManager::restart() {
    for (int index = 0; index < kPlatformCount; ++index) {
        restart(platformFromIndex(index));
    }
    restartWebRequests();
}

void TwitchRequestManager::restart(Platform platform) {
    if (!m_initialized || m_shuttingDown) return;
    if (platform == Platform::Web) {
        restartWebRequests();
        return;
    }

    stopLink(platform);
    link(platform).reconnectDelay = 3;

    if (!Mod::get()->getSettingValue<bool>("twitch-requests-enabled")) {
        setState(platform, ConnectionState::Disabled, "Desactivado en los ajustes del mod");
        return;
    }

    auto& entry = link(platform);
    entry.channel = normalizeChannel(platform, channelSetting(platform));
    if (entry.channel.empty()) {
        setState(platform, ConnectionState::NeedsChannel,
            fmt::format("Sin canal de {}", platformName(platform)));
        return;
    }
    if (!m_live) {
        setState(platform, ConnectionState::Offline, "Pausado; toca Conectar para escuchar");
        return;
    }
    connectLink(platform);
}

void TwitchRequestManager::connectLink(Platform platform) {
    stopLink(platform);

    auto& entry = link(platform);
    auto const label = channelLabel(platform, entry.channel);
    setState(platform, ConnectionState::Connecting, "Conectando a " + label + "...");

    auto const generation = entry.generation;
    ChatCallbacks callbacks;
    callbacks.onStatus = [this, platform, generation](std::string text) {
        if (generation != link(platform).generation || m_shuttingDown) return;
        setState(platform, ConnectionState::Connecting, std::move(text));
    };
    callbacks.onReady = [this, platform, generation](std::string text) {
        if (generation != link(platform).generation || m_shuttingDown) return;
        auto& current = link(platform);
        current.reconnectScheduled = false;
        current.reconnectDelay = 3;
        setState(platform, ConnectionState::Connected, std::move(text));
    };
    callbacks.onMessage = [this, platform, generation](ChatMessage message) {
        if (generation != link(platform).generation || m_shuttingDown) return;
        addRequest(platform, std::move(message));
    };
    callbacks.onError = [this, platform, generation](std::string error) {
        handleError(platform, generation, std::move(error));
    };

    entry.source = makeChatSource(platform, entry.channel, std::move(callbacks));
    if (!entry.source) {
        setState(platform, ConnectionState::Error, "No pudimos abrir el chat");
        return;
    }
    entry.source->start();
}

void TwitchRequestManager::stopLink(Platform platform) {
    auto& entry = link(platform);
    ++entry.generation;
    entry.reconnectScheduled = false;
    if (entry.source) {
        entry.source->stop();
        entry.source.reset();
    }
}

void TwitchRequestManager::restartWebRequests() {
    if (!m_initialized || m_shuttingDown) return;
    stopWebRequests();
    m_webReconnectDelay = 3;

    if (!webEnabled()) {
        setWebState(ConnectionState::Disabled, "Pagina apagada; toca Activar pagina");
        return;
    }
    if (!WebRequestSource::supported()) {
        setWebState(ConnectionState::Error, "La pagina de requests solo funciona en Windows");
        return;
    }
    if (!m_live) {
        setWebState(ConnectionState::Offline, "Pausado; toca Conectar para abrir la pagina");
        return;
    }
    connectWebRequests();
}

void TwitchRequestManager::connectWebRequests() {
    stopWebRequests();
    setWebState(ConnectionState::Connecting, "Abriendo tu pagina de requests...");
    auto const generation = m_webGeneration;

    WebRequestCallbacks callbacks;
    callbacks.onStatus = [this, generation](std::string text) {
        if (generation != m_webGeneration || m_shuttingDown) return;
        log::debug("[WebRequests] {}", text);
        setWebState(ConnectionState::Connecting, std::move(text));
    };
    callbacks.onReady = [this, generation](std::string user) {
        if (generation != m_webGeneration || m_shuttingDown) return;
        m_webReconnectScheduled = false;
        m_webReconnectDelay = 3;
        m_webUser = std::move(user);
        setWebState(ConnectionState::Connected, "Tu pagina: " + webUrl());
    };
    callbacks.onRequest = [this, generation](WebRequest incoming) {
        if (generation != m_webGeneration || m_shuttingDown) return std::string("disabled");
        return addWebRequest(std::move(incoming));
    };
    callbacks.onError = [this, generation](std::string error) {
        if (generation != m_webGeneration || m_shuttingDown) return;
        log::warn("[WebRequests] {}", error);
        setWebState(ConnectionState::Error,
            error.empty() ? "La pagina se desconecto; reintentando..." : std::move(error));
        scheduleWebReconnect();
    };
    m_webSource = std::make_unique<WebRequestSource>(std::move(callbacks));
    m_webSource->start();
}

void TwitchRequestManager::stopWebRequests() {
    ++m_webGeneration;
    m_webReconnectScheduled = false;
    if (m_webSource) {
        m_webSource->stop();
        m_webSource.reset();
    }
}

void TwitchRequestManager::scheduleWebReconnect() {
    if (m_webReconnectScheduled || m_shuttingDown || !m_live) return;
    if (!webEnabled()) return;

    m_webReconnectScheduled = true;
    int const delay = m_webReconnectDelay;
    m_webReconnectDelay = std::min(m_webReconnectDelay * 2, 60);
    auto const generation = m_webGeneration;
    paimon::scheduleMainThreadDelay(static_cast<float>(delay), [this, generation]() {
        if (generation != m_webGeneration || m_shuttingDown) return;
        m_webReconnectScheduled = false;
        connectWebRequests();
    });
}

// retry keeps the same channel; going live later can pick it up.
void TwitchRequestManager::handleError(
    Platform platform,
    uint64_t generation,
    std::string error
) {
    if (generation != link(platform).generation || m_shuttingDown) return;
    setState(platform, ConnectionState::Error,
        error.empty() ? "Chat desconectado; reintentando..." : std::move(error));
    scheduleReconnect(platform);
}

void TwitchRequestManager::scheduleReconnect(Platform platform) {
    auto& entry = link(platform);
    if (entry.reconnectScheduled || m_shuttingDown || !m_live) return;
    if (entry.channel.empty()) return;
    if (!Mod::get()->getSettingValue<bool>("twitch-requests-enabled")) return;

    entry.reconnectScheduled = true;
    int const delay = entry.reconnectDelay;
    entry.reconnectDelay = std::min(entry.reconnectDelay * 2, 60);
    auto const generation = entry.generation;
    paimon::scheduleMainThreadDelay(static_cast<float>(delay), [this, platform, generation]() {
        auto& current = link(platform);
        if (generation != current.generation || m_shuttingDown) return;
        current.reconnectScheduled = false;
        connectLink(platform);
    });
}

void TwitchRequestManager::scheduleMonitor() {
    uint64_t generation = m_monitorGeneration;
    paimon::scheduleMainThreadDelay(4.f, [this, generation]() {
        if (generation != m_monitorGeneration || m_shuttingDown) return;
        monitor();
        scheduleMonitor();
    });
}

void TwitchRequestManager::monitor() {
    if (!m_initialized || m_shuttingDown || !m_live) return;

    // requestpasses() queues lookups with no ui open: drain it so rules
    // still apply in the background.
    TwitchLevelBriefCache::get().tick();

    for (int index = 0; index < kPlatformCount; ++index) {
        auto platform = platformFromIndex(index);
        auto& entry = link(platform);
        if (entry.state == ConnectionState::Connected
            && entry.source
            && !entry.source->isOpen()) {
            scheduleReconnect(platform);
        }
    }
    if (m_webState == ConnectionState::Connected
        && m_webSource
        && !m_webSource->isOpen()) {
        scheduleWebReconnect();
    }
}

void TwitchRequestManager::addRequest(
    Platform platform,
    ChatMessage incoming
) {
    auto const& config = m_routing.platforms[static_cast<size_t>(platform)];
    auto const commands = routedCommands(m_routing, platform, commandsSetting());
    std::optional<ParsedRequest> parsed;
    RequestRoute const* route = nullptr;
    if (!incoming.rewardID.empty()) {
        if (platform != Platform::Twitch) return;
        incoming.rewardID = normalizeRewardID(incoming.rewardID);
        if (incoming.rewardID.empty()) return;
        m_lastRewardID = incoming.rewardID;
        ++m_rewardDetectionRevision;
        m_lastRewardText = incoming.text;
        clampUtf8(m_lastRewardText, 300);
        if (!m_routing.pointsEnabled) {
            m_lastIntakeStatus = "Canje detectado; activa puntos y vincula su destino";
            return;
        }
        route = findRequestRoute(m_routing, platform, incoming.rewardID, true);
        if (!route) {
            m_lastIntakeStatus = "Canje sin vincular; elige una cola en Origenes";
            return;
        }
        parsed = parseRequest(incoming.text, commands);
        if (!parsed) parsed = parseRequestBody(incoming.text);
    } else {
        if (!config.commandsEnabled) return;
        parsed = parseRequest(incoming.text, commands);
        if (parsed) route = findRequestRoute(m_routing, platform, parsed->command, false);
    }
    if (!parsed) {
        if (!incoming.rewardID.empty()) m_lastIntakeStatus = "Canje invalido: escribe ID descripcion";
        return;
    }
    auto const queue = route ? route->queue : config.queue;
    auto const sourceName = route && !route->name.empty()
        ? route->name : (incoming.rewardID.empty() ? parsed->command : "Canje de puntos");
    if (!incoming.messageID.empty()) {
        incoming.messageID = std::string(platformKey(platform)) + ':'
            + channel(platform) + ':' + incoming.messageID;
        clampUtf8(incoming.messageID, 300);
        if (std::ranges::any_of(m_requests, [&incoming](LevelRequest const& request) {
            return request.eventID == incoming.messageID;
        })) return;
        if (!rememberEvent(incoming.messageID)) return;
    }
    auto requester = std::move(incoming.requester);
    auto message = std::move(incoming.text);
    auto error = enqueueRequest(platform, std::move(requester), std::move(message),
        std::move(*parsed), false, {}, 0, queue, std::move(incoming), sourceName);
    m_lastIntakeStatus = intakeStatus(error, queue);
}

std::string TwitchRequestManager::addWebRequest(WebRequest incoming) {
    if (incoming.levelID <= 0) return "invalid";
    ParsedRequest parsed;
    parsed.levelID = incoming.levelID;
    parsed.command = "web";
    parsed.url = std::move(incoming.video);
    parsed.description = incoming.message;
    ChatMessage metadata;
    if (incoming.requesterVerified && incoming.requesterAccountID > 0) {
        metadata.userID = std::to_string(incoming.requesterAccountID);
    }
    if (!incoming.requestID.empty()) {
        metadata.messageID = "web:" + incoming.requestID;
        if (m_seenEvents.contains(metadata.messageID)) return {};
        if (std::ranges::any_of(m_requests, [&incoming](LevelRequest const& request) {
            return request.platform == Platform::Web && request.webRequestID == incoming.requestID;
        })) return {};
    }
    return enqueueRequest(
        Platform::Web,
        std::move(incoming.requester),
        std::move(incoming.message),
        std::move(parsed),
        incoming.requesterVerified,
        std::move(incoming.requestID),
        incoming.requesterAccountID,
        m_routing.platforms[static_cast<size_t>(Platform::Web)].queue,
        std::move(metadata),
        "Pagina web"
    );
}

std::string TwitchRequestManager::enqueueRequest(
    Platform platform,
    std::string requester,
    std::string message,
    ParsedRequest parsed,
    bool requesterVerified,
    std::string webRequestID,
    int requesterAccountID,
    std::string queue,
    ChatMessage metadata,
    std::string sourceName
) {
    if (!m_accepting) return "paused";
    if (parsed.levelID <= 0) return "invalid";
    queue = normalizeQueueName(queue);

    requester = trimCopy(std::move(requester));
    if (requester.empty()) requester = platformName(platform);
    clampUtf8(requester, 64);
    clampUtf8(message, 1500);
    clampUtf8(parsed.description, 1000);
    clampUtf8(metadata.userID, 128);
    clampUtf8(metadata.messageID, 300);
    parsed.url = trimCopy(std::move(parsed.url));
    if (!isValidVideoUrl(parsed.url)) parsed.url.clear();

    if (m_filters.verifiedOnly && !requesterVerified) return "unverified";
    if (m_filters.blockDuplicates
        && std::ranges::any_of(m_requests, [id = parsed.levelID, &queue](LevelRequest const& request) {
            return request.queue == queue && request.levelID == id;
        })) {
        return "duplicate";
    }

    auto const userKey = userQueueKey(platform, requester, metadata.userID, queue);
    if (m_filters.maxPerUser > 0) {
        int const pending = static_cast<int>(std::ranges::count_if(
            m_requests, [&userKey](LevelRequest const& request) {
                return !request.played
                    && userQueueKey(request.platform, request.requester, request.userID, request.queue) == userKey;
            }));
        if (pending >= m_filters.maxPerUser) return "user-limit";
    }

    int64_t const now = unixTime();
    if (m_filters.cooldownSeconds > 0) {
        auto last = m_lastRequestAt.find(userKey);
        if (last != m_lastRequestAt.end()
            && now - last->second < m_filters.cooldownSeconds) {
            return "cooldown";
        }
    }

    if (static_cast<int>(m_requests.size()) >= maxQueueSize()) return "full";
    // known levels that fail filters never enter the queue.
    if (auto passes = requestPasses(parsed.levelID, !parsed.url.empty()); passes && !*passes) return "filtered";

    LevelRequest request;
    request.entryID = m_nextEntryID++;
    request.levelID = parsed.levelID;
    request.webRequestID = std::move(webRequestID);
    request.requesterAccountID = requesterVerified ? requesterAccountID : 0;
    request.requester = std::move(requester);
    request.requesterVerified = requesterVerified;
    request.message = std::move(message);
    request.description = std::move(parsed.description);
    request.queue = std::move(queue);
    request.command = std::move(parsed.command);
    request.rewardID = std::move(metadata.rewardID);
    request.sourceName = std::move(sourceName);
    request.eventID = std::move(metadata.messageID);
    request.userID = std::move(metadata.userID);
    request.receivedAt = now;
    request.videoUrl = std::move(parsed.url);
    request.platform = platform;
    rememberEvent(request.eventID);
    m_requests.push_back(std::move(request));
    m_lastRequestAt[userKey] = now;
    ++m_queueRevision;
    saveQueue();
    showRequestNotify(m_requests.back());
    return {};
}

void TwitchRequestManager::setAccepting(bool accepting) {
    m_accepting = accepting;
    Mod::get()->setSavedValue<bool>(kAcceptingKey, accepting);
    paimon::requestDeferredModSave();
}

void TwitchRequestManager::setRandomOrder(bool random) {
    if (m_randomOrder == random) return;
    m_randomOrder = random;
    Mod::get()->setSavedValue<bool>(kRandomKey, random);
    paimon::requestDeferredModSave();
}

void TwitchRequestManager::setFilters(RequestFilters filters) {
    filters.mode = static_cast<ModeFilter>(
        std::clamp(static_cast<int>(filters.mode), 0, kModeFilterCount - 1));
    filters.difficulties &= kAllDifficulties;
    filters.lengths &= kAllLengths;
    filters.maxPerUser = std::clamp(filters.maxPerUser, 0, kMaxPerUserLimit);
    filters.cooldownSeconds = std::clamp(
        filters.cooldownSeconds, 0, kMaxCooldownSeconds);
    for (auto& rule : filters.videoRules) {
        rule.mode = static_cast<ModeFilter>(
            std::clamp(static_cast<int>(rule.mode), 0, kModeFilterCount - 1));
        rule.difficulties &= kAllDifficulties;
        if (rule.difficulties == 0) rule.difficulties = kAllDifficulties;
    }
    m_filters = filters;
    Mod::get()->setSavedValue<int64_t>(kFilterModeKey, static_cast<int64_t>(filters.mode));
    Mod::get()->setSavedValue<int64_t>(
        kFilterDifficultiesKey, static_cast<int64_t>(filters.difficulties & kAllDifficulties));
    Mod::get()->setSavedValue<int64_t>(
        kFilterLengthsKey, static_cast<int64_t>(filters.lengths & kAllLengths));
    Mod::get()->setSavedValue<bool>(kFilterVerifiedKey, filters.verifiedOnly);
    Mod::get()->setSavedValue<bool>(kFilterDuplicatesKey, filters.blockDuplicates);
    Mod::get()->setSavedValue<int64_t>(kFilterMaxPerUserKey, filters.maxPerUser);
    Mod::get()->setSavedValue<int64_t>(kFilterCooldownKey, filters.cooldownSeconds);

    auto videoRulesArray = matjson::Value::array();
    for (auto const& rule : filters.videoRules) {
        videoRulesArray.push(matjson::makeObject({
            {"mode", static_cast<int>(rule.mode)},
            {"difficulties", static_cast<int>(rule.difficulties & kAllDifficulties)},
        }));
    }
    Mod::get()->setSavedValue<matjson::Value>(kFilterVideoRulesKey, videoRulesArray);

    // the list watches queuerevision to rebuild.
    ++m_queueRevision;
    paimon::requestDeferredModSave();
}

size_t TwitchRequestManager::pendingCount() const {
    return static_cast<size_t>(std::ranges::count_if(m_requests,
        [this](LevelRequest const& request) { return inSelectedQueue(request) && !request.played; }));
}

size_t TwitchRequestManager::filteredCount() const {
    if (!m_filters.hasLevelFilters()) return 0;
    return static_cast<size_t>(std::ranges::count_if(m_requests,
        [this](LevelRequest const& request) { return inSelectedQueue(request) && filteredOut(request); }));
}

size_t TwitchRequestManager::removeFiltered() {
    if (!m_filters.hasLevelFilters()) return 0;
    size_t const before = m_requests.size();
    std::erase_if(m_requests,
        [this](LevelRequest const& request) { return inSelectedQueue(request) && filteredOut(request); });

    size_t const removed = before - m_requests.size();
    if (removed > 0) {
        ++m_queueRevision;
        saveQueue();
    }
    return removed;
}

std::optional<size_t> TwitchRequestManager::nextPendingIndex() const {
    std::vector<size_t> pending;
    for (size_t index = 0; index < m_requests.size(); ++index) {
        if (!inSelectedQueue(m_requests[index])) continue;
        if (m_requests[index].played) continue;
        if (m_filters.hasLevelFilters() && filteredOut(m_requests[index])) continue;
        pending.push_back(index);
    }
    if (pending.empty()) return std::nullopt;
    if (!m_randomOrder) return pending.front();

    static std::mt19937 engine(std::random_device{}());
    std::uniform_int_distribution<size_t> pick(0, pending.size() - 1);
    return pending[pick(engine)];
}

void TwitchRequestManager::markPlayed(size_t index, int percent) {
    if (index >= m_requests.size()) return;
    m_requests[index].played = true;
    m_requests[index].percent = std::clamp(percent, 0, 100);
    ++m_queueRevision;
    saveQueue();
}

void TwitchRequestManager::setPercent(size_t index, int percent) {
    if (index >= m_requests.size()) return;
    percent = std::clamp(percent, 0, 100);
    if (m_requests[index].percent == percent) return;
    m_requests[index].percent = percent;
    ++m_queueRevision;
    saveQueue();
}

bool TwitchRequestManager::sendWebFeedback(LevelRequest const& request,
    std::string decision, int percent, std::string note, std::string reason,
    std::string image, std::function<void(bool, std::string)> callback) {
    if (request.platform != Platform::Web || !request.requesterVerified
        || request.requesterAccountID <= 0 || request.webRequestID.empty() || !m_webSource) return false;
    return m_webSource->sendFeedback(request.webRequestID, request.levelID,
        std::move(decision), percent, std::move(note), std::move(reason),
        std::move(image), std::move(callback));
}

void TwitchRequestManager::remove(size_t index) {
    if (index >= m_requests.size()) return;
    m_requests.erase(m_requests.begin() + static_cast<std::ptrdiff_t>(index));
    ++m_queueRevision;
    saveQueue();
}

void TwitchRequestManager::moveToFront(size_t index) {
    if (index == 0 || index >= m_requests.size()) return;
    auto it = m_requests.begin() + static_cast<std::ptrdiff_t>(index);
    std::rotate(m_requests.begin(), it, it + 1);
    ++m_queueRevision;
    saveQueue();
}

void TwitchRequestManager::clear() {
    auto const before = m_requests.size();
    std::erase_if(m_requests, [this](LevelRequest const& request) { return inSelectedQueue(request); });
    if (m_selectedQueue.empty()) m_lastRequestAt.clear();
    else std::erase_if(m_lastRequestAt, [prefix = m_selectedQueue + '\n'](auto const& item) {
        return item.first.starts_with(prefix);
    });
    if (before == m_requests.size()) return;
    ++m_queueRevision;
    saveQueue();
}

int TwitchRequestManager::maxQueueSize() const {
    auto value = Mod::get()->getSettingValue<int64_t>("twitch-requests-max-queue");
    return static_cast<int>(std::clamp<int64_t>(value, 1, 500));
}

void TwitchRequestManager::loadQueue() {
    m_requests.clear();
    m_lastRequestAt.clear();
    m_nextEntryID = std::clamp<int64_t>(Mod::get()->getSavedValue<int64_t>(kNextEntryKey, 1), 1, LLONG_MAX - 1000);
    std::unordered_set<int64_t> entryIDs;
    m_recentEvents.clear();
    m_seenEvents.clear();
    auto events = Mod::get()->getSavedValue<matjson::Value>(kRecentEventsKey, matjson::Value::array());
    if (auto array = events.asArray(); array) {
        for (auto const& event : array.unwrap()) {
            auto key = event.asString().unwrapOr("");
            if (key.size() <= 300) rememberEvent(key);
        }
    }
    auto saved = Mod::get()->getSavedValue<matjson::Value>(kQueueKey, matjson::Value::array());
    auto array = saved.asArray();
    if (!array) return;

    for (auto const& item : array.unwrap()) {
        if (!item.isObject()) continue;
        int64_t levelID = item["levelID"].asInt().unwrapOr(0);
        if (levelID <= 0 || levelID > INT_MAX) continue;
        LevelRequest request;
        request.entryID = item["entryID"].asInt().unwrapOr(0);
        if (request.entryID <= 0 || request.entryID >= LLONG_MAX - 1000 || entryIDs.contains(request.entryID)) {
            request.entryID = m_nextEntryID++;
        }
        entryIDs.insert(request.entryID);
        m_nextEntryID = std::max(m_nextEntryID, request.entryID + 1);
        request.levelID = static_cast<int>(levelID);
        request.webRequestID = item["webRequestID"].asString().unwrapOr("");
        request.requesterAccountID = static_cast<int>(std::clamp<int64_t>(item["requesterAccountID"].asInt().unwrapOr(0), 0, INT_MAX));
        request.requester = item["requester"].asString().unwrapOr("Chat");
        request.requesterVerified = item["requesterVerified"].asBool().unwrapOr(false);
        request.message = item["message"].asString().unwrapOr("");
        request.description = item["description"].asString().unwrapOr("");
        request.queue = normalizeQueueName(item["queue"].asString().unwrapOr("General"));
        request.command = item["command"].asString().unwrapOr("");
        request.rewardID = normalizeRewardID(item["rewardID"].asString().unwrapOr(""));
        request.sourceName = item["sourceName"].asString().unwrapOr("");
        request.eventID = item["eventID"].asString().unwrapOr("");
        request.userID = item["userID"].asString().unwrapOr("");
        request.receivedAt = item["receivedAt"].asInt().unwrapOr(0);
        request.played = item["played"].asBool().unwrapOr(false);
        request.percent = static_cast<int>(
            std::clamp<int64_t>(item["percent"].asInt().unwrapOr(0), 0, 100));
        request.videoUrl = trimCopy(item["video"].asString().unwrapOr(""));
        if (!isValidVideoUrl(request.videoUrl)) request.videoUrl.clear();
        auto platform = item["platform"].asString().unwrapOr("twitch");
        request.platform = platform == "web" ? Platform::Web : platformFromKey(platform);
        if (!item.contains("description") && request.platform != Platform::Web) {
            auto text = trimCopy(request.message);
            auto command = text.substr(0, text.find_first_of(" \t\r\n:"));
            if (command.starts_with('!')) {
                if (auto parsed = parseRequest(text, command); parsed && parsed->levelID == request.levelID) {
                    request.description = std::move(parsed->description);
                    request.command = std::move(parsed->command);
                }
            }
        }
        clampUtf8(request.requester, 64);
        clampUtf8(request.message, 1500);
        clampUtf8(request.description, 1000);
        clampUtf8(request.eventID, 300);
        clampUtf8(request.userID, 128);
        rememberEvent(request.eventID);
        if (request.receivedAt > 0) {
            auto const key = userQueueKey(request.platform, request.requester, request.userID, request.queue);
            auto& last = m_lastRequestAt[key];
            last = std::max(last, request.receivedAt);
        }
        m_requests.push_back(std::move(request));
        if (m_requests.size() >= 500) break;
    }
    ++m_queueRevision;
}

void TwitchRequestManager::saveQueue() {
    auto array = matjson::Value::array();
    for (auto const& request : m_requests) {
        array.push(matjson::makeObject({
            {"entryID", request.entryID},
            {"levelID", request.levelID},
            {"webRequestID", request.webRequestID},
            {"requesterAccountID", request.requesterAccountID},
            {"requester", request.requester},
            {"requesterVerified", request.requesterVerified},
            {"message", request.message},
            {"description", request.description},
            {"queue", request.queue},
            {"command", request.command},
            {"rewardID", request.rewardID},
            {"sourceName", request.sourceName},
            {"eventID", request.eventID},
            {"userID", request.userID},
            {"receivedAt", request.receivedAt},
            {"played", request.played},
            {"percent", request.percent},
            {"video", request.videoUrl},
            {"platform", platformKey(request.platform)},
        }));
    }
    Mod::get()->setSavedValue(kQueueKey, array);
    Mod::get()->setSavedValue(kNextEntryKey, m_nextEntryID);
    auto events = matjson::Value::array();
    for (auto const& event : m_recentEvents) events.push(event);
    Mod::get()->setSavedValue(kRecentEventsKey, events);
    paimon::requestDeferredModSave();
}

}
