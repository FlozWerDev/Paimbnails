#include "FontPickerPopup.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::fonts;

// ── Layout constants (improved, larger cells + cleaner layout) ────
static constexpr float POPUP_W   = 360.f;
static constexpr float POPUP_H   = 218.f;
static constexpr float CORNER_R  = 12.f;
static constexpr float PAD       = 8.f;
static constexpr float PREVIEW_H = 36.f;
static constexpr float SIDEBAR_W = 76.f;
static constexpr float CELL_SIZE = 44.f;
static constexpr float CELL_GAP  = 4.f;
static constexpr float TAB_H     = 20.f;

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

// ── Factory ─────────────────────────────────────────
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

// ── Init ────────────────────────────────────────────
bool FontPickerPopup::init(
        CopyableFunction<void(std::string const&)> onSelect) {
    m_onSelect = std::move(onSelect);

    if (!Popup::init(POPUP_W, POPUP_H))
        return false;

    // No close button — dismiss by clicking outside
    if (m_closeBtn) m_closeBtn->setVisible(false);

    // Replace default bg with custom dark rounded rect
    if (m_bgSprite) m_bgSprite->setVisible(false);

    auto bg = paimon::SpriteHelper::createRoundedRect(
        POPUP_W, POPUP_H, CORNER_R,
        {0.08f, 0.08f, 0.14f, 0.97f});
    bg->setPosition({0.f, 0.f});
    m_mainLayer->addChild(bg, -1);

    // ══════════ Preview section (top, compact) ══════════
    float pvW = POPUP_W - PAD * 2;
    float pvY = POPUP_H - PAD - PREVIEW_H;

    auto pvBg = paimon::SpriteHelper::createRoundedRect(
        pvW, PREVIEW_H, 8.f, {0.13f, 0.13f, 0.20f, 1.f});
    pvBg->setPosition({PAD, pvY});
    m_mainLayer->addChild(pvBg, 1);

    m_previewContainer = CCNode::create();
    m_previewContainer->setContentSize({pvW, PREVIEW_H});
    m_previewContainer->setPosition({PAD, pvY});
    m_mainLayer->addChild(m_previewContainer, 2);

    m_previewLabel = CCLabelBMFont::create("Pick a font!", "chatFont.fnt");
    m_previewLabel->setScale(0.32f);
    m_previewLabel->setPosition({pvW / 2.f, PREVIEW_H / 2.f});
    m_previewLabel->setColor({190, 190, 200});
    m_previewContainer->addChild(m_previewLabel);

    // ══════════ Bottom section (rounded rect) ══════════
    float botH = pvY - PAD;
    float botY = PAD;
    float botW = POPUP_W - PAD * 2;

    auto botBg = paimon::SpriteHelper::createRoundedRect(
        botW, botH, 8.f, {0.11f, 0.11f, 0.18f, 1.f});
    botBg->setPosition({PAD, botY});
    m_mainLayer->addChild(botBg, 1);

    // ── Left sidebar: tab buttons + quick-picks ──
    float sideX = PAD + 4;

    m_sideMenu = CCMenu::create();
    m_sideMenu->setPosition({0, 0});
    m_mainLayer->addChild(m_sideMenu, 3);

    auto makeTabBtn = [&](const char* text, SEL_MenuHandler sel,
                          float yOff) -> CCMenuItemSpriteExtra* {
        float bw = SIDEBAR_W - 8;
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.28f);
        auto container = CCNode::create();
        container->setContentSize({bw, TAB_H});
        lbl->setPosition({bw / 2, TAB_H / 2});
        container->addChild(lbl, 1);
        auto btn = CCMenuItemSpriteExtra::create(container, this, sel);
        btn->setPosition({sideX + SIDEBAR_W / 2, botY + botH - yOff});
        return btn;
    };

    m_tabGD = makeTabBtn("GD Fonts",
        menu_selector(FontPickerPopup::onTabGD), TAB_H / 2 + 4);
    m_sideMenu->addChild(m_tabGD);

    m_tabCustom = makeTabBtn("Custom",
        menu_selector(FontPickerPopup::onTabCustom), TAB_H * 1.5f + 8);
    m_sideMenu->addChild(m_tabCustom);

    // ── Quick-pick separator ──
    float qpLabelY = botY + botH - TAB_H * 2 - 18.f;

    auto divLine = paimon::SpriteHelper::createRoundedRect(
        SIDEBAR_W - 16, 1.f, 0.5f, {0.3f, 0.3f, 0.4f, 0.4f});
    divLine->setPosition({sideX + 4, qpLabelY + 8});
    m_mainLayer->addChild(divLine, 3);

    auto qpLabel = CCLabelBMFont::create("Quick Pick", "chatFont.fnt");
    qpLabel->setScale(0.22f);
    qpLabel->setColor({120, 120, 140});
    qpLabel->setPosition({sideX + SIDEBAR_W / 2, qpLabelY});
    m_mainLayer->addChild(qpLabel, 3);

    // ── Quick-pick buttons (Big / Chat / Gold) in sidebar ──
    struct QuickFont { const char* display; const char* fontFile; const char* fontId; };
    QuickFont quickFonts[] = {
        {"Big",  "bigFont.fnt",  "big"},
        {"Chat", "chatFont.fnt", "chat"},
        {"Gold", "goldFont.fnt", "gold"},
    };

    float qpBtnH = 18.f;
    float qpStartY = qpLabelY - 14.f;
    CCMenuItemSpriteExtra* qpBtns[3] = {};

    for (int i = 0; i < 3; ++i) {
        float bw = SIDEBAR_W - 8;
        auto container = CCNode::create();
        container->setContentSize({bw, qpBtnH});

        auto cellBg = paimon::SpriteHelper::createRoundedRect(
            bw, qpBtnH, 5.f, {0.16f, 0.16f, 0.24f, 0.9f});
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
        btn->setPosition({sideX + SIDEBAR_W / 2, qpStartY - i * (qpBtnH + 3.f)});
        btn->setUserObject(CCString::create(quickFonts[i].fontId));
        m_sideMenu->addChild(btn);
        qpBtns[i] = btn;
    }
    m_qpBig  = qpBtns[0];
    m_qpChat = qpBtns[1];
    m_qpGold = qpBtns[2];

    // ── "None" button to remove font tag ──
    {
        float bw = SIDEBAR_W - 8;
        float noneY = qpStartY - 3 * (qpBtnH + 3.f);
        auto container = CCNode::create();
        container->setContentSize({bw, qpBtnH});

        auto cellBg = paimon::SpriteHelper::createRoundedRect(
            bw, qpBtnH, 5.f, {0.22f, 0.14f, 0.14f, 0.9f});
        cellBg->setPosition({0, 0});
        container->addChild(cellBg);

        auto lbl = CCLabelBMFont::create("None", "bigFont.fnt");
        if (lbl) {
            lbl->setScale(0.22f);
            lbl->setColor({200, 150, 150});
            lbl->setPosition({bw / 2, qpBtnH / 2});
            container->addChild(lbl, 1);
        }

        auto btn = CCMenuItemSpriteExtra::create(
            container, this, menu_selector(FontPickerPopup::onRemoveFont));
        btn->setPosition({sideX + SIDEBAR_W / 2, noneY});
        m_sideMenu->addChild(btn);
    }

    // ── Vertical divider ──
    float divX = PAD + SIDEBAR_W + 4;
    auto divider = paimon::SpriteHelper::createRoundedRect(
        1.5f, botH - 8.f, 1.f, {0.25f, 0.25f, 0.35f, 0.4f});
    divider->setPosition({divX, botY + 4.f});
    m_mainLayer->addChild(divider, 2);

    // ── Right side: font grid ──
    float gridX = divX + 6.f;
    float gridW = PAD + botW - gridX + PAD;
    float gridH = botH - 4.f;

    m_scroll = ScrollLayer::create({gridW, gridH});
    m_scroll->setPosition({gridX, botY + 2.f});
    m_mainLayer->addChild(m_scroll, 3);

    m_contentNode = CCNode::create();
    m_contentNode->setContentSize({gridW, gridH});
    m_scroll->m_contentLayer->addChild(m_contentNode);

    // ── Custom tab container (hidden, swaps into grid area) ──
    m_customContainer = CCNode::create();
    m_customContainer->setContentSize({gridW, gridH});
    m_customContainer->setPosition({gridX, botY + 2.f});
    m_customContainer->setVisible(false);
    m_mainLayer->addChild(m_customContainer, 4);

    // Custom tab: centered card layout
    float cardW = gridW - 16.f;
    float cardH = 84.f;
    float cardX = 8.f;
    float cardY = std::max(14.f, gridH - cardH - 12.f);

    auto cardBg = paimon::SpriteHelper::createRoundedRect(
        cardW, cardH, 8.f, {0.14f, 0.14f, 0.22f, 0.8f});
    cardBg->setPosition({cardX, cardY});
    m_customContainer->addChild(cardBg);

    auto customLabel = CCLabelBMFont::create("Enter font name:", "bigFont.fnt");
    customLabel->setScale(0.24f);
    customLabel->setColor({180, 180, 195});
    customLabel->setPosition({gridW / 2.f, cardY + cardH - 12.f});
    m_customContainer->addChild(customLabel);

    m_customInput = TextInput::create(cardW - 24.f, "myFont.fnt");
    m_customInput->setFilter("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
    m_customInput->setMaxCharCount(64);
    m_customInput->setScale(0.64f);
    m_customInput->setPosition({gridW / 2.f, cardY + cardH / 2.f + 1.f});
    m_customContainer->addChild(m_customInput);

    auto applyContainer = CCNode::create();
    float applyW = 78.f, applyH = 22.f;
    applyContainer->setContentSize({applyW, applyH});
    auto applyBg = paimon::SpriteHelper::createRoundedRect(
        applyW, applyH, 6.f, {0.28f, 0.26f, 0.45f, 1.f});
    applyBg->setPosition({0, 0});
    applyContainer->addChild(applyBg);
    auto applyLbl = CCLabelBMFont::create("Apply", "bigFont.fnt");
    applyLbl->setScale(0.28f);
    applyLbl->setPosition({applyW / 2, applyH / 2});
    applyContainer->addChild(applyLbl, 1);

    auto applyBtn = CCMenuItemSpriteExtra::create(
        applyContainer, this, menu_selector(FontPickerPopup::onCustomApply));
    applyBtn->setPosition({gridW / 2.f, cardY + 14.f});

    auto customMenu = CCMenu::create();
    customMenu->setPosition({0.f, 0.f});
    customMenu->setContentSize({gridW, gridH});
    m_customContainer->addChild(customMenu, 10);
    customMenu->addChild(applyBtn);

    auto hintLabel = CCLabelBMFont::create(
        ".fnt file in game resources", "chatFont.fnt");
    hintLabel->setScale(0.20f);
    hintLabel->setColor({110, 110, 125});
    hintLabel->setPosition({gridW / 2.f, cardY - 8.f});
    m_customContainer->addChild(hintLabel);

    // ── Build default tab ──
    updateTabHighlights();
    switchTab(Tab::GDFonts);

    return true;
}

