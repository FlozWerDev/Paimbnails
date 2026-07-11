// Scale the editor HUD for smaller displays.
//
// Rewritten: the old version multiplied every child's scale but left its
// position untouched, so edge-docked toolbars drifted and overlapped
// ("bugged by scale"). Now each HUD node is scaled AND repositioned anchored
// to its nearest screen edge/corner, applied one frame after init so widgets
// added by other suite modules are included. Full-screen nodes are skipped.

#include "../EditorModule.hpp"
#include "../api/UIScaleAPI.hpp"

#include <Geode/binding/EditButtonBar.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <cmath>
#include <string>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

float uiScale() {
    return ui_scale::currentScale();
}

// Reference coordinate a node shrinks towards: its nearest screen edge on
// each axis, or the screen center when it sits in the middle band.
float axisAnchor(float pos, float extent) {
    if (pos < extent * 0.35f) return 0.f;
    if (pos > extent * 0.65f) return extent;
    return extent / 2.f;
}

void applyHudScale(EditorUI* ui, float s) {
    if (!ui) return;
    auto win = CCDirector::get()->getWinSize();
    for (auto* child : CCArrayExt<CCNode*>(ui->getChildren())) {
        if (!child) continue;
        // Skip full-screen overlays (dim layers, popups) — scaling them
        // leaves visible borders.
        auto cs = child->getContentSize();
        if (cs.width >= win.width - 1.f && cs.height >= win.height - 1.f) continue;

        // Skip Paimon panels / EditButtonBars — scaling them warps toolbars
        // into stacked gray slabs across the canvas.
        std::string id = std::string(child->getID());
        if (id.find("paimbnails") != std::string::npos
            || id.find("flozwer.paimbnails2") != std::string::npos) {
            continue;
        }
        if (typeinfo_cast<EditButtonBar*>(child)) continue;

        child->setScale(child->getScale() * s);
        auto p = child->getPosition();
        float ax = axisAnchor(p.x, win.width);
        float ay = axisAnchor(p.y, win.height);
        child->setPosition({ax + (p.x - ax) * s, ay + (p.y - ay) * s});
    }
}

} // namespace

class $modify(PaimonUIScaleEditor, EditorUI) {
    struct Fields {
        bool applied = false;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;
        if (!moduleEnabled("editor-mod-ui-scale")) return true;
        float s = uiScale();
        if (s >= 0.99f) return true;
        // Defer one frame so every other module's HUD widget exists too.
        Loader::get()->queueInMainThread([self = Ref(this), s] {
            if (!self || self->m_fields->applied) return;
            self->m_fields->applied = true;
            applyHudScale(self, s);
            ui_scale::notifyScaleChanged(s, true);
        });
        return true;
    }
};
