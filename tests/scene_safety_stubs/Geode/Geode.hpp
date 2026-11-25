#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <stdexcept>

#define $modify(Name, Base) Name##Declaration {}; struct Name : public Base
#define schedule_selector(Method) &Method

namespace cocos2d {

struct CCObject {
    virtual ~CCObject() = default;
};

struct ccArray {
    unsigned int num = 0;
    CCObject** arr = nullptr;
};

struct CCArray {
    ccArray* data = nullptr;

    unsigned int count() const {
        if (!data) throw std::runtime_error("CCArray::count dereferenced null data");
        return data->num;
    }
};

struct CCNode : CCObject {
    CCArray* m_pChildren = nullptr;
    int zOrder = 0;
    int getZOrder() const { return zOrder; }
};

struct CCScene : CCNode {
    int nativeCalls = 0;

    CCArray* getChildren() { return m_pChildren; }

    int getHighestChildZ() {
        ++nativeCalls;
        if (!m_pChildren) return 0; // Geode's existing empty-scene fix.
        auto const count = m_pChildren->count();
        if (count && !m_pChildren->data->arr) throw std::runtime_error("missing child storage");
        int highest = 0;
        for (unsigned int i = 0; i < count; ++i) {
            auto* child = static_cast<CCNode*>(m_pChildren->data->arr[i]);
            highest = std::max(highest, child->getZOrder());
        }
        return highest;
    }
};

struct CCTransitionScene : CCScene {
    CCScene* m_pInScene = nullptr;
};

struct CCDirector {
    CCScene* runningScene = nullptr;
    CCScene* nextScene = nullptr;
    static CCDirector* get() {
        static CCDirector director;
        return &director;
    }
    CCScene* getRunningScene() const { return runningScene; }
    CCScene* getNextScene() const { return nextScene; }
};

struct CCScheduler {
    std::function<void(float)> retry;
    int registrations = 0;
    static CCScheduler* get() {
        static CCScheduler scheduler;
        return &scheduler;
    }
    template <class Target>
    void scheduleSelector(void (Target::*selector)(float), Target* target, float, bool paused) {
        assert(!paused); // AchievementNotifier is a singleton outside the scene tree.
        ++registrations;
        retry = [target, selector](float dt) { (target->*selector)(dt); };
    }
    template <class Target>
    void unscheduleSelector(void (Target::*)(float), Target*) { retry = {}; }
    void tick() {
        auto callback = retry;
        if (callback) callback(.1f);
    }
};

}

struct AchievementNotifier : cocos2d::CCNode {
    cocos2d::CCScene* m_nextScene = nullptr;
    cocos2d::CCScene* displayedOn = nullptr;
    unsigned int pending = 0;
    unsigned int displayed = 0;

    void showNextAchievement() {
        if (!pending) return;
        auto* target = m_nextScene ? m_nextScene : cocos2d::CCDirector::get()->getRunningScene();
        if (!target) throw std::runtime_error("achievement has no scene");
        target->getHighestChildZ();
        displayedOn = target;
        --pending;
        ++displayed;
    }
};

namespace geode {
struct Priority {
    static constexpr int First = -3000;
    static constexpr int VeryEarly = -3000;
    static constexpr int Early = -1000;
    static constexpr int Normal = 0;
    static constexpr int Late = 1000;
    static constexpr int VeryLate = 3000;
};
namespace prelude {
using namespace cocos2d;
template <class Target, class Source>
Target typeinfo_cast(Source* source) { return dynamic_cast<Target>(source); }
}
}
