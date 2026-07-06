#include "MenuPhysicsManager.hpp"
#include "MenuPhysicsNode.hpp"

#include <Geode/binding/FLAlertLayer.hpp>
#include <vector>

using namespace geode::prelude;

namespace paimon::menuphysics {

namespace {
    bool hasMenuButtons(CCNode* n) {
        if (!n) return false;
        if (auto* item = typeinfo_cast<CCMenuItem*>(n)) {
            if (typeinfo_cast<CCMenu*>(n->getParent())) return true;
        }
        if (auto* children = n->getChildren()) {
            for (int i = 0; i < static_cast<int>(children->count()); ++i) {
                if (hasMenuButtons(typeinfo_cast<CCNode*>(children->objectAtIndex(i)))) {
                    return true;
                }
            }
        }
        return false;
    }

    void collectPhysicsNodes(CCNode* n, std::vector<MenuPhysicsNode*>& out) {
        if (!n) return;
        static std::string const targetId = "paim-menu-physics-node"_spr;
        if (n->getID() == targetId) {
            out.push_back(static_cast<MenuPhysicsNode*>(n));
            return;
        }
        if (auto* children = n->getChildren()) {
            for (int i = 0; i < static_cast<int>(children->count()); ++i) {
                collectPhysicsNodes(typeinfo_cast<CCNode*>(children->objectAtIndex(i)), out);
            }
        }
    }
}

MenuPhysicsManager& MenuPhysicsManager::get() {
    static MenuPhysicsManager instance;
    return instance;
}

bool MenuPhysicsManager::enabled() const {
    return Mod::get()->getSettingValue<bool>("menu-physics-enable");
}

void MenuPhysicsManager::attach(CCNode* host) {
    if (!host) return;
    if (host->getChildByID("paim-menu-physics-node"_spr)) return;
    if (!hasMenuButtons(host)) return;

    if (auto* node = MenuPhysicsNode::create()) {
        host->addChild(node, 100);
    }
}

void MenuPhysicsManager::onLayerEntered(CCNode* host) {
    if (!enabled()) return;
    attach(host);
}

void MenuPhysicsManager::applyToCurrentScene() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto* children = scene->getChildren();
    if (!children) return;

    for (int i = 0; i < static_cast<int>(children->count()); ++i) {
        auto* child = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
        if (!child) continue;
        if (typeinfo_cast<FLAlertLayer*>(child)) continue;
        attach(child);
    }
}

void MenuPhysicsManager::clearFromCurrentScene() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    std::vector<MenuPhysicsNode*> nodes;
    collectPhysicsNodes(scene, nodes);
    for (auto* node : nodes) {
        node->detach();
    }
}

} // namespace paimon::menuphysics