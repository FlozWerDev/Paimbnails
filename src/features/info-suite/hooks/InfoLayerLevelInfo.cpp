// The comments layer's "i" button opens the vanilla "Level Info" alert: the
// description plus upload date, update date, stars requested and original id.
// With Extended Level Info on, that alert is replaced by the popup, which shows
// the same fields and every other one the game hides.
//
// Only the level flavour of InfoLayer is touched: the same layer also serves
// lists and profiles, and those keep the vanilla alert.

#include "../InfoModule.hpp"
#include "../ui/ExtendedInfoPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/InfoLayer.hpp>
#include <Geode/modify/InfoLayer.hpp>

using namespace geode::prelude;

class $modify(PaimonInfoSuiteInfoLayer, InfoLayer) {
    void onLevelInfo(CCObject* sender) {
        if (!paimon::info::moduleEnabled("info-mod-extended") || !m_level) {
            InfoLayer::onLevelInfo(sender);
            return;
        }

        auto popup = paimon::info::ExtendedInfoPopup::create(m_level);
        if (!popup) {
            InfoLayer::onLevelInfo(sender);
            return;
        }
        popup->show();
    }
};
