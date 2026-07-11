#pragma once

// Trigger indicator helpers (BetterEdit-inspired).

#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <cocos2d.h>
#include <vector>

namespace paimon::editor::triggers {

struct TriggerTargets {
    int targetGroup = 0;
    int centerGroup = 0;
    bool hasCenter = false;
};

// Stable palette by object ID (move/rotate/scale/pulse/...).
cocos2d::ccColor4B colorForObjectID(int objectID);

// Read target (+ optional center) group from an effect object.
TriggerTargets groupsOf(EffectGameObject* trig);

// Objects currently in a group (prefers m_groupDict).
std::vector<GameObject*> objectsInGroup(LevelEditorLayer* lel, int group);

// Draw helpers (immediate mode — call inside DrawGridLayer::draw).
void drawSolidLine(cocos2d::CCPoint a, cocos2d::CCPoint b, cocos2d::ccColor4B col, float width = 1.5f);
void drawDashedLine(cocos2d::CCPoint a, cocos2d::CCPoint b, cocos2d::ccColor4B col, float dash = 6.f, float gap = 4.f);
void drawArrowHead(cocos2d::CCPoint from, cocos2d::CCPoint to, cocos2d::ccColor4B col, float size = 6.f);

// Cluster nearby targets and return representative points (max cluster radius).
std::vector<cocos2d::CCPoint> clusterPoints(
    std::vector<cocos2d::CCPoint> const& pts, float radius = 40.f
);

} // namespace paimon::editor::triggers
