#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>

namespace paimon::community {

// paimon base community: where the player saves (cloud copy of the settings)
// and suggests (presets, gradients, icons, comment backgrounds). what gets
// approved is browsed in paimon shop.
class CommunityBasePopup : public PaimonPopup {
public:
    static CommunityBasePopup* create();
    static void open();

protected:
    bool init() override;
    void update(float dt) override;

private:
    cocos2d::CCLabelBMFont* m_account = nullptr;
    cocos2d::CCLabelBMFont* m_cloud = nullptr;
    CCMenuItemSpriteExtra* m_verify = nullptr;
    CCMenuItemSpriteExtra* m_restoreLocal = nullptr;
    CCMenuItemSpriteExtra* m_moderation = nullptr;
    bool m_busy = false;
    bool m_hadProof = false;
    int m_token = 0;

    void refreshAccount();
    void refreshCloud();
    void saveCloud();
    void loadCloud();
    void deleteCloud();
    void restoreLocal();
    void offerRestart();
};

} // namespace paimon::community
