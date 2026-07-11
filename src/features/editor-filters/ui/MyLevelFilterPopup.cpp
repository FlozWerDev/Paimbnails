#include "MyLevelFilterPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/MyLevelFilters.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/GJSearchObject.hpp>

using namespace geode::prelude;

namespace paimon::editorfilters {

namespace {
    constexpr float kPopupW = 340.f;
    constexpr float kPopupH = 200.f;

    bool* boolForTag(int tag) {
        auto& f = state();
        switch (tag) {
            case 1: return &f.tiny;
            case 2: return &f.shortLen;
            case 3: return &f.medium;
            case 4: return &f.longLen;
            case 5: return &f.xl;
            case 6: return &f.verified;
            case 7: return &f.unverified;
            default: return nullptr;
        }
    }

    CCScale9Sprite* makePanel(CCSize size) {
        auto panel = CCScale9Sprite::create("square02b_001.png");
        panel->setContentSize(size);
        panel->setColor({0, 0, 0});
        panel->setOpacity(70);
        return panel;
    }

    CCSprite* safeFrameSprite(char const* frame) {
        if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame))
            return nullptr;
        return CCSprite::createWithSpriteFrameName(frame);
    }

    CCNode* makeHeader(char const* iconFrame, char const* text) {
        auto node = CCNode::create();
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.45f);

        float gap = 4.f;
        float iconW = 0.f;
        CCSprite* icon = safeFrameSprite(iconFrame);
        if (icon) {
            icon->setScale(16.f / std::max(icon->getContentSize().height, 1.f));
            iconW = icon->getScaledContentSize().width + gap;
        }

        float labelW = label->getScaledContentSize().width;
        node->setContentSize({iconW + labelW, 20.f});
        node->setAnchorPoint({0.5f, 0.5f});

        if (icon) {
            icon->setPosition({icon->getScaledContentSize().width / 2.f, 10.f});
            node->addChild(icon);
        }
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({iconW, 10.f});
        node->addChild(label);
        return node;
    }
}

CCMenuItemToggler* MyLevelFilterPopup::makeToggler(char const* text, int tag, bool on, float scale) {
    auto labelOff = CCLabelBMFont::create(text, "bigFont.fnt");
    auto labelOn = CCLabelBMFont::create(text, "bigFont.fnt");
    labelOff->setColor({110, 110, 110});
    labelOff->setScale(scale);
    labelOn->setColor({0, 255, 127});
    labelOn->setScale(scale);

    auto toggler = CCMenuItemToggler::create(
        labelOff, labelOn, this, menu_selector(MyLevelFilterPopup::onToggle));
    toggler->setTag(tag);
    toggler->toggle(on);
    m_togglers.push_back(toggler);
    return toggler;
}

