#include "IconEditorLayer.hpp"

#include "EditorCanvas.hpp"
#include "GradientEditorPopup.hpp"
#include "IconActionSheet.hpp"
#include "IconHelpPopup.hpp"
#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "IconNamePopup.hpp"
#include "IconTryPopup.hpp"
#include "TemplatePickerPopup.hpp"
#include "../data/IconAnatomy.hpp"
#include "../data/IconPalettes.hpp"
#include "../data/IconThemes.hpp"
#include "../engine/PieceRenderer.hpp"
#include "../engine/TemplateExtractor.hpp"
#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../persist/StyleStore.hpp"
#include "../services/IconBuildService.hpp"
#include "../services/IconShare.hpp"
#include "../services/IconThumbs.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>
#include <system_error>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;
namespace kit = paimon::icon_maker::gdkit;
namespace mkui = paimon::icon_maker::ui;

namespace paimon::icon_maker {

namespace {

constexpr float kMargin = 7.f;
constexpr float kTopBarH = 30.f;
constexpr float kPanelGap = 7.f;
constexpr float kPanelInset = 8.f;
constexpr float kMinWorkspaceW = 170.f;
constexpr float kMinInspectorW = 240.f;
constexpr float kMaxInspectorW = 330.f;
constexpr float kStripH = 30.f;

// touch hit-testing needs no full canvas resolution; at this size a mask is 14 KB, not 230.
constexpr int kHitMaskSize = 120;
constexpr int kThumbSize = 44;

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
    float tabsY;
    float stripY;
    float scrollX;
    float scrollY;
    float scrollW;
    float scrollH;
};

Layout layoutFor(CCSize const& win, bool hasParts, int zoneCount, bool showStrip) {
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
    float const canvasBottom = kMargin + 44.f;
    float const availableH = std::max(1.f, canvasTop - canvasBottom);
    float const availableW = std::max(1.f, out.workspaceW - 18.f);
    out.canvasSide = std::min(availableW, availableH);
    out.canvasCY = canvasBottom + availableH / 2.f;
    out.toolsY = kMargin + 28.f;
    out.statusY = kMargin + 10.f;

    out.scrollX = out.panelX + kPanelInset;
    out.scrollW = std::max(1.f, out.panelW - kPanelInset * 2.f);

    float top = out.bodyTop - 6.f;
    out.chipsY = top - mkui::zoneChipsHeight(out.scrollW, zoneCount);
    top = out.chipsY - 6.f;
    out.tabsY = top - kit::kTabBarHeight;
    top = out.tabsY - 5.f;
    out.stripY = showStrip ? top - kStripH : top;
    if (showStrip) top = out.stripY - 5.f;

    out.scrollY = kMargin + kPanelInset;
    out.scrollH = std::max(1.f, top - out.scrollY);
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

// Representative paint color for list swatches and theme chips.
ccColor3B fillSwatch(FillSpec const& fill) {
    if (fill.chroma) return {255, 255, 255};
    switch (fill.type) {
        case FillType::Gradient: {
            if (fill.gradient.stops.empty()) return {255, 255, 255};
            auto const& c = fill.gradient.stops.front().color;
            return {c.r, c.g, c.b};
        }
        case FillType::Image:
            return {150, 200, 255};
        case FillType::Flat:
        default:
            return {fill.flat.r, fill.flat.g, fill.flat.b};
    }
}

std::string layerSubtitle(IconPiece const& piece) {
    std::string text = fillTypeName(piece.fill);
    if (piece.fill.outline.enabled) text += " + borde";
    if (piece.locked) text += "  (bloqueada)";
    if (!piece.visible) text += "  (oculta)";
    return text;
}

// Copy a picked file into the project images directory under a unique name.
geode::Result<std::string> importImageFile(std::string const& slotId,
                                           std::string const& pieceId,
                                           std::filesystem::path const& source,
                                           char const* prefix) {
    auto ext = utils::string::pathToString(source.extension());
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

std::string suffixForZone(AnatomyDef const& def, std::string const& zoneKey, int part) {
    std::string suffix;
    for (auto const& slot : def.slots) {
        if (slot.key == zoneKey) { suffix = slot.suffix; break; }
    }
    if (suffix.empty()) return {};
    if (def.partCount > 1) suffix = fmt::format("_{:02}{}", part, suffix);
    return suffix;
}

// Extract one vanilla frame for the given zone.
geode::Result<> fillPieceFromTemplate(IconProject const& project, IconPiece& piece,
                                      int iconId, int part, std::string const& zoneKey) {
    auto const* def = anatomyFor(project.type);
    if (!def) return Err("Gamemode no soportado");

    auto suffix = suffixForZone(*def, zoneKey, part);
    if (suffix.empty()) return Err("Zona desconocida");

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

// theme grid; each shows the two colors it paints with.
CCNode* makeThemeGrid(float width, std::vector<IconTheme> const& themes,
                      std::function<void(IconTheme const&)> onPick) {
    constexpr float kChipH = 34.f;
    constexpr float kGap = 5.f;
    constexpr int kPerRow = 3;

    int const rows = std::max(1,
        (static_cast<int>(themes.size()) + kPerRow - 1) / kPerRow);
    float const chipW = (width - kGap * static_cast<float>(kPerRow - 1))
        / static_cast<float>(kPerRow);
    float const height = static_cast<float>(rows) * kChipH
        + static_cast<float>(rows - 1) * kGap;

    auto* grid = CCNode::create();
    grid->setAnchorPoint({0.f, 0.f});
    grid->setContentSize({width, height});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(
        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    grid->addChild(menu, 5);

    auto cb = std::make_shared<std::function<void(IconTheme const&)>>(std::move(onPick));

    for (std::size_t i = 0; i < themes.size(); ++i) {
        auto const& theme = themes[i];

        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({chipW, kChipH});

        if (auto* plate = paimon::SpriteHelper::createColorPanel(
                chipW, kChipH, {14, 24, 52}, 210, 4.f)) {
            plate->setAnchorPoint({0.f, 0.f});
            holder->addChild(plate, -1);
        }

        float const barW = (chipW - 10.f) / 2.f;
        auto addBar = [&](float x, ccColor3B color) {
            if (auto* bar = paimon::SpriteHelper::createColorPanel(
                    barW, 9.f, color, 255, 2.f)) {
                bar->setAnchorPoint({0.f, 0.f});
                bar->setPosition({x, kChipH - 13.f});
                holder->addChild(bar);
            }
        };
        addBar(5.f, fillSwatch(theme.main));
        addBar(5.f + barW, fillSwatch(theme.secondary));

        auto* label = CCLabelBMFont::create(theme.name.c_str(), "bigFont.fnt");
        label->setAnchorPoint({0.5f, 0.5f});
        label->limitLabelWidth(chipW - 8.f, 0.32f, 0.12f);
        label->setPosition({chipW / 2.f, 9.f});
        holder->addChild(label);

        auto* btn = CCMenuItemExt::createSpriteExtra(holder,
            [cb, theme](CCMenuItemSpriteExtra*) { if (*cb) (*cb)(theme); });
        int const col = static_cast<int>(i) % kPerRow;
        int const line = static_cast<int>(i) / kPerRow;
        btn->setPosition({
            chipW / 2.f + static_cast<float>(col) * (chipW + kGap),
            height - kChipH / 2.f - static_cast<float>(line) * (kChipH + kGap),
        });
        menu->addChild(btn);
    }

    return grid;
}

}  // anonymous namespace


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
    StyleStore::get().load();

    setKeypadEnabled(true);
    setID("icon-maker-editor"_spr);

    buildBackground();
    buildTopBar();
    buildWorkspace();
    buildInspector();

    scheduleUpdate();
    schedulePreview(false);
    maybeShowTour();
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
    m_titleLabel->limitLabelWidth(win.width * 0.30f, 0.52f, 0.2f);

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

    auto addWideButton = [&](char const* text, char const* sprite,
                             std::function<void()> action) {
        auto* spr = ButtonSprite::create(text, "goldFont.fnt", sprite, 0.8f);
        if (!spr) return;
        spr->setScale(0.62f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [action](CCMenuItemSpriteExtra*) { if (action) action(); });
        x -= btn->getScaledContentSize().width / 2.f;
        btn->setPosition({x, cy});
        menu->addChild(btn);
        x -= btn->getScaledContentSize().width / 2.f + 5.f;
    };

    addWideButton("Usar", "GJ_button_01.png", [this] { this->onApply(); });
    addWideButton("Probar", "GJ_button_02.png", [this] { this->onTry(); });

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
            CCDirector::get()->getWinSize().width * 0.30f, 0.52f, 0.2f);
    }
}

void IconEditorLayer::buildWorkspace() {
    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    bool const hasParts = def && def->partCount > 1;
    auto layout = layoutFor(win, hasParts,
        static_cast<int>(visibleZones().size()), m_tab != Tab::Layers);

    if (auto* window = kit::makeWindow({layout.workspaceW, layout.panelH})) {
        window->setPosition({layout.workspaceX, kMargin});
        addChild(window, 2);
    }

    EditorCanvas::Callbacks callbacks;
    callbacks.onSelect = [this](std::string const& zoneKey, int pieceIndex) {
        this->selectFromCanvas(zoneKey, pieceIndex);
    };
    callbacks.onMove = [this](float dx, float dy) {
        m_gestureActive = true;
        edit("canvas-move", [&] {
            auto* piece = selectedPiece();
            if (!piece) return;
            piece->transform.offsetX = std::clamp(piece->transform.offsetX + dx, -1.f, 1.f);
            piece->transform.offsetY = std::clamp(piece->transform.offsetY + dy, -1.f, 1.f);
        });
    };
    callbacks.onScale = [this](float factorX, float factorY) {
        m_gestureActive = true;
        edit("canvas-scale", [&] {
            auto* piece = selectedPiece();
            if (!piece) return;
            // shared stretch goes to overall size, remainder per axis, so diagonal
            // drags don't cap out early.
            float const uniform = std::sqrt(std::max(0.0001f, factorX * factorY));
            piece->transform.scale = std::clamp(piece->transform.scale * uniform, 0.05f, 3.f);
            piece->scaleX = std::clamp(piece->scaleX * (factorX / uniform), 0.25f, 4.f);
            piece->scaleY = std::clamp(piece->scaleY * (factorY / uniform), 0.25f, 4.f);
        });
    };
    callbacks.onRotate = [this](float deltaDeg) {
        m_gestureActive = true;
        edit("canvas-rotate", [&] {
            auto* piece = selectedPiece();
            if (!piece) return;
            float angle = piece->transform.rotationDeg + deltaDeg;
            while (angle > 180.f) angle -= 360.f;
            while (angle < -180.f) angle += 360.f;
            piece->transform.rotationDeg = angle;
        });
    };
    callbacks.onPick = [this](ccColor3B color) { this->pickColor(color); };
    callbacks.onHint = [this](std::string const& text) { this->setStatus(text, false); };
    callbacks.onGestureEnd = [this] {
        m_gestureActive = false;
        m_history.breakCoalescing();
        schedulePreview(false);
        scheduleInspectorRebuild();
    };

    m_canvas = EditorCanvas::create(layout.canvasSide, std::move(callbacks));
    if (m_canvas) {
        m_canvas->setPosition({layout.workspaceCX, layout.canvasCY});
        m_canvas->setBackgroundMode(m_backgroundMode);
        m_canvas->setGuideVisible(m_showGuide);
        addChild(m_canvas, 5);
    }

    // robot/spider carry four drawings edited separately.
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

    // these tools only change the view, never the icon.
    auto* toolMenu = CCMenu::create();
    toolMenu->setPosition({0.f, 0.f});
    addChild(toolMenu, 6);

    float const toolW = layout.canvasSide / 4.f;
    float const toolLeft = layout.workspaceCX - layout.canvasSide / 2.f;
    m_toolLabelW = std::max(8.f, toolW - 12.f);

    // ButtonSprite owns its art, so state goes in the text.
    auto makeToolButton = [&](float cx, cocos2d::CCLabelBMFont** out,
                              std::function<void(CCMenuItemSpriteExtra*)> action) {
        float const btnW = std::max(12.f, toolW - 5.f);
        constexpr float btnH = 21.f;

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
    makeToolButton(toolLeft + toolW * 3.5f, &m_pickToolLabel,
        [this](CCMenuItemSpriteExtra*) {
            m_eyedropper = !m_eyedropper;
            if (m_canvas) m_canvas->setEyedropper(m_eyedropper);
            // icon pixels only reach the canvas with the eyedropper on, so redraw.
            if (m_eyedropper) schedulePreview(false);
            setStatus(m_eyedropper
                ? "Toca el icono para copiar ese color."
                : "Cuentagotas apagado.");
            refreshViewTools();
        });
    refreshViewTools();

    m_statusLabel = CCLabelBMFont::create(
        "Toca una capa para elegirla; arrastrala para moverla.", "chatFont.fnt");
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
    bool const compact = maxW < 46.f;
    auto setLabel = [maxW](CCLabelBMFont* label, char const* text) {
        if (!label) return;
        label->setString(text);
        label->limitLabelWidth(maxW, 0.38f, 0.1f);
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
    setLabel(m_pickToolLabel, m_eyedropper
        ? (compact ? "Copiando" : "Copiando color")
        : (compact ? "Pipeta" : "Cuentagotas"));
}


void IconEditorLayer::buildInspector() {
    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    auto layout = layoutFor(win, def && def->partCount > 1,
        static_cast<int>(visibleZones().size()), m_tab != Tab::Layers);

    if (auto* window = kit::makeWindow({layout.panelW, layout.panelH})) {
        window->setPosition({layout.panelX, kMargin});
        addChild(window, 2);
    }

    m_zoneChipsHost = CCNode::create();
    m_zoneChipsHost->setPosition({layout.scrollX, layout.chipsY});
    addChild(m_zoneChipsHost, 6);

    // hangs off the strip so spacing tracks chips when robot parts change zone counts.
    if (auto* line = paimon::SpriteHelper::createColorPanel(
            layout.scrollW, 1.f, {255, 255, 255}, 60, 0.f)) {
        line->setAnchorPoint({0.f, 0.f});
        line->setPosition({0.f, -4.f});
        m_zoneChipsHost->addChild(line);
    }

    m_tabsHost = CCNode::create();
    addChild(m_tabsHost, 6);

    m_stripHost = CCNode::create();
    addChild(m_stripHost, 6);

    m_inspectorHost = CCNode::create();
    addChild(m_inspectorHost, 6);

    refreshZoneChips();
    rebuildInspector();
}

void IconEditorLayer::refreshZoneChips() {
    if (!m_zoneChipsHost) return;

    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    auto layout = layoutFor(win, def && def->partCount > 1,
        static_cast<int>(visibleZones().size()), m_tab != Tab::Layers);
    m_zoneChipsHost->setPosition({layout.scrollX, layout.chipsY});

    std::vector<mkui::ZoneChip> chips;
    for (auto const& zone : visibleZones()) {
        auto key = slotStorageKey(m_currentPart, zone.key);
        mkui::ZoneChip chip;
        chip.label = std::string(zone.label);
        chip.accent = zone.accent;
        if (auto it = m_project.slots.find(key); it != m_project.slots.end()) {
            chip.layerCount = static_cast<int>(it->second.pieces.size());
        }
        if (auto it = m_zoneTextures.find(key); it != m_zoneTextures.end()) {
            chip.preview = it->second;
        }
        chips.push_back(std::move(chip));
    }

    m_zoneChipsHost->removeAllChildren();
    if (auto* line = paimon::SpriteHelper::createColorPanel(
            layout.scrollW, 1.f, {255, 255, 255}, 60, 0.f)) {
        line->setAnchorPoint({0.f, 0.f});
        line->setPosition({0.f, -4.f});
        m_zoneChipsHost->addChild(line);
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

void IconEditorLayer::refreshSelectionStrip() {
    if (!m_stripHost) return;
    m_stripHost->removeAllChildren();
    if (m_tab == Tab::Layers) return;

    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    auto layout = layoutFor(win, def && def->partCount > 1,
        static_cast<int>(visibleZones().size()), true);
    m_stripHost->setPosition({layout.scrollX, layout.stripY});

    auto zones = visibleZones();
    if (zones.empty()) return;
    auto const& zone = zones[static_cast<std::size_t>(
        std::clamp(m_zoneIndex, 0, static_cast<int>(zones.size()) - 1))];

    auto* piece = selectedPiece();
    float const width = layout.scrollW;

    auto* holder = CCNode::create();
    holder->setAnchorPoint({0.f, 0.f});
    holder->setContentSize({width, kStripH});
    if (auto* plate = kit::makePlate(width, kStripH, {255, 255, 255}, 190)) {
        plate->setPosition({0.f, 0.f});
        holder->addChild(plate, -1);
    }

    CCTexture2D* thumb = nullptr;
    if (piece) {
        if (auto it = m_pieceThumbs.find(piece->id); it != m_pieceThumbs.end()) {
            thumb = it->second;
        }
    }
    if (auto* mini = mkui::makeThumb(22.f, thumb,
            piece ? fillSwatch(piece->fill) : ccColor3B{90, 100, 120})) {
        mini->setPosition({16.f, kStripH / 2.f});
        holder->addChild(mini);
    }

    auto* name = CCLabelBMFont::create(
        piece ? piece->name.c_str() : "Ninguna capa elegida", "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.f});
    name->limitLabelWidth(width - 96.f, 0.36f, 0.12f);
    name->setPosition({31.f, kStripH / 2.f + 1.f});
    holder->addChild(name);

    auto* where = CCLabelBMFont::create(
        fmt::format("{}  -  toca para ver las capas", zone.label).c_str(), "chatFont.fnt");
    where->setAnchorPoint({0.f, 1.f});
    where->setScale(0.34f);
    where->setColor(kit::kDescColor);
    where->limitLabelWidth((width - 96.f) / 0.34f, 0.34f, 0.14f);
    where->setPosition({31.f, kStripH / 2.f});
    holder->addChild(where);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(
        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    holder->addChild(menu, 5);

    auto* hit = CCNode::create();
    hit->setAnchorPoint({0.5f, 0.5f});
    hit->setContentSize({width, kStripH});
    auto* btn = CCMenuItemExt::createSpriteExtra(hit,
        [this](CCMenuItemSpriteExtra*) { this->selectTab(Tab::Layers); });
    btn->setPosition({width / 2.f, kStripH / 2.f});
    menu->addChild(btn);

    m_stripHost->addChild(holder);
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
    if (!m_inspectorHost || !m_tabsHost) return;

    if (m_inspector) {
        // keep the scroll offset so rebuilding the list doesn't jump to top.
        if (auto* content = m_inspector->m_contentLayer) {
            m_inspectorScrollY = content->getPositionY();
        }
        m_inspector->removeFromParent();
        m_inspector = nullptr;
    }
    m_wheelTargetSet = false;

    auto win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    bool const showStrip = m_tab != Tab::Layers;
    auto layout = layoutFor(win, def && def->partCount > 1,
        static_cast<int>(visibleZones().size()), showStrip);

    m_tabsHost->removeAllChildren();
    m_tabsHost->setPosition({layout.scrollX, layout.tabsY});
    if (auto* tabs = kit::makeTabBar(layout.scrollW,
            {"Capas", "Pintura", "Forma", "Icono"}, static_cast<int>(m_tab),
            [this](int index) { this->selectTab(static_cast<Tab>(index)); })) {
        m_tabsHost->addChild(tabs);
    }

    refreshSelectionStrip();

    float const cardW = layout.scrollW;
    std::vector<CCNode*> rows;
    switch (m_tab) {
        case Tab::Paint: rows = buildPaintTab(cardW);  break;
        case Tab::Shape: rows = buildShapeTab(cardW);  break;
        case Tab::Icon:  rows = buildIconTab(cardW);   break;
        case Tab::Layers:
        default:         rows = buildLayersTab(cardW); break;
    }
    rows.erase(std::remove(rows.begin(), rows.end(), nullptr), rows.end());

    m_inspectorHost->setPosition({layout.scrollX, layout.scrollY});
    m_inspector = kit::makeScrollStack({cardW, layout.scrollH}, rows, 6.f);
    if (m_inspector) {
        m_inspector->setPosition({0.f, 0.f});
        m_inspectorHost->addChild(m_inspector);

        if (auto* content = m_inspector->m_contentLayer) {
            float const minY = layout.scrollH - content->getContentSize().height;
            content->setPositionY(std::clamp(m_inspectorScrollY, std::min(minY, 0.f), 0.f));
        }
    }

    refreshTopBar();
}


std::vector<CCNode*> IconEditorLayer::buildLayersTab(float width) {
    std::vector<CCNode*> rows;
    auto zones = visibleZones();
    if (zones.empty()) return rows;

    auto& slot = currentSlot();
    auto const& zone = zones[static_cast<std::size_t>(
        std::clamp(m_zoneIndex, 0, static_cast<int>(zones.size()) - 1))];

    rows.push_back(kit::makeHint(width, std::string(zone.hint).c_str()));

    if (slot.pieces.empty()) {
        rows.push_back(mkui::makeEmptyState(width, "Zona vacia",
            "Agrega una capa: una imagen tuya, o la forma de un icono oficial "
            "para pintarla a tu gusto."));
    }

    int const count = static_cast<int>(slot.pieces.size());
    for (int display = 0; display < count; ++display) {
        int const index = count - 1 - display;
        auto const& piece = slot.pieces[static_cast<std::size_t>(index)];

        mkui::LayerRowSpec spec;
        spec.name = piece.name;
        spec.subtitle = layerSubtitle(piece);
        spec.swatch = fillSwatch(piece.fill);
        if (auto it = m_pieceThumbs.find(piece.id); it != m_pieceThumbs.end()) {
            spec.thumb = it->second;
        }
        spec.visible = piece.visible;
        spec.locked = piece.locked;
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
        spec.onToggleLock = [this, index] {
            edit({}, [&] {
                auto& pieces = currentSlot().pieces;
                if (index < static_cast<int>(pieces.size())) {
                    pieces[static_cast<std::size_t>(index)].locked =
                        !pieces[static_cast<std::size_t>(index)].locked;
                }
            });
            pushCanvasSelection();
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

        rows.push_back(mkui::makeLayerRow(width, std::move(spec)));
    }

    rows.push_back(mkui::makeDualButtonRow(width,
        "+ Imagen", [this] { this->onAddImportLayer(); },
        "+ Forma oficial", [this] { this->onAddTemplateLayer(); }));

    return rows;
}


std::vector<CCNode*> IconEditorLayer::buildPaintTab(float width) {
    std::vector<CCNode*> rows;
    auto* piece = selectedPiece();

    if (!piece) {
        rows.push_back(mkui::makeEmptyState(width, "Ninguna capa elegida",
            "Toca una capa en el icono, o abre la pestana Capas y elige una."));
        return rows;
    }

    int const typeIndex = piece->fill.type == FillType::Gradient ? 1
                        : piece->fill.type == FillType::Image ? 2 : 0;
    rows.push_back(kit::makeTabBar(width, {"Color", "Degradado", "Imagen"}, typeIndex,
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
            rows.push_back(mkui::makeSwatchGrid(width, quickColors(), piece->fill.flat,
                [this](ccColor3B color) { this->pickColor(color); },
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
                        rememberColor({picked.r, picked.g, picked.b});
                        self->scheduleInspectorRebuild();
                    });
                    picker->show();
                }));

            rows.push_back(mkui::makeHexRow(width, piece->fill.flat,
                [this](ccColor3B color) {
                    edit("hex-color", [&] {
                        if (auto* p = selectedPiece()) {
                            p->fill.flat = {color.r, color.g, color.b, 255};
                            p->fill.chroma = false;
                        }
                    });
                }));

            if (auto const& recent = recentColors(); !recent.empty()) {
                rows.push_back(kit::makeHint(width, "Los ultimos colores que usaste."));
                rows.push_back(mkui::makeSwatchGrid(width, recent, piece->fill.flat,
                    [this](ccColor3B color) { this->pickColor(color); }, nullptr));
            }

            if (auto mine = playerColors(); !mine.empty()) {
                rows.push_back(kit::makeHint(width,
                    "Y estos son tus colores de jugador, por si quieres que el "
                    "icono combine con tu kit."));
                rows.push_back(mkui::makeSwatchGrid(width, mine, piece->fill.flat,
                    [this](ccColor3B color) { this->pickColor(color); }, nullptr));
            }
            break;
        }

        case FillType::Gradient: {
            rows.push_back(mkui::makeGradientRow(width, piece->fill.gradient, "Editar",
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

            rows.push_back(kit::makeButtonRow(width, "Combinaciones listas",
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
            rows.push_back(kit::makeButtonRow(width, "Imagen de relleno",
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
            rows.push_back(kit::makeSelectRow(width, "Encaje",
                "Como se acomoda la imagen dentro de la forma.",
                fits, static_cast<int>(piece->fill.image.fit),
                [this](int index) {
                    edit({}, [&] {
                        if (auto* p = selectedPiece()) {
                            p->fill.image.fit = static_cast<FillFitMode>(index);
                        }
                    });
                }));

            rows.push_back(kit::makeNumberRow(width, "Zoom de la imagen", nullptr,
                piece->fill.image.scale, 0.05, 6.0, 0.05, 2,
                [this](double v) {
                    edit("fill-scale", [&] {
                        if (auto* p = selectedPiece()) p->fill.image.scale = static_cast<float>(v);
                    });
                }));
            rows.push_back(kit::makeNumberRow(width, "Giro de la imagen (grados)", nullptr,
                piece->fill.image.rotationDeg, -180.0, 180.0, 5.0, 0,
                [this](double v) {
                    edit("fill-rot", [&] {
                        if (auto* p = selectedPiece()) {
                            p->fill.image.rotationDeg = static_cast<float>(v);
                        }
                    });
                }));
            rows.push_back(kit::makeNumberRow(width, "Opacidad de la imagen (%)", nullptr,
                piece->fill.image.opacity / 2.55, 0.0, 100.0, 5.0, 0,
                [this](double v) {
                    edit("fill-opacity", [&] {
                        if (auto* p = selectedPiece()) {
                            p->fill.image.opacity = static_cast<int>(std::lround(v * 2.55));
                        }
                    });
                }));
            break;
        }
    }

    rows.push_back(kit::makeToggleRow(width, "Sombreado 3D",
        "Conserva las sombras del dibujo original bajo el color.",
        piece->fill.keepLuminance,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->fill.keepLuminance = value;
            });
        }));

    rows.push_back(kit::makeToggleRow(width, "Arcoiris animado",
        "La capa cambia de color sola. Solo se anima en el garaje.",
        piece->fill.chroma,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->fill.chroma = value;
            });
            scheduleInspectorRebuild();
        }));

    // outline lives with the fill: another color pass over the same shape.
    rows.push_back(kit::makeToggleRow(width, "Borde",
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
        rows.push_back(kit::makeNumberRow(width, "Grosor del borde", nullptr,
            piece->fill.outline.width, 1.0, 20.0, 1.0, 0,
            [this](double v) {
                edit("outline-width", [&] {
                    if (auto* p = selectedPiece()) p->fill.outline.width = static_cast<float>(v);
                });
            }));
        rows.push_back(mkui::makeColorRow(width, "Color del borde", nullptr,
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
                    rememberColor({picked.r, picked.g, picked.b});
                    self->scheduleInspectorRebuild();
                });
                picker->show();
            }));
    }

    rows.push_back(mkui::makeDualButtonRow(width,
        "Guardar pintura", [this] { this->onSaveStyle(); },
        "Usar guardada", [this] { this->onApplyStyle(); }));

    rows.push_back(kit::makeButtonRow(width, "Pintar toda la zona",
        "Deja el resto de capas de esta zona con la misma pintura.",
        "Pintar", [this] {
            auto* p = selectedPiece();
            if (!p) return;
            auto fill = p->fill;
            applyFillToZone(fill, currentSlotKey());
            setStatus("Zona pintada igual.");
        }));

    return rows;
}


std::vector<CCNode*> IconEditorLayer::buildShapeTab(float width) {
    std::vector<CCNode*> rows;
    auto* piece = selectedPiece();

    if (!piece) {
        rows.push_back(mkui::makeEmptyState(width, "Ninguna capa elegida",
            "Toca una capa en el icono para moverla, estirarla o girarla."));
        return rows;
    }

    rows.push_back(kit::makeHint(width,
        "En el icono puedes arrastrar la capa, estirarla por las esquinas y "
        "girarla con el tirador de arriba. Aqui pones los numeros exactos."));

    rows.push_back(kit::makeNumberRow(width, "Tamano", nullptr,
        piece->transform.scale, 0.05, 3.0, 0.05, 2,
        [this](double v) {
            edit("scale", [&] {
                if (auto* p = selectedPiece()) p->transform.scale = static_cast<float>(v);
            });
        }));

    rows.push_back(kit::makeNumberRow(width, "Ancho", "Estira solo a lo ancho.",
        piece->scaleX, 0.25, 4.0, 0.05, 2,
        [this](double v) {
            edit("scale-x", [&] {
                if (auto* p = selectedPiece()) p->scaleX = static_cast<float>(v);
            });
        }));

    rows.push_back(kit::makeNumberRow(width, "Alto", "Estira solo a lo alto.",
        piece->scaleY, 0.25, 4.0, 0.05, 2,
        [this](double v) {
            edit("scale-y", [&] {
                if (auto* p = selectedPiece()) p->scaleY = static_cast<float>(v);
            });
        }));

    rows.push_back(kit::makeNumberRow(width, "Giro (grados)", nullptr,
        piece->transform.rotationDeg, -180.0, 180.0, 5.0, 0,
        [this](double v) {
            edit("rotation", [&] {
                if (auto* p = selectedPiece()) p->transform.rotationDeg = static_cast<float>(v);
            });
        }));

    rows.push_back(kit::makeNumberRow(width, "Opacidad (%)", nullptr,
        piece->transform.opacity / 2.55, 0.0, 100.0, 5.0, 0,
        [this](double v) {
            edit("opacity", [&] {
                if (auto* p = selectedPiece()) {
                    p->transform.opacity = static_cast<int>(std::lround(v * 2.55));
                }
            });
        }));

    rows.push_back(kit::makeNumberRow(width, "Posicion horizontal", nullptr,
        piece->transform.offsetX, -1.0, 1.0, 0.02, 2,
        [this](double v) {
            edit("offset-x", [&] {
                if (auto* p = selectedPiece()) p->transform.offsetX = static_cast<float>(v);
            });
        }));

    rows.push_back(kit::makeNumberRow(width, "Posicion vertical", nullptr,
        piece->transform.offsetY, -1.0, 1.0, 0.02, 2,
        [this](double v) {
            edit("offset-y", [&] {
                if (auto* p = selectedPiece()) p->transform.offsetY = static_cast<float>(v);
            });
        }));

    rows.push_back(mkui::makeAlignRow(width, "Alinear",
        "Pega la capa a un lado del cuadro recomendado, o la centra.",
        [this](mkui::AlignMode mode) { this->alignSelected(mode); }));

    rows.push_back(kit::makeToggleRow(width, "Espejo horizontal", nullptr,
        piece->transform.flipX,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->transform.flipX = value;
            });
        }));
    rows.push_back(kit::makeToggleRow(width, "Espejo vertical", nullptr,
        piece->transform.flipY,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->transform.flipY = value;
            });
        }));

    rows.push_back(kit::makeToggleRow(width, "Bloquear",
        "Una capa bloqueada no se mueve ni se estira desde el icono.",
        piece->locked,
        [this](bool value) {
            edit({}, [&] {
                if (auto* p = selectedPiece()) p->locked = value;
            });
            pushCanvasSelection();
            scheduleInspectorRebuild();
        }));

    rows.push_back(kit::makeButtonRow(width, "Cambiar el dibujo",
        "Sustituye la forma de esta capa sin perder como esta pintada.",
        "Cambiar", [this] { this->onReplaceShape(); }));

    rows.push_back(kit::makeButtonRow(width, "Restablecer",
        "Devuelve tamano, giro y posicion a como estaban al empezar.",
        "Reiniciar", [this] {
            edit({}, [&] {
                if (auto* p = selectedPiece()) {
                    p->transform = ts::ImageTransform{};
                    p->scaleX = 1.f;
                    p->scaleY = 1.f;
                }
            });
            scheduleInspectorRebuild();
            setStatus("Forma restablecida.");
        }));

    return rows;
}


