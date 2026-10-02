#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>

namespace paimon::frameinterp {

// interpolation popup on paiconfigkit. controls write the live config;
// disk flush stays separate so drags don't rewrite each frame.
class FrameInterpPopup : public PaimonPopup {
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
