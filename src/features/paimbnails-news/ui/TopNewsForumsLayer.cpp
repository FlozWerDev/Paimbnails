#include "TopNewsForumsLayer.hpp"

#include "../../../core/ModAuthFlow.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/AccountVerifier.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../backgrounds/services/LayerBackgroundManager.hpp"
#include "../../compat-mods/ui/ModlyUIHelpers.hpp"
#include "../../transitions/services/TransitionManager.hpp"

#include <Geode/binding/MenuLayer.hpp>
#include <Geode/cocos/touch_dispatcher/CCTouchDispatcher.h>
#include <Geode/ui/LazySprite.hpp>
#include <algorithm>
#include <array>
#include <cctype>

using namespace cocos2d;
using namespace geode::prelude;
using paimon::loc::tr;

namespace paimon::news {

namespace {

constexpr float kBannerH = 26.f;
constexpr float kToolbarH = 24.f;
constexpr float kRowHeight = 52.f;
constexpr float kServerBtnSize = 36.f;
constexpr float kServerInnerSize = 24.f;
constexpr float kFavButtonW = 62.f;
constexpr float kOpenButtonW = 58.f;
constexpr float kActionBtnH = 26.f;
constexpr float kSearchScale = 0.72f;

std::string toLowerCopy(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char ch : input) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

ccColor3B rankColor(int rank) {
    switch (rank) {
        case 1: return {255, 215, 75};
        case 2: return {210, 228, 250};
        case 3: return {235, 160, 95};
        default: return paimon::ui::palette::muted;
    }
}

ccColor3B rowTintColor(int rank) {
    switch (rank) {
        case 1: return {48, 36, 12};
        case 2: return {22, 34, 54};
        case 3: return {44, 26, 18};
        default: return {10, 18, 38};
    }
}

ccColor3B rowBorderColor(int rank) {
    switch (rank) {
        case 1: return {255, 208, 70};
        case 2: return {175, 210, 245};
        case 3: return {225, 145, 85};
        default: return {90, 145, 220};
    }
}

char const* serverButtonTexture(int rank) {
    switch (rank) {
        case 1: return "GJ_button_05.png"; // royal blue native GD button
        case 2: return "GJ_button_02.png"; // cyan native GD button
        case 3: return "GJ_button_03.png"; // magenta/purple native GD button
        default: return "GJ_button_04.png"; // dark slate native GD button
    }
}

NineSlice* makeBorderOverlay(CCSize size, ccColor3B color, GLubyte opacity) {
    auto* border = NineSlice::create("GJ_square07.png");
    if (!border) return nullptr;
    border->setScaleMultiplier(0.55f);
    border->setContentSize(size);
    border->setAnchorPoint({0.f, 0.f});
    border->setColor(color);
    border->setOpacity(opacity);
    return border;
}

int rowMenuTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

} // namespace

TopNewsForumsLayer* TopNewsForumsLayer::create(SelectChannelCallback onSelectChannel) {
    auto* ret = new TopNewsForumsLayer();
    if (ret && ret->init(std::move(onSelectChannel))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* TopNewsForumsLayer::scene(SelectChannelCallback onSelectChannel) {
    auto* scene = CCScene::create();
    if (auto* layer = TopNewsForumsLayer::create(std::move(onSelectChannel))) {
        scene->addChild(layer);
    }
    return scene;
}

void TopNewsForumsLayer::open(SelectChannelCallback onSelectChannel) {
    if (auto* sc = scene(std::move(onSelectChannel))) {
        TransitionManager::get().pushScene(sc);
    }
}

bool TopNewsForumsLayer::init(SelectChannelCallback onSelectChannel) {
    if (!CCLayer::init()) return false;
    m_onSelectChannel = std::move(onSelectChannel);

    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(0);

    buildChrome();
    refreshAuthBanner();

    m_providers = NewsService::get().topProviders();
    rebuildList();
    loadTop(true);
    return true;
}

TopNewsForumsLayer::~TopNewsForumsLayer() {
    if (m_alive) *m_alive = false;
}

void TopNewsForumsLayer::onExit() {
    m_isExiting = true;
    if (m_alive) *m_alive = false;
    CCLayer::onExit();
}

void TopNewsForumsLayer::onEnterTransitionDidFinish() {
    CCLayer::onEnterTransitionDidFinish();
    refreshAuthBanner();
}

void TopNewsForumsLayer::keyBackClicked() {
    onBack(nullptr);
}

void TopNewsForumsLayer::onBack(CCObject*) {
    if (m_isExiting) return;
    m_isExiting = true;
    CCDirector::get()->popSceneWithTransition(0.4f, PopTransition::kPopTransitionFade);
}

void TopNewsForumsLayer::onRefresh(CCObject*) {
    refreshAuthBanner();
    loadTop(true);
}

void TopNewsForumsLayer::onOpenAuth() {
    paimon::modauth::showPanel();
}

void TopNewsForumsLayer::buildAmbientDecor(CCSize const& winSize) {
    // Soft decorative sparkles in the outer screen margins so the background
    // feels rich like a GD Hall of Fame layer without distracting from the list.
    struct Sparkle {
        float nx;
        float ny;
        float scale;
        GLubyte opacity;
        float phase;
        bool big;
    };
    constexpr std::array<Sparkle, 8> kSparkles = {{
        {0.055f, 0.78f, 0.55f, 95,  0.0f, true},
        {0.085f, 0.56f, 0.42f, 70,  0.7f, false},
        {0.048f, 0.36f, 0.50f, 80,  1.4f, false},
        {0.945f, 0.78f, 0.55f, 95,  0.4f, true},
        {0.915f, 0.54f, 0.42f, 70,  1.1f, false},
        {0.952f, 0.36f, 0.50f, 80,  1.8f, false},
        {0.20f,  0.94f, 0.36f, 65,  0.9f, false},
        {0.80f,  0.94f, 0.36f, 65,  1.6f, false},
    }};

    bool const animate = paimon::ui::motionEnabled();
    for (auto const& sp : kSparkles) {
        char const* frame = sp.big ? "GJ_bigStar_001.png" : "GJ_starsIcon_001.png";
        auto* star = paimon::SpriteHelper::safeCreateWithFrameName(frame);
        if (!star) continue;
        star->setPosition({winSize.width * sp.nx, winSize.height * sp.ny});
        star->setScale(sp.scale);
        star->setOpacity(sp.opacity);
        star->setColor(sp.big ? ccColor3B{255, 225, 115} : ccColor3B{165, 215, 255});
        this->addChild(star, -6);

        if (animate) {
            float const dur = paimon::ui::motionDuration(2.2f + sp.phase * 0.4f);
            auto* bob = CCRepeatForever::create(CCSequence::create(
                CCEaseSineInOut::create(CCMoveBy::create(dur, {0.f, 4.f})),
                CCEaseSineInOut::create(CCMoveBy::create(dur, {0.f, -4.f})),
                nullptr));
            auto* pulse = CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(dur, static_cast<GLubyte>(std::min(255, sp.opacity + 45))),
                CCFadeTo::create(dur, static_cast<GLubyte>(std::max(25, sp.opacity - 25))),
                nullptr));
            star->runAction(bob);
            star->runAction(pulse);
        }
    }
}

void TopNewsForumsLayer::buildChrome() {
    auto const winSize = CCDirector::get()->getWinSize();

    if (!LayerBackgroundManager::get().applyBackground(this, "top_news_forums")) {
        paimon::ui::decorateScene(this, {18, 72, 168}, true);
    }
    buildAmbientDecor(winSize);

    // Header title flanked by native GD stars + glowing golden underline
    auto* title = paimon::ui::addSceneTitle(
        this, tr("pai.news.top.title", "Top Foros de Noticias").c_str(), 260.f);
    if (title) {
        float const halfTitleW = title->getScaledContentSize().width * 0.5f;
        float const titleY = title->getPositionY();
        for (float sign : {-1.f, 1.f}) {
            if (auto* star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
                star->setScale(0.72f);
                star->setColor({255, 222, 95});
                star->setPosition({winSize.width * 0.5f + sign * (halfTitleW + 14.f), titleY + 1.f});
                this->addChild(star, 6);
            }
        }
        if (auto* underline = paimon::ui::makeDivider(std::min(310.f, halfTitleW * 2.f + 54.f), paimon::ui::palette::gold, 155)) {
            underline->setPosition({winSize.width * 0.5f, titleY - 13.f});
            this->addChild(underline, 6);
        }
    }

    auto* topMenu = paimon::ui::makeZeroMenu("top-news-header-menu"_spr);
    this->addChild(topMenu, 15);

    auto alive = m_alive;

    if (auto* backBtn = paimon::ui::makeBackButton([alive, this] {
            if (alive && *alive) onBack(nullptr);
        })) {
        backBtn->setPosition({28.f, winSize.height - 28.f});
        topMenu->addChild(backBtn);
    }

    if (auto* refreshIcon = CCSprite::createWithSpriteFrameName("GJ_updateBtn_001.png")) {
        refreshIcon->setScale(22.f / std::max(1.f, refreshIcon->getContentSize().height));
        auto* refreshBtn = CCMenuItemSpriteExtra::create(
            refreshIcon, this, menu_selector(TopNewsForumsLayer::onRefresh));
        refreshBtn->setPosition({winSize.width - 56.f, winSize.height - 26.f});
        topMenu->addChild(refreshBtn);
    }

    if (auto* infoBtn = paimon::ui::makeInfoButton(
            tr("pai.news.top.info_title", "Top Foros de Noticias"),
            tr("pai.news.top.info",
               "Vota por tus canales de noticias favoritos con <cy>Fav</c> (+1).\n\n"
               "Para prevenir spam de bots e inyecciones, solo pueden votar usuarios con "
               "<cg>cuenta de Geometry Dash</c> verificada en <cy>Paimbnails</c>."))) {
        infoBtn->setPosition({winSize.width - 26.f, winSize.height - 26.f});
        topMenu->addChild(infoBtn);
    }

    float const cardW = std::min(448.f, winSize.width - 56.f);
    float const cardTop = winSize.height - 46.f;
    float const cardBottom = 14.f;
    float const cardLeft = (winSize.width - cardW) / 2.f;

    // Account verification banner with native GD border + status emblem
    m_authBanner = CCNode::create();
    m_authBanner->setContentSize({cardW, kBannerH});
    m_authBanner->setPosition({cardLeft, cardTop - kBannerH});
    m_authBanner->addChild(paimon::ui::makeInset({cardW, kBannerH}, 145, {8, 18, 40}), -2);
    if (auto* border = makeBorderOverlay({cardW, kBannerH}, {100, 210, 140}, 185)) {
        m_authBanner->addChild(border, 4);
        m_authBorder = border;
    }
    this->addChild(m_authBanner, 10);

    m_authIconSlot = CCNode::create();
    m_authIconSlot->setPosition({15.f, kBannerH / 2.f});
    m_authBanner->addChild(m_authIconSlot, 2);

    m_authLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_authLabel->setAnchorPoint({0.f, 0.5f});
    m_authLabel->setPosition({27.f, kBannerH / 2.f});
    m_authBanner->addChild(m_authLabel, 2);

    m_authStatsLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_authStatsLabel->setAnchorPoint({1.f, 0.5f});
    m_authStatsLabel->setColor(paimon::ui::palette::gold);
    m_authStatsLabel->setPosition({cardW - 10.f, kBannerH / 2.f});
    m_authBanner->addChild(m_authStatsLabel, 2);

    auto* bannerMenu = paimon::ui::makeZeroMenu();
    m_authBanner->addChild(bannerMenu, 5);
    m_verifyButton = paimon::ui::makeButton(
        tr("pai.news.top.verify_btn", "Verificar").c_str(),
        [alive, this] {
            if (alive && *alive) onOpenAuth();
        },
        paimon::ui::Btn::Cyan,
        74.f,
        0.46f,
        "bigFont.fnt"
    );
    if (m_verifyButton) {
        m_verifyButton->setPosition({cardW - 44.f, kBannerH / 2.f});
        bannerMenu->addChild(m_verifyButton);
    }

    // Search bar with crisp native border + clear button
    float const searchY = cardTop - kBannerH - 6.f - kToolbarH / 2.f;
    auto* searchBar = CCNode::create();
    searchBar->setContentSize({cardW, kToolbarH});
    searchBar->setPosition({cardLeft, searchY - kToolbarH / 2.f});
    searchBar->addChild(paimon::ui::makeInset({cardW, kToolbarH}, 145, {8, 16, 36}), -2);
    if (auto* searchBorder = makeBorderOverlay({cardW, kToolbarH}, {95, 165, 245}, 160)) {
        searchBar->addChild(searchBorder, 4);
    }
    this->addChild(searchBar, 9);

    if (auto* glass = paimon::SpriteHelper::safeCreateWithFrameName("gj_findBtn_001.png")) {
        glass->setScale(13.f / std::max(1.f, glass->getContentSize().height));
        glass->setOpacity(210);
        glass->setPosition({14.f, kToolbarH / 2.f});
        searchBar->addChild(glass, 2);
    }

    float const inputW = cardW - 56.f;
    m_searchInput = TextInput::create(inputW / kSearchScale,
        tr("pai.news.top.search", "Buscar foro o servidor..."), "chatFont.fnt");
    if (m_searchInput) {
        m_searchInput->setScale(kSearchScale);
        m_searchInput->setMaxCharCount(40);
        m_searchInput->setCommonFilter(CommonFilter::Text);
        if (auto* bg = m_searchInput->getBGSprite()) bg->setVisible(false);
        m_searchInput->setPosition({26.f + inputW / 2.f, kToolbarH / 2.f});
        m_searchInput->setCallback([alive, this](std::string const& text) {
            if (!alive || !*alive) return;
            m_searchQuery = toLowerCopy(text);
            if (m_clearSearchButton) {
                m_clearSearchButton->setVisible(!m_searchQuery.empty());
            }
            rebuildList();
        });
        searchBar->addChild(m_searchInput, 2);
    }

    auto* searchMenu = paimon::ui::makeZeroMenu();
    searchBar->addChild(searchMenu, 5);
    if (auto* clearBtn = paimon::ui::makeButton(
            "X",
            [alive, this] {
                if (!alive || !*alive) return;
                m_searchQuery.clear();
                if (m_searchInput) m_searchInput->setString("");
                if (m_clearSearchButton) m_clearSearchButton->setVisible(false);
                rebuildList();
            },
            paimon::ui::Btn::Red, 18.f, 0.42f, "bigFont.fnt")) {
        clearBtn->setPosition({cardW - 14.f, kToolbarH / 2.f});
        clearBtn->setVisible(false);
        searchMenu->addChild(clearBtn);
        m_clearSearchButton = clearBtn;
    }

    // Main ranking card with native GD frame + golden corner ornaments
    float const panelTop = searchY - kToolbarH / 2.f - 7.f;
    float const footerH = 16.f;
    float const panelBottom = cardBottom + footerH;
    float const panelH = std::max(118.f, panelTop - panelBottom);

    m_panel = CCNode::create();
    m_panel->setAnchorPoint({0.f, 0.f});
    m_panel->setContentSize({cardW, panelH});
    m_panel->setPosition({cardLeft, panelBottom});

    m_panel->addChild(paimon::ui::makeInset({cardW, panelH}, 158, {6, 14, 34}), -2);
    if (auto* frame = makeBorderOverlay({cardW, panelH}, {110, 175, 255}, 215)) {
        m_panel->addChild(frame, 10);
    }
    paimon::ui::addCorners(m_panel, {cardW, panelH}, SideArtStyle::PopupGold, 0.52f, true);

    // Header strip inside the main ranking card
    float const headerY = panelH - 13.f;
    if (auto* trophyStar = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
        trophyStar->setScale(0.62f);
        trophyStar->setColor({255, 218, 80});
        trophyStar->setPosition({18.f, headerY});
        m_panel->addChild(trophyStar, 3);
    }
    if (auto* heading = paimon::ui::makeTitle(
            tr("pai.news.top.ranking", "Mas votados").c_str(), cardW * 0.55f, 0.52f)) {
        heading->setAnchorPoint({0.f, 0.5f});
        heading->setPosition({29.f, headerY});
        m_panel->addChild(heading, 3);
    }

    m_panelSummaryLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_panelSummaryLabel->setAnchorPoint({1.f, 0.5f});
    m_panelSummaryLabel->setColor({185, 210, 245});
    m_panelSummaryLabel->setPosition({cardW - 14.f, headerY});
    m_panel->addChild(m_panelSummaryLabel, 3);

    if (auto* headerLine = paimon::ui::makeDivider(cardW - 22.f, paimon::ui::palette::gold, 150)) {
        headerLine->setPosition({cardW / 2.f, panelH - paimon::ui::kPanelHeader - 2.f});
        m_panel->addChild(headerLine, 3);
    }

    this->addChild(m_panel, 8);
    paimon::ui::animateIn(m_panel, 0.04f, 8.f);

    m_spinner = LoadingSpinner::create(26.f);
    if (m_spinner) {
        m_spinner->setPosition({cardW / 2.f, (panelH - paimon::ui::kPanelHeader) / 2.f});
        m_spinner->setVisible(false);
        m_panel->addChild(m_spinner, 20);
    }

    m_footerLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_footerLabel->setScale(0.42f);
    m_footerLabel->setColor(paimon::ui::palette::muted);
    m_footerLabel->setPosition({winSize.width / 2.f, cardBottom + 6.f});
    this->addChild(m_footerLabel, 10);
}

void TopNewsForumsLayer::refreshAuthBanner() {
    if (!m_authBanner || !m_authLabel) return;
    float const cardW = m_authBanner->getContentSize().width;

    auto const acc = AccountVerifier::get().verify();
    std::string reason;
    bool const eligible = paimon::modauth::hasVerifiedVoteEligibility(&reason);

    char const* iconFrame = "GJ_completesIcon_001.png";
    ccColor3B borderCol = {100, 220, 145};

    if (eligible) {
        m_authLabel->setString(fmt::format("GD + Paimbnails verificado: {}", acc.username).c_str());
        m_authLabel->setColor(paimon::ui::palette::success);
        if (m_verifyButton) m_verifyButton->setVisible(false);
        if (m_authStatsLabel) m_authStatsLabel->setVisible(true);
        iconFrame = "GJ_completesIcon_001.png";
        borderCol = {100, 220, 145};
    } else if (!acc.loggedIn()) {
        m_authLabel->setString("Inicia sesion en Geometry Dash para poder votar (+1 Fav)");
        m_authLabel->setColor(paimon::ui::palette::warning);
        if (m_verifyButton) m_verifyButton->setVisible(false);
        if (m_authStatsLabel) m_authStatsLabel->setVisible(true);
        iconFrame = "exMark_001.png";
        borderCol = {245, 185, 80};
    } else {
        m_authLabel->setString("Cuenta GD sin verificar en Paimbnails (solo lectura)");
        m_authLabel->setColor(paimon::ui::palette::danger);
        if (m_verifyButton) m_verifyButton->setVisible(true);
        if (m_authStatsLabel) m_authStatsLabel->setVisible(false);
        iconFrame = "GJ_infoIcon_001.png";
        borderCol = {245, 110, 110};
    }

    if (m_authBorder) {
        m_authBorder->setColor(borderCol);
    }
    if (m_authIconSlot) {
        m_authIconSlot->removeAllChildren();
        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(iconFrame)) {
            float const maxSide = std::max(1.f, std::max(icon->getContentSize().width, icon->getContentSize().height));
            icon->setScale(13.f / maxSide);
            m_authIconSlot->addChild(icon);
        }
    }

