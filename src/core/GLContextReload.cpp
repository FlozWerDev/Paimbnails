#include "GLContextReload.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GameManager.hpp>

#include "../framework/ModEvents.hpp"
#include "../utils/GLSLLoader.hpp"
#include "../utils/DominantColorsGPU.hpp"
#include "../utils/PaimonDrawNode.hpp"
#include "../utils/AnimatedGIFSprite.hpp"
#include "../utils/VideoThumbnailSprite.hpp"
#include "../blur/BlurSystem.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/LocalThumbs.hpp"
#include "../features/profiles/services/ProfileThumbs.hpp"
#include "../features/profiles/services/ProfileImageCache.hpp"
#include "../features/emotes/services/EmoteCache.hpp"
#include "../features/auto-preview/services/AutoPreviewStore.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../features/custom-slider/services/CustomSliderManager.hpp"
#include "../features/progressbar/services/ProgressBarManager.hpp"
#include "../features/rtx/services/RTXRenderer.hpp"
#include "../features/icon-maker/services/IconApplier.hpp"
#include "../features/icon-maker/services/IconThumbs.hpp"
#include "../features/icon-maker/ui/IconMakerUI.hpp"
#include "../features/texture-studio/services/LiveSlotRuntime.hpp"

using namespace geode::prelude;

namespace paimon::glreload {

void onBeforeGameReload() {
    paimon::texture_studio::LiveSlotRuntime::get().onGLContextReload();
    log::info("[GLContextReload] GameManager::reloadAll - soltando texturas y "
              "shaders del mod antes de que se recree el contexto GL");

    // thumbnails: RAM cache and pending queues hold dead textures.
    ThumbnailLoader::get().onGLContextReload();
    LocalThumbs::get().clearTextureCache();

    ProfileThumbs::get().clearPendingDownloads();
    ProfileThumbs::get().clearAllCache();
    clearProfileImgCache();

    paimon::emotes::EmoteCache::get().clearRam();
    paimon::autopreview::AutoPreviewStore::get().clearRamCache();
    AnimatedGIFSprite::clearCacheForReload();
    VideoThumbnailSprite::onGLContextReload();

    // persistent overlays survive the reload scene change.
    CursorManager::get().onGLContextReload();
    PetManager::get().onGLContextReload();

    paimon::rtx::RTXRenderer::get().onGLContextReload();

    LayerBackgroundManager::get().onGLContextReload();
    BlurSystem::getInstance()->onGLContextReload();
    paimon::slider::CustomSliderManager::get().invalidateImageCache();
    ProgressBarManager::get().releaseCustomTextures();
    ProgressBarManager::get().invalidateBaseline();

    paimon::icon_maker::IconApplier::get().onGLContextReload();
    paimon::icon_maker::IconThumbs::get().onGLContextReload();
    paimon::icon_maker::ui::resetCheckerTexture();

    paimon::ThumbnailBackgroundChangedEvent::setLastTexture(nullptr);
    paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID = 0;
    PaimonDrawNode::invalidateWhiteTextureCache();
    DominantColorsGPU::onGLContextReload();

    // mod shaders die with the context; CCShaderCache only rebuilds cocos defaults.
    paimon::shaders::purgeTrackedShaders();
}

} // namespace paimon::glreload

// reloadAll rebuilds GLFW and purges CCTextureCache: run before the original
// so releases still see the old GL context.
class $modify(PaimonGLReloadHook, GameManager) {
    void reloadAll(bool switchingModes, bool toFullscreen, bool borderless, bool fix, bool unused) {
        paimon::glreload::onBeforeGameReload();
        GameManager::reloadAll(switchingModes, toFullscreen, borderless, fix, unused);
    }
};
