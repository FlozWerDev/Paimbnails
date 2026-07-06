// Offscreen FBO level capture. Falls back to async PBO back-buffer read.

#include "FramebufferCapture.hpp"
#include "SceneCapture.hpp"
#include "CaptureDeathTracker.hpp"
#include "CaptureVisibilityState.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/PlayerToggleHelper.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/binding/ShaderLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/CheckpointObject.hpp>
#include <Geode/binding/CCCircleWave.hpp>
#include <Geode/binding/ExplodeItemNode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#ifdef GEODE_IS_WINDOWS
#include <excpt.h>
#endif

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif

// PlayLayer::flipArt (flipArt+0x29) and GJBaseGameLayer::updateCameraBGArt
// (updateCameraBGArt+0x1a8, null read at 0x0) crash — both reached via
// GJBaseGameLayer::updateCamera, also through Globed's updateCamera hook —
// when updateCamera runs out-of-band during the offscreen capture render:
// mid-drawScene, dt=0, with the cocos view retargeted to the capture
// resolution. Both only reposition/flip background art, so they're suppressed
// while the capture render owns the camera; the real game loop re-evaluates
// them on the next frame.
static std::atomic<bool> s_suppressCameraArt{false};

struct SuppressCameraArtGuard {
    SuppressCameraArtGuard()  { s_suppressCameraArt.store(true,  std::memory_order_relaxed); }
    ~SuppressCameraArtGuard() { s_suppressCameraArt.store(false, std::memory_order_relaxed); }
};

class $modify(PaimonCaptureFlipArtGuard, PlayLayer) {
    void flipArt(bool flip) {
        if (s_suppressCameraArt.load(std::memory_order_relaxed)) return;
        PlayLayer::flipArt(flip);
    }
};

class $modify(PaimonCaptureBGArtGuard, GJBaseGameLayer) {
    void updateCameraBGArt(cocos2d::CCPoint position, float zoom) {
        if (s_suppressCameraArt.load(std::memory_order_relaxed)) return;
        GJBaseGameLayer::updateCameraBGArt(position, zoom);
    }
};

