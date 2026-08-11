#include "DiscordPresenceManager.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../layers/PaimonHubLayer.hpp"
#include "../../../features/capture/ui/CapturePreviewPopup.hpp"
#include "../../../features/profiles/ui/ProfileSettingsPopup.hpp"
#include "../../../features/moderation/ui/VerificationCenterLayer.hpp"
#include "../../../features/paidraw/PaiDrawManager.hpp"
#include "../../../features/paidraw/PaiDrawModels.hpp"
#include "../../../features/paidraw/PaiDrawUI.hpp"
#include "../../../features/menu-music/services/MenuMusicPlayer.hpp"
#include "../../../features/menu-music/ui/MenuMusicPopup.hpp"
#include "../../../features/profile-music/services/ProfileMusicManager.hpp"
#include <Geode/Geode.hpp>
#include <cctype>
#include <ctime>
#include <thread>
#include "../../../utils/ThreadTracker.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#ifdef PAIMON_HAS_DISCORD_RPC
#include "DiscordIpcClient.hpp"
#endif

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

namespace paimon::discord {

namespace {
constexpr char const* kApplicationID = "1503220958910218260";
constexpr char const* kDefaultLargeImage = "paimbnails";

template <class T>
T* findSceneLayer() {
    auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
    return scene ? scene->getChildByType<T>(0) : nullptr;
}

std::string trimOrDefault(std::string value, std::string const& fallback);
std::string safeUtf8Truncate(std::string value, size_t maxBytes);
std::string safeUtf8Truncate(std::string value, size_t maxBytes) {
    if (value.size() <= maxBytes) return value;
    size_t pos = maxBytes;
    while (pos > 0 && (static_cast<unsigned char>(value[pos]) & 0xC0) == 0x80) {
        --pos;
    }
    value.resize(pos);
    return value;
}

std::string trimOrDefault(std::string value, std::string const& fallback) {
    if (value.empty()) return fallback;
    return safeUtf8Truncate(std::move(value), 120);
}

std::string trimAssetKey(std::string value) {
    auto isWhitespace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };

    while (!value.empty() && isWhitespace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && isWhitespace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return safeUtf8Truncate(std::move(value), 128);
}

bool isExternalImageUrl(std::string const& v) {
    return v.rfind("https://", 0) == 0 || v.rfind("http://", 0) == 0 || v.rfind("mp:", 0) == 0;
}

std::string trimExternalUrl(std::string value) {
    auto isWhitespace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };
    while (!value.empty() && isWhitespace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && isWhitespace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return safeUtf8Truncate(std::move(value), 256);
}

}

DiscordPresenceManager& DiscordPresenceManager::get() {
    // RPC worker can outlive Cocos teardown; never-freed instance avoids an atexit race.
    static auto* instance = new DiscordPresenceManager();
    return *instance;
}

void DiscordPresenceManager::init() {
    if (m_initialized || m_shutdown) return;
    m_startTimestamp = static_cast<int64_t>(std::time(nullptr));

#ifdef PAIMON_HAS_DISCORD_RPC
    DiscordIpcClient::get().setClientID(kApplicationID);
#endif

    static bool s_listenersRegistered = false;
    if (!s_listenersRegistered) {
        s_listenersRegistered = true;
        geode::listenForSettingChanges<bool>("discord-rpc-enabled", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
    }

    m_workerToken = std::make_shared<std::atomic<bool>>(true);
    paimon::ThreadTracker::get().spawn([token = m_workerToken]() {
        geode::utils::thread::setName("Paimon Discord RPC");
        using namespace std::chrono_literals;
        while (token->load(std::memory_order_acquire) && !paimon::isRuntimeShuttingDown()) {
            if (paimon::isRuntimeShuttingDown()) return;
            Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                DiscordPresenceManager::get().refreshNow();
            });
            for (int i = 0; i < 50; ++i) {
                if (!token->load(std::memory_order_acquire) || paimon::isRuntimeShuttingDown()) break;
                std::this_thread::sleep_for(100ms);
            }
        }
    });

    m_initialized = true;
    refreshSoon();
}

void DiscordPresenceManager::shutdown() {
    if (m_shutdown) return;
    m_shutdown = true;
    m_refreshScheduled = false;
    if (m_workerToken) {
        m_workerToken->store(false, std::memory_order_release);
    }

#ifdef PAIMON_HAS_DISCORD_RPC
    DiscordIpcClient::get().clear();
    DiscordIpcClient::get().close();
#endif
}

