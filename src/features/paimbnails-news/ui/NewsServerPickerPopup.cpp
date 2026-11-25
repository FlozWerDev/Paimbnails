#include "NewsServerPickerPopup.hpp"

#include "../services/PaimbnailsNewsService.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
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
constexpr float kButtonWidth = 68.f;
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
    bool enabled
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

    float const textMaxW = std::max(40.f, kRowWidth - textLeft - kButtonWidth - 16.f);

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

    auto const btnText = enabled
        ? tr("pai.news.servers.remove", "Remove")
        : tr("pai.news.servers.add", "Add");
    auto const skin = enabled ? paimon::ui::Btn::Red : paimon::ui::Btn::Green;

    WeakRef<NewsServerPickerPopup> weak = this;
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
            service.isSourceEnabled(p.channelId)
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
