// Icon gradients on the player, rethought for Paimbnails.
//
// Idea credit: "Icon Gradients" by zilko
// (https://github.com/zilko144/icon-gradients-geode, unlicensed —
// all rights reserved). This file is an independent implementation written
// from the behavior spec: same user-visible behavior (mirrored gradient
// overlays per sprite slot, animated robot/spider overlays, compat dances
// with third-party mods), own expression. Lane tables replace the
// per-slot if-ladders; one paint pass and one animation-section helper
// replace the triplicated slot/section bodies.
//
// Compatibility surface kept on purpose (protocol facts, not expression):
// node-id format "{}-gradient-{}" (+ "-line"/"animation-gradient-…"),
// shader-variant seeds, overlay extras, mod-id strings, the -9038 sentinel
// and the "0060" game variable.

#include "GradientPlayerObject.hpp"

#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

bool GradientPlayerObject::shouldReturn(GJBaseGameLayer* gameLayer, bool secondPlayer) {
    if (!gameLayer) return !Mod::get()->getSavedValue<bool>("editor-gradient");
    return !gameLayer->m_player1 || !gameLayer->m_player2;
}

IconType GradientPlayerObject::getIconType() {
    if (m_isShip) return m_isPlatformer ? IconType::Jetpack : IconType::Ship;
    static constexpr struct {
        bool PlayerObject::* flag;
        IconType type;
    } kModes[] = {
        {&PlayerObject::m_isBall, IconType::Ball},
        {&PlayerObject::m_isBird, IconType::Ufo},
        {&PlayerObject::m_isDart, IconType::Wave},
        {&PlayerObject::m_isRobot, IconType::Robot},
        {&PlayerObject::m_isSpider, IconType::Spider},
        {&PlayerObject::m_isSwing, IconType::Swing},
    };
    for (auto [flag, type] : kModes)
        if (this->*flag) return type;
    return IconType::Cube;
}

void GradientPlayerObject::updateCube(float) {
    m_fields->m_previousType = IconType(-1);
    updateGradient();
}

void GradientPlayerObject::updateFlip(float) {
    auto f = m_fields.self();
    static constexpr MirrorLane kLanes[] = {
        {&Fields::m_iconSprite, &PlayerObject::m_iconSprite},
        {&Fields::m_iconSpriteSecondary, &PlayerObject::m_iconSpriteSecondary},
        {&Fields::m_iconGlow, &PlayerObject::m_iconGlow},
        {&Fields::m_iconSpriteWhitener, &PlayerObject::m_iconSpriteWhitener},
        {&Fields::m_iconSpriteLine, &PlayerObject::m_iconSprite},
    };
    for (auto [copyMember, sourceMember] : kLanes) {
        if (CCSprite* copy = f->*copyMember) {
            CCSprite* source = this->*sourceMember;
            copy->setFlipX(source->isFlipX());
            copy->setFlipY(source->isFlipY());
        }
    }
}

void GradientPlayerObject::updateVisibility() {
    auto f = m_fields.self();
    static constexpr MirrorLane kLanes[] = {
        {&Fields::m_iconSprite, &PlayerObject::m_iconSprite},
        {&Fields::m_iconSpriteSecondary, &PlayerObject::m_iconSpriteSecondary},
        {&Fields::m_iconGlow, &PlayerObject::m_iconGlow},
        {&Fields::m_iconSpriteWhitener, &PlayerObject::m_iconSpriteWhitener},
        {&Fields::m_iconSpriteLine, &PlayerObject::m_iconSprite},
        {&Fields::m_iconSpriteLineSecondary, &PlayerObject::m_iconSpriteSecondary},
        {&Fields::m_iconSpriteLineWhitener, &PlayerObject::m_iconSpriteWhitener},
        {&Fields::m_vehicleSprite, &PlayerObject::m_vehicleSprite},
        {&Fields::m_vehicleSpriteSecondary, &PlayerObject::m_vehicleSpriteSecondary},
        {&Fields::m_vehicleGlow, &PlayerObject::m_vehicleGlow},
        {&Fields::m_vehicleSpriteWhitener, &PlayerObject::m_vehicleSpriteWhitener},
        {&Fields::m_vehicleSpriteLine, &PlayerObject::m_vehicleSprite},
        {&Fields::m_vehicleSpriteLineSecondary, &PlayerObject::m_vehicleSpriteSecondary},
        {&Fields::m_vehicleSpriteLineWhitener, &PlayerObject::m_vehicleSpriteWhitener},
    };
    for (auto [copyMember, sourceMember] : kLanes)
        if (CCSprite* copy = f->*copyMember)
            copy->setOpacity((this->*sourceMember)->getOpacity());
    for (CCSprite* overlay : f->m_animSprites) {
        auto it = f->m_animSpriteParents.find(overlay);
        if (it != f->m_animSpriteParents.end())
            overlay->setOpacity(it->second->getOpacity());
    }
}

