#pragma once
#include <Geode/Geode.hpp>
#include "../services/TransitionManager.hpp"
#include "../services/TransitionEvaluation.hpp"

class CustomTransitionLayer : public cocos2d::CCLayer {
public:
    using cocos2d::CCLayer::init;
    static cocos2d::CCScene* scene(TransitionConfig config, bool isGlobal, std::function<void(TransitionConfig)> save);
    static CustomTransitionLayer* create(TransitionConfig config, bool isGlobal, std::function<void(TransitionConfig)> save);
    bool init(TransitionConfig config, bool isGlobal, std::function<void(TransitionConfig)> save);
    void registerWithTouchDispatcher() override;
    void keyBackClicked() override;
    void update(float dt) override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    static std::string actionDisplayName(CommandAction action);
    static std::vector<CommandAction> const& allActions();
private:
    struct ObjectView {
        int command = -1;
        cocos2d::CCSprite* sprite = nullptr;
        std::shared_ptr<paimon::transitions::TransitionMedia> media;
        float baseScale = 1.f;
    };
    struct TriggerTarget { int object = -1; std::string scene = "from"; };
    enum class Drag { None, Scrub, Cut, Clip, Resize, Object };
    void buildChrome();
    void rebuildPanel();
    void layoutPropertyPanel();
    void queuePanel();
    void refreshDocument();
    void refreshTimeline();
    void refreshObjects();
    void evaluate();
    void selectCommand(int index);
    void addObject(bool solid);
    TriggerTarget newTriggerTarget() const;
    void addTrigger(CommandAction action, TriggerTarget target);
    void importForObject(int index);
    void warmObjects();
    void save();
    void previewReal();
    void exportProject();
    void importProject();
    void preset(TransitionType type);
    float totalDuration() const;
    float timeAt(float x) const;
    float timeX(float seconds) const;
    float trackY(int visibleRow) const;
    std::string commandName(int index) const;
    void status(std::string const& text);

    TransitionConfig m_config;
    std::function<void(TransitionConfig)> m_save;
    std::vector<TransitionCommand> m_commands;
    paimon::transitions::TransitionEvaluator m_evaluator;
    std::vector<ObjectView> m_objects;
    int m_selected = -1, m_tab = 0, m_trackOffset = 0, m_targetObject = -1;
    std::string m_targetScene = "from";
    bool m_importing = false, m_playing = false, m_panelQueued = false;
    bool m_sceneGuides = true;
    float m_playTime = 0.f, m_viewDuration = 1.f;
    Drag m_drag = Drag::None;
    float m_dragX = 0.f, m_dragStart = 0.f, m_dragDuration = 0.f, m_dragRange = 1.f;
    cocos2d::CCPoint m_dragPoint;
    cocos2d::CCSize m_win, m_stageSize;
    float m_mainBottom = 108.f, m_mainTop = 0.f, m_panelWidth = 204.f;
    float m_timelineWidth = 0.f;
    cocos2d::CCNode* m_stage = nullptr;
    cocos2d::CCNode* m_guideLabels = nullptr;
    cocos2d::CCLabelBMFont* m_sceneBadge = nullptr;
    cocos2d::CCLabelBMFont* m_previewLabel = nullptr;
    cocos2d::CCLayerRGBA* m_sceneA = nullptr;
    cocos2d::CCLayerRGBA* m_sceneB = nullptr;
    cocos2d::CCLayerColor* m_wash = nullptr;
    cocos2d::CCDrawNode* m_outline = nullptr;
    cocos2d::CCNode* m_timeline = nullptr;
    cocos2d::CCLayerColor* m_playhead = nullptr;
    cocos2d::CCLabelBMFont* m_clock = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_playLabel = nullptr;
    geode::TextInput* m_name = nullptr;
    geode::ScrollLayer* m_panel = nullptr;
};
