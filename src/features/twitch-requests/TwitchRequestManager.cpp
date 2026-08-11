#include "TwitchRequestManager.hpp"

#include "TwitchRequestFilters.hpp"
#include "TwitchRequestNotify.hpp"
#include "TwitchRequestParser.hpp"
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

// Preserve Twitch's original setting key.
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

// Trim UTF-8 by code point so saved queue JSON stays valid.
void clampUtf8(std::string& text, size_t limit) {
    if (text.size() <= limit) return;
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    text.resize(cut);
}

// Migrate legacy channel values into per-platform settings.
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

// Count only requests known to fail the filters.
bool filteredOut(LevelRequest const& request) {
    auto passes = requestPasses(request.levelID, !request.videoUrl.empty());
    return passes && !*passes;
}

}

std::string requestNote(LevelRequest const& request) {
    if (request.platform != Platform::Web) return {};
// Legacy queue filler is not a real note.
    if (request.message == fmt::format("Web request: {}", request.levelID)) return {};
    return trimCopy(request.message);
}

TwitchRequestManager& TwitchRequestManager::get() {
    static TwitchRequestManager instance;
    return instance;
}

// Web has no channel or ChatSource; clamp accidental Web indexing.
TwitchRequestManager::Link& TwitchRequestManager::link(Platform platform) {
    return m_links[std::min<size_t>(static_cast<size_t>(platform), kPlatformCount - 1)];
}

TwitchRequestManager::Link const& TwitchRequestManager::link(Platform platform) const {
    return m_links[std::min<size_t>(static_cast<size_t>(platform), kPlatformCount - 1)];
}

void TwitchRequestManager::init() {
    if (m_initialized) return;
// Run before going live so setting listeners remain no-ops.
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
            m_filters.videoRules.push_back(rule);
        }
    }
    loadNotifyConfig();
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

// Web is active when enabled; it has no channel setting.
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

// Writing the setting triggers its listener and restarts the source.
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
// Web's channel is the GD account and is not user-entered.
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
// Writing the setting restarts that platform through its listener.
    Mod::get()->setSettingValue<std::string>(channelSettingKey(platform), value);
}

