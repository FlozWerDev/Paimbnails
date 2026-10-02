#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../services/LevelHistoryModel.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>

namespace paimon::info {

class LevelHistoryDetailPopup : public PaimonPopup {
public:
    static LevelHistoryDetailPopup* create(HistoryEntry const& entry);

protected:
    bool init(HistoryEntry const& entry);
};

} // namespace paimon::info
