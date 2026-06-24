#include "PopupBlurService.hpp"

#include <Geode/Geode.hpp>
#include "../utils/Shaders.hpp"
#include "../core/Settings.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "../features/capture/services/SceneCapture.hpp"
#include "PaiblurNode.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

#if defined(GEODE_IS_WINDOWS)
#include <windows.h>
#endif

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::popupblur {

bool isEditorContextActive() {
    auto* director = CCDirector::get();
    if (!director) return false;
    auto* scene = director->getRunningScene();
    if (!scene) return false;
    return scene->getChildByType<LevelEditorLayer>(0) != nullptr ||
           scene->getChildByType<EditorUI>(0) != nullptr;
}

static std::string const& blurNodeIdKey() {
    static std::string const key = Mod::get()->getID() + "/popup-blur-node-id";
    return key;
}

struct RegistryEntry {
    CCNode* popupPtr = nullptr;    // identity only, never dereferenced
    Ref<CCNode> blurRef;
    CCNode* parentPtr = nullptr;    // parent snapshot at register time
    float ageSeconds = 0.f;
    bool fadingOut = false;
};

static std::unordered_map<CCNode*, RegistryEntry>& blurRegistry() {
    static auto* map = new std::unordered_map<CCNode*, RegistryEntry>();
    return *map;
}

static std::vector<Ref<CCNode>>& flashRegistry() {
    static auto* vec = new std::vector<Ref<CCNode>>();
    return *vec;
}

struct SnapshotCache {
    Ref<CCTexture2D> tex;
    CCSize size = CCSizeZero;
    double frameTime = -1.0;
    int sceneStamp = 0;       // CCDirector frame counter — invalidated if it changes too much
};
static SnapshotCache& snapshotCache() {
    static SnapshotCache c;
    return c;
}

struct BlurResultCache {
    Ref<CCTexture2D> blurredTex;
    CCSize blurredSize = CCSizeZero;
    std::string style;
    float intensity = -1.f;
    float darkness = -1.f;
    double frameTime = -1.0;
    void* snapshotToken = nullptr;
};
static BlurResultCache& blurResultCache() {
    static BlurResultCache c;
    return c;
}

void registerFlashOverlay(CCNode* flashLayer) {
    if (flashLayer) {
        auto& reg = flashRegistry();
        // Avoid duplicates.
        for (auto& ref : reg) {
            if (ref.data() == flashLayer) return;
        }
        reg.emplace_back(flashLayer);
    }
}

void unregisterFlashOverlay(CCNode* flashLayer) {
    if (flashLayer) {
        auto& reg = flashRegistry();
        reg.erase(
            std::remove_if(reg.begin(), reg.end(),
                [flashLayer](Ref<CCNode> const& ref) { return ref.data() == flashLayer; }),
            reg.end());
    }
}

CCSprite* reuseBlurForSnapshot(CCTexture2D* snapshot, std::string const& style,
                                float intensity, float darkness) {
    if (!snapshot) return nullptr;
    auto& cache = blurResultCache();
    auto* cachedTex = cache.blurredTex.data();
    if (!cachedTex) return nullptr;
    if (cache.snapshotToken != static_cast<void*>(snapshot)) return nullptr;
    if (cache.style != style) return nullptr;
    if (std::abs(cache.intensity - intensity) > 0.01f) return nullptr;
    if (std::abs(cache.darkness - darkness) > 0.01f) return nullptr;

    auto* spr = CCSprite::createWithTexture(cachedTex);
    if (spr) {
        spr->setContentSize(cache.blurredSize);
        spr->setFlipY(true);
    }
    return spr;
}

void storeBlurForSnapshot(CCTexture2D* snapshot, CCSprite* blurredSprite,
                           std::string const& style, float intensity, float darkness) {
    if (!snapshot || !blurredSprite || !blurredSprite->getTexture()) return;
    auto& cache = blurResultCache();
    cache.blurredTex = Ref<CCTexture2D>(blurredSprite->getTexture());
    cache.blurredSize = blurredSprite->getContentSize();
    cache.style = style;
    cache.intensity = intensity;
    cache.darkness = darkness;
    cache.snapshotToken = static_cast<void*>(snapshot);
    auto* director = CCDirector::get();
    cache.frameTime = director ? static_cast<double>(director->getTotalFrames()) : -1.0;
}

class WatchdogTarget : public cocos2d::CCObject {
public:
    void tick(float dt);
};

static bool g_watchdogScheduled = false;

static WatchdogTarget* getWatchdogTarget() {
    static WatchdogTarget* target = []() {
        auto* t = new WatchdogTarget();
        t->autorelease();
        t->retain();  // permanent retain — survives scene changes
        return t;
    }();
    return target;
}

static void fadeAndRemoveBlurNode(CCNode* blur, float duration) {
    if (!blur || !blur->getParent()) return;
    blur->stopAllActions();
    if (duration <= 0.01f) {
        blur->removeFromParent();
        return;
    }
    blur->runAction(CCSequence::create(
        CCFadeTo::create(duration, 0),
        CCCallFunc::create(blur, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

static void scheduleWatchdogIfNeeded() {
    if (g_watchdogScheduled) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;
    scheduler->scheduleSelector(
        schedule_selector(WatchdogTarget::tick),
        getWatchdogTarget(),
        0.25f,
        false
    );
    g_watchdogScheduled = true;
}

static void unscheduleWatchdogIfIdle() {
    if (!g_watchdogScheduled) return;
    if (!blurRegistry().empty()) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;
    scheduler->unscheduleSelector(
        schedule_selector(WatchdogTarget::tick),
        getWatchdogTarget()
    );
    g_watchdogScheduled = false;
}

static CCNode* findSceneRoot(CCNode* node) {
    if (!node) return nullptr;
    CCNode* cur = node;
    while (cur->getParent()) cur = cur->getParent();
    return cur;
}

static bool popupStillChildOf(CCNode* parent, CCNode* popupPtr) {
    if (!parent || !popupPtr) return false;
    auto* children = parent->getChildren();
    if (!children) return false;
    int count = children->count();
    for (int i = 0; i < count; ++i) {
        if (children->objectAtIndex(i) == static_cast<cocos2d::CCObject*>(popupPtr)) {
            return true;
        }
    }
    return false;
}

void WatchdogTarget::tick(float dt) {
    auto& reg = blurRegistry();
    if (reg.empty()) {
        unscheduleWatchdogIfIdle();
        return;
    }

    auto* director = CCDirector::get();
    auto* runningScene = director ? director->getRunningScene() : nullptr;

    std::vector<CCNode*> toRemoveFromRegistry;
    std::vector<CCNode*> toFadeBlurs;
    std::vector<CCNode*> toInstantRemoveBlurs;

    for (auto& [popupKey, entry] : reg) {
        entry.ageSeconds += dt;

        auto* blur = entry.blurRef.data();

        if (!blur || !blur->getParent()) {
            toRemoveFromRegistry.push_back(popupKey);
            continue;
        }

        if (runningScene) {
            CCNode* blurScene = findSceneRoot(blur);
            if (blurScene && blurScene != runningScene) {
                toInstantRemoveBlurs.push_back(blur);
                toRemoveFromRegistry.push_back(popupKey);
                continue;
            }
        }

        if (entry.fadingOut) continue;

        CCNode* parent = entry.parentPtr;
        if (!parent) {
            toFadeBlurs.push_back(blur);
            entry.fadingOut = true;
            continue;
        }

        CCNode* blurParent = blur->getParent();
        if (blurParent != parent) {
            parent = blurParent;
            entry.parentPtr = blurParent;
        }

        bool popupPresent = popupStillChildOf(parent, popupKey);
        if (!popupPresent && entry.ageSeconds > 0.2f) {
            toFadeBlurs.push_back(blur);
            entry.fadingOut = true;
            continue;
        }
    }

    // Apply ops.
    for (auto* b : toInstantRemoveBlurs) {
        if (b && b->getParent()) b->removeFromParent();
    }
    for (auto* b : toFadeBlurs) {
        fadeAndRemoveBlurNode(b, 0.18f);
    }
    for (auto* k : toRemoveFromRegistry) {
        reg.erase(k);
    }

    if (reg.empty()) {
        unscheduleWatchdogIfIdle();
    }
}

static void pruneDeadEntries() {
    auto& reg = blurRegistry();
    for (auto it = reg.begin(); it != reg.end();) {
        auto* blur = it->second.blurRef.data();
        if (!blur || !blur->getParent()) {
            it = reg.erase(it);
        } else {
            ++it;
        }
    }
}

Config getConfig() {
    Config cfg;
    cfg.enabled = paimon::settings::popupblur::enabled();
    auto styleSetting = paimon::settings::popupblur::style();
    if (styleSetting == "gaussian" || styleSetting == "paimonblur" || styleSetting == "paiblur") {
        cfg.style = styleSetting;
    } else if (styleSetting == "paimonblur-dynamic") {
        cfg.style = "paimonblur";
    } else {
        cfg.style = "paiblur";
    }
    cfg.intensity = std::max(0.1f, static_cast<float>(paimon::settings::popupblur::intensity()));
    cfg.darkness = std::clamp(static_cast<float>(paimon::settings::popupblur::darkness()), 0.0f, 1.0f);
    return cfg;
}

void registerExternalBlur(CCNode* popup, CCNode* blurNode) {
    if (!popup || !blurNode) return;
    pruneDeadEntries();
    RegistryEntry entry;
    entry.popupPtr = popup;
    entry.blurRef = Ref<CCNode>(blurNode);
    entry.parentPtr = popup->getParent();
    entry.ageSeconds = 0.f;
    entry.fadingOut = false;
    blurRegistry()[popup] = std::move(entry);
    scheduleWatchdogIfNeeded();
}

std::vector<std::pair<CCNode*, bool>> hideAllActiveBlurs() {
    pruneDeadEntries();

    std::vector<std::pair<CCNode*, bool>> hidden;
    hidden.reserve(blurRegistry().size());

    std::vector<CCNode*> deadKeys;
    for (auto& [popupPtr, entry] : blurRegistry()) {
        auto* blur = entry.blurRef.data();
        if (!blur || !blur->getParent()) {
            deadKeys.push_back(popupPtr);
            continue;
        }
        if (blur->isVisible()) {
            hidden.emplace_back(blur, true);
            blur->setVisible(false);
        }
    }
    for (auto* k : deadKeys) blurRegistry().erase(k);
    return hidden;
}

void restoreHiddenBlurs(std::vector<std::pair<CCNode*, bool>> const& hidden) {
    for (auto& [blur, wasVisible] : hidden) {
        if (blur) blur->setVisible(wasVisible);
    }
}

struct CaptureVisibilityGuard {
    std::vector<std::pair<CCNode*, bool>> hiddenBlurs;
    std::vector<std::pair<CCNode*, bool>> hiddenFlashes;
    CCNode* popup = nullptr;
    bool selfInScene = false;
    bool hadSelfVisible = false;
    bool restored = false;

    void restore() {
        if (restored) return;
        restored = true;
        for (auto& [flash, wasVisible] : hiddenFlashes) {
            if (flash) flash->setVisible(wasVisible);
        }
        if (popup && selfInScene && hadSelfVisible) popup->setVisible(true);
        restoreHiddenBlurs(hiddenBlurs);
    }

    ~CaptureVisibilityGuard() { restore(); }
};

CCTexture2D* captureSceneTexture(CCNode* popupToHide, CCSize& outSize, bool forceRefresh, int maxCaptureLongEdge) {
    auto* director = CCDirector::get();
    if (!director) return nullptr;

    if (isEditorContextActive()) return nullptr;

    auto* scene = director->getRunningScene();
    auto winSize = director->getWinSize();
    if (!scene || winSize.width <= 0.f || winSize.height <= 0.f) return nullptr;

    bool const sceneFrozenByActiveBlur = !blurRegistry().empty();
    if (!forceRefresh && sceneFrozenByActiveBlur) {
        auto& cache = snapshotCache();
        auto* tex = cache.tex.data();
        if (tex && cache.size.width >= winSize.width - 1.f &&
            cache.size.height >= winSize.height - 1.f) {
            double now = static_cast<double>(director->getTotalFrames());
            if (cache.frameTime > 0 && (now - cache.frameTime) < 18.0) {
                outSize = winSize;
                return tex;
            }
        }
    }

    int captureW = static_cast<int>(std::round(winSize.width));
    int captureH = static_cast<int>(std::round(winSize.height));
    if (maxCaptureLongEdge > 0) {
        int longEdge = std::max(captureW, captureH);
        if (longEdge > maxCaptureLongEdge) {
            float scale = static_cast<float>(maxCaptureLongEdge) / static_cast<float>(longEdge);
            captureW = std::max(16, static_cast<int>(std::round(captureW * scale)));
            captureH = std::max(16, static_cast<int>(std::round(captureH * scale)));
        }
    }
    if (captureW <= 0 || captureH <= 0) return nullptr;

    CaptureVisibilityGuard visGuard;
    visGuard.hiddenBlurs = hideAllActiveBlurs();
    visGuard.popup = popupToHide;

    if (popupToHide && popupToHide->getParent()) {
        visGuard.selfInScene = true;
        visGuard.hadSelfVisible = popupToHide->isVisible();
        if (visGuard.hadSelfVisible) popupToHide->setVisible(false);
    }

    // Hide flash layers.
    {
        auto& reg = flashRegistry();
        if (!reg.empty()) {
            reg.erase(
                std::remove_if(reg.begin(), reg.end(),
                    [](Ref<CCNode> const& ref) { return !ref || !ref->getParent(); }),
                reg.end());

            visGuard.hiddenFlashes.reserve(reg.size());
            for (auto& ref : reg) {
                CCNode* flash = ref.data();
                if (!flash->isVisible()) continue;
                bool shouldHide = true;
                if (auto* layer = typeinfo_cast<CCLayerColor*>(flash)) {
                    shouldHide = layer->getOpacity() > 0;
                }
                if (shouldHide) {
                    visGuard.hiddenFlashes.emplace_back(flash, true);
                    flash->setVisible(false);
                }
            }
        }
    }

    CCTexture2D* tex = nullptr;
    Ref<CCRenderTexture> rt;
    bool const shaderBackBufferPath = paimon::capture::playLayerShaderCaptureActive();

    if (shaderBackBufferPath) {
        paimon::capture::ActiveGuard captureCtx(winSize);
        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        paimon::capture::renderSceneGraph(scene);

        visGuard.restore();

        std::vector<uint8_t> pixels;
        int readW = 0, readH = 0;
        if (!paimon::capture::readBoundFramebufferRGBA(pixels, readW, readH)) {
            geode::log::warn("[PopupBlur] ShaderLayer back-buffer read failed");
            return nullptr;
        }
        tex = paimon::capture::createTextureFromRGBA(pixels.data(), readW, readH, false);
        if (!tex) return nullptr;
        if (prevFBO != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        }
    } else {
        rt = CCRenderTexture::create(
            captureW, captureH,
            kCCTexture2DPixelFormat_RGBA8888,
            GL_DEPTH24_STENCIL8);
        if (!rt) return nullptr;

        // Validate FBO once.
        rt->begin();
        GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        rt->end();
        if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
            geode::log::warn("[PopupBlur] FBO incomplete (status=0x{:X}), skipping capture", fboStatus);
            return nullptr;
        }

        bool useBackBufferBlit = false;
        {
            auto* glView = director->getOpenGLView();
            CCSize physSize = glView ? glView->getFrameSize() : CCSizeZero;
            int srcW = static_cast<int>(physSize.width);
            int srcH = static_cast<int>(physSize.height);
            if (srcW <= 0 || srcH <= 0) {
                // glView reported invalid; fall through to viewport.
                GLint vp[4] = {0, 0, 0, 0};
                glGetIntegerv(GL_VIEWPORT, vp);
                srcW = vp[2];
                srcH = vp[3];
            }

            if (srcW > 0 && srcH > 0) {
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
                rt->begin();
                GLint drawFBO = 0;
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawFBO);

                while (glGetError() != GL_NO_ERROR) {}

                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFBO);

                GLint vp[4] = {0, 0, captureW, captureH};
                glGetIntegerv(GL_VIEWPORT, vp);
                int dstW = vp[2] > 0 ? vp[2] : captureW;
                int dstH = vp[3] > 0 ? vp[3] : captureH;

                glBlitFramebuffer(0, 0, srcW, srcH,
                                  0, 0, dstW, dstH,
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);

                useBackBufferBlit = (glGetError() == GL_NO_ERROR);

                glBindFramebuffer(GL_FRAMEBUFFER, drawFBO);
                rt->end();
            }
        }

        if (!useBackBufferBlit) {
            geode::log::debug("[PopupBlur] back-buffer blit unavailable, falling back to re-render");
            paimon::capture::ActiveGuard captureCtx(winSize);
            rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 0.f, 0);
            paimon::capture::renderSceneGraph(scene);
            rt->end();
        }

        tex = rt->getSprite() ? rt->getSprite()->getTexture() : nullptr;
        if (!tex) return nullptr;

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        glFinish();
#else
        glFlush();
#endif

        visGuard.restore();
    }

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    tex->setTexParameters(&params);

    {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            if (rt && rt->getSprite()) {
                auto fboSpriteSize = rt->getSprite()->getContentSize();
                auto fboSpriteRect = rt->getSprite()->getTextureRect();
                geode::log::info("[PopupBlur::capture] winSize={}x{} captureWH={}x{} "
                                 "fboSprite.contentSize={}x{} fboSprite.rect={}x{} "
                                 "tex.contentSize={}x{} shaderPath={}",
                    winSize.width, winSize.height,
                    captureW, captureH,
                    fboSpriteSize.width, fboSpriteSize.height,
                    fboSpriteRect.size.width, fboSpriteRect.size.height,
                    tex->getContentSize().width, tex->getContentSize().height,
                    shaderBackBufferPath);
            } else {
                geode::log::info("[PopupBlur::capture] winSize={}x{} tex={}x{} shaderBackBuffer={}",
                    winSize.width, winSize.height,
                    tex->getContentSize().width, tex->getContentSize().height,
                    shaderBackBufferPath);
            }
        }
    }

    outSize = winSize;
    {
        auto& cache = snapshotCache();
        cache.tex = Ref<CCTexture2D>(tex);
        cache.size = winSize;
        cache.frameTime = static_cast<double>(director->getTotalFrames());
    }

    return tex;
}

