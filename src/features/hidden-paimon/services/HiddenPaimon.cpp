#include "HiddenPaimon.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/MenuLayer.hpp>

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../guide/services/PaimonGuideService.hpp"
#include "../../guide/ui/AnimatedPaimon.hpp"
#include "../../guide/ui/PaimonGuideChatPopup.hpp"
#include "../../../utils/Localization.hpp"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace paimon::hidden_paimon {

namespace {

std::mt19937& rng() {
    static std::mt19937 instance{std::random_device{}()};
    return instance;
}

float randRange(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng());
}

int randIndex(int count) {
    std::uniform_int_distribution<int> dist(0, count - 1);
    return dist(rng());
}

struct Spot {
    CCPoint pos;
    float   scale;
    float   rotation;
    bool    flipX;
    int     zOrder;
};

bool worldRect(CCNode* node, CCRect& out) {
    auto size = node->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return false;

    auto bl = node->convertToWorldSpace({0.f, 0.f});
    auto tr = node->convertToWorldSpace({size.width, size.height});
    out = CCRect(bl.x, bl.y, tr.x - bl.x, tr.y - bl.y);
    return out.size.width > 1.f && out.size.height > 1.f;
}

void collectButtons(CCNode* node, std::vector<CCMenuItem*>& out) {
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
        if (!child->isVisible()) continue;
        if (auto* item = typeinfo_cast<CCMenuItem*>(child)) {
            if (item->isEnabled()) out.push_back(item);
            continue;
        }
        collectButtons(child, out);
    }
}

// draw order is per top-level child of the layer, so a spot behind a button
// means one step under whatever menu that button lives in.
int zBehind(CCNode* anchor, CCLayer* layer) {
    auto* node = anchor;
    while (node && node->getParent() != layer) {
        node = node->getParent();
    }
    return node ? node->getZOrder() - 1 : -1;
}

float overlap(CCRect const& a, CCRect const& b) {
    float w = std::min(a.getMaxX(), b.getMaxX()) - std::max(a.getMinX(), b.getMinX());
    float h = std::min(a.getMaxY(), b.getMaxY()) - std::max(a.getMinY(), b.getMinY());
    if (w <= 0.f || h <= 0.f) return 0.f;
    return w * h;
}

// tucked under the button: her crown stays covered and only the face shows.
bool hideUnder(CCLayer* layer, CCNode* anchor, CCRect const& rect, float spriteH,
               Spot& out, CCRect& face) {
    auto win = CCDirector::get()->getWinSize();

    float height = std::clamp(rect.size.height * 1.15f, 26.f, 52.f);
    float peek = randRange(0.60f, 0.74f);
    float cy = rect.getMinY() + height * (0.5f - peek);
    if (cy - height * 0.5f < 4.f) return false;

    float cx = std::clamp(
        rect.getMidX() + randRange(-0.3f, 0.3f) * rect.size.width,
        height * 0.4f,
        win.width - height * 0.4f
    );

    face = CCRect(
        cx - height * 0.35f,
        cy - height * 0.5f,
        height * 0.7f,
        height * peek
    );

    out.pos = layer->convertToNodeSpace({cx, cy});
    out.scale = height / spriteH;
    out.flipX = cx > win.width * 0.5f;
    out.rotation = (out.flipX ? -1.f : 1.f) * randRange(4.f, 10.f);
    out.zOrder = zBehind(anchor, layer);
    return true;
}

// guide mode: she stops hiding and pops up behind the hub button.
bool perchAbove(CCLayer* layer, CCNode* anchor, CCRect const& rect, float spriteH, Spot& out) {
    auto win = CCDirector::get()->getWinSize();

    float height = std::clamp(rect.size.height * 1.35f, 34.f, 62.f);
    float cy = rect.getMaxY() + height * 0.28f;
    if (cy + height * 0.5f > win.height - 4.f) return false;

    out.pos = layer->convertToNodeSpace({rect.getMidX(), cy});
    out.scale = height / spriteH;
    out.flipX = false;
    out.rotation = 0.f;
    out.zOrder = zBehind(anchor, layer);
    return true;
}

