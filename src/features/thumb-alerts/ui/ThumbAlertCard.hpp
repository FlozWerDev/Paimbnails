#pragma once

#include "../ThumbAlerts.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <utility>
#include <vector>

namespace paimon::thumbalerts {

// Level data drawn over the level's own thumbnail. One tick drives position,
// scale, rotation and opacity together, so the clipped thumbnail, the scrims
// and the labels never drift apart mid-animation.
class ThumbAlertCard : public cocos2d::CCNodeRGBA {
public:
    static ThumbAlertCard* create(NewThumb const& item, Config const& config,
                                  cocos2d::CCTexture2D* thumbnail);

    // Rest position and scale, before the entry animation offsets it.
    void placeAt(cocos2d::CCPoint rest, float scale);

    void setOnFinished(std::function<void()> callback) { m_onFinished = std::move(callback); }

    void onEnter() override;

private:
    enum class Phase { In, Hold, Out };

    struct Pose {
        cocos2d::CCPoint offset{0.f, 0.f};
        float scaleX = 1.f;
        float scaleY = 1.f;
        float rotation = 0.f;
        float alpha = 1.f;
    };

    bool init(NewThumb const& item, Config const& config, cocos2d::CCTexture2D* thumbnail);

    void buildBackground(cocos2d::CCTexture2D* thumbnail);
    void buildContent();
    void buildTouch();
    void captureFade();

    void tick(float dt);
    void applyIdle(Pose& pose) const;
    void applyAlpha(float alpha);
    void updateThumbMotion(float alpha);
    void toPhase(Phase phase, float duration);
    void finish();
    void onOpenLevel(cocos2d::CCObject*);

    NewThumb m_item;
    Config m_config;
    cocos2d::ccColor3B m_accent{255, 214, 122};
    cocos2d::CCPoint m_rest{0.f, 0.f};
    cocos2d::CCPoint m_edge{0.f, 0.f};
    float m_baseScale = 1.f;

    float m_elapsed = 0.f;
    float m_phaseTime = 0.f;
    float m_phaseDuration = 0.f;
    Phase m_phase = Phase::In;
    bool m_finished = false;
    bool m_priorityQueued = false;

    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCSprite* m_thumb = nullptr;
    cocos2d::CCSprite* m_shine = nullptr;
    cocos2d::CCLayerColor* m_bar = nullptr;
    float m_thumbScale = 1.f;
    cocos2d::CCPoint m_thumbHome{0.f, 0.f};

    // CCLayerGradient ignores setOpacity: its alpha lives in the start/end
    // opacity pair, so the scrims fade through their own list.
    struct GradientFade {
        geode::Ref<cocos2d::CCLayerGradient> node;
        GLubyte start = 0;
        GLubyte end = 0;
    };

    std::vector<std::pair<geode::Ref<cocos2d::CCNode>, GLubyte>> m_fade;
    std::vector<GradientFade> m_fadeGradients;
    std::function<void()> m_onFinished;
};

} // namespace paimon::thumbalerts
