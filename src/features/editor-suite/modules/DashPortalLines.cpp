// Draw simple guides for selected dash orbs and portals in the editor grid.

#include "../EditorModule.hpp"

#include <Geode/binding/DrawGridLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/DrawGridLayer.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

bool on() { return moduleEnabled("editor-mod-dash-portal-lines"); }

// Common object IDs (2.2-ish)
bool isDashOrb(int id) {
    // yellow/pink/blue/green dash orbs & pads vary; common: 1704, 1751, etc.
    return id == 1704 || id == 1751 || id == 1019 || id == 2011 || id == 2012 || id == 2013;
}
bool isPortal(int id) {
    // cube/ship/ball/ufo/wave/robot/spider/swing portals (approx ranges)
    return (id >= 12 && id <= 47) || id == 111 || id == 660 || id == 745 || id == 1331
        || id == 1933 || id == 2861 || id == 2862 || id == 2863 || id == 2864;
}

ccColor4B portalColor(int id) {
    // rough palette
    if (id == 12 || id == 13) return {0, 255, 0, 180};       // cube-ish
    if (id == 45 || id == 46) return {0, 200, 255, 180};      // ship
    if (id == 47 || id == 99) return {255, 100, 255, 180};    // ball
    if (id == 111) return {255, 255, 0, 180};                 // ufo
    if (id == 660) return {100, 100, 255, 180};               // wave
    if (id == 745) return {255, 150, 50, 180};                // robot
    if (id == 1331) return {200, 100, 255, 180};              // spider
    return {255, 255, 255, 160};
}

} // namespace

class $modify(PaimonDashPortalGrid, DrawGridLayer) {
    $override
    void draw() {
        DrawGridLayer::draw();
        if (!on() || !m_editorLayer || !m_editorLayer->m_objects) return;

        int drawn = 0;
        constexpr int kMax = 80;

        for (auto* obj : CCArrayExt<GameObject*>(m_editorLayer->m_objects)) {
            if (!obj || !obj->m_isSelected) continue;
            int id = obj->m_objectID;
            auto pos = obj->getPosition();

            if (isDashOrb(id)) {
                // Horizontal dash guide ~ 5 blocks
                float len = 150.f;
                float rot = CC_DEGREES_TO_RADIANS(obj->getRotation());
                auto dir = ccp(std::cos(rot), -std::sin(rot));
                ccDrawColor4B(255, 220, 0, 200);
                glLineWidth(2.f);
                ccDrawLine(pos, pos + dir * len);
                if (++drawn >= kMax) return;
            } else if (isPortal(id)) {
                auto col = portalColor(id);
                ccDrawColor4B(col);
                glLineWidth(1.5f);
                // Vertical line through portal
                ccDrawLine(pos + ccp(0, -60), pos + ccp(0, 60));
                if (++drawn >= kMax) return;
            }
        }
    }
};
