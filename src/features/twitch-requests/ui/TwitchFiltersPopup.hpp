#pragma once

// popup behind the "filters" button: accepted levels and per-user limits.

#include <Geode/Geode.hpp>

namespace paimon::twitch {

class TwitchFiltersPopup : public geode::Popup {
public:
    static TwitchFiltersPopup* create();

protected:
    bool init() override;
    void rebuild();
    void onRemoveFiltered();

    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::twitch
