#include "IconApplier.hpp"

#include "MoreIconsBridge.hpp"
#include "../data/IconAnatomy.hpp"
#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../../../core/Settings.hpp"
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

// Output basename suffix for the current texture quality, with the pixel
// scale the sheet was authored at.
std::string qualitySuffix() {
    float factor = CCDirector::sharedDirector()->getContentScaleFactor();
    if (factor >= 4.f) return "-uhd";
    if (factor >= 2.f) return "-hd";
    return "";
}

}  // anonymous namespace

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
        if (!slotId.empty() && anatomyFor(static_cast<IconType>(typeRaw))) {
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

    // Insert first so repeated failures don't re-hit the disk every frame.
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

void IconApplier::onUpdatePlayerFrame(SimplePlayer* player, int iconId, IconType type) {
    if (!player) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    auto const* def = anatomyFor(type);
    if (!def) return;

    loadSelection();
    auto slotId = activeFor(type);

    // Exact colors apply on both paths (MoreIcons draws the frames, GD still
    // tints them). Deferred a frame so it lands after the init-flow setColors.
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
            if (ref) IconApplier::get().applyExactColors(ref, type);
        });
    }

    // With MoreIcons installed, selection and frame rendering belong to it.
    if (MoreIconsBridge::available()) return;

    if (slotId.empty()) return;

    // Only the equipped icon gets replaced; garage lists keep vanilla art.
    if (!gm || gm->activeIconForType(type) != iconId) return;

    // Robot/spider render through a GJRobotSprite child.
    if (def->partCount > 1) {
        auto* robot = type == IconType::Robot
            ? player->m_robotSprite : player->m_spiderSprite;
        applyToRobotSprite(robot, type, slotId);
        return;
    }

    auto* sheet = ensureLoaded(slotId);
    if (!sheet) return;

    auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    auto setFrame = [&](CCSprite* target, char const* slotKey, bool hideIfMissing) {
        if (!target) return;
        auto name = frameName(slotId, type, 0, slotKey);
        auto* frame = cache->spriteFrameByName(name.c_str());
        if (frame) {
            target->setDisplayFrame(frame);
            target->setVisible(true);
        } else if (hideIfMissing) {
            target->setVisible(false);
        }
    };

    setFrame(player->m_firstLayer, "main", false);
    setFrame(player->m_secondLayer, "secondary", false);
    setFrame(player->m_outlineSprite, "glow", false);
    setFrame(player->m_detailSprite, "extra", true);
    if (type == IconType::Ufo) {
        setFrame(player->m_birdDome, "tertiary", false);
    }
}

void IconApplier::applyExactColors(SimplePlayer* player, IconType type) {
    if (!player) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    // Which of our icons is showing?
    std::string slotId;
    if (MoreIconsBridge::available()) {
        slotId = MoreIconsBridge::activeOursSlotId(type);
    } else {
        loadSelection();
        slotId = activeFor(type);
    }
    if (slotId.empty()) return;

    auto loaded = IconProjectStore::get().loadProject(slotId);
    if (!loaded || !loaded.unwrap().exactColors) return;

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

// Transcribed from MoreIcons' updateRobotSprite (MIT): detach the batch node,
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

        spritePart->setBatchNode(nullptr);
        if (auto* frame = frameFor(tag, "main")) {
            spritePart->setDisplayFrame(frame);
        }

        if (secondArray && i < static_cast<unsigned int>(secondArray->count())) {
            if (auto* secondSprite = typeinfo_cast<CCSprite*>(secondArray->objectAtIndex(i))) {
                secondSprite->setBatchNode(nullptr);
                if (auto* frame = frameFor(tag, "secondary")) {
                    secondSprite->setDisplayFrame(frame);
                    secondSprite->setPosition(spritePart->getContentSize() / 2.f);
                }
            }
        }

        if (glowChildren && i < glowChildren->count()) {
            if (auto* glowChild = typeinfo_cast<CCSprite*>(glowChildren->objectAtIndex(i))) {
                glowChild->setBatchNode(nullptr);
                if (auto* frame = frameFor(tag, "glow")) {
                    glowChild->setDisplayFrame(frame);
                }
            }
        }

        if (spritePart == headSprite) {
            auto* extraFrame = frameFor(tag, "extra");
            if (extraFrame) {
                if (extraSprite) {
                    extraSprite->setBatchNode(nullptr);
                    extraSprite->setDisplayFrame(extraFrame);
                } else {
                    extraSprite = CCSprite::createWithSpriteFrame(extraFrame);
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
