// Floating view panel: grid, ground, hitboxes, effect lines, LDM, preview.
// Redesigned: one dark side panel with full-width clickable checkbox rows,
// opened from a compact "View" button on the left edge.
// (Only shown when native tabs are OFF — with tabs on, View lives as a tab.)

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../EditorUIKit.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GameObject.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {

bool hideLdm() {
    return Mod::get()->getSavedValue<bool>("paim-hide-ldm", false);
}
void setHideLdm(bool v) {
    Mod::get()->setSavedValue("paim-hide-ldm", v);
}

constexpr float kPanelW = 104.f;
constexpr float kRowW = kPanelW - 12.f;

} // namespace

// Hide LDM objects while the saved flag is on
class $modify(PaimonViewLDMObject, GameObject) {
    $override
    void setVisible(bool visible) {
        if (moduleEnabled("editor-mod-view-panel")
            && hideLdm()
            && m_isHighDetail
            && EditorUI::get()) {
            visible = false;
        }
        GameObject::setVisible(visible);
    }
};

class $modify(PaimonViewPanelUI, EditorUI) {
    struct Fields {
        CCNode* panel = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void refreshLdmVisibility() {
        if (!m_editorLayer || !m_editorLayer->m_objects) return;
        bool hide = hideLdm();
        for (auto* obj : CCArrayExt<GameObject*>(m_editorLayer->m_objects)) {
            if (!obj) continue;
            if (obj->m_isHighDetail) {
                // Hide LDM objects when flag on; restore when flag off.
                obj->setVisible(!hide);
            }
        }
    }

    void onTogglePanel(CCObject*) {
        if (m_fields->panel) {
            m_fields->panel->setVisible(!m_fields->panel->isVisible());
        }
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-view-panel")) return true;

        // Avoid duplicate VIEW: when native tabs are on, View is the tab button only.
        if (Mod::get()->getSettingValue<bool>("editor-mod-native-tabs")) {
            return true;
        }

        auto win = CCDirector::get()->getWinSize();

        // Compact opener docked to the left edge
        auto* openMenu = CCMenu::create();
        openMenu->setPosition({30.f, win.height * 0.62f});
        openMenu->setContentSize({0.f, 0.f});
        openMenu->setID("paimbnails/view-panel-open-menu");
        // Icon-only opener (eye/check) — no text "VIEW" plate.
        if (auto* openBtn = editBarToolButton(
                files::viewTab,
                { "GJ_checkOn_001.png", "edit_eStartPosBtn_001.png" },
                [this] { this->onTogglePanel(nullptr); }
            )) {
            openBtn->setID("paimbnails/view-panel-open");
            openMenu->addChild(openBtn);
        }
        this->addChild(openMenu, 50);
        if (m_uiItems) m_uiItems->addObject(openMenu);

        // Rows, top to bottom
        struct Row {
            char const* label;
            char const* gv; // nullptr => LDM row
        };
        constexpr Row kRows[] = {
            {"Grid", "0038"},
            {"Ground", "0037"},
            {"Hitboxes", "0045"},
            {"FX Lines", "0043"},
            {"Dur Lines", "0058"},
            {"Preview", "0036"},
            {"Hide LDM", nullptr},
        };
        constexpr int kRowCount = sizeof(kRows) / sizeof(kRows[0]);

        float const panelH = 34.f + kRowCount * uikit::kRowHeight;
        auto* panel = CCNode::create();
        panel->setID("paimbnails/view-panel");
        panel->setContentSize({kPanelW, panelH});
        panel->setAnchorPoint({0.f, 0.5f});
        panel->setPosition({6.f, win.height * 0.62f - 24.f - panelH / 2.f});
        panel->setVisible(false);

        if (auto* bg = uikit::darkPanel({kPanelW, panelH})) {
            bg->setPosition({kPanelW / 2.f, panelH / 2.f});
            panel->addChild(bg, -1);
        }

        auto* title = uikit::caption("VIEW", 0.32f);
        title->setPosition({kPanelW / 2.f, panelH - 13.f});
        panel->addChild(title);

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(panel->getContentSize());
        panel->addChild(menu);

        float y = panelH - 26.f - uikit::kRowHeight / 2.f;
        for (auto const& row : kRows) {
            CCMenuItemToggler* t = nullptr;
            if (row.gv) {
                bool cur = GameManager::get()->getGameVariable(row.gv);
                t = uikit::checkboxRow(row.label, kRowW, cur, [gv = row.gv](bool enabled) {
                    GameManager::get()->setGameVariable(gv, enabled);
                    if (auto* lel = LevelEditorLayer::get()) lel->updateOptions();
                });
            } else {
                t = uikit::checkboxRow(row.label, kRowW, hideLdm(), [this](bool enabled) {
                    setHideLdm(enabled);
                    this->refreshLdmVisibility();
                });
            }
            if (!t) continue;
            t->setPosition({kPanelW / 2.f, y});
            menu->addChild(t);
            y -= uikit::kRowHeight;
        }

        this->addChild(panel, 50);
        if (m_uiItems) m_uiItems->addObject(panel);
        m_fields->panel = panel;
        return true;
    }
};
