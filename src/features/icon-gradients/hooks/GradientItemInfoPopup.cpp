// shades the icon preview when better unlock info is around, after zilko's
// "icon gradients" (independent implementation; idea credit zilko144, unlicensed).

#include <Geode/Geode.hpp>
#include <Geode/modify/ItemInfoPopup.hpp>

#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

class $modify(GradientItemInfoPopup, ItemInfoPopup) {
    bool init(int p0, UnlockType p1) {
        if (!ItemInfoPopup::init(p0, p1)) return false;

        if (moduleEnabled() && Loader::get()->isModLoaded("rynat.better_unlock_info")) {
            if (GJItemIcon* item = m_mainLayer->getChildByType<GJItemIcon>(0)) {
                if (SimplePlayer* icon = item->getChildByType<SimplePlayer>(0))
                    GradientUtils::paintMenuIcon(icon, false, 2);
            }
        }

        return true;
    }
};
