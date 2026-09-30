#pragma once

#include <Geode/Geode.hpp>
#include "../services/DynamicTransitionMotion.hpp"

namespace paimon::transitions::dynamic {

class Visual : public cocos2d::CCNode {
public:
    static Visual* create(cocos2d::CCNode* from, cocos2d::CCNode* to,
        cocos2d::CCSize size, Config config, Rect origin, bool backwards, bool morphButton);
    static geode::Ref<cocos2d::CCRenderTexture> captureSurface(cocos2d::CCNode* node,
        cocos2d::CCSize size, Config config);
    static Visual* createFromSnapshots(geode::Ref<cocos2d::CCRenderTexture> from,
        geode::Ref<cocos2d::CCRenderTexture> to, cocos2d::CCSize size, Config config,
        Rect origin, bool backwards, bool morphButton);
    void setProgress(float progress);

private:
    bool initialize(bool morphButton);

    Config m_config;
    Rect m_origin;
    cocos2d::CCSize m_size;
    bool m_backwards = false;
    geode::Ref<cocos2d::CCRenderTexture> m_fromSurface, m_toSurface;
    cocos2d::CCSprite* m_background = nullptr;
    cocos2d::CCSprite* m_foreground = nullptr;
    cocos2d::CCSprite* m_button = nullptr;
    cocos2d::CCClippingNode* m_clip = nullptr;
    cocos2d::CCDrawNode* m_stencil = nullptr;
    cocos2d::CCDrawNode* m_shadow = nullptr;
};

class DynamicTransitionScene : public cocos2d::CCTransitionScene {
public:
    static DynamicTransitionScene* create(cocos2d::CCScene* destination,
        Config config, Rect origin, bool backwards, bool morphButton);
    void onEnter() override;
    void onExit() override;
    void draw() override;
    void update(float dt) override;

private:
    Config m_config;
    Rect m_origin;
    Visual* m_visual = nullptr;
    cocos2d::CCSize m_size;
    float m_elapsed = 0.f;
    bool m_backwards = false;
    bool m_morphButton = false;
    bool m_finished = false;
    void complete();
};

}
