#pragma once

#include <Geode/Geode.hpp>
#include "../../../../utils/PaimonDrawNode.hpp"

namespace paimon::menumusic {

class SpatialStagePreview : public cocos2d::CCNode {
public:
    static SpatialStagePreview* create(float width);

protected:
    bool init(float width);
    void tick(float dt);
    void redraw();

private:
    geode::Ref<PaimonDrawNode> m_draw;
    geode::Ref<cocos2d::CCLabelBMFont> m_statusLabel;
    float m_width = 0.f;
};

} // namespace paimon::menumusic
