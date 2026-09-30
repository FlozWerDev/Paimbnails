// replaces the level flavour of infolayer's "i" alert with the popup (same + hidden fields).
// lists and profiles keep the vanilla alert.

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