CCLayerColor* buildBlurNode(CCSprite* blurred, CCSize const& winSize, Config const& cfg) {
    auto root = CCLayerColor::create(ccc4(0, 0, 0, 0));
    if (!root) return nullptr;
    root->setContentSize(CCSizeZero);
    root->ignoreAnchorPointForPosition(true);
    root->setPosition(CCPointZero);
    root->setAnchorPoint(ccp(0.f, 0.f));
    root->setID("paimon-popup-blur-root"_spr);
    root->setCascadeOpacityEnabled(true);
    root->setOpacity(255);

    blurred->setOpacity(255);
    blurred->setID("paimon-popup-blur-final"_spr);

    {
        auto* texForRect = blurred->getTexture();
        if (texForRect) {
            auto texSize = texForRect->getContentSize();
            if (texSize.width > 0.f && texSize.height > 0.f) {
                blurred->setTextureRect(CCRect(0.f, 0.f, texSize.width, texSize.height));
            } else {
                blurred->setTextureRect(CCRect(0.f, 0.f, winSize.width, winSize.height));
            }
        }

        blurred->setScale(1.0f);

        if (!blurred->isFlipY()) {
            blurred->setFlipY(true);
        }

        auto curContent = blurred->getContentSize();
        if (curContent.width <= 0.f) curContent.width = winSize.width;
        if (curContent.height <= 0.f) curContent.height = winSize.height;

        blurred->setAnchorPoint(ccp(0.5f, 0.5f));
        blurred->setPosition(winSize * 0.5f);
        blurred->setScaleX(winSize.width / curContent.width);
        blurred->setScaleY(winSize.height / curContent.height);
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            geode::log::info("[PopupBlur::buildBlurNode] winSize={}x{} content={}x{} "
                             "scale={}x{} flipY={} tex.contentSize={}",
                winSize.width, winSize.height,
                curContent.width, curContent.height,
                blurred->getScaleX(), blurred->getScaleY(),
                blurred->isFlipY(),
                texForRect ? fmt::format("{}x{}", texForRect->getContentSize().width,
                                                   texForRect->getContentSize().height)
                           : std::string("null"));
        }
    }

    // Pre-bake darkness into the sprite color.
    if (cfg.darkness > 0.f) {
        float factor = std::clamp(1.0f - cfg.darkness, 0.0f, 1.0f);
        GLubyte c = static_cast<GLubyte>(std::round(factor * 255.f));
        blurred->setColor(ccc3(c, c, c));
    }

    blurred->setID("paimon-popup-blur-sprite"_spr);

    root->addChild(blurred, 0);

    return root;
}

