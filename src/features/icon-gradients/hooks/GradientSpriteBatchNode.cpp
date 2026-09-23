#include <Geode/Geode.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>

using namespace geode::prelude;

class $modify(PaimonGradientSpriteBatchNode, CCSpriteBatchNode) {
    void visit() {
        if (getUserFlag("gradient-child-shaders"_spr)) {
            CCNode::visit();
            return;
        }
        CCSpriteBatchNode::visit();
    }

    void draw() {
        if (getUserFlag("gradient-child-shaders"_spr)) {
            CCNode::draw();
            return;
        }
        CCSpriteBatchNode::draw();
    }
};
