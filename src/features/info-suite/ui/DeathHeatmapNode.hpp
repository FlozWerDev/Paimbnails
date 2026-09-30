#pragma once

#include <Geode/Geode.hpp>
#include "../services/ProgressTracker.hpp"

namespace paimon::info {

class DeathHeatmapNode : public cocos2d::CCNode {
public:
    // returns nullptr when the level has no deaths recorded yet.
    static DeathHeatmapNode* create(LevelProgress const& progress, bool practice,
                                    float width, float height);

protected:
    bool init(LevelProgress const& progress, bool practice, float width, float height);
    void rebuild(LevelProgress const& progress, bool practice);

    float m_width = 0.f;
    float m_height = 0.f;
    cocos2d::CCNode* m_columns = nullptr;
};

} // namespace paimon::info
