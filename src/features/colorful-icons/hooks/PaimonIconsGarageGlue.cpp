#include "PaimonIconsGarageGlue.hpp"

#include "../services/IconConfigStore.hpp"
#include "../services/IconRecolorEngine.hpp"
#include "../ui/PaimonIconsConfigPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGarageLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ListButtonBar.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

namespace paimon::icons::garage {

// Smoke test: this $execute runs at mod load time. If you DON'T see
// "[paimon-icons] feature module loaded" in the log, the colorful-icons
// translation unit was stripped from the final DLL (likely by LTO because
// nothing else referenced it). The fix in that case is to make sure
// CMakeLists.txt picks up src/features/colorful-icons/**/*.cpp via
// GLOB_RECURSE - which it already does in this project.
$execute {
    geode::log::info("[paimon-icons] feature module loaded ({} v{})",
        Mod::get()->getID(), Mod::get()->getVersion().toVString(false));
}

namespace {

// Forward decl so the ticker can reference it.
void requestRecolor(GJGarageLayer* layer);

// A hidden CCNode that lives on the GJGarageLayer and ticks every N seconds
// to refresh icon recoloring. This keeps the garage in sync regardless of
// which gamemode tab the user opens, page swipes, color picker close, etc.
//
// We use a tick rather than hooks on every page-swap because there are too
// many entry points (tab buttons, arrows, page indicators, "more icons" mod,
// custom mods that add sub-bars). A 0.1s tick is cheap and bullet-proof.
class GarageRecolorTicker : public CCNode {
public:
    static GarageRecolorTicker* create(GJGarageLayer* host) {
        auto* n = new GarageRecolorTicker();
        n->m_host = host;
        n->autorelease();
        n->scheduleUpdate();
        return n;
    }

    void update(float dt) override {
        m_acc += dt;
        if (m_acc < 0.1f) return;  // 10 Hz is plenty
        m_acc = 0.0f;
        if (!m_host) return;
        // m_host is a raw pointer but we live as a child of it, so as long
        // as we are alive, the host is alive too.
        requestRecolor(m_host);
    }

private:
    GJGarageLayer* m_host = nullptr;
    float m_acc = 0.0f;
};

constexpr int kRecolorDelayFrames = 1;  // Wait one frame so node-ids and other
                                        // mods finish their post-init setup.

void requestRecolor(GJGarageLayer* layer) {
    if (!layer) return;
    log::info("[paimon-icons] requestRecolor called, feature={}, mode={}",
        IconConfigStore::get().isFeatureEnabled(),
        static_cast<int>(IconConfigStore::get().config().mode));

    // Recolor the kit's icon button-bar (every page, not just the visible one).
    if (auto* bar = layer->m_iconSelection) {
        IconRecolorEngine::get().recolorListBar(bar, RecolorArea::IconKit);
    }

    // Recolor the BIG selected icon shown above the bar (m_currentIcon).
    // It is a CCMenuItemSpriteExtra wrapping a GJItemIcon, so we can reuse
    // the engine's subtree walk.
    if (auto* current = layer->m_currentIcon) {
        IconRecolorEngine::get().recolorSubtree(current, RecolorArea::IconKit);
    }

    // Also walk the whole garage layer once: covers any other icons
    // (preview popups, secondary menus added by other mods, etc.) the
    // user might see without leaving the garage.
    IconRecolorEngine::get().recolorSubtree(layer, RecolorArea::IconKit);

    IconRecolorEngine::get().applySelectedHighlight(
        layer, layer->m_iconID, static_cast<int>(layer->m_selectedIconType));
    IconRecolorEngine::get().applyAnimations(layer, RecolorArea::IconKit);
}

void installGearButton(GJGarageLayer* layer) {
    if (!layer) return;
    if (layer->getChildByID("paimbnails/colorful-icons-gear-btn"_spr)) return;

    // Build the gear icon. Use the standard GD options gear (GJ_optionsBtn02_001).
    auto* gearIcon = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
    if (!gearIcon) return;
    gearIcon->setScale(0.7f);

    auto* gearSpr = CircleButtonSprite::create(
        gearIcon,
        CircleBaseColor::Cyan,
        CircleBaseSize::Medium
    );
    if (!gearSpr) return;

    auto* btn = CCMenuItemExt::createSpriteExtra(gearSpr, [](CCMenuItemSpriteExtra*) {
        paimon::icons::ui::PaimonIconsConfigPopup::open();
    });
    if (!btn) return;
    btn->setID("paimbnails/colorful-icons-gear-btn"_spr);
    btn->setScale(0.7f);

    // We try to attach to an existing menu so the button moves nicely with
    // any layout the user (or other mods) configure. The "category-menu" is
    // available via geode.node-ids; if not, fall back to a fresh CCMenu.
    auto* host = static_cast<CCMenu*>(layer->getChildByID("category-menu"));
    if (!host) {
        host = static_cast<CCMenu*>(layer->getChildByID("currency-menu"));
    }
    if (!host) {
        // Last resort: create our own host menu in the top-right.
        host = CCMenu::create();
        host->setID("paimbnails/colorful-icons-host-menu"_spr);
        host->setPosition({0, 0});
        layer->addChild(host, 100);

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        btn->setPosition({winSize.width - 25.f, winSize.height - 25.f});
        host->addChild(btn);
        return;
    }

    host->addChild(btn);
    host->updateLayout();
}

// One-time installation of a global config-changed listener that re-runs
// recolor on the currently-open garage. We register it from the first
// onGarageInit so we don't add it at $on_mod(Loaded) (where the WeakRef
// machinery would still need a live garage to be useful).
void ensureConfigListenerRegistered() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    static auto listener = IconConfigChangedEvent("").listen([]() {
        // When the config changes, find the running garage (if any) and
        // recolor it. Loader::queueInMainThread is unnecessary; this event
        // is dispatched from the popup which runs on main.
        log::info("[paimon-icons] IconConfigChangedEvent received");
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return ListenerResult::Propagate;
        auto* garage = scene->getChildByType<GJGarageLayer>(0);
        if (garage) {
            log::info("[paimon-icons] found garage, calling requestRecolor");
            requestRecolor(garage);
        } else {
            log::info("[paimon-icons] no garage in scene");
        }
        return ListenerResult::Propagate;
    });
    listener.leak();
}

}  // anonymous namespace

void onGarageInit(GJGarageLayer* layer) {
    if (!layer) return;
    log::info("[paimon-icons] onGarageInit fired (feature={})",
        IconConfigStore::get().isFeatureEnabled());
    IconConfigStore::get().load();
    ensureConfigListenerRegistered();
    installGearButton(layer);

    // Mount a low-frequency ticker that keeps the icons recolored even as
    // the user swipes between pages, switches gamemode tabs, or opens
    // sub-popups. Without this the recolor only runs at init / on color
    // change / on config change, missing many transient redraws.
    if (!layer->getChildByID("paimbnails/colorful-icons-ticker"_spr)) {
        auto* ticker = GarageRecolorTicker::create(layer);
        ticker->setID("paimbnails/colorful-icons-ticker"_spr);
        ticker->setVisible(false);
        layer->addChild(ticker, -100);
    }

    // Defer the first recolor by one frame so the bar's child icons have
    // finished initialising. We use queueInMainThread for that.
    Ref<GJGarageLayer> ref = layer;
    Loader::get()->queueInMainThread([ref]() {
        if (!ref) return;
        requestRecolor(ref);
    });
}

void onPlayerColorChanged(GJGarageLayer* layer) {
    if (!layer) return;
    requestRecolor(layer);
}

}  // namespace paimon::icons::garage