void GradientPlayerObject::updateSprite(CCSprite* live, Ref<CCSprite>& copy, SpriteType kind, ColorType color) {
    GradientUtils::hideSprite(live);
    if (copy)
        return copy->setDisplayFrame(live->displayFrame());
    copy = CCSprite::createWithSpriteFrame(live->displayFrame());
    copy->setID(fmt::format("{}-gradient-{}"_spr, GradientUtils::getTypeID(kind), static_cast<int>(color)).c_str());
    if ((color == ColorType::Line || color == ColorType::White)
            && Loader::get()->isModLoaded("alphalaneous.fine_outline")) {
        Loader::get()->queueInMainThread([child = Ref(copy), host = Ref(live)] {
            host->addChild(child, 10);
        });
    } else {
        live->addChild(copy);
    }
    copy->setAnchorPoint({0, 0});
    copy->setVisible(true);
}

// One pass over a slot table: ensures the overlay where the config is
// non-empty, shades it, shows/hides it, and restores the live sprite's
// stock shader whenever no visible overlay covers it (line art never
// needs that restore — it draws on top of a visible host).
void GradientPlayerObject::paintSet(Gradient const& gradient, SpriteType kind, int extra,
        PaintLane const* lanes, size_t count, Fields* f) {
    IconType type = getIconType();
    auto* stock = CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor);
    for (size_t i = 0; i < count; ++i) {
        PaintLane const& lane = lanes[i];
        GradientConfig const& config = gradient.*lane.config;
        bool bare = config.isEmpty(lane.color, m_isSecondPlayer);
        if (!bare)
            updateSprite(this->*lane.live, f->*lane.copy, kind, lane.color);
        if (CCSprite* copy = f->*lane.copy) {
            GradientUtils::applyGradient(copy, config, type, lane.color, lane.seed,
                false, m_isSecondPlayer, true, extra, lane.color == ColorType::Line);
            copy->setVisible(!bare);
            if (lane.color != ColorType::Line && !copy->isVisible())
                (this->*lane.live)->setShaderProgram(stock);
        } else if (lane.color != ColorType::Line) {
            (this->*lane.live)->setShaderProgram(stock);
        }
    }
}

void GradientPlayerObject::updateIconSprite(Gradient const& gradient, Fields* f) {
    static constexpr PaintLane kLanes[] = {
        {&PlayerObject::m_iconSprite, &Fields::m_iconSprite, &Gradient::main, ColorType::Main, 105},
        {&PlayerObject::m_iconSpriteSecondary, &Fields::m_iconSpriteSecondary, &Gradient::secondary, ColorType::Secondary, 205},
        {&PlayerObject::m_iconGlow, &Fields::m_iconGlow, &Gradient::glow, ColorType::Glow, 305},
        {&PlayerObject::m_iconSpriteWhitener, &Fields::m_iconSpriteWhitener, &Gradient::white, ColorType::White, 405},
        {&PlayerObject::m_iconSprite, &Fields::m_iconSpriteLine, &Gradient::line, ColorType::Line, 505},
        {&PlayerObject::m_iconSpriteSecondary, &Fields::m_iconSpriteLineSecondary, &Gradient::line, ColorType::Line, 605},
        {&PlayerObject::m_iconSpriteWhitener, &Fields::m_iconSpriteLineWhitener, &Gradient::line, ColorType::Line, 705},
    };
    paintSet(gradient, SpriteType::Icon, 2, kLanes, std::size(kLanes), f);
}

void GradientPlayerObject::updateVehicleSprite(Gradient const& gradient, Fields* f) {
    static constexpr PaintLane kLanes[] = {
        {&PlayerObject::m_vehicleSprite, &Fields::m_vehicleSprite, &Gradient::main, ColorType::Main, 104},
        {&PlayerObject::m_vehicleSpriteSecondary, &Fields::m_vehicleSpriteSecondary, &Gradient::secondary, ColorType::Secondary, 204},
        {&PlayerObject::m_vehicleGlow, &Fields::m_vehicleGlow, &Gradient::glow, ColorType::Glow, 304},
        {&PlayerObject::m_vehicleSpriteWhitener, &Fields::m_vehicleSpriteWhitener, &Gradient::white, ColorType::White, 404},
        {&PlayerObject::m_vehicleSprite, &Fields::m_vehicleSpriteLine, &Gradient::line, ColorType::Line, 504},
        {&PlayerObject::m_vehicleSpriteSecondary, &Fields::m_vehicleSpriteLineSecondary, &Gradient::line, ColorType::Line, 604},
        {&PlayerObject::m_vehicleSpriteWhitener, &Fields::m_vehicleSpriteLineWhitener, &Gradient::line, ColorType::Line, 704},
    };
    paintSet(gradient, SpriteType::Vehicle, 44, kLanes, std::size(kLanes), f);
}

