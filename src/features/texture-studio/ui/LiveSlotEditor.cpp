#include "../../../utils/FilesystemPath.hpp"
#include "LiveSlotEditor.hpp"

#include "ParamSliderRow.hpp"
#include "../engine/ColorPresets.hpp"
#include "../engine/LiveTones.hpp"
#include "../persist/SlotStore.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/ProjectShare.hpp"
#include "../persist/TextureProjectSerialize.hpp"
#include "../services/LiveSlotRuntime.hpp"
#include "../../colorful-icons/services/IconColorMath.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/FileDialog.hpp"

#include <Geode/ui/PopupManager.hpp>

#include <algorithm>
#include <cmath>
#include <random>

using namespace geode::prelude;

namespace paimon::texture_studio {
namespace {

namespace pui = paimon::ui;

constexpr float kMargin = 10.f;
constexpr float kGap = 8.f;
constexpr float kHeaderH = 40.f;
constexpr float kFooterH = 38.f;
constexpr float kTabStrip = 32.f;
constexpr ccColor3B kText{235, 238, 245};
constexpr ccColor3B kMuted{160, 172, 190};

CCLabelBMFont* label(CCNode* parent, std::string const& text, CCPoint pos, float scale,
                     ccColor3B color = kText, CCPoint anchor = {.5f, .5f},
                     float maxWidth = 0.f, char const* font = "bigFont.fnt") {
    auto* result = CCLabelBMFont::create(text.c_str(), font);
    result->setScale(scale);
    result->setAnchorPoint(anchor);
    result->setPosition(pos);
    result->setColor(color);
    if (maxWidth > 0.f) result->limitLabelWidth(maxWidth, scale, scale * .4f);
    parent->addChild(result, 3);
    return result;
}

CCMenuItemSpriteExtra* button(CCMenu* menu, char const* text, CCPoint pos, std::function<void()> callback,
                              pui::Btn skin, float width, float scale = .5f,
                              char const* font = "bigFont.fnt") {
    auto* item = pui::makeButton(text, std::move(callback), skin, width, scale, font);
    item->setPosition(pos);
    menu->addChild(item);
    return item;
}

std::string toHex(ccColor3B const& c) {
    return fmt::format("#{:02X}{:02X}{:02X}", c.r, c.g, c.b);
}

bool sameColor(ccColor3B a, ccColor3B b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

struct PaletteSlot {
    char const* name;
    char const* role;
    ccColor3B TextureProject::* color;
    bool TextureProject::* autoFlag;
    LiveTone tone;
};

constexpr PaletteSlot kSlots[] = {
    {"Primary", "Green UI", &TextureProject::color1, nullptr, LiveTone::Primary},
    {"Secondary", "Cyan UI", &TextureProject::color2, nullptr, LiveTone::Secondary},
    {"Surface", "Panels, lists", &TextureProject::colorSurface, &TextureProject::autoSurface, LiveTone::Surface},
    {"Background", "Scenes", &TextureProject::colorBackground, &TextureProject::autoBackground, LiveTone::Background},
    {"Glow", "Highlights", &TextureProject::colorGlow, nullptr, LiveTone::Count},
    {"Detail", "Glyphs", &TextureProject::colorDetail, nullptr, LiveTone::Count},
};

}  // namespace

void LiveSlotEditor::open(std::string const& id) {
    auto* editor = new LiveSlotEditor;
    if (editor->init(id)) {
        editor->autorelease();
        geode::pushSceneWithLayer(editor);
    } else delete editor;
}

bool LiveSlotEditor::init(std::string const& id) {
    if (!CCLayer::init()) return false;
    auto loaded = SlotStore::get().loadSlot(id);
    if (!loaded) {
        Notification::create(loaded.unwrapErr(), NotificationIcon::Error)->show();
        return false;
    }
    m_project = loaded.unwrap();
    LiveSlotRuntime::normalize(m_project);
    setID("packgen-live-editor"_spr);
    setKeypadEnabled(true);

    static constexpr std::uint8_t kWhite[16] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    auto* solid = new CCTexture2D();
    if (solid->initWithData(kWhite, kCCTexture2DPixelFormat_RGBA8888, 2, 2, CCSize(2.f, 2.f))) {
        solid->autorelease();
        m_solid = solid;
    } else {
        solid->release();
    }

    auto win = CCDirector::get()->getWinSize();
    // the gradient is a live background: it previews the Background tone.
    pui::decorateScene(this, {0, 102, 255}, false);
    auto* dim = CCLayerColor::create({0, 0, 0, 70});
    dim->setContentSize(win);
    addChild(dim, -5);

    float bodyBottom = kFooterH;
    float bodyTop = win.height - kHeaderH;
    float bodyH = bodyTop - bodyBottom;
    float avail = win.width - kMargin * 2.f - kGap;
    float leftW = std::floor(avail * .53f);
    m_left = CCRect(kMargin, bodyBottom, leftW, bodyH);
    m_right = CCRect(kMargin + leftW + kGap, bodyBottom, avail - leftW, bodyH);

    for (auto const& rect : {m_left, m_right}) {
        auto* panel = pui::makeInset(rect.size, 120);
        panel->setPosition(rect.origin);
        addChild(panel, 0);
    }

    buildHeader();

    CCSize view{m_left.size.width - 18.f, m_left.size.height - kTabStrip - 8.f};
    m_controlsScroll = ScrollLayer::create(view);
    if (m_controlsScroll) m_controlsScroll->setAnchorPoint({0.f, 0.f});
    m_controlsScroll->setPosition({m_left.getMinX() + 6.f, m_left.getMinY() + 6.f});
    m_controlsScroll->setID("live-texture-controls"_spr);
    addChild(m_controlsScroll, 2);
    m_controlsBar = Scrollbar::create(m_controlsScroll);
    if (m_controlsBar) {
        m_controlsBar->setContentSize({6.f, view.height});
        m_controlsBar->setPosition({m_left.getMaxX() - 7.f, m_left.getMinY() + 6.f + view.height * .5f});
        addChild(m_controlsBar, 3);
    }
    m_controls = m_controlsScroll->m_contentLayer;

    m_tabMenu = pui::makeZeroMenu();
    addChild(m_tabMenu, 4);

    buildTabs();
    buildPage(false);
    buildBoard();
    buildFooter();
    applyPreview();
    refreshStatus(0.f);

    schedule(schedule_selector(LiveSlotEditor::refreshStatus), .2f);
    return true;
}

CCSprite* LiveSlotEditor::makeSwatch(ccColor3B color, CCSize size) {
    auto* sprite = m_solid ? CCSprite::createWithTexture(m_solid) : CCSprite::create();
    sprite->setTextureRect({0.f, 0.f, 2.f, 2.f});
    sprite->setScaleX(size.width / 2.f);
    sprite->setScaleY(size.height / 2.f);
    sprite->setColor(color);
    return sprite;
}

void LiveSlotEditor::buildHeader() {
    auto win = CCDirector::get()->getWinSize();
    float y = win.height - kHeaderH * .5f - 1.f;
    auto* menu = pui::makeZeroMenu();
    addChild(menu, 5);

    auto* backBtn = pui::makeBackButton([this] { back(); });
    backBtn->setScale(.8f);
    backBtn->setPosition({24.f, y});
    menu->addChild(backBtn);

    auto* title = pui::makeTitle("Texture Live", 150.f, .7f);
    title->setAnchorPoint({0.f, .5f});
    title->setPosition({44.f, y + 1.f});
    addChild(title, 5);

    float inputW = std::min(200.f, win.width * .34f);
    m_name = TextInput::create(inputW, "Slot name");
    m_name->setString(m_project.name);
    m_name->setMaxCharCount(40);
    m_name->setScale(.72f);
    m_name->setPosition({win.width * .5f + 20.f, y});
    m_name->setCallback([this](std::string const& name) {
        m_project.name = name;
        m_dirty = true;
    });
    addChild(m_name, 5);

    button(menu, "Import", {win.width - 104.f, y}, [this] { importConfig(); }, pui::Btn::Gray, 58.f, .5f);
    button(menu, "Export", {win.width - 40.f, y}, [this] { exportConfig(); }, pui::Btn::Gray, 58.f, .5f);
}

void LiveSlotEditor::buildFooter() {
    auto win = CCDirector::get()->getWinSize();
    float y = kFooterH * .5f;
    auto* menu = pui::makeZeroMenu();
    addChild(menu, 5);

    button(menu, "Save & Apply", {win.width - 76.f, y}, [this] { save(); }, pui::Btn::Green, 124.f, .6f,
        "goldFont.fnt");
    button(menu, "Reset", {win.width - 182.f, y}, [this] { resetAll(); }, pui::Btn::Red, 70.f, .55f);

    m_progressWidth = std::max(80.f, win.width - 250.f - 14.f);
    m_status = label(this, "", {14.f, y + 5.f}, .3f, kText, {0.f, .5f}, m_progressWidth);
    m_progressTrack = makeSwatch({0, 0, 0}, {m_progressWidth, 3.f});
    m_progressTrack->setAnchorPoint({0.f, .5f});
    m_progressTrack->setOpacity(120);
    m_progressTrack->setPosition({14.f, y - 7.f});
    addChild(m_progressTrack, 4);
    m_progressFill = makeSwatch({110, 220, 255}, {m_progressWidth, 3.f});
    m_progressFill->setAnchorPoint({0.f, .5f});
    m_progressFill->setPosition({14.f, y - 7.f});
    addChild(m_progressFill, 5);
}

void LiveSlotEditor::buildTabs() {
    m_tabMenu->removeAllChildren();
    char const* names[] = {"Colors", "Presets", "Adjust", "Scope"};
    constexpr int count = static_cast<int>(Page::Count);
    float inner = m_left.size.width - 12.f;
    float tabW = inner / count - 4.f;
    float y = m_left.getMaxY() - kTabStrip * .5f - 2.f;
    for (int i = 0; i < count; ++i) {
        bool active = static_cast<int>(m_page) == i;
        float x = m_left.getMinX() + 6.f + inner * (i + .5f) / count;
        button(m_tabMenu, names[i], {x, y}, [this, i] {
            if (static_cast<int>(m_page) == i) return;
            m_page = static_cast<Page>(i);
            deferRebuild(true, false, true);
        }, active ? pui::Btn::Green : pui::Btn::Gray, tabW, .5f);
    }
}

void LiveSlotEditor::buildPage(bool keepScroll) {
    float const previousY = m_controls->getPositionY();
    float const previousH = m_controls->getContentSize().height;
    m_controls->removeAllChildren();
    auto view = m_controlsScroll->getContentSize();

    // GenericContentLayer hides children whose bounding box is outside the
    // view, so `page` spans the whole content and builders draw into `body`.
    auto* page = CCNode::create();
    auto* body = CCNode::create();
    page->addChild(body);
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    body->addChild(menu, 4);

    float w = view.width;
    float used = 0.f;
    switch (m_page) {
        case Page::Colors:  used = buildColorsPage(menu, w); break;
        case Page::Presets: used = buildPresetsPage(menu, w); break;
        case Page::Adjust:  used = buildAdjustPage(menu, w); break;
        default:            used = buildScopePage(menu, w); break;
    }
    // builders lay out downward from the top edge (y <= 0).
    float contentHeight = std::max(view.height, used + 6.f);
    m_controls->setContentSize({w, contentHeight});
    page->setContentSize({w, contentHeight});
    page->setPosition({0.f, 0.f});
    body->setPosition({0.f, contentHeight});
    m_controls->addChild(page);
    bool keepScrollPosition = keepScroll && previousH == contentHeight;
    if (keepScrollPosition) m_controls->setPositionY(previousY);
    else m_controlsScroll->scrollToTop();
    if (m_controlsBar) m_controlsBar->setVisible(contentHeight > view.height + 1.f);
}

float LiveSlotEditor::buildColorsPage(CCMenu* menu, float w) {
    auto* page = menu->getParent();
    auto tones = LiveSlotRuntime::tonesFor(m_project);
    float y = -4.f;

    // 2 x 3 palette grid: swatch, name, hex (or Auto for derived colors).
    float cellW = w * .5f;
    float rowH = 32.f;
    for (int i = 0; i < 6; ++i) {
        auto const& slot = kSlots[i];
        float x = cellW * static_cast<float>(i % 2);
        float cy = y - rowH * (static_cast<float>(i / 2) + .5f);
        bool isAuto = slot.autoFlag && m_project.*(slot.autoFlag);
        auto shown = isAuto ? tones[static_cast<std::size_t>(slot.tone)] : m_project.*(slot.color);

        auto* face = CCNode::create();
        face->setContentSize({28.f, 28.f});
        auto* border = makeSwatch({0, 0, 0}, {28.f, 28.f});
        border->setPosition({14.f, 14.f});
        border->setOpacity(200);
        face->addChild(border);
        auto* fill = makeSwatch(shown, {24.f, 24.f});
        fill->setPosition({14.f, 14.f});
        face->addChild(fill);
        auto member = slot.color;
        auto autoFlag = slot.autoFlag;
        auto* item = CCMenuItemExt::createSpriteExtra(face, [this, member, autoFlag](CCMenuItemSpriteExtra*) {
            openPicker(member, autoFlag);
        });
        item->setPosition({x + 18.f, cy});
        menu->addChild(item);

        float textW = cellW - 40.f - (slot.autoFlag ? 34.f : 4.f);
        label(page, slot.name, {x + 36.f, cy + 6.f}, .32f, kText, {0.f, .5f}, textW);
        label(page, isAuto ? std::string("Auto - ") + slot.role : toHex(shown) + " - " + slot.role,
            {x + 36.f, cy - 7.f}, .22f, isAuto ? ccColor3B{150, 230, 150} : kMuted, {0.f, .5f}, textW);

        if (slot.autoFlag) {
            button(menu, "Auto", {x + cellW - 20.f, cy}, [this, autoFlag] {
                m_project.*autoFlag = !(m_project.*autoFlag);
                edited();
                deferRebuild(true, true);
            }, isAuto ? pui::Btn::Green : pui::Btn::Gray, 34.f, .38f);
        }
    }
    y -= rowH * 3.f + 6.f;

    float toolW = (w - 16.f) / 3.f;
    button(menu, "Swap", {w / 6.f, y - 12.f}, [this] { swapColors(); }, pui::Btn::Gray, toolW, .48f);
    button(menu, "Harmonize", {w * .5f, y - 12.f}, [this] { makeComplementary(); }, pui::Btn::Gray, toolW, .48f);
    button(menu, "Random", {w * 5.f / 6.f, y - 12.f}, [this] { randomizePalette(); }, pui::Btn::Gray, toolW, .48f);
    y -= 30.f;

    // legend: which shade each vanilla color family receives.
    label(page, "GD color -> painted tone", {2.f, y - 6.f}, .26f, kMuted, {0.f, .5f}, w - 4.f);
    y -= 14.f;
    constexpr LiveTone order[] = {
        LiveTone::Primary, LiveTone::Secondary, LiveTone::Blue, LiveTone::Pink,
        LiveTone::Red, LiveTone::Neutral, LiveTone::Surface, LiveTone::Background,
    };
    float legendW = w / 4.f;
    float legendH = 22.f;
    for (int i = 0; i < 8; ++i) {
        auto tone = order[i];
        float cx = legendW * static_cast<float>(i % 4);
        float cy = y - legendH * (static_cast<float>(i / 4) + .5f);
        auto* chip = makeSwatch(tones[static_cast<std::size_t>(tone)], {14.f, 14.f});
        chip->setPosition({cx + 10.f, cy});
        page->addChild(chip, 3);
        label(page, liveToneSource(tone), {cx + 20.f, cy + 4.f}, .22f, kText, {0.f, .5f}, legendW - 22.f);
        label(page, liveToneName(tone), {cx + 20.f, cy - 5.f}, .18f, kMuted, {0.f, .5f}, legendW - 22.f);
    }
    y -= legendH * 2.f;
    return -y;
}

float LiveSlotEditor::buildPresetsPage(CCMenu* menu, float w) {
    auto const& presets = ColorPresets::list();
    float cardW = w * .5f - 6.f;
    float cardH = 32.f;
    float y = -4.f;
    for (std::size_t i = 0; i < presets.size(); ++i) {
        auto const& preset = presets[i];
        bool active = sameColor(preset.color1, m_project.color1) && sameColor(preset.color2, m_project.color2);

        auto* card = CCNode::create();
        card->setContentSize({cardW, cardH});
        auto* back = pui::makeInset({cardW, cardH}, active ? 200 : 110);
        card->addChild(back);
        float chipX = 10.f;
        for (auto color : {preset.color1, preset.color2, preset.colorGlow}) {
            auto* chip = makeSwatch(color, {12.f, 20.f});
            chip->setPosition({chipX, cardH * .5f});
            card->addChild(chip);
            chipX += 13.f;
        }
        auto* name = CCLabelBMFont::create(preset.name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, .5f});
        name->setPosition({chipX + 2.f, cardH * .5f});
        name->limitLabelWidth(cardW - chipX - 8.f, .32f, .14f);
        name->setColor(active ? ccColor3B{255, 210, 90} : kText);
        card->addChild(name);

        auto* item = CCMenuItemExt::createSpriteExtra(card, [this, preset](CCMenuItemSpriteExtra*) {
            m_project.color1 = preset.color1;
            m_project.color2 = preset.color2;
            m_project.colorGlow = preset.colorGlow;
            m_project.brightness = preset.brightness;
            edited();
            deferRebuild(true, true);
        });
        int col = static_cast<int>(i % 2);
        int row = static_cast<int>(i / 2);
        item->setPosition({(w * .5f) * (col + .5f), y - (cardH + 4.f) * row - cardH * .5f});
        menu->addChild(item);
    }
    int rows = static_cast<int>((presets.size() + 1) / 2);
    return 4.f + rows * (cardH + 4.f);
}

float LiveSlotEditor::buildAdjustPage(CCMenu* menu, float w) {
    auto* page = menu->getParent();
    float y = -2.f;
    auto slider = [&](char const* text, float min, float max, float step, float initial,
                      std::function<void(float)> change, bool regions = false) {
        auto* row = ParamSliderRow::create(text, min, max, step, initial, w - 4.f,
            [this, change, regions](float value) { change(value); edited(regions); });
        row->setPosition({2.f, y - 13.f});
        page->addChild(row, 3);
        y -= 26.f;
    };
    slider("Intensity", 0.f, 1.f, .01f, m_project.tintStrength,
        [this](float v) { m_project.tintStrength = v; });
    slider("Brightness", 20.f, 500.f, 1.f, static_cast<float>(m_project.brightness),
        [this](float v) { m_project.brightness = static_cast<int>(std::lround(v)); });
    slider("Tone spread", 0.f, 1.f, .01f, m_project.toneSpread,
        [this](float v) { m_project.toneSpread = v; });
    slider("Saturation", 0.f, 3.f, .01f, m_project.saturation,
        [this](float v) { m_project.saturation = v; });
    slider("Contrast", -1.f, 1.f, .01f, m_project.contrast,
        [this](float v) { m_project.contrast = v; });
    slider("Glow", 0.f, 1.f, .01f, m_project.glowStrength,
        [this](float v) { m_project.glowStrength = v; });

    button(menu, m_project.alternativeGlowOverlay ? "Glow: Replace" : "Glow: Blend", {w * .5f, y - 12.f}, [this] {
        m_project.alternativeGlowOverlay = !m_project.alternativeGlowOverlay;
        edited();
        deferRebuild(true, true);
    }, pui::Btn::Gray, 110.f, .48f);
    y -= 26.f;
    label(page, "Tone spread separates blue, pink, red and gray buttons", {w * .5f, y - 4.f}, .2f, kMuted,
        {.5f, .5f}, w - 8.f);
    y -= 10.f;
    return -y;
}

float LiveSlotEditor::buildScopePage(CCMenu* menu, float w) {
    auto* page = menu->getParent();
    float y = -4.f;
    auto toggle = [&](char const* title, char const* hint, FlagMember flag) {
        bool value = m_project.*flag;
        auto* item = pui::makeToggle(value, [this, flag](bool on) {
            m_project.*flag = on;
            edited();
        }, .55f);
        item->setPosition({14.f, y - 13.f});
        menu->addChild(item);
        label(page, title, {30.f, y - 8.f}, .32f, kText, {0.f, .5f}, w - 36.f);
        label(page, hint, {30.f, y - 19.f}, .2f, kMuted, {0.f, .5f}, w - 36.f);
        y -= 28.f;
    };
    toggle("Scene backgrounds", "Menu gradients and the main menu scene", &TextureProject::livePaintBackgrounds);
    toggle("Lists & cells", "Brown list fills and alternating rows", &TextureProject::livePaintLists);

    auto slider = [&](char const* text, float min, float max, float step, float initial,
                      std::function<void(float)> change, bool regions) {
        auto* row = ParamSliderRow::create(text, min, max, step, initial, w - 4.f,
            [this, change, regions](float value) { change(value); edited(regions); });
        row->setPosition({2.f, y - 13.f});
        page->addChild(row, 3);
        y -= 26.f;
    };
    slider("Dark protect", 0.f, 255.f, 1.f, static_cast<float>(m_project.outlineProtect),
        [this](float v) { m_project.outlineProtect = static_cast<int>(std::lround(v)); }, false);
    slider("Softness", 0.f, 1.f, .01f, m_project.maskSoftness,
        [this](float v) { m_project.maskSoftness = v; }, true);
    slider("Precision", 2.f, 10.f, 1.f, static_cast<float>(m_project.clusterPrecision),
        [this](float v) { m_project.clusterPrecision = static_cast<int>(std::lround(v)); }, true);
    slider("Edge cleanup", 0.f, 4.f, 1.f, static_cast<float>(m_project.edgeCleanup),
        [this](float v) { m_project.edgeCleanup = static_cast<int>(std::lround(v)); }, true);

    button(menu, "Reanalyze", {50.f, y - 13.f}, [this] {
        unschedule(schedule_selector(LiveSlotEditor::applyPreview));
        if (auto result = LiveSlotRuntime::get().reanalyze(m_project); !result) {
            Notification::create(result.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
        if (m_compare) LiveSlotRuntime::get().showOriginal();
        refreshStatus(0.f);
        deferRebuild(true, true);
    }, pui::Btn::Gray, 90.f, .48f);
    auto cacheMiB = static_cast<double>(LiveSlotRuntime::get().diskCacheBytes()) / (1024.0 * 1024.0);
    label(page, fmt::format("Disk cache {:.1f} MiB", cacheMiB), {102.f, y - 9.f}, .24f, kText, {0.f, .5f},
        w - 106.f);
    label(page, "Reanalyze clears it", {102.f, y - 19.f}, .18f, kMuted, {0.f, .5f}, w - 106.f);
    y -= 26.f;
    return -y;
}

void LiveSlotEditor::buildBoard() {
    m_board = CCNode::create();
    m_board->setPosition(m_right.origin);
    m_board->setContentSize(m_right.size);
    addChild(m_board, 2);

    float w = m_right.size.width;
    float h = m_right.size.height;
    auto* title = pui::makeTitle("Live Preview", w * .36f, .5f);
    title->setAnchorPoint({0.f, .5f});
    title->setPosition({10.f, h - 15.f});
    m_board->addChild(title, 3);

    m_boardTabMenu = pui::makeZeroMenu();
    m_board->addChild(m_boardTabMenu, 4);
    buildBoardTabs();

    m_boardContent = CCNode::create();
    m_boardContent->setPosition({6.f, 30.f});
    m_boardContent->setContentSize({w - 12.f, h - 62.f});
    m_board->addChild(m_boardContent, 2);
    buildBoardContent();

    auto* menu = pui::makeZeroMenu();
    m_board->addChild(menu, 4);
    auto* compare = button(menu, "Show Original", {w - 62.f, 15.f}, [this] {
        m_compare = !m_compare;
        if (m_compare) LiveSlotRuntime::get().showOriginal();
        else applyPreview();
        refreshStatus(0.f);
    }, pui::Btn::Gray, 110.f, .48f);
    if (auto* sprite = typeinfo_cast<ButtonSprite*>(compare->getNormalImage())) m_compareLabel = sprite->m_label;
    label(m_board, "Gameplay and reward art keep their colors", {8.f, 15.f}, .2f, kMuted, {0.f, .5f},
        w - 130.f);
}

void LiveSlotEditor::buildBoardTabs() {
    m_boardTabMenu->removeAllChildren();
    char const* names[] = {"Buttons", "Panels", "Controls"};
    float w = m_right.size.width;
    float h = m_right.size.height;
    float tabW = std::min(52.f, (w * .58f) / 3.f - 3.f);
    for (int i = 0; i < 3; ++i) {
        bool active = static_cast<int>(m_boardMode) == i;
        float x = w - 6.f - (tabW + 3.f) * (2.5f - static_cast<float>(i)) + 1.5f;
        button(m_boardTabMenu, names[i], {x, h - 15.f}, [this, i] {
            if (static_cast<int>(m_boardMode) == i) return;
            m_boardMode = static_cast<Board>(i);
            deferRebuild(false, false, false, true);
        }, active ? pui::Btn::Green : pui::Btn::Gray, tabW, .42f);
    }
}

void LiveSlotEditor::buildBoardContent() {
    m_boardContent->removeAllChildren();
    auto size = m_boardContent->getContentSize();
    float w = size.width;
    float h = size.height;
    auto fitFrame = [&](char const* frame, float target, CCPoint pos) -> CCSprite* {
        if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame)) return nullptr;
        auto* sprite = CCSprite::createWithSpriteFrameName(frame);
        auto content = sprite->getContentSize();
        float longest = std::max(content.width, content.height);
        if (longest > 0.f) sprite->setScale(target / longest);
        sprite->setPosition(pos);
        m_boardContent->addChild(sprite, 2);
        return sprite;
    };

    if (m_boardMode == Board::Buttons) {
        struct Entry { char const* texture; char const* name; LiveTone tone; };
        constexpr Entry entries[] = {
            {"GJ_button_01.png", "Green", LiveTone::Primary},
            {"GJ_button_02.png", "Cyan", LiveTone::Secondary},
            {"GJ_button_03.png", "Pink", LiveTone::Pink},
            {"GJ_button_04.png", "Gray", LiveTone::Neutral},
            {"GJ_button_05.png", "Blue", LiveTone::Blue},
            {"GJ_button_06.png", "Red", LiveTone::Red},
        };
        float cellW = w * .5f;
        float rowH = std::min(38.f, (h - 40.f) / 3.f);
        for (int i = 0; i < 6; ++i) {
            float cx = cellW * (static_cast<float>(i % 2) + .5f);
            float cy = h - 4.f - rowH * (static_cast<float>(i / 2) + .5f);
            if (auto* sprite = pui::makeButtonSprite(entries[i].name, entries[i].texture, cellW - 22.f, .55f,
                    "bigFont.fnt")) {
                sprite->setPosition({cx, cy + 4.f});
                m_boardContent->addChild(sprite, 2);
            }
            label(m_boardContent, std::string("-> ") + liveToneName(entries[i].tone), {cx, cy - 12.f}, .2f,
                kMuted, {.5f, .5f}, cellW - 10.f);
        }
        char const* icons[] = {"GJ_arrow_01_001.png", "GJ_plusBtn_001.png", "GJ_infoIcon_001.png",
                               "GJ_likeBtn_001.png", "GJ_deleteBtn_001.png", "GJ_chatBtn_001.png"};
        for (int i = 0; i < 6; ++i) {
            fitFrame(icons[i], 24.f, {w * (static_cast<float>(i) + .5f) / 6.f, 16.f});
        }
    } else if (m_boardMode == Board::Panels) {
        if (auto* backdrop = CCSprite::create("GJ_gradientBG.png")) {
            auto content = backdrop->getContentSize();
            backdrop->setAnchorPoint({0.f, 0.f});
            backdrop->setScaleX(w / std::max(1.f, content.width));
            backdrop->setScaleY(h / std::max(1.f, content.height));
            backdrop->setColor({0, 102, 255});
            m_boardContent->addChild(backdrop, 0);
        }
        float pw = w - 30.f;
        float ph = h - 12.f;
        auto* popup = CCScale9Sprite::create("GJ_square01.png");
        popup->setContentSize({pw, ph});
        popup->setPosition({w * .5f, h * .5f});
        m_boardContent->addChild(popup, 1);
        auto* heading = pui::makeTitle("Popup", pw - 20.f, .5f);
        heading->setPosition({w * .5f, h * .5f + ph * .5f - 14.f});
        m_boardContent->addChild(heading, 3);

        float listW = pw - 28.f;
        float rowH = std::min(20.f, (ph - 70.f) / 3.f);
        float listTop = h * .5f + ph * .5f - 28.f;
        auto* listBg = CCLayerColor::create({191, 114, 62, 255}, listW, rowH * 3.f);
        listBg->setPosition({w * .5f - listW * .5f, listTop - rowH * 3.f});
        m_boardContent->addChild(listBg, 2);
        for (int i = 0; i < 3; ++i) {
            ccColor4B fill = (i % 2) ? ccColor4B{194, 114, 62, 255} : ccColor4B{161, 88, 44, 255};
            auto* row = CCLayerColor::create(fill, listW, rowH - 1.f);
            row->setPosition({w * .5f - listW * .5f, listTop - rowH * (i + 1)});
            m_boardContent->addChild(row, 2);
            label(m_boardContent, fmt::format("List row {}", i + 1), {w * .5f - listW * .5f + 6.f,
                listTop - rowH * (i + .5f)}, .26f, kText, {0.f, .5f}, listW - 12.f);
        }
        float buttonsY = h * .5f - ph * .5f + 16.f;
        if (auto* ok = pui::makeButtonSprite("OK", "GJ_button_01.png", 0.f, .55f)) {
            ok->setPosition({w * .5f - 34.f, buttonsY});
            m_boardContent->addChild(ok, 3);
        }
        if (auto* cancel = pui::makeButtonSprite("Cancel", "GJ_button_06.png", 0.f, .55f)) {
            cancel->setPosition({w * .5f + 34.f, buttonsY});
            m_boardContent->addChild(cancel, 3);
        }
    } else {
        float row1 = h - 18.f;
        char const* arrows[] = {"GJ_arrow_01_001.png", "GJ_arrow_02_001.png", "GJ_arrow_03_001.png"};
        for (int i = 0; i < 3; ++i) fitFrame(arrows[i], 24.f, {w * (i + .5f) / 3.f, row1});

        if (auto* slider = Slider::create(this, menu_selector(LiveSlotEditor::onBoardSlider), .6f)) {
            slider->setPosition({w * .5f, row1 - 34.f});
            slider->setValue(.65f);
            m_boardContent->addChild(slider, 2);
        }
        float row3 = row1 - 70.f;
        char const* toggles[] = {"GJ_checkOn_001.png", "GJ_checkOff_001.png", "GJ_tabOn_001.png",
                                 "GJ_tabOff_001.png"};
        for (int i = 0; i < 4; ++i) fitFrame(toggles[i], 26.f, {w * (i + .5f) / 4.f, row3});

        float row4 = std::max(18.f, row3 - 40.f);
        char const* misc[] = {"GJ_closeBtn_001.png", "GJ_optionsBtn_001.png", "GJ_editBtn_001.png",
                              "GJ_resetBtn_001.png"};
        for (int i = 0; i < 4; ++i) fitFrame(misc[i], 26.f, {w * (i + .5f) / 4.f, row4});
    }
}

void LiveSlotEditor::deferRebuild(bool page, bool keepScroll, bool tabs, bool board) {
    if (page) {
        m_rebuild.keepScroll = m_rebuild.page ? (m_rebuild.keepScroll && keepScroll) : keepScroll;
        m_rebuild.page = true;
    }
    m_rebuild.tabs = m_rebuild.tabs || tabs;
    m_rebuild.board = m_rebuild.board || board;
    if (m_rebuild.queued) return;
    m_rebuild.queued = true;
    WeakRef<LiveSlotEditor> weak(this);
    Loader::get()->queueInMainThread([weak] {
        if (auto self = weak.lock()) self->flushRebuild();
    });
}

void LiveSlotEditor::flushRebuild() {
    m_rebuild.queued = false;
    if (m_rebuild.tabs) buildTabs();
    if (m_rebuild.page) buildPage(m_rebuild.keepScroll);
    if (m_rebuild.board) {
        buildBoardTabs();
        buildBoardContent();
    }
    m_rebuild.page = m_rebuild.tabs = m_rebuild.board = false;
    m_rebuild.keepScroll = true;
}

void LiveSlotEditor::openPicker(ColorMember member, FlagMember autoFlag) {
    auto tones = LiveSlotRuntime::tonesFor(m_project);
    auto current = m_project.*member;
    for (auto const& slot : kSlots) {
        if (slot.color == member && slot.autoFlag && m_project.*(slot.autoFlag)) {
            current = tones[static_cast<std::size_t>(slot.tone)];
        }
    }
    auto* picker = ColorPickPopup::create(current);
    if (!picker) return;
    m_pickerTarget = CCSprite::create();
    m_pickerTarget->setColor(current);
    m_pickerColor = member;
    m_pickerAuto = autoFlag;
    m_picker = picker;
    picker->setColorTarget(m_pickerTarget);
    schedule(schedule_selector(LiveSlotEditor::previewPicker));
    WeakRef<LiveSlotEditor> weak(this);
    picker->setCallback([weak, member, autoFlag](ccColor4B const& color) {
        auto self = weak.lock();
        if (!self) return;
        self->unschedule(schedule_selector(LiveSlotEditor::previewPicker));
        self->m_pickerColor = nullptr;
        self->m_pickerAuto = nullptr;
        ccColor3B picked{color.r, color.g, color.b};
        if (autoFlag && self->m_project.*autoFlag) {
            // confirming the derived color unchanged keeps it automatic.
            auto tones = LiveSlotRuntime::tonesFor(self->m_project);
            for (auto const& slot : kSlots) {
                if (slot.color == member && sameColor(picked, tones[static_cast<std::size_t>(slot.tone)])) {
                    return;
                }
            }
        }
        self->m_project.*member = picked;
        if (autoFlag) self->m_project.*autoFlag = false;
        self->edited();
        self->deferRebuild(true, true);
    });
    picker->show();
}

void LiveSlotEditor::swapColors() {
    std::swap(m_project.color1, m_project.color2);
    edited();
    deferRebuild(true, true);
}

void LiveSlotEditor::makeComplementary() {
    auto hsv = paimon::icons::math::toHSV(m_project.color1);
    hsv.h = std::fmod(hsv.h + 180.f, 360.f);
    m_project.color2 = paimon::icons::math::fromHSV(hsv);
    edited();
    deferRebuild(true, true);
}

void LiveSlotEditor::randomizePalette() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> hueDist(0.f, 360.f);
    std::uniform_real_distribution<float> satDist(0.70f, 1.0f);
    std::uniform_real_distribution<float> valDist(0.85f, 1.0f);