// Last-resort net for the out-of-band recalc calls in renderPlayLayerToTexture:
// vanilla updateCamera/preUpdateVisibility keep finding new null-derefs when run
// mid-drawScene with the view retargeted (updateCameraBGArt null at 0x0,
// preUpdateVisibility+0x363 null read at 0x1D0). Both calls are best-effort
// repositioning/culling for the capture aspect; on an access violation the rest
// of that call is skipped and the render continues — the real game loop
// recomputes everything on the next frame. SEH frames can't hold C++ objects
// with destructors, hence the raw function-pointer shape.
#ifdef GEODE_IS_WINDOWS
static bool sehGuardedCall(void (*fn)(PlayLayer*), PlayLayer* pl) noexcept {
    __try {
        fn(pl);
        return true;
    } __except (GetExceptionCode() == 0xC0000005L /* EXCEPTION_ACCESS_VIOLATION */
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
#else
static bool sehGuardedCall(void (*fn)(PlayLayer*), PlayLayer* pl) {
    fn(pl);
    return true;
}
#endif

FramebufferCapture::CaptureRequest FramebufferCapture::s_request;
std::vector<FramebufferCapture::DeferredCallback> FramebufferCapture::s_deferredCallbacks;
bool FramebufferCapture::s_isCapturing  = false;
int  FramebufferCapture::s_captureW     = 0;
int  FramebufferCapture::s_captureH     = 0;
int  FramebufferCapture::s_maxTextureSize = 0;
bool FramebufferCapture::s_hdrMode      = false;

namespace {

enum class Phase {
    Idle,
    ArmedHide,
    WaitingFrame,
    MapPBO,
    Reading,
};

std::atomic<Phase> g_phase{Phase::Idle};

// Bumped on requestCapture/cancelPending; worker threads skip stale callbacks.
std::atomic<uint64_t> g_generation{0};

int g_waitingTicks = 0;
constexpr int kMaxWaitingTicks = 6;

// WeakRef to avoid UAF on short-lived nodes (CCCircleWave, notifications, etc).
using HiddenNodeList = std::vector<std::pair<geode::WeakRef<CCNode>, bool>>;

struct HiddenGameObjectState {
    geode::WeakRef<GameObject> obj;
    bool disabled2  = false;
    bool invisible  = false;
    bool visible    = true;
    bool hide       = false;
    uint8_t opacity = 255;
};

struct CapturePrepState {
    bool active = false;

    HiddenNodeList                     hiddenNodes;
    std::vector<HiddenGameObjectState> hiddenGameObjects;

    bool             playerVisActive = false;
    bool             playerHide1     = false;
    bool             playerHide2     = false;
    PlayerVisState   player1State;
    PlayerVisState   player2State;

        bool      plSnapActive = false;
    geode::WeakRef<cocos2d::CCNode> plRef;
    CCPoint   plPos{};
    float     plScaleX = 1.f;
    float     plScaleY = 1.f;
    CCPoint   plAnchor{};
};

CapturePrepState g_prep;

// Inspired by cdc-sys/level-thumbs-mod
void hideGameplayEffectNodes(PlayLayer* pl, HiddenNodeList& hidden) {
    if (!pl) return;
    auto hideNode = [&](CCNode* n) {
        if (n && n->isVisible()) {
            hidden.push_back({n, true});
            n->setVisible(false);
        }
    };

    if (pl->m_circleWaveArray) {
        for (auto* cw : CCArrayExt<CCCircleWave*>(pl->m_circleWaveArray)) {
            if (!cw) continue;
            if (cw->m_target == pl->m_player1 || cw->m_target == pl->m_player2) {
                hideNode(cw);
            }
        }
    }

    if (pl->m_objectLayer) {
        for (auto* child : CCArrayExt<CCNode*>(pl->m_objectLayer->getChildren())) {
            if (typeinfo_cast<ExplodeItemNode*>(child)) hideNode(child);
        }
    }
}

// Depth-first ID walk; skips batch-nodes / particles / GameObjects (no mod IDs).
void hideKnownModNodes(PlayLayer* pl, HiddenNodeList& hidden) {
    if (!pl) return;

    // Nodes the user explicitly kept visible in the layer editor are exempt.
    auto const& userShown = paimon::capture::userShownNodes();

    static const std::unordered_set<std::string_view> kModNodeIds = {
        "mat.run-info/RunInfoWidget",
        "cheeseworks.speedruntimer/timer",
        "sawblade.dim_mode/opacityLabel",
        "zilko.xdbot/state-label",
        "zilko.xdbot/frame-label",
        "zilko.xdbot/recording-audio-label",
        "zilko.xdbot/button-menu",
        "tobyadd.gdh/labels_top_left",
        "tobyadd.gdh/labels_top_right",
        "tobyadd.gdh/labels_bottom_left",
        "tobyadd.gdh/labels_bottom_right",
        "tobyadd.gdh/labels_bottom",
        "tobyadd.gdh/labels_top",
        "thesillydoggo.qolmod/noclip-tint-overlay",
        "eclipse.eclipse-menu/hitboxes",
        "eclipse.eclipse-menu/show-trajectory-draw-node",
    };
    constexpr std::string_view kGlobedPrefix = "dankmeme.globed2/";

    std::vector<CCNode*> stack;
    stack.push_back(pl);
    while (!stack.empty()) {
        CCNode* cur = stack.back();
        stack.pop_back();
        auto* children = cur->getChildren();
        if (!children) continue;
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!child) continue;
            std::string id = child->getID();
            if (!id.empty()
                && (kModNodeIds.contains(std::string_view{id})
                    || std::string_view{id}.starts_with(kGlobedPrefix))) {
                if (child->isVisible() && !userShown.count(child)) {
                    hidden.push_back({child, true});
                    child->setVisible(false);
                }
                continue; // matched: nothing relevant inside it
            }
            if (typeinfo_cast<CCSpriteBatchNode*>(child)) continue;
            if (typeinfo_cast<CCParticleSystem*>(child))  continue;
            if (typeinfo_cast<GameObject*>(child))        continue;
            stack.push_back(child);
        }
    }
}

// (4) Classifying scene-graph children reads their vtable (typeid /
// typeinfo_cast) and members. A node another mod released but left in a
// children array would fault when touched; sehClassify runs the classifier
// under SEH and skips that child on an access violation instead of crashing.
// The classifier functions must not contain __try themselves (they use C++
// objects with destructors), hence this function-pointer split.
#ifdef GEODE_IS_WINDOWS
bool sehClassify(bool (*fn)(void*), void* ctx, bool* faulted) noexcept {
    __try {
        return fn(ctx);
    } __except (GetExceptionCode() == 0xC0000005L /* EXCEPTION_ACCESS_VIOLATION */
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        if (faulted) *faulted = true;
        return false;
    }
}
#else
bool sehClassify(bool (*fn)(void*), void* ctx, bool*) { return fn(ctx); }
#endif

struct PLChildClassifyCtx {
    CCNode* child;
    std::unordered_set<CCNode*>* keep;
};

// Whether a direct PlayLayer child is non-vanilla UI to hide. Mirrors the
// original inline decision exactly.
bool classifyPlayLayerChild(void* p) {
    auto* c = static_cast<PLChildClassifyCtx*>(p);
    CCNode* child = c->child;
    if (!child->isVisible())                      return false;
    if (c->keep->count(child))                    return false;
    if (paimon::capture::isUserShown(child))      return false;
    if (typeinfo_cast<PlayerObject*>(child))      return false;
    if (typeinfo_cast<CCSpriteBatchNode*>(child)) return false;
    if (typeinfo_cast<CCParticleSystem*>(child))  return false;

    static const std::vector<std::string> kUIPatterns = {
        "ui", "uilayer", "pause", "menu", "dialog", "popup", "editor",
        "notification", "btn", "button", "overlay", "checkpoint", "fps",
        "debug", "attempt", "percent", "progress", "bar", "score",
        "practice", "hitbox", "trajectory", "status", "info", "label",
        "hud", "indicator", "counter", "timer", "stat", "cheat",
        "noclip", "speedhack", "startpos", "testmode", "macro"
    };

    std::string nid  = child->getID();
    std::string nidL = geode::utils::string::toLower(nid);
    std::string clsL = geode::utils::string::toLower(typeid(*child).name());

    bool shouldHide = false;
    if (nid.find('/') != std::string::npos) shouldHide = true;
    if (!shouldHide && !nidL.empty()) {
        for (auto const& pat : kUIPatterns) {
            if (nidL.find(pat) != std::string::npos) { shouldHide = true; break; }
        }
    }
    if (!shouldHide && (
            typeinfo_cast<CCMenu*>(child)         ||
            typeinfo_cast<CCLabelBMFont*>(child)  ||
            typeinfo_cast<CCLabelTTF*>(child))) {
        shouldHide = true;
    }
    if (!shouldHide) {
        if (clsL.find("uilayer")    != std::string::npos ||
            clsL.find("progress")   != std::string::npos ||
            clsL.find("status")     != std::string::npos ||
            clsL.find("overlay")    != std::string::npos ||
            clsL.find("hitbox")     != std::string::npos ||
            clsL.find("trajectory") != std::string::npos ||
            clsL.find("noclip")     != std::string::npos ||
            clsL.find("debugdraw")  != std::string::npos) {
            shouldHide = true;
        }
    }
    if (!shouldHide && child->getZOrder() >= 10
        && !typeinfo_cast<GJBaseGameLayer*>(child)) {
        shouldHide = true;
    }
    return shouldHide;
}

struct SceneChildClassifyCtx { CCNode* child; };

// Whether a scene child (sibling of PlayLayer) is a popup/pause/overlay to hide.
bool classifySceneChild(void* p) {
    CCNode* child = static_cast<SceneChildClassifyCtx*>(p)->child;
    if (!child->isVisible()) return false;

    std::string nid = child->getID();
    char const* cls = typeid(*child).name();

    if (typeinfo_cast<PauseLayer*>(child))               return true;
    if (typeinfo_cast<FLAlertLayer*>(child))             return true;
    if (strstr(cls, "Popup"))                            return true; // Geode popups (template)
    if (nid.find("pause") != std::string::npos ||
        nid.find("Pause") != std::string::npos)          return true;
    if (strstr(cls, "PauseLayer"))                       return true;
    if (nid.find("paimon-loading") != std::string::npos) return true;
    if (nid.find('/') != std::string::npos)              return true;
    if (child->getZOrder() > 100)                        return true;
    return false;
}

HiddenNodeList hideNonVanillaUI() {
    HiddenNodeList hidden;

    auto* director = CCDirector::get();
    if (!director) return hidden;
    auto* scene = director->getRunningScene();

    auto hideNode = [&](CCNode* n) {
        // Skip nodes the user explicitly chose to keep in the shot via the
        // layer editor; force-hiding them would silently undo that choice.
        if (n && n->isVisible() && !paimon::capture::isUserShown(n)) {
            hidden.push_back({n, true});
            n->setVisible(false);
        }
    };

    if (auto* pl = PlayLayer::get()) {
        std::unordered_set<CCNode*> keep{
            pl->m_player1, pl->m_player2, pl->m_objectLayer,
            pl->m_shaderLayer, pl->m_background, pl->m_middleground,
            pl->m_groundLayer, pl->m_groundLayer2,
            pl->m_inShaderObjectLayer, pl->m_aboveShaderObjectLayer,
            pl->m_objectParent, pl->m_inShaderParent, pl->m_aboveShaderParent,
        };
        if (pl->m_batchNodes) {
            for (auto* bn : CCArrayExt<CCNode*>(pl->m_batchNodes)) {
                if (bn) keep.insert(bn);
            }
        }
        keep.erase(nullptr);

        // Protect ancestor chain up to PlayLayer (shader wrapper nodes).
        {
            std::vector<CCNode*> keepRoots(keep.begin(), keep.end());
            for (auto* n : keepRoots) {
                for (CCNode* a = n->getParent(); a && a != pl; a = a->getParent()) {
                    keep.insert(a);
                }
            }
        }

        // Some PlayLayer UI member pointers can be stale or uninitialized
        // depending on the level type / setup state (e.g. holding 0xFFFF...FF),
        // and dereferencing them crashes. Collect the live descendants of the
        // PlayLayer so we can validate a member is a real node before touching
        // it. Comparing pointer values against this set never dereferences.
        std::unordered_set<CCNode*> liveNodes;
        {
            std::vector<CCNode*> stack{pl};
            while (!stack.empty()) {
                CCNode* cur = stack.back();
                stack.pop_back();
                auto* kids = cur->getChildren();
                if (!kids) continue;
                for (auto* k : CCArrayExt<CCNode*>(kids)) {
                    if (k && liveNodes.insert(k).second) stack.push_back(k);
                }
            }
        }
        auto hideMember = [&](CCNode* n) {
            if (n && liveNodes.count(n)) hideNode(n);
        };

        hideMember(pl->m_uiLayer);
        hideMember(pl->m_attemptLabel);
        hideMember(pl->m_percentageLabel);
        hideMember(pl->m_progressBar);
        hideMember(pl->m_debugDrawNode);
        if (pl->m_debugDrawNode && liveNodes.count(pl->m_debugDrawNode)) {
            // The debug draw node may live inside a kept gameplay container
            // (e.g. the object layer); hiding that parent would blank the
            // whole level in the capture.
            auto* ddParent = pl->m_debugDrawNode->getParent();
            if (ddParent && !keep.count(ddParent)) hideNode(ddParent);
        }
        hideMember(pl->m_infoLabel);

        bool plChildFaulted = false;
        for (auto* child : CCArrayExt<CCNode*>(pl->getChildren())) {
            if (!child) continue;
            if (keep.count(child)) continue;
            if (paimon::capture::isUserShown(child)) continue;
            // (4) Vtable access (typeid/typeinfo_cast) guarded against a
            // dangling child left in the array by another mod.
            PLChildClassifyCtx ctx{child, &keep};
            if (sehClassify(&classifyPlayLayerChild, &ctx, &plChildFaulted)) {
                hidden.push_back({child, true});
                child->setVisible(false);
            }
        }
        if (plChildFaulted) {
            log::warn("[FramebufferCapture] Skipped one or more dangling PlayLayer "
                      "children while hiding UI");
        }

            hideGameplayEffectNodes(pl, hidden);
        hideKnownModNodes(pl, hidden);
    }

    hideNode(director->getNotificationNode());

    if (scene) {
        auto* pl = PlayLayer::get();
        bool sceneFaulted = false;
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!child) continue;
            if (pl && child == pl) continue;
            if (paimon::capture::isUserShown(child)) continue;
            // (4) Vtable access guarded against a dangling scene sibling.
            SceneChildClassifyCtx ctx{child};
            if (sehClassify(&classifySceneChild, &ctx, &sceneFaulted)) {
                hidden.push_back({child, true});
                child->setVisible(false);
            }
        }
        if (sceneFaulted) {
            log::warn("[FramebufferCapture] Skipped one or more dangling scene "
                      "nodes while hiding UI");
        }
    }

    return hidden;
}

