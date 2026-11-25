#pragma once
#include <Geode/Geode.hpp>
#include "../services/TransitionManager.hpp"
#include "../services/TransitionEvaluation.hpp"
#include "../services/TransitionMedia.hpp"

// native scene lifecycle, private render surfaces, no reparenting of gd nodes.
class CustomTransitionScene : public cocos2d::CCTransitionScene {
public:
    static bool isActive();
    static CustomTransitionScene* create(cocos2d::CCScene* from, cocos2d::CCScene* to,
        std::vector<TransitionCommand> const& commands, bool isPush, float cutPoint = -1.f, float minimumDuration = 0.f);
    static CustomTransitionScene* createStinger(cocos2d::CCScene* to,
        std::shared_ptr<paimon::transitions::TransitionMedia> media, float duration, float cutPoint);
    void onEnter() override;
    void onExit() override;
    void draw() override;
    void update(float dt) override;
private:
    bool initialize(cocos2d::CCScene* to, std::vector<TransitionCommand> commands, float minimumDuration);
    bool capture(cocos2d::CCScene* scene, geode::Ref<cocos2d::CCRenderTexture>& surface,
        cocos2d::CCLayerRGBA*& container);
    void applyFrame();
    void complete();
    cocos2d::CCSprite* objectSprite(int commandIndex);
    std::vector<TransitionCommand> m_commands;
    paimon::transitions::Timeline m_timeline;
    paimon::transitions::TransitionEvaluator m_evaluator;
    std::vector<int> m_clipOfCommand;
    std::vector<float> m_objBase;
    std::vector<cocos2d::CCSprite*> m_overlays;
    cocos2d::CCLayerColor* m_wash = nullptr;
    std::vector<std::shared_ptr<paimon::transitions::TransitionMedia>> m_media;
    geode::Ref<cocos2d::CCRenderTexture> m_fromSurface, m_toSurface;
    cocos2d::CCLayerRGBA* m_from = nullptr;
    cocos2d::CCLayerRGBA* m_to = nullptr;
    cocos2d::CCSprite* m_stingerSprite = nullptr;
    std::shared_ptr<paimon::transitions::TransitionMedia> m_stinger;
    float m_elapsed = 0.f, m_cutPoint = .5f, m_sceneCut = -1.f;
    bool m_finished = false, m_captured = false;
};
