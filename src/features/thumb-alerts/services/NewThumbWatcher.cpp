#include "NewThumbWatcher.hpp"

#include "ThumbFeedSocket.hpp"
#include "../ThumbAlerts.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <vector>

using namespace geode::prelude;

namespace paimon::thumbalerts {

namespace {

constexpr char const* kSeenKey = "thumbalert-seen";
constexpr char const* kSeededKey = "thumbalert-seeded";
constexpr size_t kSeenLimit = 60;
constexpr size_t kMaxUploads = 40;
constexpr size_t kSelfUploadLimit = 20;

std::string stringField(matjson::Value const& item, char const* key) {
    return item.contains(key) ? item[key].asString().unwrapOr("") : "";
}

int intField(matjson::Value const& item, char const* key) {
    return item.contains(key) ? static_cast<int>(item[key].asInt().unwrapOr(0)) : 0;
}

bool boolField(matjson::Value const& item, char const* key) {
    return item.contains(key) && item[key].asBool().unwrapOr(false);
}

int rateTierOf(matjson::Value const& item) {
    if (boolField(item, "mythic")) return 4;
    if (boolField(item, "legendary")) return 3;
    if (boolField(item, "epic")) return 2;
    if (boolField(item, "featured")) return 1;
    return 0;
}

int pollSeconds() {
    return static_cast<int>(std::clamp<int64_t>(
        Mod::get()->getSettingValue<int64_t>("thumbalert-interval"), 60, 3600));
}

} // namespace

NewThumbWatcher& NewThumbWatcher::get() {
    static NewThumbWatcher instance;
    return instance;
}

void NewThumbWatcher::startup() {
    if (m_started) return;
    m_started = true;

    listenForSettingChanges<bool>("thumbalert-enabled", [](bool enabled) {
        if (enabled) NewThumbWatcher::get().pollNow();
    });

    this->pollNow();
    this->scheduleNextPoll();
}

void NewThumbWatcher::scheduleNextPoll() {
    paimon::scheduleMainThreadDelay(static_cast<float>(pollSeconds()), [] {
        if (paimon::isRuntimeShuttingDown()) return;
        auto& watcher = NewThumbWatcher::get();
        watcher.pollNow();
        watcher.scheduleNextPoll();
    });
}

void NewThumbWatcher::pollNow() {
    if (m_inFlight || paimon::isRuntimeShuttingDown()) return;
    if (!paimon::modules::isEnabled(kModuleId)) return;

    auto const config = readConfig();
    if (!config.enabled) return;
    // Nothing would be shown from here anyway, and the ids would be burned.
    if (!alertsAllowedHere(config)) return;

    m_inFlight = true;
    // /api/latest-uploads has no route of its own any more; the feed only comes
    // out through discovery, so the other lists are asked for at their minimum.
    HttpClient::get().fetchDiscovery(1, 1, kMaxUploads, [](bool ok, std::string const& body) {
        auto& watcher = NewThumbWatcher::get();
        watcher.m_inFlight = false;
        if (!ok || paimon::isRuntimeShuttingDown()) return;
        watcher.onResponse(body);
    });
}

void NewThumbWatcher::loadSeen() {
    if (m_loaded) return;
    m_loaded = true;
    auto const stored = Mod::get()->getSavedValue<std::string>(kSeenKey, "");
    if (stored.empty()) return;
    for (auto const& id : geode::utils::string::split(stored, "|")) {
        if (!id.empty()) m_seen.push_back(id);
    }
}

void NewThumbWatcher::saveSeen() {
    std::string joined;
    for (auto const& id : m_seen) {
        if (!joined.empty()) joined.push_back('|');
        joined += id;
    }
    Mod::get()->setSavedValue<std::string>(kSeenKey, joined);
    paimon::requestDeferredModSave();
}

void NewThumbWatcher::suppressLevel(int levelId) {
    if (levelId <= 0) return;
    if (std::ranges::find(m_selfUploads, levelId) != m_selfUploads.end()) return;
    m_selfUploads.push_back(levelId);
    while (m_selfUploads.size() > kSelfUploadLimit) m_selfUploads.pop_front();
}

bool NewThumbWatcher::markSeen(std::string const& eventId) {
    this->loadSeen();
    if (std::ranges::find(m_seen, eventId) != m_seen.end()) return false;
    m_seen.push_back(eventId);
    while (m_seen.size() > kSeenLimit) m_seen.pop_front();
    return true;
}

bool NewThumbWatcher::acceptEntry(matjson::Value const& entry, NewThumb& out, bool& marked) {
    if (!entry.isObject()) return false;

    out.levelId = intField(entry, "levelId");
    if (out.levelId <= 0) return false;

    out.eventId = stringField(entry, "eventId");
    if (out.eventId.empty()) {
        out.eventId = fmt::format("thumbnail:{}:{}", out.levelId,
                                  stringField(entry, "thumbnailId"));
    }
    // Whichever channel got here first already announced it.
    if (!this->markSeen(out.eventId)) return false;
    marked = true;

    // Our own upload already got its card off the upload reply; the feed keeps
    // one entry per level, so one match consumes one suppression.
    if (auto self = std::ranges::find(m_selfUploads, out.levelId);
        self != m_selfUploads.end()) {
        m_selfUploads.erase(self);
        return false;
    }

    out.levelName = stringField(entry, "levelName");
    out.creator = stringField(entry, "creator");
    out.uploader = stringField(entry, "username");
    out.difficulty = stringField(entry, "difficulty");
    out.length = stringField(entry, "length");
    out.stars = intField(entry, "stars");
    out.coins = intField(entry, "coins");
    out.downloads = intField(entry, "downloads");
    out.likes = intField(entry, "likes");
    out.verifiedCoins = boolField(entry, "verifiedCoins");
    out.rateTier = rateTierOf(entry);
    return true;
}

void NewThumbWatcher::onPushMessage(std::string const& message) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (!paimon::modules::isEnabled(kModuleId)) return;

