#include "ProfileReviewsPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ScissorClipNode.hpp"
#include "../../emotes/EmoteRenderer.hpp"
#include "../../emotes/services/EmoteService.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace kit = paimon::editor::kit;

namespace {

constexpr float kPopupW = 420.f;
constexpr float kPopupH = 290.f;
constexpr float kMargin = 14.f;
constexpr float kSummaryH = 62.f;
constexpr float kToolbarY = 172.f;
constexpr float kListY = 14.f;
constexpr float kListTop = 160.f;
constexpr float kScrollbarW = 8.f;
constexpr float kCellGap = 5.f;

constexpr ccColor3B kStarOn{255, 215, 60};
constexpr ccColor3B kStarOff{60, 60, 60};

char const* const kSortNames[] = {"Default", "Highest", "Lowest"};

// half-star precision, filled left to right.
CCNodeRGBA* makeStarRow(float stars, float scale, float gap) {
    auto* row = CCNodeRGBA::create();
    row->setCascadeOpacityEnabled(true);
    row->setAnchorPoint({0.f, 0.5f});

    float x = 0.f;
    float height = 0.f;
    for (int i = 1; i <= 5; ++i) {
        auto* baseSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
        if (!baseSpr) continue;
        baseSpr->setColor(kStarOff);
        auto const starSize = baseSpr->getContentSize();
        height = starSize.height * scale;

        auto* holder = CCNodeRGBA::create();
        holder->setCascadeOpacityEnabled(true);
        holder->setContentSize(starSize);
        holder->setAnchorPoint({0.f, 0.f});
        holder->setScale(scale);
        holder->setPosition({x, 0.f});
        baseSpr->setPosition({starSize.width / 2.f, starSize.height / 2.f});
        holder->addChild(baseSpr, 0);

        float const pos = static_cast<float>(i);
        float fillWidth = 0.f;
        if (stars >= pos - 0.01f) fillWidth = starSize.width;
        else if (stars >= pos - 0.5f - 0.01f) fillWidth = starSize.width * 0.5f;

        if (fillWidth >= starSize.width) {
            baseSpr->setColor(kStarOn);
        } else if (fillWidth > 0.f) {
            if (auto* fillSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
                fillSpr->setColor(kStarOn);
                fillSpr->setPosition({starSize.width / 2.f, starSize.height / 2.f});
                auto* stencil = paimon::SpriteHelper::createRectStencil(fillWidth, starSize.height);
                auto* fillClip = paimon::ScissorClipNode::create(stencil);
                fillClip->setContentSize({fillWidth, starSize.height});
                fillClip->setAnchorPoint({0.f, 0.f});
                fillClip->setPosition({0.f, 0.f});
                fillClip->addChild(fillSpr);
                holder->addChild(fillClip, 1);
            }
        }

        row->addChild(holder);
        x += starSize.width * scale + gap;
    }
    row->setContentSize({std::max(0.f, x - gap), height});
    return row;
}

}

