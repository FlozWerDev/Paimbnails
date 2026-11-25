#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include "../services/TransitionManager.hpp"

// scroll/card layout (PaiConfigKit): no hand-placed panels, so headings,
// toggles and buttons can never overlap.
class TransitionConfigPopup : public PaimonPopup {
protected:
    bool init();
    void rebuild();
    void previewConfig(TransitionConfig const& cfg);

    TransitionConfig m_editingGlobal;
    TransitionConfig m_editingLevel;
    bool m_enabled = true;
    bool m_useLevel = false;

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;

public:
    static TransitionConfigPopup* create();
};