void TwitchRequestManager::setCommandsSetting(std::string value) {
    value = trimCopy(std::move(value));
    if (value.empty()) value = "!req";
    if (value == commandsSetting()) return;
    Mod::get()->setSettingValue<std::string>("twitch-requests-commands", value);
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
    callbacks.onMessage = [this, platform, generation](
        std::string requester, std::string message
    ) {
        if (generation != link(platform).generation || m_shuttingDown) return;
        addRequest(platform, std::move(requester), std::move(message));
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
        log::info("[WebRequests] escuchando en {}", webUrl());
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

// Retry keeps the same channel; going live later can pick it up.
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
    std::string requester,
    std::string message
) {
    auto parsed = parseRequest(message, commandsSetting());
    if (!parsed) return;
    enqueueRequest(platform, std::move(requester), std::move(message), std::move(*parsed));
}

std::string TwitchRequestManager::addWebRequest(WebRequest incoming) {
    if (incoming.levelID <= 0) return "invalid";
    ParsedRequest parsed;
    parsed.levelID = incoming.levelID;
    parsed.command = "web";
    parsed.url = std::move(incoming.video);
// Empty notes hide the row's read button.
    return enqueueRequest(
        Platform::Web,
        std::move(incoming.requester),
        std::move(incoming.message),
        std::move(parsed),
        incoming.requesterVerified
    );
}

std::string TwitchRequestManager::enqueueRequest(
    Platform platform,
    std::string requester,
    std::string message,
    ParsedRequest parsed,
    bool requesterVerified
) {
    if (!m_accepting) return "paused";

    requester = trimCopy(std::move(requester));
    if (requester.empty()) requester = platformName(platform);
    clampUtf8(requester, 64);
    clampUtf8(message, 300);

    if (m_filters.verifiedOnly && !requesterVerified) return "unverified";
    if (m_filters.blockDuplicates
        && std::ranges::any_of(m_requests, [id = parsed.levelID](LevelRequest const& request) {
            return request.levelID == id;
        })) {
        return "duplicate";
    }

    auto const userKey = requesterKey(platform, requester);
    if (m_filters.maxPerUser > 0) {
        int const pending = static_cast<int>(std::ranges::count_if(
            m_requests, [&userKey](LevelRequest const& request) {
                return !request.played
                    && requesterKey(request.platform, request.requester) == userKey;
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
// Known levels that fail filters never enter the queue.
    if (auto passes = requestPasses(parsed.levelID, !parsed.url.empty()); passes && !*passes) return "filtered";

    LevelRequest request;
    request.levelID = parsed.levelID;
    request.requester = std::move(requester);
    request.requesterVerified = requesterVerified;
    request.message = std::move(message);
    request.receivedAt = now;
    request.videoUrl = std::move(parsed.url);
    request.platform = platform;
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

// The list watches queueRevision to rebuild.
    ++m_queueRevision;
    paimon::requestDeferredModSave();
}

size_t TwitchRequestManager::pendingCount() const {
    return static_cast<size_t>(std::ranges::count_if(m_requests,
        [](LevelRequest const& request) { return !request.played; }));
}

size_t TwitchRequestManager::filteredCount() const {
    if (!m_filters.hasLevelFilters()) return 0;
    return static_cast<size_t>(std::ranges::count_if(m_requests, filteredOut));
}

size_t TwitchRequestManager::removeFiltered() {
    if (!m_filters.hasLevelFilters()) return 0;
    size_t const before = m_requests.size();
    std::erase_if(m_requests, filteredOut);

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
    if (m_requests.empty()) {
        m_lastRequestAt.clear();
        return;
    }
    m_requests.clear();
    m_lastRequestAt.clear();
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
    auto saved = Mod::get()->getSavedValue<matjson::Value>(kQueueKey, matjson::Value::array());
    auto array = saved.asArray();
    if (!array) return;

    for (auto const& item : array.unwrap()) {
        int64_t levelID = item["levelID"].asInt().unwrapOr(0);
        if (levelID <= 0 || levelID > INT_MAX) continue;
        LevelRequest request;
        request.levelID = static_cast<int>(levelID);
        request.requester = item["requester"].asString().unwrapOr("Chat");
        request.requesterVerified = item["requesterVerified"].asBool().unwrapOr(false);
        request.message = item["message"].asString().unwrapOr("");
        request.receivedAt = item["receivedAt"].asInt().unwrapOr(0);
        request.played = item["played"].asBool().unwrapOr(false);
        request.percent = static_cast<int>(
            std::clamp<int64_t>(item["percent"].asInt().unwrapOr(0), 0, 100));
        request.videoUrl = item["video"].asString().unwrapOr("");
        auto platform = item["platform"].asString().unwrapOr("twitch");
        request.platform = platform == "web" ? Platform::Web : platformFromKey(platform);
        if (request.receivedAt > 0) {
            auto const key = requesterKey(request.platform, request.requester);
            auto& last = m_lastRequestAt[key];
            last = std::max(last, request.receivedAt);
        }
        m_requests.push_back(std::move(request));
        if (static_cast<int>(m_requests.size()) >= maxQueueSize()) break;
    }
    ++m_queueRevision;
}

void TwitchRequestManager::saveQueue() {
    auto array = matjson::Value::array();
    for (auto const& request : m_requests) {
        array.push(matjson::makeObject({
            {"levelID", request.levelID},
            {"requester", request.requester},
            {"requesterVerified", request.requesterVerified},
            {"message", request.message},
            {"receivedAt", request.receivedAt},
            {"played", request.played},
            {"percent", request.percent},
            {"video", request.videoUrl},
            {"platform", platformKey(request.platform)},
        }));
    }
    Mod::get()->setSavedValue(kQueueKey, array);
    paimon::requestDeferredModSave();
}

}
