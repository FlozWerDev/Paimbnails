#include "FontPickerPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <algorithm>
#include <memory>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::fonts;

namespace {
constexpr float POPUP_W = 400.f;
constexpr float POPUP_H = 300.f;
constexpr float GRID_X = 22.f;
constexpr float GRID_Y = 59.f;
constexpr float GRID_W = 348.f;
constexpr float GRID_H = 124.f;
constexpr float CELL_W = 52.f;
constexpr float CELL_H = 38.f;
constexpr float CELL_GAP = 5.f;
constexpr int GRID_COLS = 6;

struct QuickFont {
    char const* spanish;
    char const* english;
    char const* file;
    char const* id;
};
constexpr QuickFont QUICK_FONTS[] = {
    {"Grande", "Big", "bigFont.fnt", "big"},
    {"Chat", "Chat", "chatFont.fnt", "chat"},
    {"Dorada", "Gold", "goldFont.fnt", "gold"},
};

char const* localized(char const* spanish, char const* english) {
    return Localization::get().getLanguage() == Localization::Language::SPANISH ? spanish : english;
}

void fitCellBackground(ButtonSprite* background) {
    background->setScaleX(CELL_W / background->getContentWidth());
    background->setScaleY(CELL_H / background->getContentHeight());
}

// a clipped row may still have a menu hitbox outside the visible list.
class FontGridMenu : public CCMenu {
public:
    static FontGridMenu* create(ScrollLayer* scroll) {
        auto menu = std::make_unique<FontGridMenu>();
        if (!menu->init()) return nullptr;
        menu->m_scroll = scroll;
        menu->autorelease();
        return menu.release();
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        return containsTouch(touch) && CCMenu::ccTouchBegan(touch, event);
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) override {
        if (containsTouch(touch)) CCMenu::ccTouchEnded(touch, event);
        else CCMenu::ccTouchCancelled(touch, event);
    }

private:
    ScrollLayer* m_scroll = nullptr;

    bool containsTouch(CCTouch* touch) const {
        auto const point = m_scroll->convertToNodeSpace(touch->getLocation());
        return CCRect{CCPointZero, m_scroll->getContentSize()}.containsPoint(point);
    }
};
}

FontPickerPopup* FontPickerPopup::create(CopyableFunction<void(std::string const&)> onSelect) {
    auto popup = std::make_unique<FontPickerPopup>();
    if (!popup->init(std::move(onSelect))) return nullptr;
    popup->autorelease();
    return popup.release();
}

