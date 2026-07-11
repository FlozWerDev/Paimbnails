// Restore old color-trigger button textures for classic look (Tinker idea).
// Uses vanilla sprite frames that still ship in GD; no custom sheet required.

#include "../EditorModule.hpp"

#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <unordered_map>

using namespace geode::prelude;
using namespace paimon::editor;

namespace {

bool on() { return moduleEnabled("editor-mod-old-color-triggers"); }

// Common color-trigger object IDs → preferred frame names (vanilla or close).
// If a frame is missing we leave the object alone (safe).
char const* frameForId(int id) {
    switch (id) {
        case 29:  return "edit_eTintBGBtn_001.png";   // BG (legacy ids vary by version)
        case 30:  return "edit_eTintGBtn_001.png";
        case 105: return "edit_eTintObjBtn_001.png";
        case 744: return "edit_eTintLBtn_001.png";
        case 900: return "edit_eTint3DLBtn_001.png";
        case 899: return "edit_eTintG2Btn_001.png";
        default:  return nullptr;
    }
}

} // namespace

class $modify(PaimonOldColorTrig, EffectGameObject) {
    $override
    void customSetup() {
        EffectGameObject::customSetup();
        if (!on()) return;
        auto* frame = frameForId(m_objectID);
        if (!frame) return;
        auto* spr = CCSprite::createWithSpriteFrameName(frame);
        if (!spr) return;
        // Soft retexture if the object has a display sprite child
        this->setDisplayFrame(spr->displayFrame());
    }
};
