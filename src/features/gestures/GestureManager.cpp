#include "GestureManager.hpp"
#include "../quick-hub/services/QuickHubManager.hpp"
#include "../quick-hub/ui/QuickHubRadial.hpp"
#include "../main-menu-layout/ui/MainMenuLayoutEditor.hpp"
#include "../main-menu-layout/services/MainMenuLayoutManager.hpp"
#include "../settings-panel/services/SettingsPanelManager.hpp"
#include "../paimon-assistant/ui/PaimonAssistantPopup.hpp"
#include "../feedback/ui/FeedbackPopup.hpp"
#include "../capture/ui/CaptureMenuPopup.hpp"
#include "../volume-scroll/services/VolumeScrollManager.hpp"
#include "../../core/BanGate.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../utils/PaimonNotification.hpp"

#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::pausezoom {
bool isPausedAndZoomable();
void zoomInStep();
void zoomOutStep();
void resetZoom();
void toggleMenu();
void pinchZoomAt(float scaleRatio, cocos2d::CCPoint focusPoint);
void panByDelta(cocos2d::CCPoint deltaPoints);
}

namespace paimon::capture {
void triggerCaptureFromGesture();
}

namespace paimon::gestures {
namespace {

constexpr char const* kSavedBindPrefix = "gesture-bind-";
constexpr char const* kSavedMoveTol = "gesture-move-tolerance-pt";
constexpr char const* kSavedSwipeSens = "gesture-swipe-sensitivity";
constexpr char const* kSavedHudFeedback = "gesture-hud-feedback";

constexpr float kSwipeBaseDistancePt = 54.0f;
constexpr double kSwipeMaxDurationSec = 0.55;
constexpr double kTapMaxDurationSec = 0.26;
constexpr double kDoubleTapWindowSec = 0.36;
constexpr float kTapMaxMovePt = 16.0f;

CCPoint computeCentroid(std::vector<std::pair<int, GestureManager::ActivePoint>> const& pts) {
    if (pts.empty()) return CCPointZero;
    float sx = 0.f, sy = 0.f;
    for (auto const& [id, p] : pts) {
        sx += p.curPt.x;
        sy += p.curPt.y;
    }
    float const inv = 1.0f / static_cast<float>(pts.size());
    return {sx * inv, sy * inv};
}

float twoFingerDistance(std::vector<std::pair<int, GestureManager::ActivePoint>> const& pts) {
    if (pts.size() < 2) return 0.f;
    return ccpDistance(pts[0].second.curPt, pts[1].second.curPt);
}

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool openLayoutEditorInCurrentScene() {
    using namespace paimon::menu_layout;
    if (!paimon::modules::isEnabled("paimbnails.menulayout.menu")) return false;
    if (MainMenuLayoutEditor::isActive()) return true;
    auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
    if (!scene) return false;

    CCLayer* topLayer = nullptr;
    if (auto* children = scene->getChildren()) {
        for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
            auto* node = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
            if (!node || !node->isVisible()) continue;
            if (auto* layer = typeinfo_cast<CCLayer*>(node)) {
                topLayer = layer;
                break;
            }
        }
    }
    if (!topLayer) return false;
    MainMenuLayoutManager::get().captureDefaultsAndApply(topLayer);
    MainMenuLayoutEditor::open(topLayer);
    return true;
}

bool isGameplayBlocked(GestureKind kind) {
    auto* pl = PlayLayer::get();
    if (!pl) {
        if (auto* ed = LevelEditorLayer::get()) {
            return ed->m_playbackMode != PlaybackMode::Not;
        }
        return false;
    }
    if (pl->m_isPaused) return false;
    // Unpaused gameplay must remain untouched by 1F/2F gestures; only explicit
    // 3-finger swipe/double-tap capture shortcuts may inspect unpaused gameplay.
    return GestureManager::fingerCountForKind(kind) < 3;
}

} // namespace

