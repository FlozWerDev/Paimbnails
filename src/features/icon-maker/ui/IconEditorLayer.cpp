#include "IconEditorLayer.hpp"

#include "GradientEditorPopup.hpp"
#include "IconActionSheet.hpp"
#include "IconHelpPopup.hpp"
#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "IconNamePopup.hpp"
#include "TemplatePickerPopup.hpp"
#include "../data/IconAnatomy.hpp"
#include "../data/IconPalettes.hpp"
#include "../engine/PieceRenderer.hpp"
#include "../engine/TemplateExtractor.hpp"
#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../services/IconBuildService.hpp"
#include "../services/IconThumbs.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <system_error>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;
namespace kit = paimon::icon_maker::gdkit;
namespace mkui = paimon::icon_maker::ui;

namespace paimon::icon_maker {

// Each zone is a separate sprite for hit-testing and selection fades.
class EditorCanvas : public CCLayer {
public:
    using DragCallback = std::function<void(float dxFraction, float dyFraction)>;
    using PickCallback = std::function<void(std::string const& zoneKey)>;

    struct Zone {
        std::string key;
        ts::ImageBuffer pixels;
    };

    static EditorCanvas* create(float side, DragCallback onDrag, PickCallback onPick) {
        auto* ret = new EditorCanvas();
        if (ret->init(side, std::move(onDrag), std::move(onPick))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void setZones(std::vector<Zone> zones, std::string const& activeKey) {
        for (auto* sprite : m_sprites) {
            if (sprite) sprite->removeFromParent();
        }
        m_sprites.clear();
        m_zones = std::move(zones);

        for (auto const& zone : m_zones) {
            CCSprite* sprite = nullptr;
            if (!zone.pixels.empty()) {
                if (auto* texture = ts::SpritePreviewRenderer::createTexture(zone.pixels)) {
                    sprite = CCSprite::createWithTexture(texture);
                }
            }
            if (sprite) {
                sprite->setPosition({m_side / 2.f, m_side / 2.f});
                sprite->setScale(m_side / static_cast<float>(zone.pixels.width()));
                addChild(sprite, 1);
            }
            m_sprites.push_back(sprite);
        }
        setActiveZone(activeKey);
    }

    void setActiveZone(std::string const& key) {
        m_activeKey = key;
        for (std::size_t i = 0; i < m_sprites.size(); ++i) {
            if (!m_sprites[i]) continue;
            bool active = m_zones[i].key == key;
            m_sprites[i]->setVisible(!m_isolate || active);
            m_sprites[i]->setOpacity(active ? 255 : 78);
        }
    }

    void setIsolate(bool isolate) {
        m_isolate = isolate;
        setActiveZone(m_activeKey);
    }

    void setBackgroundMode(int mode) {
        m_backgroundMode = mode;
        if (m_flatBg) {
            m_flatBg->setVisible(mode != 2);
            m_flatBg->setColor(mode == 1 ? ccColor3B{226, 229, 238} : ccColor3B{18, 26, 52});
        }
        if (m_checkerBg) m_checkerBg->setVisible(mode == 2);
    }

    void setGuideVisible(bool visible) {
        if (m_guide) m_guide->setVisible(visible);
    }

protected:
    bool init(float side, DragCallback onDrag, PickCallback onPick) {
        if (!CCLayer::init()) return false;
        m_side = side;
        m_onDrag = std::move(onDrag);
        m_onPick = std::move(onPick);

        setContentSize({side, side});
        setAnchorPoint({0.5f, 0.5f});
        ignoreAnchorPointForPosition(false);

        m_flatBg = CCLayerColor::create(ccc4(18, 26, 52, 255));
        m_flatBg->setContentSize({side, side});
        addChild(m_flatBg, -4);

        if (auto* checker = mkui::checkerTexture()) {
            m_checkerBg = CCSprite::createWithTexture(checker);
            if (m_checkerBg) {
                ccTexParams params{GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT};
                checker->setTexParameters(&params);
                m_checkerBg->setTextureRect({0.f, 0.f, side, side});
                m_checkerBg->setAnchorPoint({0.f, 0.f});
                m_checkerBg->setPosition({0.f, 0.f});
                m_checkerBg->setVisible(false);
                addChild(m_checkerBg, -3);
            }
        }

        // Art inside this box uses vanilla in-game proportions.
        m_guide = CCLayerColor::create(ccc4(255, 255, 255, 16));
        float guideSide = side * 0.5f;
        m_guide->setContentSize({guideSide, guideSide});
        m_guide->setPosition({(side - guideSide) / 2.f, (side - guideSide) / 2.f});
        addChild(m_guide, -2);

        setTouchEnabled(true);
        setTouchMode(kCCTouchesOneByOne);
        return true;
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        auto local = convertTouchToNodeSpace(touch);
        if (local.x < 0.f || local.y < 0.f || local.x > m_side || local.y > m_side) {
            return false;
        }
        m_startTouch = local;
        m_lastTouch = local;
        m_moved = 0.f;
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        auto local = convertTouchToNodeSpace(touch);
        float dx = local.x - m_lastTouch.x;
        float dy = local.y - m_lastTouch.y;
        m_lastTouch = local;
        m_moved += std::fabs(dx) + std::fabs(dy);
        // Below this threshold the gesture may still become a tap.
        if (m_moved > kTapSlop && m_onDrag) {
            m_onDrag(dx / (m_side / 2.f), dy / (m_side / 2.f));
        }
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override {
        if (m_moved > kTapSlop) return;
        auto key = zoneAt(m_startTouch);
        if (!key.empty() && m_onPick) m_onPick(key);
    }

private:
    static constexpr float kTapSlop = 6.f;

    // Select the topmost zone with a solid pixel under the point.
    std::string zoneAt(CCPoint const& local) const {
        for (int i = static_cast<int>(m_zones.size()) - 1; i >= 0; --i) {
            auto const& pixels = m_zones[static_cast<std::size_t>(i)].pixels;
            if (pixels.empty()) continue;
            int px = static_cast<int>(local.x / m_side * static_cast<float>(pixels.width()));
            // Image rows run top-down; node space runs bottom-up.
            int py = static_cast<int>((1.f - local.y / m_side) * static_cast<float>(pixels.height()));
            if (px < 0 || py < 0 || px >= pixels.width() || py >= pixels.height()) continue;
            if (pixels.at(px, py).a > 40) return m_zones[static_cast<std::size_t>(i)].key;
        }
        return {};
    }

    float m_side = 200.f;
    int m_backgroundMode = 0;
    bool m_isolate = false;
    std::string m_activeKey;

    std::vector<Zone> m_zones;
    std::vector<CCSprite*> m_sprites;

    CCLayerColor* m_flatBg = nullptr;
    CCSprite* m_checkerBg = nullptr;
    CCLayerColor* m_guide = nullptr;

    CCPoint m_startTouch{};
    CCPoint m_lastTouch{};
    float m_moved = 0.f;

    DragCallback m_onDrag;
    PickCallback m_onPick;
};

namespace {

constexpr float kMargin = 7.f;
constexpr float kTopBarH = 30.f;
constexpr float kPanelGap = 7.f;
constexpr float kPanelInset = 8.f;
constexpr float kMinWorkspaceW = 170.f;
constexpr float kMinInspectorW = 235.f;
constexpr float kMaxInspectorW = 326.f;

struct Layout {
    float bodyTop;
    float panelX;
    float panelW;
    float panelH;
    float workspaceX;
    float workspaceW;
    float workspaceCX;
    float canvasSide;
    float canvasCY;
    float toolsY;
    float statusY;
    float partsY;
    float chipsY;
    float scrollX;
    float scrollY;
    float scrollW;
    float scrollH;
};

Layout layoutFor(CCSize const& win, bool hasParts, int zoneCount) {
    Layout out{};
    out.bodyTop = std::max(kMargin + 1.f, win.height - kTopBarH);
    out.workspaceX = kMargin;
    float const contentW = std::max(1.f, win.width - kMargin * 2.f);
    float const panelGap = std::min(kPanelGap, contentW * 0.04f);
    float const minWorkspaceW = std::min(kMinWorkspaceW, contentW * 0.42f);
    float const maxPanelW = std::max(1.f, contentW - panelGap - minWorkspaceW);
    float const desiredPanelW = std::clamp(
        win.width * 0.52f, kMinInspectorW, kMaxInspectorW);

    out.panelW = std::min(desiredPanelW, maxPanelW);
    out.workspaceW = std::max(1.f, contentW - panelGap - out.panelW);
    out.panelX = out.workspaceX + out.workspaceW + panelGap;
    out.panelH = std::max(1.f, out.bodyTop - kMargin);
    out.workspaceCX = out.workspaceX + out.workspaceW / 2.f;

    float const partsH = hasParts ? kit::kTabBarHeight : 0.f;
    out.partsY = out.bodyTop - 6.f - partsH / 2.f;

    float const canvasTop = out.bodyTop - (hasParts ? 10.f : 8.f) - partsH;
    float const canvasBottom = kMargin + 46.f;
    float const availableH = std::max(1.f, canvasTop - canvasBottom);
    float const availableW = std::max(1.f, out.workspaceW - 18.f);
    out.canvasSide = std::min(availableW, availableH);
    out.canvasCY = canvasBottom + availableH / 2.f;
    out.toolsY = kMargin + 30.f;
    out.statusY = kMargin + 11.f;

    out.scrollX = out.panelX + kPanelInset;
    out.scrollW = std::max(1.f, out.panelW - kPanelInset * 2.f);
    out.chipsY = out.bodyTop - 6.f - mkui::zoneChipsHeight(out.scrollW, zoneCount);
    out.scrollY = kMargin + kPanelInset;
    out.scrollH = std::max(1.f, out.chipsY - 6.f - out.scrollY);
    return out;
}

char const* fillTypeName(FillSpec const& fill) {
    if (fill.chroma) return "Arcoiris";
    switch (fill.type) {
        case FillType::Gradient: return "Degradado";
        case FillType::Image:    return "Imagen";
        case FillType::Flat:
        default:                 return "Color";
    }
}

// Representative paint color for the list swatch.
ccColor3B layerSwatch(IconPiece const& piece) {
    if (piece.fill.chroma) return {255, 255, 255};
    switch (piece.fill.type) {
        case FillType::Gradient: {
            if (piece.fill.gradient.stops.empty()) return {255, 255, 255};
            auto const& c = piece.fill.gradient.stops.front().color;
            return {c.r, c.g, c.b};
        }
        case FillType::Image:
            return {150, 200, 255};
        case FillType::Flat:
        default:
            return {piece.fill.flat.r, piece.fill.flat.g, piece.fill.flat.b};
    }
}

std::string layerSubtitle(IconPiece const& piece) {
    std::string text = fillTypeName(piece.fill);
    if (piece.fill.outline.enabled) text += " + borde";
    if (!piece.visible) text += "  (oculta)";
    return text;
}

// Copy a picked file into the project images directory under a unique name.
geode::Result<std::string> importImageFile(std::string const& slotId,
                                           std::string const& pieceId,
                                           std::filesystem::path const& source,
                                           char const* prefix) {
    auto ext = source.extension().string();
    if (ext.empty()) ext = ".png";
    auto fileName = IconPaths::sanitizeFilename(
        fmt::format("{}_{}{}", prefix, pieceId, ext));

    std::error_code ec;
    std::filesystem::create_directories(IconPaths::imagesDir(slotId), ec);
    std::filesystem::copy_file(source, IconPaths::imageFile(slotId, fileName),
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return Err("No se pudo copiar la imagen: {}", ec.message());
    }
    return Ok(fileName);
}

// Extract one vanilla frame for the selected zone.
geode::Result<> fillPieceFromTemplate(IconProject const& project, IconPiece& piece,
                                      int iconId, int part, std::string const& zoneKey) {
    auto const* def = anatomyFor(project.type);
    if (!def) return Err("Gamemode no soportado");

    std::string suffix;
    for (auto const& slot : def->slots) {
        if (slot.key == zoneKey) { suffix = slot.suffix; break; }
    }
    if (suffix.empty()) return Err("Zona desconocida");
    if (def->partCount > 1) suffix = fmt::format("_{:02}{}", part, suffix);

    auto extracted = TemplateExtractor::extractFrame(
        project.type, iconId, suffix, def->canvasUhd);
    if (!extracted) return Err("{}", extracted.unwrapErr());

    piece.shape.kind = PieceShape::Kind::Template;
    piece.shape.templateIconId = iconId;
    piece.shape.templateFrameSuffix = suffix;
    piece.shape.file = IconPaths::sanitizeFilename(
        fmt::format("tpl_{}_{}.png", piece.id,
            TemplateExtractor::sheetBase(project.type, iconId) + suffix));

    auto saved = extracted.unwrap().saveToPng(
        IconPaths::imageFile(project.id, piece.shape.file));
    if (!saved) return Err("{}", saved.unwrapErr());
    return Ok();
}

}


void IconEditorLayer::open(std::string const& slotId) {
    if (auto* layer = IconEditorLayer::create(slotId)) {
        geode::pushSceneWithLayer(layer);
    }
}

IconEditorLayer* IconEditorLayer::create(std::string const& slotId) {
    auto* ret = new IconEditorLayer();
    if (ret->init(slotId)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool IconEditorLayer::init(std::string const& slotId) {
    if (!CCLayer::init()) return false;

    auto loaded = IconProjectStore::get().loadProject(slotId);
    if (!loaded) {
        log::warn("[icon-maker] editor: {}", loaded.unwrapErr());
        return false;
    }
    m_project = loaded.unwrap();

    auto const* def = anatomyFor(m_project.type);
    if (!def) return false;

    m_currentPart = def->partCount > 1 ? 1 : 0;
    m_zoneIndex = 0;
    selectDefaultPiece();
    m_history.reset(m_project);

    setKeypadEnabled(true);
    setID("icon-maker-editor"_spr);

    buildBackground();
    buildTopBar();
    buildWorkspace();
    buildInspector();

    rebuildInspector();
    scheduleUpdate();
    schedulePreview();
    return true;
}

void IconEditorLayer::keyBackClicked() {
    onBack();
}

void IconEditorLayer::onBack() {
    if (m_dirty) saveProject(false);
    IconThumbs::get().invalidate(m_project.id);
    CCDirector::get()->popSceneWithTransition(0.4f, PopTransition::kPopTransitionFade);
}


void IconEditorLayer::buildBackground() {
    auto win = CCDirector::get()->getWinSize();

    if (auto* bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setScaleX(win.width / bg->getContentSize().width);
        bg->setScaleY(win.height / bg->getContentSize().height);
        bg->setColor({26, 48, 110});
        addChild(bg, -6);
    } else {
        auto* flat = CCLayerColor::create(ccc4(22, 40, 92, 255));
        flat->setContentSize(win);
        addChild(flat, -6);
    }

    if (auto* left = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sideArt_001.png")) {
        left->setAnchorPoint({0.f, 0.f});
        left->setPosition({-2.f, -2.f});
        left->setOpacity(120);
        addChild(left, -5);
    }
    if (auto* right = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sideArt_001.png")) {
        right->setAnchorPoint({1.f, 0.f});
        right->setFlipX(true);
        right->setPosition({win.width + 2.f, -2.f});
        right->setOpacity(120);
        addChild(right, -5);
    }
}

void IconEditorLayer::buildTopBar() {
    auto win = CCDirector::get()->getWinSize();

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    addChild(menu, 12);

    float const cy = win.height - kTopBarH / 2.f;

    if (auto* spr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        spr->setScale(0.72f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this](CCMenuItemSpriteExtra*) { this->onBack(); });
        btn->setPosition({20.f, cy});
        menu->addChild(btn);
    }

    auto const* def = anatomyFor(m_project.type);
    std::string heading = def
        ? fmt::format("{}  -  {}", m_project.name, def->displayName)
        : m_project.name;

    m_titleLabel = CCLabelBMFont::create(heading.c_str(), "goldFont.fnt");
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->limitLabelWidth(win.width * 0.36f, 0.52f, 0.2f);

    auto* titleHit = CCNode::create();
    titleHit->setAnchorPoint({0.f, 0.5f});
    titleHit->setContentSize(m_titleLabel->getScaledContentSize() + CCSize{12.f, 14.f});
    m_titleLabel->setPosition({6.f, titleHit->getContentSize().height / 2.f});
    titleHit->addChild(m_titleLabel);

    auto* titleBtn = CCMenuItemExt::createSpriteExtra(titleHit,
        [this](CCMenuItemSpriteExtra*) { this->onRename(); });
    titleBtn->setAnchorPoint({0.f, 0.5f});
    titleBtn->setPosition({38.f, cy});
    menu->addChild(titleBtn);

    float x = win.width - 16.f;

    if (auto* spr = ButtonSprite::create("Usar", "goldFont.fnt", "GJ_button_01.png", 0.8f)) {
        spr->setScale(0.62f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this](CCMenuItemSpriteExtra*) { this->onApply(); });
        x -= btn->getScaledContentSize().width / 2.f;
        btn->setPosition({x, cy});
        menu->addChild(btn);
        x -= btn->getScaledContentSize().width / 2.f + 6.f;
    }

    auto addCircle = [&](char const* frame, float glyphScale, CircleBaseColor color,
                         bool flipX, std::function<void()> action, CCSprite** outGlyph) {
        auto* glyph = CCSprite::createWithSpriteFrameName(frame);
        if (!glyph) return;
        glyph->setFlipX(flipX);

        auto* base = CircleButtonSprite::create(glyph, color, CircleBaseSize::Small);
        if (!base) return;
        base->setTopRelativeScale(glyphScale);
        if (outGlyph) *outGlyph = glyph;
        base->setScale(0.8f);

        auto* btn = CCMenuItemExt::createSpriteExtra(base,
            [action](CCMenuItemSpriteExtra*) { if (action) action(); });
        float const half = btn->getScaledContentSize().width / 2.f;
        x -= half;
        btn->setPosition({x, cy});
        menu->addChild(btn);
        x -= half + 3.f;
    };

    addCircle("GJ_optionsBtn_001.png", 0.9f, CircleBaseColor::Gray, false,
              [this] { this->onProjectMenu(); }, nullptr);
    addCircle("GJ_infoIcon_001.png", 1.f, CircleBaseColor::Cyan, false,
              [] { if (auto* p = IconHelpPopup::create()) p->show(); }, nullptr);
    addCircle("GJ_arrow_03_001.png", 1.f, CircleBaseColor::Blue, true,
              [this] { this->onRedo(); }, &m_redoGlyph);
    addCircle("GJ_arrow_03_001.png", 1.f, CircleBaseColor::Blue, false,
              [this] { this->onUndo(); }, &m_undoGlyph);

    refreshTopBar();
}

void IconEditorLayer::refreshTopBar() {
    if (m_undoGlyph) m_undoGlyph->setOpacity(m_history.canUndo() ? 255 : 90);
    if (m_redoGlyph) m_redoGlyph->setOpacity(m_history.canRedo() ? 255 : 90);

    if (m_titleLabel) {
        auto const* def = anatomyFor(m_project.type);
        m_titleLabel->setString(def
            ? fmt::format("{}  -  {}", m_project.name, def->displayName).c_str()
            : m_project.name.c_str());
        m_titleLabel->limitLabelWidth(
            CCDirector::get()->getWinSize().width * 0.36f, 0.52f, 0.2f);
    }
}

void IconEditorLayer::buildWorkspace() {
    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    bool hasParts = def && def->partCount > 1;
    auto layout = layoutFor(win, hasParts, static_cast<int>(visibleZones().size()));

    if (auto* window = kit::makeWindow({layout.workspaceW, layout.panelH})) {
        window->setPosition({layout.workspaceX, kMargin});
        addChild(window, 2);
    }

    m_canvas = EditorCanvas::create(layout.canvasSide,
        [this](float dx, float dy) { this->onCanvasDrag(dx, dy); },
        [this](std::string const& zoneKey) { this->selectZoneByKey(zoneKey); });
    if (m_canvas) {
        m_canvas->setPosition({layout.workspaceCX, layout.canvasCY});
        m_canvas->setBackgroundMode(m_backgroundMode);
        m_canvas->setGuideVisible(m_showGuide);
        addChild(m_canvas, 5);
    }

    // Robot and spider sheets contain four independently edited drawings.
    if (hasParts) {
        m_partsHost = CCNode::create();
        m_partsHost->setPosition({layout.workspaceCX, layout.partsY});
        addChild(m_partsHost, 6);

        std::vector<std::string> labels;
        for (int part = 1; part <= def->partCount; ++part) {
            labels.push_back(fmt::format("P{}", part));
        }
        auto* tabs = kit::makeTabBar(layout.canvasSide, labels, m_currentPart - 1,
            [this](int index) {
                Ref<IconEditorLayer> self = this;
                Loader::get()->queueInMainThread([self, index] {
                    if (paimon::isRuntimeShuttingDown() || !self) return;
                    self->selectPart(index + 1);
                });
            });
        if (tabs) {
            tabs->setPosition({-layout.canvasSide / 2.f, -kit::kTabBarHeight / 2.f});
            m_partsHost->addChild(tabs);
        }
    }

    if (auto* frame = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
        frame->setContentSize({layout.canvasSide + 10.f, layout.canvasSide + 10.f});
        frame->setAnchorPoint({0.5f, 0.5f});
        frame->setPosition({layout.workspaceCX, layout.canvasCY});
        addChild(frame, 4);
    }

    // View tools affect only the preview, not the icon data.
    auto* toolMenu = CCMenu::create();
    toolMenu->setPosition({0.f, 0.f});
    addChild(toolMenu, 6);

    float const toolW = layout.canvasSide / 3.f;
    float const toolLeft = layout.workspaceCX - layout.canvasSide / 2.f;
    m_toolLabelW = std::max(8.f, toolW - 14.f);

    // ButtonSprite owns the art, so state is shown in its label.
    auto makeToolButton = [&](float cx, cocos2d::CCLabelBMFont** out,
                              std::function<void(CCMenuItemSpriteExtra*)> action) {
        float const btnW = std::max(12.f, toolW - 6.f);
        constexpr float btnH = 22.f;

        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({btnW, btnH});

        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("GJ_button_04.png")) {
            bg->setContentSize({btnW, btnH});
            bg->setAnchorPoint({0.f, 0.f});
            holder->addChild(bg, -1);
        }

        auto* label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setAnchorPoint({0.5f, 0.5f});
        label->setPosition({btnW / 2.f, btnH / 2.f});
        holder->addChild(label);

        auto* btn = CCMenuItemExt::createSpriteExtra(holder, action);
        btn->setPosition({cx, layout.toolsY});
        toolMenu->addChild(btn);
        if (out) *out = label;
    };

    makeToolButton(toolLeft + toolW * 0.5f, &m_bgToolLabel,
        [this](CCMenuItemSpriteExtra*) {
            m_backgroundMode = (m_backgroundMode + 1) % 3;
            if (m_canvas) m_canvas->setBackgroundMode(m_backgroundMode);
            refreshViewTools();
        });
    makeToolButton(toolLeft + toolW * 1.5f, &m_guideToolLabel,
        [this](CCMenuItemSpriteExtra*) {
            m_showGuide = !m_showGuide;
            if (m_canvas) m_canvas->setGuideVisible(m_showGuide);
            refreshViewTools();
        });
    makeToolButton(toolLeft + toolW * 2.5f, &m_isolateToolLabel,
        [this](CCMenuItemSpriteExtra*) {
            m_isolateZone = !m_isolateZone;
            if (m_canvas) m_canvas->setIsolate(m_isolateZone);
            setStatus(m_isolateZone ? "Viendo solo esta zona." : "Viendo el icono entero.");
            refreshViewTools();
        });
    refreshViewTools();

    m_statusLabel = CCLabelBMFont::create(
        "Toca una zona del icono o arrastra para mover.", "chatFont.fnt");
    if (m_statusLabel) {
        m_statusLabel->setScale(0.45f);
        m_statusLabel->setColor(kit::kDescColor);
        m_statusLabel->setPosition({layout.workspaceCX, layout.statusY});
        m_statusLabel->limitLabelWidth(layout.workspaceW - 12.f, 0.45f, 0.28f);
        addChild(m_statusLabel, 6);
    }
}

void IconEditorLayer::refreshViewTools() {
    float const maxW = m_toolLabelW;
    bool const compact = maxW < 48.f;
    auto setLabel = [maxW](CCLabelBMFont* label, char const* text) {
        if (!label) return;
        label->setString(text);
        label->limitLabelWidth(maxW, 0.4f, 0.1f);
    };

    setLabel(m_bgToolLabel, m_backgroundMode == 1
        ? (compact ? "Claro" : "Fondo: claro")
        : m_backgroundMode == 2
            ? (compact ? "Cuadros" : "Fondo: cuadros")
            : (compact ? "Oscuro" : "Fondo: oscuro"));
    setLabel(m_guideToolLabel, m_showGuide
        ? (compact ? "Guia si" : "Guia: si")
        : (compact ? "Guia no" : "Guia: no"));
    setLabel(m_isolateToolLabel, m_isolateZone
        ? (compact ? "Solo" : "Solo esta zona")
        : (compact ? "Todo" : "Ver todo"));
}

void IconEditorLayer::buildInspector() {
    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    auto layout = layoutFor(win, def && def->partCount > 1,
                            static_cast<int>(visibleZones().size()));

    if (auto* window = kit::makeWindow({layout.panelW, layout.panelH})) {
        window->setPosition({layout.panelX, kMargin});
        addChild(window, 2);
    }

    m_zoneChipsHost = CCNode::create();
    m_zoneChipsHost->setPosition({layout.scrollX, layout.chipsY});
    addChild(m_zoneChipsHost, 6);

    if (auto* line = paimon::SpriteHelper::createColorPanel(
            layout.scrollW, 1.f, {255, 255, 255}, 60, 0.f)) {
        line->setAnchorPoint({0.f, 0.f});
        line->setPosition({layout.scrollX, layout.chipsY - 4.f});
        addChild(line, 6);
    }

    m_inspectorHost = CCNode::create();
    m_inspectorHost->setPosition({layout.scrollX, layout.scrollY});
    addChild(m_inspectorHost, 6);

    refreshZoneChips();
}

void IconEditorLayer::refreshZoneChips() {
    if (!m_zoneChipsHost) return;
    m_zoneChipsHost->removeAllChildren();

    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    auto layout = layoutFor(win, def && def->partCount > 1,
                            static_cast<int>(visibleZones().size()));

    std::vector<mkui::ZoneChip> chips;
    for (auto const& zone : visibleZones()) {
        auto key = slotStorageKey(m_currentPart, zone.key);
        int count = 0;
        if (auto it = m_project.slots.find(key); it != m_project.slots.end()) {
            count = static_cast<int>(it->second.pieces.size());
        }
        chips.push_back({std::string(zone.label), zone.accent, count});
    }

    auto* strip = mkui::makeZoneChips(layout.scrollW, chips, m_zoneIndex,
        [this](int index) {
            Ref<IconEditorLayer> self = this;
            Loader::get()->queueInMainThread([self, index] {
                if (paimon::isRuntimeShuttingDown() || !self) return;
                self->selectZone(index);
            });
        });
    if (strip) m_zoneChipsHost->addChild(strip);
}

void IconEditorLayer::scheduleInspectorRebuild() {
    if (m_rebuildQueued) return;
    m_rebuildQueued = true;
    Ref<IconEditorLayer> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        self->m_rebuildQueued = false;
        if (self->getParent()) self->rebuildInspector();
    });
}

void IconEditorLayer::rebuildInspector() {
    if (!m_inspectorHost) return;

    if (m_inspector) {
        // Preserve scroll position across list rebuilds.
        if (auto* content = m_inspector->m_contentLayer) {
            m_inspectorScrollY = content->getPositionY();
        }
        m_inspector->removeFromParent();
        m_inspector = nullptr;
    }
    m_wheelTargetSet = false;

    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    auto layout = layoutFor(win, def && def->partCount > 1,
                            static_cast<int>(visibleZones().size()));

    float const cardW = layout.scrollW;
    float const scrollH = layout.scrollH;

    std::vector<CCNode*> cards{
        buildLayersCard(cardW),
        buildPaintCard(cardW),
        buildShapeCard(cardW),
        buildProjectCard(cardW),
    };
    cards.erase(std::remove(cards.begin(), cards.end(), nullptr), cards.end());

    m_inspector = kit::makeScrollStack({cardW, scrollH}, cards, 6.f);
    if (m_inspector) {
        m_inspector->setPosition({0.f, 0.f});
        m_inspectorHost->addChild(m_inspector);

        if (auto* content = m_inspector->m_contentLayer) {
            float minY = scrollH - content->getContentSize().height;
            content->setPositionY(std::clamp(m_inspectorScrollY, std::min(minY, 0.f), 0.f));
        }
    }

    refreshTopBar();
}


CCNode* IconEditorLayer::buildLayersCard(float width) {
    float const innerW = kit::cardInnerWidth(width);
    auto zones = visibleZones();
    if (zones.empty()) return nullptr;

    auto& slot = currentSlot();
    auto const& zone = zones[static_cast<std::size_t>(
        std::clamp(m_zoneIndex, 0, static_cast<int>(zones.size()) - 1))];

    std::vector<CCNode*> rows;
    rows.push_back(kit::makeHint(innerW, std::string(zone.hint).c_str()));

    if (slot.pieces.empty()) {
        rows.push_back(mkui::makeEmptyState(innerW, "Zona vacia",
            "Agrega una capa: una imagen tuya, o la forma de un icono oficial "
            "para pintarla a tu gusto."));
    }

    int const count = static_cast<int>(slot.pieces.size());
    for (int display = 0; display < count; ++display) {
        int index = count - 1 - display;
        auto const& piece = slot.pieces[static_cast<std::size_t>(index)];

        mkui::LayerRowSpec spec;
        spec.name = piece.name;
        spec.subtitle = layerSubtitle(piece);
        spec.swatch = layerSwatch(piece);
        spec.visible = piece.visible;
        spec.selected = index == m_selectedPiece;
        spec.canMoveUp = index + 1 < count;
        spec.canMoveDown = index > 0;

        spec.onSelect = [this, index] { this->selectPiece(index); };
        spec.onToggleVisible = [this, index] {
            edit({}, [&] {
                auto& pieces = currentSlot().pieces;
                if (index < static_cast<int>(pieces.size())) {
                    pieces[static_cast<std::size_t>(index)].visible =
                        !pieces[static_cast<std::size_t>(index)].visible;
                }
            });
            scheduleInspectorRebuild();
        };
        spec.onMoveUp = [this, index] {
            edit({}, [&] {
                auto& pieces = currentSlot().pieces;
                if (index + 1 < static_cast<int>(pieces.size())) {
                    std::swap(pieces[static_cast<std::size_t>(index)],
                              pieces[static_cast<std::size_t>(index) + 1]);
                    if (m_selectedPiece == index) m_selectedPiece = index + 1;
                }
            });
            scheduleInspectorRebuild();
        };
        spec.onMoveDown = [this, index] {
            edit({}, [&] {
                auto& pieces = currentSlot().pieces;
                if (index > 0 && index < static_cast<int>(pieces.size())) {
                    std::swap(pieces[static_cast<std::size_t>(index)],
                              pieces[static_cast<std::size_t>(index) - 1]);
                    if (m_selectedPiece == index) m_selectedPiece = index - 1;
                }
            });
            scheduleInspectorRebuild();
        };
        spec.onMore = [this, index] { this->onLayerMenu(index); };

        rows.push_back(mkui::makeLayerRow(innerW, std::move(spec)));
    }

    rows.push_back(mkui::makeDualButtonRow(innerW,
        "+ Imagen", [this] { this->onAddImportLayer(); },
        "+ Forma oficial", [this] { this->onAddTemplateLayer(); }));

    std::string const heading = count == 0
        ? fmt::format("Capas de {} (vacia)", zone.label)
        : fmt::format("Capas de {} ({})", zone.label, count);
    return kit::makeCard(width, heading.c_str(), zone.accent, rows);
}


CCNode* IconEditorLayer::buildPaintCard(float width) {
    float const innerW = kit::cardInnerWidth(width);
    auto* piece = selectedPiece();

    if (!piece) {
        return kit::makeCard(width, "Pintura", mkui::kAccentPaint,
            {mkui::makeEmptyState(innerW, "Ninguna capa elegida",
                "Toca una capa de la lista de arriba para pintarla.")});
    }

    std::vector<CCNode*> rows;

    int typeIndex = piece->fill.type == FillType::Gradient ? 1
                  : piece->fill.type == FillType::Image ? 2 : 0;
    rows.push_back(kit::makeTabBar(innerW, {"Color", "Degradado", "Imagen"}, typeIndex,
        [this](int index) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) {
                    p->fill.type = static_cast<FillType>(index);
                    p->fill.chroma = false;
                }
            });
            scheduleInspectorRebuild();
        }));

    switch (piece->fill.type) {
        case FillType::Flat: {
            rows.push_back(mkui::makeSwatchGrid(innerW, quickColors(), piece->fill.flat,
                [this](ccColor3B color) {
                    edit({}, [&] {
                        if (auto* p = selectedPiece()) {
                            p->fill.flat = {color.r, color.g, color.b, 255};
                            p->fill.chroma = false;
                        }
                    });
                    scheduleInspectorRebuild();
                },
                [this] {
                    auto* p = selectedPiece();
                    if (!p) return;
                    auto* picker = ColorPickPopup::create(p->fill.flat);
                    if (!picker) return;
                    Ref<IconEditorLayer> self = this;
                    picker->setCallback([self](ccColor4B const& picked) {
                        if (paimon::isRuntimeShuttingDown() || !self) return;
                        self->edit("flat-color", [&] {
                            if (auto* p2 = self->selectedPiece()) p2->fill.flat = picked;
                        });
                        self->scheduleInspectorRebuild();
                    });
                    picker->show();
                }));

            auto mine = playerColors();
            if (!mine.empty()) {
                rows.push_back(kit::makeHint(innerW,
                    "Abajo estan tus colores de jugador, por si quieres que el "
                    "icono combine con tu kit."));
                rows.push_back(mkui::makeSwatchGrid(innerW, mine, piece->fill.flat,
                    [this](ccColor3B color) {
                        edit({}, [&] {
                            if (auto* p = selectedPiece()) {
                                p->fill.flat = {color.r, color.g, color.b, 255};
                            }
                        });
                        scheduleInspectorRebuild();
                    }, nullptr));
            }
            break;
        }

        case FillType::Gradient: {
            rows.push_back(mkui::makeGradientRow(innerW, piece->fill.gradient, "Editar",
                [this] {
                    auto* p = selectedPiece();
                    if (!p) return;
                    Ref<IconEditorLayer> self = this;
                    auto* popup = GradientEditorPopup::create(p->fill.gradient,
                        [self](GradientSpec const& spec) {
                            if (paimon::isRuntimeShuttingDown() || !self) return;
                            self->edit("gradient", [&] {
                                if (auto* p2 = self->selectedPiece()) p2->fill.gradient = spec;
                            });
                        });
                    if (popup) popup->show();
                }));

            rows.push_back(kit::makeButtonRow(innerW, "Combinaciones listas",
                "Fuego, hielo, arcoiris... una y ya esta.",
                "Elegir", [this] {
                    Ref<IconEditorLayer> self = this;
                    std::vector<IconActionSheet::Action> actions;
                    for (auto const& preset : gradientPresets()) {
                        auto spec = preset.spec;
                        actions.push_back({std::string(preset.name), "",
                            [self, spec] {
                                if (paimon::isRuntimeShuttingDown() || !self) return;
                                self->edit({}, [&] {
                                    if (auto* p = self->selectedPiece()) p->fill.gradient = spec;
                                });
                                self->scheduleInspectorRebuild();
                            }, false});
                    }
                    if (auto* sheet = IconActionSheet::create(
                            "Degradados listos", std::move(actions))) {
                        sheet->show();
                    }
                }));
            break;
        }

        case FillType::Image: {
            std::string current = piece->fill.image.file.empty()
                ? std::string("Sin imagen todavia")
                : piece->fill.image.file;
            rows.push_back(kit::makeButtonRow(innerW, "Imagen de relleno",
                current.c_str(),
                piece->fill.image.file.empty() ? "Elegir" : "Cambiar",
                [this] {
                    Ref<IconEditorLayer> self = this;
                    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
                        if (paimon::isRuntimeShuttingDown() || !self) return;
                        if (!res) return;
                        auto pathOpt = res.unwrap();
                        if (!pathOpt) return;
                        auto* p = self->selectedPiece();
                        if (!p) return;
                        auto imported = importImageFile(
                            self->m_project.id, p->id, *pathOpt, "fill");
                        if (!imported) {
                            Notification::create(imported.unwrapErr(),
                                NotificationIcon::Error, 3.f)->show();
                            return;
                        }
                        auto file = imported.unwrap();
                        self->edit({}, [&] {
                            if (auto* p2 = self->selectedPiece()) p2->fill.image.file = file;
                        });
                        self->scheduleInspectorRebuild();
                    });
                }));

            std::vector<std::string> fits{"Entera", "Cubrir", "Estirar", "Mosaico"};
            rows.push_back(kit::makeSelectRow(innerW, "Encaje",
                "Como se acomoda la imagen dentro de la forma.",
                fits, static_cast<int>(piece->fill.image.fit),
                [this](int index) {
                    edit({}, [&] {
                        if (auto* p = selectedPiece()) {
                            p->fill.image.fit = static_cast<FillFitMode>(index);
                        }
                    });
                }));

            rows.push_back(kit::makeSliderRow(innerW, "Zoom", nullptr,
                piece->fill.image.scale, 0.05, 6.0,
                [](double v) { return fmt::format("x{:.2f}", v); },
                [this](double v) {
                    edit("fill-scale", [&] {
                        if (auto* p = selectedPiece()) p->fill.image.scale = static_cast<float>(v);
                    });
                }));
            rows.push_back(kit::makeSliderRow(innerW, "Giro", nullptr,
                piece->fill.image.rotationDeg, -180.0, 180.0,
                [](double v) { return fmt::format("{}deg", static_cast<int>(v)); },
                [this](double v) {
                    edit("fill-rot", [&] {
                        if (auto* p = selectedPiece()) {
                            p->fill.image.rotationDeg = static_cast<float>(v);
                        }
                    });
                }));
            rows.push_back(kit::makeSliderRow(innerW, "Transparencia", nullptr,
                piece->fill.image.opacity, 0.0, 255.0,
                [](double v) { return fmt::format("{}%", static_cast<int>(v / 2.55)); },
                [this](double v) {
                    edit("fill-opacity", [&] {
                        if (auto* p = selectedPiece()) {
                            p->fill.image.opacity = static_cast<int>(v);
                        }
                    });
                }));
            break;
        }
    }

    rows.push_back(kit::makeToggleRow(innerW, "Sombreado 3D",
        "Conserva las sombras del dibujo original bajo el color.",
        piece->fill.keepLuminance,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->fill.keepLuminance = value;
            });
        }));

    rows.push_back(kit::makeToggleRow(innerW, "Arcoiris animado",
        "La capa cambia de color sola. Solo se anima en el garaje.",
        piece->fill.chroma,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->fill.chroma = value;
            });
            scheduleInspectorRebuild();
        }));

    // Outline is stored with paint because it is another color pass on the shape.
    rows.push_back(kit::makeToggleRow(innerW, "Borde",
        "Un contorno alrededor de la capa. Ayuda a que el icono se vea "
        "nitido en el juego.",
        piece->fill.outline.enabled,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->fill.outline.enabled = value;
            });
            scheduleInspectorRebuild();
        }));

    if (piece->fill.outline.enabled) {
        rows.push_back(kit::makeSliderRow(innerW, "Grosor del borde", nullptr,
            piece->fill.outline.width, 1.0, 20.0,
            [](double v) { return fmt::format("{}", static_cast<int>(v)); },
            [this](double v) {
                edit("outline-width", [&] {
                    if (auto* p = selectedPiece()) p->fill.outline.width = static_cast<float>(v);
                });
            }));
        rows.push_back(mkui::makeColorRow(innerW, "Color del borde", nullptr,
            piece->fill.outline.color,
            [this] {
                auto* p = selectedPiece();
                if (!p) return;
                auto* picker = ColorPickPopup::create(p->fill.outline.color);
                if (!picker) return;
                Ref<IconEditorLayer> self = this;
                picker->setCallback([self](ccColor4B const& picked) {
                    if (paimon::isRuntimeShuttingDown() || !self) return;
                    self->edit("outline-color", [&] {
                        if (auto* p2 = self->selectedPiece()) p2->fill.outline.color = picked;
                    });
                    self->scheduleInspectorRebuild();
                });
                picker->show();
            }));
    }

    return kit::makeCard(width,
        fmt::format("Pintura de {}", piece->name).c_str(),
        mkui::kAccentPaint, rows);
}