std::vector<HiddenGameObjectState> hidePracticeAndEffectObjects() {
    std::vector<HiddenGameObjectState> hidden;
    auto* pl = PlayLayer::get();
    if (!pl) return hidden;
    if (!pl->m_isPracticeMode) return hidden;

    std::unordered_set<GameObject*> seen;
    auto hideObj = [&](GameObject* obj) {
        if (!obj || seen.count(obj)) return;
        if (obj->m_isInvisible && !obj->isVisible() && obj->getOpacity() == 0) return;
        seen.insert(obj);

        HiddenGameObjectState st;
        st.obj       = obj;
        st.disabled2 = obj->m_isDisabled2;
        st.invisible = obj->m_isInvisible;
        st.visible   = obj->isVisible();
        st.hide      = obj->m_isHide;
        st.opacity   = obj->getOpacity();
        hidden.push_back(st);

        obj->m_isDisabled2 = true;
        obj->m_isInvisible = true;
        obj->setOpacity(0);
        obj->setVisible(false);
        obj->m_isHide = true;
    };

    if (pl->m_currentCheckpoint) {
        hideObj(pl->m_currentCheckpoint->m_physicalCheckpointObject);
    }
    if (pl->m_checkpointArray) {
        for (auto* cp : CCArrayExt<CheckpointObject*>(pl->m_checkpointArray)) {
            if (cp) hideObj(cp->m_physicalCheckpointObject);
        }
    }
    return hidden;
}

void restoreHiddenState() {
    if (!g_prep.active) return;

    // lock() skips nodes destroyed mid-capture (circle waves, notifications, etc).
    for (auto& [weakNode, vis] : g_prep.hiddenNodes) {
        if (auto node = weakNode.lock()) node->setVisible(vis);
    }

    for (auto& h : g_prep.hiddenGameObjects) {
        auto obj = h.obj.lock();
        if (!obj) continue;
        obj->m_isDisabled2 = h.disabled2;
        obj->m_isInvisible = h.invisible;
        obj->setVisible(h.visible);
        obj->m_isHide      = h.hide;
        obj->setOpacity(h.opacity);
    }

    if (g_prep.playerVisActive) {
        if (auto* pl = PlayLayer::get()) {
            if (g_prep.playerHide1 && pl->m_player1) {
                paimTogglePlayer(pl->m_player1, g_prep.player1State, false);
            }
            if (g_prep.playerHide2 && pl->m_player2) {
                paimTogglePlayer(pl->m_player2, g_prep.player2State, false);
            }
        }
    }

    if (g_prep.plSnapActive) {
        if (auto pl = g_prep.plRef.lock(); pl && pl->getParent()) {
            pl->setPosition(g_prep.plPos);
            pl->setScaleX(g_prep.plScaleX);
            pl->setScaleY(g_prep.plScaleY);
            pl->setAnchorPoint(g_prep.plAnchor);
        }
    }

    // Full reset.
    g_prep = {};
    // s_isCapturing/s_captureW/s_captureH reset by FramebufferCapture private statics.
}

// Pixel helpers (content check, flip, alpha fill, Lanczos-3 resize)
bool pixelBufferHasContent(uint8_t const* pixels, size_t bytes) {
    if (bytes < 4) return false;
    uint8_t r0 = pixels[0], g0 = pixels[1], b0 = pixels[2];
    size_t step = 4 * 97;
    for (size_t i = step; i + 2 < bytes; i += step) {
        if (pixels[i] != r0 || pixels[i + 1] != g0 || pixels[i + 2] != b0) return true;
    }
    return false;
}

void flipVerticalInPlace(uint8_t* data, int width, int height, int channels) {
    int rowSize = width * channels;
    std::vector<uint8_t> tmp(rowSize);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top    = data + static_cast<size_t>(y) * rowSize;
        uint8_t* bottom = data + static_cast<size_t>(height - 1 - y) * rowSize;
        std::memcpy(tmp.data(), top, rowSize);
        std::memcpy(top, bottom, rowSize);
        std::memcpy(bottom, tmp.data(), rowSize);
    }
}

void forceAlphaOpaque(uint8_t* data, size_t bytes) {
    for (size_t i = 3; i < bytes; i += 4) data[i] = 255;
}

// Separable Lanczos-3 resize (H then V) with precomputed normalized weights.
struct ResizeAxis {
    std::vector<int>   starts;  // first source index per output index
    std::vector<float> weights; // taps per output index, contiguous
    int taps = 0;
};

ResizeAxis buildLanczosAxis(int srcN, int dstN) {
    constexpr float A = 3.0f; // Lanczos lobes
    ResizeAxis ax;
    float scale       = static_cast<float>(dstN) / srcN;       // <1 = downscale
    float filterScale = std::max(1.0f, 1.0f / scale);          // widen kernel on minification
    float support     = A * filterScale;
    ax.taps = static_cast<int>(std::ceil(support * 2.0f)) + 1;
    ax.starts.resize(dstN);
    ax.weights.assign(static_cast<size_t>(dstN) * ax.taps, 0.0f);

    auto sinc = [](float x) {
        if (x == 0.0f) return 1.0f;
        float px = 3.14159265358979f * x;
        return std::sin(px) / px;
    };
    auto lanczos = [&](float x) {
        x = std::abs(x);
        return x < A ? sinc(x) * sinc(x / A) : 0.0f;
    };

    for (int i = 0; i < dstN; ++i) {
        float center = (i + 0.5f) / scale - 0.5f;
        int start = static_cast<int>(std::floor(center - support)) + 1;
        ax.starts[i] = start;
        float sum = 0.0f;
        float* w = &ax.weights[static_cast<size_t>(i) * ax.taps];
        for (int t = 0; t < ax.taps; ++t) {
            w[t] = lanczos((start + t - center) / filterScale);
            sum += w[t];
        }
        if (sum != 0.0f) {
            for (int t = 0; t < ax.taps; ++t) w[t] /= sum;
        }
    }
    return ax;
}

std::shared_ptr<uint8_t> lanczosResizeRGBA(
    uint8_t const* src, int srcW, int srcH, int dstW, int dstH)
{
    ResizeAxis axX = buildLanczosAxis(srcW, dstW);
    ResizeAxis axY = buildLanczosAxis(srcH, dstH);

    // Pass 1: horizontal → float RGB intermediate (dstW × srcH).
    std::vector<float> mid(static_cast<size_t>(dstW) * srcH * 3);
    for (int y = 0; y < srcH; ++y) {
        uint8_t const* row = src + static_cast<size_t>(y) * srcW * 4;
        float* out = mid.data() + static_cast<size_t>(y) * dstW * 3;
        for (int x = 0; x < dstW; ++x) {
            float const* w = &axX.weights[static_cast<size_t>(x) * axX.taps];
            int start = axX.starts[x];
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int t = 0; t < axX.taps; ++t) {
                int sx = std::clamp(start + t, 0, srcW - 1);
                float wt = w[t];
                uint8_t const* p = row + static_cast<size_t>(sx) * 4;
                r += p[0] * wt;
                g += p[1] * wt;
                b += p[2] * wt;
            }
            float* o = out + static_cast<size_t>(x) * 3;
            o[0] = r; o[1] = g; o[2] = b;
        }
    }

    // Pass 2: vertical → final RGBA (alpha forced opaque).
    size_t outBytes = static_cast<size_t>(dstW) * dstH * 4;
    std::shared_ptr<uint8_t> out(new uint8_t[outBytes], std::default_delete<uint8_t[]>());
    uint8_t* dst = out.get();
    for (int y = 0; y < dstH; ++y) {
        float const* w = &axY.weights[static_cast<size_t>(y) * axY.taps];
        int start = axY.starts[y];
        uint8_t* dRow = dst + static_cast<size_t>(y) * dstW * 4;
        for (int x = 0; x < dstW; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int t = 0; t < axY.taps; ++t) {
                int sy = std::clamp(start + t, 0, srcH - 1);
                float wt = w[t];
                float const* p = mid.data() + (static_cast<size_t>(sy) * dstW + x) * 3;
                r += p[0] * wt;
                g += p[1] * wt;
                b += p[2] * wt;
            }
            uint8_t* d = dRow + static_cast<size_t>(x) * 4;
            d[0] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f) + 0.5f);
            d[1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f) + 0.5f);
            d[2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f) + 0.5f);
            d[3] = 255;
        }
    }
    return out;
}

