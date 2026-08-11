#pragma once
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuGameLayer.hpp>

namespace paimon::icon_gradients {

class $modify(GradientMenuGameLayer, MenuGameLayer) {
public:
    struct Fields {
        PlayerObject* m_realPlayerObject = nullptr;
    };

    bool init();

    void resetPlayer();

    void updateGradient();
};

} // namespace paimon::icon_gradients
