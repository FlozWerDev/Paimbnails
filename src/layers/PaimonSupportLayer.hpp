#pragma once

#include <Geode/Geode.hpp>
#include <atomic>

/**
 * PaimonSupportLayer — pantalla completa estilo GD para apoyar el desarrollo de Paimbnails.
 *
 * Layout:
 *   ┌──────────────────────────────────────────────────────────┐
 *   │                  ★ Support Paimbnails ★                  │
 *   │                                                          │
 *   │   ┌─────────────────────┐  ┌──────────────────────────┐ │
 *   │   │   SUPPORTER BADGE   │  │   BENEFITS LIST          │ │
 *   │   │   ★ preview ★       │  │   ✓ Exclusive badge      │ │
 *   │   │                     │  │   ✓ Priority support     │ │
 *   │   │   "Supporter"       │  │   ✓ Early access         │ │
 *   │   │                     │  │   ✓ Credits in mod       │ │
 *   │   └─────────────────────┘  │   ✓ Help development     │ │
 *   │                            └──────────────────────────┘ │
 *   │                                                          │
 *   │   ┌──────────────────────────────────────────────────┐  │
 *   │   │   Thank you message                              │  │
 *   │   └──────────────────────────────────────────────────┘  │
 *   │                                                          │
 *   │            [ ♥ Donate ]     [ Back ]                     │
 *   └──────────────────────────────────────────────────────────┘
 */
class PaimonSupportLayer : public cocos2d::CCLayer {
protected:
    bool init() override;
    void keyBackClicked() override;

    void onBack(cocos2d::CCObject*);
    void onDonate(cocos2d::CCObject*);

    // UI builders
    void createBackground();
    void createTitle();
    void createBadgePanel();
    void createBenefitsPanel();
    void createThankYouSection();
    void createButtons();
    void createParticles();
    void spawnParticles(float dt);

    // fondo dinamico con thumbnails
    void loadShowcaseThumbnails();
    void cycleThumbnail(float dt);
    void applyThumbnailBackground(cocos2d::CCTexture2D* texture);

    cocos2d::CCSprite* m_bgThumb = nullptr;
    cocos2d::CCNode* m_bgDiagonalGlow = nullptr;
    std::vector<std::string> m_cachedThumbPaths;
    int m_currentThumbIndex = 0;
    std::atomic<bool> m_loadingThumb{false};

public:
    static PaimonSupportLayer* create();
    static cocos2d::CCScene* scene();
};
