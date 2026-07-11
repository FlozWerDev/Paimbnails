#pragma once

#include <Geode/Geode.hpp>

// Full-screen (or local) loading overlay: spinning GD loading circle with the
// floating Paimon mascot inside, gold status text with animated dots and a
// rotating fun-fact line. Falls back to geode::LoadingSpinner when the GD /
// mod textures are missing (texture packs).
class PaimonLoadingOverlay : public cocos2d::CCLayerColor {
protected:
    cocos2d::CCNode* m_badge = nullptr;          // ring + mascot cluster
    cocos2d::CCSprite* m_ring = nullptr;
    geode::LoadingSpinner* m_spinner = nullptr;  // fallback only
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_funFactLabel = nullptr;

    std::string m_baseText;      // status text without the animated dots
    float m_spinnerSize = 40.f;
    float m_centerX = 0.f;       // layout anchors set in showAt()
    float m_statusY = 0.f;
    int m_dotCount = 0;
    bool m_dismissed = false;

    bool init(std::string const& statusText, float spinnerSize);
    void showAt(cocos2d::CCNode* parent, cocos2d::CCPoint const& position, cocos2d::CCSize const& size, int zOrder);
    void positionStatusLabel();
    void updateDots(float);
    void swapFunFact(float);
    void pickNewFact();
    void startPulse();

public:
    static PaimonLoadingOverlay* create(
        std::string const& statusText = "Loading...",
        float spinnerSize = 40.f
    );

    void show(cocos2d::CCNode* parent, int zOrder = 200);
    void showLocal(cocos2d::CCNode* parent, int zOrder = 200);
    void dismiss();
    void updateText(std::string const& text);

    // Swallow touches over the covered area while loading.
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
};
