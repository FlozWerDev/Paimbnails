#include "../services/DynamicVolumeManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>

using namespace geode::prelude;
using paimon::dynvol::DynamicVolumeManager;

// a late pre-priority puts us innermost: we only fire once the other playmusic
// hook (levelselectlayer.cpp, late) lets the call through.
class $modify(PaimonDynVolFMOD, FMODAudioEngine) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("FMODAudioEngine::playMusic", geode::Priority::VeryLate);
    }

    $override
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
        if (paimon::isRuntimeShuttingDown()) return;
        DynamicVolumeManager::get().notifySongChanged(static_cast<std::string>(path));
    }
};

namespace {

class DynamicVolumeTickerNode : public CCNode {
public:
    static DynamicVolumeTickerNode* create() {
        auto* ret = new DynamicVolumeTickerNode();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        this->setID("paimon-dynamic-volume-ticker"_spr);
        return true;
    }

    void update(float dt) override {
        if (paimon::isRuntimeShuttingDown()) return;
        DynamicVolumeManager::get().update(dt);
    }
};

Ref<DynamicVolumeTickerNode> s_dynamicVolumeTicker = nullptr;

} // namespace

void initDynamicVolumeTicker() {
    if (s_dynamicVolumeTicker) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;

    s_dynamicVolumeTicker = DynamicVolumeTickerNode::create();
    if (!s_dynamicVolumeTicker) return;

    scheduler->scheduleUpdateForTarget(s_dynamicVolumeTicker.data(), 0, false);
    log::info("[DynamicVolume] Ticker initialized");
}

void shutdownDynamicVolumeTicker() {
    if (!s_dynamicVolumeTicker) return;
    if (auto* director = CCDirector::get()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleUpdateForTarget(s_dynamicVolumeTicker.data());
        }
    }
    DynamicVolumeManager::get().shutdown();
    // leak the ref on purpose: releasing a ccnode during atexit tears down
    // scheduler state that cocos has already destroyed.
    (void)s_dynamicVolumeTicker.take();
}

$on_game(Exiting) {
    shutdownDynamicVolumeTicker();
}
