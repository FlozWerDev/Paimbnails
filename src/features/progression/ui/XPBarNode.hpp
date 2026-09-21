#pragma once

#include <Geode/Geode.hpp>
#include "../data/ProgressionTiers.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace paimon::progression {

class GDProgressBar;

// Level progress bar over the game's own bar art; a gain crossing a level
// boundary splits into one segment per level so the fill never snaps back.
class XPBarNode : public cocos2d::CCNode {
public:
    static XPBarNode* create(float width, float height);

    void setTier(Tier const& tier);
    void setExp(int64_t exp);
    void animateTo(int64_t exp, float duration);
    // Fired the moment the fill crosses into the next level.
    void setLevelUpCallback(std::function<void(int)> callback);

    void update(float dt) override;

protected:
    struct Segment {
        int64_t from = 0;
        int64_t to = 0;
        float duration = 0.f;
    };

    bool init(float width, float height);
    void applyExp(int64_t exp, int level);
    void flashLevelUp();

    float m_width = 100.f;
    float m_height = 12.f;
    int64_t m_exp = 0;
    std::vector<Segment> m_queue;
    size_t m_segment = 0;
    float m_segmentTime = 0.f;
    GDProgressBar* m_bar = nullptr;
    cocos2d::CCLabelBMFont* m_label = nullptr;
    std::function<void(int)> m_onLevelUp;
};

} // namespace paimon::progression
