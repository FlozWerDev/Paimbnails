#pragma once
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "../GradientTypes.hpp"
#include "../../../framework/HookConventions.hpp"

namespace paimon::icon_gradients {

using namespace geode::prelude;

class $modify(GradientPlayerObject, PlayerObject) {
public:
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::updatePlayerFrame");
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::updatePlayerShipFrame");
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::updatePlayerRollFrame");
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::updatePlayerBirdFrame");
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::updatePlayerDartFrame");
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::createRobot");
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::createSpider");
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::updatePlayerSwingFrame");
        paimon::hooks::afterNodeIdsOrLate(self, "PlayerObject::updatePlayerJetpackFrame");
    }

    struct Fields {
        IconType m_previousType = static_cast<IconType>(-9038);

        Ref<CCSprite> m_iconSprite = nullptr;
        Ref<CCSprite> m_iconSpriteSecondary = nullptr;
        Ref<CCSprite> m_iconGlow = nullptr;
        Ref<CCSprite> m_iconSpriteWhitener = nullptr;
        Ref<CCSprite> m_iconSpriteLine = nullptr;
        Ref<CCSprite> m_iconSpriteLineSecondary = nullptr;
        Ref<CCSprite> m_iconSpriteLineWhitener = nullptr;
        Ref<CCSprite> m_vehicleSprite = nullptr;
        Ref<CCSprite> m_vehicleSpriteSecondary = nullptr;
        Ref<CCSprite> m_vehicleGlow = nullptr;
        Ref<CCSprite> m_vehicleSpriteWhitener = nullptr;
        Ref<CCSprite> m_vehicleSpriteLine = nullptr;
        Ref<CCSprite> m_vehicleSpriteLineSecondary = nullptr;
        Ref<CCSprite> m_vehicleSpriteLineWhitener = nullptr;

        std::vector<Ref<CCSprite>> m_animSprites;
        std::unordered_map<CCSprite*, Ref<CCSprite>> m_animSpriteParents;

        // Compat with the "Custom UFO N Ship Cube" doll-replacement mod:
        // while it is loaded the menu doll needs its shaded copies shown.
        bool m_menuDollPatchLoaded = false;
        bool m_separateDualIconsIsLoaded = false;
        bool m_swingFlipLoaded = false;

        bool m_animSpritesInitialized = false;
    };

    // One gradient overlay plus the live sprite it shadows: `copy` is the
    // painted duplicate, `source` the live sprite it follows for flip and
    // opacity, `live` the sprite that gets its shader restored when the
    // overlay is gone, `config` the gradient slot, `color` its channel and
    // `seed` the shader variant.
    struct MirrorLane {
        Ref<CCSprite> Fields::* copy;
        CCSprite* PlayerObject::* source;
    };

    struct PaintLane {
        CCSprite* PlayerObject::* live;
        Ref<CCSprite> Fields::* copy;
        GradientConfig Gradient::* config;
        ColorType color;
        int seed;
    };

    bool shouldReturn(GJBaseGameLayer*, bool = false);

    IconType getIconType();

    void updateCube(float);

    void updateFlip(float);

    void updateVisibility();

    void updateSprite(CCSprite*, Ref<CCSprite>&, SpriteType, ColorType);

    void paintSet(Gradient const&, SpriteType, int, PaintLane const*, size_t, auto);

    void updateIconSprite(Gradient const&, auto);

    void updateVehicleSprite(Gradient const&, auto);

    void shadeAnimSection(auto&&, GradientConfig const&, IconType, ColorType, int, bool, bool, auto);

    void updateAnimSprite(IconType, Gradient const&, auto);

    void refreshMech(IconType);

    void updateGradient();

    void togglePlayerScale(bool, bool);

    void updatePlayerFrame(int);

    void updatePlayerShipFrame(int);

    void updatePlayerRollFrame(int);

    void updatePlayerBirdFrame(int);

    void updatePlayerDartFrame(int);

    void updatePlayerSwingFrame(int);

    void updatePlayerJetpackFrame(int);

    void createRobot(int);

    void createSpider(int);

    bool init(int, int, GJBaseGameLayer*, CCLayer*, bool);
};

} // namespace paimon::icon_gradients
