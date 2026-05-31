#include "BeatShaderManager.hpp"

#include "../../audio/services/PaimonAudio.hpp"
#include "../../backgrounds/services/LayerBackgroundManager.hpp"
#include "../../../utils/Shaders.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/cocos/CCDirector.h>
#include <Geode/cocos/layers_scenes_transitions_nodes/CCScene.h>
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/binding/CreatorLayer.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/GJGarageLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>

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

// BeatShaderVanillaSwap — when cfg.type == "default" and beat-shaders is
// enabled, this node walks the layer (and its scene siblings) and swaps
// each fullscreen vanilla CCSprite with a ShaderBgSprite that uses the
// same texture/rect/scale/position but runs the beat shader on top. The
// originals are hidden and kept as Refs so we can restore them on detach.
//
// We do NOT use a CCRenderTexture: that path was unreliable across
// scenes because CCRenderTexture::beginWithClear sets up a viewport at
// pixel resolution while sibling nodes still use the design-resolution
// transform Cocos already pushed for the current frame, producing the
// "1/4 of the bg in a corner" artifact.
//
// Restored on destructor or when the feature is turned off.
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
        // Push into the autorelease pool BEFORE initSwap. This guarantees
        // that even if initSwap fails (or partially registers the node with
        // engine subsystems like the scheduler), the node is released via
        // the normal Cocos2d ref-counted path on the next frame end.
        //
        // Previously this used CC_SAFE_DELETE(node) on init failure, which
        // calls `delete node` directly, bypassing any retains acquired
        // during initSwap (e.g. by scheduleUpdate). That left dangling
        // pointers in the scheduler hash and caused a use-after-free crash
        // on a later cleanup/scheduler tick — manifesting as a NULL-vtable
        // read inside CCNode::cleanup → detachChild when an unrelated
        // popup such as ProfilePage closed.
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
        // NOTE: do NOT call scheduleUpdate() here. The swap node does not
        // override update(), so it would be a no-op anyway, and PaimonAudio
        // is already fed once per frame from ShaderBgSprite::updateShaderTime.
        // Calling scheduleUpdate would also have the engine retain `this`
        // in the scheduler hash; combined with any direct delete on init
        // failure that produced a use-after-free.

        swapFullscreenSprites(layer);
        // Also reach across the scene for siblings (MenuGameLayer's sprites).
        if (auto* parent = layer->getParent()) {
            swapSiblingSprites(parent, layer);
        }

        // If we found NOTHING to swap, fail so the caller doesn't keep us
        // around as a dead node. The applyToLayer code path will simply
        // log and move on — the next time the user opens a layer with a
        // recognised vanilla bg, swap will succeed.
        return !m_swaps.empty();
    }

    // Walk one node's children and swap any fullscreen-sized CCSprite that
    // has a texture (i.e. an actual background image). Excludes our own
    // paimon-* nodes and tiny UI sprites.
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
            // Skip hidden sprites (e.g. main-menu-bg may be hidden when a
            // custom bg is in use — we shouldn't undo that hide).
            if (!spr->isVisible()) continue;
            performSwap(spr, parent);
        }
    }

    // Walk siblings of `targetLayer` in `parent` (the CCScene) and look
    // for fullscreen-sized sprites in their child trees. This handles
    // MenuGameLayer (sibling of MenuLayer) which holds the gameplay
    // background and ground.
    //
    // We are intentionally CONSERVATIVE here: previous versions walked every
    // sibling, which meant the live BeatShaderConfigLayer popup, popup-blur
    // overlays, transition layers, etc. could all have their fullscreen
    // sprites swapped out from under them. When the popup later closed,
    // those swaps would interact with touch dispatching and trigger a
    // use-after-free in CCTargetedTouchHandler. Skip any sibling that has
    // active touch input (i.e. any Popup, FLAlertLayer, custom overlay).
    void swapSiblingSprites(CCNode* parent, cocos2d::CCNode* targetLayer) {
        if (!parent) return;
        auto* siblings = parent->getChildren();
        if (!siblings) return;
        for (int i = 0; i < siblings->count(); ++i) {
            auto* sibling = static_cast<cocos2d::CCNode*>(siblings->objectAtIndex(i));
            if (!sibling || sibling == targetLayer) continue;
            auto id = std::string(sibling->getID());
            if (!id.empty() && id.rfind("paimon-", 0) == 0) continue;
            // Skip CCLayer-derived siblings that have touch enabled.
            // Popups, FLAlertLayers, drag overlays, etc. all set
            // setTouchEnabled(true) on themselves, and hijacking sprites
            // inside their tree creates dangling touch handlers.
            if (auto* siblingLayer = typeinfo_cast<cocos2d::CCLayer*>(sibling)) {
                if (siblingLayer->isTouchEnabled() || siblingLayer->isKeypadEnabled()) {
                    continue;
                }
            }
            // Skip any node typed as a popup-ish container — Geode Popup
            // base sets a known ID prefix on its bg, which we use as a
            // cheap signal to bail.
            if (sibling->getChildByID("geode.loader/alert-bg")
                || sibling->getChildByID("paimon-popup-blur"_spr)) {
                continue;
            }
            // Recursively descend into the remaining sibling tree (the
            // common case is MenuGameLayer with its m_backgroundSprite as
            // a direct child).
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

        // Hide the original and add our replacement at the SAME zOrder so
        // the existing scene composition (groundLayer, decorations, UI)
        // continues to render on top exactly as it did before.
        original->setVisible(false);
        parent->addChild(shaderSpr, originalZ);

        m_swaps.push_back(std::move(swap));
        log::info("[BeatShaders] Swapped vanilla bg sprite at zOrder={} cs={}x{}",
                  originalZ, rect.size.width, rect.size.height);
    }

    void restoreAll() {
        for (auto& swap : m_swaps) {
            if (auto* repl = swap.replacement.data()) {
                // Detach from scheduler explicitly. If the replacement's
                // parent was destroyed via the destructor path (rare —
                // e.g. an unclean teardown) instead of removeFromParent,
                // onExit was never called, so the schedule still points at
                // an orphaned object. Calling unscheduleAllSelectors here
                // is idempotent and protects against that.
                repl->unscheduleAllSelectors();
                if (repl->getParent()) {
                    repl->removeFromParent();
                }
            }
            if (auto* orig = swap.original.data()) {
                // Only restore visibility while the original is still in
                // the tree — if it was already torn down we'd be touching
                // an orphaned node, which is harmless but pointless.
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

// VanillaBgWrapper — schedules per-frame uniform pushes onto a vanilla GD
// CCSprite (e.g. main-menu-bg) without altering its parentage. Functions as
// a lightweight piggyback for the beat-shader uniforms: the sprite still
// renders through CCSprite::draw() and we ride along on its scheduler tick
// to keep u_time and audio uniforms current.
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
        m_time += dt;
        // Drive PaimonAudio once per frame.
        static uint64_t s_lastFrame = 0;
        auto frame = static_cast<uint64_t>(cocos2d::CCDirector::get()->getTotalFrames());
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
    // We can't override draw() of a vanilla CCSprite without a hook, so we
    // push the uniforms once during apply. They persist on the GLSL program
    // for the next draw call. For continuous animation we rely on the
    // VanillaBgWrapper update() ticking — but most beat shaders animate
    // primarily via u_time which is sampled each draw() by Cocos itself
    // through setUniformsForBuiltins(). We still bump u_time / audio
    // uniforms on each apply so live config feedback works.
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

    // Migration: the first version of Beat Shaders shipped with overlay
    // procedural shader IDs (beat-bars, beat-circles, etc.). Those are
    // procedural and don't accept u_texture, so they would fail to load in
    // the postprocess pipeline. Map them onto sensible postprocess
    // equivalents so users coming from a previous build still see something.
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
    auto cfg = getConfig();
    log::info("[BeatShaders] applyToLayer key='{}' enabled={} shader='{}' layerEnabled={}",
        layerKey, cfg.enabled, cfg.shaderName, isLayerEnabled(layerKey));

    bool layerActive = cfg.enabled && isLayerEnabled(layerKey);

    auto& bgMgr = LayerBackgroundManager::get();
    auto bgCfg = bgMgr.getConfig(layerKey);

    if (layerActive) {
        // Force the chosen beat shader on this layer's bg config so any
        // ShaderBgSprite created by LayerBackgroundManager (custom image,
        // GIF, video) runs the beat shader. This covers all non-default
        // bg types automatically.
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

    // Re-apply the background through LayerBackgroundManager. If the user
    // has a custom bg this rebuilds it with our shader baked in.
    bgMgr.applyBackground(layer, layerKey);

    // ── Default-type background path ─────────────────────────────────
    // When cfg.type == "default", LayerBackgroundManager::applyBackground
    // returns early without creating any ShaderBgSprite, so the beat
    // shader has nothing to run on. We bridge that gap here by walking
    // the scene graph and swapping each fullscreen-sized vanilla CCSprite
    // (main-menu-bg in MenuLayer, m_backgroundSprite inside MenuGameLayer,
    // ground sprites, etc.) with a ShaderBgSprite that matches the
    // original transform and runs the beat shader. The originals are
    // hidden and restored on detach.
    bool isDefaultBg = (bgCfg.type == "default");
    auto* existingSwap = static_cast<BeatShaderVanillaSwap*>(
        layer->getChildByID("paimon-beat-vanilla-swap"_spr));

    if (layerActive && isDefaultBg) {
        if (existingSwap) {
            // Already swapped — just refresh shader / config in place.
            existingSwap->setShader(cfg.shaderName);
            existingSwap->updateConfig(cfg);
        } else {
            // ── Skip popup-style layers ─────────────────────────────
            // FLAlertLayer-derived layers (ProfilePage, SetupTriggerPopup,
            // every Geode Popup, etc.) don't own a fullscreen vanilla
            // background — they overlay the existing scene. Attempting
            // the vanilla swap from a popup walks the popup's siblings
            // (= the underlying scene) and previously caused crashes
            // when the popup closed:
            //   - The swap's init runs before show(), so getParent() is
            //     null → siblings are not walked → m_swaps stays empty
            //     → init returns false. Combined with the old
            //     CC_SAFE_DELETE-on-failure path that left the node
            //     scheduled, this produced a dangling pointer in the
            //     scheduler hash that surfaced as a NULL-vtable read
            //     inside CCNode::cleanup → detachChild on a later
            //     keyBackClicked of the popup.
            // Even with that fixed, popups have no own bg sprite — the
            // user's beat shader will still run on a popup's CUSTOM bg
            // (image / GIF / video) through LayerBackgroundManager above.
            if (typeinfo_cast<FLAlertLayer*>(layer)) {
                log::info("[BeatShaders] Skipping vanilla swap on popup layer '{}'", layerKey);
            } else {
                // Defer swap creation to the next main-thread tick.
                // layer->init() runs BEFORE the layer is added to the
                // scene (CCScene::addChild is what wires up the parent),
                // so siblings can't be walked synchronously here. By
                // queueing the work we let the scene settle first and
                // then attempt the swap in a context where getParent()
                // is correct.
                Ref<CCLayer> layerRef = layer;
                std::string keyCopy = layerKey;
                BeatShaderConfig cfgCopy = cfg;
                Loader::get()->queueInMainThread([layerRef, keyCopy, cfgCopy]() {
                    auto* l = layerRef.data();
                    if (!l) return;
                    if (!l->getParent()) {
                        // Layer was never added to a scene, or already
                        // removed before our deferred work ran.
                        return;
                    }
                    // Re-check existence of an existing swap — applyToLayer
                    // may have been called multiple times in the meantime.
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
        // Update the visual intensity too so the shader's u_intensity tracks
        // the user's slider in real time.
        spr->m_shaderIntensity = cfg.intensity;
    }

    // Also update any active vanilla-bg swap nodes — those wrap one or
    // more ShaderBgSprite replacements that are already in `sprites`,
    // but updateConfig() also refreshes the shader name / program if
    // that changed since the last apply call.
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
    // For every supported layer in the running scene, re-apply the beat
    // shader. This rebuilds the LayerBackgroundManager sprite for custom
    // bgs and re-creates the vanilla-bg ShaderBgSprite for default bgs.
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