// Returns texture with refcount=1; receivers must retain (geode::Ref<>).
CCTexture2D* makeTextureRGBA(uint8_t const* data, int W, int H) {
    auto* tex = new CCTexture2D();
    if (!tex->initWithData(data, kCCTexture2DPixelFormat_RGBA8888, W, H,
                           CCSize(static_cast<float>(W), static_cast<float>(H)))) {
        log::error("[FramebufferCapture] CCTexture2D init failed ({}x{})", W, H);
        tex->release();
        return nullptr;
    }
    tex->setAntiAliasTexParameters();
    return tex;
}

// CPU FXAA-style anti-aliasing for HDR mode (~100-200ms at 1080p).
void applyFXAA(uint8_t* data, int W, int H) {
    if (!data || W < 3 || H < 3) return;

    size_t bytes = static_cast<size_t>(W) * H * 4;
    std::vector<uint8_t> in(data, data + bytes);

    auto luma = [](uint8_t r, uint8_t g, uint8_t b) -> float {
        // Rec. 709 luma. Returned in [0,255].
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    };

    auto sampleLuma = [&](int x, int y) -> float {
        if (x < 0) x = 0; else if (x >= W) x = W - 1;
        if (y < 0) y = 0; else if (y >= H) y = H - 1;
        size_t i = (static_cast<size_t>(y) * W + x) * 4;
        return luma(in[i], in[i + 1], in[i + 2]);
    };

    auto samplePixelBilinear = [&](float fx, float fy, uint8_t out[3]) {
        if (fx < 0) fx = 0; else if (fx > W - 1) fx = static_cast<float>(W - 1);
        if (fy < 0) fy = 0; else if (fy > H - 1) fy = static_cast<float>(H - 1);
        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        int x1 = std::min(x0 + 1, W - 1);
        int y1 = std::min(y0 + 1, H - 1);
        float dx = fx - x0;
        float dy = fy - y0;

        size_t i00 = (static_cast<size_t>(y0) * W + x0) * 4;
        size_t i10 = (static_cast<size_t>(y0) * W + x1) * 4;
        size_t i01 = (static_cast<size_t>(y1) * W + x0) * 4;
        size_t i11 = (static_cast<size_t>(y1) * W + x1) * 4;

        for (int c = 0; c < 3; ++c) {
            float v = (1.0f - dx) * (1.0f - dy) * in[i00 + c]
                    +         dx  * (1.0f - dy) * in[i10 + c]
                    + (1.0f - dx) *         dy  * in[i01 + c]
                    +         dx  *         dy  * in[i11 + c];
            out[c] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
        }
    };

    // FXAA defaults scaled to 0..255 range.
    constexpr float EDGE_MIN = 0.0312f * 255.0f;
    constexpr float EDGE_REL = 0.063f;  // moderately aggressive

    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            float lM = sampleLuma(x, y);
            float lN = sampleLuma(x, y - 1);
            float lS = sampleLuma(x, y + 1);
            float lW = sampleLuma(x - 1, y);
            float lE = sampleLuma(x + 1, y);

            float lMin = std::min({lM, lN, lS, lW, lE});
            float lMax = std::max({lM, lN, lS, lW, lE});
            float range = lMax - lMin;

            if (range < std::max(EDGE_MIN, EDGE_REL * lMax)) continue;

            // Compare 2nd-order luma differences to decide edge direction.
            float lNW = sampleLuma(x - 1, y - 1);
            float lNE = sampleLuma(x + 1, y - 1);
            float lSW = sampleLuma(x - 1, y + 1);
            float lSE = sampleLuma(x + 1, y + 1);

            float horz = std::abs(lW + lE - 2 * lM)
                       + std::abs(lNW + lNE - 2 * lN)
                       + std::abs(lSW + lSE - 2 * lS);
            float vert = std::abs(lN + lS - 2 * lM)
                       + std::abs(lNW + lSW - 2 * lW)
                       + std::abs(lNE + lSE - 2 * lE);
            bool isHorz = horz >= vert;

            // Sample two bilinear taps offset along the edge axis.
            float dx = isHorz ? 0.0f : 0.5f;
            float dy = isHorz ? 0.5f : 0.0f;

            uint8_t pa[3], pb[3];
            samplePixelBilinear(static_cast<float>(x) + dx, static_cast<float>(y) + dy, pa);
            samplePixelBilinear(static_cast<float>(x) - dx, static_cast<float>(y) - dy, pb);

            size_t di = (static_cast<size_t>(y) * W + x) * 4;
            data[di + 0] = static_cast<uint8_t>((static_cast<int>(pa[0]) + pb[0]) / 2);
            data[di + 1] = static_cast<uint8_t>((static_cast<int>(pa[1]) + pb[1]) / 2);
            data[di + 2] = static_cast<uint8_t>((static_cast<int>(pa[2]) + pb[2]) / 2);
            // alpha unchanged (already opaque from forceAlphaOpaque)
        }
    }
}

// Resolution helpers
int resolveTargetWidth(int levelID, int nativeW) {
    // Menu / generic screenshot → use native resolution (no downscale).
    if (levelID == 0) return nativeW;

    std::string res = Mod::get()->getSettingValue<std::string>("capture-resolution");
    int target = 1920;
    if      (res == "4k")    target = 3840;
    else if (res == "1440p") target = 2560;
    else                     target = 1920;
    // Never upscale; only affects back-buffer fallback.
    return std::min(target, nativeW);
}

// Exact 16:9 at user resolution, clamped to GPU limit. Not limited by window size.
std::pair<int, int> resolveRenderTargetSize() {
    std::string res = Mod::get()->getSettingValue<std::string>("capture-resolution");
    int w = 1920;
    if      (res == "4k")    w = 3840;
    else if (res == "1440p") w = 2560;
    w = std::min(w, FramebufferCapture::getMaxTextureSize());
    int h = (w * 9 / 16) & ~1;
    return {w, h};
}

