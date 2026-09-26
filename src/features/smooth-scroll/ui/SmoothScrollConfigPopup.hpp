#pragma once
#include <Geode/Geode.hpp>

namespace paimon::smoothscroll {

// Smooth-scroll config on PaiConfigKit: cards per section, always-visible values.
class SmoothScrollConfigPopup : public geode::Popup {
public:
    static SmoothScrollConfigPopup* create();

protected:
    bool init() override;

    // Rebuilds the scrollable content (after a reset, e.g.).
    void rebuild();
    // Deferred rebuild next tick (tab change).
    void scheduleRebuild();

    geode::ScrollLayer* m_scroll = nullptr;
    int m_tab = 0; // 0 = Basic (menus), 1 = Advanced (editor)
};

} // namespace paimon::smoothscroll
