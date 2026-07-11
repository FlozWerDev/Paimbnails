// Wave (dart) treats every slope as a D block when ignore damage is on.
// Skip the slope collision while gliding so ignore-damage waves stop dying on slopes.
// Ported from Tinker (FixIgnoreDamage).

#include "../EditorModule.hpp"

#include <Geode/binding/PlayerObject.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonWaveIgnoreDamage, PlayerObject) {
    $override
    void collidedWithSlopeInternal(float dt, GameObject* object, bool forced) {
        if (moduleEnabled("editor-mod-wave-ignore-damage")
            && m_isDart && m_ignoreDamage && m_stateDartSlide <= 0) {
            return;
        }
        PlayerObject::collidedWithSlopeInternal(dt, object, forced);
    }
};
