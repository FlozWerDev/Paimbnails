#pragma once
#include <Geode/Geode.hpp>
#include <Geode/modify/SimplePlayer.hpp>

namespace paimon::icon_gradients {

class $modify(GradientSimplePlayer, SimplePlayer) {
public:
    struct Fields {
        IconType m_type = IconType::Cube;
    };

    void updatePlayerFrame(int frame, IconType type);
};

} // namespace paimon::icon_gradients
