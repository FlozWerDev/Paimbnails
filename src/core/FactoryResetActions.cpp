#include "FactoryResetActions.hpp"
#include <Geode/utils/string.hpp>
#include "Settings.hpp"
#include "SettingsMigration.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/beat-shaders/services/BeatShaderManager.hpp"
#include "../features/colorful-icons/services/IconConfigStore.hpp"
#include "../features/custom-slider/services/CustomSliderManager.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/emotes/services/EmoteService.hpp"
#include "../features/main-menu-layout/services/MainMenuLayoutManager.hpp"
#include "../features/profile-music/services/ProfileMusicManager.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/progressbar/services/ProgressBarManager.hpp"
#include "../features/quick-hub/services/QuickHubManager.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/Localization.hpp"
#include "../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>

#include <filesystem>

extern void clearProfileImgCache();

using namespace geode::prelude;

namespace {
bool shouldPreserveSaveEntry(std::string const& name) {
    auto lower = geode::utils::string::toLower(name);
    return lower == "yt-dlp" || lower == "ffmpeg";
}

void wipeDirectoryContents(std::filesystem::path const& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return;

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        auto name = geode::utils::string::pathToString(entry.path().filename());
        if (shouldPreserveSaveEntry(name)) continue;
        std::filesystem::remove_all(entry.path(), ec);
        ec.clear();
    }
}

void clearRuntimeCaches() {
    ThumbnailLoader::get().cleanup();
    ThumbnailLoader::get().clearPendingQueue();
    ThumbnailLoader::get().clearCache();
    ThumbnailLoader::get().clearDiskCache();

    ProfileThumbs::get().clearPendingDownloads();
    ProfileThumbs::get().clearNoProfileCache();
    ProfileThumbs::get().clearAllCache();

    ProfileMusicManager::get().stopProfileMusic();
    ProfileMusicManager::get().stopPreview();
    ProfileMusicManager::get().clearCache();

    AnimatedGIFSprite::clearCache();
    clearProfileImgCache();
    HttpClient::get().cleanTasks();

    paimon::emotes::EmoteCache::get().clearAll();
    paimon::emotes::EmoteService::get().clearCatalog();
}

void resetModJsonSettings() {
    auto* mod = Mod::get();
    if (!mod->hasSettings()) return;

    for (auto const& key : mod->getSettingKeys()) {
        if (auto setting = mod->getSetting(key)) {
            setting->reset();
        }
    }
}

void resetLayerBackgrounds() {
    auto& bgMgr = LayerBackgroundManager::get();
    LayerBgConfig defaultCfg;
    LayerMusicConfig defaultMusic;

    for (auto const& [key, _] : LayerBackgroundManager::LAYER_OPTIONS) {
        bgMgr.saveConfig(key, defaultCfg);
        bgMgr.saveMusicConfig(key, defaultMusic);
    }
    bgMgr.saveGlobalMusicConfig(defaultMusic);

    auto* mod = Mod::get();
    mod->setSavedValue("layerbg-migrated-v2", true);
    mod->setSavedValue("layerbg-assets-migrated-v1", true);
    mod->setSavedValue("layermusic-migrated-global", true);
    mod->setSavedValue<float>("layerbg-shader-intensity", 0.5f);
    mod->setSavedValue("bg-adaptive-colors", true);
    mod->setSavedValue("menu-immersive-mode", false);
    mod->setSavedValue("bg-dark-mode", false);
    mod->setSavedValue("bg-dark-intensity", 0.5f);
    mod->setSavedValue<std::string>("profile-bg-type", "none");
    mod->setSavedValue<std::string>("profile-bg-path", "");

    mod->setSavedValue("video-fps-limit", 30);
    mod->setSavedValue("video-quality", 0);
    mod->setSavedValue("video-audio-enabled", false);
    mod->setSavedValue<std::string>("video-blur-type", "none");
    mod->setSavedValue("video-blur-intensity", 0.5f);
    mod->setSavedValue("video-rotation", 0);
    mod->setSavedValue("video-adaptive-fps", true);
    mod->setSavedValue("video-min-fps", 12);
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    mod->setSavedValue("video-max-chunk-memory-mb", 256);
    mod->setSavedValue("video-max-concurrent", 2);
#else
    mod->setSavedValue("video-max-chunk-memory-mb", 512);
    mod->setSavedValue("video-max-concurrent", 4);
#endif
    // Drop videoMaxDecodeDimension / adaptiveSpriteFPS snapshots so the next
    // decoder open sees factory-reset quality/FPS instead of pre-reset values.
    paimon::settings::internal::invalidateSettingsCache();

    mod->setSavedValue("thumbnail-disk-cache", matjson::Value::object());
}