CCNode* IconEditorLayer::buildShapeCard(float width) {
    auto* piece = selectedPiece();
    if (!piece) return nullptr;

    float const innerW = kit::cardInnerWidth(width);
    std::vector<CCNode*> rows;

    rows.push_back(kit::makeSliderRow(innerW, "Tamano", nullptr,
        piece->transform.scale, 0.05, 3.0,
        [](double v) { return fmt::format("x{:.2f}", v); },
        [this](double v) {
            edit("scale", [&] {
                if (auto* p = selectedPiece()) p->transform.scale = static_cast<float>(v);
            });
        }));

    rows.push_back(kit::makeSliderRow(innerW, "Giro", nullptr,
        piece->transform.rotationDeg, -180.0, 180.0,
        [](double v) { return fmt::format("{}deg", static_cast<int>(v)); },
        [this](double v) {
            edit("rotation", [&] {
                if (auto* p = selectedPiece()) p->transform.rotationDeg = static_cast<float>(v);
            });
        }));

    rows.push_back(kit::makeSliderRow(innerW, "Transparencia", nullptr,
        piece->transform.opacity, 0.0, 255.0,
        [](double v) { return fmt::format("{}%", static_cast<int>(v / 2.55)); },
        [this](double v) {
            edit("opacity", [&] {
                if (auto* p = selectedPiece()) p->transform.opacity = static_cast<int>(v);
            });
        }));

    rows.push_back(mkui::makeNudgePad(innerW, "Posicion",
        "Arrastra en la vista previa, o afina con las flechas.",
        [this](float dx, float dy) { this->nudgeSelected(dx, dy); },
        [this] {
            edit({}, [&] {
                if (auto* p = selectedPiece()) {
                    p->transform.offsetX = 0.f;
                    p->transform.offsetY = 0.f;
                }
            });
            setStatus("Capa centrada.");
        }));

    rows.push_back(kit::makeToggleRow(innerW, "Espejo horizontal", nullptr,
        piece->transform.flipX,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->transform.flipX = value;
            });
        }));
    rows.push_back(kit::makeToggleRow(innerW, "Espejo vertical", nullptr,
        piece->transform.flipY,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->transform.flipY = value;
            });
        }));

    rows.push_back(kit::makeButtonRow(innerW, "Cambiar el dibujo",
        "Sustituye la forma de esta capa sin perder como esta pintada.",
        "Cambiar", [this] { this->onReplaceShape(); }));

    rows.push_back(kit::makeButtonRow(innerW, "Restablecer",
        "Devuelve tamano, giro y posicion a como estaban al empezar.",
        "Reiniciar", [this] {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->transform = ts::ImageTransform{};
            });
            scheduleInspectorRebuild();
            setStatus("Forma restablecida.");
        }));

    return kit::makeCard(width, "Forma y posicion", mkui::kAccentShape, rows);
}