// ── Tab switching ───────────────────────────────────
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
    auto setTabBg = [](CCMenuItemSpriteExtra* btn, bool active) {
        if (!btn) return;
        auto container = btn->getNormalImage();
        if (!container) return;
        if (auto old = container->getChildByTag(50))
            old->removeFromParent();
        float w = container->getContentSize().width;
        float h = container->getContentSize().height;
        ccColor4F col = active
            ? ccColor4F{0.28f, 0.26f, 0.45f, 1.f}
            : ccColor4F{0.15f, 0.15f, 0.22f, 0.7f};
        auto hl = paimon::SpriteHelper::createRoundedRect(w, h, 5.f, col);
        hl->setTag(50);
        hl->setPosition({0, 0});
        container->addChild(hl, -1);
    };
    setTabBg(m_tabGD,     m_activeTab == Tab::GDFonts);
    setTabBg(m_tabCustom, m_activeTab == Tab::Custom);
}

// ── GD Font Grid ────────────────────────────────────
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
    m_contentNode->addChild(menu);

    for (int i = 0; i < static_cast<int>(gdFonts.size()); ++i) {
        int col = i % cols;
        int row = i / cols;
        float x = col * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2;
        float y = totalH - (row * (CELL_SIZE + CELL_GAP) + CELL_SIZE / 2);

        auto const& [fontId, fontFile] = gdFonts[i];

        // Cell background with rounded corners
        auto cellBg = paimon::SpriteHelper::createRoundedRect(
            CELL_SIZE, CELL_SIZE, 6.f,
            {0.16f, 0.16f, 0.24f, 0.85f});

        auto container = CCNode::create();
        container->setContentSize({CELL_SIZE, CELL_SIZE});
        cellBg->setPosition({0, 0});
        container->addChild(cellBg);

        // Preview "Abc" in target font — bigger and centered
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

        // Font ID label at bottom — slightly larger for readability
        auto idLbl = CCLabelBMFont::create(fontId.c_str(), "chatFont.fnt");
        idLbl->setScale(0.22f);
        idLbl->setColor({140, 140, 155});
        idLbl->setPosition({CELL_SIZE / 2, 7.f});
        container->addChild(idLbl, 1);

        auto btn = CCMenuItemSpriteExtra::create(
            container, this,
            menu_selector(FontPickerPopup::onFontClicked));
        btn->setPosition({x, y});
        btn->setUserObject(CCString::create(fontId));
        menu->addChild(btn);
    }

    m_scroll->moveToTop();
}

