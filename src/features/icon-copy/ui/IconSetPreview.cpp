#include "IconSetPreview.hpp"

#include "../IconCopyStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

// Every gamemode the profile response carries, in garage tab order.
constexpr IconType kGamemodes[] = {
    IconType::Cube, IconType::Ship, IconType::Ball, IconType::Ufo, IconType::Wave,
    IconType::Robot, IconType::Spider, IconType::Swing, IconType::Jetpack,
};

// Drawn size of a SimplePlayer, in points. Its own content size is often zero,
// so measure the layer that actually gets painted and only guess as a last
// resort (robot and spider keep their parts in their own sprites).
float drawnSize(SimplePlayer* player) {
    if (auto* first = player->m_firstLayer) {
        auto const size = first->getScaledContentSize();
        float const dim = std::max(size.width, size.height);
        if (dim > 0.f) return dim;
    }
    auto const own = player->getContentSize();
    float const dim = std::max(own.width, own.height);
    return dim > 0.f ? dim : 30.f;
}

}  // anonymous namespace

CCNode* makePreview(IconSet const& set, IconType type, float box) {
    int const id = std::max(set.iconFor(type), 1);
    auto* player = SimplePlayer::create(id);
    if (!player) return nullptr;

    if (type != IconType::Cube) player->updatePlayerFrame(id, type);

    if (auto* gm = GameManager::get()) {
        auto const c1 = gm->colorForIdx(set.color1);
        auto const c2 = gm->colorForIdx(set.color2);
        player->setColor(c1);
        player->setSecondColor(c2);
        if (set.glow) {
            player->setGlowOutline(set.glowColor >= 0 ? gm->colorForIdx(set.glowColor) : c2);
        } else {
            player->disableGlowOutline();
        }
    }

    player->setScale(box / drawnSize(player));

    // The wrapper is positioned by its centre, like a sprite: CircleButtonSprite
    // and friends drop their top node at the centre of the base.
    auto* wrap = CCNode::create();
    wrap->setContentSize({box, box});
    wrap->setAnchorPoint({0.5f, 0.5f});
    wrap->ignoreAnchorPointForPosition(false);
    player->setPosition({box / 2.f, box / 2.f});
    wrap->addChild(player);
    return wrap;
}

std::span<IconType const> previewGamemodes() {
    return kGamemodes;
}

char const* gamemodeName(IconType type) {
    switch (type) {
        case IconType::Ship: return "Ship";
        case IconType::Ball: return "Ball";
        case IconType::Ufo: return "UFO";
        case IconType::Wave: return "Wave";
        case IconType::Robot: return "Robot";
        case IconType::Spider: return "Spider";
        case IconType::Swing: return "Swing";
        case IconType::Jetpack: return "Jetpack";
        default: return "Cube";
    }
}

}  // namespace paimon::iconcopy
