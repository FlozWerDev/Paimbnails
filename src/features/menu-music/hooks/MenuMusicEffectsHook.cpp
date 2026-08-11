#include "../services/MenuMusicEffects.hpp"

#include <Geode/modify/FMODAudioEngine.hpp>

using namespace geode::prelude;

class $modify(PaimonMenuMusicEffectsFMOD, FMODAudioEngine) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("FMODAudioEngine::playMusic", geode::Priority::VeryLate);
    }

    $override
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
        paimon::menumusic::MenuMusicEffects::get().onMusicStarted(
            static_cast<std::string>(path));
    }

    $override
    void update(float dt) {
        FMODAudioEngine::update(dt);
        paimon::menumusic::MenuMusicEffects::get().tick(dt);
    }
};
