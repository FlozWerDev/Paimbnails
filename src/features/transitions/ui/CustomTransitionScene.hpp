#pragma once
#include <Geode/Geode.hpp>
#include "../services/TransitionManager.hpp"
#include "../services/TransitionTimeline.hpp"
#include "../services/TransitionMedia.hpp"

// Native scene lifecycle, private render surfaces, no reparenting of GD nodes.
class CustomTransitionScene : public cocos2d::CCTransitionScene {
public:
    static bool isActive();
    static CustomTransitionScene* create(cocos2d::CCScene* from, cocos2d::CCScene* to,
        std::vector<TransitionCommand> const& commands, bool isPush);
    static CustomTransitionScene* createStinger(cocos2d::CCScene* to,
        std::shared_ptr<paimon::transitions::TransitionMedia> media, float duration, float cutPoint);
    void onEnter() override;
    void onExit() override;
    void draw() override;
    void update(float dt) override;
private:
    bool initialize(cocos2d::CCScene* to, std::vector<TransitionCommand> commands);
    bool capture(cocos2d::CCScene* scene, geode::Ref<cocos2d::CCRenderTexture>& surface,
        cocos2d::CCLayerRGBA*& container);
    void apply(std::size_t clip, float progress, float elapsed);
    void complete();
    std::vector<TransitionCommand> m_commands;
    paimon::transitions::Timeline m_timeline;
    std::vector<bool> m_done, m_started;
    std::vector<cocos2d::CCPoint> m_origins;
    std::vector<cocos2d::CCSprite*> m_overlays;
    std::vector<std::shared_ptr<paimon::transitions::TransitionMedia>> m_media;
    geode::Ref<cocos2d::CCRenderTexture> m_fromSurface, m_toSurface;
    cocos2d::CCLayerRGBA* m_from = nullptr;
    cocos2d::CCLayerRGBA* m_to = nullptr;
    cocos2d::CCSprite* m_stingerSprite = nullptr;
    std::shared_ptr<paimon::transitions::TransitionMedia> m_stinger;
    float m_elapsed = 0.f, m_cutPoint = .5f;
    bool m_finished = false, m_captured = false;
};
