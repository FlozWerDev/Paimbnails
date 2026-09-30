#pragma once

#include <Geode/Geode.hpp>
#include "../data/ProgressionBadges.hpp"
#include "../services/ProgressionService.hpp"
#include "XPBarNode.hpp"

namespace paimon::progression {

// slides over the running layer: xp gained plus one card per new badge.
class ProgressionToast : public cocos2d::CCNode {
public:
    // queues the celebration onto the running scene.
    static void present(ProgressDelta const& delta, BadgeContext const& ctx);

protected:
    static ProgressionToast* createProgress(ProgressDelta const& delta);
    static ProgressionToast* createBadge(BadgeDef const& badge, BadgeContext const& ctx);

    bool initProgress(ProgressDelta const& delta);
    bool initBadge(BadgeDef const& badge, BadgeContext const& ctx);

    void buildCard(float width, float height, cocos2d::ccColor3B accent);
    void slideIn(float delay, float hold);
    void onStartFill();

    float m_width = 252.f;
    float m_height = 76.f;
    cocos2d::CCNode* m_card = nullptr;
    geode::Ref<XPBarNode> m_pendingBar = nullptr;
    int64_t m_pendingExp = 0;
};

} // namespace paimon::progression
