#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>

namespace paimon::smoothscroll {

// smooth-scroll config on paiconfigkit: cards per section, always-visible values.
class SmoothScrollConfigPopup : public PaimonPopup {
public:
    static SmoothScrollConfigPopup* create();

protected:
    bool init() override;

    // rebuilds the scrollable content (after a reset, e.g.).
    void rebuild();
    // deferred rebuild next tick (tab change).
    void scheduleRebuild();

    geode::ScrollLayer* m_scroll = nullptr;
    int m_tab = 0; // 0 = basic (menus), 1 = advanced (editor)
};

} // namespace paimon::smoothscroll
