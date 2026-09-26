#include "IconTryPopup.hpp"

#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "../data/IconAnatomy.hpp"
#include "../data/IconPalettes.hpp"
#include "../engine/PieceRenderer.hpp"
#include "../persist/IconPaths.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/SimplePlayer.hpp>

#include <algorithm>
#include <utility>
#include <vector>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;
namespace kit = paimon::icon_maker::gdkit;
namespace mkui = paimon::icon_maker::ui;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 270.f;
constexpr float kStageH = 138.f;

constexpr float kSizes[] = {0.55f, 0.9f, 1.5f};
constexpr char const* kSizeNames[] = {"Mini", "En juego", "Garaje"};

// what GD multiplies over each icon layer.
ccColor3B tintFor(std::string_view slotKey, bool exactColors) {
    if (exactColors) return {255, 255, 255};

    auto colors = playerColors();
    if (colors.size() < 3) return {255, 255, 255};
    if (slotKey == "main")      return colors[0];
    if (slotKey == "secondary") return colors[1];
    if (slotKey == "glow")      return colors[2];
    return {255, 255, 255};
}

std::string zoneKeyOf(std::string const& storageKey) {
    auto dot = storageKey.find('.');
    return dot == std::string::npos ? storageKey : storageKey.substr(dot + 1);
}

}  // anonymous namespace

IconTryPopup* IconTryPopup::create(IconProject project) {
    auto* p = new IconTryPopup();
    if (p->init(std::move(project))) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

bool IconTryPopup::init(IconProject project) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    m_project = std::move(project);
    m_exactColors = m_project.exactColors;
    setTitle(fmt::format("Probar: {}", m_project.name).c_str());
    setID("icon-maker-try"_spr);

    auto const size = m_mainLayer->getContentSize();
    float const stageW = size.width - 30.f;
    float const stageY = size.height - 44.f - kStageH;

    m_stageBg = CCLayerColor::create(ccc4(18, 26, 52, 255));
    m_stageBg->setContentSize({stageW, kStageH});
    m_stageBg->setPosition({15.f, stageY});
    m_mainLayer->addChild(m_stageBg, 1);

    if (auto* checker = mkui::checkerTexture()) {
        m_stageChecker = CCSprite::createWithTexture(checker);
        if (m_stageChecker) {
            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT};
            checker->setTexParameters(&params);
            m_stageChecker->setTextureRect({0.f, 0.f, stageW, kStageH});
            m_stageChecker->setAnchorPoint({0.f, 0.f});
            m_stageChecker->setPosition({15.f, stageY});
            m_stageChecker->setVisible(false);
            m_mainLayer->addChild(m_stageChecker, 1);
        }
    }

    if (auto* frame = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
        frame->setContentSize({stageW + 6.f, kStageH + 6.f});
        frame->setAnchorPoint({0.f, 0.f});
        frame->setPosition({12.f, stageY - 3.f});
        m_mainLayer->addChild(frame, 2);
    }

    m_stage = CCNode::create();
    m_stage->setContentSize({stageW, kStageH});
    m_stage->setPosition({15.f, stageY});
    m_mainLayer->addChild(m_stage, 3);

    // vanilla gamemode icon alongside as a size reference.
    m_vanilla = SimplePlayer::create(1);
    if (m_vanilla) {
        if (auto* gm = GameManager::get()) {
            m_vanilla->updatePlayerFrame(
                gm->activeIconForType(m_project.type), m_project.type);
            m_vanilla->setColor(gm->colorForIdx(gm->getPlayerColor()));
            m_vanilla->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
        }
        m_vanilla->setPosition({stageW - 32.f, kStageH / 2.f});
        m_vanilla->setScale(0.9f);
        m_vanilla->setOpacity(150);
        m_stage->addChild(m_vanilla, 10);
    }

    m_statusLabel = CCLabelBMFont::create("Dibujando el icono...", "chatFont.fnt");
    m_statusLabel->setScale(0.5f);
    m_statusLabel->setColor(kit::kDescColor);
    m_statusLabel->setPosition({size.width / 2.f, stageY - 12.f});
    m_mainLayer->addChild(m_statusLabel, 4);

    m_controlsHost = CCNode::create();
    m_controlsHost->setPosition({15.f, 12.f});
    m_mainLayer->addChild(m_controlsHost, 4);

    refreshControls();
    kickRender();
    return true;
}

void IconTryPopup::kickRender() {
    auto const* def = anatomyFor(m_project.type);
    if (!def) return;

    m_busy = true;

    IconProject snapshot = m_project;
    int const canvasSize = def->canvasUhd;
    auto imagesDir = IconPaths::imagesDir(snapshot.id);

    std::vector<std::pair<std::string, int>> keys;
    int const firstPart = def->partCount > 1 ? 1 : 0;
    int const lastPart = def->partCount > 1 ? def->partCount : 0;
    for (int part = firstPart; part <= lastPart; ++part) {
        for (char const* key : {"glow", "tertiary", "secondary", "main", "extra"}) {
            for (auto const& slot : def->slots) {
                if (slot.key != key) continue;
                if (def->partCount > 1 && part > 1 && slot.key == "extra") continue;
                keys.emplace_back(slotStorageKey(part, key), part);
            }
        }
    }

    Ref<IconTryPopup> self = this;
    paimon::ThreadTracker::get().spawn(
        [self, snapshot, keys, canvasSize, imagesDir]() {
            geode::utils::thread::setName("icon-maker-try");

            std::vector<std::pair<std::string, ts::ImageBuffer>> composites;
            composites.reserve(keys.size());
            for (auto const& [key, part] : keys) {
                (void)part;
                if (auto r = PieceRenderer::renderSlot(
                        snapshot, key, canvasSize, imagesDir)) {
                    composites.emplace_back(key, r.unwrap());
                }
            }

            Loader::get()->queueInMainThread(
                [self, keys, composites = std::move(composites)]() mutable {
                    if (paimon::isRuntimeShuttingDown() || !self) return;

                    self->m_zones.clear();
                    for (auto& [key, image] : composites) {
                        RenderedZone zone;
                        zone.slotKey = key;
                        for (auto const& [candidate, part] : keys) {
                            if (candidate == key) { zone.part = part; break; }
                        }
                        zone.texture = ts::SpritePreviewRenderer::createTexture(image);
                        if (zone.texture) self->m_zones.push_back(std::move(zone));
                    }
                    self->m_busy = false;
                    self->rebuildPreview();
                });
        });
}

