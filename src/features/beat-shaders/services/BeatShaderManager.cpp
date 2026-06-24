#include "BeatShaderManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include "../../audio/services/PaimonAudio.hpp"
#include "../../backgrounds/services/LayerBackgroundManager.hpp"
#include "../../../utils/Shaders.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/CCDirector.h>
#include <Geode/cocos/layers_scenes_transitions_nodes/CCScene.h>

using namespace geode::prelude;

namespace paimon::beat_shaders {

namespace {

constexpr char const* kKeyEnabled    = "beat-shaders-enabled";
constexpr char const* kKeyShader     = "beat-shaders-shader";
constexpr char const* kKeyIntensity  = "beat-shaders-intensity";
constexpr char const* kKeyBassMult   = "beat-shaders-bass-mult";
constexpr char const* kKeyMidMult    = "beat-shaders-mid-mult";
constexpr char const* kKeyTrebleMult = "beat-shaders-treble-mult";
constexpr char const* kKeyBeatMult   = "beat-shaders-beat-mult";
constexpr char const* kKeyEnergyMult = "beat-shaders-energy-mult";

// Tag value used to mark sprites whose shader we hijacked so we can clean
// them up when the feature is disabled or the layer changes.
constexpr int kHijackedSpriteTag = 0x4245415; // 'BEAS' (custom magic)

std::string layerEnabledKey(std::string const& layer) {
    return std::string("beat-shaders-layer-") + layer;
}

// BeatShaderVanillaSwap — for default backgrounds: swaps each fullscreen vanilla
// CCSprite with a ShaderBgSprite running the beat shader, hiding the originals
// (kept as Refs) and restoring them on detach. Avoids CCRenderTexture, which gave
// viewport-mismatch artifacts across scenes. Restored in the destructor or when off.
class BeatShaderVanillaSwap : public CCNode {
public:
    struct SwappedSprite {
        Ref<cocos2d::CCSprite>           original;     // hidden
        Ref<Shaders::ShaderBgSprite>     replacement;  // visible, runs shader
        Ref<cocos2d::CCNode>             parent;       // where replacement was added
        int                              originalZOrder = 0;
    };

    std::vector<SwappedSprite>      m_swaps;
    cocos2d::CCSize                 m_winSize;
    std::string                     m_shaderName;
    BeatShaderConfig                m_cfg;
    Ref<cocos2d::CCLayer>           m_targetLayer;

    static BeatShaderVanillaSwap* createForLayer(
        CCLayer* layer,
        std::string const& shaderName,
        BeatShaderConfig const& cfg
    ) {
        if (!layer) return nullptr;
        auto* node = new BeatShaderVanillaSwap();
        if (!node) return nullptr;
        // Autorelease BEFORE initSwap so a failed init still releases via the normal
        // ref-counted path (a direct delete left dangling scheduler entries → use-after-free).
        node->autorelease();
        if (!node->initSwap(layer, shaderName, cfg)) {
            return nullptr;
        }
        return node;
    }

    bool initSwap(CCLayer* layer, std::string const& shaderName, BeatShaderConfig const& cfg) {
        if (!CCNode::init()) return false;

        m_winSize     = cocos2d::CCDirector::get()->getWinSize();
        m_shaderName  = shaderName;
        m_cfg         = cfg;
        m_targetLayer = layer;
        this->setID("paimon-beat-vanilla-swap"_spr);
        this->setContentSize({0, 0});
        this->setVisible(false); // tick-only, never draws itself
        // Don't scheduleUpdate() here: no update() override, and it would retain
        // this in the scheduler hash.

        swapFullscreenSprites(layer);
        // Also reach across the scene for siblings (MenuGameLayer's sprites).
        if (auto* parent = layer->getParent()) {
            swapSiblingSprites(parent, layer);
        }

        // Fail if nothing was swapped so the caller doesn't keep a dead node.
        return !m_swaps.empty();
    }