// ── Preview ─────────────────────────────────────────
void FontPickerPopup::showPreview(
        std::string const& fontId, std::string const& fontFile) {
    if (m_previewFontSprite) {
        m_previewFontSprite->removeFromParent();
        m_previewFontSprite = nullptr;
    }

    auto pvSize = m_previewContainer->getContentSize();
    float pvW = pvSize.width;
    float pvH = pvSize.height;

    // Big preview text in target font
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

    // Update label with font name
    m_previewLabel->setString(fmt::format("Font: {}", fontId).c_str());
    m_previewLabel->setAnchorPoint({0.f, 0.5f});
    m_previewLabel->setPosition({pvH + 10.f, pvH / 2});
}

// ── Events ──────────────────────────────────────────
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
    // Signal with empty string so the callback strips the existing tag
    if (m_onSelect) m_onSelect("");

    // Reset preview
    if (m_previewFontSprite) {
        m_previewFontSprite->removeFromParent();
        m_previewFontSprite = nullptr;
    }
    m_previewLabel->setAnchorPoint({0.5f, 0.5f});
    auto pvSize = m_previewContainer->getContentSize();
    m_previewLabel->setPosition({pvSize.width / 2.f, pvSize.height / 2.f});
    m_previewLabel->setString("Default font");
}

// ── Touch handling ──────────────────────────────────
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
    return itemWorld.x >= scrollWorld.x && itemWorld.x <= scrollWorld.x + scrollSize.width
        && itemWorld.y >= scrollWorld.y && itemWorld.y <= scrollWorld.y + scrollSize.height;
}

// ── Positioning ─────────────────────────────────────
void FontPickerPopup::positionBelow(CCNode* anchor, float gap) {
    (void)anchor;
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float halfH = POPUP_H * 0.5f;
    float y = std::clamp(halfH + gap, halfH, winSize.height - halfH);
    m_mainLayer->setPosition({winSize.width * 0.5f, y});
}
