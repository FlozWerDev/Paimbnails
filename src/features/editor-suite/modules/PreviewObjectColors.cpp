// Tint create-tab object sprites with the level's color channel preview
// so builders can preview how objects will look (Tinker "Preview Object Colors").

#include "../EditorModule.hpp"

#include <Geode/binding/CreateMenuItem.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-preview-object-colors"); }
}

class $modify(PaimonPreviewObjColorsUI, EditorUI) {
    struct Fields {
        bool scheduled = false;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    void applyPreviews(float) {
        if (!on() || !m_createButtonArray || !m_editorLayer) return;
        auto* mgr = m_editorLayer->m_effectManager;
        if (!mgr) return;

        // Sample main object color channel (1004 = Obj) and a few user channels.
        auto objCol = mgr->colorForIndex(1004);
        ccColor3B tint{objCol.r, objCol.g, objCol.b};

        for (auto* item : CCArrayExt<CreateMenuItem*>(m_createButtonArray)) {
            if (!item) continue;
            auto* img = item->getNormalImage();
            if (!img) continue;
            // Soft tint on the root sprite and its children.
            auto applyTint = [&](CCNode* node, auto&& self) -> void {
                if (!node) return;
                if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
                    auto c = rgba->getColor();
                    ccColor3B mixed{
                        static_cast<GLubyte>((c.r * 0.35f) + (tint.r * 0.65f)),
                        static_cast<GLubyte>((c.g * 0.35f) + (tint.g * 0.65f)),
                        static_cast<GLubyte>((c.b * 0.35f) + (tint.b * 0.65f)),
                    };
                    rgba->setColor(mixed);
                }
                for (auto* ch : CCArrayExt<CCNode*>(node->getChildren())) {
                    self(ch, self);
                }
            };
            applyTint(img, applyTint);
        }
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!on()) return true;
        this->schedule(schedule_selector(PaimonPreviewObjColorsUI::applyPreviews), 0.5f);
        m_fields->scheduled = true;
        Loader::get()->queueInMainThread([self = Ref(this)] {
            if (self) static_cast<PaimonPreviewObjColorsUI*>(self.data())->applyPreviews(0.f);
        });
        return true;
    }

    $override
    void selectBuildTab(int tab) {
        EditorUI::selectBuildTab(tab);
        if (on()) applyPreviews(0.f);
    }
};