void resetBeatShaders() {
    auto& beatMgr = paimon::beat_shaders::BeatShaderManager::get();
    paimon::beat_shaders::BeatShaderConfig cfg;
    beatMgr.saveConfig(cfg);
    for (auto const& [layer, _] : beatMgr.availableLayers()) {
        beatMgr.setLayerEnabled(layer, true);
    }
}

void resetFeatureManagers() {
    paimon::menu_layout::MainMenuLayoutManager::get().load();
    paimon::menu_layout::MainMenuLayoutManager::get().resetAll();

    paimon::slider::CustomSliderManager::get().loadConfig();
    paimon::slider::CustomSliderManager::get().resetToDefaults();

    paimon::icons::IconConfigStore::get().resetToDefaults();

    ProgressBarManager::get().resetToDefaults();

    paimon::quickhub::QuickHubManager::get().resetToDefault();
    paimon::quickhub::QuickHubManager::get().setHoldCtrlEnabled(true);

    auto& tm = TransitionManager::get();
    tm.setGlobalConfig(TransitionConfig{});
    tm.clearLevelEntryConfig();
    tm.setEnabled(true);
    tm.saveConfig();
}

void refreshMenuIfVisible() {
    TransitionManager::get().replaceScene(MenuLayer::scene(false));
}
} // namespace

namespace paimon::factory_reset {

void execute() {
    log::info("[FactoryReset] Starting full settings reset");

    clearRuntimeCaches();

    auto* mod = Mod::get();
    auto saveDir = mod->getSaveDir();
    auto configDir = mod->getConfigDir();

    std::error_code ec;
    std::filesystem::remove(saveDir / "saved.json", ec);
    ec.clear();

    wipeDirectoryContents(saveDir);
    wipeDirectoryContents(configDir);

    (void)mod->loadData();

    resetModJsonSettings();
    paimon::settings::forceResetSavedValuesToDefaults();
    resetLayerBackgrounds();
    resetBeatShaders();
    resetFeatureManagers();

    Localization::get().setLanguage(
        Localization::languageFromId(mod->getSettingValue<std::string>("language")),
        false
    );

    if (auto result = mod->saveData(); result.isErr()) {
        log::warn("[FactoryReset] saveData failed: {}", result.unwrapErr());
    }

    refreshMenuIfVisible();

    log::info("[FactoryReset] Completed");
    PaimonNotify::create(
        "Ajustes reiniciados. El menu se ha recargado con valores por defecto.",
        NotificationIcon::Success
    )->show();
}

void requestWithConfirmation() {
    auto popup = PopupManager::get().quickPopup(
        "Reiniciar ajustes",
        "Esto <cr>borrara TODA la configuracion</c> del mod:\n"
        "fondos, layout del menu, shaders, sliders,\n"
        "Discord, miniaturas guardadas y mas.\n\n"
        "<cy>No se puede deshacer.</c>\n"
        "Usalo para comprobar si un bug viene de ajustes guardados.",
        "Cancelar",
        "Reiniciar todo",
        [](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            execute();
        }
    );
    popup.blockClosingFor(0.6f);
    popup.showInstant();
}

} // namespace paimon::factory_reset