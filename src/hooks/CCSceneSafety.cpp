#include <Geode/Geode.hpp>
#include <Geode/modify/AchievementNotifier.hpp>
#include <Geode/modify/CCScene.hpp>

using namespace geode::prelude;

class $modify(PaimonSafeCCScene, CCScene) {
    static void onModify(auto& self) {
        // VeryEarly is the SDK-recommended stand-in for First and still runs
        // before any other mod's getHighestChildZ wrapper.
        (void)self.setHookPriorityPre("cocos2d::CCScene::getHighestChildZ", geode::Priority::VeryEarly);
    }

    int getHighestChildZ() {
        // getChildren() may sort the array before returning it. Inspect the
        // backing storage first: count() itself crashes when data is null.
        auto* children = m_pChildren;
        if (!children || !children->data || !children->data->arr || children->data->num == 0) {
            return 0;
        }
        return CCScene::getHighestChildZ();
    }
};

class $modify(PaimonSafeAchievementNotifier, AchievementNotifier) {
    void showNextAchievement() {
        auto* director = CCDirector::get();
        auto* scene = director->getNextScene();
        if (!scene) scene = director->getRunningScene();
        if (auto* transition = typeinfo_cast<CCTransitionScene*>(scene)) {
            scene = transition->m_pInScene;
        }

        auto* children = scene ? scene->m_pChildren : nullptr;
        if (!scene || (children && (!children->data || !children->data->arr))) {
            // keep the native queue intact until a scene can own the bar.
            // the notifier is not in the scene tree, so schedule it unpaused.
            CCScheduler::get()->scheduleSelector(
                schedule_selector(PaimonSafeAchievementNotifier::retryAchievement), this, .1f, false);
            return;
        }

        CCScheduler::get()->unscheduleSelector(
            schedule_selector(PaimonSafeAchievementNotifier::retryAchievement), this);
        // native m_nextScene is a weak pointer. Never inspect an old scene:
        // resolve the destination from scenes the director still owns instead.
        m_nextScene = scene;
        AchievementNotifier::showNextAchievement();
    }

    void retryAchievement(float) {
        showNextAchievement();
    }
};