std::vector<CCNode*> IconEditorLayer::buildIconTab(float width) {
    std::vector<CCNode*> rows;
    auto const* def = anatomyFor(m_project.type);

    rows.push_back(kit::makeHint(width,
        "Un tema pinta el icono entero de una vez: cuerpo, detalle, cupula y "
        "brillo. El blanco se queda como esta."));

    std::vector<IconTheme> themes;
    if (IconTheme mine; currentKitTheme(mine)) themes.push_back(std::move(mine));
    for (auto const& theme : iconThemes()) themes.push_back(theme);

    rows.push_back(makeThemeGrid(width, themes, [this](IconTheme const& theme) {
        this->applyTheme(theme, true);
    }));

    rows.push_back(kit::makeToggleRow(width, "Colores reales",
        "Muestra los colores que pintaste en vez de dejar que el juego los "
        "cambie por tus colores de jugador.",
        m_project.exactColors,
        [this](bool value) {
            edit({}, [&] { m_project.exactColors = value; });
        }));

    rows.push_back(kit::makeButtonRow(width, "Partir de un icono oficial",
        "Rellena todas las zonas con el dibujo de un icono del juego.",
        "Cargar", [this] { this->onLoadWholeIcon(); }));

    if (def && def->partCount > 1) {
        rows.push_back(kit::makeButtonRow(width, "Copiar esta parte",
            "Deja las otras tres partes iguales a la que estas editando.",
            "Copiar", [this] { this->onCopyPartToOthers(); }));
    }

    rows.push_back(kit::makeButtonRow(width, "Probar el icono",
        "Lo ves como se vera en el juego, con tus colores.",
        "Probar", [this] { this->onTry(); }));

    rows.push_back(kit::makeButtonRow(width, "Exportar",
        "Genera los archivos del icono o instalalo en More Icons.",
        "Exportar", [this] { this->onExport(); }));

    rows.push_back(kit::makeButtonRow(width, "Compartir",
        "Empaqueta el icono en un archivo para pasarselo a alguien.",
        "Compartir", [this] {
            auto exported = IconShare::exportProject(m_project.id);
            if (!exported) {
                setStatus("No se pudo compartir: " + exported.unwrapErr(), false);
                return;
            }
            geode::utils::file::openFolder(exported.unwrap().parent_path());
            setStatus("Archivo listo para compartir.");
        }));

    rows.push_back(kit::makeButtonRow(width, "Como funciona",
        "Que es una zona, que es una capa y que hace cada ajuste.",
        "Ayuda", [] { if (auto* p = IconHelpPopup::create()) p->show(); }));

    return rows;
}


