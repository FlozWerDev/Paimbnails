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
    geode::log::debug("[paimon-icons] feature module loaded ({} v{})",
        Mod::get()->getID(), Mod::get()->getVersion().toVString(false));
}

namespace {

void requestRecolor(GJGarageLayer* layer) {
    if (!layer) return;

    // Recolor the kit's icon button-bar (every cached page).
    if (auto* bar = layer->m_iconSelection) {
        IconRecolorEngine::get().recolorListBar(bar, RecolorArea::IconKit);
    }

    // Walk the whole garage layer once -- covers the selected icon preview
    // and any secondary menus added by other mods.
    IconRecolorEngine::get().recolorSubtree(layer, RecolorArea::IconKit);
}

void requestVanillaRestore(GJGarageLayer* layer) {
    if (!layer) return;
    if (auto* bar = layer->m_iconSelection) {
        IconRecolorEngine::get().restoreListBar(bar);
    }
    IconRecolorEngine::get().restoreVanilla(layer);
}

bool colorsEqual(ccColor3B a, ccColor3B b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

class GarageRecolorTicker : public CCNode {
public:
    static GarageRecolorTicker* create(GJGarageLayer* host, SimplePlayer* buttonIcon) {
        auto* n = new GarageRecolorTicker();
        n->m_host = host;
        n->m_buttonIcon = buttonIcon;
        n->autorelease();
        n->scheduleUpdate();
        return n;
    }

    void update(float dt) override {
        refreshButtonIcon();

        m_acc += dt;
        auto& store = IconConfigStore::get();
        if (!store.isFeatureEnabled()) {
            m_acc = 0.0f;
            return;
        }
        // Rainbow needs constant repaints; static modes only need an
        // occasional sweep to catch icons created behind our hooks.
        const float interval =
            store.config().mode == ColorMode::Rainbow ? 0.1f : 0.5f;
        if (m_acc < interval) return;
        m_acc = 0.0f;
        if (m_host) requestRecolor(m_host);
    }

private:
    // Mirror the garage's selected icon + player colors on the gear button's
    // mini icon. Runs every frame but only touches nodes when something
    // actually changed.
    void refreshButtonIcon() {
        if (!m_buttonIcon || !m_host) return;

        int typeRaw = static_cast<int>(m_host->m_selectedIconType);
        if (typeRaw < 0 || typeRaw > 8) typeRaw = 0;
        int iconID = m_host->m_iconID;
        if (iconID < 1) iconID = 1;

        auto* gm = GameManager::get();
        if (!gm) return;
        ccColor3B c1 = gm->colorForIdx(gm->getPlayerColor());
        ccColor3B c2 = gm->colorForIdx(gm->getPlayerColor2());

        if (iconID == m_lastIconID && typeRaw == m_lastTypeRaw
            && colorsEqual(c1, m_lastC1) && colorsEqual(c2, m_lastC2)) {
            return;
        }
        m_lastIconID  = iconID;
        m_lastTypeRaw = typeRaw;
        m_lastC1      = c1;
        m_lastC2      = c2;

        m_buttonIcon->updatePlayerFrame(iconID, static_cast<IconType>(typeRaw));
        m_buttonIcon->setColors(c1, c2);
    }

    GJGarageLayer* m_host = nullptr;
    Ref<SimplePlayer> m_buttonIcon;
    float m_acc = 0.0f;
    int m_lastIconID  = -1;
    int m_lastTypeRaw = -1;
    ccColor3B m_lastC1{0, 0, 0};
    ccColor3B m_lastC2{0, 0, 0};
};

// Returns the button's mini SimplePlayer so the ticker can keep it in sync
// with the selected icon.
SimplePlayer* installPaimonIconsButton(GJGarageLayer* layer) {
    if (!layer) return nullptr;
    if (layer->getChildByID("paimbnails/paimon-icons-btn"_spr)) return nullptr;

    // The button shows the icon you have selected, live. SimplePlayer has a
    // zero content size, and BasedButtonSprite scales its top node by content
    // size (zero => infinite scale, painting the whole layer), so it must go
    // inside a wrapper with a real size.
    auto* mini = SimplePlayer::create(1);
    if (!mini) return nullptr;
    auto* wrap = CCNode::create();
    wrap->setContentSize({32.f, 32.f});
    wrap->setAnchorPoint({0.5f, 0.5f});
    mini->setPosition({16.f, 16.f});
    wrap->addChild(mini);

    auto* spr = CircleButtonSprite::create(
        wrap,
        CircleBaseColor::Cyan,
        CircleBaseSize::Medium
    );
    if (!spr) return nullptr;

    auto* btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
        paimon::icons::ui::PaimonIconsConfigPopup::open();
    });
    if (!btn) return nullptr;
    btn->setID("paimbnails/paimon-icons-btn"_spr);
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
        return mini;
    }

    host->addChild(btn);
    host->updateLayout();
    return mini;
}

// One-time registration of a config-changed listener that repaints (or
// restores) the garage that is currently on screen.
void ensureConfigListenerRegistered() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    static auto listener = IconConfigChangedEvent("").listen([]() {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return ListenerResult::Propagate;
        auto* garage = scene->getChildByType<GJGarageLayer>(0);
        if (!garage) return ListenerResult::Propagate;
        if (IconConfigStore::get().isFeatureEnabled()) {
            requestRecolor(garage);
        } else {
            // Master switch turned off: put the icons back to stock right away.
            requestVanillaRestore(garage);
        }
        return ListenerResult::Propagate;
    });
    listener.leak();
}

}  // anonymous namespace

void onGarageInit(GJGarageLayer* layer) {
    if (!layer) return;
    IconConfigStore::get().load();
    ensureConfigListenerRegistered();
    SimplePlayer* buttonIcon = installPaimonIconsButton(layer);

    // Mount a low-frequency ticker that keeps icons recolored even as the
    // user swipes pages, switches tabs, or opens sub-popups. It also mirrors
    // the selected icon on the button in real time.
    if (!layer->getChildByID("paimbnails/colorful-icons-ticker"_spr)) {
        auto* ticker = GarageRecolorTicker::create(layer, buttonIcon);
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