// Render PlayLayer to offscreen FBO: retargets cocos view + ShaderLayer,
// drains GL errors, returns bottom-up RGBA pixels or nullptr (→ back-buffer fallback).
std::shared_ptr<std::vector<uint8_t>> renderPlayLayerToTexture(
    PlayLayer* pl, int W, int H)
{
    auto* director = CCDirector::get();
    auto* glView   = director ? director->getOpenGLView() : nullptr;
    if (!pl || !glView || W <= 0 || H <= 0) return nullptr;

    // Suppress flipArt/updateCameraBGArt for the whole render — updateCamera(0.f)
    // below can reach them with the view retargeted, which crashes (see guard above).
    SuppressCameraArtGuard suppressCameraArt;

    while (glGetError() != GL_NO_ERROR) {} // drain stale errors

    GLint oldFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);

    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    if (!tex) return nullptr;
    // Keep cocos' texture-binding cache coherent.
    ccGLBindTexture2D(tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE
        || glGetError() != GL_NO_ERROR) {
        log::warn("[FramebufferCapture] Offscreen FBO incomplete at {}x{}", W, H);
        glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
        if (fbo) glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        return nullptr;
    }

    // GL objects are torn down and the previous FBO rebound on EVERY exit path
    // (including the mid-render aborts below), so a fault never leaks the FBO
    // nor leaves it bound for the next real frame.
    struct GLCleanupGuard {
        GLuint tex; GLuint fbo; GLint oldFBO;
        ~GLCleanupGuard() {
            glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
            if (fbo) glDeleteFramebuffers(1, &fbo);
            if (tex) glDeleteTextures(1, &tex);
        }
    } glCleanup{tex, fbo, oldFBO};

    // (1c) Validate the retarget math before dividing: a zero/negative display
    // factor or window size would produce inf/NaN in the viewport/projection.
    // Bail to the back-buffer fallback instead of poisoning GL state.
    CCSize oldWinSize = director->getWinSize();
    float displayFactor = geode::utils::getDisplayFactor();
    if (displayFactor <= 0.f || oldWinSize.width <= 0.f || oldWinSize.height <= 0.f) {
        log::warn("[FramebufferCapture] Bad view metrics (displayFactor={}, winSize={}x{}); "
                  "skipping offscreen render -> back-buffer fallback",
                  displayFactor, oldWinSize.width, oldWinSize.height);
        return nullptr; // glCleanup restores GL
    }

    CCSize oldDesign  = glView->getDesignResolutionSize();
    CCSize oldScreen  = glView->m_obScreenSize;
    float  oldScaleX  = glView->m_fScaleX;
    float  oldScaleY  = glView->m_fScaleY;

    // (1b) RAII net: the cocos view is always restored, even on a mid-render
    // abort, so the next real frame is never left at the capture resolution.
    struct ViewRestoreGuard {
        CCDirector* director; CCEGLView* glView;
        CCSize oldDesign, oldScreen; float oldScaleX, oldScaleY;
        bool armed = false;
        ~ViewRestoreGuard() {
            if (!armed) return;
            glView->m_fScaleX = oldScaleX;
            glView->m_fScaleY = oldScaleY;
            director->m_obWinSizeInPoints = oldDesign;
            glView->m_obScreenSize = oldScreen;
            glView->setDesignResolutionSize(oldDesign.width, oldDesign.height, kResolutionExactFit);
            director->setViewport();
        }
    } viewGuard{director, glView, oldDesign, oldScreen, oldScaleX, oldScaleY};

    glView->m_fScaleX = static_cast<float>(W) / oldWinSize.width  / displayFactor;
    glView->m_fScaleY = static_cast<float>(H) / oldWinSize.height / displayFactor;

    float aspect = static_cast<float>(W) / H;
    CCSize newRes{std::round(320.f * aspect), 320.f};
    director->m_obWinSizeInPoints = newRes;
    glView->m_obScreenSize = CCSize{static_cast<float>(W), static_cast<float>(H)};
    glView->setDesignResolutionSize(newRes.width, newRes.height, kResolutionExactFit);
    viewGuard.armed = true;

    glViewport(0, 0, W, H);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto sizesMatch = [](CCSize const& a, CCSize const& b) {
        constexpr float EPS = 0.1f;
        return std::abs(a.width - b.width) < EPS
            && std::abs(a.height - b.height) < EPS;
    };

    CCSize newWinSize  = director->getWinSize();
    bool aspectMatches = sizesMatch(oldWinSize, newWinSize);

    // (2) Third-party camera hooks (e.g. Globed) run inside the out-of-band
    // updateCamera/preUpdateVisibility recalcs below and have repeatedly
    // null-derefed with the view retargeted. When such a mod is present, skip
    // those best-effort recalcs entirely (they only tweak non-16:9 framing);
    // the real game loop recomputes them next frame, and the RAII guards keep
    // the view/GL/camera state clean regardless.
    static bool s_cameraModWarned = false;
    bool thirdPartyCameraMod = Loader::get()->isModLoaded("dankmeme.globed2");
    if (thirdPartyCameraMod && !s_cameraModWarned) {
        s_cameraModWarned = true;
        log::warn("[FramebufferCapture] Third-party camera mod detected; skipping "
                  "out-of-band camera recalc during offscreen render for safety");
    }
    bool doCameraRecalc = !aspectMatches && !thirdPartyCameraMod;

    // When the window aspect differs from 16:9 the camera and UI-trigger
    // layers are positioned for the old winSize; recalculate for the render.
    bool hadUIPos = false;
    CCPoint oldUIPos{};
    if (doCameraRecalc) {
        pl->m_calculateTargetHeightOffset = true;
        pl->m_updateGroundShadows = true;
        if (!sehGuardedCall(+[](PlayLayer* p) { p->updateCamera(0.f); }, pl)) {
            log::warn("[FramebufferCapture] updateCamera faulted during offscreen "
                      "render; continuing with previous camera");
        }
        if (auto* uiTrigger = pl->m_uiTriggerUI;
            uiTrigger && uiTrigger->getChildrenCount() > 0) {
            oldUIPos = uiTrigger->getPosition();
            hadUIPos = true;
            uiTrigger->setPosition(oldUIPos + ccp(
                newWinSize.width - oldWinSize.width,
                newWinSize.height - oldWinSize.height));
        }
    }

    // Shader retarget: rebuild internal render texture at capture size.
    auto* shader = pl->m_shaderLayer;
    CCSize const captureSize{static_cast<float>(W), static_cast<float>(H)};
    bool hadShader = shader && shader->getParent()
                  && !sizesMatch(shader->m_targetTextureSize, captureSize);
    CCSize oldShaderScreen = hadShader ? shader->m_screenSize : CCSize{};
    CCSize oldShaderTarget = hadShader ? shader->m_targetTextureSize : CCSize{};
    bool pixelateHardEdges = hadShader ? shader->m_state.m_pixelateHardEdges : false;
    auto applyLinearFilter = [&]() {
        if (shader && shader->m_sprite && shader->m_sprite->getTexture()) {
            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
            shader->m_sprite->getTexture()->setTexParameters(&params);
        }
    };

    // (1a/1b) Shader state is always put back, even on an abort between retarget
    // and restore. setupShader/prePixelateShader are SEH-guarded: doubling an
    // already-large shader FBO is exactly the allocation that faults mid-capture.
    struct ShaderRestoreGuard {
        PlayLayer* pl; ShaderLayer* shader;
        CCSize oldScreen, oldTarget; bool pixelateHardEdges;
        bool armed = false;
        ~ShaderRestoreGuard() {
            if (!armed || !shader) return;
            shader->m_screenSize        = oldScreen;
            shader->m_targetTextureSize = oldTarget;
            sehGuardedCall(+[](PlayLayer* p) { if (p->m_shaderLayer) p->m_shaderLayer->setupShader(false); }, pl);
            if (!pixelateHardEdges && shader->m_sprite && shader->m_sprite->getTexture()) {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                shader->m_sprite->getTexture()->setTexParameters(&params);
            }
            sehGuardedCall(+[](PlayLayer* p) { if (p->m_shaderLayer) p->m_shaderLayer->prePixelateShader(); }, pl);
        }
    } shaderGuard{pl, hadShader ? shader : nullptr,
                  oldShaderScreen, oldShaderTarget, pixelateHardEdges, hadShader};

    if (hadShader) {
        shader->m_screenSize        = newWinSize;
        shader->m_targetTextureSize = captureSize;
        sehGuardedCall(+[](PlayLayer* p) { if (p->m_shaderLayer) p->m_shaderLayer->setupShader(false); }, pl);
        if (!pixelateHardEdges) applyLinearFilter();
        sehGuardedCall(+[](PlayLayer* p) { if (p->m_shaderLayer) p->m_shaderLayer->prePixelateShader(); }, pl);
        sehGuardedCall(+[](PlayLayer* p) { p->updateShaderLayer(0.f); }, pl);
        // setupShader recreates the shader's FBO; rebind ours for the visit.
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // (1d) If setupShader left our FBO incomplete, abort cleanly instead of
        // drawing into an invalid target. The guards restore shader/view/GL.
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            log::warn("[FramebufferCapture] FBO incomplete after shader retarget "
                      "-> back-buffer fallback");
            return nullptr;
        }
    }

    if (doCameraRecalc
        && !sehGuardedCall(+[](PlayLayer* p) { p->preUpdateVisibility(0.f); }, pl)) {
        log::warn("[FramebufferCapture] preUpdateVisibility faulted during "
                  "offscreen render; skipped");
    }

    // (1a) visit() drives the whole scene draw with the retargeted view; SEH
    // guard it so a fault inside a hooked node's draw falls back to the
    // back-buffer instead of crashing.
    if (!sehGuardedCall(+[](PlayLayer* p) { p->visit(); }, pl)) {
        log::warn("[FramebufferCapture] pl->visit() faulted during offscreen "
                  "render -> back-buffer fallback");
        return nullptr; // guards restore shader/view/GL
    }

    // Drain benign GL errors from retargeted shader render.
    while (glGetError() != GL_NO_ERROR) {}

    auto raw = std::make_shared<std::vector<uint8_t>>(
        static_cast<size_t>(W) * H * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, raw->data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    bool readOk = (glGetError() == GL_NO_ERROR)
               && pixelBufferHasContent(raw->data(), raw->size());

    // Camera flags marked dirty for next frame; shader/view/GL are restored by
    // the RAII guards above on scope exit.
    if (!aspectMatches) {
        pl->m_updateGroundShadows = true;
        pl->m_calculateTargetHeightOffset = true;
        if (hadUIPos && pl->m_uiTriggerUI) {
            pl->m_uiTriggerUI->setPosition(oldUIPos);
        }
    }

    if (!readOk) {
        log::warn("[FramebufferCapture] Offscreen render returned empty content");
        return nullptr;
    }
    return raw;
}

