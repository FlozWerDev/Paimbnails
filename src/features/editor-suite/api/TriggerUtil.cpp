#include "TriggerUtil.hpp"

#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/utils/cocos.hpp>
#include <cmath>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editor::triggers {

ccColor4B colorForObjectID(int objectID) {
    switch (objectID) {
        case 901:  return {255, 0, 255, 210};     // Move
        case 1346: return {127, 127, 255, 210};   // Rotate
        case 2067: return {63, 191, 255, 210};    // Scale
        case 1006: return {255, 255, 0, 210};     // Pulse
        case 1007: return {0, 255, 255, 210};     // Alpha
        case 1049: return {0, 255, 127, 210};     // Toggle
        case 1268: return {35, 204, 127, 210};    // Spawn
        case 1616: return {255, 120, 40, 210};    // Stop
        case 1815: return {255, 80, 80, 210};     // Collision
        case 1811: return {200, 200, 255, 210};   // OnDeath
        case 1817: return {180, 255, 180, 210};   // Instant Count
        case 3600: return {255, 180, 255, 210};   // Random
        default:   return {255, 255, 255, 180};
    }
}

TriggerTargets groupsOf(EffectGameObject* trig) {
    TriggerTargets t{};
    if (!trig) return t;
    t.targetGroup = trig->m_targetGroupID;
    t.centerGroup = trig->m_centerGroupID;
    t.hasCenter = t.centerGroup > 0 && t.centerGroup != t.targetGroup;
    return t;
}

std::vector<GameObject*> objectsInGroup(LevelEditorLayer* lel, int group) {
    std::vector<GameObject*> out;
    if (!lel || group <= 0) return out;

    if (lel->m_groupDict) {
        if (auto* arr = typeinfo_cast<CCArray*>(lel->m_groupDict->objectForKey(group))) {
            for (auto* o : CCArrayExt<GameObject*>(arr)) {
                if (o) out.push_back(o);
            }
            return out;
        }
    }

    if (!lel->m_objects) return out;
    for (auto* o : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!o || !o->m_groups) continue;
        for (short i = 0; i < o->m_groupCount; ++i) {
            if (o->m_groups->at(static_cast<size_t>(i)) == group) {
                out.push_back(o);
                break;
            }
        }
    }
    return out;
}

void drawSolidLine(CCPoint a, CCPoint b, ccColor4B col, float width) {
    ccDrawColor4B(col);
    glLineWidth(width);
    ccDrawLine(a, b);
}

void drawDashedLine(CCPoint a, CCPoint b, ccColor4B col, float dash, float gap) {
    auto dx = b.x - a.x;
    auto dy = b.y - a.y;
    auto len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.f) return;
    dx /= len;
    dy /= len;
    float pos = 0.f;
    bool draw = true;
    while (pos < len) {
        float seg = draw ? dash : gap;
        float next = std::min(pos + seg, len);
        if (draw) {
            CCPoint p0{a.x + dx * pos, a.y + dy * pos};
            CCPoint p1{a.x + dx * next, a.y + dy * next};
            drawSolidLine(p0, p1, col, 1.25f);
        }
        pos = next;
        draw = !draw;
    }
}

void drawArrowHead(CCPoint from, CCPoint to, ccColor4B col, float size) {
    auto dx = to.x - from.x;
    auto dy = to.y - from.y;
    auto len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.f) return;
    dx /= len;
    dy /= len;
    CCPoint left{to.x - dx * size + dy * size * 0.55f, to.y - dy * size - dx * size * 0.55f};
    CCPoint right{to.x - dx * size - dy * size * 0.55f, to.y - dy * size + dx * size * 0.55f};
    drawSolidLine(to, left, col, 1.25f);
    drawSolidLine(to, right, col, 1.25f);
}

std::vector<CCPoint> clusterPoints(std::vector<CCPoint> const& pts, float radius) {
    std::vector<CCPoint> out;
    std::vector<bool> used(pts.size(), false);
    float r2 = radius * radius;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (used[i]) continue;
        float sx = pts[i].x, sy = pts[i].y;
        int n = 1;
        used[i] = true;
        for (size_t j = i + 1; j < pts.size(); ++j) {
            if (used[j]) continue;
            float ddx = pts[j].x - pts[i].x;
            float ddy = pts[j].y - pts[i].y;
            if (ddx * ddx + ddy * ddy <= r2) {
                used[j] = true;
                sx += pts[j].x;
                sy += pts[j].y;
                ++n;
            }
        }
        out.push_back({sx / n, sy / n});
    }
    return out;
}

} // namespace paimon::editor::triggers
