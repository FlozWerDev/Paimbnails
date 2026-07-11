// Hide/show editor UI toggle next to undo buttons + suite UIShow event.

#include "../EditorModule.hpp"
#include "../EditorEvents.hpp"
#include "../EditorAssets.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace paimon::editor;
using namespace paimon::editor::assets;

class $modify(PaimonHideUI, EditorUI) {
    struct Fields {
        Ref<CCMenuItemToggler> hideToggle;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void onHideToggle(CCObject*) {
        if (!moduleEnabled("editor-mod-hide-ui") || !m_fields->hideToggle) return;
        // create(on=visible, off=hidden): toggled reflects "off" face when UI hidden.
        this->showUI(m_fields->hideToggle->isToggled());
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-hide-ui")) return true;

        auto* menu = this->getChildByID("undo-menu");
        if (!menu) return true;

        // Off = UI visible, on = UI hidden. The callback observes the state
        // before the click, which is exactly the desired value for showUI().
        auto* tog = circleToggler(
            files::hideUiOff, { "GJ_checkOff_001.png" },
            files::hideUiOn,  { "GJ_checkOn_001.png" },
            0.85f,
            CircleBaseColor::Gray,
            CircleBaseColor::Green,
            this,
            menu_selector(PaimonHideUI::onHideToggle),
            CircleBaseSize::Small
        );
        if (!tog) return true;
        if (auto* off = typeinfo_cast<CCNodeRGBA*>(tog->m_offButton)) {
            off->setOpacity(160);
        }
        tog->setID("paimbnails/hide-ui-toggle");
        tog->m_notClickable = true;
        normalizeToolbarToggle(tog);
        menu->addChild(tog);
        menu->updateLayout();
        m_fields->hideToggle = tog;
        return true;
    }

    $override
    void showUI(bool show) {
        EditorUI::showUI(show);
        EditorUIShowEvent().send(this, show);
        if (m_fields->hideToggle) {
            bool const desired = !show;
            // A click callback runs before CCMenuItemToggler flips itself.
            // Synchronize on the next main-thread turn so we never double-flip.
            Loader::get()->queueInMainThread([
                toggle = Ref(m_fields->hideToggle.data()), desired
            ] {
                if (toggle && toggle->isToggled() != desired) {
                    toggle->toggle(desired);
                }
            });
            if (m_editorLayer) {
                m_fields->hideToggle->setVisible(
                    m_editorLayer->m_playbackMode != PlaybackMode::Playing
                );
            }
        }
    }
};