    float h1 = hueDist(gen);
    float h2 = std::fmod(h1 + 120.f + (hueDist(gen) > 180.f ? 60.f : 0.f), 360.f);

    m_project.color1 = paimon::icons::math::fromHSV({h1, satDist(gen), valDist(gen)});
    m_project.color2 = paimon::icons::math::fromHSV({h2, satDist(gen), valDist(gen)});
    edited();
    deferRebuild(true, true);
}

void LiveSlotEditor::resetAll() {
    WeakRef<LiveSlotEditor> weak(this);
    PopupManager::get().quickPopup("Reset Slot",
        "Reset every color and setting of this slot to the defaults?",
        "Cancel", "Reset",
        [weak](FLAlertLayer*, bool yes) {
            auto self = weak.lock();
            if (!yes || !self) return;
            auto defaults = TextureProject{};
            defaults.id = self->m_project.id;
            defaults.name = self->m_project.name;
            defaults.author = self->m_project.author;
            defaults.createdAt = self->m_project.createdAt;
            LiveSlotRuntime::normalize(defaults);
            self->m_project = std::move(defaults);
            self->edited(true);
            self->deferRebuild(true, false);
        }).showInstant();
}

void LiveSlotEditor::edited(bool regions) {
    m_dirty = true;
    m_project.modifiedAt = nowUnixMs();
    unschedule(schedule_selector(LiveSlotEditor::applyPreview));
    if (regions) scheduleOnce(schedule_selector(LiveSlotEditor::applyPreview), .25f);
    else applyPreview();
}