bool pickSpot(CCLayer* layer, bool guideOn, float spriteH, Spot& out) {
    if (guideOn) {
        if (auto* hub = layer->getChildByIDRecursive("paimon-hub-btn"_spr)) {
            CCRect rect;
            if (worldRect(hub, rect) && perchAbove(layer, hub, rect, spriteH, out)) {
                return true;
            }
        }
    }

    std::vector<CCMenuItem*> buttons;
    collectButtons(layer, buttons);

    std::vector<std::pair<CCNode*, CCRect>> rects;
    for (auto* button : buttons) {
        CCRect rect;
        if (worldRect(button, rect)) rects.emplace_back(button, rect);
    }

    std::vector<Spot> spots;
    for (auto const& [button, rect] : rects) {
        if (rect.size.height < 20.f || rect.size.height > 160.f) continue;
        if (rect.size.width < 20.f || rect.size.width > 220.f) continue;

        Spot candidate;
        CCRect face;
        if (!hideUnder(layer, button, rect, spriteH, candidate, face)) continue;

        // neighbour covering the peeking part would leave her invisible.
        float area = face.size.width * face.size.height;
        bool buried = false;
        for (auto const& [other, otherRect] : rects) {
            if (other == button) continue;
            if (overlap(face, otherRect) > area * 0.45f) {
                buried = true;
                break;
            }
        }
        if (!buried) spots.push_back(candidate);
    }
    if (spots.empty()) return false;

    out = spots[randIndex(static_cast<int>(spots.size()))];
    return true;
}

void playLater(CCNode* host, float delay, char const* file, float pitch, float volume) {
    host->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CallFuncExt::create([file, pitch, volume] {
            if (auto* fmod = FMODAudioEngine::sharedEngine()) {
                fmod->playEffect(file, pitch, 1.f, volume);
            }
        }),
        nullptr
    ));
}

// variety from pitching the two stock blast files; crack leads so the pair never phase-aligns.
void playBlast(CCNode* host) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    engine->playEffect("explode_11.ogg", randRange(1.35f, 1.75f), 1.f, randRange(0.40f, 0.55f));
    playLater(host, 0.03f, "explode_11.ogg", randRange(0.55f, 0.72f), 1.f);
    playLater(host, randRange(0.08f, 0.16f), "magicExplosion.ogg",
              randRange(0.62f, 0.88f), randRange(0.35f, 0.50f));

    if (randRange(0.f, 1.f) < 0.3f) {
        playLater(host, randRange(0.22f, 0.34f), "explode_11.ogg", randRange(0.60f, 0.80f), 0.55f);
    }
}

