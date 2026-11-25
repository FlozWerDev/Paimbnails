#include "../src/hooks/CCSceneSafety.cpp"

#include <iostream>
#include <memory>

using namespace cocos2d;

namespace {

void staleAchievementSceneIsReplacedWithoutDereferencingIt() {
    CCScene current;
    CCDirector::get()->runningScene = &current;
    CCDirector::get()->nextScene = nullptr;
    PaimonSafeAchievementNotifier notifier;
    auto expired = std::make_unique<CCScene>();
    notifier.m_nextScene = expired.get();
    expired.reset();
    notifier.pending = 2;
    notifier.showNextAchievement();
    assert(notifier.displayedOn == &current);
    assert(notifier.m_nextScene == &current);
    assert(notifier.pending == 1 && notifier.displayed == 1);
    notifier.showNextAchievement();
    assert(notifier.pending == 0 && notifier.displayed == 2);
}

void achievementsUseTheDestinationDuringTransitions() {
    CCScene current, destination;
    CCTransitionScene transition;
    transition.m_pInScene = &destination;
    auto* director = CCDirector::get();
    director->runningScene = &current;
    director->nextScene = &transition;
    PaimonSafeAchievementNotifier notifier;
    notifier.pending = 1;
    notifier.showNextAchievement();
    assert(notifier.displayedOn == &destination);

    director->runningScene = &transition;
    director->nextScene = nullptr;
    notifier.pending = 1;
    notifier.showNextAchievement();
    assert(notifier.displayedOn == &destination);

    director->runningScene = &current;
    director->nextScene = &destination;
    notifier.pending = 1;
    notifier.showNextAchievement();
    assert(notifier.displayedOn == &destination);
}

void missingSceneDefersWithoutLosingOrDuplicatingAchievements() {
    auto* director = CCDirector::get();
    director->runningScene = director->nextScene = nullptr;
    auto* scheduler = CCScheduler::get();
    PaimonSafeAchievementNotifier notifier;
    notifier.pending = 1;
    notifier.showNextAchievement();
    notifier.showNextAchievement();
    scheduler->tick();
    assert(notifier.pending == 1 && notifier.displayed == 0);
    assert(scheduler->retry);

    CCScene current;
    director->runningScene = &current;
    scheduler->tick();
    assert(notifier.pending == 0 && notifier.displayed == 1);
    assert(!scheduler->retry);
    scheduler->tick();
    assert(notifier.displayed == 1);
}

void invalidBackingStorageAndIncompleteTransitionsWaitForAValidScene() {
    CCScene current;
    CCArray children;
    current.m_pChildren = &children;
    auto* director = CCDirector::get();
    director->runningScene = &current;
    director->nextScene = nullptr;
    PaimonSafeAchievementNotifier notifier;
    notifier.pending = 1;
    notifier.showNextAchievement();
    assert(notifier.pending == 1 && notifier.displayed == 0);

    ccArray storage;
    children.data = &storage;
    CCScheduler::get()->tick();
    assert(notifier.pending == 1 && notifier.displayed == 0);

    CCTransitionScene transition;
    director->runningScene = &transition;
    CCScheduler::get()->tick();
    assert(notifier.pending == 1 && notifier.displayed == 0);

    CCScene destination;
    transition.m_pInScene = &destination;
    CCScheduler::get()->tick();
    assert(notifier.displayedOn == &destination && notifier.displayed == 1);
    assert(!CCScheduler::get()->retry);
}

}

int main() try {
    PaimonSafeCCScene scene;
    assert(scene.getHighestChildZ() == 0);

    CCArray children;
    scene.m_pChildren = &children;
    // Crash dump: CCArray exists, but count() reads data == nullptr (RAX == 0).
    assert(scene.getHighestChildZ() == 0);

    ccArray storage;
    children.data = &storage;
    assert(scene.getHighestChildZ() == 0);
    storage.num = 1;
    assert(scene.getHighestChildZ() == 0);

    CCNode low, high, negative;
    low.zOrder = 5;
    high.zOrder = 1000;
    negative.zOrder = -10;
    CCObject* nodes[] = {&low, &negative, &high};
    storage.arr = nodes;
    storage.num = 3;
    assert(scene.getHighestChildZ() == 1000);
    storage.num = 0;
    assert(scene.getHighestChildZ() == 0);
    storage.num = 1;
    storage.arr = nodes + 1;
    assert(scene.getHighestChildZ() == 0); // Native behavior floors Z at zero.

    std::cout << "PASS: scene highest Z handles null backing storage and preserves native Z\n";
    staleAchievementSceneIsReplacedWithoutDereferencingIt();
    achievementsUseTheDestinationDuringTransitions();
    missingSceneDefersWithoutLosingOrDuplicatingAchievements();
    invalidBackingStorageAndIncompleteTransitionsWaitForAValidScene();
    std::cout << "PASS: achievements use live scenes and retry once a destination is available\n";
} catch (std::exception const& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
