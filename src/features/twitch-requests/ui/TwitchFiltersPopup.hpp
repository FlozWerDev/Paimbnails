#pragma once
#include "../../../ui/PaimonPopup.hpp"

// popup behind the "filters" button: accepted levels and per-user limits.

#include <Geode/Geode.hpp>

namespace paimon::twitch {

class TwitchFiltersPopup : public PaimonPopup {
public:
    static TwitchFiltersPopup* create();

protected:
    bool init() override;
    void rebuild();
    void onRemoveFiltered();

    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::twitch
