#pragma once

#include <Geode/Geode.hpp>
#include "../services/DynamicTransitionMotion.hpp"
#include <vector>

namespace paimon::transitions::dynamic {

// Composites two opaque captures as rounded, antialiased meshes; no stencil or live scene visits.
class Visual : public cocos2d::CCNode {
public:
    struct Batch {
        cocos2d::CCTexture2D* texture = nullptr;
        GLint first = 0;
        GLsizei count = 0;
    };

    static Visual* create(geode::Ref<cocos2d::CCTexture2D> from, geode::Ref<cocos2d::CCTexture2D> to,
        cocos2d::CCSize size, Config config, Rect origin, bool backwards, bool morphButton,
        bool panel = false);
    static geode::Ref<cocos2d::CCTexture2D> capture(cocos2d::CCNode* node, cocos2d::CCSize size,
        float quality);
    static float pixelsPerPoint();

    void setDestination(geode::Ref<cocos2d::CCTexture2D> to);
    bool hasDestination() const { return m_to.data() != nullptr; }
    void setProgress(float progress);
    void draw() override;

private:
    void rebuild();

    Config m_config;
    Rect m_origin;
    cocos2d::CCSize m_size;
    float m_progress = 0.f;
    float m_pixels = 1.f;
    bool m_backwards = false;
    bool m_panel = false;
    bool m_dirty = true;
    geode::Ref<cocos2d::CCTexture2D> m_from, m_to;
    std::vector<cocos2d::ccV2F_C4B_T2F> m_vertices;
    std::vector<Batch> m_batches;
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
    bool m_firstStep = true;
    void abandonVisual();
    void complete();
};

}
