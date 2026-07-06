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
    constexpr float kPopupW = 360.f;
    constexpr float kPopupH = 230.f;

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

    CCMenu* makeRowMenu(float width) {
        auto menu = CCMenu::create();
        menu->setContentSize({width, 30.f});
        menu->setAnchorPoint({0.5f, 0.5f});
        menu->ignoreAnchorPointForPosition(false);
        menu->setLayout(RowLayout::create()->setGap(14.f));
        return menu;
    }
}

CCMenuItemToggler* MyLevelFilterPopup::makeToggler(char const* text, int tag, bool on) {
    auto labelOff = CCLabelBMFont::create(text, "bigFont.fnt");
    auto labelOn = CCLabelBMFont::create(text, "bigFont.fnt");
    labelOff->setColor({125, 125, 125});
    labelOff->setScale(0.6f);
    labelOn->setScale(0.6f);

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
    float cy = size.height / 2.f;
    auto& f = state();

    auto lengthLabel = CCLabelBMFont::create("Length", "goldFont.fnt");
    lengthLabel->setScale(0.5f);
    lengthLabel->setPosition({cx, cy + 56.f});
    m_mainLayer->addChild(lengthLabel);

    auto lengthMenu = makeRowMenu(330.f);
    lengthMenu->addChild(makeToggler("Tiny",   1, f.tiny));
    lengthMenu->addChild(makeToggler("Short",  2, f.shortLen));
    lengthMenu->addChild(makeToggler("Medium", 3, f.medium));
    lengthMenu->addChild(makeToggler("Long",   4, f.longLen));
    lengthMenu->addChild(makeToggler("XL",     5, f.xl));
    lengthMenu->setPosition({cx, cy + 34.f});
    lengthMenu->updateLayout();
    m_mainLayer->addChild(lengthMenu);

    auto verifyLabel = CCLabelBMFont::create("Status", "goldFont.fnt");
    verifyLabel->setScale(0.5f);
    verifyLabel->setPosition({cx, cy + 8.f});
    m_mainLayer->addChild(verifyLabel);

    auto verifyMenu = makeRowMenu(240.f);
    verifyMenu->addChild(makeToggler("Verified",   6, f.verified));
    verifyMenu->addChild(makeToggler("Unverified", 7, f.unverified));
    verifyMenu->setPosition({cx, cy - 14.f});
    verifyMenu->updateLayout();
    m_mainLayer->addChild(verifyMenu);

    m_songInput = TextInput::create(180.f, "Song ID");
    m_songInput->setFilter("0123456789");
    m_songInput->setString(f.songID);
    m_songInput->setCallback([](std::string const& text) {
        state().songID = text;
    });
    m_songInput->setPosition({cx, cy - 50.f});
    m_mainLayer->addChild(m_songInput);

    auto trashSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
    trashSpr->setScale(0.7f);
    auto trashBtn = CCMenuItemSpriteExtra::create(
        trashSpr, this, menu_selector(MyLevelFilterPopup::onTrash));
    auto trashMenu = CCMenu::create();
    trashMenu->addChild(trashBtn);
    trashMenu->setPosition({size.width - 24.f, size.height - 24.f});
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