// Covers one animation section (a host list, or the single extra sprite):
// fresh overlay per host, host hidden unless it is line art, the pair
// tracked for opacity, then shaded. `single` sections keep a fixed seed
// and fixed node id; lists count up from the base seed.
void GradientPlayerObject::shadeAnimSection(auto&& hosts, GradientConfig const& config, IconType type,
        ColorType color, int seedBase, bool line, bool single, Fields* f) {
    if (config.isEmpty(color, m_isSecondPlayer)) return;
    std::string kind = GradientUtils::getTypeID(SpriteType::Animation);
    bool outline = Loader::get()->isModLoaded("alphalaneous.fine_outline");
    int count = 0;
    for (auto* host : hosts) {
        ++count;
        CCSprite* overlay = CCSprite::createWithSpriteFrame(host->displayFrame());
        if (single) {
            if (line) overlay->setID(fmt::format("{}-gradient-line"_spr, kind).c_str());
            else overlay->setID(fmt::format("{}-gradient"_spr, kind).c_str());
        } else {
            if (line) overlay->setID(fmt::format("{}-gradient-line-{}"_spr, kind, count).c_str());
            else overlay->setID(fmt::format("{}-gradient-{}"_spr, kind, count).c_str());
        }
        overlay->setAnchorPoint({0, 0});
        if (outline && (line || single)) {
            Loader::get()->queueInMainThread([child = Ref(overlay), hostCopy = Ref(host)] {
                hostCopy->addChild(child, 10);
            });
        } else {
            host->addChild(overlay);
        }
        if (!line) GradientUtils::hideSprite(host);
        f->m_animSprites.push_back(overlay);
        f->m_animSpriteParents[overlay] = host;
        GradientUtils::applyGradient(overlay, config, type, color,
            single ? seedBase : seedBase + count, false, m_isSecondPlayer, true, 55, line);
    }
}

void GradientPlayerObject::updateAnimSprite(IconType type, Gradient const& gradient, Fields* f) {
    GJRobotSprite* mech = type == IconType::Robot ? m_robotSprite : m_spiderSprite;
    if (!mech || !mech->m_paSprite) return;
    GradientUtils::patchBatchNode(type == IconType::Robot ? m_robotBatchNode : m_spiderBatchNode);

    shadeAnimSection(CCArrayExt<CCSpritePart*>(mech->m_paSprite->m_spriteParts),
        gradient.main, type, ColorType::Main, 100, false, false, f);
    shadeAnimSection(CCArrayExt<CCSprite*>(mech->m_secondArray),
        gradient.secondary, type, ColorType::Secondary, 200, false, false, f);
    shadeAnimSection(mech->m_glowSprite->getChildrenExt<CCSprite*>(),
        gradient.glow, type, ColorType::Glow, 300, false, false, f);
    CCSprite* extra[] = {mech->m_extraSprite};
    shadeAnimSection(extra, gradient.white, type, ColorType::White, 400, false, true, f);
    shadeAnimSection(CCArrayExt<CCSpritePart*>(mech->m_paSprite->m_spriteParts),
        gradient.line, type, ColorType::Line, 500, true, false, f);
    shadeAnimSection(CCArrayExt<CCSprite*>(mech->m_secondArray),
        gradient.line, type, ColorType::Line, 600, true, false, f);
    shadeAnimSection(extra, gradient.line, type, ColorType::Line, 700, true, true, f);
}

// Robot and spider share the whole refresh dance; only the icon kind differs.
void GradientPlayerObject::refreshMech(IconType type) {
    GJBaseGameLayer* layer = m_gameLayer ? m_gameLayer : GJBaseGameLayer::get();
    if (!layer || (this != layer->m_player1 && this != layer->m_player2)) return;
    auto paint = [this, type, layer] {
        if (getTag() == 0xCB04) return;
        if (shouldReturn(GJBaseGameLayer::get())) return;
        updateAnimSprite(type, GradientUtils::getGradient(type, this == layer->m_player2), m_fields.self());
    };
    if (!m_fields->m_animSpritesInitialized) {
        Loader::get()->queueInMainThread([paint, this] {
            paint();
            m_fields->m_animSpritesInitialized = true;
        });
    } else {
        paint();
    }
}