    // Swap any fullscreen textured CCSprite child (excludes paimon-* and small UI sprites).
    void swapFullscreenSprites(CCNode* parent) {
        if (!parent) return;
        auto* children = parent->getChildren();
        if (!children) return;

        // Snapshot first — we'll mutate the parent's children below.
        std::vector<cocos2d::CCNode*> snapshot;
        snapshot.reserve(children->count());
        for (int i = 0; i < children->count(); ++i) {
            snapshot.push_back(static_cast<cocos2d::CCNode*>(children->objectAtIndex(i)));
        }

        for (auto* child : snapshot) {
            if (!child) continue;
            auto id = std::string(child->getID());
            if (!id.empty() && id.rfind("paimon-", 0) == 0) continue;
            auto* spr = typeinfo_cast<cocos2d::CCSprite*>(child);
            if (!spr) continue;
            if (!spr->getTexture()) continue;
            auto cs = spr->getContentSize();
            if (cs.width < m_winSize.width * 0.5f) continue;
            if (cs.height < m_winSize.height * 0.5f) continue;
            // Skip hidden sprites (don't undo a custom-bg hide).
            if (!spr->isVisible()) continue;
            performSwap(spr, parent);
        }
    }

    // Swap fullscreen sprites in sibling trees (e.g. MenuGameLayer's bg/ground).
    // Conservative: skip siblings with touch input (popups/overlays) to avoid
    // dangling touch handlers when they close.
    void swapSiblingSprites(CCNode* parent, cocos2d::CCNode* targetLayer) {
        if (!parent) return;
        auto* siblings = parent->getChildren();
        if (!siblings) return;
        for (int i = 0; i < siblings->count(); ++i) {
            auto* sibling = static_cast<cocos2d::CCNode*>(siblings->objectAtIndex(i));
            if (!sibling || sibling == targetLayer) continue;
            auto id = std::string(sibling->getID());
            if (!id.empty() && id.rfind("paimon-", 0) == 0) continue;
            // Skip touch-enabled CCLayer siblings (popups/overlays); hijacking
            // their sprites dangles touch handlers.
            if (auto* siblingLayer = typeinfo_cast<cocos2d::CCLayer*>(sibling)) {
                if (siblingLayer->isTouchEnabled() || siblingLayer->isKeypadEnabled()) {
                    continue;
                }
            }
            // Skip popup-ish containers (detected via known bg IDs).
            if (sibling->getChildByID("geode.loader/alert-bg")
                || sibling->getChildByID("paimon-popup-blur"_spr)) {
                continue;
            }
            // Descend into the remaining sibling tree.
            walkAndSwap(sibling, /*depth*/0);
        }
    }

    void walkAndSwap(cocos2d::CCNode* node, int depth) {
        if (!node) return;
        if (depth > 3) return; // bound recursion
        auto id = std::string(node->getID());
        if (!id.empty() && id.rfind("paimon-", 0) == 0) return;

        if (auto* spr = typeinfo_cast<cocos2d::CCSprite*>(node)) {
            auto cs = spr->getContentSize();
            if (spr->getTexture() && spr->isVisible()
                && cs.width  >= m_winSize.width  * 0.5f
                && cs.height >= m_winSize.height * 0.5f) {
                if (auto* p = spr->getParent()) {
                    performSwap(spr, p);
                }
                return;
            }
        }

        auto* children = node->getChildren();
        if (!children) return;
        std::vector<cocos2d::CCNode*> snapshot;
        snapshot.reserve(children->count());
        for (int i = 0; i < children->count(); ++i) {
            snapshot.push_back(static_cast<cocos2d::CCNode*>(children->objectAtIndex(i)));
        }
        for (auto* c : snapshot) {
            walkAndSwap(c, depth + 1);
        }
    }

