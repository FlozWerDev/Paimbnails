#include "UnregisteredProfilePopup.hpp"
#include "../services/InfoStore.hpp"
#include "../services/SearchObjectBuilder.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/general.hpp>

using namespace geode::prelude;

namespace paimon::info {

namespace {

constexpr float kPopupW = 320.f;
constexpr float kPopupH = 200.f;

} // namespace

UnregisteredProfilePopup* UnregisteredProfilePopup::create(int userID, std::string userName) {
    auto ret = new UnregisteredProfilePopup();
    if (ret && ret->init(userID, std::move(userName))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool UnregisteredProfilePopup::init(int userID, std::string userName) {
    if (userID <= 0) return false;
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_userID = userID;
    m_userName = std::move(userName);

    // Fall back to whatever name we saw for this id before.
    if (m_userName.empty() || m_userName == "-") {
        m_userName = InfoStore::get().username(userID);
    }
    if (m_userName.empty()) m_userName = "Jugador sin cuenta";

    this->setTitle(m_userName);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    auto note = CCLabelBMFont::create(
        "Este jugador nunca registro una cuenta,\n"
        "asi que no tiene perfil, ni iconos, ni stats.",
        "chatFont.fnt");
    note->setAlignment(kCCTextAlignmentCenter);
    note->setScale(0.48f);
    note->setColor({170, 170, 170});
    note->setPosition({cx, content.height - 62.f});
    m_mainLayer->addChild(note);

    auto idLabel = CCLabelBMFont::create(fmt::format("User ID  {}", m_userID).c_str(),
                                         "goldFont.fnt");
    idLabel->setScale(0.6f);
    idLabel->setPosition({cx, content.height - 100.f});
    m_mainLayer->addChild(idLabel);

    auto menu = CCMenu::create();
    menu->setPosition({cx, 40.f});
    menu->setContentSize({kPopupW - 40.f, 34.f});
    menu->setLayout(RowLayout::create()->setGap(10.f)->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(menu);

    auto copySpr = ButtonSprite::create("Copiar ID", "bigFont.fnt", "GJ_button_04.png", 0.6f);
    menu->addChild(CCMenuItemSpriteExtra::create(
        copySpr, this, menu_selector(UnregisteredProfilePopup::onCopyID)));

    auto levelsSpr = ButtonSprite::create("Sus niveles", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    menu->addChild(CCMenuItemSpriteExtra::create(
        levelsSpr, this, menu_selector(UnregisteredProfilePopup::onLevels)));

    menu->updateLayout();
    return true;
}

void UnregisteredProfilePopup::onLevels(CCObject*) {
    int userID = m_userID;
    this->onClose(nullptr);

    // UsersLevels takes the plain user id as its query, the same call the game
    // makes when you tap a creator's name.
    SearchFilters filters;
    filters.query = std::to_string(userID);
    pushBrowser(buildSearchObject(SearchType::UsersLevels, filters));
}

void UnregisteredProfilePopup::onCopyID(CCObject*) {
    clipboard::write(std::to_string(m_userID));
    PaimonNotify::create("User ID copiado", NotificationIcon::Success)->show();
}

} // namespace paimon::info
