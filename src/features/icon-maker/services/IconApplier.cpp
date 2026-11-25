#include "IconApplier.hpp"

#include "MoreIconsBridge.hpp"
#include "../../../utils/ProjectStorage.hpp"
#include "../data/IconAnatomy.hpp"
#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../texture-studio/data/PlistParser.hpp"
#include "../../texture-studio/data/ImageBuffer.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"

#include <Geode/Geode.hpp>
#include <matjson.hpp>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;

namespace paimon::icon_maker {

namespace {

constexpr char const* kActiveKey = "icon-maker.active";

thread_local int s_vanillaDepth = 0;

// output basename suffix for the current texture quality, with the pixel
// scale the sheet was authored at.
std::string qualitySuffix() {
    float factor = CCDirector::sharedDirector()->getContentScaleFactor();
    if (factor >= 4.f) return "-uhd";
    if (factor >= 2.f) return "-hd";
    return "";
}

// Straight-alpha RGBA textures from SpritePreviewRenderer::createTexture must
// use GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA so they never render with dark or
// glowing premultiplied-alpha artifacts.
void applyStraightAlphaBlend(CCSprite* sprite, bool additive = false) {
    if (!sprite) return;
    if (additive) {
        sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    } else {
        sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    }
}

void recenterChild(CCSprite* parent, CCSprite* child) {
    if (!parent || !child) return;
    if (child->getParent() == parent) {
        auto const sz = parent->getContentSize();
        child->setPosition({sz.width * 0.5f, sz.height * 0.5f});
    }
}

void applySpriteFrameClean(CCSprite* target, CCSpriteFrame* frame, bool additive = false) {
    if (!target || !frame) return;
    target->setBatchNode(nullptr);
    target->setDisplayFrame(frame);
    applyStraightAlphaBlend(target, additive);
}

bool isProjectExactColors(std::string const& slotId) {
    if (slotId.empty()) return false;
    auto loaded = IconProjectStore::get().loadProject(slotId);
    return loaded && loaded.unwrap().exactColors;
}

}  // anonymous namespace

IconApplier::VanillaScope::VanillaScope() { ++s_vanillaDepth; }
IconApplier::VanillaScope::~VanillaScope() { --s_vanillaDepth; }

bool IconApplier::isVanillaSuppressed() {
    return s_vanillaDepth > 0;
}

IconApplier& IconApplier::get() {
    static IconApplier instance;
    return instance;
}

void IconApplier::loadSelection() {
    if (m_selectionLoaded) return;
    m_selectionLoaded = true;

    auto saved = Mod::get()->getSavedValue<matjson::Value>(kActiveKey, matjson::Value::object());
    if (!saved.isObject()) return;
    for (auto const& entry : saved) {
        auto key = entry.getKey();
        if (!key) continue;
        int typeRaw = 0;
        auto parsed = geode::utils::numFromString<int>(*key);
        if (!parsed) continue;
        typeRaw = parsed.unwrap();
        auto slotId = entry.asString().unwrapOr("");
        if (!slotId.empty() && anatomyFor(static_cast<IconType>(typeRaw)) &&
            paimon::storage::resolveProjectDirectory(IconPaths::slotsDir(), slotId)) {
            m_active[typeRaw] = slotId;
        }
    }
}

void IconApplier::saveSelection() {
    auto obj = matjson::Value::object();
    for (auto const& [typeRaw, slotId] : m_active) {
        obj[std::to_string(typeRaw)] = slotId;
    }
    Mod::get()->setSavedValue(kActiveKey, obj);
}

void IconApplier::setActive(IconType type, std::string slotId) {
    loadSelection();
    m_active[static_cast<int>(type)] = std::move(slotId);
    saveSelection();
}

void IconApplier::clearActive(IconType type) {
    loadSelection();
    m_active.erase(static_cast<int>(type));
    saveSelection();
}

std::string IconApplier::activeFor(IconType type) {
    loadSelection();
    auto it = m_active.find(static_cast<int>(type));
    return it != m_active.end() ? it->second : std::string{};
}

void IconApplier::onDeleteProject(std::string_view slotId, IconType type) {
    loadSelection();
    if (activeFor(type) == slotId) {
        clearActive(type);
    }
    for (auto it = m_active.begin(); it != m_active.end(); ) {
        if (it->second == slotId) it = m_active.erase(it);
        else ++it;
    }
    saveSelection();
    MoreIconsBridge::clearIcon(type, slotId);
    invalidate(slotId);
}

void IconApplier::unloadSheet(LoadedSheet& sheet) {
    auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    for (auto const& name : sheet.frameNames) {
        cache->removeSpriteFrameByName(name.c_str());
    }
    sheet.frameNames.clear();
    sheet.texture = nullptr;
    sheet.valid = false;
}

IconApplier::LoadedSheet* IconApplier::ensureLoaded(std::string const& slotId) {
    auto it = m_sheets.find(slotId);
    if (it != m_sheets.end()) {
        return it->second.valid ? &it->second : nullptr;
    }

    // old saved selections may predate the store's ID validation.
    if (!paimon::storage::resolveProjectDirectory(IconPaths::slotsDir(), slotId)) return nullptr;

    // insert first so repeated failures don't re-hit the disk every frame.
    auto& sheet = m_sheets[slotId];

    auto suffix = qualitySuffix();
    auto dir = IconPaths::outputDir(slotId);
    auto plistPath = dir / (slotId + suffix + ".plist");
    auto pngPath = dir / (slotId + suffix + ".png");

    auto parsed = ts::PlistParser::parseFile(plistPath);
    if (!parsed) {
        log::warn("[icon-maker] plist de '{}' ilegible: {}", slotId, parsed.unwrapErr());
        return nullptr;
    }
    auto atlas = ts::ImageBuffer::loadFromFile(pngPath);
    if (!atlas) {
        log::warn("[icon-maker] png de '{}' ilegible: {}", slotId, atlas.unwrapErr());
        return nullptr;
    }

    auto* texture = ts::SpritePreviewRenderer::createTexture(atlas.unwrap());
    if (!texture) {
        log::warn("[icon-maker] no se pudo crear la textura de '{}'", slotId);
        return nullptr;
    }

    auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    for (auto const& info : parsed.unwrap().frames) {
        auto* frame = CCSpriteFrame::createWithTexture(
            texture,
            CCRect(info.rectX, info.rectY, info.rectW, info.rectH),
            info.rotated,
            CCPoint(info.offsetX, info.offsetY),
            CCSize(info.sourceW, info.sourceH));
        if (!frame) continue;
        cache->addSpriteFrame(frame, info.name.c_str());
        sheet.frameNames.push_back(info.name);
    }

    if (sheet.frameNames.empty()) {
        log::warn("[icon-maker] la hoja de '{}' no aporto frames", slotId);
        return nullptr;
    }

    sheet.texture = texture;
    sheet.valid = true;
    return &sheet;
}

void IconApplier::invalidate(std::string_view slotId) {
    auto it = m_sheets.find(std::string(slotId));
    if (it == m_sheets.end()) return;
    unloadSheet(it->second);
    m_sheets.erase(it);
}

void IconApplier::onGLContextReload() {
    for (auto& [id, sheet] : m_sheets) {
        unloadSheet(sheet);
    }
    m_sheets.clear();
}

bool IconApplier::applySlotToSimplePlayer(SimplePlayer* player, IconType type,
                                          std::string const& slotId, bool forceApply) {
    if (!player || slotId.empty()) return false;
    auto const* def = anatomyFor(type);
    if (!def) return false;

    if (!forceApply && MoreIconsBridge::available()) return false;

    if (def->partCount > 1) {
        auto* robot = type == IconType::Robot
            ? player->m_robotSprite : player->m_spiderSprite;
        applyToRobotSprite(robot, type, slotId);
        return true;
    }

    auto* sheet = ensureLoaded(slotId);
    if (!sheet) return false;

    auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    auto frameFor = [&](char const* slotKey) -> CCSpriteFrame* {
        auto name = frameName(slotId, type, 0, slotKey);
        return cache->spriteFrameByName(name.c_str());
    };

    if (auto* mainFrame = frameFor("main")) {
        applySpriteFrameClean(player->m_firstLayer, mainFrame);
        if (player->m_firstLayer) player->m_firstLayer->setVisible(true);
    }

    if (player->m_secondLayer) {
        if (auto* secFrame = frameFor("secondary")) {
            applySpriteFrameClean(player->m_secondLayer, secFrame);
            player->m_secondLayer->setVisible(true);
        }
        recenterChild(player->m_firstLayer, player->m_secondLayer);
    }

    if (player->m_outlineSprite) {
        if (auto* glowFrame = frameFor("glow")) {
            applySpriteFrameClean(player->m_outlineSprite, glowFrame);
            // Respect the player's glow toggle instead of forcing glow visible!
            player->m_outlineSprite->setVisible(player->m_hasGlowOutline);
        }
    }

    if (player->m_detailSprite) {
        if (auto* extraFrame = frameFor("extra")) {
            applySpriteFrameClean(player->m_detailSprite, extraFrame);
            player->m_detailSprite->setVisible(true);
            recenterChild(player->m_firstLayer, player->m_detailSprite);
        } else {
            player->m_detailSprite->setVisible(false);
        }
    }

    if (type == IconType::Ufo && player->m_birdDome) {
        if (auto* domeFrame = frameFor("tertiary")) {
            applySpriteFrameClean(player->m_birdDome, domeFrame);
            player->m_birdDome->setVisible(true);
        }
        recenterChild(player->m_firstLayer, player->m_birdDome);
    }

    return true;
}

void IconApplier::onUpdatePlayerFrame(SimplePlayer* player, int iconId, IconType type) {
    if (!player || isVanillaSuppressed()) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    auto const* def = anatomyFor(type);
    if (!def) return;

    loadSelection();
    auto slotId = activeFor(type);

    bool oursEquipped = false;
    auto* gm = GameManager::get();
    if (gm && gm->activeIconForType(type) == iconId) {
        if (MoreIconsBridge::available()) {
            oursEquipped = MoreIconsBridge::isOurActive(type);
        } else {
            oursEquipped = !slotId.empty();
        }
    }
    if (oursEquipped) {
        Ref<SimplePlayer> ref = player;
        Loader::get()->queueInMainThread([ref, type]() {
            if (paimon::isRuntimeShuttingDown() || !ref) return;
            IconApplier::get().applyExactColors(ref, type);
        });
    }

    // with More Icons installed, selection and frame rendering belong to it.
    if (MoreIconsBridge::available()) return;

    if (slotId.empty()) return;
    if (!gm || gm->activeIconForType(type) != iconId) return;

    applySlotToSimplePlayer(player, type, slotId, false);
}

void IconApplier::applyToPlayerObject(PlayerObject* player, IconType type) {
    if (!player) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    auto const* def = anatomyFor(type);
    if (!def) return;

    // Only apply to the local player (never remote Globed / multiplayer dolls).
    if (auto* gameLayer = player->m_gameLayer ? player->m_gameLayer : GJBaseGameLayer::get()) {
        if (player != gameLayer->m_player1) return;
    }

    if (MoreIconsBridge::available()) {
        if (MoreIconsBridge::isOurActive(type)) {
            Ref<PlayerObject> ref = player;
            Loader::get()->queueInMainThread([ref, type]() {
                if (paimon::isRuntimeShuttingDown() || !ref) return;
                IconApplier::get().applyExactColorsToPlayerObject(ref, type);
            });
        }
        return;
    }

    loadSelection();
    auto slotId = activeFor(type);
    if (slotId.empty()) return;

    if (def->partCount > 1) {
        auto* robot = type == IconType::Robot
            ? static_cast<GJRobotSprite*>(player->m_robotSprite)
            : static_cast<GJRobotSprite*>(player->m_spiderSprite);
        applyToRobotSprite(robot, type, slotId);
    } else {
        auto* sheet = ensureLoaded(slotId);
        if (!sheet) return;

        auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
        auto frameFor = [&](char const* slotKey) -> CCSpriteFrame* {
            auto name = frameName(slotId, type, 0, slotKey);
            return cache->spriteFrameByName(name.c_str());
        };

        bool const isVehicle = type == IconType::Ship ||
                               type == IconType::Ufo ||
                               type == IconType::Jetpack;

        CCSprite* primary   = isVehicle ? player->m_vehicleSprite          : player->m_iconSprite;
        CCSprite* secondary = isVehicle ? player->m_vehicleSpriteSecondary : player->m_iconSpriteSecondary;
        CCSprite* glow      = isVehicle ? player->m_vehicleGlow            : player->m_iconGlow;
        CCSprite* extra     = isVehicle ? player->m_vehicleSpriteWhitener  : player->m_iconSpriteWhitener;

        if (auto* mainFrame = frameFor("main")) {
            applySpriteFrameClean(primary, mainFrame);
        }
        if (secondary) {
            if (auto* secFrame = frameFor("secondary")) {
                applySpriteFrameClean(secondary, secFrame);
            }
            recenterChild(primary, secondary);
        }
        if (glow) {
            if (auto* glowFrame = frameFor("glow")) {
                applySpriteFrameClean(glow, glowFrame);
            }
        }
        if (extra) {
            if (auto* extraFrame = frameFor("extra")) {
                applySpriteFrameClean(extra, extraFrame);
                extra->setVisible(true);
                recenterChild(primary, extra);
            } else {
                extra->setVisible(false);
            }
        }
        if (type == IconType::Ufo && player->m_birdVehicle) {
            if (auto* domeFrame = frameFor("tertiary")) {
                applySpriteFrameClean(player->m_birdVehicle, domeFrame);
            }
            recenterChild(primary, player->m_birdVehicle);
        }
    }

    Ref<PlayerObject> ref = player;
    Loader::get()->queueInMainThread([ref, type]() {
        if (paimon::isRuntimeShuttingDown() || !ref) return;
        IconApplier::get().applyExactColorsToPlayerObject(ref, type);
    });
}

void IconApplier::applyExactColorsForSlot(SimplePlayer* player, IconType type,
                                          std::string const& slotId) {
    if (!player || slotId.empty()) return;
    if (!isProjectExactColors(slotId)) return;

    constexpr cocos2d::ccColor3B kWhite{255, 255, 255};
    if (player->m_firstLayer)  player->m_firstLayer->setColor(kWhite);
    if (player->m_secondLayer) player->m_secondLayer->setColor(kWhite);
    if (player->m_birdDome)    player->m_birdDome->setColor(kWhite);

    auto const* def = anatomyFor(type);
    if (def && def->partCount > 1) {
        auto* robot = type == IconType::Robot
            ? player->m_robotSprite : player->m_spiderSprite;
        if (robot && robot->m_paSprite && robot->m_paSprite->m_spriteParts) {
            for (auto* part : CCArrayExt<CCSprite*>(robot->m_paSprite->m_spriteParts)) {
                if (part) part->setColor(kWhite);
            }
            if (robot->m_secondArray) {
                for (auto* second : CCArrayExt<CCSprite*>(robot->m_secondArray)) {
                    if (second) second->setColor(kWhite);
                }
            }
        }
    }
}

void IconApplier::applyExactColors(SimplePlayer* player, IconType type) {
    if (!player) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    std::string slotId;
    if (MoreIconsBridge::available()) {
        slotId = MoreIconsBridge::activeOursSlotId(type);
    } else {
        loadSelection();
        slotId = activeFor(type);
    }
    if (slotId.empty()) return;

    applyExactColorsForSlot(player, type, slotId);
}

void IconApplier::applyExactColorsToPlayerObject(PlayerObject* player, IconType type) {
    if (!player) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    std::string slotId;
    if (MoreIconsBridge::available()) {
        slotId = MoreIconsBridge::activeOursSlotId(type);
    } else {
        loadSelection();
        slotId = activeFor(type);
    }
    if (!isProjectExactColors(slotId)) return;

    constexpr cocos2d::ccColor3B kWhite{255, 255, 255};
    bool const isVehicle = type == IconType::Ship ||
                           type == IconType::Ufo ||
                           type == IconType::Jetpack;

    if (isVehicle) {
        if (player->m_vehicleSprite)          player->m_vehicleSprite->setColor(kWhite);
        if (player->m_vehicleSpriteSecondary) player->m_vehicleSpriteSecondary->setColor(kWhite);
        if (player->m_birdVehicle)            player->m_birdVehicle->setColor(kWhite);
    } else if (type == IconType::Robot || type == IconType::Spider) {
        auto* robot = type == IconType::Robot
            ? static_cast<GJRobotSprite*>(player->m_robotSprite)
            : static_cast<GJRobotSprite*>(player->m_spiderSprite);
        if (robot && robot->m_paSprite && robot->m_paSprite->m_spriteParts) {
            for (auto* part : CCArrayExt<CCSprite*>(robot->m_paSprite->m_spriteParts)) {
                if (part) part->setColor(kWhite);
            }
            if (robot->m_secondArray) {
                for (auto* second : CCArrayExt<CCSprite*>(robot->m_secondArray)) {
                    if (second) second->setColor(kWhite);
                }
            }
        }
    } else {
        if (player->m_iconSprite)          player->m_iconSprite->setColor(kWhite);
        if (player->m_iconSpriteSecondary) player->m_iconSpriteSecondary->setColor(kWhite);
    }
}

// transcribed from More Icons' updateRobotSprite (MIT): detach the batch node,
// point everything at our texture and swap each animated part's frames.
void IconApplier::applyToRobotSprite(GJRobotSprite* sprite, IconType type,
                                     std::string const& slotId) {
    if (!sprite) return;

    auto* sheet = ensureLoaded(slotId);
    if (!sheet || !sheet->texture) return;

    auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    auto frameFor = [&](int part, char const* slotKey) -> CCSpriteFrame* {
        auto name = frameName(slotId, type, part, slotKey);
        return cache->spriteFrameByName(name.c_str());
    };

    sprite->setBatchNode(nullptr);
    sprite->setTexture(sheet->texture);
    applyStraightAlphaBlend(sprite);

    auto* paSprite = sprite->m_paSprite;
    if (!paSprite || !paSprite->m_spriteParts) return;
    paSprite->setBatchNode(nullptr);
    paSprite->setTexture(sheet->texture);

    auto spriteParts = CCArrayExt<CCSprite*>(paSprite->m_spriteParts);
    auto* secondArray = sprite->m_secondArray;
    auto* glowChildren = sprite->m_glowSprite ? sprite->m_glowSprite->getChildren() : nullptr;
    auto* headSprite = sprite->m_headSprite;
    auto* extraSprite = sprite->m_extraSprite;

    for (unsigned int i = 0; i < spriteParts.size(); ++i) {
        auto* spritePart = spriteParts[i];
        if (!spritePart) continue;
        int tag = spritePart->getTag();

        if (auto* frame = frameFor(tag, "main")) {
            applySpriteFrameClean(spritePart, frame);
        }

        if (secondArray && i < static_cast<unsigned int>(secondArray->count())) {
            if (auto* secondSprite = typeinfo_cast<CCSprite*>(secondArray->objectAtIndex(i))) {
                if (auto* frame = frameFor(tag, "secondary")) {
                    applySpriteFrameClean(secondSprite, frame);
                    secondSprite->setPosition(spritePart->getContentSize() / 2.f);
                }
            }
        }

        if (glowChildren && i < glowChildren->count()) {
            if (auto* glowChild = typeinfo_cast<CCSprite*>(glowChildren->objectAtIndex(i))) {
                if (auto* frame = frameFor(tag, "glow")) {
                    applySpriteFrameClean(glowChild, frame);
                }
            }
        }

        if (spritePart == headSprite) {
            auto* extraFrame = frameFor(tag, "extra");
            if (extraFrame) {
                if (extraSprite) {
                    applySpriteFrameClean(extraSprite, extraFrame);
                } else {
                    extraSprite = CCSprite::createWithSpriteFrame(extraFrame);
                    applyStraightAlphaBlend(extraSprite);
                    sprite->m_extraSprite = extraSprite;
                    spritePart->addChild(extraSprite, 2);
                }
                extraSprite->setPosition(spritePart->getContentSize() / 2.f);
            }
            if (extraSprite) extraSprite->setVisible(extraFrame != nullptr);
        }
    }
}

}  // namespace paimon::icon_maker