bool FontPickerPopup::init(CopyableFunction<void(std::string const&)> onSelect) {
    if (!PaimonPopup::init(POPUP_W, POPUP_H)) return false;
    m_onSelect = std::move(onSelect);
    setID("font-picker-popup"_spr);
    setTitle(localized("Fuentes", "Fonts"));
    addInfoButton(localized("Fuentes", "Fonts"),
        localized("Elige una fuente para tu texto. <cg>Fuentes GD</c> incluye las 59 fuentes del juego. "
            "<cy>Personalizada</c> permite usar un archivo .fnt de los recursos del juego. "
            "<cr>Normal</c> restaura la fuente original.",
            "Choose a font for your text. <cg>GD Fonts</c> includes the game's 59 numbered fonts. "
            "<cy>Custom</c> accepts a .fnt file from the game resources. "
            "<cr>Default</c> restores the original font."));

    auto* tabMenu = paimon::ui::makeZeroMenu("font-tabs-menu"_spr);
    m_mainLayer->addChild(tabMenu, 3);
    m_tabGD = paimon::ui::makeButton(localized("Fuentes GD", "GD Fonts"),
        [this] { switchTab(Tab::GDFonts); }, paimon::ui::Btn::Green, 130.f, .65f, "bigFont.fnt");
    m_tabGD->setID("font-tab-gd"_spr);
    m_tabGD->setPosition({130.f, 254.f});
    tabMenu->addChild(m_tabGD);
    m_tabCustom = paimon::ui::makeButton(localized("Personalizada", "Custom"),
        [this] { switchTab(Tab::Custom); }, paimon::ui::Btn::Gray, 130.f, .65f, "bigFont.fnt");
    m_tabCustom->setID("font-tab-custom"_spr);
    m_tabCustom->setPosition({270.f, 254.f});
    tabMenu->addChild(m_tabCustom);
    paimon::ui::matchButtonLabels({m_tabGD, m_tabCustom});

    auto* previewBackground = paimon::ui::makeInset({372.f, 42.f}, 110);
    previewBackground->setPosition({14.f, 198.f});
    m_mainLayer->addChild(previewBackground, 1);
    m_previewContainer = CCNode::create();
    m_previewContainer->setID("font-preview"_spr);
    m_previewContainer->setContentSize({372.f, 42.f});
    m_previewContainer->setPosition({14.f, 198.f});
    m_mainLayer->addChild(m_previewContainer, 2);
    auto* previewTitle = paimon::ui::makeTitle(localized("Vista previa", "Preview"), 100.f, .35f);
    previewTitle->setAnchorPoint({0.f, .5f});
    previewTitle->setPosition({12.f, 29.f});
    m_previewContainer->addChild(previewTitle);
    m_previewLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_previewLabel->setAnchorPoint({0.f, .5f});
    m_previewLabel->setPosition({12.f, 13.f});
    m_previewContainer->addChild(m_previewLabel);

    auto* gridBackground = paimon::ui::makeInset({372.f, 138.f}, 90);
    gridBackground->setPosition({14.f, 52.f});
    m_mainLayer->addChild(gridBackground, 1);
    m_scroll = ScrollLayer::create({GRID_W, GRID_H});
    if (m_scroll) m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setID("font-grid-scroll"_spr);
    m_scroll->setPosition({GRID_X, GRID_Y});
    m_scroll->setStealingTouches(true);
    m_mainLayer->addChild(m_scroll, 3);
    m_scrollbar = Scrollbar::create(m_scroll);
    m_scrollbar->setID("font-grid-scrollbar"_spr);
    m_scrollbar->setPosition({380.f, GRID_Y + GRID_H * .5f});
    m_mainLayer->addChild(m_scrollbar, 4);

    m_contentNode = CCNode::create();
    m_contentNode->setAnchorPoint({0.f, 0.f});
    m_contentNode->ignoreAnchorPointForPosition(false);
    m_scroll->m_contentLayer->addChild(m_contentNode);
    buildGDFontGrid();

    m_customContainer = CCNode::create();
    m_customContainer->setID("font-custom-panel"_spr);
    m_customContainer->setContentSize({GRID_W, GRID_H});
    m_customContainer->setPosition({GRID_X, GRID_Y});
    m_mainLayer->addChild(m_customContainer, 3);
    auto* customTitle = paimon::ui::makeTitle(localized("Fuente personalizada", "Custom font"), 300.f, .45f);
    customTitle->setPosition({GRID_W * .5f, 108.f});
    m_customContainer->addChild(customTitle);
    auto* customHint = paimon::ui::makeText(
        localized("Nombre del archivo en los recursos del juego", "Font filename in the game resources"),
        310.f, .6f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
    customHint->setPosition({GRID_W * .5f, 87.f});
    m_customContainer->addChild(customHint);
    m_customInput = TextInput::create(280.f, "myFont.fnt", "chatFont.fnt");
    m_customInput->setID("font-custom-input"_spr);
    m_customInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
    m_customInput->setMaxCharCount(64);
    m_customInput->setScale(.85f);
    m_customInput->setPosition({GRID_W * .5f, 60.f});
    m_customContainer->addChild(m_customInput);
    auto* customMenu = paimon::ui::makeZeroMenu();
    m_customContainer->addChild(customMenu, 1);
    auto* apply = paimon::ui::makeButton(localized("Usar", "Apply"),
        [this] { onCustomApply(nullptr); }, paimon::ui::Btn::Green, 96.f, .7f, "bigFont.fnt");
    apply->setID("font-custom-apply"_spr);
    apply->setPosition({GRID_W * .5f, 25.f});
    customMenu->addChild(apply);

    auto* presets = paimon::ui::makeZeroMenu("font-presets-menu"_spr);
    m_mainLayer->addChild(presets, 3);
    for (std::size_t index = 0; index < std::size(QUICK_FONTS); ++index) {
        auto const& font = QUICK_FONTS[index];
        auto* sprite = paimon::ui::makeButtonSprite(localized(font.spanish, font.english),
            paimon::ui::Btn::Gray, 78.f, .65f, font.file);
        auto* button = CCMenuItemSpriteExtra::create(sprite, this,
            menu_selector(FontPickerPopup::onQuickPick));
        button->setID(fmt::format("font-preset-{}", font.id));
        button->setUserObject(CCString::create(font.id));
        button->setPosition({71.f + 86.f * static_cast<float>(index), 27.f});
        presets->addChild(button);
        m_quickButtons.push_back(button);
    }
    auto* reset = paimon::ui::makeButton(localized("Normal", "Default"),
        [this] { onRemoveFont(nullptr); }, paimon::ui::Btn::Red, 78.f, .65f, "bigFont.fnt");
    reset->setID("font-reset"_spr);
    reset->setPosition({329.f, 27.f});
    presets->addChild(reset);

    showPreview("", "chatFont.fnt");
    switchTab(Tab::GDFonts);
    scheduleUpdate();
    return true;
}

void FontPickerPopup::switchTab(Tab tab) {
    m_activeTab = tab;
    updateTabHighlights();
    bool const isGD = tab == Tab::GDFonts;
    m_scroll->setVisible(isGD);
    m_scroll->setTouchEnabled(isGD);
    m_scroll->setMouseEnabled(isGD);
    m_gridMenu->setTouchEnabled(isGD);
    m_scrollbar->setVisible(isGD);
    m_scrollbar->setTouchEnabled(isGD);
    m_scrollbar->setMouseEnabled(isGD);
    m_customContainer->setVisible(!isGD);
    if (isGD) m_customInput->unfocus();
    m_fontPreviewsPending = true;
}

void FontPickerPopup::updateTabHighlights() {
    paimon::ui::setButtonSkin(m_tabGD,
        m_activeTab == Tab::GDFonts ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    paimon::ui::setButtonSkin(m_tabCustom,
        m_activeTab == Tab::Custom ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    paimon::ui::matchButtonLabels({m_tabGD, m_tabCustom});
}

void FontPickerPopup::buildGDFontGrid() {
    constexpr int count = 59;
    constexpr int rows = (count + GRID_COLS - 1) / GRID_COLS;
    constexpr float totalH = rows * CELL_H + (rows - 1) * CELL_GAP;
    constexpr float left = (GRID_W - GRID_COLS * CELL_W - (GRID_COLS - 1) * CELL_GAP) * .5f;
    m_contentNode->setContentSize({GRID_W, totalH});
    m_scroll->m_contentLayer->setContentSize({GRID_W, totalH});
    m_gridMenu = FontGridMenu::create(m_scroll);
    m_gridMenu->setID("font-grid-menu"_spr);
    m_gridMenu->setPosition({0.f, 0.f});
    m_gridMenu->setContentSize({GRID_W, totalH});
    m_contentNode->addChild(m_gridMenu);
    m_fontPreviews.reserve(count);
    for (int index = 0; index < count; ++index) {
        auto const fontId = fmt::format("{:02}", index + 1);
        auto* face = CCNode::create();
        face->setContentSize({CELL_W, CELL_H});
        auto* background = paimon::ui::makeButtonSprite("", paimon::ui::Btn::Gray, CELL_W, 1.f);
        fitCellBackground(background);
        background->setPosition({CELL_W * .5f, CELL_H * .5f});
        face->addChild(background);
        auto* number = CCLabelBMFont::create(fontId.c_str(), "chatFont.fnt");
        number->setScale(.5f);
        number->setPosition({CELL_W * .5f, 7.f});
        face->addChild(number, 1);
        m_fontPreviews.push_back({face, background, fmt::format("gjFont{}.fnt", fontId)});
        auto* button = CCMenuItemSpriteExtra::create(face, this,
            menu_selector(FontPickerPopup::onFontClicked));
        button->setID(fmt::format("font-{}", fontId));
        button->setUserObject(CCString::create(fontId));
        button->m_scaleMultiplier = 1.06f;
        button->setPosition({left + CELL_W * .5f + (index % GRID_COLS) * (CELL_W + CELL_GAP),
            totalH - CELL_H * .5f - (index / GRID_COLS) * (CELL_H + CELL_GAP)});
        m_gridMenu->addChild(button);
    }
    m_scroll->scrollToTop();
}

void FontPickerPopup::update(float) {
    if (m_activeTab != Tab::GDFonts) return;
    float const scrollY = m_scroll->m_contentLayer->getPositionY();
    if (!m_fontPreviewsPending && scrollY == m_lastPreviewScrollY) return;
    m_lastPreviewScrollY = scrollY;
    m_fontPreviewsPending = false;
    // load one visible atlas per frame and retain it when switching tabs.
    for (auto& entry : m_fontPreviews) {
        if (entry.loaded) continue;
        auto const center = m_scroll->convertToNodeSpace(
            entry.cell->convertToWorldSpace({CELL_W * .5f, CELL_H * .5f}));
        if (center.y < -CELL_H || center.y > GRID_H + CELL_H) continue;
        entry.loaded = true;
        if (auto* preview = CCLabelBMFont::create("ABC", entry.file.c_str())) {
            float const sx = (CELL_W - 10.f) / std::max(1.f, preview->getContentWidth());
            float const sy = (CELL_H - 16.f) / std::max(1.f, preview->getContentHeight());
            preview->setScale(std::min({sx, sy, .55f}));
            preview->setPosition({CELL_W * .5f, CELL_H * .5f + 5.f});
            entry.cell->addChild(preview, 1);
        }
        m_fontPreviewsPending = true;
        break;
    }
}

void FontPickerPopup::showPreview(std::string const& fontId, std::string const& fontFile) {
    if (m_previewFontSprite) {
        m_previewFontSprite->removeFromParent();
        m_previewFontSprite = nullptr;
    }
    auto* preview = CCLabelBMFont::create("GEOMETRY DASH", fontFile.c_str());
    if (preview) {
        preview->setScale(std::min({226.f / std::max(1.f, preview->getContentWidth()),
            28.f / std::max(1.f, preview->getContentHeight()), .7f}));
        preview->setPosition({246.f, 21.f});
        m_previewContainer->addChild(preview, 1);
        m_previewFontSprite = preview;
    }
    m_previewLabel->setString(fontId.empty() ? localized("Normal", "Default") : fontId.c_str());
    m_previewLabel->limitLabelWidth(100.f, .65f, .15f);
    for (auto const& entry : m_fontPreviews) {
        if (entry.file != m_selectedFontFile && entry.file != fontFile) continue;
        entry.background->updateBGImage(paimon::ui::buttonTexture(
            entry.file == fontFile ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray));
        fitCellBackground(entry.background);
    }
    m_selectedFontFile = fontFile;
    for (std::size_t index = 0; index < m_quickButtons.size(); ++index) {
        paimon::ui::setButtonSkin(m_quickButtons[index],
            fontId == QUICK_FONTS[index].id ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
}

void FontPickerPopup::onQuickPick(CCObject* sender) {
    auto* button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    auto* name = button ? typeinfo_cast<CCString*>(button->getUserObject()) : nullptr;
    if (!name) return;
    for (auto const& font : QUICK_FONTS) {
        if (font.id != std::string_view(name->getCString())) continue;
        showPreview(font.id, font.file);
        if (m_onSelect) m_onSelect(fmt::format("<f:{}> ", font.id));
        return;
    }
}

void FontPickerPopup::onFontClicked(CCObject* sender) {
    if (m_activeTab != Tab::GDFonts) return;
    auto* button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    auto* name = button ? typeinfo_cast<CCString*>(button->getUserObject()) : nullptr;
    if (!name) return;
    std::string const fontId = name->getCString();
    showPreview(fontId, fmt::format("gjFont{}.fnt", fontId));
    if (m_onSelect) m_onSelect(fmt::format("<f:{}> ", fontId));
}

void FontPickerPopup::onCustomApply(CCObject*) {
    auto value = m_customInput->getString();
    if (value.empty()) return;
    if (!value.ends_with(".fnt")) value += ".fnt";
    auto* files = CCFileUtils::sharedFileUtils();
    auto const path = files->fullPathForFilename(value.c_str(), false);
    if (path.empty() || !files->isFileExist(path)) {
        Notification::create(localized("No se encontro la fuente", "Font not found"), NotificationIcon::Error)->show();
        return;
    }
    showPreview(value, value);
    if (m_onSelect) m_onSelect(fmt::format("<f:{}> ", value));
}

void FontPickerPopup::onRemoveFont(CCObject*) {
    showPreview("", "chatFont.fnt");
    if (m_onSelect) m_onSelect("");
}

bool FontPickerPopup::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto const point = m_mainLayer->convertToNodeSpace(touch->getLocation());
    m_touchHitOutside = !CCRect{CCPointZero, m_mainLayer->getContentSize()}.containsPoint(point);
    return true;
}

void FontPickerPopup::ccTouchEnded(CCTouch* touch, CCEvent*) {
    auto const point = m_mainLayer->convertToNodeSpace(touch->getLocation());
    if (m_touchHitOutside && !CCRect{CCPointZero, m_mainLayer->getContentSize()}.containsPoint(point)) {
        onClose(nullptr);
    }
}

void FontPickerPopup::closeAnimated() {
    onClose(nullptr);
}
