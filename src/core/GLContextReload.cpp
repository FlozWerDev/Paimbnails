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
#include "../features/icon-maker/services/IconApplier.hpp"
#include "../features/icon-gallery/services/GalleryStore.hpp"
#include "../features/icon-maker/services/IconThumbs.hpp"

using namespace geode::prelude;

namespace paimon::glreload {

void onBeforeGameReload() {
    log::info("[GLContextReload] GameManager::reloadAll - soltando texturas y "
              "shaders del mod antes de que se recree el contexto GL");

    // Thumbnails: cache RAM + colas pendientes que retienen texturas viejas.
    ThumbnailLoader::get().onGLContextReload();
    LocalThumbs::get().clearTextureCache();

    // Perfiles.
    ProfileThumbs::get().clearPendingDownloads();
    ProfileThumbs::get().clearAllCache();
    clearProfileImgCache();

    // Emotes / previews / GIFs / videos.
    paimon::emotes::EmoteCache::get().clearRam();
    paimon::autopreview::AutoPreviewStore::get().clearRamCache();
    AnimatedGIFSprite::clearCacheForReload();
    VideoThumbnailSprite::onGLContextReload();

    // Overlays persistentes (sobreviven al cambio de escena del reload).
    CursorManager::get().onGLContextReload();
    PetManager::get().onGLContextReload();

    // Fondos / blur / slider / barra de progreso.
    LayerBackgroundManager::get().onGLContextReload();
    BlurSystem::getInstance()->onGLContextReload();
    paimon::slider::CustomSliderManager::get().invalidateImageCache();
    ProgressBarManager::get().releaseCustomTextures();
    ProgressBarManager::get().invalidateBaseline();

    // Iconos creados con el Icon Maker (texturas + frames registrados).
    paimon::icon_maker::IconApplier::get().onGLContextReload();
    paimon::icon_maker::IconThumbs::get().onGLContextReload();

    // Vistas previas de la tienda de iconos (texturas creadas desde PNG en RAM).
    paimon::icon_gallery::GalleryStore::get().onGLContextReload();

    // Estáticos sueltos.
    paimon::ThumbnailBackgroundChangedEvent::setLastTexture(nullptr);
    paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID = 0;
    PaimonDrawNode::invalidateWhiteTextureCache();
    DominantColorsGPU::onGLContextReload();

    // GL programs compilados por el mod: mueren con el contexto y CCShaderCache
    // solo recompila los default de cocos, no los nuestros.
    paimon::shaders::purgeTrackedShaders();
}

} // namespace paimon::glreload

// GD ejecuta reloadAll al aplicar cambios de resolución, fullscreen/windowed o
// calidad de texturas: recrea la ventana GLFW (nuevo contexto GL) y purga
// CCTextureCache. Se despacha ANTES del original, con el contexto viejo aún
// activo, para que los release() hagan glDeleteTextures sobre names propios y
// ningún cache sirva texturas muertas a las escenas nuevas.
class $modify(PaimonGLReloadHook, GameManager) {
    void reloadAll(bool switchingModes, bool toFullscreen, bool borderless, bool fix, bool unused) {
        paimon::glreload::onBeforeGameReload();
        GameManager::reloadAll(switchingModes, toFullscreen, borderless, fix, unused);
    }
};
