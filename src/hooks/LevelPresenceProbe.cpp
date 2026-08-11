// TEMPORARY probe for the Globed "stale level" report. Remove once diagnosed.
//
// Globed (dankmeme.globed2) registers the level you are in from inside
// PlayLayer::init, before the original runs: GlobedGJBGL::setupPreInit()
// computes an `active` flag and only then calls RoomManager::joinLevel(level).
//   active = connected && level->m_levelID != 0 && m_levelType != Editor
// It sends the matching level-leave from its own PlayLayer::onQuit hook, also
// before the original, and only while that same `active` flag holds. There is
// no periodic resync, so a single init or onQuit that never reaches Globed's
// hook leaves its server showing the previous level.
//
// Each probe is installed twice, at both ends of the hook chain. If the
// [early] line shows up without its [late] counterpart, some mod in between
// returned without calling the original and the call never reached Globed.

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>

using namespace geode::prelude;

namespace {

// The three level fields Globed reads to decide whether to announce the level.
void logLevelState(char const* tag, PlayLayer* self, GJGameLevel* level) {
    auto* gm = GameManager::get();
    log::info(
        "[PresenceProbe] {} playLayer={} level={} id={} account={} type={} gm.m_playLayer={}",
        tag,
        static_cast<void*>(self),
        static_cast<void*>(level),
        level ? level->m_levelID.value() : -1,
        level ? level->m_accountID.value() : -1,
        level ? static_cast<int>(level->m_levelType) : -1,
        gm ? static_cast<void*>(gm->m_playLayer) : nullptr
    );
}

} // namespace

class $modify(PaimonPresenceProbeEarly, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("PlayLayer::init", geode::Priority::VeryEarly);
        (void)self.setHookPriorityPre("PlayLayer::onQuit", geode::Priority::VeryEarly);
    }

    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        logLevelState("init [early]", this, level);
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    $override
    void onQuit() {
        logLevelState("onQuit [early]", this, this->m_level);
        PlayLayer::onQuit();
    }
};

class $modify(PaimonPresenceProbeLate, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("PlayLayer::init", geode::Priority::VeryLate);
        (void)self.setHookPriorityPre("PlayLayer::onQuit", geode::Priority::VeryLate);
    }

    $override
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        logLevelState("init [late]", this, level);
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    $override
    void onQuit() {
        logLevelState("onQuit [late]", this, this->m_level);
        PlayLayer::onQuit();
    }
};

// Pause -> Exit is the reported path. GD's PauseLayer::onQuit reaches the level
// through GameManager::m_playLayer, so log what that pointer holds when the
// button is pressed: if it is null or stale, PlayLayer::onQuit runs on the
// wrong instance (or not at all) and Globed never hears about the exit.
class $modify(PaimonPresenceProbePause, PauseLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("PauseLayer::onQuit", geode::Priority::VeryEarly);
    }

    void onQuit(CCObject* sender) {
        auto* gm = GameManager::get();
        log::info(
            "[PresenceProbe] PauseLayer::onQuit [early] gm.m_playLayer={} PlayLayer::get()={}",
            gm ? static_cast<void*>(gm->m_playLayer) : nullptr,
            static_cast<void*>(PlayLayer::get())
        );
        PauseLayer::onQuit(sender);
        log::info("[PresenceProbe] PauseLayer::onQuit returned");
    }
};
