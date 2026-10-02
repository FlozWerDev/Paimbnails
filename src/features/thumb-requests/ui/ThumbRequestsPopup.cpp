#include "ThumbRequestsPopup.hpp"

#include "../services/RequestFeed.hpp"
#include "../../twitch-requests/services/TwitchLevelOpen.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/utils/web.hpp>

#include <fmt/format.h>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::thumbreq {

namespace {

constexpr float kPopupW = 420.f;
constexpr float kPopupH = 300.f;
constexpr float kListW = 396.f;
constexpr float kListH = 190.f;
constexpr float kRowH = 56.f;
constexpr int kFilterTag = 8400;
constexpr int kLevelTag = 8500;
constexpr auto kVideoKey = "request-video"_spr;

// filter -> server query. first sends "" and fetches all.
char const* filterQuery(int filter) {
    switch (filter) {
        case 1: return "pending";
        case 2: return "sent";
        default: return "";
    }
}

GJFeatureState featureState(int tier) {
    switch (tier) {
        case 1: return GJFeatureState::Featured;
        case 2: return GJFeatureState::Epic;
        case 3: return GJFeatureState::Legendary;
        case 4: return GJFeatureState::Mythic;
        default: return GJFeatureState::None;
    }
}

ccColor3B statusColor(Status status) {
    switch (status) {
        case Status::Sent:     return {110, 225, 140};
        case Status::Rejected: return {240, 120, 120};
        default:               return {235, 205, 120};
    }
}

std::string statusLabel(Status status) {
    switch (status) {
        case Status::Sent:     return Localization::get().getString("thumbreq.status.sent");
        case Status::Rejected: return Localization::get().getString("thumbreq.status.rejected");
        default:               return Localization::get().getString("thumbreq.status.pending");
    }
}

std::string tierLabel(int tier) {
    switch (tier) {
        case 1: return "Featured";
        case 2: return "Epic";
        case 3: return "Legendary";
        case 4: return "Mythic";
        default: return Localization::get().getString("thumbreq.tier.star");
    }
}

} // namespace

ThumbRequestsPopup* ThumbRequestsPopup::create() {
    auto ret = new ThumbRequestsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ThumbRequestsPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);
    this->setTitle(Localization::get().getString("thumbreq.title"));

    if (auto* logo = paimon::SpriteHelper::safeCreate("Logo.png"_spr)) {
        logo->setScale(0.11f);
        logo->setPosition({30.f, kPopupH - 26.f});
        logo->setOpacity(210);
        m_mainLayer->addChild(logo, 5);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({kPopupW / 2.f, kPopupH - 54.f});
    m_mainLayer->addChild(menu, 3);

    char const* keys[] = {"thumbreq.filter.all", "thumbreq.filter.pending", "thumbreq.filter.sent"};
    for (int i = 0; i < 3; i++) {
        auto* face = ButtonSprite::create(Localization::get().getString(keys[i]).c_str(),
                                          104, true, "bigFont.fnt", "GJ_button_04.png", 22.f, 0.32f);
        auto* btn = CCMenuItemSpriteExtra::create(face, this,
                                                  menu_selector(ThumbRequestsPopup::onFilter));
        btn->setTag(kFilterTag + i);
        btn->setPosition({(i - 1) * 116.f, 0.f});
        menu->addChild(btn);
        m_filterButtons.push_back(btn);
    }

    m_scroll = ScrollLayer::create({kListW, kListH});
    m_scroll->setPosition({(kPopupW - kListW) / 2.f, 16.f});
    m_mainLayer->addChild(m_scroll, 1);

    auto* borders = ListBorders::create();
    borders->setContentSize({kListW, kListH});
    borders->setPosition({kPopupW / 2.f, 16.f + kListH / 2.f});
    m_mainLayer->addChild(borders, 4);

    this->reload();
    return true;
}

void ThumbRequestsPopup::onFilter(CCObject* sender) {
    int const filter = sender->getTag() - kFilterTag;
    if (filter == m_filter) return;
    m_filter = filter;
    this->reload();
}

void ThumbRequestsPopup::reload() {
    m_loading = true;
    m_failed = false;
    int const generation = ++m_generation;
    this->buildRows();

    RequestFeed::get().fetch(
        filterQuery(m_filter),
        [self = WeakRef<ThumbRequestsPopup>(this), generation](
            bool ok, std::vector<Request> const& requests) {
            auto* popup = self.lock().data();
            if (!popup || !popup->getParent()) return;
            if (popup->m_generation != generation) return;
            popup->m_loading = false;
            popup->m_failed = !ok;
            popup->m_requests = requests;
            popup->buildRows();
        });
}

void ThumbRequestsPopup::showMessage(std::string const& text) {
    auto* label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    label->setScale(0.5f);
    label->setOpacity(160);
    label->setPosition({kListW / 2.f, kListH / 2.f});
    m_scroll->m_contentLayer->setContentSize({kListW, kListH});
    m_scroll->m_contentLayer->addChild(label, 1);
}