    float const rightReserved = (m_verifyButton && m_verifyButton->isVisible()) ? 104.f : 112.f;
    float const maxW = std::max(80.f, cardW - 34.f - rightReserved);
    m_authLabel->limitLabelWidth(maxW, 0.45f, 0.2f);
    refreshSummaryStats();
}

void TopNewsForumsLayer::refreshSummaryStats() {
    int totalVotes = 0;
    for (auto const& p : m_providers) totalVotes += std::max(0, p.favoritesCount);

    if (m_authStatsLabel) {
        m_authStatsLabel->setString(fmt::format("{} favs", totalVotes).c_str());
        m_authStatsLabel->limitLabelWidth(86.f, 0.34f, 0.16f);
    }
    if (m_panelSummaryLabel) {
        m_panelSummaryLabel->setString(
            fmt::format("{} foros · {} favs", m_providers.size(), totalVotes).c_str());
        m_panelSummaryLabel->limitLabelWidth(135.f, 0.42f, 0.2f);
    }
}

void TopNewsForumsLayer::loadTop(bool force) {
    if (m_loading) return;
    m_loading = true;
    if (m_spinner) m_spinner->setVisible(true);

    auto alive = m_alive;
    NewsService::get().fetchTopProviders(force, [alive, this](bool ok, std::vector<NewsProvider> const& list) {
        if (!alive || !*alive || m_isExiting) return;
        m_loading = false;
        if (m_spinner) m_spinner->setVisible(false);

        if (!list.empty()) {
            m_providers = list;
        }
        refreshAuthBanner();
        rebuildList();
        if (!ok && m_footerLabel) {
            m_footerLabel->setString("No se pudo actualizar el top (mostrando cache)");
        }
    });
}