CCNode* IconEditorLayer::buildProjectCard(float width) {
    float const innerW = kit::cardInnerWidth(width);
    auto const* def = anatomyFor(m_project.type);

    std::vector<CCNode*> rows;

    rows.push_back(kit::makeToggleRow(innerW, "Colores reales",
        "Muestra los colores que pintaste en vez de dejar que el juego los "
        "cambie por tus colores de jugador.",
        m_project.exactColors,
        [this](bool value) {
            edit({}, [&] { m_project.exactColors = value; });
        }));

    if (def && def->partCount > 1) {
        rows.push_back(kit::makeButtonRow(innerW, "Copiar esta parte",
            "Deja las otras tres partes iguales a la que estas editando.",
            "Copiar", [this] { this->onCopyPartToOthers(); }));
    }

    rows.push_back(kit::makeButtonRow(innerW, "Exportar",
        "Genera los archivos del icono o instalalo en More Icons.",
        "Exportar", [this] { this->onExport(); }));

    rows.push_back(kit::makeButtonRow(innerW, "Como funciona",
        "Que es una zona, que es una capa y que hace cada ajuste.",
        "Ayuda", [] { if (auto* p = IconHelpPopup::create()) p->show(); }));

    return kit::makeCard(width, "Este icono", mkui::kAccentProject, rows);
}


