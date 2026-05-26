#include "DiscordPresenceManager.hpp"

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
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/binding/CreatorLayer.hpp>
#include <Geode/binding/LevelSearchLayer.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/LeaderboardsLayer.hpp>
#include <Geode/binding/GauntletLayer.hpp>
#include <Geode/binding/DailyLevelPage.hpp>
#include <Geode/binding/GJShopLayer.hpp>
#include <Geode/binding/ChallengesPage.hpp>
#include <Geode/binding/RewardsPage.hpp>
#include <Geode/binding/SecretLayer.hpp>
#include <Geode/binding/SecretLayer2.hpp>
#include <Geode/binding/SecretLayer3.hpp>
#include <Geode/binding/SecretLayer4.hpp>
#include <Geode/binding/SecretLayer5.hpp>
#include <Geode/binding/LevelSelectLayer.hpp>
#include <Geode/binding/GJGarageLayer.hpp>
#include <Geode/binding/GJPathsLayer.hpp>
#include <Geode/binding/LevelAreaLayer.hpp>
#include <Geode/binding/LevelAreaInnerLayer.hpp>
#include <Geode/binding/GauntletSelectLayer.hpp>
#include <cctype>
#include <ctime>
#include <thread>
#include "../../../utils/ThreadTracker.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#ifdef PAIMON_HAS_DISCORD_RPC
#include <discord-rpc.hpp>
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

// Truncate to at most `maxBytes` while keeping the resulting string a valid
// UTF-8 sequence. Discord rejects the entire presence (silently — the user
// just stops seeing it) if any field contains a malformed UTF-8 sequence,
// which is what happens if a multi-byte codepoint gets cut in half by a
// blind .resize(N) on a string with non-ASCII content (Japanese level
// names, emojis, etc.).
std::string safeUtf8Truncate(std::string value, size_t maxBytes) {
    if (value.size() <= maxBytes) return value;
    size_t pos = maxBytes;
    // Walk back over UTF-8 continuation bytes (0b10xxxxxx) so we land on the
    // first byte of a codepoint, then drop the partial codepoint entirely.
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

// Discord only accepts http(s) URLs for presence buttons. We hardcode our
// buttons to the project's GitHub and Discord, so this validator is unused.
// (Kept commented in case we need it again in the future.)
}

DiscordPresenceManager& DiscordPresenceManager::get() {
    static auto* instance = new DiscordPresenceManager();
    return *instance;
}

void DiscordPresenceManager::init() {
    if (m_initialized || m_shutdown) return;
    m_startTimestamp = static_cast<int64_t>(std::time(nullptr));

#ifdef PAIMON_HAS_DISCORD_RPC
    ::discord::RPCManager::get()
        .setClientID(kApplicationID)
        .initialize();
#endif

    static bool s_listenersRegistered = false;
    if (!s_listenersRegistered) {
        s_listenersRegistered = true;
        geode::listenForSettingChanges<bool>("discord-rpc-enabled", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<bool>("discord-rpc-private-mode", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<bool>("discord-rpc-idle-when-unfocused", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<bool>("discord-rpc-show-progress", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<bool>("discord-rpc-include-paimbnails-features", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<std::string>("discord-rpc-large-text", +[](std::string const&) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<std::string>("discord-rpc-large-image-key", +[](std::string const&) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<std::string>("discord-rpc-small-image-key", +[](std::string const&) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<std::string>("discord-rpc-activity-type", +[](std::string const&) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<bool>("discord-rpc-show-timestamp", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<bool>("discord-rpc-override-details", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<std::string>("discord-rpc-custom-details", +[](std::string const&) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<bool>("discord-rpc-override-state", +[](bool) {
            DiscordPresenceManager::get().refreshSoon();
        });
        geode::listenForSettingChanges<std::string>("discord-rpc-custom-state", +[](std::string const&) {
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
            // Perf: 5s polling is sufficient for Discord presence updates
            // (scene changes trigger refreshSoon() immediately anyway)
            // Sleep in 100ms chunks to support instant cancellation/shutdown
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
    ::discord::RPCManager::get().clearPresence();
    ::discord::RPCManager::get().shutdown();
#endif
}

void DiscordPresenceManager::refreshSoon() {
    if (m_shutdown) return;
    if (!m_initialized) init();
    if (m_refreshScheduled) return;
    m_refreshScheduled = true;
    Loader::get()->queueInMainThread([this]() {
        m_refreshScheduled = false;
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
    if (m_shutdown || !m_initialized) return;

#ifndef PAIMON_HAS_DISCORD_RPC
    return;
#else
    if (!paimon::settings::discord_rpc::enabled()) {
        ::discord::RPCManager::get().clearPresence();
        return;
    }

    auto payload = applyAssetFallbacks(buildPayload());
    if (payload == m_lastPayload) {
        return;
    }
    m_lastPayload = payload;

    auto& presence = ::discord::Presence::get();
    presence.clear();
    presence
        .setState(payload.state)
        .setDetails(payload.details)
        .setLargeImageKey(payload.largeImage)
        .setLargeImageText(payload.largeImageText)
        .setSmallImageKey(payload.smallImage)
        .setSmallImageText(payload.smallImageText)
        .setInstance(false);

    // Activity type (Playing / Listening / Watching / Competing).
    {
        auto type = paimon::settings::discord_rpc::activityType();
        auto activity = ::discord::ActivityType::Game;
        if (type == "Listening") activity = ::discord::ActivityType::Listening;
        else if (type == "Watching") activity = ::discord::ActivityType::Watching;
        else if (type == "Competing") activity = ::discord::ActivityType::Competing;
        presence.setActivityType(activity);
    }

    // Elapsed-time counter.
    if (paimon::settings::discord_rpc::showTimestamp()) {
        presence.setStartTimestamp(payload.startTimestamp ? payload.startTimestamp : m_startTimestamp);
    } else {
        presence.setStartTimestamp(static_cast<int64_t>(0));
    }

    // Botones fijos: siempre apuntamos al GitHub y al Discord oficial del mod.
    // No exponemos esto al usuario para evitar que escriba URLs invalidas que
    // hagan que Discord rechace la presence entera.
    presence.setButton1("Paimbnails Page", "https://github.com/FlozWerDev/Paimbnails", true);
    presence.setButton2("Paimbnails Discord", "https://discord.gg/5N5vpSfZwY", true);

    presence.refresh();
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

    // Apply user-defined overrides for details/state lines, capped to Discord's
    // 128-char limit so the presence is never rejected.
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

    // A "listening" activity makes Details the primary line and State the
    // secondary; the small image text is not shown in that mode.
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
    // Discord solo acepta asset keys ya registradas en la app. Si el usuario
    // deja un campo vacio, usamos los defaults del mod para no romper la presence.
    auto customLargeImage = trimAssetKey(paimon::settings::discord_rpc::largeImageKey());
    auto customSmallImage = trimAssetKey(paimon::settings::discord_rpc::smallImageKey());

    payload.largeImage = customLargeImage.empty() ? kDefaultLargeImage : customLargeImage;
    if (!customSmallImage.empty()) {
        payload.smallImage = customSmallImage;
    }
    payload.largeImageText = "Paimbnails Rich Presence";

    // Explicit user tooltip always wins when provided.
    auto customText = paimon::settings::discord_rpc::largeText();
    if (!customText.empty()) {
        if (customText.size() > 128) customText.resize(128);
        payload.largeImageText = customText;
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