CCNode* TopNewsForumsLayer::createServerIconFace(NewsProvider const& provider, int rank) {
    auto* container = CCNode::create();
    container->setContentSize({kServerBtnSize, kServerBtnSize});
    container->setAnchorPoint({0.5f, 0.5f});

    // Native GD glossy square button frame so the server icon never looks cut off
    if (auto* btnFrame = paimon::SpriteHelper::safeCreateScale9(serverButtonTexture(rank))) {
        btnFrame->setContentSize({kServerBtnSize, kServerBtnSize});
        btnFrame->setPosition({kServerBtnSize * 0.5f, kServerBtnSize * 0.5f});
        container->addChild(btnFrame, 0);
    }

    // Dark recessed inner well to make bright or transparent logos pop cleanly
    auto* well = paimon::ui::makeInset({kServerInnerSize + 2.f, kServerInnerSize + 2.f}, 145, {10, 14, 26});
    well->setAnchorPoint({0.5f, 0.5f});
    well->setPosition({kServerBtnSize * 0.5f, kServerBtnSize * 0.5f});
    container->addChild(well, 1);

    if (!provider.iconUrl.empty()) {
        // Gentle rounded-square mask matching the GD button interior + fit-inside scaling
        auto* clipper = CCClippingNode::create();
        clipper->setContentSize({kServerInnerSize, kServerInnerSize});
        clipper->setAnchorPoint({0.5f, 0.5f});
        clipper->setPosition({kServerBtnSize * 0.5f, kServerBtnSize * 0.5f});
        clipper->setStencil(paimon::SpriteHelper::createRoundedRectStencil(
            kServerInnerSize, kServerInnerSize, 4.f));

        auto* sprite = LazySprite::create({kServerInnerSize, kServerInnerSize}, false);
        sprite->setPosition({kServerInnerSize * 0.5f, kServerInnerSize * 0.5f});
        sprite->setOpacity(0);
        clipper->addChild(sprite, 1);

        // LazySprite invokes its load callback while alive; avoid self-owned WeakRef<LazySprite>.
        sprite->setLoadCallback([sprite](Result<> res) {
            if (!res.isOk() || !sprite || !sprite->getParent()) return;
            auto const sz = sprite->getContentSize();
            if (sz.width <= 0.f || sz.height <= 0.f) return;
            // Fit inside cleanly (never crop/cut off the server artwork)
            sprite->setScale(std::min(kServerInnerSize / sz.width, kServerInnerSize / sz.height));
            sprite->runAction(CCFadeIn::create(0.22f));
        });
        sprite->loadFromUrl(provider.iconUrl);
        container->addChild(clipper, 2);
    } else {
        std::string initial = "?";
        if (!provider.name.empty()) {
            auto c0 = static_cast<unsigned char>(provider.name[0]);
            std::size_t n = c0 < 0x80 ? 1 : (c0 < 0xE0 ? 2 : (c0 < 0xF0 ? 3 : 4));
            initial = provider.name.substr(0, n);
            if (n == 1) initial[0] = static_cast<char>(std::toupper(c0));
        }
        auto* letter = CCLabelBMFont::create(initial.c_str(), "bigFont.fnt");
        letter->setPosition({kServerBtnSize * 0.5f, kServerBtnSize * 0.5f});
        letter->setScale(0.45f);
        letter->setColor(paimon::ui::palette::text);
        container->addChild(letter, 2);
    }

    // Subtle golden star badge on top-right corner of the #1 server button
    if (rank == 1) {
        if (auto* crownStar = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
            crownStar->setScale(0.46f);
            crownStar->setColor({255, 225, 85});
            crownStar->setPosition({kServerBtnSize - 2.f, kServerBtnSize - 2.f});
            container->addChild(crownStar, 5);
        }
    }

    return container;
}