std::vector<SlotDef> IconEditorLayer::visibleZones() const {
    std::vector<SlotDef> out;
    auto const* def = anatomyFor(m_project.type);
    if (!def) return out;
    for (auto const& slot : def->slots) {
        // "Extra" exists only on the first robot/spider part.
        if (def->partCount > 1 && m_currentPart > 1 && slot.key == "extra") continue;
        out.push_back(slot);
    }
    return out;
}

std::string IconEditorLayer::currentSlotKey() const {
    auto zones = visibleZones();
    if (zones.empty()) return slotStorageKey(m_currentPart, "main");
    int index = std::clamp(m_zoneIndex, 0, static_cast<int>(zones.size()) - 1);
    return slotStorageKey(m_currentPart, zones[static_cast<std::size_t>(index)].key);
}

IconSlotContent& IconEditorLayer::currentSlot() {
    return m_project.slots[currentSlotKey()];
}

IconPiece* IconEditorLayer::selectedPiece() {
    auto& slot = currentSlot();
    if (m_selectedPiece < 0 ||
        m_selectedPiece >= static_cast<int>(slot.pieces.size())) {
        return nullptr;
    }
    return &slot.pieces[static_cast<std::size_t>(m_selectedPiece)];
}

void IconEditorLayer::selectDefaultPiece() {
    auto const& pieces = currentSlot().pieces;
    m_selectedPiece = pieces.empty() ? -1 : static_cast<int>(pieces.size()) - 1;
}