GestureManager& GestureManager::get() {
    static GestureManager instance;
    return instance;
}

std::vector<GestureActionMeta> const& GestureManager::actions() {
    static std::vector<GestureActionMeta> const list = {
        {GestureActionId::QuickHub, "quick-hub", "Quick Hub Radial",
         "Abre el menu radial de accesos rapidos", "General", GestureKind::Hold1F, false},
        {GestureActionId::LayoutEditor, "layout-editor", "Editor de Layout",
         "Personaliza botones del menu o pausa", "General", GestureKind::Hold2F, false},
        {GestureActionId::VolumeMusic, "volume-music", "Volumen de Musica",
         "Ajusta el volumen de la musica", "Audio", GestureKind::Drag3FVertical, true},
        {GestureActionId::VolumeSFX, "volume-sfx", "Volumen de SFX",
         "Ajusta el volumen de efectos", "Audio", GestureKind::Drag3FHorizontal, true},
        {GestureActionId::CaptureThumbnail, "capture-thumb", "Capturar Miniatura",
         "Toma una foto del nivel actual", "Captura", GestureKind::None, false},
        {GestureActionId::CaptureMenu, "capture-menu", "Menu de Captura",
         "Abre el menu rapido de captura", "Captura", GestureKind::None, false},
        {GestureActionId::SettingsPanel, "settings-panel", "Panel de Ajustes",
         "Abre el panel lateral de ajustes", "General", GestureKind::None, false},
        {GestureActionId::PaimonAssistant, "paimon-assistant", "Asistente Paimon",
         "Abre la superposicion del asistente", "General", GestureKind::None, false},
        {GestureActionId::PaimonFeedback, "paimon-feedback", "Paimon Feedback",
         "Captura la pantalla para enviar feedback", "General", GestureKind::None, false},
        {GestureActionId::PauseZoomIn, "pause-zoom-in", "Pause Zoom: Acercar",
         "Acerca la camara durante la pausa", "Gameplay", GestureKind::None, false},
        {GestureActionId::PauseZoomOut, "pause-zoom-out", "Pause Zoom: Alejar",
         "Aleja la camara durante la pausa", "Gameplay", GestureKind::None, false},
        {GestureActionId::PauseZoomReset, "pause-zoom-reset", "Pause Zoom: Reset",
         "Restaura el zoom en pausa", "Gameplay", GestureKind::None, false},
        {GestureActionId::PauseZoomToggleMenu, "pause-zoom-toggle", "Pause Zoom: Menu",
         "Oculta o muestra el menu de pausa", "Gameplay", GestureKind::None, false},
    };
    return list;
}

std::vector<GestureKind> const& GestureManager::discreteKinds() {
    static std::vector<GestureKind> const list = {
        GestureKind::None,
        GestureKind::Hold1F,
        GestureKind::Hold2F,
        GestureKind::Hold3F,
        GestureKind::DoubleTap2F,
        GestureKind::DoubleTap3F,
        GestureKind::SwipeUp2F,
        GestureKind::SwipeDown2F,
        GestureKind::SwipeLeft2F,
        GestureKind::SwipeRight2F,
        GestureKind::SwipeUp3F,
        GestureKind::SwipeDown3F,
        GestureKind::SwipeLeft3F,
        GestureKind::SwipeRight3F,
    };
    return list;
}

std::vector<GestureKind> const& GestureManager::allKinds() {
    static std::vector<GestureKind> const list = {
        GestureKind::None,
        GestureKind::Hold1F,
        GestureKind::Hold2F,
        GestureKind::Hold3F,
        GestureKind::DoubleTap2F,
        GestureKind::DoubleTap3F,
        GestureKind::SwipeUp2F,
        GestureKind::SwipeDown2F,
        GestureKind::SwipeLeft2F,
        GestureKind::SwipeRight2F,
        GestureKind::SwipeUp3F,
        GestureKind::SwipeDown3F,
        GestureKind::SwipeLeft3F,
        GestureKind::SwipeRight3F,
        GestureKind::Drag2FVertical,
        GestureKind::Drag2FHorizontal,
        GestureKind::Drag3FVertical,
        GestureKind::Drag3FHorizontal,
    };
    return list;
}