CCNode* TopNewsForumsLayer::createForumRow(NewsProvider const& provider, float width) {
    int const rank = provider.rank > 0 ? provider.rank : 1;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowHeight});

    // Tinted card backdrop + crisp native GD outline
    row->addChild(paimon::ui::makeInset({width, kRowHeight}, rank <= 3 ? 160 : 125, rowTintColor(rank)), -2);
    if (auto* rowBorder = makeBorderOverlay({width, kRowHeight}, rowBorderColor(rank), rank <= 3 ? 195 : 115)) {
        row->addChild(rowBorder, 3);
    }

    // Colored rank accent stripe on the left edge
    auto* stripe = paimon::ui::makeInset({3.5f, kRowHeight - 10.f}, 230, rankColor(rank));
    stripe->setPosition({4.f, 5.f});
    row->addChild(stripe, 1);

    // Rank medal / number
    if (rank <= 3) {
        if (auto* medalStar = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png")) {
            medalStar->setScale(0.36f);
            medalStar->setColor(rankColor(rank));
            medalStar->setOpacity(65);
            medalStar->setPosition({22.f, kRowHeight * 0.5f});
            row->addChild(medalStar, 0);
        }
    }
    auto* rankLbl = CCLabelBMFont::create(fmt::format("#{}", rank).c_str(), "goldFont.fnt");
    rankLbl->setColor(rankColor(rank));
    rankLbl->limitLabelWidth(26.f, rank <= 3 ? 0.52f : 0.44f, 0.24f);
    rankLbl->setPosition({22.f, kRowHeight * 0.5f});
    row->addChild(rankLbl, 2);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(rowMenuTouchPrio());
    row->addChild(menu, 5);

    auto alive = m_alive;
    std::string cid = provider.channelId;

    // Server avatar inside a clickable native GD button
    float const avatarCenterX = 58.f;
    if (auto* iconFace = createServerIconFace(provider, rank)) {
        auto* iconBtn = CCMenuItemExt::createSpriteExtra(iconFace, [alive, this, cid](CCMenuItemSpriteExtra*) {
            if (alive && *alive) onOpenForum(cid);
        });
        iconBtn->m_scaleMultiplier = 1.1f;
        iconBtn->setPosition({avatarCenterX, kRowHeight * 0.5f});
        menu->addChild(iconBtn);
    }

    float const textLeft = avatarCenterX + kServerBtnSize * 0.5f + 10.f;
    float const actionsWidth = kFavButtonW + kOpenButtonW + 22.f;
    float const textMaxW = std::max(50.f, width - textLeft - actionsWidth);

    std::string sub;
    if (!provider.channelName.empty()) {
        sub = "#" + provider.channelName;
        if (!provider.guildName.empty() && provider.guildName != provider.name) {
            sub += " · " + provider.guildName;
        }
    } else {
        sub = fmt::format("ID {}", provider.channelId);
    }

    auto* titleLbl = CCLabelBMFont::create(provider.name.c_str(), "bigFont.fnt");
    titleLbl->setAnchorPoint({0.f, 0.5f});
    titleLbl->setColor(rank == 1 || provider.isDefault ? paimon::ui::palette::gold : paimon::ui::palette::text);
    titleLbl->limitLabelWidth(textMaxW, 0.40f, 0.14f);
    titleLbl->setPosition({textLeft, kRowHeight - 16.f});
    row->addChild(titleLbl, 2);

    auto* subLbl = CCLabelBMFont::create(sub.c_str(), "chatFont.fnt");
    subLbl->setAnchorPoint({0.f, 0.5f});
    subLbl->setColor(paimon::ui::palette::muted);
    subLbl->limitLabelWidth(textMaxW, 0.45f, 0.16f);
    subLbl->setPosition({textLeft, 14.f});
    row->addChild(subLbl, 2);

    // Native GD glossy Fav (+1) button
    auto* favFace = CCNode::create();
    favFace->setContentSize({kFavButtonW, kActionBtnH});
    favFace->setAnchorPoint({0.5f, 0.5f});
    if (auto* favBg = paimon::SpriteHelper::safeCreateScale9("GJ_button_04.png")) {
        favBg->setContentSize({kFavButtonW, kActionBtnH});
        favBg->setPosition({kFavButtonW * 0.5f, kActionBtnH * 0.5f});
        if (provider.favoritedByMe) {
            favBg->setColor({255, 200, 75});
        }
        favFace->addChild(favBg, 0);
    }

    if (auto* star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
        star->setScale(13.f / std::max(1.f, star->getContentSize().height));
        star->setPosition({15.f, kActionBtnH * 0.5f});
        star->setColor(provider.favoritedByMe ? ccColor3B{255, 235, 110} : ccColor3B{185, 195, 215});
        favFace->addChild(star, 1);
    }

    auto* countLbl = CCLabelBMFont::create(std::to_string(provider.favoritesCount).c_str(), "bigFont.fnt");
    countLbl->setAnchorPoint({0.f, 0.5f});
    countLbl->setColor(provider.favoritedByMe ? ccColor3B{255, 245, 180} : paimon::ui::palette::text);
    countLbl->limitLabelWidth(kFavButtonW - 30.f, 0.36f, 0.14f);
    countLbl->setPosition({26.f, kActionBtnH * 0.5f});
    favFace->addChild(countLbl, 1);

    bool const pending = m_pendingVotes.count(provider.channelId) > 0;
    auto* favBtn = CCMenuItemExt::createSpriteExtra(favFace, [alive, this, cid](CCMenuItemSpriteExtra*) {
        if (alive && *alive) onToggleFavorite(cid);
    });
    favBtn->m_scaleMultiplier = 1.08f;
    favBtn->setEnabled(!pending);
    if (pending) favBtn->setOpacity(150);
    favBtn->setPosition({width - kOpenButtonW - kFavButtonW / 2.f - 15.f, kRowHeight * 0.5f});
    menu->addChild(favBtn);

    // Native GD green Open / Read button
    auto* openBtn = paimon::ui::makeButton(
        tr("pai.news.top.open", "Leer").c_str(),
        [alive, this, cid] {
            if (alive && *alive) onOpenForum(cid);
        },
        paimon::ui::Btn::Green,
        kOpenButtonW,
        0.50f,
        "bigFont.fnt"
    );
    if (openBtn) {
        openBtn->setPosition({width - kOpenButtonW / 2.f - 9.f, kRowHeight * 0.5f});
        menu->addChild(openBtn);
    }

    return row;
}

