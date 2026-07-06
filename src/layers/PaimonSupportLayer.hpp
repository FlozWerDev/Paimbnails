#pragma once

#include <Geode/Geode.hpp>
#include <atomic>

// Full-screen GD-style layer for supporting Paimbnails development.
class PaimonSupportLayer : public cocos2d::CCLayer {
protected:
    bool init() override;
    void onExit() override;
    void keyBackClicked() override;

    void onBack(cocos2d::CCObject*);
    void onDonate(cocos2d::CCObject*);

    void createBackground();
    void createTitle();
    void createBadgePanel();
    void createBenefitsPanel();
    void createThankYouSection();
    void createButtons();
    void createParticles();
    void spawnParticles(float dt);

    void loadShowcaseThumbnails();
    void cycleThumbnail(float dt);
    void applyThumbnailBackground(cocos2d::CCTexture2D* texture);

    cocos2d::CCSprite* m_bgThumb = nullptr;
    cocos2d::CCNode* m_bgDiagonalGlow = nullptr;
    cocos2d::CCNode* m_badgePanelContainer = nullptr;
    cocos2d::CCNode* m_benefitsPanelContainer = nullptr;
    std::vector<std::string> m_cachedThumbPaths;
    int m_currentThumbIndex = 0;
    std::atomic<bool> m_loadingThumb{false};
    std::atomic<bool> m_alive{true};

public:
    static PaimonSupportLayer* create();
    static cocos2d::CCScene* scene();
};

