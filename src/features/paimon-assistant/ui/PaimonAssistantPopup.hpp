#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/TextInput.hpp>

#include <array>
#include <vector>

// overlay fullscreen estilo computer-use: Ctrl+J lo alterna, blur en toda
// la pantalla, esquinas con glow estilo Claude/GPT, animacion de fondo por effort,
// label dorado, celda de texto y selector de effort. Sin ventana popup.
namespace paimon::assistant {

class AssistantShaderQuad;

class PaimonAssistantPopup : public cocos2d::CCLayer {
public:
    static void toggle();
    static PaimonAssistantPopup* create();

    void show();
    void close();

    bool init() override;
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;

    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;
    void keyBackClicked() override;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void registerWithTouchDispatcher() override;

protected:
    enum class Effort { Normal = 0, High = 1, Ultra = 2 };

private:
    static PaimonAssistantPopup* s_instance;
    static Effort loadEffort();
    static void saveEffort(Effort effort);

    void buildDim();
    void buildShaderLayers();
    void buildContent();
    void runShowAnimation();
    void doClose();

    void syncShaderUniforms();
    void refreshEffortButtons();
    void setEffort(Effort effort);
    void onAsk(cocos2d::CCObject*);
    void runComputerUse();
    void finishComputerUse(cocos2d::CCNode*);
    char const* effortName() const;

    cocos2d::CCLayerColor* m_dim = nullptr;
    AssistantShaderQuad* m_backdropShader = nullptr;
    AssistantShaderQuad* m_cornerGlowShader = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    geode::NineSlice* m_inputBorder = nullptr;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_effortBtns;
    CCMenuItemSpriteExtra* m_askBtn = nullptr;

    Effort m_effort = Effort::Normal;
    std::array<float, 3> m_effortWeights{1.f, 0.f, 0.f};
    float m_shaderTime = 0.f;
    float m_fade = 0.f;
    float m_workingMix = 0.f;
    bool m_working = false;
    bool m_closing = false;
    bool m_priorityScheduled = false;
};

} // namespace paimon::assistant
