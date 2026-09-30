#pragma once

#include "VersusRankBadgeNode.hpp"

#include <Geode/Geode.hpp>

namespace paimon::versus {

// where the season stands: the number, what is left of it, what it does to your
// elo when it closes, and which mutators the queue is running this week.
class VersusSeasonPopup : public geode::Popup {
public:
    static VersusSeasonPopup* create();

protected:
    bool init() override;
    void buildSeason();
    void buildMutators();
};

} // namespace paimon::versus