std::vector<SlotDef> IconEditorLayer::visibleZones() const {
    std::vector<SlotDef> out;
    auto const* def = anatomyFor(m_project.type);
    if (!def) return out;
    for (auto const& slot : def->slots) {
        // "extra" only exists on robot/spider part 1.
        if (def->partCount > 1 && m_currentPart > 1 && slot.key == "extra") continue;
        out.push_back(slot);
    }
    return out;
}

std::string IconEditorLayer::currentSlotKey() const {
    auto zones = visibleZones();
    if (zones.empty()) return slotStorageKey(m_currentPart, "main");
    int const index = std::clamp(m_zoneIndex, 0, static_cast<int>(zones.size()) - 1);
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
    pushCanvasSelection();
    schedulePreview(false);
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
    pushCanvasSelection();
    setStatus(fmt::format("Editando: {}", zones[static_cast<std::size_t>(zoneIndex)].label));
}

void IconEditorLayer::selectPiece(int index) {
    if (index == m_selectedPiece) return;
    m_selectedPiece = index;
    pushCanvasSelection();
    scheduleInspectorRebuild();
}

void IconEditorLayer::selectTab(Tab tab) {
    if (tab == m_tab) return;
    m_tab = tab;
    m_inspectorScrollY = 0.f;
    // arrives from the bar's own button, so the bar rebuilds next frame,
    // not under the touch dispatcher.
    scheduleInspectorRebuild();
}

