#pragma once

#include "../data/VersusRanks.hpp"

#include <Geode/Geode.hpp>

namespace paimon::versus {

class VersusRankBadgeNode : public cocos2d::CCNode {
public:
    static VersusRankBadgeNode* create(RankInfo const& rank, float size);

    void setRank(RankInfo const& rank);
    void setShowPips(bool show);
    // greys the badge out for a player who has never duelled.
    void setDim(bool dim);
    void playPromotion();

    RankInfo const& rank() const { return m_rank; }

protected:
    bool init(RankInfo const& rank, float size);
    void rebuild();

    RankInfo m_rank;
    float m_size = 48.f;
    bool m_showPips = true;
    bool m_dim = false;
    cocos2d::CCNode* m_content = nullptr;
};

} // namespace paimon::versus