ProfileReviewsPopup* ProfileReviewsPopup::create(int accountID) {
    auto ret = new ProfileReviewsPopup();
    if (ret && ret->init(accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileReviewsPopup::init(int accountID) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    m_accountID = accountID;
    this->setTitle("Profile Reviews");
    this->addCorners();
    this->addInfoButton("Profile Reviews",
        "Ratings other players left on this profile. The top panel shows the <cy>average</c> "
        "out of 5 stars and how the votes are spread.\n"
        "Use the <cg>star chips</c> to show only one rating, <cy>sort</c> by score, and "
        "<co>refresh</c> to reload. Your own review is highlighted in gold.");

    buildSummary();
    buildToolbar();

    auto* listBg = paimon::ui::makeInset({kPopupW - kMargin * 2.f, kListTop - kListY}, 60);
    listBg->setPosition({kMargin, kListY});
    m_mainLayer->addChild(listBg, 0);

    m_emptyLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_emptyLabel->setScale(0.6f);
    m_emptyLabel->setColor(paimon::ui::palette::dim);
    m_emptyLabel->setPosition({kPopupW / 2.f, (kListY + kListTop) / 2.f});
    m_emptyLabel->setVisible(false);
    m_mainLayer->addChild(m_emptyLabel, 3);

    m_spinner = PaimonLoadingOverlay::create("Loading...", 35.f);
    m_spinner->show(m_mainLayer, 10);

    loadReviews();
    paimon::markDynamicPopup(this);
    return true;
}

void ProfileReviewsPopup::buildSummary() {
    float const width = kPopupW - kMargin * 2.f;
    float const bottom = kPopupH - 38.f - kSummaryH;
    auto* panel = paimon::ui::makeInset({width, kSummaryH}, 90);
    panel->setPosition({kMargin, bottom});
    m_mainLayer->addChild(panel, 1);

    float const leftCX = kMargin + 64.f;

    m_averageLabel = CCLabelBMFont::create("...", "bigFont.fnt");
    m_averageLabel->setScale(0.75f);
    m_averageLabel->setColor(kStarOn);
    m_averageLabel->setPosition({leftCX, bottom + kSummaryH - 19.f});
    m_mainLayer->addChild(m_averageLabel, 2);

    m_averageStars = makeStarRow(0.f, 0.42f, 2.f);
    m_averageStars->setAnchorPoint({0.5f, 0.5f});
    m_averageStars->ignoreAnchorPointForPosition(false);
    m_averageStars->setPosition({leftCX, bottom + 22.f});
    m_mainLayer->addChild(m_averageStars, 2);

    m_countLabel = CCLabelBMFont::create("Loading...", "chatFont.fnt");
    m_countLabel->setScale(0.5f);
    m_countLabel->setColor({180, 180, 180});
    m_countLabel->setPosition({leftCX, bottom + 9.f});
    m_mainLayer->addChild(m_countLabel, 2);

    auto* split = CCLayerColor::create({255, 255, 255, 30}, 1.f, kSummaryH - 14.f);
    split->setPosition({kMargin + 132.f, bottom + 7.f});
    m_mainLayer->addChild(split, 2);

    float const barsLeft = kMargin + 146.f;
    float const barX = barsLeft + 22.f;
    float const barW = kMargin + width - 36.f - barX;
    float const rowStep = 10.5f;
    float const firstY = bottom + kSummaryH - 10.f;
    for (int level = 5; level >= 1; --level) {
        int const slot = level - 1;
        float const y = firstY - static_cast<float>(5 - level) * rowStep;

        auto* num = CCLabelBMFont::create(std::to_string(level).c_str(), "bigFont.fnt");
        num->setScale(0.26f);
        num->setPosition({barsLeft + 3.f, y});
        m_mainLayer->addChild(num, 2);

        if (auto* star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
            star->setScale(0.22f);
            star->setColor(kStarOn);
            star->setPosition({barsLeft + 12.f, y});
            m_mainLayer->addChild(star, 2);
        }

        auto* track = CCLayerColor::create({0, 0, 0, 130}, barW, 5.f);
        track->setPosition({barX, y - 2.5f});
        m_mainLayer->addChild(track, 2);

        auto* fill = CCLayerColor::create({kStarOn.r, kStarOn.g, kStarOn.b, 255}, barW, 5.f);
        fill->setAnchorPoint({0.f, 0.5f});
        fill->ignoreAnchorPointForPosition(false);
        fill->setPosition({barX, y});
        fill->setScaleX(0.f);
        m_mainLayer->addChild(fill, 3);
        m_barFills[static_cast<size_t>(slot)] = fill;

        auto* count = CCLabelBMFont::create("0", "bigFont.fnt");
        count->setScale(0.24f);
        count->setAnchorPoint({1.f, 0.5f});
        count->setColor(paimon::ui::palette::muted);
        count->setPosition({kMargin + width - 8.f, y});
        m_mainLayer->addChild(count, 2);
        m_barCounts[static_cast<size_t>(slot)] = count;
    }
}

void ProfileReviewsPopup::buildToolbar() {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setID("reviews-toolbar"_spr);
    m_mainLayer->addChild(menu, 5);

    constexpr float kChipW = 32.f;
    constexpr float kChipH = 18.f;
    constexpr float kGap = 3.f;
    float x = kMargin + kChipW * 0.5f;
    for (int filter = 0; filter <= 5; ++filter) {
        // chip 0 is "all", then 5 down to 1 so the strongest rating reads first.
        int const level = filter == 0 ? 0 : 6 - filter;
        auto pill = filter == 0
            ? kit::pill(nullptr, nullptr, "All", "GJ_button_04.png", {kChipW, kChipH},
                [this] { m_filter = 0; refreshToolbar(); rebuildList(); }, "bigFont.fnt")
            : kit::pill(nullptr, "GJ_starsIcon_001.png", std::to_string(level).c_str(), "GJ_button_04.png",
                {kChipW, kChipH}, [this, level] {
                    m_filter = m_filter == level ? 0 : level;
                    refreshToolbar();
                    rebuildList();
                }, "bigFont.fnt", kStarOn);
        pill.button->setPosition({x, kToolbarY});
        menu->addChild(pill.button);
        m_filterPills.push_back(pill);
        x += kChipW + kGap;
    }

    m_shownLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_shownLabel->setScale(0.42f);
    m_shownLabel->setAnchorPoint({0.f, 0.5f});
    m_shownLabel->setColor(paimon::ui::palette::dim);
    m_shownLabel->setPosition({x - kChipW * 0.5f + 4.f, kToolbarY});
    m_mainLayer->addChild(m_shownLabel, 5);

    float const right = kPopupW - kMargin;
    auto* refreshSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png");
    if (refreshSpr) {
        refreshSpr->setScale(0.42f);
        auto* refreshBtn = CCMenuItemExt::createSpriteExtra(refreshSpr, [this](auto*) {
            if (m_spinner) return;
            m_spinner = PaimonLoadingOverlay::create("Loading...", 35.f);
            m_spinner->show(m_mainLayer, 10);
            loadReviews();
        });
        refreshBtn->setID("reviews-refresh-btn"_spr);
        refreshBtn->setPosition({right - 9.f, kToolbarY});
        menu->addChild(refreshBtn);
    }

    constexpr float kSortW = 86.f;
    m_sortPill = kit::pill(nullptr, "GJ_sortIcon_001.png", kSortNames[0], "GJ_button_05.png",
        {kSortW, kChipH}, [this] {
            m_sort = (m_sort + 1) % static_cast<int>(std::size(kSortNames));
            refreshToolbar();
            rebuildList();
        }, "bigFont.fnt");
    m_sortPill.button->setPosition({right - 22.f - kSortW * 0.5f, kToolbarY});
    menu->addChild(m_sortPill.button);

    refreshToolbar();
}

void ProfileReviewsPopup::refreshToolbar() {
    for (size_t i = 0; i < m_filterPills.size(); ++i) {
        int const level = i == 0 ? 0 : 6 - static_cast<int>(i);
        bool const active = level == m_filter;
        kit::setPillSkin(m_filterPills[i], active ? "GJ_button_01.png" : "GJ_button_04.png");
    }
    kit::setPillText(m_sortPill, kSortNames[std::clamp(m_sort, 0, 2)]);
}

void ProfileReviewsPopup::loadReviews() {
    std::string username;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }

    std::string endpoint = fmt::format("/api/profile-ratings/{}?username={}", m_accountID, HttpClient::encodeQueryParam(username));

    uint64_t gen = ++m_requestGeneration;
    WeakRef<ProfileReviewsPopup> self = this;
    HttpClient::get().get(endpoint, [self, gen](bool ok, std::string const& resp) {
        auto popup = self.lock();
        if (!popup || gen != popup->m_requestGeneration) return;

        if (popup->m_spinner) {
            popup->m_spinner->dismiss();
            popup->m_spinner = nullptr;
        }

        if (!ok) {
            popup->applyResponse(0.f, 0, matjson::Value());
            return;
        }

        auto parsed = matjson::parse(resp);
        if (!parsed.isOk()) {
            popup->applyResponse(0.f, 0, matjson::Value());
            return;
        }
        auto root = parsed.unwrap();

        float avg = 0.f;
        int count = 0;
        if (root["average"].isNumber()) avg = static_cast<float>(root["average"].asDouble().unwrapOr(0.0));
        if (root["count"].isNumber()) count = static_cast<int>(root["count"].asInt().unwrapOr(0));

        popup->applyResponse(avg, count, root["reviews"]);
    });
}

void ProfileReviewsPopup::applyResponse(float average, int count, matjson::Value const& reviews) {
    std::string self;
    if (auto gm = GameManager::get()) self = geode::utils::string::toLower(gm->m_playerName);

    m_reviews.clear();
    if (reviews.isArray()) {
        int order = 0;
        for (auto const& item : reviews.asArray().unwrapOr(std::vector<matjson::Value>{})) {
            if (!item.isObject()) continue;
            Review review;
            review.username = item["username"].asString().unwrapOr("???");
            review.stars = static_cast<float>(item["stars"].asDouble().unwrapOr(0.0));
            review.message = item["message"].asString().unwrapOr("");
            review.bucket = std::clamp(static_cast<int>(std::lround(review.stars)), 1, 5);
            review.order = order++;
            review.own = !self.empty() && geode::utils::string::toLower(review.username) == self;
            m_reviews.push_back(std::move(review));
        }
    }

    updateSummary(average, std::max(count, static_cast<int>(m_reviews.size())));
    rebuildList();
}

void ProfileReviewsPopup::updateSummary(float average, int count) {
    if (m_averageLabel) {
        m_averageLabel->setString(count > 0 ? fmt::format("{:.1f}", average).c_str() : "-");
        m_averageLabel->setScale(0.f);
        m_averageLabel->runAction(CCEaseBackOut::create(CCScaleTo::create(0.3f, 0.75f)));
    }
    if (m_averageStars) {
        auto pos = m_averageStars->getPosition();
        auto* parent = m_averageStars->getParent();
        m_averageStars->removeFromParent();
        m_averageStars = makeStarRow(average, 0.42f, 2.f);
        m_averageStars->setAnchorPoint({0.5f, 0.5f});
        m_averageStars->ignoreAnchorPointForPosition(false);
        m_averageStars->setPosition(pos);
        if (parent) parent->addChild(m_averageStars, 2);
    }
    if (m_countLabel) {
        m_countLabel->setString(count == 0
            ? "No ratings yet"
            : fmt::format("{} rating{}", count, count == 1 ? "" : "s").c_str());
    }

    std::array<int, 5> buckets{};
    for (auto const& review : m_reviews) buckets[static_cast<size_t>(review.bucket - 1)]++;
    int const most = std::max(1, *std::max_element(buckets.begin(), buckets.end()));
    for (size_t i = 0; i < buckets.size(); ++i) {
        if (auto* label = m_barCounts[i]) label->setString(std::to_string(buckets[i]).c_str());
        if (auto* fill = m_barFills[i]) {
            float const target = static_cast<float>(buckets[i]) / static_cast<float>(most);
            fill->stopAllActions();
            if (paimon::ui::motionEnabled()) {
                fill->setScaleX(0.f);
                fill->runAction(CCSequence::create(
                    CCDelayTime::create(0.06f * static_cast<float>(4 - i)),
                    CCEaseSineOut::create(CCScaleTo::create(paimon::ui::motionDuration(0.35f), target, 1.f)),
                    nullptr));
            } else {
                fill->setScaleX(target);
            }
        }
    }
}

void ProfileReviewsPopup::rebuildList() {
    if (m_scrollView) m_scrollView->removeFromParent();
    if (m_scrollbar) m_scrollbar->removeFromParent();
    m_scrollView = nullptr;
    m_scrollbar = nullptr;

    std::vector<Review const*> shown;
    for (auto const& review : m_reviews) {
        if (m_filter == 0 || review.bucket == m_filter) shown.push_back(&review);
    }
    std::stable_sort(shown.begin(), shown.end(), [this](Review const* a, Review const* b) {
        if (a->own != b->own) return a->own;
        if (m_sort == 1 && a->stars != b->stars) return a->stars > b->stars;
        if (m_sort == 2 && a->stars != b->stars) return a->stars < b->stars;
        return a->order < b->order;
    });

    if (m_shownLabel) {
        m_shownLabel->setString(m_reviews.empty() ? "" : fmt::format("{}/{}", shown.size(), m_reviews.size()).c_str());
    }

    if (shown.empty()) {
        if (m_emptyLabel) {
            m_emptyLabel->setString(m_reviews.empty() ? "No reviews yet" : "No reviews with this rating");
            m_emptyLabel->setVisible(true);
            m_emptyLabel->setOpacity(0);
            m_emptyLabel->runAction(CCFadeIn::create(0.3f));
        }
        return;
    }
    if (m_emptyLabel) m_emptyLabel->setVisible(false);

    float const listW = kPopupW - kMargin * 2.f;
    float const scrollW = listW - kScrollbarW - 12.f;
    float const scrollH = kListTop - kListY - 8.f;

    std::vector<CCNode*> cells;
    float totalH = kCellGap;
    for (size_t i = 0; i < shown.size(); ++i) {
        auto* cell = createReviewCell(*shown[i], scrollW, static_cast<int>(i));
        totalH += cell->getContentHeight() + kCellGap;
        cells.push_back(cell);
    }
    totalH = std::max(totalH, scrollH);

    auto* scroll = geode::ScrollLayer::create({scrollW, scrollH});
    scroll->setPosition({kMargin + 4.f, kListY + 4.f});
    scroll->m_contentLayer->setContentSize({scrollW, totalH});

    float yPos = totalH - kCellGap;
    for (auto* cell : cells) {
        yPos -= cell->getContentHeight();
        cell->setPosition({0.f, yPos});
        scroll->m_contentLayer->addChild(cell);
        yPos -= kCellGap;
    }
    scroll->scrollToTop();
    m_mainLayer->addChild(scroll, 2);
    m_scrollView = scroll;

    if (auto* bar = Scrollbar::create(scroll)) {
        bar->setContentSize({kScrollbarW, scrollH - 4.f});
        bar->setPosition({kMargin + listW - 4.f - kScrollbarW * 0.5f, kListY + 4.f + scrollH * 0.5f});
        m_mainLayer->addChild(bar, 3);
        m_scrollbar = bar;
    }

    animateReviewCells(cells);
}

void ProfileReviewsPopup::animateReviewCells(std::vector<CCNode*> const& cells) {
    if (!paimon::ui::motionEnabled()) return;
    // only the first screenful animates; the rest is off-view anyway.
    int const animated = std::min(static_cast<int>(cells.size()), 8);
    for (int i = 0; i < animated; i++) {
        auto* cell = cells[static_cast<size_t>(i)];
        auto finalPos = cell->getPosition();
        cell->setPosition({finalPos.x, finalPos.y - 18.f});

        float const delay = 0.04f * static_cast<float>(i);
        float const duration = paimon::ui::motionDuration(0.3f);
        cell->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCEaseOut::create(CCMoveTo::create(duration, finalPos), 2.5f),
            nullptr));

        // the cell cascades, so every child keeps its own resting opacity.
        if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(cell)) {
            rgba->setOpacity(0);
            rgba->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCFadeTo::create(duration, 255),
                nullptr));
        }
    }
}

