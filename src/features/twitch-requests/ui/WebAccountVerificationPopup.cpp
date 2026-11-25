#include "WebAccountVerificationPopup.hpp"

#include "../services/WebAccountVerification.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/AccountVerifier.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/ProfilePage.hpp>
#include <Geode/ui/GeodeUI.hpp>

using namespace geode::prelude;

namespace paimon::twitch {
namespace {

CCMenuItemSpriteExtra* addButton(CCMenu* menu, char const* title, CCPoint position,
    std::function<void()> action, paimon::ui::Btn skin = paimon::ui::Btn::Cyan, float scale = .48f) {
    auto* sprite = paimon::ui::makeButtonSprite(title, skin, 0.f, scale, "bigFont.fnt");
    if (!sprite) return nullptr;
    auto* button = CCMenuItemExt::createSpriteExtra(sprite,
        [action = std::move(action)](CCMenuItemSpriteExtra*) { action(); });
    button->setPosition(position);
    menu->addChild(button);
    return button;
}

void setEnabled(CCMenuItemSpriteExtra* button, bool enabled) {
    if (!button) return;
    button->setEnabled(enabled);
    button->setOpacity(enabled ? 255 : 110);
}

} // namespace

WebAccountVerificationPopup* WebAccountVerificationPopup::create() {
    auto* popup = new WebAccountVerificationPopup;
    if (popup->init()) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

WebAccountVerificationPopup::~WebAccountVerificationPopup() {
    if (m_flow) m_flow->cancel();
}

bool WebAccountVerificationPopup::init() {
    m_flow = createWebAccountVerification();
    namespace ui = paimon::ui;
    constexpr float W = 380.f, H = 250.f, cx = W / 2.f;
    if (!m_flow || !PaimonPopup::init(W, H)) return false;
    setTitle("Verificar cuenta de GD", "goldFont.fnt", .62f, 19.f);
    addCorners();
    addInfoButton("Como funciona la verificacion", webAccountVerificationInfo(),
        Anchor::TopRight, {-16.f, -16.f});
    paimon::markDynamicPopup(this);

    auto* divider = ui::makeDivider(320.f);
    divider->setPosition({cx, H - 34.f});
    m_mainLayer->addChild(divider);

    // identity pill: gold name + cyan account id
    auto account = AccountVerifier::get().verify();
    auto* name = ui::makeLabel(account.username.c_str(), 150.f, .42f, ui::palette::gold);
    auto idText = fmt::format("#{}", account.accountID);
    auto* id = ui::makeLabel(idText.c_str(), 90.f, .34f, ui::palette::info);
    float const nameW = name->getScaledContentWidth(), idW = id->getScaledContentWidth();
    float const pillW = std::max(130.f, nameW + idW + 34.f);
    auto* pill = ui::makeInset({pillW, 22.f}, 110);
    pill->setPosition({cx - pillW / 2.f, 181.f});
    m_mainLayer->addChild(pill);
    float const startX = cx - (nameW + 8.f + idW) / 2.f;
    name->setAnchorPoint({0.f, .5f});
    name->setPosition({startX, 192.f});
    id->setAnchorPoint({0.f, .5f});
    id->setPosition({startX + nameW + 8.f, 191.f});
    m_mainLayer->addChild(name);
    m_mainLayer->addChild(id);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    m_mainLayer->addChild(menu, 5);

    // two mode cards, each tinted like its button
    auto addCard = [&](float x, ccColor3B tint, char const* hint) {
        auto* card = ui::makeInset({170.f, 58.f}, 90, tint);
        card->setPosition({x, 118.f});
        m_mainLayer->addChild(card);
        auto* text = ui::makeText(hint, 152.f, .32f, ui::palette::muted, kCCTextAlignmentCenter);
        text->setPosition({x + 85.f, 133.f});
        m_mainLayer->addChild(text);
    };
    addCard(14.f, {20, 70, 25}, "Publica y borra el codigo por ti.");
    addCard(196.f, {15, 60, 75}, "Tu publicas y borras el codigo.");
    m_automatic = addButton(menu, "Automatico", {99.f, 156.f}, [this] { onAutomatic(); }, ui::Btn::Green, .5f);
    m_manual = addButton(menu, "Manual", {281.f, 156.f}, [this] { m_flow->start(VerificationMode::Manual); refresh(); }, ui::Btn::Cyan, .5f);

    auto* inset = ui::makeInset({352.f, 50.f}, 120);
    inset->setPosition({14.f, 62.f});
    m_mainLayer->addChild(inset);
    m_status = CCLabelBMFont::create("", "chatFont.fnt", 336.f / .4f, kCCTextAlignmentCenter);
    m_status->setScale(.4f);
    m_status->setPosition({cx, 87.f});
    m_mainLayer->addChild(m_status);
    m_code = CCLabelBMFont::create("", "goldFont.fnt");
    m_code->setScale(.5f);
    m_code->setPosition({cx, 72.f});
    m_mainLayer->addChild(m_code);

    m_copy = addButton(menu, "Copiar codigo", {80.f, 40.f}, [this] {
        PlatformToolbox::copyToClipboard(m_flow->code());
        PaimonNotify::show("Codigo copiado. Publicalo en tu perfil de GD.", NotificationIcon::Info);
    }, ui::Btn::Gray, .4f);
    addButton(menu, "Mi perfil", {cx, 40.f}, [accountID = account.accountID] {
        if (auto* profile = ProfilePage::create(accountID, true)) profile->show();
    }, ui::Btn::Gray, .4f);
    m_check = addButton(menu, "Comprobar", {300.f, 40.f}, [this] { m_flow->check(); refresh(); }, ui::Btn::Green, .4f);
    m_cleanup = addButton(menu, "Borrar temporal", {125.f, 17.f}, [this] { m_flow->retryCleanup(); refresh(); }, ui::Btn::Cyan, .36f);
    m_forget = addButton(menu, "Ya lo borre", {255.f, 17.f}, [this] { onForgetComment(); }, ui::Btn::Gray, .36f);
    refresh();
    scheduleUpdate();
    return true;
}

void WebAccountVerificationPopup::onAutomatic() {
    if (m_flow->busy() || m_flow->commentID() > 0) return;
    WeakRef<WebAccountVerificationPopup> self = this;
    createQuickPopup("Autorizar verificacion automatica",
        "Al pulsar <cg>Autorizar</c>, el mod publicara un codigo temporal en <cy>tu perfil de GD</c>, "
        "pedira al servidor que lo compruebe y despues borrara solo ese comentario.\n\n"
        "La credencial de GD solo se envia a <cy>Geometry Dash</c>. "
        "Puedes elegir <cj>Manual</c> para publicar y borrar tu mismo.",
        "Cancelar", "Autorizar", [self](auto*, bool confirmed) {
            if (auto popup = self.lock(); popup && popup->getParent() && confirmed) {
                popup->m_flow->start(VerificationMode::Automatic);
                popup->refresh();
            }
        });
}

void WebAccountVerificationPopup::onForgetComment() {
    WeakRef<WebAccountVerificationPopup> self = this;
    createQuickPopup("Comentario eliminado manualmente",
        "Confirma que ya borraste el comentario temporal desde tu perfil. "
        "Esto solo olvidara su registro local; no borra ningun comentario en GD.",
        "Cancelar", "Confirmar", [self](auto*, bool confirmed) {
            if (auto popup = self.lock(); popup && popup->getParent() && confirmed) {
                popup->m_flow->acknowledgeManualCleanup();
                popup->refresh();
            }
        });
}

void WebAccountVerificationPopup::refresh() {
    if (m_lastStatus != m_flow->status()) {
        m_lastStatus = m_flow->status();
        m_status->setString(m_lastStatus.c_str());
    }
    if (m_lastCode != m_flow->code()) {
        m_lastCode = m_flow->code();
        m_code->setString(m_lastCode.c_str());
        m_status->setPositionY(m_lastCode.empty() ? 87.f : 94.f);
    }
    bool const pendingCleanup = m_flow->commentID() > 0;
    setEnabled(m_automatic, !m_flow->busy() && !pendingCleanup);
    setEnabled(m_manual, !m_flow->busy() && !pendingCleanup);
    setEnabled(m_copy, !m_flow->code().empty());
    setEnabled(m_check, m_flow->stage() == VerificationStage::Waiting && !m_flow->busy());
    if (m_cleanup) { m_cleanup->setVisible(pendingCleanup); setEnabled(m_cleanup, !m_flow->busy()); }
    if (m_forget) { m_forget->setVisible(pendingCleanup); setEnabled(m_forget, !m_flow->busy()); }
}

void WebAccountVerificationPopup::update(float) { refresh(); }

void WebAccountVerificationPopup::onClose(CCObject* sender) {
    m_flow->cancel();
    PaimonPopup::onClose(sender);
}

} // namespace paimon::twitch