void IconTryPopup::rebuildPreview() {
    if (!m_stage) return;

    // collect before touching anything: deleting while iterating the child array is trouble.
    std::vector<CCNode*> stale;
    for (auto* child : CCArrayExt<CCNode*>(m_stage->getChildren())) {
        if (child != m_vanilla) stale.push_back(child);
    }
    for (auto* child : stale) child->removeFromParent();

    if (m_statusLabel) {
        m_statusLabel->setString(m_busy
            ? "Dibujando el icono..."
            : (m_zones.empty() ? "Este icono todavia no tiene nada dibujado."
                               : "Asi se vera en el juego."));
    }
    if (m_busy || m_zones.empty()) return;

    auto const* def = anatomyFor(m_project.type);
    if (!def) return;

    float const stageW = m_stage->getContentSize().width;
    float const stageH = m_stage->getContentSize().height;
    int const parts = def->partCount > 1 ? def->partCount : 1;

    // robot/spider preview per part: the animated skeleton adds nothing for color/size.
    float const slotW = (stageW - 76.f) / static_cast<float>(parts);
    float const base = kSizes[std::clamp(m_sizeIndex, 0, 2)] * 52.f;

    for (int index = 0; index < parts; ++index) {
        int const part = def->partCount > 1 ? index + 1 : 0;
        float const cx = 10.f + slotW * (static_cast<float>(index) + 0.5f);

        for (auto const& zone : m_zones) {
            if (zone.part != part || !zone.texture) continue;
            auto const key = zoneKeyOf(zone.slotKey);
            if (key == "glow" && !m_showGlow) continue;

            auto* sprite = CCSprite::createWithTexture(zone.texture);
            if (!sprite) continue;
            float const longest = std::max(sprite->getContentSize().width,
                                           sprite->getContentSize().height);
            if (longest > 0.f) sprite->setScale(base * 2.f / longest);
            sprite->setPosition({cx, stageH / 2.f});
            sprite->setColor(tintFor(key, m_exactColors));
            m_stage->addChild(sprite, key == "glow" ? 1 : 2);
        }

        if (parts > 1) {
            auto* label = CCLabelBMFont::create(
                fmt::format("P{}", part).c_str(), "chatFont.fnt");
            label->setScale(0.4f);
            label->setColor(kit::kDescColor);
            label->setPosition({cx, 9.f});
            m_stage->addChild(label, 5);
        }
    }
}

void IconTryPopup::refreshControls() {
    if (!m_controlsHost) return;
    m_controlsHost->removeAllChildren();

    float const width = m_mainLayer->getContentSize().width - 30.f;

    std::vector<CCNode*> rows;
    rows.push_back(kit::makeToggleRow(width, "Colores reales",
        "Con esto puesto se ven los colores que pintaste; sin el, los tuyos.",
        m_exactColors,
        [this](bool value) {
            m_exactColors = value;
            rebuildPreview();
        }));
    rows.push_back(kit::makeSelectRow(width, "Tamano", nullptr,
        {kSizeNames[0], kSizeNames[1], kSizeNames[2]}, m_sizeIndex,
        [this](int index) {
            m_sizeIndex = index;
            rebuildPreview();
        }));

    float y = 0.f;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        auto* row = *it;
        if (!row) continue;
        row->setAnchorPoint({0.f, 0.f});
        row->setPosition({0.f, y});
        m_controlsHost->addChild(row);
        y += row->getContentSize().height + 4.f;
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(
        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    m_controlsHost->addChild(menu, 5);

    float x = width;
    auto addToggleButton = [&](std::string text, std::function<void()> action) {
        auto* spr = ButtonSprite::create(text.c_str(), "goldFont.fnt",
                                         "GJ_button_04.png", 0.8f);
        if (!spr) return;
        spr->setScale(0.55f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [action](CCMenuItemSpriteExtra*) { if (action) action(); });
        x -= btn->getScaledContentSize().width / 2.f;
        btn->setPosition({x, y + 10.f});
        menu->addChild(btn);
        x -= btn->getScaledContentSize().width / 2.f + 5.f;
    };

    addToggleButton(m_backgroundMode == 1 ? "Fondo claro"
                  : m_backgroundMode == 2 ? "Fondo cuadros" : "Fondo oscuro",
        [this] {
            m_backgroundMode = (m_backgroundMode + 1) % 3;
            if (m_stageBg) {
                m_stageBg->setVisible(m_backgroundMode != 2);
                m_stageBg->setColor(m_backgroundMode == 1
                    ? ccColor3B{226, 229, 238} : ccColor3B{18, 26, 52});
            }
            if (m_stageChecker) m_stageChecker->setVisible(m_backgroundMode == 2);
            refreshControls();
        });
    addToggleButton(m_showGlow ? "Brillo: si" : "Brillo: no",
        [this] {
            m_showGlow = !m_showGlow;
            rebuildPreview();
            refreshControls();
        });
}

}  // namespace paimon::icon_maker
