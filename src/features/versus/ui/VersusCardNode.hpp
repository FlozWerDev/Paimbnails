#pragma once

#include "../data/VersusCards.hpp"

#include <Geode/Geode.hpp>

namespace paimon::versus {

class VersusCardNode : public cocos2d::CCNode {
public:
    static VersusCardNode* create(CardId id, float width);

    void setCard(CardId id);
    void playDraw(float delay);

    CardId card() const { return m_card; }

protected:
    bool init(CardId id, float width, bool faceDown);
    void rebuild();

    CardId m_card = CardId::Fog;
    float m_width = 60.f;
    bool m_faceDown = false;
    cocos2d::CCNode* m_content = nullptr;
};

} // namespace paimon::versus
