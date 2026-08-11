#pragma once
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

namespace paimon::icon_gradients {

class $modify(GradientBaseGameLayer, GJBaseGameLayer) {
public:
    struct Fields {
        bool isExitingDual = false;
        SimplePlayer* dualSimplePlayer = nullptr;
    };

    void playExitDualEffect(PlayerObject* p0);
};

} // namespace paimon::icon_gradients