    void performSwap(cocos2d::CCSprite* original, cocos2d::CCNode* parent) {
        if (!original || !parent) return;
        auto* tex = original->getTexture();
        if (!tex) return;

        auto rect = original->getTextureRect();
        auto* shaderSpr = Shaders::ShaderBgSprite::createWithTexture(tex);
        if (!shaderSpr) return;

        // Match original sprite's rect, transform, and color so the swap
        // is visually invisible without the shader.
        shaderSpr->setTextureRect(rect);
        shaderSpr->setAnchorPoint(original->getAnchorPoint());
        shaderSpr->setPosition(original->getPosition());
        shaderSpr->setScaleX(original->getScaleX());
        shaderSpr->setScaleY(original->getScaleY());
        shaderSpr->setRotation(original->getRotation());
        shaderSpr->setSkewX(original->getSkewX());
        shaderSpr->setSkewY(original->getSkewY());
        shaderSpr->setColor(original->getColor());
        shaderSpr->setOpacity(original->getOpacity());
        shaderSpr->setFlipX(original->isFlipX());
        shaderSpr->setFlipY(original->isFlipY());
        shaderSpr->setVisible(true);
        shaderSpr->setID("paimon-beat-vanilla-swap-spr"_spr);

        auto* program = Shaders::getBgShaderProgram(m_shaderName);
        if (program) shaderSpr->setShaderProgram(program);

        shaderSpr->m_shaderIntensity = m_cfg.intensity;
        shaderSpr->m_screenW         = m_winSize.width;
        shaderSpr->m_screenH         = m_winSize.height;
        shaderSpr->m_shaderTime      = 0.f;
        shaderSpr->m_audioReactive   = m_cfg.enabled ? 1.f : 0.f;
        shaderSpr->m_bassMult        = m_cfg.bassMult;
        shaderSpr->m_midMult         = m_cfg.midMult;
        shaderSpr->m_trebleMult      = m_cfg.trebleMult;
        shaderSpr->m_beatMult        = m_cfg.beatMult;
        shaderSpr->m_energyMult      = m_cfg.energyMult;
        shaderSpr->schedule(schedule_selector(Shaders::ShaderBgSprite::updateShaderTime));

        int originalZ = original->getZOrder();
        SwappedSprite swap;
        swap.original       = original;
        swap.replacement    = shaderSpr;
        swap.parent         = parent;
        swap.originalZOrder = originalZ;

        // Hide the original and add the replacement at the same zOrder to keep composition.
        original->setVisible(false);
        parent->addChild(shaderSpr, originalZ);

        m_swaps.push_back(std::move(swap));
        log::info("[BeatShaders] Swapped vanilla bg sprite at zOrder={} cs={}x{}",
                  originalZ, rect.size.width, rect.size.height);
    }

    void restoreAll() {
        for (auto& swap : m_swaps) {
            if (auto* repl = swap.replacement.data()) {
                // Detach from scheduler explicitly (idempotent) in case onExit never ran.
                repl->unscheduleAllSelectors();
                if (repl->getParent()) {
                    repl->removeFromParent();
                }
            }
            if (auto* orig = swap.original.data()) {
                // Only restore visibility if the original is still in the tree.
                if (orig->getParent()) {
                    orig->setVisible(true);
                }
            }
        }
        m_swaps.clear();
    }

    ~BeatShaderVanillaSwap() override {
        restoreAll();
    }

    // Apply live config changes to all replacement sprites without
    // recreating them.
    void updateConfig(BeatShaderConfig const& cfg) {
        m_cfg = cfg;
        for (auto& swap : m_swaps) {
            auto* spr = swap.replacement.data();
            if (!spr) continue;
            spr->m_shaderIntensity = cfg.intensity;
            spr->m_audioReactive   = cfg.enabled ? 1.f : 0.f;
            spr->m_bassMult        = cfg.bassMult;
            spr->m_midMult         = cfg.midMult;
            spr->m_trebleMult      = cfg.trebleMult;
            spr->m_beatMult        = cfg.beatMult;
            spr->m_energyMult      = cfg.energyMult;
        }
    }

    // Replace the running shader program on every replacement sprite.
    void setShader(std::string const& shaderName) {
        if (m_shaderName == shaderName) return;
        m_shaderName = shaderName;
        auto* program = Shaders::getBgShaderProgram(shaderName);
        if (!program) return;
        for (auto& swap : m_swaps) {
            if (auto* spr = swap.replacement.data()) {
                spr->setShaderProgram(program);
            }
        }
    }
};

// VanillaBgWrapper — ticks per-frame uniform pushes onto a vanilla CCSprite
// (e.g. main-menu-bg) to keep u_time/audio uniforms current without reparenting it.
class VanillaBgWrapper : public CCNode {
public:
    cocos2d::CCSprite* m_target = nullptr;
    float              m_time   = 0.f;
    cocos2d::CCSize    m_screenSize;

    static VanillaBgWrapper* attach(cocos2d::CCSprite* target) {
        if (!target) return nullptr;
        // Reuse existing wrapper if already attached.
        if (auto* prev = target->getChildByID("paimon-beat-vanilla-tick"_spr)) {
            return static_cast<VanillaBgWrapper*>(prev);
        }
        auto* w = new VanillaBgWrapper();
        if (!w) return nullptr;
        w->autorelease();
        w->m_target = target;
        w->setID("paimon-beat-vanilla-tick"_spr);
        w->setVisible(false); // tick-only node, draws nothing
        target->addChild(w);
        w->m_screenSize = cocos2d::CCDirector::get()->getWinSize();
        w->scheduleUpdate();
        return w;
    }