void DiscordPresenceManager::refreshSoon() {
    if (m_shutdown || paimon::isRuntimeShuttingDown()) return;
    if (!m_initialized) init();
    if (m_refreshScheduled) return;
    m_refreshScheduled = true;
    Loader::get()->queueInMainThread([this]() {
        if (paimon::isRuntimeShuttingDown()) return;
        m_refreshScheduled = false;
        if (m_shutdown) return;
        refreshNow();
    });
}

void DiscordPresenceManager::setTemporaryContext(std::string const& key, std::string const& state, std::string const& details) {
    PresencePayload payload;
    payload.state = state;
    payload.details = details;
    payload.startTimestamp = m_startTimestamp;
    m_temporaryContexts[key] = payload;
    refreshSoon();
}

void DiscordPresenceManager::clearTemporaryContext(std::string const& key) {
    m_temporaryContexts.erase(key);
    refreshSoon();
}

void DiscordPresenceManager::refreshNow() {
    if (m_shutdown || !m_initialized || paimon::isRuntimeShuttingDown()) return;

#ifndef PAIMON_HAS_DISCORD_RPC
    return;
#else
    if (!paimon::settings::discord_rpc::enabled()) {
        DiscordIpcClient::get().clear();
        return;
    }

    auto payload = applyAssetFallbacks(buildPayload());
    if (payload == m_lastPayload) {
        return;
    }
    m_lastPayload = payload;

    DiscordActivity activity;
    activity.state = payload.state;
    activity.details = payload.details;
    activity.largeImage = payload.largeImage;
    activity.largeText = payload.largeImageText;
    activity.smallImage = payload.smallImage;
    activity.smallText = payload.smallImageText;

    {
        auto type = paimon::settings::discord_rpc::activityType();
        activity.type = DiscordActivityType::Playing;
        if (type == "Listening") activity.type = DiscordActivityType::Listening;
        else if (type == "Watching") activity.type = DiscordActivityType::Watching;
        else if (type == "Competing") activity.type = DiscordActivityType::Competing;
    }

    if (paimon::settings::discord_rpc::showTimestamp()) {
        activity.startTimestamp = payload.startTimestamp ? payload.startTimestamp : m_startTimestamp;
    }

    activity.button1Label = "Paimbnails Page";
    activity.button1Url = "https://github.com/FlozWerDev/Paimbnails";
    activity.button2Label = "Paimbnails Discord";
    activity.button2Url = "https://discord.gg/5N5vpSfZwY";

    DiscordIpcClient::get().update(activity);
#endif
}

PresencePayload DiscordPresenceManager::buildPayload() {
    PresencePayload payload;
    for (auto const& [_, ctx] : m_temporaryContexts) {
        payload = ctx;
        break;
    }

    if (payload.state.empty() && payload.details.empty()) {
        payload = buildScenePayload();
    }

    payload.startTimestamp = m_startTimestamp;

    auto capField = [](std::string value) {
        return safeUtf8Truncate(std::move(value), 128);
    };

    if (paimon::settings::discord_rpc::overrideDetails()) {
        auto custom = paimon::settings::discord_rpc::customDetails();
        if (!custom.empty()) payload.details = capField(custom);
    }
    if (paimon::settings::discord_rpc::overrideState()) {
        auto custom = paimon::settings::discord_rpc::customState();
        if (!custom.empty()) payload.state = capField(custom);
    }

    return payload;
}