bool MyLevelFilterPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Filter My Levels");

    auto size = m_mainLayer->getContentSize();
    float cx = size.width / 2.f;
    auto& f = state();

    // decorative GD-style corners
    if (auto cornerL = safeFrameSprite("dailyLevelCorner_001.png")) {
        cornerL->setAnchorPoint({0.f, 0.f});
        cornerL->setPosition({1.5f, 1.5f});
        m_mainLayer->addChild(cornerL);
    }
    if (auto cornerR = safeFrameSprite("dailyLevelCorner_001.png")) {
        cornerR->setFlipX(true);
        cornerR->setAnchorPoint({1.f, 0.f});
        cornerR->setPosition({size.width - 1.5f, 1.5f});
        m_mainLayer->addChild(cornerR);
    }

    // --- Length section ---
    auto lengthPanel = makePanel({312.f, 56.f});
    lengthPanel->setPosition({cx, 122.f});
    m_mainLayer->addChild(lengthPanel);

    auto lengthHeader = makeHeader("GJ_timeIcon_001.png", "Length");
    lengthHeader->setPosition({cx, 150.f});
    m_mainLayer->addChild(lengthHeader);

    auto lengthMenu = CCMenu::create();
    lengthMenu->setContentSize({296.f, 26.f});
    lengthMenu->setAnchorPoint({0.5f, 0.5f});
    lengthMenu->ignoreAnchorPointForPosition(false);
    lengthMenu->setLayout(RowLayout::create()->setGap(10.f));
    lengthMenu->addChild(makeToggler("Tiny",   1, f.tiny));
    lengthMenu->addChild(makeToggler("Short",  2, f.shortLen));
    lengthMenu->addChild(makeToggler("Medium", 3, f.medium));
    lengthMenu->addChild(makeToggler("Long",   4, f.longLen));
    lengthMenu->addChild(makeToggler("XL",     5, f.xl));
    lengthMenu->setPosition({cx, 116.f});
    lengthMenu->updateLayout();
    m_mainLayer->addChild(lengthMenu);

    // --- Status section (left) ---
    auto statusPanel = makePanel({154.f, 58.f});
    statusPanel->setPosition({cx - 79.f, 52.f});
    m_mainLayer->addChild(statusPanel);

    auto statusHeader = makeHeader("GJ_completesIcon_001.png", "Status");
    statusHeader->setPosition({cx - 79.f, 81.f});
    m_mainLayer->addChild(statusHeader);

    auto statusMenu = CCMenu::create();
    statusMenu->setContentSize({140.f, 44.f});
    statusMenu->setAnchorPoint({0.5f, 0.5f});
    statusMenu->ignoreAnchorPointForPosition(false);
    statusMenu->setLayout(ColumnLayout::create()->setGap(4.f)->setAxisReverse(true));
    statusMenu->addChild(makeToggler("Verified",   6, f.verified,   0.5f));
    statusMenu->addChild(makeToggler("Unverified", 7, f.unverified, 0.5f));
    statusMenu->setPosition({cx - 79.f, 47.f});
    statusMenu->updateLayout();
    m_mainLayer->addChild(statusMenu);

    // --- Song section (right) ---
    auto songPanel = makePanel({150.f, 58.f});
    songPanel->setPosition({cx + 81.f, 52.f});
    m_mainLayer->addChild(songPanel);

    auto songHeader = makeHeader("GJ_musicIcon_001.png", "Song ID");
    songHeader->setPosition({cx + 81.f, 81.f});
    m_mainLayer->addChild(songHeader);

    m_songInput = TextInput::create(120.f, "Song ID");
    m_songInput->setFilter("0123456789");
    m_songInput->setString(f.songID);
    m_songInput->setCallback([](std::string const& text) {
        state().songID = text;
    });
    m_songInput->setPosition({cx + 81.f, 47.f});
    m_mainLayer->addChild(m_songInput);

    // --- Trash (clear filters) ---
    auto trashSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
    trashSpr->setScale(0.65f);
    auto trashBtn = CCMenuItemSpriteExtra::create(
        trashSpr, this, menu_selector(MyLevelFilterPopup::onTrash));
    auto trashMenu = CCMenu::create();
    trashMenu->addChild(trashBtn);
    trashMenu->setPosition({size.width - 22.f, size.height - 22.f});
    m_mainLayer->addChild(trashMenu);

    return true;
}

void MyLevelFilterPopup::onToggle(CCObject* sender) {
    auto toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;
    if (auto* target = boolForTag(toggler->getTag())) {
        *target = !*target;
    }
}

void MyLevelFilterPopup::onTrash(CCObject*) {
    reset();
    for (auto* t : m_togglers) {
        if (t) t->toggle(false);
    }
    if (m_songInput) m_songInput->setString("");
}

void MyLevelFilterPopup::reloadBrowser() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;
    auto* browser = scene->getChildByType<LevelBrowserLayer>(0);
    if (!browser || !browser->m_searchObject) return;
    browser->m_searchObject->m_page = 0;
    browser->loadPage(browser->m_searchObject);
}

void MyLevelFilterPopup::onClose(CCObject* sender) {
    Popup::onClose(sender);
    reloadBrowser();
}

MyLevelFilterPopup* MyLevelFilterPopup::create() {
    auto ret = new MyLevelFilterPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

} // namespace paimon::editorfilters