void IconEditorLayer::selectPart(int part) {
    auto const* def = anatomyFor(m_project.type);
    if (!def || part < 1 || part > def->partCount || part == m_currentPart) return;
    m_currentPart = part;

    auto zones = visibleZones();
    m_zoneIndex = std::clamp(m_zoneIndex, 0, static_cast<int>(zones.size()) - 1);
    selectDefaultPiece();
    m_inspectorScrollY = 0.f;

    refreshZoneChips();
    rebuildInspector();
    schedulePreview();
}

void IconEditorLayer::selectZone(int zoneIndex) {
    auto zones = visibleZones();
    if (zones.empty()) return;
    zoneIndex = std::clamp(zoneIndex, 0, static_cast<int>(zones.size()) - 1);
    if (zoneIndex == m_zoneIndex) return;

    m_zoneIndex = zoneIndex;
    selectDefaultPiece();
    m_inspectorScrollY = 0.f;

    refreshZoneChips();
    rebuildInspector();
    if (m_canvas) m_canvas->setActiveZone(currentSlotKey());
    setStatus(fmt::format("Editando: {}", zones[static_cast<std::size_t>(zoneIndex)].label));
}

void IconEditorLayer::selectZoneByKey(std::string const& storageKey) {
    auto zones = visibleZones();
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (slotStorageKey(m_currentPart, zones[i].key) == storageKey) {
            selectZone(static_cast<int>(i));
            return;
        }
    }
}