void IconEditorLayer::selectFromCanvas(std::string const& zoneKey, int pieceIndex) {
    auto zones = visibleZones();
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (slotStorageKey(m_currentPart, zones[i].key) != zoneKey) continue;
        m_zoneIndex = static_cast<int>(i);
        break;
    }
    m_selectedPiece = pieceIndex;
    m_inspectorScrollY = 0.f;

    if (m_canvas) m_canvas->setActiveZone(currentSlotKey());
    pushCanvasSelection();

    if (auto* piece = selectedPiece()) {
        setStatus(fmt::format("Elegida: {}", piece->name));
    }
    scheduleInspectorRebuild();

    Ref<IconEditorLayer> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown() || !self || !self->getParent()) return;
        self->refreshZoneChips();
    });
}

void IconEditorLayer::pushCanvasSelection() {
    if (!m_canvas) return;
    auto* piece = selectedPiece();
    m_canvas->setSelection(currentSlotKey(), m_selectedPiece, piece && piece->locked);
}

void IconEditorLayer::edit(std::string coalesceKey, std::function<void()> mutate) {
    if (!mutate) return;
    m_history.push(m_project, std::move(coalesceKey));
    mutate();
    m_history.commit(m_project);

    m_dirty = true;
    m_autosaveCountdown = 2.f;
    schedulePreview(m_gestureActive);
    refreshTopBar();
}