static void switchBlurToOpaque(CCNode* root) {
    if (!root) return;
    auto* spr = typeinfo_cast<CCSprite*>(root->getChildByID("paimon-popup-blur-sprite"_spr));
    if (!spr) return;
    spr->setBlendFunc({GL_ONE, GL_ZERO});
    if (auto* layer = typeinfo_cast<CCLayerRGBA*>(root)) {
        layer->setCascadeOpacityEnabled(false);
    }
}

static void switchBlurToTransparent(CCNode* root) {
    if (!root) return;
    auto* spr = typeinfo_cast<CCSprite*>(root->getChildByID("paimon-popup-blur-sprite"_spr));
    if (!spr) return;
    spr->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    if (auto* layer = typeinfo_cast<CCLayerRGBA*>(root)) {
        layer->setCascadeOpacityEnabled(true);
    }
}

static CCTexture2D* getDarkFallbackTexture() {
    static auto* s_texPtr = []() -> CCTexture2D* {
        auto* t = new CCTexture2D();
        unsigned char blackPixel[4] = {0, 0, 0, 255};
        if (!t->initWithData(blackPixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSizeMake(1, 1))) {
            t->release();
            return nullptr;
        }
        return t;
    }();
    return s_texPtr;
}

CCSprite* normalizeBlurSpriteToWinSize(CCSprite* input, CCSize const& winSize) {
    if (!input) return nullptr;
    if (winSize.width <= 0.f || winSize.height <= 0.f) return input;
    int w = static_cast<int>(std::round(winSize.width));
    int h = static_cast<int>(std::round(winSize.height));
    if (w <= 0 || h <= 0) return input;

    auto* rt = CCRenderTexture::create(w, h, kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
    if (!rt) {
        return input;
    }

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    rt->getSprite()->getTexture()->setTexParameters(&params);

    auto rectSize = input->getTextureRect().size;
    if (rectSize.width <= 0.f || rectSize.height <= 0.f) {
        rectSize = input->getContentSize();
    }
    if (rectSize.width <= 0.f || rectSize.height <= 0.f) {
        if (auto* tex = input->getTexture()) {
            rectSize = tex->getContentSize();
        }
    }
    if (rectSize.width <= 0.f) rectSize.width = winSize.width;
    if (rectSize.height <= 0.f) rectSize.height = winSize.height;

    input->setAnchorPoint({0.5f, 0.5f});
    input->setPosition(winSize * 0.5f);
    input->setScaleX(winSize.width / rectSize.width);
    input->setScaleY(winSize.height / rectSize.height);
    input->setFlipY(false);

    rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
    input->visit();
    rt->end();

    auto* finalSprite = CCSprite::createWithTexture(rt->getSprite()->getTexture());
    if (!finalSprite) return input;
    finalSprite->setAnchorPoint({0.5f, 0.5f});
    finalSprite->setFlipY(true);
    finalSprite->getTexture()->setTexParameters(&params);
    finalSprite->setContentSize(winSize);
    return finalSprite;
}

static cocos2d::CCNode* applyPaiblurDynamic(CCNode* popup, CCNode* parent, Config const& cfg) {
    if (!popup || !parent) return nullptr;
    auto* director = CCDirector::get();
    if (!director) return nullptr;
    auto winSize = director->getWinSize();
    if (winSize.width <= 0.f || winSize.height <= 0.f) return nullptr;

    auto* paiblur = paimon::paiblur::PaiblurNode::create(winSize, cfg.intensity, cfg.darkness);
    if (!paiblur) return nullptr;

    parent->addChild(paiblur, popup->getZOrder() - 1);

    {
        auto& reg = blurRegistry();
        for (auto& [key, entry] : reg) {
            if (key == popup) continue;
            auto* prevBlur = entry.blurRef.data();
            if (prevBlur && prevBlur->isVisible() && !entry.fadingOut) {
                prevBlur->setVisible(false);
            }
        }
    }

    RegistryEntry entry;
    entry.popupPtr = popup;
    entry.blurRef = Ref<CCNode>(paiblur);
    entry.parentPtr = parent;
    entry.ageSeconds = 0.f;
    entry.fadingOut = false;
    blurRegistry()[popup] = std::move(entry);
    scheduleWatchdogIfNeeded();

    // Fade-in with the popup entry.
    float fadeDuration = std::clamp(
        static_cast<float>(paimon::settings::popupblur::fadeDuration()),
        0.0f, 1.0f);
    paiblur->fadeIn(fadeDuration);

    return paiblur;
}

bool captureAndApply(CCNode* popup) {
    if (!popup) return false;
    auto cfg = getConfig();
    if (!cfg.enabled) return false;
    return captureAndApplyWithConfig(popup, cfg);
}

bool captureAndApplyWithConfig(CCNode* popup, Config cfg) {
    if (!popup) return false;

    if (isEditorContextActive()) return false;

    if (paimon::compat::ModCompat::externalGlobalBlurActive()) {
        return false;
    }

    auto* parent = popup->getParent();
    if (!parent) return false;

    cleanup(popup);

    if (cfg.style == "paiblur") {
        if (auto* applied = applyPaiblurDynamic(popup, parent, cfg)) {
            return true;
        }
        log::warn("[PopupBlur] Paiblur unavailable, falling back to paimonblur");
    }

    CCSize captureSize = CCSizeZero;
    auto* tex = captureSceneTexture(popup, captureSize);
    if (!tex || captureSize.width <= 0.f || captureSize.height <= 0.f) return false;

    float effectiveIntensity = cfg.intensity;
    if (cfg.style == "paimonblur") {
        effectiveIntensity = std::min(10.0f, cfg.intensity * 1.15f + 0.35f);
    }

    CCSprite* blurred = nullptr;
    {
        auto& cache = blurResultCache();
        auto* cachedTex = cache.blurredTex.data();
        bool sameSnapshot = cache.snapshotToken == static_cast<void*>(tex);
        bool sameStyle = cache.style == cfg.style;
        bool sameIntensity = std::abs(cache.intensity - effectiveIntensity) < 0.01f;
        bool sameDarkness = std::abs(cache.darkness - cfg.darkness) < 0.01f;
        if (cachedTex && sameSnapshot && sameStyle && sameIntensity && sameDarkness) {
            blurred = CCSprite::createWithTexture(cachedTex);
            if (blurred) {
                // Restore size/anchor that buildBlurNode sets later.
                blurred->setContentSize(cache.blurredSize);
                blurred->setFlipY(true);
            }
        }
    }

    if (!blurred) {
        if (cfg.style == "paimonblur") {
            blurred = Shaders::createPopupPaimonBlurredSprite(tex, captureSize, effectiveIntensity);
        } else {
            blurred = Shaders::createPopupBlurredSprite(tex, captureSize, effectiveIntensity);
        }
        if (blurred && blurred->getTexture()) {
            auto& cache = blurResultCache();
            cache.blurredTex = Ref<CCTexture2D>(blurred->getTexture());
            cache.blurredSize = blurred->getContentSize();
            cache.style = cfg.style;
            cache.intensity = effectiveIntensity;
            cache.darkness = cfg.darkness;
            cache.snapshotToken = static_cast<void*>(tex);
            if (auto* dir = CCDirector::get()) {
                cache.frameTime = static_cast<double>(dir->getTotalFrames());
            }
        }
    }

    if (!blurred) {
        geode::log::warn("[PopupBlur] Blur failed, using dark fallback");
        if (auto* fallbackTex = getDarkFallbackTexture()) {
            blurred = CCSprite::createWithTexture(fallbackTex);
        }
        if (!blurred) return false;
    }

    auto* director = CCDirector::get();
    if (!director) return false;
    auto winSize = director->getWinSize();

    auto* root = buildBlurNode(blurred, winSize, cfg);
    if (!root) return false;

    // Insert just below the popup in z-order.
    parent->addChild(root, popup->getZOrder() - 1);

    {
        auto& reg = blurRegistry();
        for (auto& [key, entry] : reg) {
            if (key == popup) continue;  // don't hide the one we just created
            auto* prevBlur = entry.blurRef.data();
            if (prevBlur && prevBlur->isVisible() && !entry.fadingOut) {
                prevBlur->setVisible(false);
            }
        }
    }

    RegistryEntry entry;
    entry.popupPtr = popup;
    entry.blurRef = Ref<CCNode>(root);
    entry.parentPtr = parent;
    entry.ageSeconds = 0.f;
    entry.fadingOut = false;
    blurRegistry()[popup] = std::move(entry);
    scheduleWatchdogIfNeeded();

    float fadeDuration = std::clamp(
        static_cast<float>(paimon::settings::popupblur::fadeDuration()),
        0.0f, 1.0f);
    if (fadeDuration > 0.01f) {
        root->setOpacity(0);
        switchBlurToTransparent(root);
        WeakRef<CCNode> weakRoot(root);
        root->runAction(CCSequence::create(
            CCFadeTo::create(fadeDuration, 255),
            CallFuncExt::create([weakRoot] {
                if (auto r = weakRoot.lock()) {
                    switchBlurToOpaque(r);
                }
            }),
            nullptr
        ));
    } else {
        // No fade: opaque blend immediately for max perf.
        switchBlurToOpaque(root);
    }
    return true;
}

static void cleanupImpl(CCNode* popup, float fadeDuration) {
    if (!popup) return;

    auto& reg = blurRegistry();
    auto it = reg.find(popup);
    if (it == reg.end()) return;

    auto* blurNode = it->second.blurRef.data();
    reg.erase(it);  // remove from registry now — excluded from future captures
    unscheduleWatchdogIfIdle();

    if (!blurNode || !blurNode->getParent()) {
        for (auto& [key, entry] : reg) {
            auto* prevBlur = entry.blurRef.data();
            if (prevBlur && !prevBlur->isVisible() && !entry.fadingOut) {
                prevBlur->setVisible(true);
            }
        }
        return;
    }
    for (auto& [key, entry] : reg) {
        auto* prevBlur = entry.blurRef.data();
        if (prevBlur && !prevBlur->isVisible() && !entry.fadingOut) {
            prevBlur->setVisible(true);
        }
    }

    if (fadeDuration <= 0.01f) {
        Ref<CCNode> keepAlive = Ref<CCNode>(blurNode);
        Loader::get()->queueInMainThread([keepAlive]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (auto* node = keepAlive.data(); node && node->getParent()) {
                node->removeFromParent();
            }
        });
        return;
    }

    blurNode->stopAllActions();
    switchBlurToTransparent(blurNode);
    blurNode->runAction(CCSequence::create(
        CCFadeTo::create(fadeDuration, 0),
        CCCallFunc::create(blurNode, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

void cleanup(CCNode* popup) {
    cleanupImpl(popup, 0.f);
}

void cleanupWithFade(CCNode* popup, float duration) {
    cleanupImpl(popup, duration);
}

void cleanupAllActive(float fadeDuration) {
    {
        auto& cache = snapshotCache();
        cache.tex = nullptr;
        cache.size = CCSizeZero;
        cache.frameTime = -1.0;
    }
    {
        auto& bcache = blurResultCache();
        bcache.blurredTex = nullptr;
        bcache.snapshotToken = nullptr;
        bcache.frameTime = -1.0;
    }

    auto& reg = blurRegistry();
    if (reg.empty()) return;

    std::vector<Ref<CCNode>> blurs;
    blurs.reserve(reg.size());
    for (auto& [_, entry] : reg) {
        blurs.push_back(entry.blurRef);
    }
    reg.clear();
    unscheduleWatchdogIfIdle();

    for (auto& ref : blurs) {
        auto* node = ref.data();
        if (!node || !node->getParent()) continue;
        fadeAndRemoveBlurNode(node, fadeDuration);
    }
}

} // namespace paimon::popupblur
