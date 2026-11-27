// player icon gradients, after zilko's "icon gradients" (independent implementation, own expression).
// compat surface kept on purpose: "{}-gradient-{}" node ids, shader seeds, mod-id strings, -9038, "0060".

#include "GradientPlayerObject.hpp"

#include "../GradientCache.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

bool GradientPlayerObject::shouldReturn(GJBaseGameLayer* gameLayer, bool) {
    if (!moduleEnabled() || GradientCache::isModDisabled()) return true;
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
            if (CCSprite* source = this->*sourceMember) {
                copy->setFlipX(source->isFlipX());
                copy->setFlipY(source->isFlipY());
            }
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
            if (CCSprite* source = this->*sourceMember)
                copy->setOpacity(source->getOpacity());
    for (auto const& pair : f->m_robotAnimOverlays) {
        if (pair.overlay && pair.overlay->getParent() && pair.host && pair.host->getParent())
            pair.overlay->setOpacity(pair.host->getOpacity());
    }
    for (auto const& pair : f->m_spiderAnimOverlays) {
        if (pair.overlay && pair.overlay->getParent() && pair.host && pair.host->getParent())
            pair.overlay->setOpacity(pair.host->getOpacity());
    }
}

void GradientPlayerObject::updateSprite(CCSprite* live, Ref<CCSprite>& copy, SpriteType kind, ColorType color) {
    if (!live) return;
    CCSpriteFrame* frame = live->displayFrame();
    if (!frame) return;
    GradientUtils::hideSprite(live);
    if (copy)
        return copy->setDisplayFrame(frame);
    copy = CCSprite::createWithSpriteFrame(frame);
    if (!copy) return;
    copy->setID(fmt::format("{}-gradient-{}"_spr, GradientUtils::getTypeID(kind), static_cast<int>(color)).c_str());
    if ((color == ColorType::Line || color == ColorType::White)
            && Loader::get()->isModLoaded("alphalaneous.fine_outline")) {
        Loader::get()->queueInMainThread([child = Ref(copy), host = Ref(live)] {
            if (host && host->getParent() && child && !child->getParent()) host->addChild(child, 10);
        });
    } else {
        live->addChild(copy);
    }
    copy->setAnchorPoint({0, 0});
    copy->setVisible(true);
}

