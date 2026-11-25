#include "../../../framework/compat/NativeUI.hpp"
#include "DiscordPresenceManager.hpp"
#include "../../../layers/PaimonHubLayer.hpp"
#include "../../../features/capture/ui/CapturePreviewPopup.hpp"
#include "../../../features/profiles/ui/ProfileSettingsPopup.hpp"
#include "../../../features/moderation/ui/VerificationCenterLayer.hpp"
#include "../../../features/paidraw/PaiDrawManager.hpp"
#include "../../../features/paidraw/PaiDrawUI.hpp"
#include "../../../features/menu-music/services/MenuMusicPlayer.hpp"
#include "../../../features/menu-music/ui/MenuMusicPopup.hpp"
#include "../../../utils/ActivePauseLayer.hpp"
#include "../../../features/profile-music/services/ProfileMusicManager.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif
using namespace geode::prelude;

namespace paimon::discord {
namespace {
CCScene* activeScene() {
    auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
    for (int depth = 0; depth < 4; ++depth) {
        auto* transition = typeinfo_cast<CCTransitionScene*>(scene);
        if (!transition || !transition->m_pInScene || transition->m_pInScene == scene) break;
        scene = transition->m_pInScene;
    }
    return scene;
}
template <class T> T* findSceneLayer() {
    auto* scene = activeScene();
    return scene ? scene->getChildByType<T>(0) : nullptr;
}

std::string featureScreen(CCNode* node, int depth, int& budget) {
    if (!node || !node->isVisible() || depth < 0 || --budget < 0) return {};
    constexpr std::array screens{
        std::pair{"packgen-live-editor", "Editing a live texture palette"},
        std::pair{"texture-studio-layer", "Browsing texture palettes"},
        std::pair{"texture-studio-editor", "Editing a texture project"},
        std::pair{"fusion-editor-layer", "Combining texture images"},
        std::pair{"gif-import-popup", "Importing images into the level editor"},
        std::pair{"icon-maker-gallery", "Browsing custom icons"},
        std::pair{"icon-maker-editor", "Creating a custom icon"},
        std::pair{"icon-maker-gradient-popup", "Editing an icon gradient"},
        std::pair{"paimon-multisettings-panel", "Configuring Paimbnails"},
        std::pair{"collab-room", "Managing a collaborative editor room"},
        std::pair{"collab-chat", "Using collaborative editor chat"}
    };
    std::string_view id = node->getID();
    for (auto const& [key, label] : screens) {
        std::string_view suffix(key);
        if (id == suffix || (id.size() > suffix.size() && id.ends_with(suffix) &&
            id[id.size() - suffix.size() - 1] == '/')) return label;
    }
    if (!depth || !node->getChildren()) return {};
    auto* children = node->getChildren();
    for (int i = static_cast<int>(children->count()) - 1; i >= 0 && budget > 0; --i) {
        if (--budget < 0) break;
        auto* child = static_cast<CCNode*>(children->objectAtIndex(i));
        if (!typeinfo_cast<CCLayer*>(child)) continue;
        auto label = featureScreen(child, depth - 1, budget);
        if (!label.empty()) return label;
    }
    return {};
}
void appendSegment(std::string& line, std::string const& segment) {
    if (segment.empty()) return;
    if (!line.empty()) line += " | ";
    line += segment;
}
}

PresenceContext DiscordPresenceManager::captureContext(PresenceConfiguration const& configuration) {
    PresenceContext context;
    auto& payload = context.automatic;
    auto token = [&](std::string key, std::string value) {
        if (!value.empty()) context.tokens.emplace_back(std::move(key), cleanText(value));
    };
    auto screen = [&](std::string key, std::string state, std::string details, std::string image) {
        context.activityKey = std::move(key);
        payload.state = std::move(state);
        payload.details = std::move(details);
        payload.smallImage = std::move(image);
        payload.smallImageText = payload.state;
        token("screen", payload.state);
    };
    if (configuration.flag("discord-rpc-private-mode")) {
        context.category = findSceneLayer<PlayLayer>() ? "gameplay" : findSceneLayer<LevelEditorLayer>() ? "editor" : "menu";
        screen("private", "Private session", "Playing Geometry Dash", "");
        return context;
    }
    if (configuration.flag("discord-rpc-idle-when-unfocused") && !isFocused()) {
        screen("idle", "Away from the game", "Geometry Dash is unfocused", "idle");
        return context;
    }
    auto identifyLevel = [&](GJGameLevel* level) {
        if (!level) return;
        context.levelKey = std::to_string(level->m_levelID.value()) + ":" +
            std::to_string(reinterpret_cast<std::uintptr_t>(level));
        bool sensitive = level->m_unlisted || level->m_levelType == GJLevelType::Editor;
        bool share = !sensitive || configuration.flag("discord-rpc-share-private-levels");
        if (!share) { payload.details = "Private / local level"; return; }
        std::string title;
        if (configuration.flag("discord-rpc-show-level-name")) title = cleanText(std::string(level->m_levelName));
        if (title.empty()) title = "A level";
        payload.details = title;
        if (configuration.flag("discord-rpc-show-level-name")) token("level", title);
        if (configuration.flag("discord-rpc-show-creator")) {
            auto creator = cleanText(std::string(level->m_creatorName));
            if (!creator.empty()) { payload.details += " by " + creator; token("creator", creator); }
        }
        if (configuration.flag("discord-rpc-show-level-id") && level->m_levelID.value() > 0) {
            auto id = std::to_string(level->m_levelID.value());
            token("level_id", id);
            appendSegment(payload.details, "ID " + id);
        }
    };
    if (configuration.flag("discord-rpc-include-paimbnails-features")) {
        int budget = 256;
        auto* scene = activeScene();
        auto feature = featureScreen(scene, 3, budget);
        if (!feature.empty()) { screen(feature, feature, "Paimbnails tools", "paimbnails"); return context; }
        if (findSceneLayer<paidraw::PaiDrawGameLayer>() || findSceneLayer<paidraw::PaiDrawRoomLayer>() ||
            findSceneLayer<paidraw::PaiDrawLobbyLayer>() || findSceneLayer<paidraw::PaiDrawResultsLayer>()) {
            auto session = paidraw::PaiDrawManager::get().activitySnapshot(configuration.flag("discord-rpc-show-room-names"));
            std::string state = findSceneLayer<paidraw::PaiDrawResultsLayer>() ? "Viewing PaiDraw results"
                : findSceneLayer<paidraw::PaiDrawGameLayer>()
                    ? (session.localPlayerIsDrawer ? "Drawing in PaiDraw" : "Guessing in PaiDraw")
                    : session.inRoom ? "Waiting in a PaiDraw room" : "Browsing PaiDraw rooms";
            auto details = session.offlinePreview ? "PaiDraw offline preview" : session.connecting ? "Connecting to PaiDraw"
                : session.connected ? "Online drawing with Paimbnails" : "PaiDraw disconnected";
            screen("paidraw:" + state, state, details, "paidraw");
            if (configuration.flag("discord-rpc-show-room-names")) {
                auto room = cleanText(session.roomName);
                if (!room.empty()) { payload.details = room; token("room", room); }
            }
            if (configuration.flag("discord-rpc-show-party-size") && session.inRoom && session.connected && !session.offlinePreview) {
                context.partySize = session.playerCount;
                context.partyMax = std::max(context.partySize, session.maxPlayers);
                token("players", std::to_string(context.partySize));
            }
            // never expose guesses, the drawing word, passwords or chat messages.
            return context;
        }
        if (findSceneLayer<paimon::menumusic::MenuMusicPopup>()) {
            auto& music = paimon::menumusic::MenuMusicPlayer::get();
            bool paused = music.isPaused();
            bool playing = music.state().isPlaying && !paused;
            screen("menu-music", paused ? "Menu music paused" : playing ? "Listening to menu music" : "Browsing menu music", "Using Menu Music", "music");
            if (configuration.flag("discord-rpc-show-music") && (playing || paused)) {
                if (auto* track = music.currentTrack()) {
                    auto song = cleanText(track->displayName);
                    auto artist = cleanText(track->artist);
                    token("song", song); token("artist", artist);
                    if (!song.empty()) payload.details = song;
                    if (!artist.empty()) appendSegment(payload.details, artist);
                }
            }
            return context;
        }
        if (findSceneLayer<PaimonHubLayer>()) { screen("hub", "Browsing Paimon Hub", "Managing Paimbnails features", "paimon-hub"); return context; }
        if (findSceneLayer<CapturePreviewPopup>()) { screen("capture", "Previewing a thumbnail capture", "Adjusting a new capture", "capture"); return context; }
        if (findSceneLayer<VerificationCenterLayer>()) { screen("moderation", "Reviewing pending media", "Paimbnails verification center", "moderation"); return context; }
        if (findSceneLayer<ProfileSettingsPopup>()) { screen("profile-settings", "Customizing a profile", "Editing profile appearance", "profile"); return context; }
    }
    if (auto* play = findSceneLayer<PlayLayer>()) {
        context.category = "gameplay";
        auto* level = play->m_level;
        bool platformer = level && level->isPlatformer();
        std::string mode = play->m_isPracticeMode ? "Practice" : "Normal";
        std::string action = play->m_hasCompletedLevel ? "Level completed"
            : play->m_isPaused || paimon::hasPauseLayerInScene() ? "Paused"
            : play->m_player1 && play->m_player1->m_isDead ? "Died - retrying"
            : play->m_isPracticeMode ? "Practicing" : "Playing";
        screen("gameplay", action + (platformer ? " / Platformer" : " / " + mode), "In gameplay",
            level && configuration.flag("discord-rpc-show-difficulty") ? resolveDifficultyAsset(level) : "play");
        identifyLevel(level);
        context.levelKey += ":play:" + std::to_string(reinterpret_cast<std::uintptr_t>(play));
        context.activityKey += ":" + context.levelKey + ":" + mode;
        context.attemptKey = context.levelKey + ":" + std::to_string(play->m_attempts);
        token("mode", platformer ? mode + " platformer" : mode);
        if (level && configuration.flag("discord-rpc-show-difficulty")) token("difficulty", resolveDifficultyAsset(level));
        if (configuration.flag("discord-rpc-show-progress") && !platformer) {
            auto progress = play->getCurrentPercent();
            int percent = std::isfinite(progress) ? static_cast<int>(std::clamp(progress, 0.f, 100.f)) : 0;
            token("progress", std::to_string(percent));
            appendSegment(payload.state, std::to_string(percent) + "%");
        }
        if (configuration.flag("discord-rpc-show-attempts")) {
            token("attempts", std::to_string(std::max(0, play->m_attempts)));
            appendSegment(payload.state, "Attempt " + std::to_string(std::max(0, play->m_attempts)));
        }
        if (level && configuration.flag("discord-rpc-show-best") && !platformer) {
            int best = play->m_isPracticeMode ? level->m_practicePercent : level->m_normalPercent.value();
            token("best", std::to_string(std::clamp(best, 0, 100)));
            payload.smallImageText = "Best " + std::to_string(std::clamp(best, 0, 100)) + "% / " + mode;
        }
        return context;
    }
    if (auto* editor = findSceneLayer<LevelEditorLayer>()) {
        context.category = "editor";
        auto testing = editor->m_playbackMode != PlaybackMode::Not;
        screen("editor", testing ? (editor->m_playbackMode == PlaybackMode::Playing ? "Playtesting in the editor" : "Editor playtest paused") : "Editing a level", "Geometry Dash level editor", "editor");
        identifyLevel(editor->m_level);
        context.levelKey += ":editor:" + std::to_string(reinterpret_cast<std::uintptr_t>(editor));
        context.activityKey += ":" + context.levelKey + (testing ? ":playtest" : ":editing");
        token("mode", testing ? "Editor playtest" : "Editor");
        if (configuration.flag("discord-rpc-show-editor-stats") && editor->m_objects) {
            auto count = std::to_string(editor->m_objects->count());
            token("objects", count);
            appendSegment(payload.state, count + " objects");
        }
        return context;
    }
    if (auto* page = findSceneLayer<LevelInfoLayer>()) {
        screen("level-info", "Viewing level information", "Inspecting a level",
            page->m_level && configuration.flag("discord-rpc-show-difficulty") ? resolveDifficultyAsset(page->m_level) : "level-info");
        identifyLevel(page->m_level);
        context.activityKey += ":" + context.levelKey;
        context.levelKey.clear(); // Browsing level info is not a gameplay/editor session.
        return context;
    }
    if (auto* profile = findSceneLayer<ProfilePage>()) {
        bool music = configuration.flag("discord-rpc-include-paimbnails-features") &&
            configuration.flag("discord-rpc-show-music") && ProfileMusicManager::get().isPlaying();
        screen("profile", music ? (ProfileMusicManager::get().isPaused() ? "Profile music paused" : "Listening to profile music")
            : "Viewing a profile", "Browsing community profiles", music ? "music" : "profile");
        if (configuration.flag("discord-rpc-show-profile-names") && profile->m_score) {
            auto name = cleanText(std::string(profile->m_score->m_userName));
            if (!name.empty()) { payload.details = "Viewing " + name; token("profile", name); }
        }
        return context;
    }
    if (auto* browser = findSceneLayer<LevelBrowserLayer>()) {
        std::string list = "Level browser";
        if (auto* search = browser->m_searchObject) {
            if (search->m_searchType == SearchType::MyLevels) list = "My local levels";
            else if (search->m_searchType == SearchType::Featured) list = "Featured levels";
            else if (search->m_searchType == SearchType::Trending) list = "Trending levels";
            else if (search->m_searchType == SearchType::Awarded) list = "Awarded levels";
        }
        screen("browser:" + list, "Browsing levels", list, "browser");
        return context;
    }
    if (findSceneLayer<LevelSearchLayer>()) screen("search", "Searching for levels", "Geometry Dash level search", "search");
    else if (findSceneLayer<CreatorLayer>()) screen("creator", "Browsing online features", "Creator menu", "creator");
    else if (findSceneLayer<LeaderboardsLayer>()) screen("leaderboards", "Browsing leaderboards", "Checking community rankings", "leaderboards");
    else if (findSceneLayer<GauntletSelectLayer>() || findSceneLayer<GauntletLayer>()) screen("gauntlets", "Exploring gauntlets", "Browsing gauntlet levels", "gauntlet");
    else if (findSceneLayer<DailyLevelPage>()) screen("timed-levels", "Checking timed levels", "Daily / weekly / event levels", "daily");
    else if (findSceneLayer<GJGarageLayer>()) screen("garage", "Customizing icons", "Geometry Dash garage", "garage");
    else if (findSceneLayer<GJShopLayer>()) screen("shop", "Visiting a shop", "Browsing Geometry Dash items", "shop");
    else if (findSceneLayer<ChallengesPage>()) screen("quests", "Checking quests", "Reviewing quest progress", "quests");
    else if (findSceneLayer<RewardsPage>()) screen("rewards", "Opening reward chests", "Claiming rewards", "rewards");
    else if (findSceneLayer<SecretLayer>() || findSceneLayer<SecretLayer2>() || findSceneLayer<SecretLayer3>() || findSceneLayer<SecretLayer4>() || findSceneLayer<SecretLayer5>()) screen("secrets", "Exploring secret areas", "Vaults and hidden rooms", "vault");
    else if (findSceneLayer<GJPathsLayer>()) screen("paths", "Browsing paths", "Geometry Dash paths", "paths");
    else if (findSceneLayer<LevelAreaLayer>() || findSceneLayer<LevelAreaInnerLayer>()) screen("tower", "Exploring the Tower", "Geometry Dash story areas", "tower");
    else if (findSceneLayer<LevelSelectLayer>()) screen("official", "Browsing official levels", "Geometry Dash main levels", "main-levels");
    else if (findSceneLayer<MenuLayer>()) screen("menu", "At the main menu", "Geometry Dash with Paimbnails", "menu");
    else screen("other", "Inside Geometry Dash", "Using Paimbnails", "paimbnails");
    return context;
}

bool DiscordPresenceManager::isFocused() const {
#ifdef GEODE_IS_WINDOWS
    auto hwnd = GetForegroundWindow();
    if (!hwnd) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
#else
    // windows-only check: other platforms always report focused.
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

    auto diff = paimon::compat::averageDifficulty(level);
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


} // namespace paimon::discord
