#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include "../data/ProgressionBadges.hpp"

namespace paimon::progression {

class BadgeDetailPopup : public PaimonPopup {
public:
    static BadgeDetailPopup* create(BadgeDef const& badge, BadgeContext const& ctx);

protected:
    bool init(BadgeDef const& badge, BadgeContext const& ctx);
};

} // namespace paimon::progression