void IconEditorLayer::applyRestoredProject() {
    auto zones = visibleZones();
    m_zoneIndex = std::clamp(m_zoneIndex, 0,
        std::max(0, static_cast<int>(zones.size()) - 1));

    int const pieceCount = static_cast<int>(currentSlot().pieces.size());
    if (m_selectedPiece >= pieceCount) m_selectedPiece = pieceCount - 1;

    m_dirty = true;
    m_autosaveCountdown = 2.f;
    refreshZoneChips();
    rebuildInspector();
    pushCanvasSelection();
    schedulePreview(false);
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

void IconEditorLayer::pickColor(ccColor3B color) {
    auto* piece = selectedPiece();
    if (!piece) {
        setStatus("Elige una capa para pintarla.", false);
        return;
    }
    edit({}, [&] {
        if (auto* p = selectedPiece()) {
            p->fill.type = FillType::Flat;
            p->fill.flat = {color.r, color.g, color.b, 255};
            p->fill.chroma = false;
        }
    });
    rememberColor(color);
    scheduleInspectorRebuild();
}

void IconEditorLayer::alignSelected(mkui::AlignMode mode) {
    if (!selectedPiece()) {
        setStatus("Elige una capa primero.", false);
        return;
    }

    auto const* def = anatomyFor(m_project.type);
    auto it = m_slotRenders.find(currentSlotKey());
    if (!def || it == m_slotRenders.end()) return;

    PieceRender const* render = nullptr;
    for (auto const& entry : it->second.pieces) {
        if (entry.index == m_selectedPiece) { render = &entry; break; }
    }
    if (!render || !render->hasBounds) {
        setStatus("Esa capa todavia no dibuja nada.", false);
        return;
    }

    float const canvas = static_cast<float>(def->canvasUhd);
    float const guide = static_cast<float>(def->guideUhd);
    float const guideMin = (canvas - guide) / 2.f;
    float const guideMax = guideMin + guide;
    float const half = canvas / 2.f;

    float dx = 0.f;
    float dy = 0.f;
    switch (mode) {
        case mkui::AlignMode::Left:
            dx = guideMin - static_cast<float>(render->boundsX); break;
        case mkui::AlignMode::Right:
            dx = guideMax - static_cast<float>(render->boundsX + render->boundsW); break;
        case mkui::AlignMode::CenterH:
            dx = half - (static_cast<float>(render->boundsX) + render->boundsW / 2.f); break;
        case mkui::AlignMode::Top:
            dy = guideMin - static_cast<float>(render->boundsY); break;
        case mkui::AlignMode::Bottom:
            dy = guideMax - static_cast<float>(render->boundsY + render->boundsH); break;
        case mkui::AlignMode::CenterV:
            dy = half - (static_cast<float>(render->boundsY) + render->boundsH / 2.f); break;
    }

    edit({}, [&] {
        auto* piece = selectedPiece();
        if (!piece) return;
        piece->transform.offsetX =
            std::clamp(piece->transform.offsetX + dx / half, -1.f, 1.f);
        // render rows run top-down, offset bottom-up.
        piece->transform.offsetY =
            std::clamp(piece->transform.offsetY - dy / half, -1.f, 1.f);
    });
    scheduleInspectorRebuild();
}

void IconEditorLayer::applyFillToZone(FillSpec const& fill, std::string const& storageKey) {
    edit({}, [&] {
        auto it = m_project.slots.find(storageKey);
        if (it == m_project.slots.end()) return;
        for (auto& piece : it->second.pieces) {
            // outline stays per-layer by user choice, not from the copied fill.
            auto outline = piece.fill.outline;
            piece.fill = fill;
            piece.fill.outline = outline;
        }
    });
    refreshZoneChips();
    scheduleInspectorRebuild();
}

void IconEditorLayer::applyTheme(IconTheme const& theme, bool wholeIcon) {
    auto const* def = anatomyFor(m_project.type);
    if (!def) return;

    int touched = 0;
    edit({}, [&] {
        int const firstPart = def->partCount > 1 ? 1 : 0;
        int const lastPart = def->partCount > 1 ? def->partCount : 0;
        for (int part = firstPart; part <= lastPart; ++part) {
            if (!wholeIcon && part != m_currentPart) continue;
            for (auto const& slot : def->slots) {
                if (def->partCount > 1 && part > 1 && slot.key == "extra") continue;
                FillSpec fill;
                if (!themeFillFor(theme, slot.key, fill)) continue;

                auto it = m_project.slots.find(slotStorageKey(part, slot.key));
                if (it == m_project.slots.end()) continue;
                for (auto& piece : it->second.pieces) {
                    auto outline = piece.fill.outline;
                    piece.fill = fill;
                    piece.fill.outline = outline;
                    ++touched;
                }
            }
        }
    });

    refreshZoneChips();
    scheduleInspectorRebuild();
    setStatus(touched > 0
        ? fmt::format("Icono pintado con \"{}\".", theme.name)
        : std::string("Todavia no hay capas que pintar."), touched > 0);
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
        self->pushCanvasSelection();
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
        self->pushCanvasSelection();
        self->scheduleInspectorRebuild();
        self->setStatus("Forma agregada.");
    });
    if (popup) popup->show();
}

