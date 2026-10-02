#include "UnregisteredProfilePopup.hpp"
#include "../services/InfoStore.hpp"
#include "../services/SearchObjectBuilder.hpp"
#include "../../../ui/PaimonUI.hpp"
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
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_userID = userID;
    m_userName = std::move(userName);

    // fall back to whatever name we saw for this id before.
    if (m_userName.empty() || m_userName == "-") {
        m_userName = InfoStore::get().username(userID);
    }
    if (m_userName.empty()) m_userName = "Jugador sin cuenta";

    this->setTitle(m_userName);
    this->addInfoButton("Jugador sin cuenta",
        "Este User ID aparece en servidores pero <cr>nunca registro una cuenta</c>. "
        "Puedes <cg>copiar su ID</c> o ver <cg>sus niveles</c> subidos.");

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    CCSize const panelSize{content.width - 40.f, 92.f};
    auto panel = paimon::ui::makeInset(panelSize);
    panel->setPosition({cx - panelSize.width / 2.f, 54.f});
    m_mainLayer->addChild(panel);

    float const panelCx = panelSize.width / 2.f;

    auto note = paimon::ui::makeText(
        "Este jugador nunca registro una cuenta,\n"
        "asi que no tiene perfil, ni iconos, ni stats.",
        panelSize.width - 24.f, 0.48f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
    note->setPosition({panelCx, panelSize.height - 26.f});
    panel->addChild(note);

    auto idLabel = paimon::ui::makeTitle(fmt::format("User ID  {}", m_userID).c_str(),
        panelSize.width - 24.f, 0.6f);
    idLabel->setPosition({panelCx, 22.f});
    panel->addChild(idLabel);

    auto menu = CCMenu::create();
    menu->setPosition({cx, 32.f});
    menu->setContentSize({kPopupW - 40.f, 34.f});
    menu->setLayout(RowLayout::create()->setGap(10.f)->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(menu);

    menu->addChild(paimon::ui::makeButton("Copiar ID",
        [this] { this->onCopyID(nullptr); }, paimon::ui::Btn::Gray, 0.f, 0.6f, "bigFont.fnt"));

    menu->addChild(paimon::ui::makeButton("Sus niveles",
        [this] { this->onLevels(nullptr); }, paimon::ui::Btn::Green, 0.f, 0.7f));

    menu->updateLayout();
    return true;
}

void UnregisteredProfilePopup::onLevels(CCObject*) {
    int userID = m_userID;
    this->onClose(nullptr);

    // userslevels takes the plain user id as its query, the same call the game
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
