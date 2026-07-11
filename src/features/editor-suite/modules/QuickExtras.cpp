// Quick Extras: shortcut to Edit Special / Extra object properties (Tinker-inspired).

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/SetGroupIDLayer.hpp>
#include <Geode/binding/SetupObjectOptionsPopup.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {
bool on() { return moduleEnabled("editor-mod-quick-extras"); }
}

class $modify(PaimonQuickExtrasUI, EditorUI) {
    struct Fields {
        Ref<CCMenuItemSpriteExtra> button;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void onQuickExtras(CCObject*) {
        if (!on()) return;
        bool const hasSelection = m_selectedObject
            || (m_selectedObjects && m_selectedObjects->count() > 0);
        if (!hasSelection) return;

        auto* groupLayer = SetGroupIDLayer::create(m_selectedObject, m_selectedObjects);
        if (auto* popup = SetupObjectOptionsPopup::create(
                m_selectedObject, m_selectedObjects, groupLayer
            )) {
            popup->show();
        }
    }

    void syncQuickExtrasButton() {
        if (!m_fields->button) return;
        bool const enabled = m_selectedObject
            || (m_selectedObjects && m_selectedObjects->count() > 0);
        m_fields->button->setEnabled(enabled);
        m_fields->button->m_animationEnabled = enabled;
        if (auto* image = typeinfo_cast<CCNodeRGBA*>(m_fields->button->getNormalImage())) {
            image->setColor(enabled ? ccColor3B{255, 255, 255} : ccColor3B{166, 166, 166});
            image->setOpacity(enabled ? 255 : 175);
        }
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!on()) return true;

        CCNode* menuNode = this->querySelector("editor-buttons-menu");
        if (!menuNode && m_editSpecialBtn) menuNode = m_editSpecialBtn->getParent();
        if (!menuNode) return true;

        auto* icon = loadIcon(
            files::quickExtras,
            { "edit_eStartPosBtn_001.png", "GJ_optionsBtn_001.png" },
            1.f
        );
        if (!icon) return true;
        auto* spr = EditorButtonSprite::create(icon, EditorBaseColor::Pink);
        if (!spr) return true;
        auto* btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(PaimonQuickExtrasUI::onQuickExtras)
        );
        btn->setID("paimbnails/quick-extras");
        fitGridItem(btn, menuNode);
        menuNode->addChild(btn);
        if (m_uiItems) m_uiItems->addObject(btn);
        if (auto* menu = typeinfo_cast<CCMenu*>(menuNode)) menu->updateLayout();
        m_fields->button = btn;
        syncQuickExtrasButton();
        return true;
    }

    $override
    void updateButtons() {
        EditorUI::updateButtons();
        syncQuickExtrasButton();
    }
};