void IconEditorLayer::selectPiece(int index) {
    if (index == m_selectedPiece) return;
    m_selectedPiece = index;
    scheduleInspectorRebuild();
}

void IconEditorLayer::edit(std::string coalesceKey, std::function<void()> mutate) {
    if (!mutate) return;
    m_history.push(m_project, std::move(coalesceKey));
    mutate();
    m_history.commit(m_project);

    m_dirty = true;
    m_autosaveCountdown = 2.f;
    schedulePreview();
    refreshTopBar();
}

void IconEditorLayer::applyRestoredProject() {
    auto zones = visibleZones();
    m_zoneIndex = std::clamp(m_zoneIndex, 0,
        std::max(0, static_cast<int>(zones.size()) - 1));

    int pieceCount = static_cast<int>(currentSlot().pieces.size());
    if (m_selectedPiece >= pieceCount) m_selectedPiece = pieceCount - 1;

    m_dirty = true;
    m_autosaveCountdown = 2.f;
    refreshZoneChips();
    rebuildInspector();
    schedulePreview();
}

void IconEditorLayer::onUndo() {
    auto const* restored = m_history.undo();
    if (!restored) {
        setStatus("No hay nada que deshacer.");
        return;
    }
    m_project = *restored;
    applyRestoredProject();
    setStatus("Deshecho.");
}

void IconEditorLayer::onRedo() {
    auto const* restored = m_history.redo();
    if (!restored) {
        setStatus("No hay nada que rehacer.");
        return;
    }
    m_project = *restored;
    applyRestoredProject();
    setStatus("Rehecho.");
}


void IconEditorLayer::onRename() {
    Ref<IconEditorLayer> self = this;
    auto* popup = IconNamePopup::create("Nombre del icono", "Mi icono",
        m_project.name, [self](std::string const& name) {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            self->edit({}, [&] { self->m_project.name = name; });
            self->refreshTopBar();
            self->saveProject(false);
        });
    if (popup) popup->show();
}

void IconEditorLayer::onAddImportLayer() {
    Ref<IconEditorLayer> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        if (!res) {
            Notification::create("No se pudo abrir el selector de archivos.",
                NotificationIcon::Error, 2.5f)->show();
            return;
        }
        auto pathOpt = res.unwrap();
        if (!pathOpt) return;

        IconPiece piece;
        piece.id = self->m_project.makePieceId();
        piece.name = fmt::format("Capa {}", self->currentSlot().pieces.size() + 1);

        auto imported = importImageFile(self->m_project.id, piece.id, *pathOpt, "imp");
        if (!imported) {
            Notification::create(imported.unwrapErr(), NotificationIcon::Error, 3.f)->show();
            return;
        }
        piece.shape.kind = PieceShape::Kind::Import;
        piece.shape.file = imported.unwrap();

        self->edit({}, [&] {
            auto& pieces = self->currentSlot().pieces;
            pieces.push_back(std::move(piece));
            self->m_selectedPiece = static_cast<int>(pieces.size()) - 1;
        });
        self->refreshZoneChips();
        self->scheduleInspectorRebuild();
        self->setStatus("Capa agregada.");
    });
}