    void update(float dt) override {
        if (paimon::isRuntimeShuttingDown()) {
            unscheduleUpdate();
            return;
        }
        m_time += dt;
        static uint64_t s_lastFrame = 0;
        auto* director = cocos2d::CCDirector::get();
        if (!director) return;
        auto frame = static_cast<uint64_t>(director->getTotalFrames());
        if (frame != s_lastFrame) {
            s_lastFrame = frame;
            PaimonAudio::get().update(dt);
        }
    }
};

void pushUniformsForVanillaSprite(cocos2d::CCSprite* sprite, BeatShaderConfig const& cfg) {
    if (!sprite) return;
    auto* shader = sprite->getShaderProgram();
    if (!shader) return;
    // Can't override a vanilla sprite's draw() without a hook, so push the uniforms
    // here; they persist on the program for the next draw. Cocos samples u_time each
    // draw via setUniformsForBuiltins, so we just bump uniforms on apply for live feedback.
    shader->use();
    GLint loc;

    auto winSize = cocos2d::CCDirector::get()->getWinSize();
    loc = shader->getUniformLocationForName("u_intensity");
    if (loc != -1) shader->setUniformLocationWith1f(loc, cfg.intensity);

    loc = shader->getUniformLocationForName("u_screenSize");
    if (loc != -1) shader->setUniformLocationWith2f(loc, winSize.width, winSize.height);
}

// Find the vanilla GD background sprite for a given layer key. Returns the
// CCSprite that we can attach a shader program to.
cocos2d::CCSprite* findVanillaBgSprite(cocos2d::CCLayer* layer) {
    if (!layer) return nullptr;
    static char const* ids[] = {"main-menu-bg", "background", "bg", "bg-texture", nullptr};
    for (int i = 0; ids[i]; ++i) {
        if (auto* node = layer->getChildByID(ids[i])) {
            if (auto* spr = typeinfo_cast<cocos2d::CCSprite*>(node)) return spr;
        }
    }
    // Fallback: largest fullscreen-sized child sprite.
    auto* children = layer->getChildren();
    if (!children) return nullptr;
    auto ws = cocos2d::CCDirector::get()->getWinSize();
    for (int i = 0; i < children->count(); ++i) {
        auto* child = static_cast<cocos2d::CCNode*>(children->objectAtIndex(i));
        auto* spr = typeinfo_cast<cocos2d::CCSprite*>(child);
        if (!spr) continue;
        auto cs = spr->getContentSize();
        if (cs.width >= ws.width * 0.5f && cs.height >= ws.height * 0.5f
            && spr->isVisible()) {
            return spr;
        }
    }
    return nullptr;
}

// Recursively walk a node tree and collect every ShaderBgSprite child.
void collectShaderSprites(CCNode* node, std::vector<Shaders::ShaderBgSprite*>& out) {
    if (!node) return;
    if (auto* spr = typeinfo_cast<Shaders::ShaderBgSprite*>(node)) {
        out.push_back(spr);
    }
    auto* children = node->getChildren();
    if (!children) return;
    for (int i = 0; i < children->count(); ++i) {
        if (auto* child = static_cast<CCNode*>(children->objectAtIndex(i))) {
            collectShaderSprites(child, out);
        }
    }
}

CCLayer* findLayerByKey(std::string const& key) {
    auto* dir = CCDirector::get();
    if (!dir) return nullptr;
    auto* scene = dir->getRunningScene();
    if (!scene) return nullptr;

    auto* children = scene->getChildren();
    if (!children) return nullptr;

    for (int i = 0; i < children->count(); ++i) {
        auto* child = static_cast<CCNode*>(children->objectAtIndex(i));
        if (!child) continue;

        if (key == "menu") {
            if (auto* l = typeinfo_cast<MenuLayer*>(child)) return l;
        } else if (key == "creator") {
            if (auto* l = typeinfo_cast<CreatorLayer*>(child)) return l;
        } else if (key == "levelinfo") {
            if (auto* l = typeinfo_cast<LevelInfoLayer*>(child)) return l;
        } else if (key == "garage") {
            if (auto* l = typeinfo_cast<GJGarageLayer*>(child)) return l;
        }
    }
    return nullptr;
}

} // anonymous namespace

BeatShaderManager& BeatShaderManager::get() {
    static BeatShaderManager instance;
    return instance;
}

BeatShaderConfig BeatShaderManager::getConfig() const {
    auto* mod = Mod::get();
    BeatShaderConfig cfg;
    cfg.enabled    = mod->getSavedValue<bool>(kKeyEnabled, false);
    cfg.shaderName = mod->getSavedValue<std::string>(kKeyShader, std::string("glitch-beat"));
    cfg.intensity  = static_cast<float>(mod->getSavedValue<double>(kKeyIntensity, 0.7));
    cfg.bassMult   = static_cast<float>(mod->getSavedValue<double>(kKeyBassMult, 1.0));
    cfg.midMult    = static_cast<float>(mod->getSavedValue<double>(kKeyMidMult, 1.0));
    cfg.trebleMult = static_cast<float>(mod->getSavedValue<double>(kKeyTrebleMult, 1.0));
    cfg.beatMult   = static_cast<float>(mod->getSavedValue<double>(kKeyBeatMult, 1.0));
    cfg.energyMult = static_cast<float>(mod->getSavedValue<double>(kKeyEnergyMult, 1.0));

    // Migration: map legacy overlay shader IDs (beat-bars, etc.) onto a postprocess
    // equivalent, since those procedural shaders don't accept u_texture.
    static auto const isLegacy = [](std::string const& s) {
        return s == "beat-bars" || s == "beat-circles" || s == "beat-grid"
            || s == "freq-spectrum" || s == "beat-tunnel" || s == "audio-aurora";
    };
    if (isLegacy(cfg.shaderName)) {
        cfg.shaderName = "glitch-beat";
    }
    return cfg;
}

void BeatShaderManager::saveConfig(BeatShaderConfig const& cfg) {
    auto* mod = Mod::get();
    mod->setSavedValue<bool>(kKeyEnabled, cfg.enabled);
    mod->setSavedValue<std::string>(kKeyShader, cfg.shaderName);
    mod->setSavedValue<double>(kKeyIntensity, static_cast<double>(cfg.intensity));
    mod->setSavedValue<double>(kKeyBassMult, static_cast<double>(cfg.bassMult));
    mod->setSavedValue<double>(kKeyMidMult, static_cast<double>(cfg.midMult));
    mod->setSavedValue<double>(kKeyTrebleMult, static_cast<double>(cfg.trebleMult));
    mod->setSavedValue<double>(kKeyBeatMult, static_cast<double>(cfg.beatMult));
    mod->setSavedValue<double>(kKeyEnergyMult, static_cast<double>(cfg.energyMult));

    // Flip the global gate so any ShaderBgSprite pushing uniforms picks up
    // the new state immediately (used by all bg sprites, not just ours).
    Shaders::ShaderBgSpriteAudioGate::setEnabled(cfg.enabled);

    if (cfg.enabled) activateAudioIfNeeded();
    else deactivateAudioIfUnused();
}

bool BeatShaderManager::isLayerEnabled(std::string const& key) const {
    return Mod::get()->getSavedValue<bool>(layerEnabledKey(key), true);
}

void BeatShaderManager::setLayerEnabled(std::string const& key, bool enabled) {
    Mod::get()->setSavedValue<bool>(layerEnabledKey(key), enabled);
}

std::vector<BeatShaderManager::ShaderEntry> BeatShaderManager::availableShaders() const {
    return {
        {"glitch-beat",       "Glitch",        "Slicing horizontal + chromatico, intensifica con bass."},
        {"wave-beat",         "Wave",          "Distorsion sinusoidal con frecuencia/amplitud reactiva."},
        {"chromatic-beat",    "Chromatic",     "Aberracion cromatica radial que pulsa con beat."},
        {"pixelate-beat",     "Pixelate",      "Tamano de pixel variable segun bass."},
        {"shockwave-beat",    "Shockwave",     "Anillos radiales emitidos con cada beat."},
        {"rgb-split-beat",    "RGB Split",     "Separacion fuerte + zoom pulse en cada beat."},
        {"kaleidoscope-beat", "Kaleidoscope",  "Espejo caleidoscopico, segmentos crecen con bass."},
        {"zoom-pulse-beat",   "Zoom Pulse",    "Zoom radial que late con cada golpe."},
        {"scanlines-beat",    "Scanlines",     "Lineas CRT animadas, banda brillante con beat."},
        {"vortex-beat",       "Vortex",        "Remolino central que se intensifica con bass+beat."},
        {"edge-pulse-beat",   "Edge Pulse",    "Detector de bordes con neon que pulsa al ritmo."},
        {"hue-shift-beat",    "Hue Shift",     "Rotacion de tono continua + saturacion con bass."},
        {"liquid-beat",       "Liquid",        "Distorsion fluida tipo agua, bass = amplitud."},
        {"mosaic-beat",       "Mosaic",        "Bloques voronoi pulsando, celdas crecen con bass."},
        {"dream-beat",        "Dream",         "Soft blur + halo + sparkles que respiran con la musica."},
    };
}

std::vector<std::pair<std::string, std::string>> BeatShaderManager::availableLayers() const {
    return {
        {"menu",         "Menu"},
        {"creator",      "Creator"},
        {"levelinfo",    "Level Info"},
        {"levelselect",  "Level Select"},
        {"browser",      "Browser"},
        {"search",       "Search"},
        {"leaderboards", "Leaderboards"},
        {"profile",      "Profile"},
        {"garage",       "Garage"},
    };
}

void BeatShaderManager::applyToLayer(CCLayer* layer, std::string const& layerKey) {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
    auto cfg = getConfig();
    log::info("[BeatShaders] applyToLayer key='{}' enabled={} shader='{}' layerEnabled={}",
        layerKey, cfg.enabled, cfg.shaderName, isLayerEnabled(layerKey));

    bool layerActive = cfg.enabled && isLayerEnabled(layerKey);

    auto& bgMgr = LayerBackgroundManager::get();
    auto bgCfg = bgMgr.getConfig(layerKey);

    if (layerActive) {
        // Force the beat shader on this layer's bg config so any ShaderBgSprite
        // (image/GIF/video) created by LayerBackgroundManager runs it.
        bgCfg.shader = cfg.shaderName;
    } else {
        // Clear only OUR shader IDs — preserve any user-picked filter.
        std::string s = bgCfg.shader;
        if (s == "glitch-beat" || s == "wave-beat" || s == "chromatic-beat"
            || s == "pixelate-beat" || s == "shockwave-beat" || s == "rgb-split-beat"
            || s == "kaleidoscope-beat" || s == "zoom-pulse-beat" || s == "scanlines-beat"
            || s == "vortex-beat" || s == "edge-pulse-beat" || s == "hue-shift-beat"
            || s == "liquid-beat" || s == "mosaic-beat" || s == "dream-beat") {
            bgCfg.shader = "none";
        }
    }
    bgMgr.saveConfig(layerKey, bgCfg);

    // Make sure FMOD FFT is hot.
    if (layerActive) activateAudioIfNeeded();

    if (!layer) {
        // No layer to mutate yet — saved-value side is enough.
        return;
    }

    // Re-apply via LayerBackgroundManager. Skip MenuLayer: it owns its own wallpaper
    // path, and applyBackground there duplicates containers.
    if (layerKey != "menu") {
        bgMgr.applyBackground(layer, layerKey);
    }

    // Default bg path: applyBackground creates no ShaderBgSprite, so swap each
    // fullscreen vanilla sprite with one running the beat shader (originals hidden,
    // restored on detach).
    bool isDefaultBg = (bgCfg.type == "default");
    auto* existingSwap = static_cast<BeatShaderVanillaSwap*>(
        layer->getChildByID("paimon-beat-vanilla-swap"_spr));

    if (layerActive && isDefaultBg) {
        if (existingSwap) {
            // Already swapped — just refresh shader / config in place.
            existingSwap->setShader(cfg.shaderName);
            existingSwap->updateConfig(cfg);
        } else {
            // Skip popup-style (FLAlertLayer-derived) layers: they have no fullscreen
            // vanilla bg, and swapping their siblings crashed on close. Their custom
            // bg still gets the shader via LayerBackgroundManager above.
            if (typeinfo_cast<FLAlertLayer*>(layer)) {
                log::info("[BeatShaders] Skipping vanilla swap on popup layer '{}'", layerKey);
            } else {
                // Defer swap to the next tick: init() runs before the layer is added
                // to a scene, so siblings can't be walked yet.
                Ref<CCLayer> layerRef = layer;
                std::string keyCopy = layerKey;
                BeatShaderConfig cfgCopy = cfg;
                Loader::get()->queueInMainThread([layerRef, keyCopy, cfgCopy]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (BeatShaderManager::get().isShuttingDown()) return;
                    auto* l = layerRef.data();
                    if (!l) return;
                    if (!l->getParent()) {
                        // Layer never added to a scene, or removed before deferred work ran.
                        return;
                    }
                    // Re-check for an existing swap (applyToLayer may have run again).
                    if (l->getChildByID("paimon-beat-vanilla-swap"_spr)) return;
                    if (auto* swapNode = BeatShaderVanillaSwap::createForLayer(
                            l, cfgCopy.shaderName, cfgCopy)) {
                        l->addChild(swapNode);
                        log::info("[BeatShaders] (deferred) Swapped vanilla bg sprites on '{}'", keyCopy);
                    } else {
                        log::warn("[BeatShaders] (deferred) No vanilla bg sprites found to swap on '{}'", keyCopy);
                    }
                });
            }
        }
    } else {
        // Feature off OR a custom background is in use → remove our swap
        // so vanilla rendering takes over.
        if (existingSwap) {
            existingSwap->removeFromParent();
            log::info("[BeatShaders] Removed vanilla-swap from '{}'", layerKey);
        }
    }
}

