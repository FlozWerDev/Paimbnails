#include "BanUserPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/ui/LoadingSpinner.hpp>
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SafeText.hpp"
#include "../../../core/ModAuthFlow.hpp"
#include "../../../core/BanPolicy.hpp"
#include "../../emotes/ui/EmoteButton.hpp"
#include "../../emotes/ui/EmoteAutocomplete.hpp"
#include "../../../ui/PaimonUI.hpp"

using namespace geode::prelude;

BanUserPopup* BanUserPopup::create(std::string const& username) {
    auto ret = new BanUserPopup();
    if (ret && ret->init(username)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BanUserPopup::init(std::string const& username) {
    if (!PaimonPopup::init(300.f, 210.f)) return false;

    // keep the exact target: sanitizing an identifier could silently point the
    // action at a different account. Invalid names are rejected in onBan.
    m_username = username;
    this->setTitle(Localization::get().getString("ban.popup.title"));
    this->addInfoButton(Localization::get().getString("ban.popup.title"),
        "Enter the <cr>reason</c> this user is being banned from Paimbnails. "
        "The reason is shown to the user and saved in the moderation log.");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto const displayName = paimon::text::sanitizeDisplayText(m_username, 64);
    auto lbl = CCLabelBMFont::create(fmt::format(fmt::runtime(Localization::get().getString("ban.popup.user")), displayName).c_str(), "goldFont.fnt");
    lbl->setScale(0.6f);
    lbl->setPosition({cx, content.height - 46.f});
    lbl->setID("username-label"_spr);
    m_mainLayer->addChild(lbl);

    CCSize insetSize = {content.width - 30.f, 70.f};
    auto inset = paimon::ui::makeInset(insetSize);
    inset->setPosition({cx - insetSize.width / 2.f, content.height / 2.f - 24.f});
    m_mainLayer->addChild(inset);

    auto reasonLabel = paimon::ui::makeLabel("Reason", insetSize.width - 20.f, 0.4f);
    reasonLabel->setAnchorPoint({0.f, 0.5f});
    reasonLabel->setPosition({cx - insetSize.width / 2.f + 4.f, content.height / 2.f + 36.f});
    m_mainLayer->addChild(reasonLabel);

    m_input = TextInput::create(240.f, Localization::get().getString("ban.popup.placeholder"));
    m_input->setCommonFilter(geode::CommonFilter::Any);
    m_input->setMaxCharCount(200);
    m_input->setID("reason-input"_spr);
    m_input->setPosition({cx, content.height / 2.f + 8.f});
    m_mainLayer->addChild(m_input, 1);

    {
        paimon::emotes::EmoteInputContext ctx;
        ctx.getText = [this]() -> std::string {
            if (!m_input) return "";
            return m_input->getString();
        };
        ctx.setText = [this](std::string const& text) {
            if (!m_input) return;
            m_input->setString(text);
        };
        ctx.charLimit = 200;
        auto emoteBtn = paimon::emotes::EmoteButton::create(std::move(ctx));
        auto emoteMenu = CCMenu::create();
        emoteMenu->setID("emote-menu"_spr);
        emoteMenu->setPosition({content.width - 18.f, content.height / 2.f + 8.f});
        emoteBtn->setScale(0.45f);
        emoteMenu->addChild(emoteBtn);
        m_mainLayer->addChild(emoteMenu, 5);
    }

    {
        auto ac = paimon::emotes::EmoteAutocomplete::create(
            m_input,
            [this](std::string const& newText) {
                if (m_input) m_input->setString(newText);
            }
        );
        ac->setPosition({cx - 60.f, content.height / 2.f + 34.f});
        m_mainLayer->addChild(ac, 100);
    }

    auto btn = paimon::ui::makeButton(
        Localization::get().getString("ban.popup.ban_btn").c_str(),
        [this] { this->onBan(nullptr); }, paimon::ui::Btn::Red, 0.f, 0.8f);
    btn->setID("ban-btn"_spr);
    auto menu = CCMenu::create();
    menu->setID("ban-menu"_spr);
    menu->addChild(btn);
    menu->setPosition({cx, 34.f});
    m_mainLayer->addChild(menu);

    paimon::markDynamicPopup(this);
    return true;
}

void BanUserPopup::onBan(CCObject*) {
    if (m_pending) return;
    if (!m_input || !paimon::modauth::isVerified()) {
        PaimonNotify::show(Localization::get().getString("ban.verify_required"), NotificationIcon::Warning);
        return;
    }
    if (!paimon::text::isSafeAccountName(m_username)) {
        PaimonNotify::show(Localization::get().getString("ban.popup.invalid_user"), NotificationIcon::Error);
        return;
    }

    std::string const reason = paimon::text::sanitizeDisplayText(m_input->getString(), 200);
    if (reason.empty()) {
        PaimonNotify::show(Localization::get().getString("ban.popup.enter_reason"), NotificationIcon::Error);
        return;
    }

    auto* game = GameManager::get();
    if (game && paimon::ban::sameAccountName(m_username, game->m_playerName)) {
        PaimonNotify::show(Localization::get().getString("ban.profile.self_ban"), NotificationIcon::Warning);
        return;
    }

    m_pending = true;
    auto spinner = PaimonLoadingOverlay::create("Loading...", 30.f);
    spinner->show(m_mainLayer, 100);
    Ref<PaimonLoadingOverlay> loading = spinner;

    WeakRef<BanUserPopup> self = this;
    HttpClient::get().banUser(m_username, reason, [self, loading](bool success, std::string) {
        if (auto popup = self.lock()) {
            popup->m_pending = false;
            if (loading) loading->dismiss();
            if (success) {
                PaimonNotify::show(Localization::get().getString("ban.popup.success"), NotificationIcon::Success);
                popup->onClose(nullptr);
            } else {
                PaimonNotify::show(Localization::get().getString("ban.popup.error"), NotificationIcon::Error);
            }
        }
    });
}