#ifdef GEODE_IS_WINDOWS
// Async PBO readback: avoids pipeline sync + 8-33 MB transfer stall.
GLuint g_pbo  = 0;
int    g_pboW = 0;
int    g_pboH = 0;

void deletePboIfAny() {
    if (g_pbo) {
        glDeleteBuffers(1, &g_pbo);
        g_pbo = 0;
    }
    g_pboW = 0;
    g_pboH = 0;
}

// Issue async glReadPixels into PBO; returns false so caller can fall back.
bool issuePboRead(int W, int H) {
    // Never leak or overwrite a live PBO handle (double-free / stale map guard).
    deletePboIfAny();
    while (glGetError() != GL_NO_ERROR) {} // drain stale errors

    GLuint pbo = 0;
    glGenBuffers(1, &pbo);
    if (!pbo) return false;

    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER,
                 static_cast<GLsizeiptr>(static_cast<size_t>(W) * H * 4),
                 nullptr, GL_STREAM_READ);

    GLint origFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &origFBO);
    if (origFBO != 0) glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); // → PBO, async
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    if (origFBO != 0) glBindFramebuffer(GL_FRAMEBUFFER, origFBO);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteBuffers(1, &pbo);
        return false;
    }
    g_pbo  = pbo;
    g_pboW = W;
    g_pboH = H;
    return true;
}
#endif // GEODE_IS_WINDOWS

} // namespace

// Public API: trivial accessors / state
bool FramebufferCapture::isCapturing()    { return s_isCapturing; }
void FramebufferCapture::setHDRMode(bool enabled) { s_hdrMode = enabled; }
bool FramebufferCapture::isHDRMode()      { return s_hdrMode; }

std::pair<int, int> FramebufferCapture::getCaptureSize() {
    return {s_captureW, s_captureH};
}

int FramebufferCapture::getMaxTextureSize() {
    if (s_maxTextureSize <= 0) {
        GLint maxTex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        s_maxTextureSize = (maxTex > 0) ? static_cast<int>(maxTex) : 4096;
        log::info("[FramebufferCapture] GL_MAX_TEXTURE_SIZE = {}", s_maxTextureSize);
    }
    return s_maxTextureSize;
}

CaptureValidation FramebufferCapture::validateCaptureConditions() {
    CaptureValidation result;
    if (CCDirector::get()->getContentScaleFactor() < 4.0f) {
        result.canCapture = false;
        result.reason = "Thumbnails require High Graphics quality. Enable it in GD settings.";
        return result;
    }
    if (GameManager::sharedState()->m_performanceMode) {
        result.canCapture = false;
        result.reason = "Thumbnails cannot be taken with Low Detail Mode enabled. Disable it in GD settings.";
        return result;
    }
    if (auto* pl = PlayLayer::get()) {
        bool dead = pl->m_playerDied || (pl->m_player1 && pl->m_player1->m_isDead);
        if (!dead) {
            dead = paimon::capture::hasRecentDeath(pl->m_gameState.m_currentProgress);
        }
        if (dead) {
            result.canCapture = false;
            result.reason = "Cannot capture while the player is dead. Wait until respawn.";
            return result;
        }
    }
    return result;
}

bool FramebufferCapture::hasPendingCapture() {
    return g_phase.load() != Phase::Idle;
}

void FramebufferCapture::clearCaptureFlags() {
    s_isCapturing = false;
    s_captureW    = 0;
    s_captureH    = 0;
}

namespace {
    // Restore hidden state + clear the publish flags.
    inline void restoreCaptureState() {
        restoreHiddenState();
        FramebufferCapture::clearCaptureFlags();
    }
}


void FramebufferCapture::requestCapture(
    int levelID,
    geode::CopyableFunction<void(bool, CCTexture2D*, std::shared_ptr<uint8_t>, int, int)> callback,
    CCNode* nodeToCapture,
    bool hidePlayer1,
    bool hidePlayer2)
{
    log::info("[FramebufferCapture] requestCapture levelID={} hidePlayer1={} hidePlayer2={} hdr={}",
              levelID, hidePlayer1, hidePlayer2, s_hdrMode);

    // Drop any in-flight request, notify callback, and start fresh.
    Phase prev = g_phase.exchange(Phase::Idle);
    if (prev != Phase::Idle) {
        log::warn("[FramebufferCapture] Replacing in-flight capture (prev phase={})", static_cast<int>(prev));
        if (s_request.callback) {
            auto old = std::move(s_request.callback);
            old(false, nullptr, nullptr, 0, 0);
        }
        if (g_prep.active) restoreCaptureState();
#ifdef GEODE_IS_WINDOWS
        deletePboIfAny();
#endif
    }

    s_request.levelID       = levelID;
    s_request.callback      = std::move(callback);
    s_request.nodeToCapture = nodeToCapture;
    s_request.hidePlayer1   = hidePlayer1;
    s_request.hidePlayer2   = hidePlayer2;
    s_request.active        = true;

    g_generation.fetch_add(1, std::memory_order_relaxed);
    g_waitingTicks = 0;
    g_phase.store(Phase::ArmedHide);
}

void FramebufferCapture::cancelPending() {
    Phase prev = g_phase.exchange(Phase::Idle);
    g_generation.fetch_add(1, std::memory_order_relaxed);
    if (prev == Phase::Idle && !s_request.active) {
        // Nothing to cancel; still flush any deferred callbacks.
        for (auto& d : s_deferredCallbacks) if (d.texture) d.texture->release();
        s_deferredCallbacks.clear();
        return;
    }

    log::info("[FramebufferCapture] cancelPending (phase={})", static_cast<int>(prev));

    if (s_request.callback) {
        auto old = std::move(s_request.callback);
        old(false, nullptr, nullptr, 0, 0);
    }
    s_request.active        = false;
    s_request.callback      = nullptr;
    s_request.nodeToCapture = nullptr;
    s_request.hidePlayer1   = false;
    s_request.hidePlayer2   = false;

    if (g_prep.active) restoreCaptureState();
#ifdef GEODE_IS_WINDOWS
    deletePboIfAny();
#endif

    for (auto& d : s_deferredCallbacks) if (d.texture) d.texture->release();
    s_deferredCallbacks.clear();
}

