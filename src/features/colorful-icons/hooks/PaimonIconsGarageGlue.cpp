#include "PaimonIconsGarageGlue.hpp"

#include "../services/IconConfigStore.hpp"
#include "../services/IconRecolorEngine.hpp"
#include "../ui/PaimonIconsConfigPopup.hpp"

#include <Geode/Geode.hpp>
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace paimon::icons::garage {

$execute {
    geode::log::debug("[paimon-icons] feature module loaded ({} v{})",
        Mod::get()->getID(), Mod::get()->getVersion().toVString(false));
}

namespace {

void requestRecolor(GJGarageLayer* layer) {
    if (!layer) return;

// Recolor the kit's icon button bar.
    if (auto* bar = layer->m_iconSelection) {
        IconRecolorEngine::get().recolorListBar(bar, RecolorArea::IconKit);
    }

// Walk the garage once to cover previews and secondary mod menus.
    IconRecolorEngine::get().recolorSubtree(layer, RecolorArea::IconKit);
}

void requestVanillaRestore(GJGarageLayer* layer) {
    if (!layer) return;
    if (auto* bar = layer->m_iconSelection) {
        IconRecolorEngine::get().restoreListBar(bar);
    }
    IconRecolorEngine::get().restoreVanilla(layer);
}

void applyCurrentConfig(GJGarageLayer* layer) {
    auto& store = IconConfigStore::get();
    if (store.isFeatureEnabled() && store.config().apply.kit) requestRecolor(layer);
    else requestVanillaRestore(layer);
}

GJGarageLayer* findGarageLayer(CCNode* root) {
    if (!root) return nullptr;
    if (auto* garage = typeinfo_cast<GJGarageLayer*>(root)) return garage;
    if (auto* children = root->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (auto* garage = findGarageLayer(child)) return garage;
        }
    }
    return nullptr;
}

void refreshVisibleGarage() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    applyCurrentConfig(findGarageLayer(scene));
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
        n->m_featureEnabled = IconConfigStore::get().isFeatureEnabled();
        n->autorelease();
        n->scheduleUpdate();
        return n;
    }

    void update(float dt) override {
        refreshButtonIcon();

        m_acc += dt;
        auto& store = IconConfigStore::get();
        bool const enabled = store.isFeatureEnabled();
        if (enabled != m_featureEnabled) {
            m_featureEnabled = enabled;
            applyCurrentConfig(m_host);
        }
        if (!enabled) {
            m_acc = 0.0f;
            return;
        }
// Rainbow repaints constantly; static modes only need periodic sweeps.
        const float interval =
            store.config().mode == ColorMode::Rainbow ? 0.1f : 0.5f;
        if (m_acc < interval) return;
        m_acc = 0.0f;
        if (m_host) requestRecolor(m_host);
    }

private:
// Mirror the selected icon and player colors on the gear button when changed.
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
    bool m_featureEnabled = false;
    int m_lastIconID  = -1;
    int m_lastTypeRaw = -1;
    ccColor3B m_lastC1{0, 0, 0};
    ccColor3B m_lastC2{0, 0, 0};
};

constexpr float kColumnStep = 40.f;

// Left-column menu used by the icon kit and other mods.
CCMenu* colorColumnOf(GJGarageLayer* layer) {
    auto* anchor = layer->getChildByIDRecursive("player-color-1-button");
    if (!anchor) anchor = layer->getChildByIDRecursive("player-color-2-button");
    if (!anchor) return nullptr;
    return typeinfo_cast<CCMenu*>(anchor->getParent());
}

// Return the button's mini SimplePlayer for ticker updates.
SimplePlayer* installPaimonIconsButton(GJGarageLayer* layer) {
    if (!layer) return nullptr;
    if (layer->getChildByID("paimbnails/paimon-icons-btn"_spr)) return nullptr;

// Wrap SimplePlayer because its zero content size would make BasedButtonSprite
// scale it infinitely.
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

// Node IDs do not expose category/currency menus; place this below the color
// buttons to avoid covering stats-menu.
    if (auto* column = colorColumnOf(layer)) {
        float lowestY = 1e9f;
        float x = 0.f;
        for (auto* child : CCArrayExt<CCNode*>(column->getChildren())) {
            if (!child->isVisible()) continue;
            if (child->getPositionY() < lowestY) {
                lowestY = child->getPositionY();
                x = child->getPositionX();
            }
        }
        if (lowestY < 1e8f) {
            CCPoint spot{x, lowestY - kColumnStep};
// Use the lower corner if the left column is already full.
            if (column->convertToWorldSpace(spot).y >= 24.f) {
                btn->setPosition(spot);
                column->addChild(btn);
                return mini;
            }
        }
    }

// Fallback menu in the lower-left, away from stats-menu.
    auto* host = CCMenu::create();
    host->setID("paimbnails/colorful-icons-host-menu"_spr);
    host->setPosition({0, 0});
    layer->addChild(host, 100);
    btn->setPosition({26.f, 26.f});
    host->addChild(btn);
    return mini;
}

// Repaint or restore the visible garage after config changes.
void ensureConfigListenerRegistered() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    static auto listener = IconConfigChangedEvent("").listen([]() {
        refreshVisibleGarage();
        return ListenerResult::Propagate;
    });
    listener.leak();

// Geode's settings UI bypasses IconConfigStore, so it needs this repaint path.
    listenForSettingChanges<bool>("colorful-icons-enabled", [](bool) {
        refreshVisibleGarage();
    });
}

}

void onGarageInit(GJGarageLayer* layer) {
    if (!layer) return;
    IconConfigStore::get().load();
    ensureConfigListenerRegistered();
    SimplePlayer* buttonIcon = installPaimonIconsButton(layer);

// Periodic ticker keeps pages, tabs, sub-popups, and the button icon in sync.
    if (!layer->getChildByID("paimbnails/colorful-icons-ticker"_spr)) {
        auto* ticker = GarageRecolorTicker::create(layer, buttonIcon);
        ticker->setID("paimbnails/colorful-icons-ticker"_spr);
        ticker->setVisible(false);
        layer->addChild(ticker, -100);
    }

// Defer the first recolor until bar children finish initializing.
    Ref<GJGarageLayer> ref = layer;
    Loader::get()->queueInMainThread([ref]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!ref) return;
        applyCurrentConfig(ref);
    });
}

void onPlayerColorChanged(GJGarageLayer* layer) {
    if (!layer) return;
    applyCurrentConfig(layer);
}

}
