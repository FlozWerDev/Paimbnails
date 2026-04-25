#include "FramebufferCapture.hpp"
#include "../../../core/Settings.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <Geode/cocos/kazmath/include/kazmath/mat4.h>
#include <Geode/binding/ShaderLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <array>
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <cstring>

using namespace geode::prelude;
using namespace cocos2d;

// Forward declarations for scaling helpers (defined at bottom of file)
static std::shared_ptr<uint8_t> bilinearDownscale(
    uint8_t const* src, int srcW, int srcH, int dstW, int dstH);

// respaldo: algunos headers gl no exponen gl_read_framebuffer
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER GL_FRAMEBUFFER
#endif

// respaldo para enums de estado de mezcla
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB 0x80C8
#endif

// ─────────────────────────────────────────────────────────────
// inicializacion miembros estaticos
// ─────────────────────────────────────────────────────────────
FramebufferCapture::CaptureRequest FramebufferCapture::s_request;
std::vector<FramebufferCapture::DeferredCallback> FramebufferCapture::s_deferredCallbacks;
bool FramebufferCapture::s_isCapturing = false;
int  FramebufferCapture::s_captureW   = 0;
int  FramebufferCapture::s_captureH   = 0;
int  FramebufferCapture::s_maxTextureSize = 0;

bool FramebufferCapture::isCapturing() { return s_isCapturing; }

 std::pair<int, int> FramebufferCapture::getCaptureSize() {
    return { s_captureW, s_captureH };
}

int FramebufferCapture::getMaxTextureSize() {
    if (s_maxTextureSize <= 0) {
        GLint maxTex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        s_maxTextureSize = (maxTex > 0) ? static_cast<int>(maxTex) : 4096;
        log::info("[FramebufferCapture] GPU GL_MAX_TEXTURE_SIZE = {}", s_maxTextureSize);
    }
    return s_maxTextureSize;
}

// ─────────────────────────────────────────────────────────────
// captureguard - raii: establece isCapturing + captureSize
// ─────────────────────────────────────────────────────────────
FramebufferCapture::CaptureGuard::CaptureGuard(int w, int h) {
    s_isCapturing = true;
    s_captureW    = w;
    s_captureH    = h;
    log::debug("[FramebufferCapture] CaptureGuard ON  ({}x{})", w, h);
}
FramebufferCapture::CaptureGuard::~CaptureGuard() {
    s_isCapturing = false;
    s_captureW    = 0;
    s_captureH    = 0;
    log::debug("[FramebufferCapture] CaptureGuard OFF");
}

