#pragma once

#include <Geode/Geode.hpp>
#include "MenuPhysicsWorld.hpp"
#include <vector>

namespace paimon::menuphysics {

inline constexpr char const* kNodeIDRaw = "paim-menu-physics-node";

class MenuPhysicsNode : public cocos2d::CCLayer {
public:
    static MenuPhysicsNode* create();

    void detach();

protected:
    bool init() override;
    void update(float dt) override;
    void onExit() override;

    void captureHost();
    void updateReturnAnimation(float dt);
    void finishDetach();

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void registerWithTouchDispatcher() override;

private:
    struct Original {
        geode::Ref<cocos2d::CCNode> node;
        cocos2d::CCPoint pos;
        float rotation = 0.f;
        cocos2d::CCPoint returnStartPos;
        float returnStartRotation = 0.f;
    };

    PhysicsWorld m_world;
    std::vector<Original> m_originals;
    bool m_captured = false;
    bool m_detaching = false;
    float m_returnElapsed = 0.f;
    cocos2d::CCPoint m_touchStart {0.f, 0.f};
    bool m_dragMoved = false;
};

} // namespace paimon::menuphysics