PresencePayload DiscordPresenceManager::buildScenePayload() {
    PresencePayload payload;

    if (isIdle()) {
        payload.state = "Idle in Paimbnails";
        payload.details = "Geometry Dash is unfocused";
        payload.smallImage = "idle";
        payload.smallImageText = "Desktop idle";
        return payload;
    }

    if (paimon::settings::discord_rpc::includePaimbnailsFeatures()) {
        if (findSceneLayer<paidraw::PaiDrawGameLayer>() || findSceneLayer<paidraw::PaiDrawRoomLayer>() ||
            findSceneLayer<paidraw::PaiDrawLobbyLayer>() || findSceneLayer<paidraw::PaiDrawResultsLayer>()) {
            auto paiDraw = paidraw::PaiDrawManager::get().snapshot();
            if (paiDraw.currentRoomId != 0 || paiDraw.connected || paiDraw.connecting) {
                payload.state = paiDraw.currentRoom.state == paidraw::RoomState::InGame ? "Drawing in PaiDraw" : "In a PaiDraw room";
                payload.details = !paiDraw.currentRoom.config.name.empty()
                    ? fmt::format("{} | {} players", paiDraw.currentRoom.config.name, paiDraw.currentRoom.playerCount())
                    : "Online drawing with Paimbnails";
                payload.smallImage = "paidraw";
                payload.smallImageText = paiDraw.currentRound.timeLeftSeconds > 0
                    ? fmt::format("{}s left", paiDraw.currentRound.timeLeftSeconds)
                    : "PaiDraw";
                return payload;
            }
        }

        if (findSceneLayer<paimon::menumusic::MenuMusicPopup>()) {
            auto& menuMusic = paimon::menumusic::MenuMusicPlayer::get();
            if (auto* track = menuMusic.currentTrack()) {
                payload.state = "Using Menu Music";
                payload.details = track->artist.empty()
                    ? trimOrDefault(track->displayName, "Custom menu music")
                    : trimOrDefault(fmt::format("{} | {}", track->displayName, track->artist), "Custom menu music");
                payload.smallImage = "music";
                payload.smallImageText = menuMusic.isPaused() ? "Paused" : "Now playing";
                return payload;
            }
        }

        if (findSceneLayer<ProfilePage>() && ProfileMusicManager::get().isPlaying()) {
            auto& profileMusic = ProfileMusicManager::get();
            payload.state = "Listening to profile music";
            payload.details = profileMusic.getCurrentPlayingProfile() > 0
                ? fmt::format("Profile {}", profileMusic.getCurrentPlayingProfile())
                : "Profile music active";
            payload.smallImage = "music";
            payload.smallImageText = profileMusic.isPaused() ? "Paused" : "Profile music";
            return payload;
        }
    }

    if (findSceneLayer<PaimonHubLayer>()) {
        payload.state = "Browsing Paimon Hub";
        payload.details = "Managing Paimbnails features";
        payload.smallImage = "paimon-hub";
        payload.smallImageText = "Paimon Hub";
        return payload;
    }

    if (findSceneLayer<CapturePreviewPopup>()) {
        payload.state = "Previewing a thumbnail capture";
        payload.details = "Fine-tuning a new Paimbnails shot";
        payload.smallImage = "capture";
        payload.smallImageText = "Capture preview";
        return payload;
    }

    if (findSceneLayer<VerificationCenterLayer>()) {
        payload.state = "Reviewing pending media";
        payload.details = "Inside the Paimbnails verification center";
        payload.smallImage = "moderation";
        payload.smallImageText = "Verification center";
        return payload;
    }

    if (findSceneLayer<ProfileSettingsPopup>()) {
        payload.state = "Editing profile customization";
        payload.details = "Adjusting profile media and settings";
        payload.smallImage = "profile";
        payload.smallImageText = "Profile settings";
        return payload;
    }

    if (auto* layer = findSceneLayer<PlayLayer>()) {
        auto* level = layer->m_level;
        std::string state = layer->m_isPracticeMode ? "Practicing a level" : "Playing a level";
        if (level && level->isPlatformer()) {
            state = layer->m_isPracticeMode ? "Practicing a platformer" : "Playing a platformer";
        }

        std::string details = "In gameplay";
        if (level) {
            if (paimon::settings::discord_rpc::privateMode() && level->m_unlisted) {
                details = "Private level session";
            } else {
                auto title = sanitizeLevelTitle(level->m_levelName);
                auto creator = sanitizeCreatorName(level->m_creatorName);
                details = creator.empty() ? title : fmt::format("{} by {}", title, creator);
            }
        }

        payload.state = state;
        payload.details = details;
        payload.smallImage = level ? resolveDifficultyAsset(level) : "play";
        if (level) {
            if (paimon::settings::discord_rpc::showProgress() && !level->isPlatformer()) {
                payload.smallImageText = fmt::format("Best {}%", level->m_normalPercent.value());
            } else if (level->isPlatformer()) {
                payload.smallImageText = "Platformer gameplay";
            } else {
                payload.smallImageText = "Gameplay";
            }
        }
        return payload;
    }

    if (auto* layer = findSceneLayer<LevelEditorLayer>()) {
        auto* level = layer->m_level;
        payload.state = "Editing a level";
        if (level && !paimon::settings::discord_rpc::privateMode()) {
            payload.details = trimOrDefault(level->m_levelName, "Level editor");
        } else {
            payload.details = "Working in the Geometry Dash editor";
        }
        payload.smallImage = "editor";
        payload.smallImageText = "Level editor";
        return payload;
    }

    if (auto* layer = findSceneLayer<ProfilePage>()) {
        payload.state = "Viewing a profile";
        if (paimon::settings::discord_rpc::privateMode()) {
            payload.details = "Browsing community profiles";
        } else {
            payload.details = fmt::format("Account {}", layer->m_accountID);
        }
        payload.smallImage = "profile";
        payload.smallImageText = "Profile page";
        return payload;
    }

    if (auto* layer = findSceneLayer<LevelInfoLayer>()) {
        auto* level = layer->m_level;
        payload.state = "Viewing level info";
        if (level) {
            auto title = sanitizeLevelTitle(level->m_levelName);
            auto creator = sanitizeCreatorName(level->m_creatorName);
            payload.details = creator.empty() ? title : fmt::format("{} by {}", title, creator);
            payload.smallImage = resolveDifficultyAsset(level);
            payload.smallImageText = level->m_stars.value() > 0
                ? fmt::format("{} stars", level->m_stars.value())
                : "Unrated";
        } else {
            payload.details = "Inspecting a level";
            payload.smallImage = "level-info";
            payload.smallImageText = "Level info";
        }
        return payload;
    }

    if (auto* layer = findSceneLayer<LevelBrowserLayer>()) {
        payload.state = "Browsing level lists";
        payload.details = layer->m_searchObject ? "Looking through online levels" : "Exploring levels";
        payload.smallImage = "browser";
        payload.smallImageText = "Level browser";
        return payload;
    }

    if (findSceneLayer<LevelSearchLayer>()) {
        payload.state = "Searching for levels";
        payload.details = "Looking for something new to play";
        payload.smallImage = "search";
        payload.smallImageText = "Level search";
        return payload;
    }

    if (findSceneLayer<CreatorLayer>()) {
        payload.state = "Using online features";
        payload.details = "Inside the Creator tab";
        payload.smallImage = "creator";
        payload.smallImageText = "Creator";
        return payload;
    }

    if (findSceneLayer<LeaderboardsLayer>()) {
        payload.state = "Browsing leaderboards";
        payload.details = "Checking community rankings";
        payload.smallImage = "leaderboards";
        payload.smallImageText = "Leaderboards";
        return payload;
    }

    if (findSceneLayer<GauntletSelectLayer>() || findSceneLayer<GauntletLayer>()) {
        payload.state = "Exploring gauntlets";
        payload.details = "Checking curated challenge paths";
        payload.smallImage = "gauntlet";
        payload.smallImageText = "Gauntlets";
        return payload;
    }

    if (findSceneLayer<DailyLevelPage>()) {
        payload.state = "Checking timed levels";
        payload.details = "Daily, weekly, or event content";
        payload.smallImage = "daily";
        payload.smallImageText = "Timed levels";
        return payload;
    }

    if (findSceneLayer<GJGarageLayer>()) {
        payload.state = "Customizing icons";
        payload.details = "Tweaking the player look";
        payload.smallImage = "garage";
        payload.smallImageText = "Garage";
        return payload;
    }

    if (findSceneLayer<GJShopLayer>()) {
        payload.state = "Shopping in Geometry Dash";
        payload.details = "Visiting one of the shops";
        payload.smallImage = "shop";
        payload.smallImageText = "Shop";
        return payload;
    }

    if (findSceneLayer<ChallengesPage>()) {
        payload.state = "Checking quests";
        payload.details = "Reviewing challenge progress";
        payload.smallImage = "quests";
        payload.smallImageText = "Quests";
        return payload;
    }

    if (findSceneLayer<RewardsPage>()) {
        payload.state = "Opening chests";
        payload.details = "Claiming rewards";
        payload.smallImage = "rewards";
        payload.smallImageText = "Rewards";
        return payload;
    }

    if (findSceneLayer<SecretLayer>() || findSceneLayer<SecretLayer2>() || findSceneLayer<SecretLayer3>() ||
        findSceneLayer<SecretLayer4>() || findSceneLayer<SecretLayer5>()) {
        payload.state = "Exploring secret areas";
        payload.details = "Messing with vaults and hidden rooms";
        payload.smallImage = "vault";
        payload.smallImageText = "Secrets";
        return payload;
    }

    if (findSceneLayer<GJPathsLayer>()) {
        payload.state = "Unlocking paths";
        payload.details = "Progressing through the Path system";
        payload.smallImage = "paths";
        payload.smallImageText = "Paths";
        return payload;
    }

    if (findSceneLayer<LevelAreaLayer>() || findSceneLayer<LevelAreaInnerLayer>()) {
        payload.state = "Exploring the Tower";
        payload.details = "Walking around story content";
        payload.smallImage = "tower";
        payload.smallImageText = "Tower";
        return payload;
    }

    if (findSceneLayer<LevelSelectLayer>()) {
        payload.state = "Exploring main levels";
        payload.details = "Browsing official Geometry Dash levels";
        payload.smallImage = "main-levels";
        payload.smallImageText = "Official levels";
        return payload;
    }

    if (findSceneLayer<MenuLayer>()) {
        payload.state = "Browsing menus";
        payload.details = "At the main menu with Paimbnails";
        payload.smallImage = "menu";
        payload.smallImageText = "Main menu";
        return payload;
    }

    payload.state = "Using Paimbnails";
    payload.details = "Inside Geometry Dash";
    payload.smallImage = "paimbnails";
    payload.smallImageText = "Paimbnails";
    return payload;
}