void IconEditorLayer::onLoadWholeIcon() {
    Ref<IconEditorLayer> self = this;
    auto* picker = TemplatePickerPopup::create(m_project.type, [self](int iconId) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        auto const* def = anatomyFor(self->m_project.type);
        if (!def) return;

        int loaded = 0;
        self->edit({}, [&] {
            for (auto const& slot : def->slots) {
                if (def->partCount > 1 && self->m_currentPart > 1 && slot.key == "extra") {
                    continue;
                }
                IconPiece piece;
                piece.id = self->m_project.makePieceId();
                piece.name = std::string(slot.label);
                if (auto r = fillPieceFromTemplate(self->m_project, piece, iconId,
                        self->m_currentPart, std::string(slot.key)); !r) {
                    continue;
                }
                auto key = slotStorageKey(self->m_currentPart, slot.key);
                // starting from an official icon resets that zone.
                self->m_project.slots[key].pieces.clear();
                self->m_project.slots[key].pieces.push_back(std::move(piece));
                ++loaded;
            }
            self->m_selectedPiece = 0;
        });

        self->refreshZoneChips();
        self->pushCanvasSelection();
        self->scheduleInspectorRebuild();
        self->setStatus(loaded > 0
            ? fmt::format("Cargado el icono {}.", iconId)
            : std::string("No se pudo sacar ese icono."), loaded > 0);
    });
    if (picker) picker->show();
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
        {"Elegir una forma", "Un icono oficial del juego, o uno de los tuyos.",
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
                 },
                 [self](std::string const& projectId) {
                     if (paimon::isRuntimeShuttingDown() || !self) return;
                     self->adoptShapeFromProject(projectId);
                 });
             if (picker) picker->show();
         }, false},
    };

    if (auto* sheet = IconActionSheet::create("Cambiar el dibujo", std::move(actions))) {
        sheet->show();
    }
}

