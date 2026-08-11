#include "DeathHeatmapNode.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::info {

namespace {

// Green → yellow → red, so a glance tells you where the level bites.
ccColor3B heatColor(float t) {
    t = std::clamp(t, 0.f, 1.f);
    if (t < 0.5f) {
        float k = t / 0.5f;
        return {
            static_cast<GLubyte>(90 + k * (255 - 90)),
            static_cast<GLubyte>(210 + k * (215 - 210)),
            static_cast<GLubyte>(120 - k * 60),
        };
    }
    float k = (t - 0.5f) / 0.5f;
    return {
        255,
        static_cast<GLubyte>(215 - k * 160),
        static_cast<GLubyte>(60 - k * 45),
    };
}

} // namespace

DeathHeatmapNode* DeathHeatmapNode::create(LevelProgress const& progress, bool practice,
                                           float width, float height) {
    if (progress.totalDeaths(practice) <= 0) return nullptr;

    auto ret = new DeathHeatmapNode();
    if (ret && ret->init(progress, practice, width, height)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool DeathHeatmapNode::init(LevelProgress const& progress, bool practice,
                            float width, float height) {
    if (!CCNode::init()) return false;

    m_width = width;
    m_height = height;
    this->setContentSize({width, height});
    // A plain CCNode ignores its anchor point unless told otherwise, and callers
    // position this strip by its centre.
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({0.5f, 0.5f});

    if (auto bg = paimon::SpriteHelper::safeCreateScale9("square02_001.png")) {
        bg->setContentSize({width, height});
        bg->setColor({0, 0, 0});
        bg->setOpacity(120);
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        this->addChild(bg, -1);
    }

    m_columns = CCNode::create();
    m_columns->setContentSize({width, height});
    m_columns->setPosition({0.f, 0.f});
    this->addChild(m_columns);

    rebuild(progress, practice);
    return true;
}

void DeathHeatmapNode::setPractice(LevelProgress const& progress, bool practice) {
    rebuild(progress, practice);
}

void DeathHeatmapNode::rebuild(LevelProgress const& progress, bool practice) {
    if (!m_columns) return;
    m_columns->removeAllChildren();

    auto const& buckets = practice ? progress.deathsPractice : progress.deathsNormal;

    uint32_t peak = 0;
    for (auto count : buckets) peak = std::max(peak, count);
    if (peak == 0) return;

    float inset = 2.f;
    float usableW = m_width - inset * 2.f;
    float usableH = m_height - inset * 2.f;
    float columnW = usableW / static_cast<float>(kPercentBuckets);

    for (int i = 0; i < kPercentBuckets; i++) {
        if (buckets[i] == 0) continue;

        // Square root keeps a single brutal percent from flattening everything
        // else into invisibility.
        float ratio = std::sqrt(static_cast<float>(buckets[i]) / static_cast<float>(peak));
        float columnH = std::max(2.f, usableH * ratio);

        auto color = heatColor(ratio);
        auto column = CCLayerColor::create(ccc4(color.r, color.g, color.b, 235));
        column->setContentSize({std::max(1.f, columnW - 0.5f), columnH});
        column->setPosition({inset + i * columnW, inset});
        m_columns->addChild(column);
    }
}

} // namespace paimon::info
