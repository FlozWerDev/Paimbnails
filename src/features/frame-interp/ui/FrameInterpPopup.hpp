#pragma once

#include <Geode/Geode.hpp>

namespace paimon::frameinterp {

// Popup de la interpolacion, sobre PaiConfigKit. Los controles escriben en la
// config viva; el volcado a disco va aparte para no reescribir en cada arrastre.
class FrameInterpPopup : public geode::Popup {
public:
    static FrameInterpPopup* create();

protected:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;

    void rebuild();
    void scheduleRebuild();
    void touched();
    void flush(float dt);
    void refreshStats(float dt);

private:
    bool m_dirty = false;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_statsLabel = nullptr;
};

} // namespace paimon::frameinterp
