#pragma once
#include <Geode/Geode.hpp>
#include <Geode/cocos/layers_scenes_transitions_nodes/CCLayer.h>
#include <Geode/cocos/actions/CCActionInterval.h>
#include <Geode/cocos/actions/CCActionInstant.h>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace paimon::profilebg {

class AnimatedGradientLayer : public cocos2d::CCLayerGradient {
public:
    static AnimatedGradientLayer* create(
        cocos2d::ccColor3B a,
        cocos2d::ccColor3B b
    ) {
        auto* node = new AnimatedGradientLayer();
        if (node && node->initWithColor(
                cocos2d::ccc4(a.r, a.g, a.b, 255),
                cocos2d::ccc4(b.r, b.g, b.b, 255)
            )) {
            node->m_baseA = a;
            node->m_baseB = b;
            node->setStartColor(a);
            node->setEndColor(b);
            node->setVector({1.f, 0.f});
            node->autorelease();
            return node;
        }
        delete node;
        return nullptr;
    }

    cocos2d::ccColor3B baseColorA() const { return m_baseA; }
    cocos2d::ccColor3B baseColorB() const { return m_baseB; }

    void setEffect(std::string const& effect, float speed) {
        m_effect  = effect;
        m_speed   = std::clamp(speed, 0.1f, 5.0f);
        m_time    = 0.f;

        this->stopAllActions();
        this->setRotation(0.f);
        this->setScale(1.f);
        this->setStartColor(m_baseA);
        this->setEndColor(m_baseB);
        this->setVector({1.f, 0.f});

        if (m_effect == "none") {
            this->unscheduleUpdate();
            return;
        }

        auto sz = this->getContentSize();
        if (m_effect == "rotate") {
            float oversize = std::sqrt(sz.width * sz.width + sz.height * sz.height);
            float scale = oversize / std::max(1.f, std::min(sz.width, sz.height));
            this->setScale(std::max(scale, 1.5f));

            float duration = std::max(0.5f, 8.0f / m_speed);
            this->runAction(cocos2d::CCRepeatForever::create(
                cocos2d::CCRotateBy::create(duration, 360.f)
            ));
            this->unscheduleUpdate();
        }
        else if (m_effect == "pulse") {
            float duration = std::max(0.2f, 1.2f / m_speed);
            auto seq = cocos2d::CCSequence::create(
                cocos2d::CCEaseInOut::create(cocos2d::CCScaleTo::create(duration, 1.08f), 2.f),
                cocos2d::CCEaseInOut::create(cocos2d::CCScaleTo::create(duration, 1.0f),  2.f),
                nullptr
            );
            this->runAction(cocos2d::CCRepeatForever::create(seq));
            this->unscheduleUpdate();
        }
        else if (m_effect == "slide") {
            this->setScaleX(1.6f);
            float distance = sz.width * 0.25f;
            float duration = std::max(0.3f, 2.0f / m_speed);
            auto seq = cocos2d::CCSequence::create(
                cocos2d::CCEaseInOut::create(
                    cocos2d::CCMoveBy::create(duration, ccp( distance, 0)), 2.f),
                cocos2d::CCEaseInOut::create(
                    cocos2d::CCMoveBy::create(duration, ccp(-distance * 2.f, 0)), 2.f),
                cocos2d::CCEaseInOut::create(
                    cocos2d::CCMoveBy::create(duration, ccp( distance, 0)), 2.f),
                nullptr
            );
            this->runAction(cocos2d::CCRepeatForever::create(seq));
            this->unscheduleUpdate();
        }
        else if (m_effect == "shift") {
            this->scheduleUpdate();
        }
        else {
            this->unscheduleUpdate();
        }
    }

    std::string const& effect() const { return m_effect; }
    float speed() const { return m_speed; }

    virtual void update(float dt) override {
        cocos2d::CCLayerGradient::update(dt);
        if (m_effect != "shift") return;

        float halfPeriod = std::max(0.3f, 1.5f / m_speed);
        m_time += dt;
        float local = std::fmod(m_time, halfPeriod * 2.f);

        float t;
        cocos2d::ccColor3B from, to;
        if (local < halfPeriod) {
            t = local / halfPeriod;
            from = m_baseA;
            to   = m_baseB;
        } else {
            t = (local - halfPeriod) / halfPeriod;
            from = m_baseB;
            to   = m_baseA;
        }

        auto lerp = [](GLubyte a, GLubyte b, float k) -> GLubyte {
            float v = (float)a + ((float)b - (float)a) * k;
            v = std::clamp(v, 0.f, 255.f);
            return (GLubyte)v;
        };

        cocos2d::ccColor3B startCol = {
            lerp(from.r, to.r, t),
            lerp(from.g, to.g, t),
            lerp(from.b, to.b, t)
        };
        cocos2d::ccColor3B endCol = {
            lerp(to.r, from.r, t),
            lerp(to.g, from.g, t),
            lerp(to.b, from.b, t)
        };

        this->setStartColor(startCol);
        this->setEndColor(endCol);
    }

protected:
    cocos2d::ccColor3B m_baseA{255,255,255};
    cocos2d::ccColor3B m_baseB{255,255,255};
    std::string m_effect = "none";
    float       m_speed  = 1.0f;
    float       m_time   = 0.0f;
};

inline std::vector<std::string> const& availableEffects() {
    static std::vector<std::string> const list = {
        "none", "rotate", "pulse", "shift", "slide"
    };
    return list;
}

inline bool isValidEffect(std::string const& effect) {
    auto const& list = availableEffects();
    return std::find(list.begin(), list.end(), effect) != list.end();
}

inline std::string normalizeEffect(std::string const& effect) {
    return isValidEffect(effect) ? effect : std::string("none");
}

inline float normalizeSpeed(float speed) {
    if (!std::isfinite(speed)) return 1.0f;
    return std::clamp(speed, 0.1f, 5.0f);
}

}