// State machine driver, called from CCEGLView pre-swap.
void FramebufferCapture::executeIfPending() {
    Phase phase = g_phase.load();

    // (3) Runtime tearing down: never touch GL, hide state or map a PBO. Reset
    // everything to Idle so nothing is left half-armed across shutdown.
    if (paimon::isRuntimeShuttingDown()) {
        if (phase != Phase::Idle || s_request.active) {
            log::warn("[FramebufferCapture] Runtime shutting down; aborting pending capture (phase={})",
                      static_cast<int>(phase));
            if (s_request.callback) {
                auto cb = std::move(s_request.callback);
                cb(false, nullptr, nullptr, 0, 0);
            }
            s_request.active = false;
            if (g_prep.active) restoreCaptureState();
#ifdef GEODE_IS_WINDOWS
            deletePboIfAny();
#endif
            g_phase.store(Phase::Idle);
        }
        return;
    }

    // Phase: ArmedHide — hide UI; don't read yet (back-buffer still has UI).
    if (phase == Phase::ArmedHide) {
        // Special-case: legacy RTT node capture.
        if (s_request.nodeToCapture) {
            doCaptureNode(s_request.nodeToCapture);
            s_request.active = false;
            s_request.callback = nullptr;
            s_request.nodeToCapture = nullptr;
            g_phase.store(Phase::Idle);
            return;
        }

        g_prep = {};
        g_prep.active           = true;
        g_prep.hiddenNodes      = hideNonVanillaUI();
        g_prep.hiddenGameObjects = hidePracticeAndEffectObjects();

        auto* pl = PlayLayer::get();
        if (pl) {
            g_prep.plSnapActive = true;
            g_prep.plRef        = pl;
            g_prep.plPos        = pl->getPosition();
            g_prep.plScaleX     = pl->getScaleX();
            g_prep.plScaleY     = pl->getScaleY();
            g_prep.plAnchor     = pl->getAnchorPoint();

            if (s_request.hidePlayer1 && pl->m_player1) {
                paimTogglePlayer(pl->m_player1, g_prep.player1State, true);
                g_prep.playerHide1 = true;
            }
            if (s_request.hidePlayer2 && pl->m_player2) {
                paimTogglePlayer(pl->m_player2, g_prep.player2State, true);
                g_prep.playerHide2 = true;
            }
            g_prep.playerVisActive = (g_prep.playerHide1 || g_prep.playerHide2);
        }

        // Publish capture dimensions.
        auto* dir = CCDirector::get();
        auto* glView = dir ? dir->getOpenGLView() : nullptr;
        CCSize fs = glView ? glView->getFrameSize() : CCSize(0, 0);
        s_isCapturing = true;
        s_captureW    = std::max(1, static_cast<int>(fs.width));
        s_captureH    = std::max(1, static_cast<int>(fs.height));

        log::info("[FramebufferCapture] ArmedHide: hidden {} UI nodes, {} game objects, hdr={}",
                  g_prep.hiddenNodes.size(), g_prep.hiddenGameObjects.size(), s_hdrMode);

        // Level captures: offscreen render at requested resolution in one pre-swap.
        if (s_request.levelID != 0 && pl) {
            // Player may have died between request and pre-swap (keybind captures).
            bool dead = pl->m_playerDied || (pl->m_player1 && pl->m_player1->m_isDead);
            if (!dead) {
                dead = paimon::capture::hasRecentDeath(pl->m_gameState.m_currentProgress);
            }
            if (dead) {
                log::info("[FramebufferCapture] Player died mid-capture; aborting render");
                restoreCaptureState();
                if (s_request.callback) {
                    auto cb = std::move(s_request.callback);
                    cb(false, nullptr, nullptr, 0, 0);
                }
                s_request.active = false;
                g_phase.store(Phase::Idle);
                return;
            }

            auto [targetW, targetH] = resolveRenderTargetSize();

            // HDR: render supersampled and let the worker's Lanczos downscale
            // resolve it back to the target — true SSAA instead of the CPU
            // FXAA pass (which still covers the cases below). Skipped when a
            // level shader is active: the shader's internal render texture is
            // retargeted to the render size, and doubling an already-large
            // shader FBO is the kind of allocation that fails mid-capture.
            int renderW = targetW;
            int renderH = targetH;
            bool shaderActive = pl->m_shaderLayer && pl->m_shaderLayer->getParent();
            if (s_hdrMode && !shaderActive) {
                int ssW = std::min({targetW * 2, getMaxTextureSize(), 5120});
                ssW &= ~1;
                if (ssW >= targetW * 5 / 4) { // only if it buys real supersampling
                    renderW = ssW;
                    renderH = (ssW * 9 / 16) & ~1;
                }
            }

            s_captureW = targetW;
            s_captureH = targetH;
            if (auto raw = renderPlayLayerToTexture(pl, renderW, renderH)) {
                restoreCaptureState();
                dispatchProcessing(std::move(raw), renderW, renderH);
                return;
            }
            log::warn("[FramebufferCapture] Offscreen render failed; falling back to back-buffer capture");
            s_captureW = std::max(1, static_cast<int>(fs.width));
            s_captureH = std::max(1, static_cast<int>(fs.height));
        }

        g_phase.store(Phase::WaitingFrame);
        return;
    }

    // Phase: WaitingFrame
    // The frame just rendered (about to swap) was rendered with UI hidden.
    // Read it, restore UI immediately, kick the heavy work to a worker.
    if (phase == Phase::WaitingFrame) {
        // Other schedulers (e.g. PauseLayer's reShowOverlay) may have re-shown
        // UI since our hide. Re-run the hide pass; if anything was newly visible,
        // the read is dirty, so hide again and wait one more frame (capped).
        auto extraNodes = hideNonVanillaUI();
        auto extraGameObj = hidePracticeAndEffectObjects();

        bool foundDirty = !extraNodes.empty() || !extraGameObj.empty();
        if (foundDirty && g_waitingTicks < kMaxWaitingTicks) {
            for (auto& p : extraNodes) g_prep.hiddenNodes.push_back(p);
            for (auto& g : extraGameObj) g_prep.hiddenGameObjects.push_back(g);
            ++g_waitingTicks;
            log::info("[FramebufferCapture] Re-hid {} nodes + {} objs; waiting tick {}/{}",
                      extraNodes.size(), extraGameObj.size(),
                      g_waitingTicks, kMaxWaitingTicks);
            return; // skip the read this frame; try again next swap
        }
        if (foundDirty) {
            log::warn("[FramebufferCapture] Reached max waiting ticks ({}); reading anyway",
                      kMaxWaitingTicks);
            // Append them so they get restored properly in restoreCaptureState().
            for (auto& p : extraNodes) g_prep.hiddenNodes.push_back(p);
            for (auto& g : extraGameObj) g_prep.hiddenGameObjects.push_back(g);
        }

        // Death conditions were validated at request time, but keybind captures
        // run during live gameplay: the player can die in the 1-2 frames between
        // the request and this read. Abort cleanly instead of delivering a frame
        // with the death explosion (or a missing player).
        if (s_request.levelID != 0) {
            if (auto* pl = PlayLayer::get()) {
                bool dead = pl->m_playerDied || (pl->m_player1 && pl->m_player1->m_isDead);
                if (!dead) {
                    dead = paimon::capture::hasRecentDeath(pl->m_gameState.m_currentProgress);
                }
                if (dead) {
                    log::info("[FramebufferCapture] Player died mid-capture; aborting read");
                    restoreCaptureState();
                    if (s_request.callback) {
                        auto cb = std::move(s_request.callback);
                        cb(false, nullptr, nullptr, 0, 0);
                    }
                    s_request.active = false;
                    g_phase.store(Phase::Idle);
                    return;
                }
            }
        }

        auto* director = CCDirector::get();
        auto* glView   = director ? director->getOpenGLView() : nullptr;
        if (!director || !glView) {
            log::error("[FramebufferCapture] Reading: director/glView null");
            restoreCaptureState();
            if (s_request.callback) {
                auto cb = std::move(s_request.callback);
                cb(false, nullptr, nullptr, 0, 0);
            }
            s_request.active = false;
            g_phase.store(Phase::Idle);
            return;
        }

        CCSize fs = glView->getFrameSize();
        int W = static_cast<int>(fs.width);
        int H = static_cast<int>(fs.height);
        if (W <= 0 || H <= 0) {
            GLint vp[4] = {};
            glGetIntegerv(GL_VIEWPORT, vp);
            W = vp[2]; H = vp[3];
        }
        if (W <= 0 || H <= 0) {
            log::error("[FramebufferCapture] Invalid back-buffer dimensions");
            restoreCaptureState();
            if (s_request.callback) {
                auto cb = std::move(s_request.callback);
                cb(false, nullptr, nullptr, 0, 0);
            }
            s_request.active = false;
            g_phase.store(Phase::Idle);
            return;
        }

#ifdef GEODE_IS_WINDOWS
        // Async PBO readback — non-stalling, maps next pre-swap.
        if (issuePboRead(W, H)) {
            restoreCaptureState();
            g_phase.store(Phase::MapPBO);
            return;
        }
        log::warn("[FramebufferCapture] PBO readback unavailable; using sync read");
#endif

        // Sync fallback: ensure default FBO is bound for read (some hooks leave a non-default FBO).
        GLint origFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &origFBO);
        if (origFBO != 0) glBindFramebuffer(GL_FRAMEBUFFER, 0);

        auto rawPixels = std::make_shared<std::vector<uint8_t>>(
            static_cast<size_t>(W) * H * 4);

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rawPixels->data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);

        if (origFBO != 0) glBindFramebuffer(GL_FRAMEBUFFER, origFBO);

        GLenum err = glGetError();
        bool readOk = (err == GL_NO_ERROR) &&
                      pixelBufferHasContent(rawPixels->data(), rawPixels->size());

        // Retry once with explicit FBO 0 bind (some ANGLE hosts use separate render target).
        if (!readOk && origFBO != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rawPixels->data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glBindFramebuffer(GL_FRAMEBUFFER, origFBO);
            readOk = glGetError() == GL_NO_ERROR &&
                     pixelBufferHasContent(rawPixels->data(), rawPixels->size());
        }

        // Restore UI before the swap.
        restoreCaptureState();

        if (!readOk) {
            log::warn("[FramebufferCapture] Back-buffer read returned empty content");
            if (s_request.callback) {
                auto cb = std::move(s_request.callback);
                cb(false, nullptr, nullptr, 0, 0);
            }
            s_request.active = false;
            g_phase.store(Phase::Idle);
            return;
        }

        dispatchProcessing(std::move(rawPixels), W, H);
        return;
    }

