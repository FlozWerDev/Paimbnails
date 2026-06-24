#include "PaimonIconsGarageGlue.hpp"

#include "../services/IconConfigStore.hpp"
#include "../services/IconRecolorEngine.hpp"
#include "../ui/PaimonIconsConfigPopup.hpp"

#include <Geode/Geode.hpp>
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace paimon::icons::garage {

// Smoke test: if you don't see this in the log, colorful-icons was stripped
// from the DLL (likely by LTO). Fix: CMakeLists.txt GLOB_RECURSE already
// picks up these files.
$execute {
    geode::log::info("[paimon-icons] feature module loaded ({} v{})",
        Mod::get()->getID(), Mod::get()->getVersion().toVString(false));
}

namespace {

void requestRecolor(GJGarageLayer* layer);

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
        if (m_acc < 0.1f) return;
        m_acc = 0.0f;
        if (!m_host) return;
        requestRecolor(m_host);
    }

private:
    GJGarageLayer* m_host = nullptr;
    float m_acc = 0.0f;
};

// Wait one frame so node-ids and other mods finish their post-init setup.
constexpr int kRecolorDelayFrames = 1;

void requestRecolor(GJGarageLayer* layer) {
    if (!layer) return;
    log::info("[paimon-icons] requestRecolor called, feature={}, mode={}",
        IconConfigStore::get().isFeatureEnabled(),
        static_cast<int>(IconConfigStore::get().config().mode));

    // Recolor the kit's icon button-bar (every page).
    if (auto* bar = layer->m_iconSelection) {
        IconRecolorEngine::get().recolorListBar(bar, RecolorArea::IconKit);
    }

    // Recolor the big selected icon (m_currentIcon).
    if (auto* current = layer->m_currentIcon) {
        IconRecolorEngine::get().recolorSubtree(current, RecolorArea::IconKit);
    }

    // Walk the whole garage layer once -- covers preview popups, secondary
    // menus added by other mods, etc.
    IconRecolorEngine::get().recolorSubtree(layer, RecolorArea::IconKit);

    IconRecolorEngine::get().applySelectedHighlight(
        layer, layer->m_iconID, static_cast<int>(layer->m_selectedIconType));
    IconRecolorEngine::get().applyAnimations(layer, RecolorArea::IconKit);
}

void installGearButton(GJGarageLayer* layer) {
    if (!layer) return;
    if (layer->getChildByID("paimbnails/colorful-icons-gear-btn"_spr)) return;

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

    // Attach to an existing menu so the button moves with layouts.
    // Try "category-menu" (via node-ids), then "currency-menu", then create fresh.
    auto* host = static_cast<CCMenu*>(layer->getChildByID("category-menu"));
    if (!host) {
        host = static_cast<CCMenu*>(layer->getChildByID("currency-menu"));
    }
    if (!host) {
        // Last resort: create our own host menu.
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

// One-time registration of a config-changed listener that recolor the garage.
void ensureConfigListenerRegistered() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    static auto listener = IconConfigChangedEvent("").listen([]() {
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

    // Mount a low-frequency ticker that keeps icons recolored even as the
    // user swipes pages, switches tabs, or opens sub-popups.
    if (!layer->getChildByID("paimbnails/colorful-icons-ticker"_spr)) {
        auto* ticker = GarageRecolorTicker::create(layer);
        ticker->setID("paimbnails/colorful-icons-ticker"_spr);
        ticker->setVisible(false);
        layer->addChild(ticker, -100);
    }

    // Defer the first recolor by one frame so the bar's child icons
    // have finished initialising.
    Ref<GJGarageLayer> ref = layer;
    Loader::get()->queueInMainThread([ref]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!ref) return;
        requestRecolor(ref);
    });
}

void onPlayerColorChanged(GJGarageLayer* layer) {
    if (!layer) return;
    requestRecolor(layer);
}

}  // namespace paimon::icons::garage