void IconEditorLayer::adoptShapeFromProject(std::string const& projectId) {
    if (!selectedPiece()) return;

    auto loaded = IconProjectStore::get().loadProject(projectId);
    if (!loaded) {
        setStatus("No se pudo abrir ese icono: " + loaded.unwrapErr(), false);
        return;
    }
    auto const source = loaded.unwrap();

    auto zones = visibleZones();
    if (zones.empty()) return;
    auto const zoneKey = std::string(
        zones[static_cast<std::size_t>(m_zoneIndex)].key);

    auto it = source.slots.find(slotStorageKey(m_currentPart, zoneKey));
    if (it == source.slots.end() || it->second.pieces.empty()) {
        setStatus("Ese icono no tiene nada en esta zona.", false);
        return;
    }

    auto const& sourcePiece = it->second.pieces.back();
    if (sourcePiece.shape.file.empty()) {
        setStatus("Esa capa no tiene dibujo que copiar.", false);
        return;
    }

    // PNG is copied into the project so it reopens even if the source icon is deleted.
    auto const name = IconPaths::sanitizeFilename(
        fmt::format("prestada_{}_{}", selectedPiece()->id, sourcePiece.shape.file));
    std::error_code ec;
    std::filesystem::create_directories(IconPaths::imagesDir(m_project.id), ec);
    std::filesystem::copy_file(
        IconPaths::imageFile(projectId, sourcePiece.shape.file),
        IconPaths::imageFile(m_project.id, name),
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        setStatus("No se pudo copiar el dibujo: " + ec.message(), false);
        return;
    }

    auto shape = sourcePiece.shape;
    shape.file = name;
    edit({}, [&] {
        if (auto* p = selectedPiece()) p->shape = shape;
    });
    scheduleInspectorRebuild();
    setStatus(fmt::format("Forma copiada de \"{}\".", source.name));
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
            self->pushCanvasSelection();
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
             self->pushCanvasSelection();
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

void IconEditorLayer::onSaveStyle() {
    auto* piece = selectedPiece();
    if (!piece) {
        setStatus("Elige una capa para guardar su pintura.", false);
        return;
    }

    auto fill = piece->fill;
    Ref<IconEditorLayer> self = this;
    auto* popup = IconNamePopup::create("Nombre de la pintura", "Mi pintura",
        fillTypeName(fill), [self, fill](std::string const& name) {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            if (auto r = StyleStore::get().add(name, fill); !r) {
                self->setStatus(r.unwrapErr(), false);
                return;
            }
            self->setStatus("Pintura guardada.");
        });
    if (popup) popup->show();
}

void IconEditorLayer::onApplyStyle() {
    auto const& styles = StyleStore::get().list();
    if (styles.empty()) {
        setStatus("Todavia no has guardado ninguna pintura.", false);
        return;
    }
    if (!selectedPiece()) {
        setStatus("Elige una capa primero.", false);
        return;
    }

    Ref<IconEditorLayer> self = this;
    std::vector<IconActionSheet::Action> actions;
    for (auto const& style : styles) {
        auto fill = style.fill;
        actions.push_back({style.name, fillTypeName(fill), [self, fill] {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            self->edit({}, [&] {
                if (auto* p = self->selectedPiece()) {
                    auto outline = p->fill.outline;
                    p->fill = fill;
                    p->fill.outline = outline;
                }
            });
            self->scheduleInspectorRebuild();
            self->setStatus("Pintura aplicada.");
        }, false});
    }

    actions.push_back({"Borrar una guardada", "", [self] {
        if (!self) return;
        std::vector<IconActionSheet::Action> removals;
        for (auto const& style : StyleStore::get().list()) {
            auto id = style.id;
            removals.push_back({style.name, "", [self, id] {
                if (paimon::isRuntimeShuttingDown() || !self) return;
                if (auto r = StyleStore::get().remove(id); !r) {
                    self->setStatus(r.unwrapErr(), false);
                    return;
                }
                self->setStatus("Pintura borrada.");
            }, true});
        }
        if (auto* sheet = IconActionSheet::create(
                "Borrar pintura guardada", std::move(removals))) {
            sheet->show();
        }
    }, true});

    if (auto* sheet = IconActionSheet::create(
            "Pinturas guardadas", std::move(actions))) {
        sheet->show();
    }
}

void IconEditorLayer::onProjectMenu() {
    Ref<IconEditorLayer> self = this;
    std::vector<IconActionSheet::Action> actions{
        {"Guardar ahora", "El icono se guarda solo, esto es por si acaso.",
         [self] { if (self) self->saveProject(true); }, false},
        {"Renombrar icono", "", [self] { if (self) self->onRename(); }, false},
        {"Ajustar la vista", "Devuelve el zoom y el encuadre a como estaban.",
         [self] {
             if (!self || !self->m_canvas) return;
             self->m_canvas->resetView();
             self->setStatus("Vista ajustada.");
         }, false},
        {"Exportar o instalar", "Archivos del icono y copia a More Icons.",
         [self] { if (self) self->onExport(); }, false},
        {"Como funciona", "Guia rapida del creador.",
         [] { if (auto* p = IconHelpPopup::create()) p->show(); }, false},
    };
    if (auto* sheet = IconActionSheet::create("Opciones", std::move(actions))) {
        sheet->show();
    }
}

void IconEditorLayer::maybeShowTour() {
    if (Mod::get()->getSavedValue<bool>("icon-maker.editor-tour-seen")) return;
    Mod::get()->setSavedValue("icon-maker.editor-tour-seen", true);

    auto const win = CCDirector::get()->getWinSize();
    auto const* def = anatomyFor(m_project.type);
    auto layout = layoutFor(win, def && def->partCount > 1,
        static_cast<int>(visibleZones().size()), m_tab != Tab::Layers);

    auto* host = CCNode::create();
    host->setContentSize(win);
    addChild(host, 40);

    auto* dim = CCLayerColor::create(ccc4(0, 0, 0, 148));
    dim->setContentSize(win);
    host->addChild(dim, -1);

    auto* outlines = PaimonDrawNode::create();
    host->addChild(outlines, 1);

    // one box + line per site, from the same layout the editor draws, so window
    // resizes can't desync them.
    auto highlight = [&](CCRect const& rect, char const* text, bool labelBelow) {
        ccColor4F const accent{0.42f, 0.80f, 1.f, 1.f};
        CCPoint const bl{rect.origin.x, rect.origin.y};
        CCPoint const br{rect.origin.x + rect.size.width, rect.origin.y};
        CCPoint const tr{br.x, rect.origin.y + rect.size.height};
        CCPoint const tl{bl.x, tr.y};
        outlines->drawSegment(bl, br, 0.8f, accent);
        outlines->drawSegment(br, tr, 0.8f, accent);
        outlines->drawSegment(tr, tl, 0.8f, accent);
        outlines->drawSegment(tl, bl, 0.8f, accent);

        auto* label = CCLabelBMFont::create(text, "chatFont.fnt",
            rect.size.width / 0.42f, kCCTextAlignmentCenter);
        label->setScale(0.42f);
        label->setAnchorPoint({0.5f, labelBelow ? 1.f : 0.f});
        label->setPosition({rect.getMidX(),
            labelBelow ? rect.origin.y - 5.f : tr.y + 5.f});
        host->addChild(label, 2);
    };

    highlight({layout.workspaceCX - layout.canvasSide / 2.f,
               layout.canvasCY - layout.canvasSide / 2.f,
               layout.canvasSide, layout.canvasSide},
              "Toca una capa para elegirla. Arrastra para moverla, las esquinas "
              "la estiran y el tirador de arriba la gira.", true);

    highlight({layout.scrollX, layout.chipsY, layout.scrollW,
               mkui::zoneChipsHeight(layout.scrollW,
                   static_cast<int>(visibleZones().size()))},
              "Las zonas del icono.", true);

    highlight({layout.scrollX, layout.tabsY, layout.scrollW, kit::kTabBarHeight},
              "Capas, pintura, forma y ajustes del icono.", true);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    host->addChild(menu, 3);

    Ref<CCNode> hostRef = host;
    auto* swallow = CCNode::create();
    swallow->setAnchorPoint({0.5f, 0.5f});
    swallow->setContentSize(win);
    if (auto* btn = CCMenuItemExt::createSpriteExtra(swallow,
            [hostRef](CCMenuItemSpriteExtra*) {
                if (hostRef) hostRef->removeFromParent();
            })) {
        btn->setPosition({win.width / 2.f, win.height / 2.f});
        menu->addChild(btn);
    }

    if (auto* spr = ButtonSprite::create("Entendido", "goldFont.fnt",
                                         "GJ_button_01.png", 0.8f)) {
        spr->setScale(0.7f);
        if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                [hostRef](CCMenuItemSpriteExtra*) {
                    if (hostRef) hostRef->removeFromParent();
                })) {
            btn->setPosition({win.width / 2.f, 26.f});
            menu->addChild(btn);
        }
    }
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


