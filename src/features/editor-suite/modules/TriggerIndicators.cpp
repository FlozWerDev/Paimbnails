// Draw lines from triggers to target / center groups (BetterEdit-inspired).
// Uses Paimon TriggerUtil API: colors, dashed center lines, clustering, arrows.

#include "../EditorModule.hpp"
#include "../api/TriggerUtil.hpp"

#include <Geode/binding/DrawGridLayer.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/DrawGridLayer.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;
using namespace paimon::editor::triggers;

class $modify(PaimonTriggerLinesGrid, DrawGridLayer) {
    $override
    void draw() {
        DrawGridLayer::draw();
        if (!moduleEnabled("editor-mod-trigger-lines")) return;
        if (!m_editorLayer || !m_editorLayer->m_objectLayer) return;

        bool showAll = moduleSetting<bool>("editor-mod-trigger-lines-all", false);
        bool arrows = moduleSetting<bool>("editor-mod-trigger-lines-arrows", true);
        bool cluster = moduleSetting<bool>("editor-mod-trigger-lines-cluster", true);
        auto* effects = m_effectGameObjects;
        if (!effects) return;

        int drawn = 0;
        constexpr int kMaxLines = 500;

        for (auto* obj : CCArrayExt<GameObject*>(effects)) {
            auto* trig = typeinfo_cast<EffectGameObject*>(obj);
            if (!trig) continue;
            if (!showAll && !trig->m_isSelected) continue;

            auto groups = groupsOf(trig);
            if (groups.targetGroup <= 0 && !groups.hasCenter) continue;

            auto from = trig->getPosition();
            auto col = colorForObjectID(trig->m_objectID);

            auto drawGroup = [&](int gid, bool dashed) {
                if (gid <= 0) return;
                auto objs = objectsInGroup(m_editorLayer, gid);
                if (objs.empty()) return;

                std::vector<CCPoint> pts;
                pts.reserve(objs.size());
                for (auto* o : objs) {
                    if (!o || o == trig) continue;
                    pts.push_back(o->getPosition());
                }
                if (cluster && pts.size() > 1) {
                    pts = clusterPoints(pts, 45.f);
                }
                for (auto const& to : pts) {
                    if (dashed) drawDashedLine(from, to, col);
                    else drawSolidLine(from, to, col);
                    if (arrows) drawArrowHead(from, to, col);
                    if (++drawn >= kMaxLines) return;
                }
            };

            drawGroup(groups.targetGroup, false);
            if (drawn >= kMaxLines) return;
            if (groups.hasCenter) {
                // Center groups: dashed (BE convention).
                auto centerCol = col;
                centerCol.a = static_cast<GLubyte>(std::min(255, col.a + 0));
                // slightly dimmer via thinner dash already
                drawGroup(groups.centerGroup, true);
            }
            if (drawn >= kMaxLines) return;
        }
    }
};
