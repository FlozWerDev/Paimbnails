#include "AmbientRuntime.hpp"
#include "AmbientAnalyzer.hpp"
#include "AmbientConfig.hpp"
#include "AmbientDecorator.hpp"
#include "../../liquid-glass/services/GlassRuntime.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/loader/GameEvent.hpp>
#include <Geode/ui/WindowResizeEvent.hpp>

using namespace geode::prelude;

namespace paimon::ambient {
namespace {

class AmbientTicker : public CCObject {
public:
    void update(float dt) override {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!AmbientRuntime::get().active()) return;
        AmbientAnalyzer::get().advanceFrame(dt);
        watchPopups();
    }
};

// Same late priority tier as GlassTicker so popups opened this frame are caught before draw.
constexpr int kWatcherPriority = 1001;

void startTicker() {
    static AmbientTicker* ticker = [] {
        auto* object = new AmbientTicker();
        CCScheduler::get()->scheduleUpdateForTarget(object, kWatcherPriority, false);
        return object;
    }();
    (void)ticker;
}

} // namespace

AmbientRuntime& AmbientRuntime::get() {
    static AmbientRuntime instance;
    return instance;
}

void AmbientRuntime::ensureLoaded() {
    if (!m_loaded) reload();
}

void AmbientRuntime::reload() {
    bool const first = !m_loaded;
    m_loaded = true;
    m_settings = loadSettings();
    // Enforce mutual exclusion: Dynamic Ambient cannot stay active with Liquid Glass on.
    if (m_settings.enabled && isBlockedByGlass()) {
        m_settings.enabled = false;
        saveSettings(m_settings);
    }
    ++m_revision;
    if (first) {
        startTicker();
        m_resizeListener = WindowResizeEvent().listen([](CCSize const&) {
            if (!AmbientRuntime::get().active()) return;
            Loader::get()->queueInMainThread([] { decorateRunningScene(); });
        });
    }
}

AmbientSettings const& AmbientRuntime::settings() {
    ensureLoaded();
    return m_settings;
}

bool AmbientRuntime::isBlockedByGlass() const {
    return paimon::glass::GlassRuntime::get().active();
}

bool AmbientRuntime::active() {
    ensureLoaded();
    return m_settings.enabled && !isBlockedByGlass();
}

bool AmbientRuntime::allows(AmbientScope scope) {
    if (!active()) return false;
    switch (scope) {
        case AmbientScope::Buttons: return m_settings.affectButtons;
        case AmbientScope::Panels:  return m_settings.affectPanels;
        case AmbientScope::Assets:  return m_settings.affectAssets;
        case AmbientScope::Always:  return true;
    }
    return false;
}

bool AmbientRuntime::setEnabled(bool enabled) {
    ensureLoaded();
    if (enabled && isBlockedByGlass()) {
        return false;
    }
    m_settings.enabled = enabled;
    saveSettings(m_settings);
    ++m_revision;
    if (enabled) decorateRunningScene();
    return true;
}

void AmbientRuntime::forceDisable() {
    ensureLoaded();
    if (!m_settings.enabled) return;
    m_settings.enabled = false;
    saveSettings(m_settings);
    ++m_revision;
}

} // namespace paimon::ambient

$on_game(Loaded) {
    (void)paimon::ambient::AmbientRuntime::get().settings();
}