GestureActionMeta const* GestureManager::findAction(GestureActionId id) {
    for (auto const& a : actions()) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

GestureActionMeta const* GestureManager::findActionByKey(std::string_view key) {
    for (auto const& a : actions()) {
        if (a.key == key) return &a;
    }
    return nullptr;
}

char const* GestureManager::kindId(GestureKind kind) {
    switch (kind) {
        case GestureKind::None:             return "none";
        case GestureKind::Hold1F:           return "hold-1f";
        case GestureKind::Hold2F:           return "hold-2f";
        case GestureKind::Hold3F:           return "hold-3f";
        case GestureKind::DoubleTap2F:      return "dtap-2f";
        case GestureKind::DoubleTap3F:      return "dtap-3f";
        case GestureKind::SwipeUp2F:        return "swipe-up-2f";
        case GestureKind::SwipeDown2F:      return "swipe-down-2f";
        case GestureKind::SwipeLeft2F:      return "swipe-left-2f";
        case GestureKind::SwipeRight2F:     return "swipe-right-2f";
        case GestureKind::SwipeUp3F:        return "swipe-up-3f";
        case GestureKind::SwipeDown3F:      return "swipe-down-3f";
        case GestureKind::SwipeLeft3F:      return "swipe-left-3f";
        case GestureKind::SwipeRight3F:     return "swipe-right-3f";
        case GestureKind::Drag2FVertical:   return "drag-2f-v";
        case GestureKind::Drag2FHorizontal: return "drag-2f-h";
        case GestureKind::Drag3FVertical:   return "drag-3f-v";
        case GestureKind::Drag3FHorizontal: return "drag-3f-h";
    }
    return "none";
}

char const* GestureManager::kindDisplayName(GestureKind kind) {
    switch (kind) {
        case GestureKind::None:             return "Desactivado";
        case GestureKind::Hold1F:           return "Mantener 1 dedo";
        case GestureKind::Hold2F:           return "Mantener 2 dedos";
        case GestureKind::Hold3F:           return "Mantener 3 dedos";
        case GestureKind::DoubleTap2F:      return "Doble toque 2 dedos";
        case GestureKind::DoubleTap3F:      return "Doble toque 3 dedos";
        case GestureKind::SwipeUp2F:        return "Deslizar arriba 2D";
        case GestureKind::SwipeDown2F:      return "Deslizar abajo 2D";
        case GestureKind::SwipeLeft2F:      return "Deslizar izq. 2D";
        case GestureKind::SwipeRight2F:     return "Deslizar der. 2D";
        case GestureKind::SwipeUp3F:        return "Deslizar arriba 3D";
        case GestureKind::SwipeDown3F:      return "Deslizar abajo 3D";
        case GestureKind::SwipeLeft3F:      return "Deslizar izq. 3D";
        case GestureKind::SwipeRight3F:     return "Deslizar der. 3D";
        case GestureKind::Drag2FVertical:   return "Arrastre Vert. 2D";
        case GestureKind::Drag2FHorizontal: return "Arrastre Horiz. 2D";
        case GestureKind::Drag3FVertical:   return "Arrastre Vert. 3D";
        case GestureKind::Drag3FHorizontal: return "Arrastre Horiz. 3D";
    }
    return "Desactivado";
}

GestureKind GestureManager::kindFromId(std::string_view id) {
    for (auto kind : allKinds()) {
        if (id == kindId(kind)) return kind;
    }
    return GestureKind::None;
}

bool GestureManager::isDragKind(GestureKind kind) {
    return kind == GestureKind::Drag2FVertical || kind == GestureKind::Drag2FHorizontal ||
           kind == GestureKind::Drag3FVertical || kind == GestureKind::Drag3FHorizontal;
}

bool GestureManager::isHoldKind(GestureKind kind) {
    return kind == GestureKind::Hold1F || kind == GestureKind::Hold2F || kind == GestureKind::Hold3F;
}

int GestureManager::fingerCountForKind(GestureKind kind) {
    switch (kind) {
        case GestureKind::Hold1F:
            return 1;
        case GestureKind::Hold2F:
        case GestureKind::DoubleTap2F:
        case GestureKind::SwipeUp2F:
        case GestureKind::SwipeDown2F:
        case GestureKind::SwipeLeft2F:
        case GestureKind::SwipeRight2F:
        case GestureKind::Drag2FVertical:
        case GestureKind::Drag2FHorizontal:
            return 2;
        case GestureKind::Hold3F:
        case GestureKind::DoubleTap3F:
        case GestureKind::SwipeUp3F:
        case GestureKind::SwipeDown3F:
        case GestureKind::SwipeLeft3F:
        case GestureKind::SwipeRight3F:
        case GestureKind::Drag3FVertical:
        case GestureKind::Drag3FHorizontal:
            return 3;
        case GestureKind::None:
        default:
            return 0;
    }
}

GestureKind GestureManager::getBinding(GestureActionId id) const {
    auto const* meta = findAction(id);
    if (!meta) return GestureKind::None;
    std::string savedKey = std::string(kSavedBindPrefix) + meta->key;
    auto* mod = Mod::get();
    if (!mod) return meta->defaultGesture;
    std::string val = mod->getSavedValue<std::string>(savedKey, "");
    if (val.empty()) return meta->defaultGesture;
    return kindFromId(val);
}

void GestureManager::setBinding(GestureActionId id, GestureKind kind) {
    auto const* meta = findAction(id);
    auto* mod = Mod::get();
    if (!meta || !mod) return;
    if (!meta->supportsContinuousDrag && isDragKind(kind)) return;
    mod->setSavedValue<std::string>(std::string(kSavedBindPrefix) + meta->key, kindId(kind));
}

std::optional<GestureActionId> GestureManager::findConflict(GestureActionId exclude, GestureKind kind) const {
    if (kind == GestureKind::None) return std::nullopt;
    for (auto const& meta : actions()) {
        if (meta.id == exclude) continue;
        if (getBinding(meta.id) == kind) return meta.id;
    }
    return std::nullopt;
}

std::optional<GestureActionId> GestureManager::actionForKind(GestureKind kind) const {
    if (kind == GestureKind::None) return std::nullopt;
    for (auto const& meta : actions()) {
        if (getBinding(meta.id) == kind) return meta.id;
    }
    return std::nullopt;
}

void GestureManager::resetAllBindings() {
    for (auto const& meta : actions()) {
        setBinding(meta.id, meta.defaultGesture);
    }
    setHoldSeconds(paimon::quickhub::QuickHubManager::kHoldSecondsDefault);
    setMoveTolerancePt(14.0f);
    setSwipeSensitivity(1.0f);
    setHudFeedback(true);
}

GestureComfortSettings GestureManager::comfort() const {
    GestureComfortSettings out;
    out.holdSeconds = paimon::quickhub::QuickHubManager::holdSeconds();
    if (auto* mod = Mod::get()) {
        out.moveTolerancePt = std::clamp(mod->getSavedValue<float>(kSavedMoveTol, 14.0f), 8.0f, 32.0f);
        out.swipeSensitivity = std::clamp(mod->getSavedValue<float>(kSavedSwipeSens, 1.0f), 0.5f, 2.0f);
        out.hudFeedback = mod->getSavedValue<bool>(kSavedHudFeedback, true);
    }
    return out;
}

void GestureManager::setHoldSeconds(float seconds) {
    paimon::quickhub::QuickHubManager::setHoldSeconds(seconds);
}

void GestureManager::setMoveTolerancePt(float points) {
    if (auto* mod = Mod::get()) {
        if (!std::isfinite(points)) points = 14.0f;
        mod->setSavedValue<float>(kSavedMoveTol, std::clamp(std::round(points), 8.0f, 32.0f));
    }
}

void GestureManager::setSwipeSensitivity(float sensitivity) {
    if (auto* mod = Mod::get()) {
        if (!std::isfinite(sensitivity)) sensitivity = 1.0f;
        mod->setSavedValue<float>(kSavedSwipeSens, std::clamp(std::round(sensitivity * 10.f) / 10.f, 0.5f, 2.0f));
    }
}

void GestureManager::setHudFeedback(bool enabled) {
    if (auto* mod = Mod::get()) {
        mod->setSavedValue<bool>(kSavedHudFeedback, enabled);
    }
}

CCPoint GestureManager::viewPixelsToPoint(float px, float py) {
    auto* dir = CCDirector::get();
    if (!dir) return {px, py};
    auto win = dir->getWinSize();
    auto* glView = dir->getOpenGLView();
    if (!glView || win.width <= 0.f || win.height <= 0.f) return {px, py};
    auto frame = glView->getFrameSize();
    if (frame.width <= 0.f || frame.height <= 0.f) return {px, py};
    // CCEGLViewProtocol inputs arrive in top-left frame pixels. Convert to
    // Cocos bottom-left UI point space so movement thresholds match across DPIs.
    return {
        (px / frame.width) * win.width,
        ((frame.height - py) / frame.height) * win.height
    };
}

bool GestureManager::triggerAction(GestureActionId id) {
    if (paimon::isRuntimeShuttingDown() || paimon::ban::isBanned()) return false;

    auto const showFeedback = [this](char const* label) {
        if (!comfort().hudFeedback || !label) return;
        PaimonNotify::show(label, NotificationIcon::Info, 1.0f);
    };

    switch (id) {
        case GestureActionId::QuickHub: {
            if (!paimon::modules::isEnabled("paimbnails.quickhub.global")) return false;
            if (!paimon::quickhub::QuickHubManager::canOpenInCurrentContext()) return false;
            paimon::quickhub::QuickHubRadial::openRadial();
            return true;
        }
        case GestureActionId::LayoutEditor: {
            if (!paimon::quickhub::QuickHubManager::canOpenInCurrentContext()) return false;
            return openLayoutEditorInCurrentScene();
        }
        case GestureActionId::VolumeMusic: {
            paimon::volscroll::VolumeScrollManager::get().onScroll(
                paimon::volscroll::VolumeKind::Music, 0.05f);
            return true;
        }
        case GestureActionId::VolumeSFX: {
            paimon::volscroll::VolumeScrollManager::get().onScroll(
                paimon::volscroll::VolumeKind::SFX, 0.05f);
            return true;
        }
        case GestureActionId::CaptureThumbnail: {
            paimon::capture::triggerCaptureFromGesture();
            return true;
        }
        case GestureActionId::CaptureMenu: {
            CaptureMenuPopup::toggle();
            return true;
        }
        case GestureActionId::SettingsPanel: {
            SettingsPanelManager::get().toggle();
            return true;
        }
        case GestureActionId::PaimonAssistant: {
            paimon::assistant::PaimonAssistantPopup::toggle();
            return true;
        }
        case GestureActionId::PaimonFeedback: {
            paimon::feedback::FeedbackPopup::open();
            return true;
        }
        case GestureActionId::PauseZoomIn: {
            if (!paimon::pausezoom::isPausedAndZoomable()) return false;
            paimon::pausezoom::zoomInStep();
            showFeedback("Pause Zoom +");
            return true;
        }
        case GestureActionId::PauseZoomOut: {
            if (!paimon::pausezoom::isPausedAndZoomable()) return false;
            paimon::pausezoom::zoomOutStep();
            showFeedback("Pause Zoom -");
            return true;
        }
        case GestureActionId::PauseZoomReset: {
            if (!paimon::pausezoom::isPausedAndZoomable()) return false;
            paimon::pausezoom::resetZoom();
            showFeedback("Pause Zoom Reset");
            return true;
        }
        case GestureActionId::PauseZoomToggleMenu: {
            if (!paimon::pausezoom::isPausedAndZoomable()) return false;
            paimon::pausezoom::toggleMenu();
            return true;
        }
    }
    return false;
}

void GestureManager::handleDiscreteGesture(GestureKind kind) {
    if (kind == GestureKind::None || isDragKind(kind)) return;
    if (isGameplayBlocked(kind)) return;
    auto act = actionForKind(kind);
    if (!act) return;
    triggerAction(*act);
}

void GestureManager::resetGestureTracking() {
    m_active.clear();
    m_peakFingers = 0;
    m_movedPastTap = false;
    m_swipeFired = false;
    m_pinchActive = false;
    m_pinchHidingDisabled = false;
    m_blockedByUi = false;
    m_lastPinchDist = 0.f;
}

void GestureManager::onTouchesBegan(int num, int ids[], float xs[], float ys[], double, bool claimedByUi) {
    double const t = nowSec();
    bool const wasEmpty = m_active.empty();

    if (claimedByUi && wasEmpty) {
        m_blockedByUi = true;
    }

    for (int i = 0; i < num; ++i) {
        CCPoint pt = viewPixelsToPoint(xs[i], ys[i]);
        auto it = std::find_if(m_active.begin(), m_active.end(),
            [id = ids[i]](auto const& entry) { return entry.first == id; });
        if (it != m_active.end()) {
            it->second = {pt, pt, t};
        } else {
            m_active.push_back({ids[i], ActivePoint{pt, pt, t}});
        }
    }

    int const count = static_cast<int>(m_active.size());
    if (wasEmpty) {
        m_peakFingers = count;
        m_movedPastTap = false;
        m_swipeFired = false;
        m_pinchActive = false;
        m_strokeStartTime = t;
        m_strokeStartCentroid = computeCentroid(m_active);
        m_lastCentroid = m_strokeStartCentroid;
    } else {
        m_peakFingers = std::max(m_peakFingers, count);
        // Re-anchor stroke centroid on multi-finger escalation so earlier drift
        // doesn't trigger an immediate false swipe when 2nd/3rd finger lands.
        m_strokeStartTime = t;
        m_strokeStartCentroid = computeCentroid(m_active);
        m_lastCentroid = m_strokeStartCentroid;
    }

    if (!m_blockedByUi && count == 2 && paimon::pausezoom::isPausedAndZoomable()) {
        float const dist = twoFingerDistance(m_active);
        if (dist > 12.f) {
            m_pinchActive = true;
            m_lastPinchDist = dist;
            m_lastCentroid = computeCentroid(m_active);
        }
    } else {
        m_pinchActive = false;
    }
}

void GestureManager::onTouchesMoved(int num, int ids[], float xs[], float ys[], double) {
    if (m_active.empty()) return;

    for (int i = 0; i < num; ++i) {
        CCPoint pt = viewPixelsToPoint(xs[i], ys[i]);
        for (auto& [id, data] : m_active) {
            if (id == ids[i]) {
                data.curPt = pt;
                break;
            }
        }
    }

    if (m_blockedByUi) return;

    int const count = static_cast<int>(m_active.size());
    CCPoint const centroid = computeCentroid(m_active);

    if (ccpDistance(centroid, m_strokeStartCentroid) > kTapMaxMovePt) {
        m_movedPastTap = true;
    }

    // Pause Zoom multi-touch pinch & pan (2 fingers inside Pause menu)
    if (m_pinchActive && count == 2 && paimon::pausezoom::isPausedAndZoomable()) {
        float const dist = twoFingerDistance(m_active);
        if (m_lastPinchDist > 12.f && dist > 12.f) {
            float ratio = dist / m_lastPinchDist;
            if (std::fabs(ratio - 1.0f) > 0.004f) {
                paimon::pausezoom::pinchZoomAt(ratio, centroid);
                m_lastPinchDist = dist;
                m_movedPastTap = true;
                m_swipeFired = true; // don't also trigger a 2F swipe after zooming
            }
        }
        CCPoint panDelta{centroid.x - m_lastCentroid.x, centroid.y - m_lastCentroid.y};
        if (std::fabs(panDelta.x) > 0.25f || std::fabs(panDelta.y) > 0.25f) {
            paimon::pausezoom::panByDelta(panDelta);
        }
        m_lastCentroid = centroid;
        return;
    }

    m_lastCentroid = centroid;

    // 2F or 3F directional swipe detection
    if (!m_swipeFired && (count == 2 || count == 3)) {
        double const elapsed = nowSec() - m_strokeStartTime;
        if (elapsed <= kSwipeMaxDurationSec) {
            auto const comf = comfort();
            float const threshold = kSwipeBaseDistancePt / std::clamp(comf.swipeSensitivity, 0.5f, 2.0f);
            float const dx = centroid.x - m_strokeStartCentroid.x;
            float const dy = centroid.y - m_strokeStartCentroid.y;
            if (std::hypot(dx, dy) >= threshold) {
                GestureKind swipe = GestureKind::None;
                if (std::fabs(dx) >= std::fabs(dy)) {
                    if (count == 2) swipe = dx > 0 ? GestureKind::SwipeRight2F : GestureKind::SwipeLeft2F;
                    else            swipe = dx > 0 ? GestureKind::SwipeRight3F : GestureKind::SwipeLeft3F;
                } else {
                    if (count == 2) swipe = dy > 0 ? GestureKind::SwipeUp2F : GestureKind::SwipeDown2F;
                    else            swipe = dy > 0 ? GestureKind::SwipeUp3F : GestureKind::SwipeDown3F;
                }
                if (actionForKind(swipe).has_value() && !isGameplayBlocked(swipe)) {
                    m_swipeFired = true;
                    handleDiscreteGesture(swipe);
                }
            }
        }
    }
}

void GestureManager::onTouchesEnd(int num, int ids[], float xs[], float ys[], double, bool cancelled) {
    if (!cancelled) {
        for (int i = 0; i < num; ++i) {
            CCPoint pt = viewPixelsToPoint(xs[i], ys[i]);
            for (auto& [id, data] : m_active) {
                if (id == ids[i]) {
                    data.curPt = pt;
                    break;
                }
            }
        }
    }

    for (int i = 0; i < num; ++i) {
        std::erase_if(m_active, [id = ids[i]](auto const& entry) { return entry.first == id; });
    }

    if (m_active.size() != 2) {
        m_pinchActive = false;
    }

    if (!m_active.empty()) return;

    if (!cancelled && !m_blockedByUi && !m_swipeFired && !m_movedPastTap &&
        (m_peakFingers == 2 || m_peakFingers == 3)) {
        double const t = nowSec();
        if (t - m_strokeStartTime <= kTapMaxDurationSec) {
            if (m_lastTapFingers == m_peakFingers &&
                (t - m_lastTapTime) <= kDoubleTapWindowSec &&
                ccpDistance(m_strokeStartCentroid, m_lastTapCentroid) <= 45.0f) {
                GestureKind dtap = (m_peakFingers == 2)
                    ? GestureKind::DoubleTap2F
                    : GestureKind::DoubleTap3F;
                m_lastTapFingers = 0;
                m_lastTapTime = 0.0;
                handleDiscreteGesture(dtap);
                resetGestureTracking();
                return;
            }
            m_lastTapFingers = m_peakFingers;
            m_lastTapTime = t;
            m_lastTapCentroid = m_strokeStartCentroid;
        } else {
            m_lastTapFingers = 0;
        }
    }

    resetGestureTracking();
}

} // namespace paimon::gestures
