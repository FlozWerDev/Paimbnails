#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

class $modify(GradientMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        if (
            !moduleEnabled()
            || !Loader::get()->isModLoaded("capeling.icon_profile")
        ) {
            return true;
        }

        auto* spr = getChildByIDRecursive("profile-icon");
        if (!spr) return true;
        auto* icon = spr->getChildByType<SimplePlayer>(0);
        if (!icon) return true;

        IconType type = GradientUtils::getIconType(icon);
        Gradient gradient = GradientUtils::getGradient(type, false);
        GradientUtils::applyGradient(icon, gradient, false, false, 2);
        return true;
    }
};