void GradientPlayerObject::updateGradient() {
    GJBaseGameLayer* layer = GJBaseGameLayer::get();
    if (shouldReturn(layer)) return;
    // Remote multiplayer dolls are PlayerObjects too; only the local pair wears this kit.
    if (layer && this != layer->m_player1 && this != layer->m_player2) return;
    auto f = m_fields.self();

    if (f->m_swingFlipLoaded)
        schedule(schedule_selector(GradientPlayerObject::updateFlip));

    IconType type = getIconType();
    if (type == f->m_previousType) return;
    f->m_previousType = type;

    Gradient gradient = GradientUtils::getGradient(type, m_isSecondPlayer);

    // Compat with the UFO/ship/cube replacer mod: its sprites win while it
    // is around, so ours step aside (hidden) and the stock ones come back.
    static constexpr Ref<CCSprite> Fields::* kCopies[] = {
        &Fields::m_iconSprite, &Fields::m_iconSpriteSecondary, &Fields::m_iconGlow,
        &Fields::m_iconSpriteWhitener, &Fields::m_iconSpriteLine,
    };
    if (f->m_menuDollPatchLoaded)
        for (auto member : kCopies)
            if (CCSprite* copy = f->*member) copy->setVisible(true);

    if (type != IconType::Ship && type != IconType::Jetpack && type != IconType::Ufo) {
        updateIconSprite(gradient, f);
        return;
    }
    updateVehicleSprite(gradient, f);
    updateIconSprite(GradientUtils::getGradient(IconType::Cube, m_isSecondPlayer), f);

    if (f->m_menuDollPatchLoaded && !m_isSecondPlayer) {
        for (auto member : kCopies)
            if (CCSprite* copy = f->*member) copy->setVisible(false);
        static constexpr CCSprite* PlayerObject::* kLive[] = {
            &PlayerObject::m_iconSprite, &PlayerObject::m_iconSpriteSecondary,
            &PlayerObject::m_iconGlow, &PlayerObject::m_iconSpriteWhitener,
        };
        for (auto member : kLive) {
            (this->*member)->setVisible(true);
            (this->*member)->setOpacity(255);
        }
        m_iconGlow->setVisible(m_hasGlow);
    }
}

bool GradientPlayerObject::init(int p0, int p1, GJBaseGameLayer* p2, CCLayer* p3, bool p4) {
    if (!PlayerObject::init(p0, p1, p2, p3, p4)) return false;
    auto f = m_fields.self();
    auto* loader = Loader::get();
    f->m_menuDollPatchLoaded = loader->isModLoaded("yellowcat98.custom_ufo_n_ship_cube");
    f->m_separateDualIconsIsLoaded = sdiEnabled();
    f->m_swingFlipLoaded = loader->isModLoaded("rgc_exists.swingcopter_flip");
    loader->queueInMainThread([self = Ref(this)] {
        if (LevelEditorLayer::get() && self->m_isSecondPlayer && sdiEnabled())
            self->schedule(schedule_selector(GradientPlayerObject::updateCube));
        self->updateGradient();
    });
    return true;
}

void GradientPlayerObject::togglePlayerScale(bool p0, bool p1) {
    PlayerObject::togglePlayerScale(p0, p1);
    if (GameManager::get()->getGameVariable("0060")) {
        m_fields->m_previousType = IconType(-9038);
        updateGradient();
    }
}

void GradientPlayerObject::updatePlayerFrame(int p0) {
    PlayerObject::updatePlayerFrame(p0);
    if (getIconType() == IconType::Cube)
        updateGradient();
}
void GradientPlayerObject::updatePlayerShipFrame(int p0) { PlayerObject::updatePlayerShipFrame(p0); updateGradient(); }
void GradientPlayerObject::updatePlayerRollFrame(int p0) { PlayerObject::updatePlayerRollFrame(p0); updateGradient(); }
void GradientPlayerObject::updatePlayerBirdFrame(int p0) { PlayerObject::updatePlayerBirdFrame(p0); updateGradient(); }
void GradientPlayerObject::updatePlayerDartFrame(int p0) { PlayerObject::updatePlayerDartFrame(p0); updateGradient(); }
void GradientPlayerObject::updatePlayerSwingFrame(int p0) { PlayerObject::updatePlayerSwingFrame(p0); updateGradient(); }
void GradientPlayerObject::updatePlayerJetpackFrame(int p0) { PlayerObject::updatePlayerJetpackFrame(p0); updateGradient(); }

void GradientPlayerObject::createRobot(int p0) {
    PlayerObject::createRobot(p0);
    refreshMech(IconType::Robot);
}

void GradientPlayerObject::createSpider(int p0) {
    PlayerObject::createSpider(p0);
    refreshMech(IconType::Spider);
}