void spawnBlastFX(CCNode* host, CCPoint pos) {
    static char const* kBursts[] = {
        "explodeEffect.plist",
        "explodeEffectGrav.plist",
        "explodeEffectVortex.plist",
    };

    if (auto* burst = CCParticleSystemQuad::create(kBursts[randIndex(3)], false)) {
        burst->setPosition(pos);
        burst->setPositionType(kCCPositionTypeGrouped);
        burst->setAutoRemoveOnFinish(true);
        burst->setScale(1.4f);
        host->addChild(burst, 100);
    }

    if (auto* debris = CCParticleSystemQuad::create("glassDestroy01.plist", false)) {
        debris->setPosition(pos);
        debris->setPositionType(kCCPositionTypeGrouped);
        debris->setAutoRemoveOnFinish(true);
        debris->setScale(1.8f);
        host->addChild(debris, 100);
    }

    auto* flash = CCSprite::create("paim_progGlow.png"_spr);
    if (!flash) return;

    flash->setPosition(pos);
    flash->setColor({255, 214, 150});
    flash->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    flash->setScale(0.12f);
    flash->runAction(CCSequence::create(
        CCSpawn::create(
            CCEaseOut::create(CCScaleTo::create(0.26f, 0.8f), 2.f),
            CCFadeOut::create(0.26f),
            nullptr
        ),
        CCCallFunc::create(flash, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
    host->addChild(flash, 101);
}

void burst(CCMenuItemSpriteExtra* button) {
    auto* menu = button->getParent();
    auto* layer = menu ? menu->getParent() : nullptr;
    if (!layer) return;

    auto world = menu->convertToWorldSpace(button->getPosition());
    playBlast(layer);
    spawnBlastFX(layer, layer->convertToNodeSpace(world));

    if (auto* image = button->getNormalImage()) {
        image->runAction(CCFadeOut::create(0.28f));
    }

    WeakRef<CCNode> weakMenu(menu);
    button->runAction(CCSequence::create(
        CCSpawn::create(
            CCScaleTo::create(0.3f, 0.f),
            CCRotateBy::create(0.3f, 360.f),
            nullptr
        ),
        CallFuncExt::create([weakMenu] {
            if (auto menu = weakMenu.lock()) menu->removeFromParent();
        }),
        nullptr
    ));
}

void addPeekLoop(CCNode* button) {
    button->runAction(CCRepeatForever::create(CCSequence::create(
        CCDelayTime::create(randRange(1.f, 3.f)),
        CCEaseSineInOut::create(CCMoveBy::create(1.6f, {0.f, -2.5f})),
        CCDelayTime::create(randRange(1.5f, 3.5f)),
        CCEaseSineInOut::create(CCMoveBy::create(1.4f, {0.f, 2.5f})),
        nullptr
    )));
}

void addGuideOverlay(CCMenuItemSpriteExtra* button, float spriteScale) {
    auto* animated = guide::AnimatedPaimon::create(spriteScale);
    if (!animated) return;

    animated->setLively(true);
    animated->play(guide::AnimatedPaimon::Animation::Idle);
    animated->setAnchorPoint({0.5f, 0.5f});
    animated->setPosition(button->getContentSize() * 0.5f);
    animated->setID("paimon-hidden-animated"_spr);
    button->addChild(animated, 1);

    WeakRef<guide::AnimatedPaimon> weakAnim(animated);
    auto bubbleTick = CallFuncExt::create([weakAnim] {
        auto anim = weakAnim.lock();
        if (!anim) return;
        if (randRange(0.f, 1.f) >= 0.3f) return;
        anim->showBubble(Localization::get().getString("pai.guide.bubble"), 3.f);
    });
    animated->runAction(CCRepeatForever::create(CCSequence::create(
        CCDelayTime::create(30.f),
        bubbleTick,
        nullptr
    )));
}

} // namespace

void attach(CCLayer* layer) {
    if (!layer) return;

    static std::string const menuId = "paimon-hidden-menu"_spr;
    if (auto* previous = layer->getChildByID(menuId)) {
        previous->removeFromParent();
    }

    bool guideOn = guide::PaimonGuideService::get().isEnabled();
    if (!guideOn && !modules::isEnabled(kModuleId)) return;

    auto* sprite = CCSprite::create("paim_Paimon.png"_spr);
    if (!sprite) return;

    float spriteH = sprite->getContentSize().height;
    if (spriteH <= 0.f) return;

    Spot spot;
    if (!pickSpot(layer, guideOn, spriteH, spot)) return;

    sprite->setScale(spot.scale);
    sprite->setFlipX(spot.flipX);
    sprite->setOpacity(guideOn ? 0 : 205);

    auto* button = CCMenuItemExt::createSpriteExtra(sprite, [guideOn](CCMenuItemSpriteExtra* sender) {
        if (guideOn) {
            if (auto* popup = guide::PaimonGuideChatPopup::create()) popup->show();
            return;
        }
        burst(sender);
    });
    if (!button) return;

    button->setRotation(spot.rotation);
    button->setID("paimon-hidden-btn"_spr);

    auto* menu = CCMenu::create();
    menu->setPosition(spot.pos);
    menu->setContentSize(sprite->getScaledContentSize());
    menu->setID(menuId);
    menu->addChild(button);

// hitbox hides under the button, so one step below menu priority keeps the
// button clickable and leaves her only the peeking part.
    menu->setTouchPriority(kCCMenuHandlerPriority + 1);

    if (guideOn) {
        addGuideOverlay(button, spot.scale);
    } else {
        addPeekLoop(button);
    }

    layer->addChild(menu, spot.zOrder);
}

void refresh(CCNode* scene) {
    if (!scene) return;

    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (auto* menuLayer = typeinfo_cast<MenuLayer*>(child)) {
            attach(menuLayer);
            return;
        }
    }
}

} // namespace paimon::hidden_paimon
