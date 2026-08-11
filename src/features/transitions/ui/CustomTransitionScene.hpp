#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <unordered_map>
#include "../services/TransitionManager.hpp"

// Scene that executes a scripted custom transition,
// keeping both source and destination layers alive simultaneously.
struct NodeState {
    cocos2d::CCPoint position;
    float scale = 1.f;
    float rotation = 0.f;
    GLubyte opacity = 255;
    int zOrder = 0;
    bool visible = true;
};

class CustomTransitionScene : public cocos2d::CCScene {
public:
    static bool isActive();

    static CustomTransitionScene* create(
        cocos2d::CCScene* fromScene,
        cocos2d::CCScene* destScene,
        std::vector<TransitionCommand> const& commands,
        bool isPush);

    bool initWithScenes(
        cocos2d::CCScene* fromScene,
        cocos2d::CCScene* destScene,
        std::vector<TransitionCommand> const& commands,
        bool isPush);

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    ~CustomTransitionScene();

private:
    void triggerSafeFallback(char const* where, char const* reason = nullptr);
    void beginCommand(TransitionCommand const& cmd);
    bool beginCommandSafe(TransitionCommand const& cmd);
    void updateCommand(TransitionCommand const& cmd, float progress);
    bool updateCommandSafe(TransitionCommand const& cmd, float progress);
    void finishCurrentCommand();
    void finishTransition();
    void onTransitionFinished(float dt);
    void restoreSceneChildren(cocos2d::CCLayerColor* container, cocos2d::CCScene* scene);
    void restoreTouchDispatch();
    cocos2d::CCLayerColor* getTarget(std::string const& targetName);

    cocos2d::CCLayerColor* m_fromContainer = nullptr;
    cocos2d::CCLayerColor* m_toContainer = nullptr;
    geode::Ref<cocos2d::CCScene> m_fromScene;
    geode::Ref<cocos2d::CCScene> m_destScene;

    std::vector<TransitionCommand> m_commands;
    int m_currentCommandIdx = 0;
    float m_commandElapsed = 0.f;
    float m_globalElapsed = 0.f;
    float m_totalDuration = 0.f;
    bool m_isPush = false;
    bool m_finished = false;
    bool m_started = false;
    bool m_touchDispatchDisabled = false;

    std::unordered_map<cocos2d::CCNode*, NodeState> m_originalStates;
};
