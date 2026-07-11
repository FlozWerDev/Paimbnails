#pragma once

#include <Geode/Geode.hpp>
#include <atomic>

class PaimonSupportLayer : public cocos2d::CCLayer {
protected:
    bool init() override;
    void onExit() override;
    void keyBackClicked() override;

    void onBack(cocos2d::CCObject*);
    void onDonate(cocos2d::CCObject*);

    void buildUI();
    void loadShowcaseThumbnails();
    void cycleThumbnail(float dt);
    void applyThumbnailBackground(cocos2d::CCTexture2D* texture);
    void spawnParticles(float dt);

    cocos2d::CCSprite* m_bgThumb = nullptr;
    std::vector<std::string> m_cachedThumbPaths;
    int m_currentThumbIndex = 0;
    std::atomic<bool> m_loadingThumb{false};
    std::atomic<bool> m_alive{true};

public:
    static PaimonSupportLayer* create();
    static cocos2d::CCScene* scene();
};
