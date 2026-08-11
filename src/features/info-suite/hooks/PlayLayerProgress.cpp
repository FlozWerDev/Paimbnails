// Feeds ProgressTracker with gameplay deaths, attempts, jumps, completions, and
// play time while keeping practice runs separate.

#include "../InfoModule.hpp"
#include "../services/ProgressTracker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <chrono>

using namespace geode::prelude;

namespace {

bool progressEnabled() {
    return paimon::info::moduleEnabled("info-mod-progress");
}

}

class $modify(PaimonInfoSuitePlayLayer, PlayLayer) {
    struct Fields {
        std::chrono::steady_clock::time_point m_enteredAt{};
        bool m_timing = false;
        bool m_attemptOpen = false;
        bool m_everOpened = false;
        int m_runJumps = 0;
    };

    int trackedLevelID() {
        // Unsaved editor tests have no stable ID to track.
        return m_level ? m_level->m_levelID.value() : 0;
    }

    void addJump() {
        int levelID = trackedLevelID();
        if (levelID <= 0) return;

        // Recover if resetLevel was skipped, but ignore inputs during death.
        if (!m_fields->m_attemptOpen) {
            if (m_fields->m_everOpened) return;
            beginAttempt();
        }

        m_fields->m_runJumps++;
        paimon::info::ProgressTracker::get().recordJump(levelID, m_isPracticeMode);
    }

    void beginAttempt() {
        if (m_fields->m_attemptOpen) return;
        m_fields->m_attemptOpen = true;
        m_fields->m_everOpened = true;
        m_fields->m_runJumps = 0;

        int levelID = trackedLevelID();
        if (levelID > 0) {
            paimon::info::ProgressTracker::get().recordAttempt(levelID, m_isPracticeMode);
        }
    }

    // The flag prevents repeated destroyPlayer calls from ending one death twice.
    void endAttempt(int percent, bool practice) {
        if (!m_fields->m_attemptOpen) return;
        m_fields->m_attemptOpen = false;

        int levelID = trackedLevelID();
        if (levelID > 0) {
            // Keep zero-jump attempts so the chart matches the attempt count.
            paimon::info::ProgressTracker::get().recordRun(
                levelID, m_fields->m_runJumps, percent, practice);
        }
        m_fields->m_runJumps = 0;
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        if (progressEnabled()) {
            m_fields->m_enteredAt = std::chrono::steady_clock::now();
            m_fields->m_timing = true;
        }
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // The anticheat spike calls destroyPlayer without a real death.
        bool realDeath = progressEnabled() && object != this->m_anticheatSpike
            && m_fields->m_attemptOpen;

        // Read percent before the base call resets the player.
        int percent = realDeath ? this->getCurrentPercentInt() : 0;
        int levelID = realDeath ? trackedLevelID() : 0;
        bool practice = m_isPracticeMode;

        PlayLayer::destroyPlayer(player, object);

        if (!realDeath || levelID <= 0) return;
        paimon::info::ProgressTracker::get().recordDeath(levelID, percent, practice);
        endAttempt(percent, practice);
    }

    void resetLevel() {
        if (progressEnabled()) {
            int levelID = trackedLevelID();
            if (levelID > 0) {
                paimon::info::ProgressTracker::get().recordBest(
                    levelID, this->getCurrentPercentInt(), m_isPracticeMode);
            }
            // A restart without death closes the current run.
            endAttempt(this->getCurrentPercentInt(), m_isPracticeMode);
            beginAttempt();
        }
        PlayLayer::resetLevel();
    }

    void levelComplete() {
        if (progressEnabled()) {
            int levelID = trackedLevelID();
            if (levelID > 0) {
                paimon::info::ProgressTracker::get().recordCompletion(levelID, m_isPracticeMode);
            }
            endAttempt(100, m_isPracticeMode);
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        if (progressEnabled() && m_fields->m_timing) {
            m_fields->m_timing = false;
            int levelID = trackedLevelID();
            if (levelID > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - m_fields->m_enteredAt).count();
                paimon::info::ProgressTracker::get().recordPlayTime(levelID, elapsed);
                paimon::info::ProgressTracker::get().recordBest(
                    levelID, this->getCurrentPercentInt(), m_isPracticeMode);
            }
            endAttempt(this->getCurrentPercentInt(), m_isPracticeMode);
            // Flush on exit instead of writing after every death.
            paimon::info::ProgressTracker::get().save();
        }
        PlayLayer::onQuit();
    }
};

// Count jumps at the input hook; GJBaseGameLayer also handles editor playtests.
class $modify(PaimonInfoSuiteJumps, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (!down || button != 1 || !progressEnabled()) return;

        auto* play = PlayLayer::get();
        if (!play || static_cast<GJBaseGameLayer*>(play) != this) return;
        if (play->m_isPaused || play->m_hasCompletedLevel) return;

        static_cast<PaimonInfoSuitePlayLayer*>(play)->addJump();
    }
};
