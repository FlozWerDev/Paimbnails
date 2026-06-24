#pragma once

#include <Geode/Geode.hpp>

namespace paimon::menuphysics {

class MenuPhysicsManager {
public:
    static MenuPhysicsManager& get();

    bool enabled() const;

    void onLayerEntered(cocos2d::CCNode* host);
    void applyToCurrentScene();
    void clearFromCurrentScene();

private:
    void attach(cocos2d::CCNode* host);
};

} // namespace paimon::menuphysics