void TopNewsForumsLayer::rebuildList() {
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

    auto const panelSize = m_panel->getContentSize();
    float const scrollW = panelSize.width - 18.f;
    float const scrollH = panelSize.height - paimon::ui::kPanelHeader - 14.f;

    std::vector<NewsProvider> filtered;
    for (auto const& p : m_providers) {
        if (!m_searchQuery.empty()) {
            auto hay = toLowerCopy(p.name + " " + p.channelName + " " + p.guildName + " " + p.channelId);
            if (hay.find(m_searchQuery) == std::string::npos) continue;
        }
        filtered.push_back(p);
    }

    std::vector<CCNode*> rows;
    rows.reserve(filtered.size());
    for (auto const& p : filtered) {
        rows.push_back(createForumRow(p, scrollW - 4.f));
    }

    m_scroll = paimon::configkit::makeScrollStack({scrollW, scrollH}, rows, 6.f);
    if (!m_scroll) return;
    m_scroll->setPosition({9.f, 7.f});
    m_panel->addChild(m_scroll, 2);

    if (hadScroll && m_scroll->m_contentLayer) {
        float const minY = std::min(0.f, scrollH - m_scroll->m_contentLayer->getContentSize().height);
        m_scroll->m_contentLayer->setPositionY(std::clamp(prevY, minY, 0.f));
    }

    refreshSummaryStats();
    if (m_footerLabel) {
        m_footerLabel->setString(fmt::format(
            "{} foro(s) de noticias — 1 voto por cuenta de GD verificada",
            filtered.size()
        ).c_str());
    }
}