#ifdef GEODE_IS_WINDOWS
    // MapPBO: GPU has finished the async read from last frame — non-stalling map.
    if (phase == Phase::MapPBO) {
        if (!g_pbo) {
            log::error("[FramebufferCapture] MapPBO with no PBO");
            if (s_request.callback) {
                auto cb = std::move(s_request.callback);
                cb(false, nullptr, nullptr, 0, 0);
            }
            s_request.active = false;
            g_phase.store(Phase::Idle);
            return;
        }

        int W = g_pboW, H = g_pboH;
        auto rawPixels = std::make_shared<std::vector<uint8_t>>();
        bool readOk = false;

        glBindBuffer(GL_PIXEL_PACK_BUFFER, g_pbo);
        if (void* mapped = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY)) {
            size_t bytes = static_cast<size_t>(W) * H * 4;
            auto* p = static_cast<uint8_t const*>(mapped);
            rawPixels->assign(p, p + bytes);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            readOk = pixelBufferHasContent(rawPixels->data(), rawPixels->size());
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        deletePboIfAny();

        if (!readOk) {
            log::warn("[FramebufferCapture] PBO map failed or returned empty content");
            if (s_request.callback) {
                auto cb = std::move(s_request.callback);
                cb(false, nullptr, nullptr, 0, 0);
            }
            s_request.active = false;
            g_phase.store(Phase::Idle);
            return;
        }

        dispatchProcessing(std::move(rawPixels), W, H);
        return;
    }
#endif

    // Phase::Idle|Reading: nothing to do.
}

// Shared tail of both read paths: downscale + FXAA on worker, texture upload on main.
void FramebufferCapture::dispatchProcessing(
    std::shared_ptr<std::vector<uint8_t>> rawPixels, int W, int H)
{
    int targetW = resolveTargetWidth(s_request.levelID, W);
    int targetH = std::max(1, static_cast<int>(
        std::round(static_cast<double>(H) * targetW / W)));
    int levelID = s_request.levelID;

    // Extract callback before worker thread to avoid dangling reference in s_request.
    auto callback = std::move(s_request.callback);
    s_request.active        = false;
    s_request.callback      = nullptr;
    s_request.nodeToCapture = nullptr;

    log::info("[FramebufferCapture] Processing: native={}x{} → output={}x{} (levelID={})",
              W, H, targetW, targetH, levelID);

    g_phase.store(Phase::Reading);

    uint64_t gen = g_generation.load(std::memory_order_relaxed);
    bool hdrOn = s_hdrMode;

    // Worker: flip + downscale + FXAA + alpha=255
    paimon::ThreadTracker::get().spawn(
        [rawPixels, W, H, targetW, targetH, gen, hdrOn,
         callback = std::move(callback)]() mutable {
            geode::utils::thread::setName("Paimon Capture Downscale");
            if (paimon::isRuntimeShuttingDown()) return;

            flipVerticalInPlace(rawPixels->data(), W, H, 4);
            forceAlphaOpaque(rawPixels->data(), rawPixels->size());

            std::shared_ptr<uint8_t> outBuf;
            int outW = W, outH = H;
            if (targetW < W) {
                outBuf = lanczosResizeRGBA(
                    rawPixels->data(), W, H, targetW, targetH);
                outW = targetW; outH = targetH;
            } else {
                // Alias vector storage (avoids copying 8-33 MB); aliasing shared_ptr keeps vector alive.
                outBuf = std::shared_ptr<uint8_t>(rawPixels, rawPixels->data());
            }

            if (paimon::isRuntimeShuttingDown() || !outBuf) {
                if (g_generation.load(std::memory_order_relaxed) == gen) {
                    g_phase.store(Phase::Idle);
                }
                return;
            }

            // Skip FXAA when Lanczos already anti-aliased (supersampled capture); FXAA for non-supersampled cases.
            if (hdrOn && targetW >= W) {
                applyFXAA(outBuf.get(), outW, outH);
            }

            // Texture upload must be on main thread (GL context).
            Loader::get()->queueInMainThread(
                [outBuf, outW, outH, gen,
                 callback = std::move(callback)]() mutable {
                    if (paimon::isRuntimeShuttingDown()) return;

                    // Superseded by a newer request — drop stale callback.
                    if (g_generation.load(std::memory_order_relaxed) != gen) {
                        log::info("[FramebufferCapture] Worker dropped: superseded");
                        return;
                    }

                    CCTexture2D* tex = makeTextureRGBA(outBuf.get(), outW, outH);
                    bool ok = tex != nullptr;
                    if (callback) callback(ok, tex, outBuf, outW, outH);
                    if (tex) tex->release();
                    g_phase.store(Phase::Idle);
                });
        });
}

// Legacy path drain (async path delivers via queueInMainThread).
void FramebufferCapture::processDeferredCallbacks() {
    if (s_deferredCallbacks.empty()) return;
    auto callbacks = std::move(s_deferredCallbacks);
    s_deferredCallbacks.clear();
    for (auto& d : callbacks) {
        if (d.callback) {
            d.callback(d.success, d.texture, d.rgbaData, d.width, d.height);
        }
        if (d.texture) d.texture->release();
    }
}

// Editor mini-preview renderer: same hide+offscreen as capture, no flash (single frame).
CCTexture2D* FramebufferCapture::renderPreviewTexture(int width, int height) {
    auto* pl = PlayLayer::get();
    if (!pl || width <= 0 || height <= 0) return nullptr;
    // Don't fight an in-flight capture over hide/restore state.
    if (g_phase.load() != Phase::Idle) return nullptr;

    auto hiddenNodes = hideNonVanillaUI();
    auto hiddenObjs  = hidePracticeAndEffectObjects();

    auto raw = renderPlayLayerToTexture(pl, width, height);

    for (auto& [weakNode, vis] : hiddenNodes) {
        if (auto node = weakNode.lock()) node->setVisible(vis);
    }
    for (auto& h : hiddenObjs) {
        auto obj = h.obj.lock();
        if (!obj) continue;
        obj->m_isDisabled2 = h.disabled2;
        obj->m_isInvisible = h.invisible;
        obj->setVisible(h.visible);
        obj->m_isHide      = h.hide;
        obj->setOpacity(h.opacity);
    }

    if (!raw) return nullptr;
    forceAlphaOpaque(raw->data(), raw->size());
    auto* tex = makeTextureRGBA(raw->data(), width, height);
    if (tex) tex->autorelease();
    return tex;
}

// doCaptureNode — kept for the legacy single-node RTT path.
void FramebufferCapture::doCaptureNode(CCNode* node) {
    if (!node) {
        if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
        return;
    }
    auto contentSize = node->getContentSize();
    float scale = node->getScale();
    int W = std::max(1, static_cast<int>(contentSize.width  * scale));
    int H = std::max(1, static_cast<int>(contentSize.height * scale));

    auto* rt = CCRenderTexture::create(W, H, kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
    if (!rt) {
        if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
        return;
    }

    auto origPos = node->getPosition();
    auto origAnchor = node->getAnchorPoint();
    node->setPosition(ccp(W / 2.f, H / 2.f));
    node->setAnchorPoint(ccp(0.5f, 0.5f));

    rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 0.f, 0);
    node->visit();
    rt->end();

    node->setPosition(origPos);
    node->setAnchorPoint(origAnchor);

    CCImage* img = rt->newCCImage(true);
    if (!img) {
        if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
        return;
    }

    int iw = img->getWidth(), ih = img->getHeight();
    unsigned char* data = img->getData();
    if (!data || iw <= 0 || ih <= 0) {
        img->release();
        if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
        return;
    }

    size_t bytes = static_cast<size_t>(iw) * ih * 4;
    std::shared_ptr<uint8_t> rgba(new uint8_t[bytes], std::default_delete<uint8_t[]>());
    std::memcpy(rgba.get(), data, bytes);
    forceAlphaOpaque(rgba.get(), bytes);
    img->release();

    auto* tex = makeTextureRGBA(rgba.get(), iw, ih);
    if (!tex) {
        if (s_request.callback) s_request.callback(false, nullptr, nullptr, 0, 0);
        return;
    }
    if (s_request.callback) {
        s_deferredCallbacks.push_back({s_request.callback, true, tex, rgba, iw, ih});
    } else {
        tex->release();
    }
}

