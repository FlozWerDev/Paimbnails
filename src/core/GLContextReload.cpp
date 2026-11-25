#include "GLContextReload.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GameManager.hpp>
#if defined(GEODE_IS_ANDROID)
#include <Geode/modify/AppDelegate.hpp>
#include <EGL/egl.h>
#endif

#include "../framework/ModEvents.hpp"
#include "../utils/GLSLLoader.hpp"
#include "../utils/DominantColorsGPU.hpp"
#include "../utils/PaimonDrawNode.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../blur/BlurSystem.hpp"
#include "../blur/PaiblurNode.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profiles/services/ProfileImageCache.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/auto-preview/services/AutoPreviewStore.hpp"
#include "../features/pet/services/PetManager.hpp"
#if defined(GEODE_IS_DESKTOP)
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/cursor/services/CursorShopImages.hpp"
#endif
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/backgrounds/services/WallpaperThumbnailCache.hpp"
#include "../features/backgrounds/ui/LiveWallpaperNode.hpp"
#include "../features/backgrounds/ui/SceneWallpaperNode.hpp"
#include "../features/custom-slider/services/CustomSliderManager.hpp"
#include "../features/progressbar/services/ProgressBarManager.hpp"
#include "../features/rtx/services/RTXRenderer.hpp"
#include "../features/icon-maker/services/IconApplier.hpp"
#include "../features/icon-maker/services/IconThumbs.hpp"
#include "../features/icon-maker/ui/IconMakerUI.hpp"
#include "../features/texture-studio/services/LiveSlotRuntime.hpp"
#include "../features/liquid-glass/services/GlassNode.hpp"
#include "../features/dynamic-ambient/services/AmbientNode.hpp"
#include "../features/button-skins/services/ButtonSkinRuntime.hpp"
#include "../features/volume-scroll/services/VolumeScrollManager.hpp"
#include "../features/menu-music/ui/components/VinylDisc.hpp"
#include "../features/thread-images/ChatImageService.hpp"

using namespace geode::prelude;

namespace paimon::glreload {

namespace {

void resetModGLState(bool oldContextStillAlive) {
    paimon::texture_studio::LiveSlotRuntime::get().onGLContextReload();
    log::info("[GLContextReload] soltando texturas y shaders del mod (contextAlive={})",
              oldContextStillAlive);

    // thumbnails: ram cache and pending queues hold dead textures.
    ThumbnailLoader::get().onGLContextReload();
    LocalThumbs::get().clearTextureCache();

    ProfileThumbs::get().clearPendingDownloads();
    ProfileThumbs::get().clearAllCache();
    clearProfileImgCache();

    paimon::emotes::EmoteCache::get().clearRam();
    paimon::autopreview::AutoPreviewStore::get().clearRamCache();
    AnimatedGIFSprite::clearCacheForReload();
    VideoThumbnailSprite::onGLContextReload();
    paimon::chatimg::ChatImageService::get().clearTextureCache();

    // persistent overlays survive the reload scene change.
#if defined(GEODE_IS_DESKTOP)
    CursorManager::get().onGLContextReload();
    paimon::cursorshop::ShopImages::get().clear();
#endif
    PetManager::get().onGLContextReload();
    paimon::volscroll::VolumeScrollManager::get().releaseSharedResources();
    paimon::menumusic::VinylDisc::clearTextureCache();

    paimon::rtx::RTXRenderer::get().onGLContextReload(oldContextStillAlive);

    LayerBackgroundManager::get().onGLContextReload();
    paimon::wallpaper::LiveWallpaperNode::onGLContextReload();
    paimon::wallpaper::SceneWallpaperNode::onGLContextReload();
    paimon::wallpaper::WallpaperThumbnailCache::get().clearTextures();
    paimon::paiblur::PaiblurNode::onGLContextReloadAll(oldContextStillAlive);
    BlurSystem::getInstance()->onGLContextReload();
    paimon::glass::GlassNode::onGLContextReloadAll();
    paimon::ambient::AmbientNode::onGLContextReloadAll();
    paimon::slider::CustomSliderManager::get().invalidateImageCache();
    ProgressBarManager::get().releaseCustomTextures();
    ProgressBarManager::get().invalidateBaseline();

    paimon::icon_maker::IconApplier::get().onGLContextReload();
    paimon::icon_maker::IconThumbs::get().onGLContextReload();
    paimon::icon_maker::ui::resetCheckerTexture();
    paimon::buttonskin::ButtonSkinRuntime::get().onGLContextReload();

    paimon::ThumbnailBackgroundChangedEvent::setLastTexture(nullptr);
    paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID = 0;
    PaimonDrawNode::invalidateWhiteTextureCache();
    DominantColorsGPU::onGLContextReload();

    // mod shaders die with the context; ccshadercache only rebuilds cocos defaults.
    paimon::shaders::purgeTrackedShaders();
}

} // namespace

void onBeforeGameReload() {
    resetModGLState(true);
}

void onContextLost() {
    resetModGLState(false);
}

#if defined(GEODE_IS_ANDROID)
void syncAndroidContext() {
    static void const* s_lastEglContext = nullptr;
    if (void const* cur = static_cast<void const*>(eglGetCurrentContext())) {
        if (s_lastEglContext && s_lastEglContext != cur) {
            onContextLost();
        }
        s_lastEglContext = cur;
    }
}
#endif

} // namespace paimon::glreload

// reloadall rebuilds glfw and purges cctexturecache: run before the original
// so releases still see the old gl context.
class $modify(PaimonGLReloadHook, GameManager) {
    void reloadAll(bool switchingModes, bool toFullscreen, bool borderless, bool fix, bool unused) {
        paimon::glreload::onBeforeGameReload();
        GameManager::reloadAll(switchingModes, toFullscreen, borderless, fix, unused);
    }
};

#if defined(GEODE_IS_ANDROID)
class $modify(PaimonAndroidForegroundReloadHook, AppDelegate) {
    void applicationWillEnterForeground() {
        AppDelegate::applicationWillEnterForeground();
        paimon::glreload::syncAndroidContext();
    }
};

$on_game(TexturesLoaded) {
    paimon::glreload::syncAndroidContext();
}
#endif
