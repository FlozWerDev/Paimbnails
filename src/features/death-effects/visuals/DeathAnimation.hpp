#pragma once
#include <Geode/Geode.hpp>
#include <array>

namespace paimon::death_effects {
inline constexpr std::array<char const*, 12> kAnimationNames = {
    "Supernova", "Vortex", "Prism", "Shockwave", "Embers", "Frost",
    "Glitch", "Solar Flare", "Lotus", "Atom", "Rift", "Stardust"
};
// -1: original; 12: random. Invalid saved values fall back to original.
int selectedAnimation();
int resolveAnimation(int selection);
void prewarmAnimation();
bool spawnAnimation(cocos2d::CCNode* parent, cocos2d::CCPoint position, int style,
                    cocos2d::ccColor3B color, float scale = 1.f);
void clearAnimations(cocos2d::CCNode* parent);

class DeathAnimationPopup : public geode::Popup {
public:
    static DeathAnimationPopup* create();
protected:
    bool init() override;
private:
    void onSelect(cocos2d::CCObject* sender);
    void onPreview(cocos2d::CCObject*);
    void refresh();
    cocos2d::CCNode* m_preview = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_previewHint = nullptr;
    std::array<CCMenuItemSpriteExtra*, 14> m_choices{};
};
}