CCNode* ProfileReviewsPopup::createReviewCell(Review const& review, float width, int index) {
    constexpr float kHeaderH = 24.f;
    constexpr float kPadX = 10.f;
    constexpr float kMsgScale = 0.5f;
    float const msgW = width - kPadX * 2.f;

    CCNode* body = nullptr;
    if (!review.message.empty()) {
        if (paimon::emotes::EmoteService::get().isLoaded() &&
            paimon::emotes::EmoteRenderer::hasEmoteSyntax(review.message)) {
            body = paimon::emotes::EmoteRenderer::renderComment(
                review.message, 18.f, msgW, "chatFont.fnt", kMsgScale);
        }
        if (!body) {
            auto* label = CCLabelBMFont::create(review.message.c_str(), "chatFont.fnt",
                msgW / kMsgScale, kCCTextAlignmentLeft);
            label->setScale(kMsgScale);
            label->setColor({220, 220, 230});
            body = label;
        }
    }
    float const bodyH = body ? body->getScaledContentSize().height : 0.f;
    float const cellH = kHeaderH + (body ? bodyH + 8.f : 2.f);

    auto* cell = CCNodeRGBA::create();
    cell->setCascadeOpacityEnabled(true);
    cell->setContentSize({width, cellH});

    ccColor3B const tint = review.own ? ccColor3B{255, 190, 60} : ccColor3B{0, 0, 0};
    GLubyte const alpha = review.own ? 55 : (index % 2 == 0 ? 85 : 60);
    auto* bg = paimon::ui::makeInset({width, cellH}, alpha, tint);
    bg->setPosition({0.f, 0.f});
    cell->addChild(bg, -1);

    float const headerY = cellH - kHeaderH * 0.5f - 1.f;
    auto* nameLabel = CCLabelBMFont::create(review.username.c_str(), "goldFont.fnt");
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->limitLabelWidth(width * 0.45f, 0.48f, 0.2f);
    nameLabel->setPosition({kPadX, headerY});
    cell->addChild(nameLabel);

    if (review.own) {
        auto* you = CCLabelBMFont::create("YOU", "bigFont.fnt");
        you->setScale(0.22f);
        you->setColor(kStarOn);
        you->setAnchorPoint({0.f, 0.5f});
        you->setPosition({kPadX + nameLabel->getScaledContentSize().width + 6.f, headerY});
        cell->addChild(you);
    }

    auto* stars = makeStarRow(review.stars, 0.32f, 2.f);
    stars->setAnchorPoint({1.f, 0.5f});
    stars->ignoreAnchorPointForPosition(false);
    stars->setPosition({width - kPadX, headerY});
    cell->addChild(stars);

    if (body) {
        body->setAnchorPoint({0.f, 1.f});
        body->setPosition({kPadX, cellH - kHeaderH - 2.f});
        cell->addChild(body);
    }

    return cell;
}