void LiveSlotEditor::applyPreview(float) {
    if (m_compare) return;
    if (auto result = LiveSlotRuntime::get().preview(m_project); !result) {
        Notification::create(result.unwrapErr(), NotificationIcon::Error)->show();
    }
}

void LiveSlotEditor::refreshStatus(float) {
    auto& runtime = LiveSlotRuntime::get();
    std::string text = m_compare ? "Original colors" : runtime.status();
    if (m_dirty) text += " - unsaved";
    if (m_status && text != m_status->getString()) {
        m_status->setString(text.c_str());
        m_status->limitLabelWidth(m_progressWidth, .3f, .14f);
    }
    float progress = m_compare ? -1.f : runtime.progress();
    if (m_progressFill && m_progressTrack) {
        bool busy = progress >= 0.f;
        m_progressTrack->setVisible(busy);
        m_progressFill->setVisible(busy);
        if (busy) m_progressFill->setScaleX(std::max(.01f, m_progressWidth * progress) / 2.f);
    }
    if (m_compareLabel) {
        char const* caption = m_compare ? "Show Painted" : "Show Original";
        if (std::string_view(caption) != m_compareLabel->getString()) m_compareLabel->setString(caption);
    }
}

void LiveSlotEditor::previewPicker(float) {
    if (!m_pickerTarget || !m_pickerColor) return;
    if (!m_picker.lock()) {
        // closed without confirming: keep the live edits and refresh the swatches.
        unschedule(schedule_selector(LiveSlotEditor::previewPicker));
        m_pickerColor = nullptr;
        m_pickerAuto = nullptr;
        deferRebuild(true, true);
        return;
    }
    auto color = m_pickerTarget->getColor();
    auto previous = m_project.*m_pickerColor;
    bool wasAuto = m_pickerAuto && m_project.*m_pickerAuto;
    if (!wasAuto && sameColor(color, previous)) return;
    if (wasAuto) {
        // opening the picker on an Auto color must not switch it to manual.
        auto tones = LiveSlotRuntime::tonesFor(m_project);
        bool same = false;
        for (auto const& slot : kSlots) {
            if (slot.color == m_pickerColor) same = sameColor(color, tones[static_cast<std::size_t>(slot.tone)]);
        }
        if (same) return;
        m_project.*m_pickerAuto = false;
    }
    m_project.*m_pickerColor = color;
    edited();
}