void IconEditorLayer::schedulePreview(bool fast) {
    if (!fast) {
        m_previewFast = false;
    } else if (m_previewCountdown < 0.f) {
        m_previewFast = true;
    }
    m_previewCountdown = fast ? 0.05f : 0.24f;
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
        m_previewCountdown = 0.1f;
        return;
    }
    kickPreviewJob();
}

void IconEditorLayer::scrollWheel(float x, float y) {
    // wheel zooms over the canvas, scrolls the panel outside it.
    if (m_canvas) {
        auto const local = m_canvas->viewportFromScreen(geode::cocos::getMousePos());
        auto const size = m_canvas->getContentSize();
        if (local.x >= 0.f && local.y >= 0.f &&
            local.x <= size.width && local.y <= size.height) {
            m_canvas->zoomAt(y < 0.f ? 1.12f : 1.f / 1.12f, local);
            return;
        }
    }
    if (kit::queueWheelScroll(m_inspector, x, y, m_wheelTargetY, m_wheelTargetSet)) {
        return;
    }
    CCLayer::scrollWheel(x, y);
}

std::vector<std::string> IconEditorLayer::drawOrderKeys() const {
    std::vector<std::string> keys;
    auto const* def = anatomyFor(m_project.type);
    if (!def) return keys;

    // back-to-front, so glow lands behind white.
    for (char const* key : {"glow", "tertiary", "secondary", "main", "extra"}) {
        for (auto const& slot : def->slots) {
            if (slot.key != key) continue;
            if (def->partCount > 1 && m_currentPart > 1 && slot.key == "extra") continue;
            keys.push_back(slotStorageKey(m_currentPart, key));
        }
    }
    return keys;
}

void IconEditorLayer::kickPreviewJob() {
    auto const* def = anatomyFor(m_project.type);
    if (!def || !m_canvas) return;

    m_compileBusy = true;
    int const generation = m_generation->fetch_add(1) + 1;

    IconProject snapshot = m_project;
    int const canvasSize = def->canvasUhd;
    auto imagesDir = IconPaths::imagesDir(snapshot.id);
    std::string const activeKey = currentSlotKey();

    std::vector<std::string> keys;
    if (m_previewFast) {
        // mid-drag only the active zone changes; repainting all five at 15 Hz shows.
        keys.push_back(activeKey);
    } else {
        keys = drawOrderKeys();
    }
    m_previewFast = false;

    Ref<IconEditorLayer> self = this;
    auto generationBox = m_generation;

    paimon::ThreadTracker::get().spawn(
        [self, generationBox, generation, snapshot, keys, canvasSize, imagesDir, activeKey]() {
            geode::utils::thread::setName("icon-maker-preview");

            std::vector<std::pair<std::string, SlotRender>> rendered;
            rendered.reserve(keys.size());
            for (auto const& key : keys) {
                rendered.emplace_back(key, PieceRenderer::renderSlotDetailed(
                    snapshot, key, canvasSize, kHitMaskSize, imagesDir));
            }

            Loader::get()->queueInMainThread(
                [self, generationBox, generation, activeKey,
                 rendered = std::move(rendered)]() mutable {
                    if (paimon::isRuntimeShuttingDown() || !self) return;
                    self->m_compileBusy = false;
                    if (generationBox->load() != generation) return;
                    self->applyPreview(std::move(rendered), activeKey);
                });
        });
}

void IconEditorLayer::applyPreview(std::vector<std::pair<std::string, SlotRender>> rendered,
                                   std::string const& activeKey) {
    if (!m_canvas) return;

    for (auto& [key, slot] : rendered) {
        if (auto* texture = ts::SpritePreviewRenderer::createTexture(slot.composite)) {
            m_zoneTextures[key] = texture;
        } else {
            m_zoneTextures.erase(key);
        }

        for (auto& piece : slot.pieces) {
            auto thumb = piece.pixels.resizedBilinear(kThumbSize, kThumbSize);
            if (auto* texture = ts::SpritePreviewRenderer::createTexture(thumb)) {
                m_pieceThumbs[piece.pieceId] = texture;
            }
            // full-size pixels only waste memory past the thumbnail; the mask is what's used.
            piece.pixels = ts::ImageBuffer{};
        }
        m_slotRenders[key] = std::move(slot);
    }

    std::vector<EditorCanvas::Zone> zones;
    for (auto const& key : drawOrderKeys()) {
        EditorCanvas::Zone zone;
        zone.key = key;
        if (auto it = m_slotRenders.find(key); it != m_slotRenders.end()) {
            // canvas needs pixels for the eyedropper only; copying them every drag repaint is dear.
            if (m_eyedropper) zone.composite = it->second.composite;
            zone.pieces = it->second.pieces;
        }
        if (auto it = m_zoneTextures.find(key); it != m_zoneTextures.end()) {
            zone.texture = it->second;
        }
        zones.push_back(std::move(zone));
    }

    m_canvas->setZones(std::move(zones), activeKey);
    m_canvas->setIsolate(m_isolateZone);
    m_canvas->setEyedropper(m_eyedropper);
    pushCanvasSelection();

    // mid-drag chips and list stay frozen; rebuilt on release in onGestureEnd.
    if (m_gestureActive) return;
    refreshZoneChips();
    if (m_tab == Tab::Layers) scheduleInspectorRebuild();
    else refreshSelectionStrip();
}


void IconEditorLayer::onTry() {
    if (m_dirty) saveProject(false);
    if (auto* popup = IconTryPopup::create(m_project)) popup->show();
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

}  // namespace paimon::icon_maker