void IconEditorLayer::onAddTemplateLayer() {
    Ref<IconEditorLayer> self = this;
    auto* popup = TemplatePickerPopup::create(m_project.type, [self](int iconId) {
        if (paimon::isRuntimeShuttingDown() || !self) return;

        auto zones = self->visibleZones();
        if (zones.empty()) return;
        auto zoneKey = std::string(
            zones[static_cast<std::size_t>(self->m_zoneIndex)].key);

        IconPiece piece;
        piece.id = self->m_project.makePieceId();
        piece.name = fmt::format("Icono {}", iconId);

        if (auto r = fillPieceFromTemplate(self->m_project, piece, iconId,
                                           self->m_currentPart, zoneKey); !r) {
            Notification::create(("Forma oficial: " + r.unwrapErr()).c_str(),
                NotificationIcon::Error, 3.f)->show();
            return;
        }

        self->edit({}, [&] {
            auto& pieces = self->currentSlot().pieces;
            pieces.push_back(std::move(piece));
            self->m_selectedPiece = static_cast<int>(pieces.size()) - 1;
        });
        self->refreshZoneChips();
        self->scheduleInspectorRebuild();
        self->setStatus("Forma agregada.");
    });
    if (popup) popup->show();
}

void IconEditorLayer::onReplaceShape() {
    if (!selectedPiece()) return;

    Ref<IconEditorLayer> self = this;
    std::vector<IconActionSheet::Action> actions{
        {"Usar una imagen mia", "Elige un PNG de tu computadora.",
         [self] {
             if (!self) return;
             pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
                 if (paimon::isRuntimeShuttingDown() || !self) return;
                 if (!res) return;
                 auto pathOpt = res.unwrap();
                 if (!pathOpt) return;
                 auto* piece = self->selectedPiece();
                 if (!piece) return;
                 auto imported = importImageFile(
                     self->m_project.id, piece->id, *pathOpt, "imp");
                 if (!imported) {
                     Notification::create(imported.unwrapErr(),
                         NotificationIcon::Error, 3.f)->show();
                     return;
                 }
                 auto file = imported.unwrap();
                 self->edit({}, [&] {
                     if (auto* p = self->selectedPiece()) {
                         p->shape.kind = PieceShape::Kind::Import;
                         p->shape.file = file;
                     }
                 });
                 self->scheduleInspectorRebuild();
             });
         }, false},
        {"Usar la forma de un icono oficial", "El dibujo del juego, para pintarlo tu.",
         [self] {
             if (!self) return;
             auto* picker = TemplatePickerPopup::create(self->m_project.type,
                 [self](int iconId) {
                     if (paimon::isRuntimeShuttingDown() || !self) return;
                     auto* piece = self->selectedPiece();
                     if (!piece) return;
                     auto zones = self->visibleZones();
                     if (zones.empty()) return;
                     auto zoneKey = std::string(
                         zones[static_cast<std::size_t>(self->m_zoneIndex)].key);

                     IconPiece updated = *piece;
                     if (auto r = fillPieceFromTemplate(self->m_project, updated, iconId,
                                                        self->m_currentPart, zoneKey); !r) {
                         Notification::create(("Forma oficial: " + r.unwrapErr()).c_str(),
                             NotificationIcon::Error, 3.f)->show();
                         return;
                     }
                     self->edit({}, [&] {
                         if (auto* p = self->selectedPiece()) p->shape = updated.shape;
                     });
                     self->scheduleInspectorRebuild();
                 });
             if (picker) picker->show();
         }, false},
    };

    if (auto* sheet = IconActionSheet::create("Cambiar el dibujo", std::move(actions))) {
        sheet->show();
    }
}

void IconEditorLayer::onLayerMenu(int pieceIndex) {
    auto& pieces = currentSlot().pieces;
    if (pieceIndex < 0 || pieceIndex >= static_cast<int>(pieces.size())) return;
    std::string const name = pieces[static_cast<std::size_t>(pieceIndex)].name;

    Ref<IconEditorLayer> self = this;
    std::vector<IconActionSheet::Action> actions{
        {"Renombrar", "", [self, pieceIndex] {
            if (!self) return;
            auto& list = self->currentSlot().pieces;
            if (pieceIndex >= static_cast<int>(list.size())) return;
            auto* popup = IconNamePopup::create("Nombre de la capa", "Capa",
                list[static_cast<std::size_t>(pieceIndex)].name,
                [self, pieceIndex](std::string const& value) {
                    if (paimon::isRuntimeShuttingDown() || !self) return;
                    self->edit({}, [&] {
                        auto& target = self->currentSlot().pieces;
                        if (pieceIndex < static_cast<int>(target.size())) {
                            target[static_cast<std::size_t>(pieceIndex)].name = value;
                        }
                    });
                    self->scheduleInspectorRebuild();
                });
            if (popup) popup->show();
        }, false},
        {"Duplicar", "Una copia identica encima de esta.", [self, pieceIndex] {
            if (!self) return;
            self->edit({}, [&] {
                auto& list = self->currentSlot().pieces;
                if (pieceIndex >= static_cast<int>(list.size())) return;
                IconPiece copy = list[static_cast<std::size_t>(pieceIndex)];
                copy.id = self->m_project.makePieceId();
                copy.name += " (copia)";
                list.insert(list.begin() + pieceIndex + 1, std::move(copy));
                self->m_selectedPiece = pieceIndex + 1;
            });
            self->refreshZoneChips();
            self->scheduleInspectorRebuild();
        }, false},
        {"Copiar a otras zonas", "Repite esta capa en el resto de las zonas.",
         [self, pieceIndex] { if (self) self->onCopyLayerToZones(pieceIndex); }, false},
        {"Borrar", "No se puede deshacer con el boton atras, pero si con deshacer.",
         [self, pieceIndex] {
             if (!self) return;
             self->edit({}, [&] {
                 auto& list = self->currentSlot().pieces;
                 if (pieceIndex >= static_cast<int>(list.size())) return;
                 list.erase(list.begin() + pieceIndex);
                 self->m_selectedPiece = list.empty()
                     ? -1 : std::min<int>(pieceIndex, static_cast<int>(list.size()) - 1);
             });
             self->refreshZoneChips();
             self->scheduleInspectorRebuild();
             self->setStatus("Capa borrada.");
         }, true},
    };

    if (auto* sheet = IconActionSheet::create(name, std::move(actions))) sheet->show();
}

void IconEditorLayer::onCopyLayerToZones(int pieceIndex) {
    auto& pieces = currentSlot().pieces;
    if (pieceIndex < 0 || pieceIndex >= static_cast<int>(pieces.size())) return;
    IconPiece const source = pieces[static_cast<std::size_t>(pieceIndex)];

    edit({}, [&] {
        for (auto const& zone : visibleZones()) {
            auto key = slotStorageKey(m_currentPart, zone.key);
            if (key == currentSlotKey()) continue;
            IconPiece copy = source;
            copy.id = m_project.makePieceId();
            m_project.slots[key].pieces.push_back(std::move(copy));
        }
    });
    refreshZoneChips();
    scheduleInspectorRebuild();
    setStatus("Capa copiada a las demas zonas.");
}

void IconEditorLayer::onCopyPartToOthers() {
    auto const* def = anatomyFor(m_project.type);
    if (!def || def->partCount <= 1) return;

    Ref<IconEditorLayer> self = this;
    PopupManager::get().quickPopup(
        "Copiar parte",
        fmt::format("Dejar las otras partes iguales a la <cy>parte {}</c>?\n"
                    "Se reemplaza lo que tengan ahora.", m_currentPart),
        "Cancelar", "Copiar",
        [self](FLAlertLayer*, bool confirmed) {
            if (!confirmed || paimon::isRuntimeShuttingDown() || !self) return;
            auto const* anatomy = anatomyFor(self->m_project.type);
            if (!anatomy) return;

            self->edit({}, [&] {
                for (auto const& slot : anatomy->slots) {
                    if (slot.key == "extra") continue;
                    auto sourceKey = slotStorageKey(self->m_currentPart, slot.key);
                    auto sourceIt = self->m_project.slots.find(sourceKey);
                    for (int part = 1; part <= anatomy->partCount; ++part) {
                        if (part == self->m_currentPart) continue;
                        auto targetKey = slotStorageKey(part, slot.key);
                        if (sourceIt == self->m_project.slots.end()) {
                            self->m_project.slots.erase(targetKey);
                            continue;
                        }
                        auto copy = sourceIt->second;
                        for (auto& piece : copy.pieces) {
                            piece.id = self->m_project.makePieceId();
                        }
                        self->m_project.slots[targetKey] = std::move(copy);
                    }
                }
            });
            self->setStatus("Partes copiadas.");
        }).showInstant();
}

