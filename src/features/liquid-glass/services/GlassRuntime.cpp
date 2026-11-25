#include "GlassRuntime.hpp"
#include "GlassCapture.hpp"
#include "GlassConfig.hpp"
#include "GlassDecorator.hpp"
#include "../../dynamic-ambient/services/AmbientRuntime.hpp"
#include "../../../core/Settings.hpp"

#include <Geode/loader/GameEvent.hpp>
#include <Geode/ui/WindowResizeEvent.hpp>

using namespace geode::prelude;

namespace paimon::glass {
namespace {

// frees idle copies once per second. Nothing in the material runs per
// frame unless a plate is actually drawn, apart from the popup watcher:
// a look at the running scene's direct children.
class GlassTicker : public CCObject {
public:
    void tick(float) {
        GlassCapture::get().trimIdle();
        GlassRuntime::get().refreshMotion();
    }

    // runs inside the scheduler, so before the scene is visited and drawn.
    void update(float) override {
        watchPopups();
    }
};

// after the other update targets, so popups they open this frame count.
constexpr int kWatcherPriority = 1000;

void startTicker() {
    static GlassTicker* ticker = [] {
        auto* object = new GlassTicker();
        auto* scheduler = CCScheduler::get();
        scheduler->scheduleSelector(
            schedule_selector(GlassTicker::tick), object, 1.f, false);
        scheduler->scheduleUpdateForTarget(object, kWatcherPriority, false);
        return object;
    }();
    (void)ticker;
}

} // namespace

GlassRuntime& GlassRuntime::get() {
    static GlassRuntime instance;
    return instance;
}

void GlassRuntime::ensureLoaded() {
    if (!m_loaded) reload();
}

void GlassRuntime::reload() {
    bool const first = !m_loaded;
    m_loaded = true;
    m_settings = loadSettings();
    m_tier = resolveTier(m_settings, isMobilePlatform());
    if (m_tier != GlassTier::Off) {
        paimon::ambient::AmbientRuntime::get().forceDisable();
    }
    m_profile = profileFor(m_tier);
    ++m_revision;
    refreshMotion();
    GlassCapture::get().setProfile(m_profile, blurSpreadFor(m_settings.blur));
    if (first) {
        startTicker();
        // 2.2091 popups can rebuild their content on resize; plates are
        // children of the old background, so decorate the new one.
        m_resizeListener = WindowResizeEvent().listen([](CCSize const&) {
            if (!GlassRuntime::get().active()) return;
            Loader::get()->queueInMainThread([] { decorateRunningScene(); });
        });
    }
}

GlassSettings const& GlassRuntime::settings() {
    ensureLoaded();
    return m_settings;
}

GlassTier GlassRuntime::tier() {
    ensureLoaded();
    return m_tier;
}

GlassProfile const& GlassRuntime::profile() {
    ensureLoaded();
    return m_profile;
}

void GlassRuntime::refreshMotion() {
    m_reducedMotion = paimon::settings::smoothui::reducedMotion();
}

bool GlassRuntime::allows(GlassScope scope) {
    ensureLoaded();
    if (m_tier == GlassTier::Off) return false;
    switch (scope) {
        case GlassScope::Popups: return m_settings.popups;
        case GlassScope::Buttons: return m_settings.buttons;
        case GlassScope::Always: return true;
    }
    return false;
}

} // namespace paimon::glass

// start the ticker (and the popup watcher) even if the first popup shown
// has a native show() and no ButtonSprite has loaded the settings yet.
$on_game(Loaded) {
    (void)paimon::glass::GlassRuntime::get().settings();
}
