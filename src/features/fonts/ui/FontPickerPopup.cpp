#include "FontPickerPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::fonts;

static constexpr float POPUP_W   = 360.f;
static constexpr float POPUP_H   = 218.f;
static constexpr float PAD       = 8.f;
static constexpr float PREVIEW_H = 36.f;
static constexpr float SIDEBAR_W = 76.f;
static constexpr float CELL_SIZE = 44.f;
static constexpr float CELL_GAP  = 4.f;

static std::vector<std::pair<std::string, std::string>> getGDFonts() {
    std::vector<std::pair<std::string, std::string>> v;
    v.reserve(59);
    for (int i = 1; i <= 59; ++i) {
        char id[8], file[32];
        std::snprintf(id, sizeof(id), "%02d", i);
        std::snprintf(file, sizeof(file), "gjFont%02d.fnt", i);
        v.push_back({id, file});
    }
    return v;
}

FontPickerPopup* FontPickerPopup::create(
        CopyableFunction<void(std::string const&)> onSelect) {
    auto ret = new FontPickerPopup();
    if (ret && ret->init(std::move(onSelect))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool FontPickerPopup::init(
        CopyableFunction<void(std::string const&)> onSelect) {
    m_onSelect = std::move(onSelect);

    if (!PaimonPopup::init(POPUP_W, POPUP_H))
        return false;
    paimon::markDynamicPopup(this);

    if (m_closeBtn) m_closeBtn->setVisible(false);

    this->setTitle("Fonts");
    this->addInfoButton("Font Picker",
        "Insert a <cy>font tag</c> into your text. <cg>GD Fonts</c> are the game's "
        "numbered fonts; <co>Custom</c> lets you type any <cy>.fnt</c> name present in "
        "the game resources. <cr>None</c> clears the font back to default.");

    float pvW = POPUP_W - PAD * 2;
    float pvY = POPUP_H - PAD - PREVIEW_H - 24.f;

    auto pvBg = paimon::ui::makeInset({pvW, PREVIEW_H}, 230);
    pvBg->setPosition({PAD, pvY});
    m_mainLayer->addChild(pvBg, 1);

    m_previewContainer = CCNode::create();
    m_previewContainer->setContentSize({pvW, PREVIEW_H});
    m_previewContainer->setPosition({PAD, pvY});
    m_mainLayer->addChild(m_previewContainer, 2);

    m_previewLabel = CCLabelBMFont::create("Pick a font!", "chatFont.fnt");
    m_previewLabel->setScale(0.32f);
    m_previewLabel->setPosition({pvW / 2.f, PREVIEW_H / 2.f});
    m_previewLabel->setColor(paimon::ui::palette::muted);
    m_previewContainer->addChild(m_previewLabel);

    float botH = pvY - PAD;
    float botY = PAD;
    float botW = POPUP_W - PAD * 2;

    auto botBg = paimon::ui::makeInset({botW, botH}, 90);
    botBg->setPosition({PAD, botY});
    m_mainLayer->addChild(botBg, 1);

    float sideX = PAD + 4;

    m_sideMenu = CCMenu::create();
    m_sideMenu->setID("font-sidebar-menu"_spr);
    m_sideMenu->setContentSize({SIDEBAR_W, botH - 4.f});
    m_sideMenu->setAnchorPoint({0.f, 1.f});
    m_sideMenu->ignoreAnchorPointForPosition(false);
    m_sideMenu->setPosition({sideX, botY + botH - 2.f});
    m_sideMenu->setLayout(
        ColumnLayout::create()
            ->setGap(4.f)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAxisReverse(true)
    );
    m_mainLayer->addChild(m_sideMenu, 3);

    auto makeTabBtn = [&](const char* text, SEL_MenuHandler sel) -> CCMenuItemSpriteExtra* {
        auto spr = paimon::ui::makeButtonSprite(text, paimon::ui::Btn::Gray,
            SIDEBAR_W - 8.f, 0.5f, "bigFont.fnt");
        return CCMenuItemSpriteExtra::create(spr, this, sel);
    };

    m_tabGD = makeTabBtn("GD Fonts",
        menu_selector(FontPickerPopup::onTabGD));
    m_sideMenu->addChild(m_tabGD);

    m_tabCustom = makeTabBtn("Custom",
        menu_selector(FontPickerPopup::onTabCustom));
    m_sideMenu->addChild(m_tabCustom);

    auto divLine = paimon::ui::makeDivider(SIDEBAR_W - 16.f);
    m_sideMenu->addChild(divLine);

    auto qpLabel = paimon::ui::makeLabel("Quick Pick", SIDEBAR_W - 10.f, 0.3f,
        paimon::ui::palette::gold);
    m_sideMenu->addChild(qpLabel);

    struct QuickFont { const char* display; const char* fontFile; const char* fontId; };
    QuickFont quickFonts[] = {
        {"Big",  "bigFont.fnt",  "big"},
        {"Chat", "chatFont.fnt", "chat"},
        {"Gold", "goldFont.fnt", "gold"},
    };

    float qpBtnH = 18.f;

    for (int i = 0; i < 3; ++i) {
        float bw = SIDEBAR_W - 8;
        auto container = CCNode::create();
        container->setContentSize({bw, qpBtnH});

        auto cellBg = paimon::ui::makeInset({bw, qpBtnH}, 150);
        cellBg->setPosition({0, 0});
        container->addChild(cellBg);

        auto lbl = CCLabelBMFont::create(quickFonts[i].display, quickFonts[i].fontFile);
        if (lbl) {
            float maxW = bw - 8.f;
            float scX = maxW / lbl->getContentSize().width;
            float scY = (qpBtnH - 6.f) / lbl->getContentSize().height;
            lbl->setScale(std::min({scX, scY, 0.28f}));
            lbl->setPosition({bw / 2, qpBtnH / 2});
            container->addChild(lbl, 1);
        }

        auto btn = CCMenuItemSpriteExtra::create(
            container, this, menu_selector(FontPickerPopup::onQuickPick));
        btn->setUserObject(CCString::create(quickFonts[i].fontId));
        m_sideMenu->addChild(btn);
    }

    {
        auto spr = paimon::ui::makeButtonSprite("None", paimon::ui::Btn::Red,
            SIDEBAR_W - 8.f, 0.4f, "bigFont.fnt");
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(FontPickerPopup::onRemoveFont));
        m_sideMenu->addChild(btn);
    }

    m_sideMenu->updateLayout();

    float divX = PAD + SIDEBAR_W + 4;
    auto divider = paimon::ui::makeInset({2.f, botH - 8.f}, 70);
    divider->setPosition({divX, botY + 4.f});
    m_mainLayer->addChild(divider, 2);

    float gridX = divX + 6.f;
    float gridW = PAD + botW - gridX + PAD;
    float gridH = botH - 4.f;

    m_scroll = ScrollLayer::create({gridW, gridH});
    m_scroll->setPosition({gridX, botY + 2.f});
    m_mainLayer->addChild(m_scroll, 3);

    m_contentNode = CCNode::create();
    m_contentNode->setContentSize({gridW, gridH});
    m_scroll->m_contentLayer->addChild(m_contentNode);

    m_customContainer = CCNode::create();
    m_customContainer->setContentSize({gridW, gridH});
    m_customContainer->setPosition({gridX, botY + 2.f});
    m_customContainer->setVisible(false);
    m_mainLayer->addChild(m_customContainer, 4);

    float cardW = gridW - 16.f;
    float cardH = 84.f;
    float cardX = 8.f;
    float cardY = std::max(14.f, gridH - cardH - 12.f);

    auto cardBg = paimon::ui::makeInset({cardW, cardH}, 150);
    cardBg->setPosition({cardX, cardY});
    m_customContainer->addChild(cardBg);

    auto customLabel = paimon::ui::makeLabel("Enter font name:", cardW - 16.f, 0.4f,
        paimon::ui::palette::muted);
    customLabel->setPosition({gridW / 2.f, cardY + cardH - 12.f});
    m_customContainer->addChild(customLabel);

    m_customInput = TextInput::create(cardW - 24.f, "myFont.fnt");
    m_customInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
    m_customInput->setMaxCharCount(64);
    m_customInput->setScale(0.64f);
    m_customInput->setPosition({gridW / 2.f, cardY + cardH / 2.f + 1.f});
    m_customContainer->addChild(m_customInput);

    auto applySpr = paimon::ui::makeButtonSprite("Apply", paimon::ui::Btn::Green,
        82.f, 0.5f, "bigFont.fnt");
    auto applyBtn = CCMenuItemSpriteExtra::create(
        applySpr, this, menu_selector(FontPickerPopup::onCustomApply));
    applyBtn->setPosition({gridW / 2.f, cardY + 14.f});

    auto customMenu = CCMenu::create();
    customMenu->setPosition({0.f, 0.f});
    customMenu->setContentSize({gridW, gridH});
    m_customContainer->addChild(customMenu, 10);
    customMenu->addChild(applyBtn);

    auto hintLabel = paimon::ui::makeLabel(
        ".fnt file in game resources", gridW - 10.f, 0.3f, paimon::ui::palette::dim);
    hintLabel->setPosition({gridW / 2.f, cardY - 8.f});
    m_customContainer->addChild(hintLabel);

    updateTabHighlights();
    switchTab(Tab::GDFonts);

    return true;
}

void FontPickerPopup::onTabGD(CCObject*)     { switchTab(Tab::GDFonts); }
void FontPickerPopup::onTabCustom(CCObject*) { switchTab(Tab::Custom); }

void FontPickerPopup::switchTab(Tab tab) {
    m_activeTab = tab;
    updateTabHighlights();

    bool isGD = (tab == Tab::GDFonts);
    m_scroll->setVisible(isGD);
    m_customContainer->setVisible(!isGD);

    if (isGD) buildGDFontGrid();
}

void FontPickerPopup::updateTabHighlights() {
    paimon::ui::setButtonSkin(m_tabGD,
        m_activeTab == Tab::GDFonts ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    paimon::ui::setButtonSkin(m_tabCustom,
        m_activeTab == Tab::Custom ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
}

void FontPickerPopup::buildGDFontGrid() {
    m_contentNode->removeAllChildren();

    auto gdFonts = getGDFonts();
    float gridW = m_scroll->getContentSize().width;
    float gridH = m_scroll->getContentSize().height;
    int cols = std::max(1,
        static_cast<int>((gridW + CELL_GAP) / (CELL_SIZE + CELL_GAP)));
    int rows = (static_cast<int>(gdFonts.size()) + cols - 1) / cols;
    float contentH = rows * (CELL_SIZE + CELL_GAP);
    float totalH = std::max(contentH, gridH);

    m_contentNode->setContentSize({gridW, totalH});
    m_scroll->m_contentLayer->setContentSize({gridW, totalH});

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    menu->setContentSize({gridW, totalH});
    menu->setLayout(
        RowLayout::create()
            ->setGap(CELL_GAP)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::End)
    );
    m_contentNode->addChild(menu);

    for (int i = 0; i < static_cast<int>(gdFonts.size()); ++i) {
        auto const& [fontId, fontFile] = gdFonts[i];

        auto cellBg = paimon::ui::makeInset({CELL_SIZE, CELL_SIZE}, 150);

        auto container = CCNode::create();
        container->setContentSize({CELL_SIZE, CELL_SIZE});
        cellBg->setPosition({0, 0});
        container->addChild(cellBg);

        auto preview = CCLabelBMFont::create("Abc", fontFile.c_str());
        if (preview) {
            float maxW = CELL_SIZE - 8.f;
            float maxH = CELL_SIZE - 16.f;
            float scX = maxW / preview->getContentSize().width;
            float scY = maxH / preview->getContentSize().height;
            preview->setScale(std::min({scX, scY, 0.45f}));
            preview->setPosition({CELL_SIZE / 2, CELL_SIZE / 2 + 4});
            container->addChild(preview, 1);
        }

        auto idLbl = CCLabelBMFont::create(fontId.c_str(), "chatFont.fnt");
        idLbl->setScale(0.22f);
        idLbl->setColor(paimon::ui::palette::dim);
        idLbl->setPosition({CELL_SIZE / 2, 7.f});
        container->addChild(idLbl, 1);

        auto btn = CCMenuItemSpriteExtra::create(
            container, this,
            menu_selector(FontPickerPopup::onFontClicked));
        btn->setUserObject(CCString::create(fontId));
        menu->addChild(btn);
    }

    menu->updateLayout();
    m_scroll->moveToTop();
}

void FontPickerPopup::showPreview(
        std::string const& fontId, std::string const& fontFile) {
    if (m_previewFontSprite) {
        m_previewFontSprite->removeFromParent();
        m_previewFontSprite = nullptr;
    }

    auto pvSize = m_previewContainer->getContentSize();
    float pvH = pvSize.height;

    auto preview = CCLabelBMFont::create("AaBbCc", fontFile.c_str());
    if (preview) {
        float maxD = pvH - 8.f;
        float sc = maxD / std::max(
            preview->getContentSize().width,
            preview->getContentSize().height);
        sc = std::min(sc, 0.42f);
        preview->setScale(sc);
        preview->setPosition({pvH / 2 + 6.f, pvH / 2});
        m_previewContainer->addChild(preview, 1);
        m_previewFontSprite = preview;
    }

    m_previewLabel->setString(fmt::format("Font: {}", fontId).c_str());
    m_previewLabel->setAnchorPoint({0.f, 0.5f});
    m_previewLabel->setPosition({pvH + 10.f, pvH / 2});
}

void FontPickerPopup::onQuickPick(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string fontId = nameObj->getCString();
    std::string fontFile;
    if (fontId == "big")       fontFile = "bigFont.fnt";
    else if (fontId == "chat") fontFile = "chatFont.fnt";
    else if (fontId == "gold") fontFile = "goldFont.fnt";
    else                       fontFile = "chatFont.fnt";

    showPreview(fontId, fontFile);

    std::string tag = "<f:" + fontId + "> ";
    if (m_onSelect) m_onSelect(tag);
}

void FontPickerPopup::onFontClicked(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    if (!isInsideVisibleScroll(btn)) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string fontId = nameObj->getCString();
    std::string fontFile = fmt::format("gjFont{}.fnt", fontId);

    showPreview(fontId, fontFile);

    std::string tag = "<f:" + fontId + "> ";
    if (m_onSelect) m_onSelect(tag);
}

void FontPickerPopup::onCustomApply(CCObject*) {
    if (!m_customInput) return;

    std::string val = m_customInput->getString();
    if (val.empty()) return;

    while (!val.empty() && val.front() == ' ') val.erase(val.begin());
    while (!val.empty() && val.back() == ' ') val.pop_back();
    if (val.empty()) return;

    if (val.size() < 4 || val.substr(val.size() - 4) != ".fnt") {
        val += ".fnt";
    }

    showPreview(val, val);

    std::string tag = "<f:" + val + "> ";
    if (m_onSelect) m_onSelect(tag);
}

void FontPickerPopup::onRemoveFont(CCObject*) {
    // empty tag removes the current font.
    if (m_onSelect) m_onSelect("");

    if (m_previewFontSprite) {
        m_previewFontSprite->removeFromParent();
        m_previewFontSprite = nullptr;
    }
    m_previewLabel->setAnchorPoint({0.5f, 0.5f});
    auto pvSize = m_previewContainer->getContentSize();
    m_previewLabel->setPosition({pvSize.width / 2.f, pvSize.height / 2.f});
    m_previewLabel->setString("Default font");
}

bool FontPickerPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    auto loc = touch->getLocation();
    auto local = m_mainLayer->convertToNodeSpace(loc);
    auto size = m_mainLayer->getContentSize();
    m_touchHitOutside = !CCRect(0, 0, size.width, size.height).containsPoint(local);
    return true;
}

void FontPickerPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (m_touchHitOutside) {
        auto loc = touch->getLocation();
        auto local = m_mainLayer->convertToNodeSpace(loc);
        auto size = m_mainLayer->getContentSize();
        if (!CCRect(0, 0, size.width, size.height).containsPoint(local)) {
            onClose(nullptr);
            return;
        }
    }
}

bool FontPickerPopup::isInsideVisibleScroll(CCNode* item) {
    if (!m_scroll || !item) return false;
    auto scrollWorld = m_scroll->convertToWorldSpace({0, 0});
    auto scrollSize = m_scroll->getContentSize();
    auto itemWorld = item->getParent()->convertToWorldSpace(item->getPosition());
    float half = CELL_SIZE * 0.5f;
    return itemWorld.x + half >= scrollWorld.x && itemWorld.x - half <= scrollWorld.x + scrollSize.width
        && itemWorld.y + half >= scrollWorld.y && itemWorld.y - half <= scrollWorld.y + scrollSize.height;
}

void FontPickerPopup::positionBelow(CCNode* anchor, float gap) {
    (void)anchor;
    auto winSize = CCDirector::get()->getWinSize();
    float halfH = POPUP_H * 0.5f;
    float y = std::clamp(halfH + gap, halfH, winSize.height - halfH);
    m_mainLayer->setPosition({winSize.width * 0.5f, y});
}

void FontPickerPopup::closeAnimated() {
    onClose(nullptr);
}