    // Dropped before the id is recorded, exactly like the poll skips running in
    // these scenes: the entry stays unseen and the catch-up poll shows it once
    // the player is somewhere the card is welcome.
    auto const config = readConfig();
    if (!config.enabled || !alertsAllowedHere(config)) return;

    auto parsed = matjson::parse(message);
    if (!parsed.isOk()) return;

    auto const json = parsed.unwrap();
    if (stringField(json, "type") != "thumbnail") return;
    if (!json.contains("data")) return;

    NewThumb item;
    bool marked = false;
    if (!this->acceptEntry(json["data"], item, marked)) {
        if (marked) this->saveSeen();
        return;
    }
    this->saveSeen();

    // Before the first poll there is no baseline, and a pushed entry is new by
    // definition, so it does not need one.
    if (!Mod::get()->getSavedValue<bool>(kSeededKey, false)) {
        Mod::get()->setSavedValue<bool>(kSeededKey, true);
    }
    showThumbAlert(std::move(item));
}

void NewThumbWatcher::onResponse(std::string const& body) {
    auto parsed = matjson::parse(body);
    if (!parsed.isOk()) return;

    auto const json = parsed.unwrap();
    if (!json.contains("latestUploads") || !json["latestUploads"].isObject()) return;

    auto const& feed = json["latestUploads"];
    if (!feed.contains("uploads") || !feed["uploads"].isArray()) return;

    auto uploads = feed["uploads"].asArray();
    if (!uploads.isOk()) return;

    auto const entries = uploads.unwrap();
    std::vector<NewThumb> fresh;
    bool marked = false;

    // The feed is newest first; walk it backwards so several new thumbnails
    // come out in the order they were published.
    size_t const count = std::min(entries.size(), kMaxUploads);
    for (size_t index = count; index-- > 0;) {
        auto const& entry = entries[index];
        if (!entry.isObject()) continue;

        NewThumb item;
        if (this->acceptEntry(entry, item, marked)) fresh.push_back(std::move(item));
    }

    // Suppressed entries count too: they are new ids, and forgetting them would
    // replay our own upload on the next launch.
    if (!marked) return;
    this->saveSeen();

    // First run only records the baseline: everything already on the feed is
    // old news, and 20 cards in a row would be a wall.
    if (!Mod::get()->getSavedValue<bool>(kSeededKey, false)) {
        Mod::get()->setSavedValue<bool>(kSeededKey, true);
        paimon::requestDeferredModSave();
        return;
    }
    if (fresh.empty()) return;

    auto const config = readConfig();
    size_t const shown = std::min<size_t>(fresh.size(), static_cast<size_t>(config.maxBatch));
    for (size_t index = fresh.size() - shown; index < fresh.size(); ++index) {
        showThumbAlert(fresh[index]);
    }
}

} // namespace paimon::thumbalerts

$on_game(Loaded) {
    paimon::thumbalerts::NewThumbWatcher::get().startup();
    paimon::thumbalerts::ThumbFeedSocket::get().start();
}

$execute {
    ButtonSettingPressedEventV3(Mod::get(), "thumbalert-preview").listen([](auto buttonKey) {
        if (buttonKey != "run") return;
        paimon::thumbalerts::showThumbAlertPreview();
    }).leak();

    ButtonSettingPressedEventV3(Mod::get(), "thumbalert-check-now").listen([](auto buttonKey) {
        if (buttonKey != "run") return;
        paimon::thumbalerts::NewThumbWatcher::get().pollNow();
        PaimonNotify::create("Checking for new thumbnails...", NotificationIcon::Info)->show();
    }).leak();
}
