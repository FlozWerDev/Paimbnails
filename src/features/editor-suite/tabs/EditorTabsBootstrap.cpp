// Registers Editor Suite features through the Paimon Tabs API.
//
// Rules (learned the hard way):
//   * Search / Favs are ACTION-ONLY — open an overlay, never spawn a panel over
//     the create bar (that left full-width gray slabs on the canvas).
//   * View is a tiny floating card of toggles that does NOT hide create bars.

#include "EditorTabsAPI.hpp"
#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../EditorUIKit.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/loader/Loader.hpp>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;
using namespace paimon::editor::tabs;

namespace paimon::editor {
void openObjectSearchOverlay();
}

namespace {

bool settingOn(char const* key, bool def = true) {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting(key)) return def;
    return mod->getSettingValue<bool>(key);
}

bool hideLdm() {
    return Mod::get()->getSavedValue<bool>("paim-hide-ldm", false);
}
void setHideLdm(bool v) {
    Mod::get()->setSavedValue("paim-hide-ldm", v);
}

// View-only: short toggles in a compact card (no EditButtonBar chrome).
CCNode* buildViewTabContent() {
    std::vector<Ref<CCNode>> nodes;

    auto addGv = [&](char const* label, char const* gv) {
        bool cur = GameManager::get()->getGameVariable(gv);
        auto* t = uikit::fixedToggle(label, cur, [gv](bool enabled) {
            GameManager::get()->setGameVariable(gv, enabled);
            if (auto* lel = LevelEditorLayer::get()) {
                lel->updateOptions();
            }
        });
        if (t) nodes.push_back(t);
    };

    addGv("Grid", "0038");
    addGv("Ground", "0037");
    addGv("Hitbox", "0045");
    addGv("FX Line", "0043");
    addGv("Dur Line", "0058");
    addGv("Preview", "0036");

    auto* ldm = uikit::fixedToggle("Hide LDM", hideLdm(), [](bool enabled) {
        setHideLdm(enabled);
        auto* lel = LevelEditorLayer::get();
        if (!lel || !lel->m_objects) return;
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (obj && obj->m_isHighDetail) obj->setVisible(!enabled);
        }
    });
    if (ldm) nodes.push_back(ldm);

    return makeButtonBar(std::move(nodes));
}

void registerSuiteTabs() {
    if (!settingOn("editor-suite-enable", true) || !settingOn("editor-mod-native-tabs", true)) {
        log::info("[EditorTabs] native tabs disabled by settings");
        return;
    }

    // View — only panel tab
    {
        TabDesc d;
        d.id = "flozwer.paimbnails2/view-tab";
        d.mode = Mode::Build;
        d.displayName = "View";
        d.displayOrder = 10;
        d.actionOnly = false;
        d.createContent = [] { return buildViewTabContent(); };
        d.createIcon = tabIcon(files::viewTab, "GJ_checkOn_001.png");
        d.isEnabled = [] {
            return settingOn("editor-suite-enable")
                && settingOn("editor-mod-native-tabs")
                && settingOn("editor-mod-view-panel");
        };
        registerTab(std::move(d));
    }

    // Search — action only (overlay). Never a mid-screen panel.
    {
        TabDesc d;
        d.id = "flozwer.paimbnails2/search-tab";
        d.mode = Mode::Build;
        d.displayName = "Search";
        d.displayOrder = 20;
        d.actionOnly = true;
        d.onActivate = [] { paimon::editor::openObjectSearchOverlay(); };
        d.createIcon = tabIcon(files::objectSearch, "gj_findBtn_001.png");
        d.isEnabled = [] {
            return settingOn("editor-suite-enable")
                && settingOn("editor-mod-native-tabs")
                && settingOn("editor-mod-object-search");
        };
        registerTab(std::move(d));
    }
}

} // namespace

$on_mod(Loaded) {
    registerSuiteTabs();
    log::info("[EditorTabs] bootstrap (own backend) tabs={}", registeredTabIds().size());
}
