// Extended Level Info is merged into the level screen's own info button: no
// extra button is added, tapping the vanilla one opens the compact stats popup
// instead of the small "Level Stats" alert. Turning the module off restores the
// original.
//
// The method is onLevelInfo, not onInfo — onInfo is the comments button, which
// pushes InfoLayer (see hooks/InfoLayerLevelInfo.cpp for that side).

#include "../InfoModule.hpp"
#include "../services/ProgressTracker.hpp"
#include "../ui/DeathHeatmapNode.hpp"
#include "../ui/LevelStatsPopup.hpp"
#include "../ui/UnregisteredProfilePopup.hpp"
#include "../../../framework/HookConventions.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

namespace {

bool extendedInfoEnabled() {
    return paimon::info::moduleEnabled("info-mod-extended");
}

bool unregProfilesEnabled() {
    return paimon::info::moduleEnabled("info-mod-unreg-profiles");
}

bool heatmapEnabled() {
    return paimon::info::subEnabled("info-mod-progress", "info-mod-death-heatmap", true);
}

} // namespace

class $modify(PaimonInfoSuiteLevelInfo, LevelInfoLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "LevelInfoLayer::init");
    }

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        addHeatmapStrip();
        return true;
    }

    // The vanilla handler shows the small "Level Stats" alert: attempts, jumps
    // and the two percentages. The popup shows the same numbers split by mode,
    // plus where you die and how many jumps each attempt took.
    void onLevelInfo(CCObject* sender) {
        if (!extendedInfoEnabled() || !m_level) {
            LevelInfoLayer::onLevelInfo(sender);
            return;
        }

        auto popup = paimon::info::LevelStatsPopup::create(m_level);
        if (!popup) {
            LevelInfoLayer::onLevelInfo(sender);
            return;
        }
        popup->show();
    }

    // The creator of a level can be a green player too, and tapping their name
    // does nothing in vanilla when there is no account behind it.
    void onViewProfile(CCObject* sender) {
        if (unregProfilesEnabled() && m_level
            && m_level->m_accountID.value() <= 0 && m_level->m_userID.value() > 0) {
            auto popup = paimon::info::UnregisteredProfilePopup::create(
                m_level->m_userID.value(), std::string(m_level->m_creatorName));
            if (popup) {
                popup->show();
                return;
            }
        }
        LevelInfoLayer::onViewProfile(sender);
    }

    // A compact death strip under the level's own progress bar. If node-ids is
    // not around to name that bar we skip it rather than guess a position and
    // land on top of something else; the popup still has the full heatmap.
    void addHeatmapStrip() {
        if (!heatmapEnabled() || !m_level) return;
        if (this->getChildByID("info-suite-heatmap"_spr)) return;

        auto const* progress =
            paimon::info::ProgressTracker::get().find(m_level->m_levelID.value());
        if (!progress || progress->totalDeaths(false) <= 0) return;

        auto* bar = this->getChildByID("normal-mode-percentage");
        if (!bar) bar = this->getChildByID("progress-bar");
        if (!bar) return;

        float width = bar->getScaledContentSize().width;
        if (width < 40.f) return;

        auto* strip = paimon::info::DeathHeatmapNode::create(*progress, false, width, 12.f);
        if (!strip) return;

        strip->setID("info-suite-heatmap"_spr);
        strip->setPosition({bar->getPositionX(), bar->getPositionY() - 14.f});
        this->addChild(strip, bar->getZOrder());
    }
};