void BeatShaderManager::refreshLiveSpriteUniforms() {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
    auto* dir = CCDirector::get();
    if (!dir) return;
    auto* scene = dir->getRunningScene();
    if (!scene) return;

    auto cfg = getConfig();
    float gate = cfg.enabled ? 1.0f : 0.0f;

    std::vector<Shaders::ShaderBgSprite*> sprites;
    collectShaderSprites(scene, sprites);

    for (auto* spr : sprites) {
        spr->m_audioReactive  = gate;
        spr->m_bassMult       = cfg.bassMult;
        spr->m_midMult        = cfg.midMult;
        spr->m_trebleMult     = cfg.trebleMult;
        spr->m_beatMult       = cfg.beatMult;
        spr->m_energyMult     = cfg.energyMult;
        spr->m_shaderIntensity = cfg.intensity;
    }

    // Also update active vanilla-bg swap nodes (updateConfig refreshes shader name/program too).
    std::vector<BeatShaderVanillaSwap*> swaps;
    std::function<void(CCNode*)> walk = [&](CCNode* node) {
        if (!node) return;
        if (auto* sw = typeinfo_cast<BeatShaderVanillaSwap*>(node)) {
            swaps.push_back(sw);
        }
        if (auto* children = node->getChildren()) {
            for (int i = 0; i < children->count(); ++i) {
                walk(static_cast<CCNode*>(children->objectAtIndex(i)));
            }
        }
    };
    walk(scene);
    for (auto* sw : swaps) {
        sw->updateConfig(cfg);
        sw->setShader(cfg.shaderName);
    }

    log::debug("[BeatShaders] refreshLiveSpriteUniforms sprites={} swaps={} gate={:.1f}",
        sprites.size(), swaps.size(), gate);
}

void BeatShaderManager::rebuildBackgrounds() {
    if (m_shuttingDown || paimon::isRuntimeShuttingDown()) return;
    // Re-apply the beat shader on every supported layer in the running scene
    // (rebuilds custom-bg sprites and re-creates the vanilla-bg ShaderBgSprite).
    for (auto const& [key, _] : availableLayers()) {
        if (auto* layer = findLayerByKey(key)) {
            applyToLayer(layer, key);
        }
    }
    refreshLiveSpriteUniforms();
}

void BeatShaderManager::init() {
    auto cfg = getConfig();
    Shaders::ShaderBgSpriteAudioGate::setEnabled(cfg.enabled);
    if (cfg.enabled) activateAudioIfNeeded();
}

void BeatShaderManager::shutdown() {
    m_shuttingDown = true;
    deactivateAudioIfUnused();
}

void BeatShaderManager::activateAudioIfNeeded() {
    if (m_audioActive) return;
    PaimonAudio::get().activate();
    m_audioActive = true;
}

void BeatShaderManager::deactivateAudioIfUnused() {
    if (!m_audioActive) return;
    PaimonAudio::get().deactivate();
    m_audioActive = false;
}

} // namespace paimon::beat_shaders