void ThumbRequestsPopup::buildRows() {
    for (size_t i = 0; i < m_filterButtons.size(); i++) {
        bool const active = static_cast<int>(i) == m_filter;
        m_filterButtons[i]->setColor(active ? ccColor3B{255, 255, 255} : ccColor3B{124, 130, 148});
        m_filterButtons[i]->setOpacity(active ? 255 : 190);
    }

    m_scroll->m_contentLayer->removeAllChildren();

    if (m_loading) {
        this->showMessage(Localization::get().getString("thumbreq.loading"));
        return;
    }
    if (m_failed) {
        this->showMessage(Localization::get().getString("thumbreq.failed"));
        return;
    }
    if (m_requests.empty()) {
        this->showMessage(Localization::get().getString("thumbreq.empty"));
        return;
    }

    float const height = std::max(kListH, m_requests.size() * kRowH);
    m_scroll->m_contentLayer->setContentSize({kListW, height});

    for (size_t i = 0; i < m_requests.size(); i++) {
        float const y = height - kRowH * (i + 0.5f);
        if (auto* row = this->createRow(m_requests[i], y, i % 2 == 0)) {
            m_scroll->m_contentLayer->addChild(row, 1);
        }
    }

    m_scroll->moveToTop();
}

CCNode* ThumbRequestsPopup::createRow(Request const& request, float y, bool odd) {
    auto* row = CCNode::create();
    row->setPosition({0.f, y});

    auto* strip = CCLayerColor::create(
        odd ? ccColor4B{0, 0, 0, 95} : ccColor4B{0, 0, 0, 60}, kListW, kRowH - 2.f);
    strip->setPosition({0.f, -(kRowH - 2.f) / 2.f});
    row->addChild(strip, 0);

    // face carries its own rate glow: hand-hung coins doubled it vs the difficulty.
    if (auto* face = GJDifficultySprite::create(difficultyFace(request.shownDifficulty()),
                                                GJDifficultyName::Short)) {
        face->updateFeatureState(featureState(request.status == Status::Sent ? request.sentTier : 0));
        face->setScale(0.6f);
        face->setPosition({30.f, 0.f});
        row->addChild(face, 1);
    }

    auto* name = CCLabelBMFont::create(request.levelName.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->setScale(std::min(0.46f, 190.f / std::max(1.f, name->getContentSize().width)));
    name->setPosition({58.f, 16.f});
    row->addChild(name, 1);

    std::string const mode = request.mode == "platformer"
        ? Localization::get().getString("thumbreq.mode.platformer")
        : Localization::get().getString("thumbreq.mode.classic");
    auto* meta = CCLabelBMFont::create(
        fmt::format("{} - {} - #{}", request.shownDifficulty(), mode, request.levelId).c_str(),
        "chatFont.fnt");
    meta->setAnchorPoint({0.f, 0.5f});
    meta->setScale(std::min(0.38f, 210.f / std::max(1.f, meta->getContentSize().width)));
    meta->setOpacity(180);
    meta->setPosition({58.f, 1.f});
    row->addChild(meta, 1);

    std::string byline = request.requester.empty()
        ? std::string{}
        : fmt::format(fmt::runtime(Localization::get().getString("thumbreq.by")), request.requester);
    if (request.status == Status::Sent) {
        byline += fmt::format(" - {}", tierLabel(request.sentTier));
    }
    if (!byline.empty()) {
        auto* who = CCLabelBMFont::create(byline.c_str(), "chatFont.fnt");
        who->setAnchorPoint({0.f, 0.5f});
        who->setScale(std::min(0.34f, 200.f / std::max(1.f, who->getContentSize().width)));
        who->setOpacity(140);
        who->setPosition({58.f, -14.f});
        row->addChild(who, 1);
    }

    auto* mark = CCLabelBMFont::create(statusLabel(request.status).c_str(), "bigFont.fnt");
    mark->setAnchorPoint({1.f, 0.5f});
    mark->setScale(0.32f);
    mark->setColor(statusColor(request.status));
    mark->setPosition({kListW - 12.f, 16.f});
    row->addChild(mark, 1);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    row->addChild(menu, 2);

    auto* playSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playBtn2_001.png");
    if (playSpr) {
        playSpr->setScale(0.28f);
        auto* play = CCMenuItemSpriteExtra::create(playSpr, this,
                                                   menu_selector(ThumbRequestsPopup::onOpenLevel));
        play->setTag(kLevelTag + request.levelId);
        play->setPosition({kListW - 26.f, -10.f});
        menu->addChild(play);
    }

    if (!request.video.empty()) {
        auto* videoSpr = paimon::SpriteHelper::safeCreateWithFrameName("gj_ytIcon_001.png");
        if (videoSpr) {
            videoSpr->setScale(0.5f);
            auto* video = CCMenuItemSpriteExtra::create(videoSpr, this,
                                                        menu_selector(ThumbRequestsPopup::onVideo));
            video->setUserObject(kVideoKey, CCString::create(request.video));
            video->setPosition({kListW - 58.f, -10.f});
            menu->addChild(video);
        }
    }

    return row;
}

void ThumbRequestsPopup::onOpenLevel(CCObject* sender) {
    int const levelId = sender->getTag() - kLevelTag;
    if (levelId <= 0) return;
    paimon::twitch::openRequestedLevel(levelId, false);
}

void ThumbRequestsPopup::onVideo(CCObject* sender) {
    auto* item = typeinfo_cast<CCNode*>(sender);
    if (!item) return;
    auto* stored = typeinfo_cast<CCString*>(item->getUserObject(kVideoKey));
    if (!stored) return;

    std::string const url = stored->getCString();
    if (!HttpClient::isUrlSafe(url)) return;
    web::openLinkInBrowser(url);
}

} // namespace paimon::thumbreq
