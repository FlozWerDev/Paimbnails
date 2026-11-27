#include "NewsServerPickerPopup.hpp"

#include "TopNewsForumsLayer.hpp"
#include "../services/PaimbnailsNewsService.hpp"
#include "../../../core/ModAuthFlow.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/AccountVerifier.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../compat-mods/ui/ModlyUIHelpers.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/touch_dispatcher/CCTouchDispatcher.h>
#include <algorithm>
#include <vector>

using namespace cocos2d;
using namespace geode::prelude;
using paimon::loc::tr;

namespace paimon::news {

namespace {

constexpr float kWidth = 360.f;
constexpr float kHeight = 260.f;
constexpr float kPanelWidth = 320.f;
constexpr float kPanelHeight = 180.f;
constexpr float kPanelBottom = 16.f;
constexpr float kRowWidth = 302.f;
constexpr float kRowHeight = 40.f;
constexpr float kAvatarSize = 24.f;
constexpr float kFavButtonW = 48.f;
constexpr float kButtonWidth = 64.f;
constexpr float kButtonScale = 0.52f;

int rowMenuTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

} // namespace

NewsServerPickerPopup* NewsServerPickerPopup::create(std::function<void()> onChanged) {
    auto* popup = new NewsServerPickerPopup;
    if (popup->init(std::move(onChanged))) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool NewsServerPickerPopup::init(std::function<void()> onChanged) {
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
    m_onChanged = std::move(onChanged);

    setTitle(tr("pai.news.servers.title", "News servers").c_str());
    addCorners(geode::SideArtStyle::PopupGold, 1.f, true);
    addInfoButton(tr("pai.news.servers.info_title", "Verified servers"),
        tr("pai.news.servers.info",
           "Add or remove news tabs. You can toggle Vanilla and verified servers here."));

    m_panel = paimon::ui::makePanel({kPanelWidth, kPanelHeight},
        tr("pai.news.servers.verified", "Verified").c_str(), 75);
    m_panel->setPosition({(kWidth - kPanelWidth) / 2.f, kPanelBottom});
    m_mainLayer->addChild(m_panel);

    auto* headerMenu = paimon::ui::makeZeroMenu();
    m_mainLayer->addChild(headerMenu, 10);
    WeakRef<NewsServerPickerPopup> weakSelf = this;
    if (auto* topBtn = paimon::ui::makeButton(
            tr("pai.news.servers.top_btn", "Top").c_str(),
            [weakSelf] {
                auto self = weakSelf.lock();
                if (!self) return;
                auto onChanged = self->m_onChanged;
                self->onClose(nullptr);
                TopNewsForumsLayer::open([onChanged](std::string const&) {
                    if (onChanged) onChanged();
                });
            },
            paimon::ui::Btn::Blue, 52.f, 0.44f, "bigFont.fnt")) {
        topBtn->setPosition({kWidth - 50.f, kHeight - 24.f});
        headerMenu->addChild(topBtn);
    }

    rebuildList();
    paimon::ui::animateIn(m_panel, 0.04f, 8.f);

    WeakRef<NewsServerPickerPopup> weak = this;
    NewsService::get().fetchProviders(false, [weak](bool, std::vector<NewsProvider> const&) {
        if (auto self = weak.lock()) self->rebuildList();
    });
    return true;
}

CCNode* NewsServerPickerPopup::createSourceRow(
    std::string const& sourceId,
    std::string const& title,
    std::string const& subtitle,
    std::string const& iconUrl,
    bool showAvatar,
    bool enabled,
    int favoritesCount,
    bool favoritedByMe,
    bool canFavorite
) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({kRowWidth, kRowHeight});
    row->addChild(paimon::ui::makeInset({kRowWidth, kRowHeight}, 65), -1);

    float textLeft = 10.f;
    if (showAvatar) {
        if (auto* avatar = paimon::compat_mods::createAvatar(
                iconUrl, !iconUrl.empty(), title, kAvatarSize)) {
            avatar->setPosition({10.f + kAvatarSize / 2.f, kRowHeight / 2.f});
            row->addChild(avatar, 1);
            textLeft += kAvatarSize + 8.f;
        }
    }

    float const favReserve = canFavorite ? (kFavButtonW + 6.f) : 0.f;
    float const textMaxW = std::max(40.f, kRowWidth - textLeft - kButtonWidth - favReserve - 16.f);

    auto* titleLbl = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
    titleLbl->setAnchorPoint({0.f, 0.5f});
    titleLbl->setColor(enabled ? paimon::ui::palette::gold : paimon::ui::palette::text);
    titleLbl->limitLabelWidth(textMaxW, 0.38f, 0.12f);
    titleLbl->setPosition({textLeft, subtitle.empty() ? kRowHeight / 2.f : kRowHeight - 13.f});
    row->addChild(titleLbl, 1);

    if (!subtitle.empty()) {
        auto* subLbl = CCLabelBMFont::create(subtitle.c_str(), "chatFont.fnt");
        subLbl->setAnchorPoint({0.f, 0.5f});
        subLbl->setColor(paimon::ui::palette::muted);
        subLbl->limitLabelWidth(textMaxW, 0.44f, 0.15f);
        subLbl->setPosition({textLeft, 12.f});
        row->addChild(subLbl, 1);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(rowMenuTouchPrio());
    row->addChild(menu, 5);

    WeakRef<NewsServerPickerPopup> weak = this;
    if (canFavorite) {
        auto* favFace = CCNode::create();
        favFace->setContentSize({kFavButtonW, 22.f});
        favFace->setAnchorPoint({0.5f, 0.5f});
        ccColor3B const favBg = favoritedByMe ? ccColor3B{130, 92, 18} : ccColor3B{34, 42, 62};
        favFace->addChild(paimon::ui::makeInset({kFavButtonW, 22.f}, favoritedByMe ? 190 : 140, favBg), -1);

        if (auto* star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
            star->setScale(11.f / std::max(1.f, star->getContentSize().height));
            star->setPosition({11.f, 11.f});
            star->setColor(favoritedByMe ? ccColor3B{255, 225, 90} : ccColor3B{150, 160, 180});
            favFace->addChild(star, 1);
        }

        auto* countLbl = CCLabelBMFont::create(std::to_string(favoritesCount).c_str(), "bigFont.fnt");
        countLbl->setAnchorPoint({0.f, 0.5f});
        countLbl->setColor(favoritedByMe ? paimon::ui::palette::gold : paimon::ui::palette::text);
        countLbl->limitLabelWidth(kFavButtonW - 22.f, 0.30f, 0.12f);
        countLbl->setPosition({19.f, 11.f});
        favFace->addChild(countLbl, 1);

        bool const pending = m_pendingVotes.count(sourceId) > 0;
        auto* favBtn = CCMenuItemExt::createSpriteExtra(favFace, [weak, sourceId](CCMenuItemSpriteExtra*) {
            if (auto self = weak.lock()) self->onToggleFavorite(sourceId);
        });
        favBtn->m_scaleMultiplier = 1.08f;
        favBtn->setEnabled(!pending);
        if (pending) favBtn->setOpacity(150);
        favBtn->setPosition({kRowWidth - kButtonWidth - kFavButtonW / 2.f - 14.f, kRowHeight / 2.f});
        menu->addChild(favBtn);
    }

    auto const btnText = enabled
        ? tr("pai.news.servers.remove", "Remove")
        : tr("pai.news.servers.add", "Add");
    auto const skin = enabled ? paimon::ui::Btn::Red : paimon::ui::Btn::Green;

    auto* btn = paimon::ui::makeButton(btnText.c_str(), [weak, sourceId, enabled] {
        auto self = weak.lock();
        if (!self) return;
        if (!NewsService::get().setSourceEnabled(sourceId, !enabled)) {
            Notification::create(
                tr("pai.news.servers.min_one", "Keep at least one news source").c_str(),
                NotificationIcon::Warning
            )->show();
            return;
        }
        if (self->m_onChanged) self->m_onChanged();
        self->rebuildList();
    }, skin, kButtonWidth, kButtonScale, "bigFont.fnt");
    if (btn) {
        btn->setPosition({kRowWidth - kButtonWidth / 2.f - 8.f, kRowHeight / 2.f});
        menu->addChild(btn);
    }

    return row;
}

void NewsServerPickerPopup::onToggleFavorite(std::string const& channelId) {
    auto const acc = AccountVerifier::get().verify();
    if (!acc.loggedIn()) {
        PaimonNotify::show("Inicia sesion en Geometry Dash para poder votar.", NotificationIcon::Warning);
        return;
    }

    std::string reason;
    if (!paimon::modauth::hasVerifiedVoteEligibility(&reason)) {
        auto const token = HttpClient::get().getSessionToken();
        if (token.empty() || !paimon::auth::UnifiedAuthFlow::validSessionToken(token, acc.accountID)) {
            PaimonNotify::show("Verifica tu cuenta en Paimbnails para poder votar.", NotificationIcon::Warning);
            paimon::modauth::showPanel();
            return;
        }
    }

    if (m_pendingVotes.count(channelId) > 0) return;
    m_pendingVotes.insert(channelId);
    rebuildList();

    WeakRef<NewsServerPickerPopup> weak = this;
    NewsService::get().toggleFavoriteProvider(
        channelId,
        std::nullopt,
        [weak, channelId](bool ok, int count, bool fav, std::string const& err) {
            auto self = weak.lock();
            if (!self) return;
            self->m_pendingVotes.erase(channelId);
            if (!ok) {
                PaimonNotify::show(err.empty() ? "No se pudo registrar tu voto." : err, NotificationIcon::Warning);
                if (err.find("Verifica") != std::string::npos || err.find("sesion") != std::string::npos) {
                    paimon::modauth::showPanel();
                }
                self->rebuildList();
                return;
            }
            self->rebuildList();
            PaimonNotify::show(
                fav ? fmt::format("Fav añadido (+1 · total {})", count)
                    : fmt::format("Fav quitado (total {})", count),
                NotificationIcon::Success
            );
        }
    );
}

void NewsServerPickerPopup::rebuildList() {
    if (!m_panel) return;

    float prevY = 0.f;
    bool hadScroll = false;
    if (m_scroll) {
        if (m_scroll->m_contentLayer) {
            prevY = m_scroll->m_contentLayer->getPositionY();
            hadScroll = true;
        }
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    auto& service = NewsService::get();
    std::vector<CCNode*> rows;
    rows.push_back(createSourceRow(
        std::string(kVanillaSourceId),
        tr("pai.news.tab.vanilla", "Vanilla"),
        tr("pai.news.servers.vanilla_desc", "Official Geometry Dash news"),
        {},
        false,
        service.isVanillaEnabled()
    ));

    for (auto const& p : service.providers()) {
        std::string sub;
        if (!p.channelName.empty()) {
            sub = "#" + p.channelName;
        } else {
            sub = fmt::format(fmt::runtime(tr("pai.news.servers.channel_fmt", "Channel {}")), p.channelId);
        }
        rows.push_back(createSourceRow(
            p.channelId,
            p.name,
            sub,
            p.iconUrl,
            true,
            service.isSourceEnabled(p.channelId),
            p.favoritesCount,
            p.favoritedByMe,
            true
        ));
    }

    CCSize const scrollSize{kRowWidth + 4.f, kPanelHeight - paimon::ui::kPanelHeader - 10.f};
    m_scroll = paimon::configkit::makeScrollStack(scrollSize, rows, 5.f);
    if (!m_scroll) return;
    m_scroll->setPosition({(kPanelWidth - scrollSize.width) / 2.f, 6.f});
    m_panel->addChild(m_scroll, 2);

    if (hadScroll && m_scroll->m_contentLayer) {
        float const minY = std::min(0.f, scrollSize.height - m_scroll->m_contentLayer->getContentSize().height);
        m_scroll->m_contentLayer->setPositionY(std::clamp(prevY, minY, 0.f));
    }
}

} // namespace paimon::news