PresencePayload DiscordPresenceManager::applyAssetFallbacks(PresencePayload payload) {
    // Large image: allow either a Discord asset key (max 128) or an external https:// URL (max 256 -> Discord mp:external)
    auto rawLarge = paimon::settings::discord_rpc::largeImageKey();
    std::string customLargeImage;
    bool largeIsExternal = false;
    {
        std::string trimmed = trimExternalUrl(rawLarge);
        if (isExternalImageUrl(trimmed)) {
            customLargeImage = std::move(trimmed);
            largeIsExternal = true;
        } else {
            customLargeImage = trimAssetKey(std::move(trimmed));
        }
    }

    // Small image: same — asset key or external URL for per-user custom image
    auto rawSmall = paimon::settings::discord_rpc::smallImageKey();
    std::string customSmallImage;
    bool smallIsExternal = false;
    {
        std::string trimmed = trimExternalUrl(rawSmall);
        if (isExternalImageUrl(trimmed)) {
            customSmallImage = std::move(trimmed);
            smallIsExternal = true;
        } else {
            customSmallImage = trimAssetKey(std::move(trimmed));
        }
    }

    if (largeIsExternal) {
        payload.largeImage = customLargeImage;
    } else {
        payload.largeImage = customLargeImage.empty() ? kDefaultLargeImage : customLargeImage;
    }
    if (!customSmallImage.empty()) {
        // External URLs and asset keys both override the scene small image.
        // For per-user local images, the popup uploads to catbox and stores the https:// URL here.
        payload.smallImage = customSmallImage;
    }
    payload.largeImageText = "Paimbnails Rich Presence";
    auto customText = paimon::settings::discord_rpc::largeText();
    if (!customText.empty()) {
        payload.largeImageText = safeUtf8Truncate(std::move(customText), 128);
    }
    // Small image hover text is now fully customizable independently from scene.
    auto customSmallText = paimon::settings::discord_rpc::smallText();
    if (!customSmallText.empty()) {
        payload.smallImageText = safeUtf8Truncate(std::move(customSmallText), 128);
    }

    return payload;
}