// ─────────────────────────────────────────────────────────────
// glstateguard - raii: guarda y restaura estado gl critico
// ─────────────────────────────────────────────────────────────
FramebufferCapture::GLStateGuard::GLStateGuard() {
    GLint vp[4] = {};
    glGetIntegerv(GL_VIEWPORT, vp);
    m_viewport[0] = vp[0]; m_viewport[1] = vp[1];
    m_viewport[2] = vp[2]; m_viewport[3] = vp[3];
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    m_fbo = static_cast<int>(fbo);
    m_blend = glIsEnabled(GL_BLEND) == GL_TRUE;
    GLint src = 0, dst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    m_blendSrc = static_cast<int>(src);
    m_blendDst = static_cast<int>(dst);
    m_scissor = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    m_depthTest = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
    m_stencilTest = glIsEnabled(GL_STENCIL_TEST) == GL_TRUE;
}
FramebufferCapture::GLStateGuard::~GLStateGuard() {
    glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_fbo));
    if (m_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(static_cast<GLenum>(m_blendSrc), static_cast<GLenum>(m_blendDst));
    if (m_scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (m_depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (m_stencilTest) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
}

// ─────────────────────────────────────────────────────────────
// auxiliar configuracion calidad
// ─────────────────────────────────────────────────────────────
static CaptureQualitySettings getQualitySettings() {
    CaptureQualitySettings settings;

    // Determine target width from the capture-resolution setting
    std::string res = geode::Mod::get()->getSettingValue<std::string>("capture-resolution");
    if (res == "4k")         settings.targetWidth = 3840;
    else if (res == "1440p") settings.targetWidth = 2560;
    else                     settings.targetWidth = 1920; // default 1080p

    // No supersampling (1:1) — rendering directly at target resolution
    // avoids the massive cost of rendering at 2x then CPU-downscaling.
    // GD's rendering pipeline already handles antialiasing.
    settings.supersampleFactor    = 1;
    settings.useAntialiasing      = true;
    settings.highQualityFiltering = true;

    log::info(
        "[FramebufferCapture] Capture resolution '{}' -> Target Width: {}",
        res,
        settings.targetWidth
    );

    return settings;
}

// ─────────────────────────────────────────────────────────────
// api publica
// ─────────────────────────────────────────────────────────────
void FramebufferCapture::requestCapture(
    int levelID,
    geode::CopyableFunction<void(bool, CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback,
    CCNode* nodeToCapture
) {
    log::info("[FramebufferCapture] Capture requested for level {}", levelID);

    if (s_request.active) {
        log::warn("[FramebufferCapture] A capture is already pending – replacing it");
    }

    s_request.levelID       = levelID;
    s_request.callback      = std::move(callback);
    s_request.nodeToCapture = nodeToCapture;
    s_request.active        = true;
}

void FramebufferCapture::cancelPending() {
    if (s_request.active) {
        log::info("[FramebufferCapture] Pending capture cancelled");
    }
    // siempre liberar el callback para soltar cualquier Ref<> capturado
    // (la lambda puede sobrevivir despues de ejecutar si solo se puso active=false)
    s_request.active   = false;
    s_request.callback = nullptr;
    s_request.nodeToCapture = nullptr;
    // limpiar deferred callbacks pendientes para evitar que se ejecuten
    // despues de cancelar (los nodos referenciados pueden ya no existir)
    s_deferredCallbacks.clear();
}

bool FramebufferCapture::hasPendingCapture() {
    return s_request.active;
}

// ─────────────────────────────────────────────────────────────
// executeIfPending - despacho principal
//
// estrategia manejo shaders:
//   shaderlayer en gd redirige todo el renderizado a su
//   fbo interno, aplica shaders post-proceso y luego
//   compone resultado en pantalla. si ocultamos
//   shaderlayer y llamamos scene->visit() en nuestro fbo,
//   contenido va al fbo de shaderlayer (que no podemos leer)
//   asi que nuestro fbo termina negro o con contenido
//   aplastado en esquina por error coordenadas.
//
//   por tanto:
//     • con shaders activos usamos captura directa
//       (lee back-buffer con frame final compuesto
//       y shaders aplicados correctamente).
//     • doCaptureDirectWithScale ya recurre a
//       doCaptureRerender si falla lectura back-buffer.
//     • sin shaders activos, usamos doCaptureRerender
//       para captura alta resolucion.
// ─────────────────────────────────────────────────────────────

// auxiliar: detecta si shaderlayer tiene shaders activos
static bool hasActiveShaders() {
    auto* playLayer = PlayLayer::get();
    if (!playLayer) return false;

    auto* shaderLayer = playLayer->m_shaderLayer;
    if (!shaderLayer) return false;

    // shaderlayer activo si tiene textura render interna
    if (shaderLayer->m_renderTexture != nullptr) {
        log::info("[FramebufferCapture] ShaderLayer detected with active render texture");
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────
// hideNonVanillaUI
//
// oculta todo elemento que NO sea parte del gameplay vanilla:
// - m_uiLayer, m_attemptLabel, m_percentageLabel
// - cualquier hijo de PlayLayer con ID/clase de UI/overlays
// - nodos de mods (IDs con '/' = geode mod prefix)
// - CCMenu, CCLabelBMFont sueltos fuera del objectLayer
// - pauselayers, alertas, popups en la escena
//
// devuelve vector de nodos ocultados para restaurar despues.
// ─────────────────────────────────────────────────────────────
static std::vector<std::pair<CCNode*, bool>> hideNonVanillaUI() {
    std::vector<std::pair<CCNode*, bool>> hidden;

    auto* director = CCDirector::sharedDirector();
    if (!director) return hidden;

    auto* scene = director->getRunningScene();
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return hidden;

    auto hideNode = [&](CCNode* node) {
        if (node && node->isVisible()) {
            hidden.push_back({node, true});
            node->setVisible(false);
        }
    };

    // 1. vanilla UI directos
    hideNode(pl->m_uiLayer);
    hideNode(pl->m_attemptLabel);
    hideNode(pl->m_percentageLabel);

    // 2. set de punteros vanilla que NO debemos tocar
    std::unordered_set<CCNode*> vanillaKeep;
    vanillaKeep.insert(pl->m_player1);
    vanillaKeep.insert(pl->m_player2);
    vanillaKeep.insert(pl->m_objectLayer);
    if (pl->m_shaderLayer) vanillaKeep.insert(pl->m_shaderLayer);
    if (pl->m_uiLayer)     vanillaKeep.insert(pl->m_uiLayer);      // ya oculto
    if (pl->m_attemptLabel)    vanillaKeep.insert(pl->m_attemptLabel);
    if (pl->m_percentageLabel) vanillaKeep.insert(pl->m_percentageLabel);
    // game sections (background, middleground, ground, etc.)
    vanillaKeep.insert(pl->m_background);
    vanillaKeep.insert(pl->m_middleground);
    vanillaKeep.insert(pl->m_groundLayer);
    vanillaKeep.insert(pl->m_groundLayer2);
    if (pl->m_inShaderObjectLayer)    vanillaKeep.insert(pl->m_inShaderObjectLayer);
    if (pl->m_aboveShaderObjectLayer) vanillaKeep.insert(pl->m_aboveShaderObjectLayer);
    // m_batchNodes is a CCArray* containing all gameplay batch nodes
    if (pl->m_batchNodes) {
        for (auto* bn : CCArrayExt<CCNode*>(pl->m_batchNodes)) {
            if (bn) vanillaKeep.insert(bn);
        }
    }
    vanillaKeep.erase(nullptr);

    // patrones de ID que indican elementos no-gameplay
    static const std::vector<std::string> uiPatterns = {
        "ui", "uilayer", "pause", "menu", "dialog", "popup", "editor",
        "notification", "btn", "button", "overlay", "checkpoint", "fps",
        "debug", "attempt", "percent", "progress", "bar", "score",
        "practice", "hitbox", "trajectory", "status", "info", "label",
        "hud", "indicator", "counter", "timer", "stat", "cheat",
        "noclip", "speedhack", "startpos", "testmode", "macro"
    };

    // 3. recorre hijos directos de PlayLayer
    for (auto* child : CCArrayExt<CCNode*>(pl->getChildren())) {
        if (!child || !child->isVisible()) continue;
        if (vanillaKeep.count(child)) continue;

        // player objects nunca
        if (typeinfo_cast<PlayerObject*>(child)) continue;

        // batch nodes / spritebatchnode = gameplay visual
        if (typeinfo_cast<CCSpriteBatchNode*>(child)) continue;

        // particle systems = efectos del nivel
        if (typeinfo_cast<CCParticleSystem*>(child)) continue;

        std::string nid = child->getID();
        auto nidL = geode::utils::string::toLower(nid);
        std::string cls = typeid(*child).name();
        auto clsL = geode::utils::string::toLower(cls);

        bool shouldHide = false;

        // IDs con '/' son de mods (geode "modid/nodeid")
        if (nid.find('/') != std::string::npos) {
            shouldHide = true;
        }

        // IDs que matcheen patrones UI
        if (!shouldHide && !nidL.empty()) {
            for (auto const& p : uiPatterns) {
                if (nidL.find(p) != std::string::npos) {
                    shouldHide = true;
                    break;
                }
            }
        }

        // clases que son UI
        if (!shouldHide) {
            if (typeinfo_cast<CCMenu*>(child) ||
                typeinfo_cast<CCLabelBMFont*>(child) ||
                typeinfo_cast<CCLabelTTF*>(child)) {
                shouldHide = true;
            }
        }

        // clases conocidas de overlays de mods
        if (!shouldHide) {
            if (clsL.find("uilayer") != std::string::npos ||
                clsL.find("progress") != std::string::npos ||
                clsL.find("status") != std::string::npos ||
                clsL.find("overlay") != std::string::npos ||
                clsL.find("hitbox") != std::string::npos ||
                clsL.find("trajectory") != std::string::npos ||
                clsL.find("noclip") != std::string::npos ||
                clsL.find("debugdraw") != std::string::npos) {
                shouldHide = true;
            }
        }

        // z-order alto suele ser overlay
        if (!shouldHide && child->getZOrder() >= 10) {
            // solo si no es un tipo gameplay conocido
            if (!typeinfo_cast<GJBaseGameLayer*>(child)) {
                shouldHide = true;
            }
        }

        if (shouldHide) {
            hidden.push_back({child, true});
            child->setVisible(false);
        }
    }

    // 4. recorre hijos de la escena (pauselayers, alertas, overlays)
    if (scene) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!child || !child->isVisible()) continue;
            if (child == pl) continue;

            auto cls = typeid(*child).name();
            std::string nid = child->getID();
            bool hide = false;

            if (typeinfo_cast<FLAlertLayer*>(child))        hide = true;
            else if (nid.find("pause") != std::string::npos ||
                     nid.find("Pause") != std::string::npos) hide = true;
            else if (strstr(cls, "PauseLayer"))              hide = true;
            else if (nid.find("paimon-loading") != std::string::npos) hide = true;
            else if (nid.find('/') != std::string::npos)     hide = true; // nodos de mods en escena
            else if (child->getZOrder() > 100)               hide = true;

            if (hide) {
                hidden.push_back({child, true});
                child->setVisible(false);
            }
        }
    }

    log::info("[FramebufferCapture] hideNonVanillaUI: hidden {} nodes", hidden.size());
    return hidden;
}

// ─────────────────────────────────────────────────────────────
// restoreHiddenNodes — restaura visibilidad
// ─────────────────────────────────────────────────────────────
static void restoreHiddenNodes(std::vector<std::pair<CCNode*, bool>>& hidden) {
    for (auto& pair : hidden) {
        pair.first->setVisible(pair.second);
    }
    hidden.clear();
}

// ─────────────────────────────────────────────────────────────
// executeIfPending
// ─────────────────────────────────────────────────────────────
void FramebufferCapture::executeIfPending() {
    if (!s_request.active) return;

    // ── captura nodo (caso especial) ──────────────────────────
    if (s_request.nodeToCapture) {
        doCaptureNode(s_request.nodeToCapture);
        s_request.active = false;
        return;
    }

    // ── configuracion ────────────────────────────────────────
    auto quality = getQualitySettings();
    int targetW  = quality.targetWidth;

    // asegura ancho minimo render (1080p)
    int renderWidth = std::max(1920, targetW);

    // aplica supersampling
    int supersampleWidth = renderWidth * quality.supersampleFactor;
    int clampedSuperW    = std::max(1920, std::min(15360, supersampleWidth));

    // limita a maximo gpu temprano
    int maxTex = getMaxTextureSize();
    clampedSuperW = std::min(clampedSuperW, maxTex);

    // obtiene viewport
    auto* director = CCDirector::sharedDirector();
    int vpW = 0, vpH = 0;
    if (director && director->getOpenGLView()) {
        auto frameSize = director->getOpenGLView()->getFrameSize();
        vpW = static_cast<int>(frameSize.width);
        vpH = static_cast<int>(frameSize.height);
    }

    // ── elige estrategia segun shaders activos ──────
    bool shadersActive = hasActiveShaders();

    if (shadersActive) {
        log::info("[FramebufferCapture] Shaders ACTIVE – using direct capture (back-buffer): "
                  "Target={}px, Viewport={}x{}", clampedSuperW, vpW, vpH);
        doCaptureDirectWithScale(clampedSuperW, vpW, vpH, quality);
    } else {
        log::info("[FramebufferCapture] No shaders – using rerender capture: "
                  "Target={}px (SS={}px), Viewport={}x{}", renderWidth, clampedSuperW, vpW, vpH);
        doCaptureRerender(clampedSuperW, vpW, vpH, quality);
    }

    s_request.active = false;
}

// ─────────────────────────────────────────────────────────────
// procesa callbacks diferidos
// ─────────────────────────────────────────────────────────────
void FramebufferCapture::processDeferredCallbacks() {
    if (s_deferredCallbacks.empty()) return;

    log::debug("[FramebufferCapture] Processing {} deferred callbacks", s_deferredCallbacks.size());

    for (auto& d : s_deferredCallbacks) {
        if (d.callback) {
            // validacion de textura sin excepciones — verificar puntero directamente
            if (d.texture && d.texture->getPixelsWide() == 0) {
                d.texture = nullptr;
                d.success = false;
            }
            d.callback(d.success, d.texture, d.rgbaData, d.width, d.height);
        }
        if (d.texture) {
            d.texture->release();
        }
    }

    s_deferredCallbacks.clear();
}

// ─────────────────────────────────────────────────────────────
// auxiliar: crea buffer rgba, alpha->255
// ─────────────────────────────────────────────────────────────
static std::shared_ptr<uint8_t> makeRGBABuffer(const unsigned char* data, int W, int H) {
    size_t pixelCount = static_cast<size_t>(W) * static_cast<size_t>(H);
    size_t bytes      = pixelCount * 4;
    std::shared_ptr<uint8_t> buf(new uint8_t[bytes], std::default_delete<uint8_t[]>());
    uint8_t* dst = buf.get();

    // Fast memcpy + alpha fixup (avoids per-pixel branch for RGB)
    std::memcpy(dst, data, bytes);
    for (size_t i = 3; i < bytes; i += 4) {
        dst[i] = 255; // force alpha opaque
    }
    return buf;
}

// ─────────────────────────────────────────────────────────────
// auxiliar: crea y retiene cctexture2d de buffer rgba
// devuelve nullptr si falla
// ─────────────────────────────────────────────────────────────
static CCTexture2D* createTextureFromRGBA(const uint8_t* data, int W, int H) {
    auto* tex = new CCTexture2D();
    if (!tex->initWithData(data, kCCTexture2DPixelFormat_RGBA8888, W, H,
                           CCSize(static_cast<float>(W), static_cast<float>(H)))) {
        log::error("[FramebufferCapture] Failed to create CCTexture2D ({}x{})", W, H);
        tex->release();
        return nullptr;
    }
    tex->setAntiAliasTexParameters();
    tex->retain();
    return tex;
}

// ─────────────────────────────────────────────────────────────
// auxiliar: chequea si buffer es color uniforme
// devuelve true si imagen tiene contenido variado
// ─────────────────────────────────────────────────────────────
static bool pixelBufferHasContent(std::vector<uint8_t> const& pixels, int width, int height) {
    if (pixels.size() < 4) return false;

    uint8_t r0 = pixels[0], g0 = pixels[1], b0 = pixels[2];
    // muestrea cada ~97 pixeles (primo para buena distribucion)
    size_t step = static_cast<size_t>(4) * 97;
    size_t limit = pixels.size() - 3; // -3 asegura i+2 valido
    for (size_t i = step; i < limit; i += step) {
        if (pixels[i] != r0 || pixels[i + 1] != g0 || pixels[i + 2] != b0) {
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// doCaptureDirectWithScale
//
// lee pixeles del back-buffer en swapBuffers
// captura todo en pantalla, incluyendo shaders
// luego escala lanczos-3 a ancho objetivo
//
// estrategia:
//   1. intenta leer fbo actual (lo que mostrara swapBuffers)
//   2. si uniforme, prueba fbo 0 (framebuffer estandar)
//   3. si sigue uniforme, recurre a rerender
// ─────────────────────────────────────────────────────────────
void FramebufferCapture::doCaptureDirectWithScale(int targetWidth, int viewportW, int viewportH, CaptureQualitySettings const& quality) {
    auto* director = CCDirector::sharedDirector();
        if (!director) {
            log::error("[FramebufferCapture] CCDirector is null");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        auto* scene = director->getRunningScene();

        // ── oculta todo lo que no sea gameplay vanilla ────────────
        auto hiddenNodes = hideNonVanillaUI();

        log::info("[FramebufferCapture] Direct capture: hidden {} UI overlay nodes", hiddenNodes.size());

        // ── fuerza re-render de la escena al back-buffer ─────────
        // Esto asegura que los pixeles que leemos NO tengan UI,
        // independientemente del estado del back-buffer previo.
        if (scene) {
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            scene->visit();
        }

        // ── reune valores tamano para diagnostico ──
        auto* glView = director->getOpenGLView();
        CCSize frameSize = glView ? glView->getFrameSize() : CCSize(0, 0);
        CCSize winPixels = director->getWinSizeInPixels();
        CCSize winSize   = director->getWinSize();

        GLint currentViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, currentViewport);

        GLint currentFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

        log::info("[FramebufferCapture] Direct capture diagnostics:");
        log::info("  frameSize      = {}x{}", frameSize.width, frameSize.height);
        log::info("  winSizeInPx    = {}x{}", winPixels.width, winPixels.height);
        log::info("  winSize        = {}x{}", winSize.width, winSize.height);
        log::info("  GL viewport    = ({},{}) {}x{}", currentViewport[0], currentViewport[1], currentViewport[2], currentViewport[3]);
        log::info("  current FBO    = {}", currentFBO);

        // ── determina dimensiones lectura ────────────────────────
        // usa tamano frame (dimensiones reales ventana)
        // viewport en swapBuffers puede no coincidir
        // pero tamano frame es lo que ventana contiene
        int glWidth  = static_cast<int>(frameSize.width);
        int glHeight = static_cast<int>(frameSize.height);

        if (glWidth <= 0 || glHeight <= 0) {
            glWidth  = static_cast<int>(winPixels.width);
            glHeight = static_cast<int>(winPixels.height);
        }
        if (glWidth <= 0 || glHeight <= 0) {
            glWidth  = currentViewport[2];
            glHeight = currentViewport[3];
        }
        if (glWidth <= 0 || glHeight <= 0) {
            log::error("[FramebufferCapture] All size queries returned invalid");
            for (auto& pair : hiddenNodes) pair.first->setVisible(pair.second);
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        log::info("[FramebufferCapture] Reading {}x{} pixels", glWidth, glHeight);

        // overflow guard: glWidth * glHeight * 4 must fit in size_t
        constexpr size_t kMaxPixels = SIZE_MAX / 4;
        if (static_cast<size_t>(glWidth) * static_cast<size_t>(glHeight) > kMaxPixels) {
            log::error("[FramebufferCapture] Pixel buffer size overflow: {}x{}", glWidth, glHeight);
            for (auto& pair : hiddenNodes) pair.first->setVisible(pair.second);
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        // ── intento 1: lee fbo actual ─────
        // en swapBuffers, suele ser framebuffer
        // a presentar. en algunos setups (angle,
        // shader mods) puede no ser fbo 0.
        glFinish();

        std::vector<uint8_t> pixels(static_cast<size_t>(glWidth) * glHeight * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, glWidth, glHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);

        GLenum err = glGetError();
        bool hasContent = (err == GL_NO_ERROR) && pixelBufferHasContent(pixels, glWidth, glHeight);

        if (!hasContent && err != GL_NO_ERROR) {
            log::warn("[FramebufferCapture] GL error reading from FBO {}: 0x{:X}", currentFBO, err);
        }

        // ── intento 2: si fbo no era 0, prueba fbo 0 ───────────
        if (!hasContent && currentFBO != 0) {
            log::info("[FramebufferCapture] Current FBO {} had no content, trying FBO 0", currentFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glFinish();

            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, glWidth, glHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);

            err = glGetError();
            hasContent = (err == GL_NO_ERROR) && pixelBufferHasContent(pixels, glWidth, glHeight);

            // restaura fbo original
            glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);
        }

        // ── intento 3: prueba lectura tamano viewport fbo 0 ────
        // en algunos sistemas, viewport es menor que frame
        // y contenido juego solo esta en region viewport
        if (!hasContent && (currentViewport[2] != glWidth || currentViewport[3] != glHeight)
            && currentViewport[2] > 0 && currentViewport[3] > 0) {
            int vpW = currentViewport[2];
            int vpH = currentViewport[3];
            int vpX = currentViewport[0];
            int vpY = currentViewport[1];

            log::info("[FramebufferCapture] Frame size read failed, trying viewport-sized read: ({},{}) {}x{}", vpX, vpY, vpW, vpH);

            std::vector<uint8_t> vpPixels(static_cast<size_t>(vpW) * vpH * 4);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glFinish();

            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(vpX, vpY, vpW, vpH, GL_RGBA, GL_UNSIGNED_BYTE, vpPixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);

            glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);

            err = glGetError();
            if (err == GL_NO_ERROR && pixelBufferHasContent(vpPixels, vpW, vpH)) {
                pixels = std::move(vpPixels);
                glWidth = vpW;
                glHeight = vpH;
                hasContent = true;
                log::info("[FramebufferCapture] Viewport-sized read succeeded: {}x{}", vpW, vpH);
            }
        }

        if (!hasContent) {
            log::warn("[FramebufferCapture] All direct capture attempts failed – falling back to rerender");
            // restaura nodos ocultos antes de fallback
            for (auto& pair : hiddenNodes) pair.first->setVisible(pair.second);
            doCaptureRerender(targetWidth, viewportW, viewportH, quality);
            return;
        }

        // ── restaura nodos ocultos tras lectura pixeles ──────────
        for (auto& pair : hiddenNodes) pair.first->setVisible(pair.second);

        // NOTE: Auto-crop of black borders was removed.
        // Black border cropping is now only available via the manual
        // "Crop Borders" button in CapturePreviewPopup / CaptureEditPopup.
        // This ensures captures always return the full unmodified frame.

        // ── voltea (opengl origen abajo-izq -> arriba-izq) ──────
        flipVertical(pixels, glWidth, glHeight, 4);

        // ── escala abajo a ancho objetivo ────────────────────────
        double aspect = static_cast<double>(glWidth) / static_cast<double>(glHeight);
        int outW = targetWidth;
        int outH = static_cast<int>(std::round(outW / aspect));
        if (outH <= 0) outH = 1;

        log::info("[FramebufferCapture] Scaling {}x{} -> {}x{}", glWidth, glHeight, outW, outH);

        std::shared_ptr<uint8_t> outBuffer;
        if (outW == glWidth && outH == glHeight) {
            // no requiere escalado
            outBuffer = makeRGBABuffer(pixels.data(), glWidth, glHeight);
        } else {
            // Use bilinear for small scale ratios (< 2x), lanczos for larger
            double ratio = static_cast<double>(glWidth) / outW;
            if (ratio < 2.0) {
                outBuffer = bilinearDownscale(pixels.data(), glWidth, glHeight, outW, outH);
            } else {
                outBuffer = lanczosDownscale(pixels.data(), glWidth, glHeight, outW, outH);
            }
        }

        if (!outBuffer) {
            log::error("[FramebufferCapture] Downscale failed");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        // ── crea textura ───────────────────────────────────
        auto* texture = createTextureFromRGBA(outBuffer.get(), outW, outH);
        if (!texture) {
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        log::info("[FramebufferCapture] Direct capture completed: {}x{}", outW, outH);

        if (s_request.callback) {
            s_deferredCallbacks.push_back({s_request.callback, true, texture, outBuffer, outW, outH});
        }
}

// ─────────────────────────────────────────────────────────────
// doCaptureRerender
//
// re-renderiza escena en ccrendertexture separado a
// resolucion objetivo. shaderlayer queda habilitado
// asi escena se renderiza tal cual; evita bug captura negra
// donde contenido iba a fbo interno shaderlayer
// pero no se dibujaba en nuestro target.
//
// mejoras sobre enfoque ingenuo:
//   • captureguard fija isCapturing()+captureSize asi
//     shaderlayerhook redimensiona fbo interno.
//   • glstateguard guarda/restaura viewport, fbo y mezcla.
//   • chequeo limite gpu via gl_max_texture_size.
//   • glfinish() antes de leer asegura renderizado listo.
// ─────────────────────────────────────────────────────────────
void FramebufferCapture::doCaptureRerender(int targetWidth, int viewportW, int viewportH, CaptureQualitySettings const& quality) {
    auto* director = CCDirector::sharedDirector();
        if (!director) {
            log::error("[FramebufferCapture] CCDirector is null");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        auto* scene = director->getRunningScene();
        if (!scene) {
            log::error("[FramebufferCapture] Scene is null");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        // ── calcula dimensiones ─────────────────────────────
        auto winSize = director->getWinSize();
        double aspect = winSize.height > 0
            ? (static_cast<double>(winSize.width) / static_cast<double>(winSize.height))
            : (16.0 / 9.0);

        int renderW = std::max(1, targetWidth);
        int renderH = std::max(1, static_cast<int>(std::round(renderW / aspect)));

        // ── limita a tamano maximo textura gpu ────────────────
        int maxTex = getMaxTextureSize();
        if (renderW > maxTex || renderH > maxTex) {
            log::warn("[FramebufferCapture] Requested {}x{} exceeds GL_MAX_TEXTURE_SIZE={}, clamping",
                      renderW, renderH, maxTex);
            if (renderW > maxTex) {
                renderW = maxTex;
                renderH = std::max(1, static_cast<int>(std::round(renderW / aspect)));
            }
            if (renderH > maxTex) {
                renderH = maxTex;
                renderW = std::max(1, static_cast<int>(std::round(renderH * aspect)));
            }
        }

        log::info("[FramebufferCapture] Rerender: winSize={}x{}, renderSize={}x{}, aspect={:.4f}, maxTex={}",
                  winSize.width, winSize.height, renderW, renderH, aspect, maxTex);

        // ── oculta todo lo que no sea gameplay vanilla ──────────
        auto hiddenNodes = hideNonVanillaUI();

        // ── guarda estado gl (restaurado auto al salir ambito) ──
        GLStateGuard glGuard;

        // ── crea textura render ────────────────────────────
        auto* rt = CCRenderTexture::create(renderW, renderH, kCCTexture2DPixelFormat_RGBA8888);
        if (!rt) {
            log::warn("[FramebufferCapture] FBO at {}x{} failed – trying half resolution", renderW, renderH);
            renderW = std::max(1, renderW / 2);
            renderH = std::max(1, renderH / 2);
            rt = CCRenderTexture::create(renderW, renderH, kCCTexture2DPixelFormat_RGBA8888);
            if (!rt) {
                for (auto& pair : hiddenNodes) pair.first->setVisible(pair.second);
                if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
                return;
            }
            log::info("[FramebufferCapture] FBO fallback succeeded at {}x{}", renderW, renderH);
        }

        // ── activa flag captura tras saber dimensiones finales ──
        // shaderlayerhook lee esto para redimensionar fbo interno.
        CaptureGuard capGuard(renderW, renderH);

        // ── renderiza ───────────────────────────────────────────
        rt->begin();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        kmGLPushMatrix();

        float scaleX = static_cast<float>(renderW) / winSize.width;
        float scaleY = static_cast<float>(renderH) / winSize.height;
        {
            kmMat4 scaleMat;
            memset(&scaleMat, 0, sizeof(scaleMat));
            scaleMat.mat[0] = scaleX;
            scaleMat.mat[5] = scaleY;
            scaleMat.mat[10] = 1.0f;
            scaleMat.mat[15] = 1.0f;
            kmGLMultMatrix(&scaleMat);
        }

        scene->visit();

        kmGLPopMatrix();
        rt->end();

        // ── restaura nodos ocultos ─────────────────────────────
        for (auto& pair : hiddenNodes) pair.first->setVisible(pair.second);

        // ── sincroniza gpu antes leer pixeles ────────────────
        glFinish();

        GLenum glErr = glGetError();
        if (glErr != GL_NO_ERROR) {
            log::warn("[FramebufferCapture] GL error after render pass: 0x{:X}", glErr);
        }

        CCImage* img = rt->newCCImage(true);
        if (!img) {
            log::error("[FramebufferCapture] newCCImage() returned null");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        int W = img->getWidth();
        int H = img->getHeight();
        unsigned char* data = img->getData();

        if (!data || W <= 0 || H <= 0) {
            log::error("[FramebufferCapture] Invalid image data: {}x{}", W, H);
            img->release();
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        log::info("[FramebufferCapture] Got image: {}x{}", W, H);

        // ── construye buffer rgba ────────────────────────────────
        auto rgbaBuffer = makeRGBABuffer(data, W, H);
        img->release();

        // ── crea textura ───────────────────────────────────
        auto* texture = createTextureFromRGBA(rgbaBuffer.get(), W, H);
        if (!texture) {
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        log::info("[FramebufferCapture] Rerender capture completed: {}x{}", W, H);

        if (s_request.callback) {
            s_deferredCallbacks.push_back({s_request.callback, true, texture, rgbaBuffer, W, H});
        }
}

// ─────────────────────────────────────────────────────────────
// doCaptureNode - renderiza nodo unico a fbo
// ─────────────────────────────────────────────────────────────
void FramebufferCapture::doCaptureNode(CCNode* node) {
    log::info("[FramebufferCapture] Capturing specific node");

        if (!node) {
            log::error("[FramebufferCapture] Node is null");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        auto contentSize = node->getContentSize();
        float scale = node->getScale();
        int width  = static_cast<int>(contentSize.width  * scale);
        int height = static_cast<int>(contentSize.height * scale);

        if (width <= 0 || height <= 0) {
            log::error("[FramebufferCapture] Invalid node size: {}x{}", width, height);
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        auto* rt = CCRenderTexture::create(width, height);
        if (!rt) {
            log::error("[FramebufferCapture] Failed to create CCRenderTexture for node");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        // guarda / fija transformacion
        auto origPos    = node->getPosition();
        auto origAnchor = node->getAnchorPoint();
        node->setPosition(ccp(width / 2.0f, height / 2.0f));
        node->setAnchorPoint(ccp(0.5f, 0.5f));

        rt->begin();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        node->visit();
        rt->end();

        node->setPosition(origPos);
        node->setAnchorPoint(origAnchor);

        CCImage* img = rt->newCCImage(true);
        if (!img) {
            log::error("[FramebufferCapture] newCCImage() returned null for node");
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        int W = img->getWidth();
        int H = img->getHeight();
        unsigned char* data = img->getData();

        if (!data || W <= 0 || H <= 0) {
            log::error("[FramebufferCapture] Invalid node image data");
            img->release();
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        auto rgbaBuffer = makeRGBABuffer(data, W, H);
        img->release();

        auto* texture = createTextureFromRGBA(rgbaBuffer.get(), W, H);
        if (!texture) {
            if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
            return;
        }

        log::info("[FramebufferCapture] Node capture completed: {}x{}", W, H);

        if (s_request.callback) {
            s_deferredCallbacks.push_back({s_request.callback, true, texture, rgbaBuffer, W, H});
        }
}

// ─────────────────────────────────────────────────────────────
// flipVertical
// ─────────────────────────────────────────────────────────────
void FramebufferCapture::flipVertical(std::vector<uint8_t>& pixels, int width, int height, int channels) {
    int rowSize = width * channels;
    std::vector<uint8_t> temp(rowSize);

    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top    = pixels.data() + y * rowSize;
        uint8_t* bottom = pixels.data() + (height - 1 - y) * rowSize;
        std::memcpy(temp.data(), top, rowSize);
        std::memcpy(top, bottom, rowSize);
        std::memcpy(bottom, temp.data(), rowSize);
    }
}

// ─────────────────────────────────────────────────────────────
// fast bilinear downscale (for scale ratios < 2x)
// ─────────────────────────────────────────────────────────────
static std::shared_ptr<uint8_t> bilinearDownscale(
    uint8_t const* src, int srcW, int srcH,
    int dstW, int dstH
) {
    size_t outBytes = static_cast<size_t>(dstW) * dstH * 4;
    std::shared_ptr<uint8_t> out(new uint8_t[outBytes], std::default_delete<uint8_t[]>());
    uint8_t* dst = out.get();

    double ratioX = static_cast<double>(srcW) / dstW;
    double ratioY = static_cast<double>(srcH) / dstH;

    for (int y = 0; y < dstH; ++y) {
        double srcY = (y + 0.5) * ratioY - 0.5;
        int y0 = std::max(0, static_cast<int>(std::floor(srcY)));
        int y1 = std::min(srcH - 1, y0 + 1);
        double fy = srcY - y0;

        for (int x = 0; x < dstW; ++x) {
            double srcX = (x + 0.5) * ratioX - 0.5;
            int x0 = std::max(0, static_cast<int>(std::floor(srcX)));
            int x1 = std::min(srcW - 1, x0 + 1);
            double fx = srcX - x0;

            size_t i00 = (static_cast<size_t>(y0) * srcW + x0) * 4;
            size_t i10 = (static_cast<size_t>(y0) * srcW + x1) * 4;
            size_t i01 = (static_cast<size_t>(y1) * srcW + x0) * 4;
            size_t i11 = (static_cast<size_t>(y1) * srcW + x1) * 4;

            double w00 = (1.0 - fx) * (1.0 - fy);
            double w10 = fx * (1.0 - fy);
            double w01 = (1.0 - fx) * fy;
            double w11 = fx * fy;

            size_t di = (static_cast<size_t>(y) * dstW + x) * 4;
            for (int c = 0; c < 3; ++c) {
                double val = src[i00 + c] * w00 + src[i10 + c] * w10 +
                             src[i01 + c] * w01 + src[i11 + c] * w11;
                dst[di + c] = static_cast<uint8_t>(std::clamp(val, 0.0, 255.0));
            }
            dst[di + 3] = 255;
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
// escalado lanczos-3 (rgba, dos pasos separable)
// ─────────────────────────────────────────────────────────────
static constexpr double PI_VAL = 3.14159265358979323846;

static inline double lanczos3(double x) {
    if (x == 0.0) return 1.0;
    if (x < -3.0 || x > 3.0) return 0.0;
    double px = PI_VAL * x;
    return (3.0 * std::sin(px) * std::sin(px / 3.0)) / (px * px);
}

std::shared_ptr<uint8_t> FramebufferCapture::lanczosDownscale(
    uint8_t const* src, int srcW, int srcH,
    int dstW, int dstH
) {
    if (!src || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return nullptr;

    // si dimensiones coinciden, copia con arreglo alpha
    if (dstW == srcW && dstH == srcH) {
        return makeRGBABuffer(src, dstW, dstH);
    }

    const int A = 3; // ventana lanczos-3

    // ── paso 1: horizontal srcW×srcH → dstW×srcH ───────────
    std::vector<float> hPass(static_cast<size_t>(dstW) * srcH * 4, 0.0f);
    {
        double ratioX  = static_cast<double>(srcW) / dstW;
        double filterW = std::max(ratioX, 1.0);

        for (int y = 0; y < srcH; ++y) {
            for (int x = 0; x < dstW; ++x) {
                double center = (x + 0.5) * ratioX - 0.5;
                int left  = static_cast<int>(std::floor(center - A * filterW));
                int right = static_cast<int>(std::ceil (center + A * filterW));
                left  = std::max(left,  0);
                right = std::min(right, srcW - 1);

                double sumW = 0;
                double sumR = 0, sumG = 0, sumB = 0, sumA = 0;

                for (int sx = left; sx <= right; ++sx) {
                    double w = lanczos3((sx - center) / filterW);
                    size_t si = (static_cast<size_t>(y) * srcW + sx) * 4;
                    sumR += src[si + 0] * w;
                    sumG += src[si + 1] * w;
                    sumB += src[si + 2] * w;
                    sumA += src[si + 3] * w;
                    sumW += w;
                }

                if (sumW != 0) { sumR /= sumW; sumG /= sumW; sumB /= sumW; sumA /= sumW; }

                size_t di = (static_cast<size_t>(y) * dstW + x) * 4;
                hPass[di + 0] = static_cast<float>(sumR);
                hPass[di + 1] = static_cast<float>(sumG);
                hPass[di + 2] = static_cast<float>(sumB);
                hPass[di + 3] = static_cast<float>(sumA);
            }
        }
    }

    // ── paso 2: vertical dstW×srcH → dstW×dstH ─────────────
    size_t outBytes = static_cast<size_t>(dstW) * dstH * 4;
    std::shared_ptr<uint8_t> out(new uint8_t[outBytes], std::default_delete<uint8_t[]>());
    uint8_t* outPtr = out.get();

    {
        double ratioY  = static_cast<double>(srcH) / dstH;
        double filterH = std::max(ratioY, 1.0);

        for (int y = 0; y < dstH; ++y) {
            double center = (y + 0.5) * ratioY - 0.5;
            int top    = static_cast<int>(std::floor(center - A * filterH));
            int bottom = static_cast<int>(std::ceil (center + A * filterH));
            top    = std::max(top,    0);
            bottom = std::min(bottom, srcH - 1);

            for (int x = 0; x < dstW; ++x) {
                double sumW = 0;
                double sumR = 0, sumG = 0, sumB = 0, sumA = 0;

                for (int sy = top; sy <= bottom; ++sy) {
                    double w = lanczos3((sy - center) / filterH);
                    size_t si = (static_cast<size_t>(sy) * dstW + x) * 4;
                    sumR += hPass[si + 0] * w;
                    sumG += hPass[si + 1] * w;
                    sumB += hPass[si + 2] * w;
                    sumA += hPass[si + 3] * w;
                    sumW += w;
                }

                if (sumW != 0) { sumR /= sumW; sumG /= sumW; sumB /= sumW; sumA /= sumW; }

                size_t di = (static_cast<size_t>(y) * dstW + x) * 4;
                outPtr[di + 0] = static_cast<uint8_t>(std::clamp(sumR, 0.0, 255.0));
                outPtr[di + 1] = static_cast<uint8_t>(std::clamp(sumG, 0.0, 255.0));
                outPtr[di + 2] = static_cast<uint8_t>(std::clamp(sumB, 0.0, 255.0));
                outPtr[di + 3] = 255; // fuerza opaco
            }
        }
    }

    return out;
}
