// When toggled on, run Build Helper automatically after paste.
// Uses FakePause API (no real pause layer shown).

#include "../EditorModule.hpp"
#include "../EditorAssets.hpp"
#include "../api/FakePause.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;
using namespace paimon::editor::assets;

namespace {
constexpr char const* kSavedState = "paim-auto-build-helper-enabled";

bool& autoBHActive() {
    static bool s = false;
    return s;
}
}

class $modify(PaimonAutoBHUI, EditorUI) {
    struct Fields {
        Ref<CCMenuItemToggler> tog;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void onToggleABH(CCObject*) {
        if (!moduleEnabled("editor-mod-auto-build-helper")) return;
        if (!m_fields->tog) return;
        // CCMenuItemToggler invokes the selector before changing its state.
        autoBHActive() = !m_fields->tog->isToggled();
        Mod::get()->setSavedValue(kSavedState, autoBHActive());
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        autoBHActive() = Mod::get()->getSavedValue<bool>(kSavedState, false);
        if (!moduleEnabled("editor-mod-auto-build-helper")) return true;

        // Tinker keeps this state toggle with the other toolbar toggles. Fall
        // back to undo-menu only on layouts that do not expose that menu.
        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("toolbar-toggles-menu"));
        bool const toolbarSlot = menu != nullptr;
        if (!menu) menu = typeinfo_cast<CCMenu*>(this->getChildByID("undo-menu"));
        if (!menu) return true;

        // Custom art is ~40pt while GJ_hammerIcon is ~20pt; ButtonSprite does
        // not rescale its top, so size the custom icon down to match.
        float const iconScale = hasCustom(files::autoBuildHelper) ? 0.5f : 1.f;
        auto* off = ButtonSprite::create(
            loadIcon(files::autoBuildHelper, {"GJ_hammerIcon_001.png"}, iconScale),
            40, true, 40.f, "GJ_button_01.png", 0.62f
        );
        auto* onSpr = ButtonSprite::create(
            loadIcon(files::autoBuildHelper, {"GJ_hammerIcon_001.png"}, iconScale),
            40, true, 40.f, "GJ_button_02.png", 0.62f
        );
        if (!off || !onSpr) return true;

        auto* tog = CCMenuItemToggler::create(
            off, onSpr, this, menu_selector(PaimonAutoBHUI::onToggleABH)
        );
        tog->setID("paimbnails/auto-build-helper");
        tog->toggle(autoBHActive());
        normalizeToolbarToggle(
            tog,
            toolbarSlot ? cocos2d::CCSize{40.f, 40.f} : cocos2d::CCSize{35.f, 40.f}
        );
        menu->addChild(tog);
        menu->updateLayout();
        if (m_uiItems) m_uiItems->addObject(tog);
        m_fields->tog = tog;
        return true;
    }

    $override
    CCArray* pasteObjects(gd::string str, bool withColor, bool noUndo) {
        auto* result = EditorUI::pasteObjects(str, withColor, noUndo);
        if (moduleEnabled("editor-mod-auto-build-helper") && autoBHActive() && m_editorLayer) {
            runBuildHelper(m_editorLayer);
        }
        return result;
    }
};
