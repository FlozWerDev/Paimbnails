#include "MyLevelFilterPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/MyLevelFilters.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/GJSearchObject.hpp>

using namespace geode::prelude;
namespace ui = paimon::ui;

namespace paimon::editorfilters {

namespace {
    constexpr float kPopupW = 360.f;
    constexpr float kPopupH = 230.f;

    bool* boolForTag(int tag) {
        static bool FilterState::* const kByTag[] = {
            nullptr,
            &FilterState::tiny,
            &FilterState::shortLen,
            &FilterState::medium,
            &FilterState::longLen,
            &FilterState::xl,
            &FilterState::verified,
            &FilterState::unverified,
        };
        if (tag < 1 || tag > 7) return nullptr;
        return &(state().*kByTag[tag]);
    }
}

// GD checkbox with its label to the right; the toggler keeps the tag/selector
// the rest of the popup relies on.
CCMenuItemToggler* MyLevelFilterPopup::makeToggler(char const* text, int tag, bool on, float scale) {
    auto* off = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    auto* onSpr = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    auto* toggler = CCMenuItemToggler::create(
        off, onSpr, this, menu_selector(MyLevelFilterPopup::onToggle));
    toggler->setScale(0.6f);
    toggler->setTag(tag);
    toggler->toggle(on);

    auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setScale(scale);
    label->limitLabelWidth(70.f, scale, 0.2f);
    label->setPosition({14.f, 0.f});
    toggler->addChild(label);

    m_togglers.push_back(toggler);
    return toggler;
}

bool MyLevelFilterPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Filter My Levels");
    addInfoButton("Filtrar mis niveles",
        "Marca una o varias casillas de <cy>Length</c> para ver solo niveles de "
        "esas duraciones, y en <cy>Status</c> si estan verificados. Escribe un "
        "<cg>Song ID</c> para quedarte con los que usan esa cancion. El boton de "
        "la papelera limpia todos los filtros.");
    addCorners();

    auto size = m_mainLayer->getContentSize();
    float const cx = size.width / 2.f;
    auto& f = state();

    float const lengthW = size.width - 24.f;
    auto* lengthPanel = ui::makePanel({lengthW, 60.f}, "Length");
    lengthPanel->setPosition({12.f, size.height - 42.f - 60.f});
    m_mainLayer->addChild(lengthPanel);

    auto* lengthMenu = CCMenu::create();
    lengthMenu->setContentSize({lengthW - 16.f, 24.f});
    lengthMenu->setAnchorPoint({0.5f, 0.5f});
    lengthMenu->ignoreAnchorPointForPosition(false);
    lengthMenu->setLayout(RowLayout::create()->setGap(2.f));
    lengthMenu->addChild(makeToggler("Tiny",   1, f.tiny,     0.42f));
    lengthMenu->addChild(makeToggler("Short",  2, f.shortLen, 0.42f));
    lengthMenu->addChild(makeToggler("Medium", 3, f.medium,   0.42f));
    lengthMenu->addChild(makeToggler("Long",   4, f.longLen,  0.42f));
    lengthMenu->addChild(makeToggler("XL",     5, f.xl,       0.42f));
    lengthMenu->setPosition({lengthW / 2.f + 12.f, size.height - 42.f - 60.f + 18.f});
    lengthMenu->updateLayout();
    m_mainLayer->addChild(lengthMenu);

    float const halfW = (size.width - 32.f) / 2.f;
    float const lowRowY = 44.f;

    auto* statusPanel = ui::makePanel({halfW, 74.f}, "Status");
    statusPanel->setPosition({12.f, lowRowY});
    m_mainLayer->addChild(statusPanel);

    auto* statusMenu = CCMenu::create();
    statusMenu->setContentSize({halfW - 16.f, 44.f});
    statusMenu->setAnchorPoint({0.5f, 0.5f});
    statusMenu->ignoreAnchorPointForPosition(false);
    statusMenu->setLayout(ColumnLayout::create()->setGap(6.f)->setAxisReverse(true)
        ->setCrossAxisLineAlignment(AxisAlignment::Start));
    statusMenu->addChild(makeToggler("Verified",   6, f.verified,   0.46f));
    statusMenu->addChild(makeToggler("Unverified", 7, f.unverified, 0.46f));
    statusMenu->setPosition({12.f + halfW / 2.f, lowRowY + 24.f});
    statusMenu->updateLayout();
    m_mainLayer->addChild(statusMenu);

    auto* songPanel = ui::makePanel({halfW, 74.f}, "Song ID");
    songPanel->setPosition({size.width - 12.f - halfW, lowRowY});
    m_mainLayer->addChild(songPanel);

    m_songInput = TextInput::create(halfW - 24.f, "Song ID");
    m_songInput->setFilter("0123456789");
    m_songInput->setString(f.songID);
    m_songInput->setCallback([](std::string const& text) {
        state().songID = text;
    });
    m_songInput->setPosition({size.width - 12.f - halfW / 2.f, lowRowY + 26.f});
    m_mainLayer->addChild(m_songInput);

    auto trashSpr = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
    if (!trashSpr) return true;
    trashSpr->setScale(0.65f);
    auto trashBtn = CCMenuItemSpriteExtra::create(
        trashSpr, this, menu_selector(MyLevelFilterPopup::onTrash));
    auto trashMenu = CCMenu::create();
    trashMenu->addChild(trashBtn);
    trashMenu->setPosition({cx, 20.f});
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