bool DiscordPresenceManager::isIdle() const {
    return paimon::settings::discord_rpc::idleWhenUnfocused() && !isFocused();
}

bool DiscordPresenceManager::isFocused() const {
#ifdef GEODE_IS_WINDOWS
    auto hwnd = GetForegroundWindow();
    if (!hwnd) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
#else
    return true;
#endif
}

std::string DiscordPresenceManager::resolveDifficultyAsset(GJGameLevel* level) const {
    if (!level) return "level";
    if (level->m_autoLevel) return "auto";
    if (level->m_demon) {
        switch (level->m_demonDifficulty) {
            case 3: return "easy_demon";
            case 4: return "medium_demon";
            case 0: return "hard_demon";
            case 5: return "insane_demon";
            case 6: return "extreme_demon";
            default: return "demon";
        }
    }

    auto diff = level->getAverageDifficulty();
    if (level->m_levelType == GJLevelType::Main) {
        diff = static_cast<int>(level->m_difficulty);
    }
    switch (diff) {
        case 1: return "easy";
        case 2: return "normal";
        case 3: return "hard";
        case 4: return "harder";
        case 5: return "insane";
        default: return "na";
    }
}

std::string DiscordPresenceManager::sanitizeLevelTitle(std::string const& name) const {
    if (paimon::settings::discord_rpc::privateMode()) {
        return "A level";
    }
    return trimOrDefault(name, "Unnamed level");
}

std::string DiscordPresenceManager::sanitizeCreatorName(std::string const& name) const {
    if (paimon::settings::discord_rpc::privateMode()) {
        return {};
    }
    return trimOrDefault(name, "Unknown creator");
}

} // namespace paimon::discord
