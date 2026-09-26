#pragma once

#include <Geode/Geode.hpp>
#include "../data/ProgressionBadges.hpp"

namespace paimon::progression {

// Grid tile, reused larger in the detail popup and level-up overlay.
class BadgeIconNode : public cocos2d::CCNode {
public:
    static BadgeIconNode* create(BadgeDef const& badge, BadgeContext const& ctx, float size);

    void playIntro(float delay);
    void playUnlock();

protected:
    bool init(BadgeDef const& badge, BadgeContext const& ctx, float size);

    BadgeDef const* m_badge = nullptr;
    bool m_unlocked = false;
    float m_size = 46.f;
    cocos2d::CCNode* m_content = nullptr;
};

} // namespace paimon::progression
