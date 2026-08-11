#pragma once

// A strip showing where a level kills you. One column per percent, coloured
// from calm to hot relative to the worst percent on that level, so the shape is
// readable whether you died 20 times or 20000.

#include <Geode/Geode.hpp>
#include "../services/ProgressTracker.hpp"

namespace paimon::info {

class DeathHeatmapNode : public cocos2d::CCNode {
public:
    // Returns nullptr when the level has no deaths recorded yet.
    static DeathHeatmapNode* create(LevelProgress const& progress, bool practice,
                                    float width, float height);

    void setPractice(LevelProgress const& progress, bool practice);

protected:
    bool init(LevelProgress const& progress, bool practice, float width, float height);
    void rebuild(LevelProgress const& progress, bool practice);

    float m_width = 0.f;
    float m_height = 0.f;
    cocos2d::CCNode* m_columns = nullptr;
};

} // namespace paimon::info
