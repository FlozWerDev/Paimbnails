#pragma once
#include <Geode/Geode.hpp>
#include "../GradientTypes.hpp"
#include <memory>
#include <unordered_map>

namespace paimon::icon_gradients {
struct GradientImageAtlas {
    geode::Ref<cocos2d::CCTexture2D> texture;
    std::unordered_map<std::string, int> slots;
    int columns = 1;
    int rows = 1;
    size_t textureBytes = 0;
};
std::shared_ptr<GradientImageAtlas> getGradientImageAtlas(std::vector<SimplePoint> const& points);
// The draw hook binds the atlas per sprite, including during preview fades.
void setGradientImage(cocos2d::CCSprite* sprite, std::shared_ptr<GradientImageAtlas> atlas);
}
