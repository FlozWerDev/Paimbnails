#pragma once

#include <Geode/Geode.hpp>

namespace paimon::transitions {

class LevelEntryConfigPopup : public geode::Popup {
public:
    static LevelEntryConfigPopup* create();

protected:
    bool init() override;
    void rebuild();

    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::transitions