void IconEditorLayer::onProjectMenu() {
    Ref<IconEditorLayer> self = this;
    std::vector<IconActionSheet::Action> actions{
        {"Guardar ahora", "El icono se guarda solo, esto es por si acaso.",
         [self] { if (self) self->saveProject(true); }, false},
        {"Renombrar icono", "", [self] { if (self) self->onRename(); }, false},
        {"Exportar o instalar", "Archivos del icono y copia a More Icons.",
         [self] { if (self) self->onExport(); }, false},
        {"Como funciona", "Guia rapida del creador.",
         [] { if (auto* p = IconHelpPopup::create()) p->show(); }, false},
    };
    if (auto* sheet = IconActionSheet::create("Opciones", std::move(actions))) {
        sheet->show();
    }
}

void IconEditorLayer::onCanvasDrag(float dxFraction, float dyFraction) {
    if (!selectedPiece()) {
        setStatus("Elige una capa para poder moverla.", false);
        return;
    }
    edit("drag", [&] {
        auto* piece = selectedPiece();
        piece->transform.offsetX =
            std::clamp(piece->transform.offsetX + dxFraction, -1.f, 1.f);
        piece->transform.offsetY =
            std::clamp(piece->transform.offsetY + dyFraction, -1.f, 1.f);
    });
}

void IconEditorLayer::nudgeSelected(float dxFraction, float dyFraction) {
    if (!selectedPiece()) return;
    edit("nudge", [&] {
        auto* piece = selectedPiece();
        piece->transform.offsetX =
            std::clamp(piece->transform.offsetX + dxFraction, -1.f, 1.f);
        piece->transform.offsetY =
            std::clamp(piece->transform.offsetY + dyFraction, -1.f, 1.f);
    });
}

void IconEditorLayer::saveProject(bool notify) {
    m_project.modifiedAt = nowUnixMs();
    if (auto r = IconProjectStore::get().saveProject(m_project); !r) {
        setStatus("No se pudo guardar: " + r.unwrapErr(), false);
        return;
    }
    m_dirty = false;
    m_autosaveCountdown = -1.f;
    IconThumbs::get().invalidate(m_project.id);
    if (notify) setStatus("Guardado.");
}

void IconEditorLayer::setStatus(std::string const& text, bool good) {
    if (!m_statusLabel) return;
    m_statusLabel->setString(text.c_str());
    m_statusLabel->setColor(good ? kit::kDescColor : mkui::kAccentDanger);
    m_statusLabel->setScale(0.45f);
    m_statusLabel->limitLabelWidth(
        CCDirector::get()->getWinSize().width * 0.38f, 0.45f, 0.28f);
}


void IconEditorLayer::schedulePreview() {
    m_previewCountdown = 0.28f;
}

void IconEditorLayer::update(float dt) {
    if (m_autosaveCountdown > 0.f) {
        m_autosaveCountdown -= dt;
        if (m_autosaveCountdown <= 0.f && m_dirty) saveProject(false);
    }

    kit::stepWheelScroll(m_inspector, m_wheelTargetY, m_wheelTargetSet, dt);

    if (m_previewCountdown < 0.f) return;
    m_previewCountdown -= dt;
    if (m_previewCountdown > 0.f) return;
    m_previewCountdown = -1.f;
    if (m_compileBusy) {
        m_previewCountdown = 0.15f;
        return;
    }
    kickPreviewJob();
}

void IconEditorLayer::scrollWheel(float x, float y) {
    if (kit::queueWheelScroll(m_inspector, x, y, m_wheelTargetY, m_wheelTargetSet)) {
        return;
    }
    CCLayer::scrollWheel(x, y);
}

void IconEditorLayer::kickPreviewJob() {
    auto const* def = anatomyFor(m_project.type);
    if (!def || !m_canvas) return;

    m_compileBusy = true;
    int generation = m_generation->fetch_add(1) + 1;

    IconProject snapshot = m_project;
    int canvasSize = def->canvasUhd;
    auto imagesDir = IconPaths::imagesDir(snapshot.id);
    std::string activeKey = currentSlotKey();

    // Draw back-to-front so glow stays behind white details.
    std::vector<std::string> keys;
    for (char const* key : {"glow", "tertiary", "secondary", "main", "extra"}) {
        for (auto const& slot : def->slots) {
            if (slot.key != key) continue;
            if (def->partCount > 1 && m_currentPart > 1 && slot.key == "extra") continue;
            keys.push_back(slotStorageKey(m_currentPart, key));
        }
    }

    Ref<IconEditorLayer> self = this;
    auto generationBox = m_generation;

    paimon::ThreadTracker::get().spawn(
        [self, generationBox, generation, snapshot, keys, canvasSize, imagesDir, activeKey]() {
            std::vector<EditorCanvas::Zone> zones;
            zones.reserve(keys.size());
            for (auto const& key : keys) {
                ts::ImageBuffer pixels;
                if (auto r = PieceRenderer::renderSlot(snapshot, key, canvasSize, imagesDir)) {
                    pixels = r.unwrap();
                }
                zones.push_back({key, std::move(pixels)});
            }

            Loader::get()->queueInMainThread(
                [self, generationBox, generation, activeKey,
                 zones = std::move(zones)]() mutable {
                    if (paimon::isRuntimeShuttingDown() || !self) return;
                    self->m_compileBusy = false;
                    if (generationBox->load() != generation) return;
                    if (self->m_canvas) {
                        self->m_canvas->setZones(std::move(zones), activeKey);
                        self->m_canvas->setIsolate(self->m_isolateZone);
                    }
                });
        });
}


void IconEditorLayer::onApply() {
    if (m_compileBusy) {
        setStatus("Un momento, todavia esta dibujando...");
        return;
    }
    saveProject(false);
    setStatus("Preparando el icono...");
    m_compileBusy = true;

    Ref<IconEditorLayer> self = this;
    IconBuildService::buildAndApply(m_project,
        [self](geode::Result<std::string> result) {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            self->m_compileBusy = false;
            if (!result) {
                self->setStatus("Error: " + result.unwrapErr(), false);
                return;
            }
            self->m_project.hasBuiltOnce = true;
            self->m_project.lastBuiltAt = nowUnixMs();
            self->setStatus(result.unwrap());
        });
}

void IconEditorLayer::onExport() {
    if (m_compileBusy) {
        setStatus("Un momento, todavia esta dibujando...");
        return;
    }
    saveProject(false);
    setStatus("Generando los archivos...");
    m_compileBusy = true;

    Ref<IconEditorLayer> self = this;
    IconBuildService::build(m_project, [self](geode::Result<std::string> result) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        self->m_compileBusy = false;
        if (!result) {
            self->setStatus("Error: " + result.unwrapErr(), false);
            return;
        }
        self->m_project.hasBuiltOnce = true;
        self->m_project.lastBuiltAt = nowUnixMs();
        self->setStatus("Archivos generados.");

        auto const* def = anatomyFor(self->m_project.type);
        std::string folder = def ? std::string(def->folderName) : "icon";
        std::string projectId = self->m_project.id;

        PopupManager::get().quickPopup(
            "Exportar icono",
            "Ya estan los archivos en SD, HD y UHD.\n"
            "<cy>Carpeta</c>: abrelos para usarlos donde quieras.\n"
            "<cg>More Icons</c>: los instala ahi para que el icono\n"
            "siga funcionando aunque quites Paimbnails.",
            "Carpeta", "More Icons",
            [projectId, folder](FLAlertLayer*, bool toMoreIcons) {
                auto outputDir = IconPaths::outputDir(projectId);
                if (!toMoreIcons) {
                    geode::utils::file::openFolder(outputDir);
                    return;
                }
                auto targetDir = Mod::get()->getConfigDir().parent_path()
                    / "hiimjustin000.more_icons" / folder;
                std::error_code ec;
                std::filesystem::create_directories(targetDir, ec);
                if (ec) {
                    Notification::create("No se pudo crear la carpeta de More Icons.",
                        NotificationIcon::Error, 3.f)->show();
                    return;
                }
                int copied = 0;
                for (auto const& suffix : {"-uhd", "-hd", ""}) {
                    for (auto const& ext : {".png", ".plist"}) {
                        auto name = projectId + suffix + ext;
                        std::filesystem::copy_file(
                            outputDir / name, targetDir / name,
                            std::filesystem::copy_options::overwrite_existing, ec);
                        if (!ec) ++copied;
                    }
                }
                if (copied > 0) {
                    Notification::create(
                        "Copiado a More Icons. Reinicia el juego para verlo alli.",
                        NotificationIcon::Success, 3.f)->show();
                } else {
                    Notification::create("No se pudo copiar a More Icons.",
                        NotificationIcon::Error, 3.f)->show();
                }
            }).showInstant();
    });
}

}