void TopNewsForumsLayer::onToggleFavorite(std::string const& channelId) {
    auto const acc = AccountVerifier::get().verify();
    if (!acc.loggedIn()) {
        PaimonNotify::show("Inicia sesion en Geometry Dash para poder votar.", NotificationIcon::Warning);
        refreshAuthBanner();
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

    auto alive = m_alive;
    NewsService::get().toggleFavoriteProvider(
        channelId,
        std::nullopt,
        [alive, this, channelId](bool ok, int count, bool fav, std::string const& err) {
            if (!alive || !*alive || m_isExiting) return;
            m_pendingVotes.erase(channelId);

            if (!ok) {
                PaimonNotify::show(err.empty() ? "No se pudo registrar tu voto." : err, NotificationIcon::Warning);
                if (err.find("Verifica") != std::string::npos || err.find("sesion") != std::string::npos) {
                    paimon::modauth::showPanel();
                }
                refreshAuthBanner();
                rebuildList();
                return;
            }

            m_providers = NewsService::get().topProviders();
            refreshAuthBanner();
            rebuildList();
            PaimonNotify::show(
                fav ? fmt::format("Fav anadido (+1 · total {})", count)
                    : fmt::format("Fav quitado (total {})", count),
                NotificationIcon::Success
            );
        }
    );
}

void TopNewsForumsLayer::onOpenForum(std::string const& channelId) {
    NewsService::get().setSourceEnabled(channelId, true);
    NewsService::get().setPendingOpenChannel(channelId);

    if (m_isExiting) return;
    m_isExiting = true;

    if (m_onSelectChannel) {
        auto cb = m_onSelectChannel;
        CCDirector::get()->popScene();
        geode::queueInMainThread([cb, channelId]() {
            if (cb) cb(channelId);
        });
        return;
    }

    CCDirector::get()->popScene();
    geode::queueInMainThread([]() {
        auto* scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        if (auto* menu = scene->getChildByType<MenuLayer>(0)) {
            menu->onNews(nullptr);
            return;
        }
        PaimonNotify::show("Foro activado. Abre Noticias desde el menu principal.", NotificationIcon::Info);
    });
}

} // namespace paimon::news