void LiveSlotEditor::exportConfig() {
    auto name = SlotPaths::sanitizeFilename(m_project.name) + ".json";
    WeakRef<LiveSlotEditor> weak(this);
    pt::saveJson(name, [weak](geode::Result<std::optional<std::filesystem::path>> result) {
        auto self = weak.lock();
        auto path = std::move(result).unwrapOr(std::nullopt);
        if (!self || !path) return;
        auto exported = ProjectShare::exportTo(*path, self->m_project);
        if (!exported) {
            Notification::create(exported.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
        Notification::create("Slot configuration exported.", NotificationIcon::Success)->show();
    });
}

void LiveSlotEditor::importConfig() {
    WeakRef<LiveSlotEditor> weak(this);
    pt::pickJson([weak](geode::Result<std::optional<std::filesystem::path>> result) {
        auto self = weak.lock();
        auto path = std::move(result).unwrapOr(std::nullopt);
        if (!self || !path) return;
        auto rd = geode::utils::file::readJson(paimon::paths::toSdkPath(*path));
        if (!rd) {
            Notification::create("Failed to read JSON: " + rd.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
        auto parsed = rd.unwrap().as<TextureProject>();
        if (!parsed) {
            Notification::create("Invalid Texture Project JSON", NotificationIcon::Error)->show();
            return;
        }
        auto project = parsed.unwrap();
        project.id = self->m_project.id;
        if (project.name.empty()) project.name = self->m_project.name;
        LiveSlotRuntime::normalize(project);
        self->m_project = std::move(project);
        if (self->m_name) self->m_name->setString(self->m_project.name);
        self->edited(true);
        self->deferRebuild(true, false);
        Notification::create("Configuration imported successfully!", NotificationIcon::Success)->show();
    });
}

void LiveSlotEditor::save() {
    unschedule(schedule_selector(LiveSlotEditor::applyPreview));
    if (m_project.name.find_first_not_of(" \t\r\n") == std::string::npos) {
        Notification::create("Enter a slot name.", NotificationIcon::Warning)->show();
        return;
    }
    LiveSlotRuntime::normalize(m_project);
    m_project.modifiedAt = nowUnixMs();
    auto saved = SlotStore::get().saveSlot(m_project);
    if (!saved) {
        Notification::create("Save failed: " + saved.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    auto applied = LiveSlotRuntime::get().activate(m_project);
    if (!applied) {
        Notification::create("Saved; apply failed: " + applied.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    m_compare = false;
    m_dirty = false;
    refreshStatus(0.f);
    Notification::create("Slot saved and applied to game!", NotificationIcon::Success)->show();
}

void LiveSlotEditor::onExit() {
    unschedule(schedule_selector(LiveSlotEditor::applyPreview));
    unschedule(schedule_selector(LiveSlotEditor::previewPicker));
    if (!paimon::isRuntimeShuttingDown()) LiveSlotRuntime::get().restoreSaved();
    CCLayer::onExit();
}

void LiveSlotEditor::keyBackClicked() { back(); }

void LiveSlotEditor::back() {
    if (!m_dirty) return close();
    WeakRef<LiveSlotEditor> weak(this);
    PopupManager::get().quickPopup("Unsaved Changes",
        "Leave the editor and <cr>discard</c> your changes?",
        "Stay", "Leave",
        [weak](FLAlertLayer*, bool leave) {
            auto self = weak.lock();
            if (leave && self) self->close();
        }).showInstant();
}

void LiveSlotEditor::close() {
    CCDirector::get()->popSceneWithTransition(.3f, PopTransition::kPopTransitionFade);
}

} // namespace paimon::texture_studio