// line art draws over its host, so only the other slots restore the stock shader.
void GradientPlayerObject::paintSet(Gradient const& gradient, SpriteType kind, int extra,
        PaintLane const* lanes, size_t count, Fields* f) {
    IconType type = getIconType();
    auto* stock = CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor);
    for (size_t i = 0; i < count; ++i) {
        PaintLane const& lane = lanes[i];
        CCSprite* live = this->*lane.live;
        if (!live) continue;
        GradientConfig const& config = gradient.*lane.config;
        bool bare = config.isEmpty(lane.color, m_isSecondPlayer);
        if (!bare)
            updateSprite(live, f->*lane.copy, kind, lane.color);
        if (CCSprite* copy = f->*lane.copy) {
            GradientUtils::applyGradient(copy, config, type, lane.color, lane.seed,
                false, m_isSecondPlayer, true, extra, lane.color == ColorType::Line);
            copy->setVisible(!bare);
            if (lane.color != ColorType::Line && !copy->isVisible() && stock)
                live->setShaderProgram(stock);
        } else if (lane.color != ColorType::Line && stock) {
            live->setShaderProgram(stock);
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

// single sprites keep a fixed shader seed and node id; lists advance from the base seed.
void GradientPlayerObject::shadeAnimSection(auto&& hosts, GradientConfig const& config, IconType type,
        ColorType color, int seedBase, bool line, bool single, std::vector<Fields::AnimOverlay>& animList) {
    bool const empty = config.isEmpty(color, m_isSecondPlayer);
    std::string kind = GradientUtils::getTypeID(SpriteType::Animation);
    bool outline = Loader::get()->isModLoaded("alphalaneous.fine_outline");
    auto* stock = empty && !line
        ? CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor)
        : nullptr;
    int count = 0;
    for (auto* host : hosts) {
        ++count;
        if (!host) continue;
        std::string nodeId;
        if (single) {
            nodeId = line ? fmt::format("{}-gradient-line"_spr, kind)
                          : fmt::format("{}-gradient"_spr, kind);
        } else {
            nodeId = line ? fmt::format("{}-gradient-line-{}"_spr, kind, count)
                          : fmt::format("{}-gradient-{}"_spr, kind, count);
        }

        CCSprite* overlay = typeinfo_cast<CCSprite*>(host->getChildByID(nodeId));
        if (empty) {
            if (overlay) overlay->setVisible(false);
            if (!line && stock) host->setShaderProgram(stock);
            continue;
        }

        CCSpriteFrame* frame = host->displayFrame();
        if (!frame) continue;

        if (overlay) {
            overlay->setDisplayFrame(frame);
            overlay->setVisible(true);
        } else {
            overlay = CCSprite::createWithSpriteFrame(frame);
            if (!overlay) continue;
            overlay->setID(nodeId.c_str());
            overlay->setAnchorPoint({0, 0});
            if (outline && (line || single)) {
                Loader::get()->queueInMainThread([child = Ref(overlay), hostCopy = Ref(host), nodeId] {
                    if (hostCopy && hostCopy->getParent() && child && !child->getParent() && !hostCopy->getChildByID(nodeId))
                        hostCopy->addChild(child, 10);
                });
            } else {
                host->addChild(overlay);
            }
        }
        if (!line) GradientUtils::hideSprite(host);
        animList.push_back({overlay, host});
        GradientUtils::applyGradient(overlay, config, type, color,
            single ? seedBase : seedBase + count, false, m_isSecondPlayer, true, 55, line);
    }
}

void GradientPlayerObject::updateAnimSprite(IconType type, Gradient const& gradient, Fields* f) {
    if (type != IconType::Robot && type != IconType::Spider) return;
    GJRobotSprite* mech = type == IconType::Robot ? m_robotSprite : m_spiderSprite;
    if (!mech || !mech->m_paSprite) return;
    // Never call removeFromParent() on prior overlays here: PlayerObject::createRobot /
    // createSpider may have already detached the old GJRobotSprite from its CCSpriteBatchNode,
    // so calling CCSprite::removeChild on an orphaned host crashes in CCSpriteBatchNode::swap.
    // Dropping our Refs releases orphaned hosts via ~CCNode(), while live hosts reuse overlays by ID.
    auto& animList = type == IconType::Robot ? f->m_robotAnimOverlays : f->m_spiderAnimOverlays;
    animList.clear();
    GradientUtils::enableChildShaders(type == IconType::Robot ? m_robotBatchNode : m_spiderBatchNode);

    if (mech->m_paSprite->m_spriteParts) {
        shadeAnimSection(CCArrayExt<CCSpritePart*>(mech->m_paSprite->m_spriteParts),
            gradient.main, type, ColorType::Main, 100, false, false, animList);
    }
    if (mech->m_secondArray) {
        shadeAnimSection(CCArrayExt<CCSprite*>(mech->m_secondArray),
            gradient.secondary, type, ColorType::Secondary, 200, false, false, animList);
    }
    if (mech->m_glowSprite) {
        shadeAnimSection(mech->m_glowSprite->getChildrenExt<CCSprite*>(),
            gradient.glow, type, ColorType::Glow, 300, false, false, animList);
    }
    if (mech->m_extraSprite) {
        CCSprite* extra[] = {mech->m_extraSprite};
        shadeAnimSection(extra, gradient.white, type, ColorType::White, 400, false, true, animList);
    }
    if (mech->m_paSprite->m_spriteParts) {
        shadeAnimSection(CCArrayExt<CCSpritePart*>(mech->m_paSprite->m_spriteParts),
            gradient.line, type, ColorType::Line, 500, true, false, animList);
    }
    if (mech->m_secondArray) {
        shadeAnimSection(CCArrayExt<CCSprite*>(mech->m_secondArray),
            gradient.line, type, ColorType::Line, 600, true, false, animList);
    }
    if (mech->m_extraSprite) {
        CCSprite* extra[] = {mech->m_extraSprite};
        shadeAnimSection(extra, gradient.line, type, ColorType::Line, 700, true, true, animList);
    }
}

// robot and spider share the whole refresh dance; only the icon kind differs.
void GradientPlayerObject::refreshMech(IconType type) {
    GJBaseGameLayer* layer = m_gameLayer ? m_gameLayer : GJBaseGameLayer::get();
    if (!layer || (this != layer->m_player1 && this != layer->m_player2)) return;
    auto paint = [self = Ref(this), type] {
        auto* current = GJBaseGameLayer::get();
        if (!current || (self.data() != current->m_player1 && self.data() != current->m_player2)) return;
        if (self->getTag() == 0xCB04 || self->shouldReturn(current)) return;
        self->updateAnimSprite(type, GradientUtils::getGradient(type, self.data() == current->m_player2), self->m_fields.self());
    };
    if (!m_fields->m_animSpritesInitialized) {
        Loader::get()->queueInMainThread([paint, self = Ref(this)] {
            paint();
            self->m_fields->m_animSpritesInitialized = true;
        });
    } else {
        paint();
    }
}

void GradientPlayerObject::updateGradient() {
    if (!moduleEnabled()) return;
    GJBaseGameLayer* layer = GJBaseGameLayer::get();
    if (shouldReturn(layer)) return;
    // remote multiplayer dolls are playerobjects too; only the local pair wears this kit.
    if (layer && this != layer->m_player1 && this != layer->m_player2) return;
    auto f = m_fields.self();

    if (f->m_swingFlipLoaded)
        schedule(schedule_selector(GradientPlayerObject::updateFlip));

    IconType type = getIconType();
    if (type == f->m_previousType) return;
    f->m_previousType = type;

    Gradient gradient = GradientUtils::getGradient(type, m_isSecondPlayer);

    // compat with the ufo/ship/cube replacer mod: its sprites win while it
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
            if (CCSprite* live = this->*member) {
                live->setVisible(true);
                live->setOpacity(255);
            }
        }
        if (m_iconGlow) m_iconGlow->setVisible(m_hasGlow);
    }
}

bool GradientPlayerObject::init(int p0, int p1, GJBaseGameLayer* p2, CCLayer* p3, bool p4) {
    if (!PlayerObject::init(p0, p1, p2, p3, p4)) return false;
    auto f = m_fields.self();
    auto* loader = Loader::get();
    f->m_menuDollPatchLoaded = loader->isModLoaded("yellowcat98.custom_ufo_n_ship_cube");
    f->m_swingFlipLoaded = loader->isModLoaded("rgc_exists.swingcopter_flip");
    loader->queueInMainThread([self = Ref(this)] {
        if (!self || !self->getParent()) return;
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
