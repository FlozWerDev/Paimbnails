#include "../../../ui/PaimonUI.hpp"
#include "CommunityHubLayer.hpp"
#include "../../audio/services/CaveAudio.hpp"
#include <Geode/utils/string.hpp>
#include "../../../core/BanGate.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/ModProfileCache.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../compat-mods/services/ModlyRepo.hpp"
#include "../../compat-mods/ui/ModlyModPopup.hpp"
#include "../../compat-mods/ui/ModlyUIHelpers.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../backgrounds/services/LayerBackgroundManager.hpp"
#include "../../moderation/services/GdUserResolver.hpp"
#include "../../profiles/services/ProfileThumbs.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/GDRobTopCache.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../../utils/ScissorClipNode.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../thumbnails/ui/LocalThumbnailViewPopup.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <matjson.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace geode::prelude;

namespace {
    constexpr char const* kGdSecret = "Wmfd2893gb7";
    constexpr int kMaxIconsInFlight = 3;
    constexpr int kMaxIconAttempts = 3;
    constexpr float kIconTickInterval = 0.3f;
    constexpr int kMaxParticles = 16;
    constexpr float kTopButtonThreshold = 90.f;

    // card geometry: tabs, toolbar, list and status line share one panel.
    constexpr float kCardPad = 6.f;
    constexpr float kTabsH = 24.f;
    constexpr float kToolbarH = 22.f;
    constexpr float kFooterH = 16.f;
    constexpr float kScrollGutter = 12.f;
    constexpr float kToolButton = 22.f;
    constexpr float kSortButtonW = 84.f;
    constexpr float kSearchScale = 0.75f;
    constexpr GLubyte kBorderOpacity = 150;
    constexpr std::uintmax_t kMaxSnapshotBytes = 4 * 1024 * 1024;

    std::string formatStamp(std::time_t when, bool withDate) {
        std::tm tmv{};
#if defined(GEODE_IS_WINDOWS)
        localtime_s(&tmv, &when);
#else
        localtime_r(&when, &tmv);
#endif
        char buf[24];
        std::strftime(buf, sizeof(buf), withDate ? "%d/%m %H:%M" : "%H:%M", &tmv);
        return buf;
    }

    GLubyte stepChannel(GLubyte cur, GLubyte target, float k) {
        int diff = static_cast<int>(target) - static_cast<int>(cur);
        if (diff == 0) return cur;
        int step = static_cast<int>(std::lround(static_cast<float>(diff) * k));
        // integer rounding would otherwise stall a few units short of the target.
        if (step == 0) step = diff > 0 ? 1 : -1;
        return static_cast<GLubyte>(static_cast<int>(cur) + step);
    }

    ccColor3B stepColor(ccColor3B cur, ccColor3B target, float k) {
        return {stepChannel(cur.r, target.r, k), stepChannel(cur.g, target.g, k),
                stepChannel(cur.b, target.b, k)};
    }

    bool sameColor(ccColor3B a, ccColor3B b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    struct CachedModEntry {
        std::string username;
        std::string role;
        int accountID = 0;
    };
    static std::vector<CachedModEntry> s_cachedModEntries;
    // intentionally heap-allocated: ref<> releasing ccobject after
    // ccpoolmanager is gone would crash at atexit.
    static Ref<CCArray>& getCachedModScores() {
        static auto* s_ptr = new Ref<CCArray>();
        return *s_ptr;
    }
    static std::time_t s_modCacheTimestamp = 0;
    static bool s_modCacheValid = false;
    static constexpr std::time_t k_modCacheTTL = 900;

    // shares the score cache lifetime: both hold the same gjuserscore objects.
    std::unordered_set<std::string>& iconReadyNames() {
        static auto* names = new std::unordered_set<std::string>();
        return *names;
    }

    bool isIconReady(std::string const& key) {
        return iconReadyNames().count(key) > 0;
    }

    // gjuserscore::create() leaves fields untouched; set every field the cell reads.
    void fillPlaceholderScore(GJUserScore* score, std::string const& username, bool admin, int accountID) {
        score->m_userName = username;
        score->m_userID = 0;
        score->m_accountID = accountID;
        score->m_stars = 0;
        score->m_moons = 0;
        score->m_diamonds = 0;
        score->m_demons = 0;
        score->m_secretCoins = 0;
        score->m_userCoins = 0;
        score->m_creatorPoints = 0;
        score->m_globalRank = 0;
        score->m_playerRank = 0;
        score->m_color1 = 0;
        score->m_color2 = 3;
        score->m_color3 = 0;
        score->m_special = 0;
        score->m_glowEnabled = false;
        score->m_iconType = IconType::Cube;
        score->m_iconID = 1;
        score->m_playerCube = 1;
        score->m_modBadge = admin ? 2 : 1;
    }

    void normalizeIconID(GJUserScore* score) {
        if (!score) return;
        if (score->m_iconID <= 0) {
            switch (score->m_iconType) {
                case IconType::Cube: score->m_iconID = score->m_playerCube; break;
                case IconType::Ship: score->m_iconID = score->m_playerShip; break;
                case IconType::Ball: score->m_iconID = score->m_playerBall; break;
                case IconType::Ufo: score->m_iconID = score->m_playerUfo; break;
                case IconType::Wave: score->m_iconID = score->m_playerWave; break;
                case IconType::Robot: score->m_iconID = score->m_playerRobot; break;
                case IconType::Spider: score->m_iconID = score->m_playerSpider; break;
                case IconType::Swing: score->m_iconID = score->m_playerSwing; break;
                case IconType::Jetpack: score->m_iconID = score->m_playerJetpack; break;
                default: score->m_iconID = score->m_playerCube; break;
            }
        }
        if (score->m_iconID <= 0) score->m_iconID = 1;
    }

    void applyGdScore(GJUserScore* target, GJUserScore* source) {
        if (!target || !source) return;

        int existingModBadge = target->m_modBadge;

        if (!static_cast<std::string>(source->m_userName).empty()) {
            target->m_userName = source->m_userName;
        }
        target->m_userID = source->m_userID;
        target->m_accountID = source->m_accountID;
        target->m_stars = source->m_stars;
        target->m_moons = source->m_moons;
        target->m_diamonds = source->m_diamonds;
        target->m_demons = source->m_demons;
        target->m_playerRank = source->m_playerRank;
        target->m_creatorPoints = source->m_creatorPoints;
        target->m_secretCoins = source->m_secretCoins;
        target->m_userCoins = source->m_userCoins;
        target->m_color1 = source->m_color1;
        target->m_color2 = source->m_color2;
        target->m_color3 = source->m_color3;
        target->m_special = source->m_special;
        target->m_iconType = source->m_iconType;
        target->m_playerCube = source->m_playerCube;
        target->m_playerShip = source->m_playerShip;
        target->m_playerBall = source->m_playerBall;
        target->m_playerUfo = source->m_playerUfo;
        target->m_playerWave = source->m_playerWave;
        target->m_playerRobot = source->m_playerRobot;
        target->m_playerSpider = source->m_playerSpider;
        target->m_playerSwing = source->m_playerSwing;
        target->m_playerJetpack = source->m_playerJetpack;
        target->m_playerStreak = source->m_playerStreak;
        target->m_playerExplosion = source->m_playerExplosion;
        target->m_glowEnabled = source->m_glowEnabled;
        target->m_globalRank = source->m_globalRank;
        target->m_iconID = source->m_iconID;

        normalizeIconID(target);
        target->m_modBadge = existingModBadge;
    }

    Ref<GJUserScore> parseUserInfo(std::string const& body, int fallbackAccountID) {
        if (body.empty() || body == "-1") return nullptr;

        auto* dict = GameLevelManager::responseToDict(body, false);
        if (!dict) dict = GameLevelManager::responseToDict(body, true);
        if (!dict) return nullptr;

        auto* score = GJUserScore::create(dict);
        if (!score) return nullptr;

        if (score->m_accountID <= 0) score->m_accountID = fallbackAccountID;
        normalizeIconID(score);
        return score;
    }

    std::string accountIDKey(std::string const& username) {
        return fmt::format("gd-accid-{}", geode::utils::string::toLower(username));
    }

    int rememberedAccountID(std::string const& username) {
        return static_cast<int>(Mod::get()->getSavedValue<int64_t>(accountIDKey(username), 0));
    }

    void rememberAccountID(std::string const& username, int accountID) {
        if (accountID > 0) Mod::get()->setSavedValue<int64_t>(accountIDKey(username), accountID);
    }

    void fitLabel(CCLabelBMFont* label, float maxWidth) {
        if (!label || maxWidth <= 0.f) return;
        float width = label->getScaledContentSize().width;
        if (width > maxWidth) label->setScale(label->getScale() * (maxWidth / width));
    }
}

CommunityHubLayer* CommunityHubLayer::create() {
    auto ret = new CommunityHubLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* CommunityHubLayer::scene() {
    if (!paimon::modules::isEnabled("paimbnails.community.menu")) return nullptr;
    auto scene = CCScene::create();
    scene->addChild(CommunityHubLayer::create());
    return scene;
}

CommunityHubLayer::~CommunityHubLayer() {
    log::info("[CommunityHub] destroyed");
    this->unscheduleAllSelectors();
    hideLoading();
    for (auto& entry : m_thumbnailEntries) {
        if (entry.levelId > 0) ThumbnailLoader::get().cancelLoad(entry.levelId);
    }
    clearList();
    removeCaveEffect();
}

bool CommunityHubLayer::init() {
    if (!CCLayer::init()) return false;
    log::info("[CommunityHub] init");

    buildChrome();
    showLoading();

    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(0);
#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
    this->setKeyboardEnabled(true);
#endif

    applyCaveEffect();
    this->scheduleUpdate();

    loadTab(Tab::Moderators);
    playIntro();
    return true;
}

void CommunityHubLayer::buildChrome() {
    auto winSize = CCDirector::get()->getWinSize();
    auto& loc = Localization::get();

    if (!LayerBackgroundManager::get().applyBackground(this, "community_hub")) {
        paimon::ui::decorateScene(this);
    }

    m_accent = m_accentTarget = accentFor(Tab::Moderators);
    m_fxLayer = CCNode::create();
    this->addChild(m_fxLayer, 0);

    // one card holds the tabs, toolbar, list and status line; the header floats above it.
    float cardW = std::min(410.f, winSize.width - 110.f);
    float cardTop = winSize.height - 50.f;
    float cardBottom = 10.f;
    float cardH = cardTop - cardBottom;
    m_cardRect = CCRect(winSize.width / 2.f - cardW / 2.f, cardBottom, cardW, cardH);

    m_tabBaseY = cardTop - kCardPad - kTabsH / 2.f;
    m_toolbarY = m_tabBaseY - kTabsH / 2.f - 5.f - kToolbarH / 2.f;
    float listTop = m_toolbarY - kToolbarH / 2.f - 5.f;
    float listBottom = cardBottom + kFooterH + 4.f;
    m_listW = cardW - kCardPad * 2.f - kScrollGutter;
    m_listH = listTop - listBottom;
    m_listCenter = ccp(m_cardRect.getMinX() + kCardPad + m_listW / 2.f, listBottom + m_listH / 2.f);

    float headerY = winSize.height - 19.f;
    m_subtitleY = winSize.height - 39.f;
    m_title = CCLabelBMFont::create(loc.getString("community.title").c_str(), "goldFont.fnt");
    m_title->setScale(0.8f);
    m_title->limitLabelWidth(std::max(120.f, winSize.width - 160.f), 0.8f, 0.4f);
    m_title->setPosition({winSize.width / 2.f, headerY});
    this->addChild(m_title, 10);

    m_card = CCNodeRGBA::create();
    m_card->setCascadeOpacityEnabled(true);
    m_card->setContentSize({cardW, cardH});
    m_card->setAnchorPoint({0.5f, 0.5f});
    m_card->setPosition({m_cardRect.getMidX(), m_cardRect.getMidY()});
    this->addChild(m_card, 1);
    m_card->addChild(paimon::ui::makeInset({cardW, cardH}, 125), 0);

    // recessed track behind the segmented tabs.
    auto* track = paimon::ui::makeInset({cardW - kCardPad * 2.f, kTabsH + 4.f}, 110);
    track->setPosition({kCardPad, cardH - kCardPad - kTabsH - 2.f});
    m_card->addChild(track, 1);

    m_listFrame = CCNode::create();
    m_listFrame->setContentSize({m_listW, m_listH});
    m_listFrame->setAnchorPoint({0.5f, 0.5f});
    m_listFrame->setPosition(m_listCenter);
    this->addChild(m_listFrame, 2);
    m_listFrame->addChild(paimon::ui::makeInset({m_listW, m_listH}, 70));

    // accent outline around the card; follows the active tab color.
    if (auto* border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
        border->setContentSize({cardW + 4.f, cardH + 4.f});
        border->setAnchorPoint({0.5f, 0.5f});
        border->setPosition({m_cardRect.getMidX(), m_cardRect.getMidY()});
        border->setColor(m_accent);
        border->setOpacity(kBorderOpacity);
        this->addChild(border, 6);
        m_listBorder = border;
    }
    auto menu = CCMenu::create();
    menu->setPosition(0, 0);
    menu->setZOrder(20);
    this->addChild(menu);

    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this,
        menu_selector(CommunityHubLayer::onBack)
    );
    backBtn->setPosition(25, winSize.height - 25);
    menu->addChild(backBtn);

    CCNode* infoFace = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png");
    if (infoFace) {
        infoFace->setScale(0.8f);
    } else {
        auto fallback = CCLabelBMFont::create("?", "bigFont.fnt");
        fallback->setScale(0.5f);
        infoFace = fallback;
    }
    m_infoButton = CCMenuItemSpriteExtra::create(
        infoFace, this, menu_selector(CommunityHubLayer::onInfoButton));
    // top-right keeps it clear of the side art in the bottom corners.
    m_infoButton->setPosition({winSize.width - 25.f, winSize.height - 25.f});
    menu->addChild(m_infoButton);

    m_tabsMenu = CCMenu::create();
    m_tabsMenu->setPosition(0, 0);
    m_tabsMenu->setZOrder(10);
    this->addChild(m_tabsMenu);

    constexpr float kSegGap = 2.f;
    float trackW = cardW - kCardPad * 2.f - 4.f;
    float segW = (trackW - kSegGap * 3.f) / 4.f;
    float firstX = m_cardRect.getMinX() + kCardPad + 2.f + segW / 2.f;

    char const* ids[] = {"mods", "creators", "thumbnails", "compat_mods"};
    char const* keys[] = {
        "community.tab_mods", "community.tab_creators",
        "community.tab_thumbnails", "community.tab_compat_mods"
    };

    for (int i = 0; i < 4; i++) {
        auto tab = createTabButton(loc.getString(keys[i]), ids[i],
            {firstX + (segW + kSegGap) * static_cast<float>(i), m_tabBaseY}, segW);
        m_tabsMenu->addChild(tab);
    }

    m_tabs.front()->toggle(true);
    m_tabs.front()->setZOrder(2);

    buildHeader();
    buildToolbar();
    buildFooter();
    updateTabHeader();
}

void CommunityHubLayer::buildHeader() {
    auto winSize = CCDirector::get()->getWinSize();
    m_subtitle = CCLabelBMFont::create("", "chatFont.fnt");
    m_subtitle->setScale(0.5f);
    m_subtitle->setColor({205, 205, 215});
    m_subtitle->setOpacity(220);
    m_subtitle->setPosition({winSize.width / 2.f, m_subtitleY});
    this->addChild(m_subtitle, 10);
}

char const* CommunityHubLayer::tabIconFrame(Tab tab) const {
    switch (tab) {
        case Tab::Moderators: return "GJ_hammerIcon_001.png";
        case Tab::TopCreators: return "GJ_creatorBtn_001.png";
        case Tab::TopThumbnails: return "GJ_bigStar_001.png";
        case Tab::CompatibleMods: return "gj_folderBtn_001.png";
    }
    return "GJ_infoIcon_001.png";
}

std::string CommunityHubLayer::tabSubtitle(Tab tab) const {
    auto& loc = Localization::get();
    switch (tab) {
        case Tab::Moderators: return loc.getString("community.sub_mods");
        case Tab::TopCreators: return loc.getString("community.sub_creators");
        case Tab::TopThumbnails: return loc.getString("community.sub_thumbs");
        case Tab::CompatibleMods: return loc.getString("community.sub_compat");
    }
    return "";
}

ccColor3B CommunityHubLayer::accentFor(Tab tab) const {
    switch (tab) {
        case Tab::Moderators: return {170, 130, 255};
        case Tab::TopCreators: return {255, 200, 80};
        case Tab::TopThumbnails: return {255, 125, 175};
        case Tab::CompatibleMods: return {110, 225, 150};
    }
    return {255, 255, 255};
}

void CommunityHubLayer::updateTabHeader() {
    m_accentTarget = accentFor(m_currentTab);
    if (m_subtitle) {
        std::string next = tabSubtitle(m_currentTab);
        std::string prev = m_subtitle->getString();
        bool changed = next != prev;
        m_subtitle->setString(next.c_str());
        // first fill belongs to playIntro, which reads the node position as its destination.
        if (changed && !prev.empty() && paimon::ui::motionEnabled()) {
            auto base = ccp(CCDirector::get()->getWinSize().width / 2.f, m_subtitleY);
            m_subtitle->stopAllActions();
            m_subtitle->setPosition({base.x, base.y - 5.f});
            m_subtitle->setOpacity(0);
            m_subtitle->runAction(CCFadeTo::create(0.28f, 220));
            m_subtitle->runAction(CCEaseSineOut::create(CCMoveTo::create(0.28f, base)));
        }
    }
    // creators/thumbnails are the only tabs with a meaningful sort toggle.
    bool sortable = m_currentTab == Tab::TopCreators || m_currentTab == Tab::TopThumbnails;
    if (m_sortButton) m_sortButton->setVisible(sortable);
    layoutToolbar(sortable);
}

void CommunityHubLayer::buildToolbar() {
    auto& loc = Localization::get();
    float left = m_cardRect.getMinX() + kCardPad;
    float right = m_cardRect.getMaxX() - kCardPad;
    float y = m_toolbarY;

    // custom field: the input's own background is hidden so the glass can sit inside it.
    m_searchBg = paimon::ui::makeInset({120.f, kToolbarH}, 140);
    m_searchBg->setPosition({left, y - kToolbarH / 2.f});
    this->addChild(m_searchBg, 10);

    if (auto* glass = paimon::SpriteHelper::createWithFallback("gj_findBtn_001.png", "GJ_searchBtn_001.png")) {
        glass->setScale(12.f / std::max(glass->getContentSize().height, 1.f));
        glass->setOpacity(170);
        glass->setPosition({left + 12.f, y});
        this->addChild(glass, 11);
    }

    m_searchInput = TextInput::create(120.f, loc.getString("community.search"), "chatFont.fnt");
    m_searchInput->setScale(kSearchScale);
    m_searchInput->setMaxCharCount(40);
    m_searchInput->setCommonFilter(CommonFilter::Text);
    m_searchInput->setAlignment(Label::Alignment::Left);
    if (auto* bg = m_searchInput->getBGSprite()) bg->setVisible(false);
    m_searchInput->setCallback([this](std::string const& text) { onSearchChanged(text); });
    this->addChild(m_searchInput, 11);

    m_toolbarMenu = CCMenu::create();
    m_toolbarMenu->setPosition(0, 0);
    m_toolbarMenu->setZOrder(12);
    this->addChild(m_toolbarMenu);
    auto* toolbarMenu = static_cast<CCMenu*>(m_toolbarMenu);

    auto* refreshSprite = CCSprite::createWithSpriteFrameName("GJ_updateBtn_001.png");
    refreshSprite->setScale(kToolButton / std::max(refreshSprite->getContentSize().height, 1.f));
    m_refreshButton = CCMenuItemSpriteExtra::create(
        refreshSprite, this, menu_selector(CommunityHubLayer::onRefresh));
    m_refreshButton->setPosition({right - kToolButton / 2.f, y});
    toolbarMenu->addChild(m_refreshButton);

    // sort pill shows the active order, so the toolbar never needs a floating caption.
    auto* sortFace = CCNode::create();
    sortFace->setContentSize({kSortButtonW, kToolbarH});
    sortFace->addChild(paimon::ui::makeInset({kSortButtonW, kToolbarH}, 140), 0);
    if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sortIcon_001.png")) {
        icon->setScale(12.f / std::max(icon->getContentSize().height, 1.f));
        icon->setPosition({12.f, kToolbarH / 2.f});
        sortFace->addChild(icon, 1);
    }
    m_sortLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_sortLabel->setAnchorPoint({0.f, 0.5f});
    m_sortLabel->setColor(paimon::ui::palette::muted);
    m_sortLabel->setPosition({22.f, kToolbarH / 2.f + 1.f});
    sortFace->addChild(m_sortLabel, 2);
    m_sortButton = CCMenuItemSpriteExtra::create(
        sortFace, this, menu_selector(CommunityHubLayer::onSort));
    m_sortButton->m_scaleMultiplier = 1.06f;
    m_sortButton->setPosition({right - kToolButton - 4.f - kSortButtonW / 2.f, y});
    toolbarMenu->addChild(m_sortButton);

    // appears once the list is scrolled; sits inside the list's bottom-right corner.
    if (auto* sprite = paimon::ui::makeCircleSprite("edit_upBtn_001.png",
            CircleBaseColor::Blue, CircleBaseSize::Small)) {
        sprite->setScale(0.6f);
        m_topButton = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(CommunityHubLayer::onScrollTop));
        m_topButton->setPosition({m_listCenter.x + m_listW / 2.f - 20.f, m_listCenter.y - m_listH / 2.f + 22.f});
        m_topButton->setScale(0.f);
        m_topButton->m_baseScale = 1.f;
        m_topButton->setEnabled(false);
        toolbarMenu->addChild(m_topButton, 5);
    }

    layoutToolbar(false);
}

// the field takes whatever the sort pill leaves free.
void CommunityHubLayer::layoutToolbar(bool sortable) {
    float left = m_cardRect.getMinX() + kCardPad;
    float right = m_cardRect.getMaxX() - kCardPad;
    float fieldRight = right - kToolButton - 4.f - (sortable ? kSortButtonW + 4.f : 0.f);
    float fieldW = std::max(80.f, fieldRight - left);
    if (m_searchBg) m_searchBg->setContentSize({fieldW, kToolbarH});
    if (m_searchInput) {
        float inputW = fieldW - 26.f;
        m_searchInput->setContentWidth(inputW / kSearchScale);
        m_searchInput->setPosition({left + 22.f + inputW / 2.f, m_toolbarY});
    }
}

void CommunityHubLayer::setSortLabel(std::string const& text) {
    if (!m_sortLabel) return;
    m_sortLabel->setString(text.c_str());
    m_sortLabel->setScale(0.32f);
    fitLabel(m_sortLabel, kSortButtonW - 28.f);
}

void CommunityHubLayer::buildFooter() {
    m_footerLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_footerLabel->setScale(0.42f);
    m_footerLabel->setColor({195, 200, 215});
    m_footerLabel->setOpacity(205);
    m_footerLabel->setPosition({m_cardRect.getMidX() + 5.f, m_cardRect.getMinY() + kFooterH / 2.f + 2.f});
    this->addChild(m_footerLabel, 10);
}

CCMenuItemToggler* CommunityHubLayer::createTabButton(std::string const& text, char const* id, CCPoint pos, float width) {
    float tabW = width;
    float tabH = kTabsH;

    Tab tab = Tab::Moderators;
    std::string ids = id;
    if (ids == "creators") tab = Tab::TopCreators;
    else if (ids == "thumbnails") tab = Tab::TopThumbnails;
    else if (ids == "compat_mods") tab = Tab::CompatibleMods;
    char const* iconFrame = tabIconFrame(tab);
    ccColor3B accent = accentFor(tab);

    // segmented control: the active segment is filled with its tab's accent, the rest sit on the track.
    auto buildFace = [&](bool active) -> CCNode* {
        auto node = CCNode::create();
        node->setContentSize({tabW, tabH});

        if (active) {
            node->addChild(paimon::ui::makeInset({tabW, tabH}, 235, accent), 0);
        }

        float textX = tabW / 2.f;
        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(iconFrame)) {
            float h = icon->getContentSize().height;
            if (h > 0.f) icon->setScale(13.f / h);
            icon->setPosition({12.f, tabH / 2.f});
            if (!active) icon->setOpacity(150);
            node->addChild(icon, 2);
            textX = tabW / 2.f + 7.f;
        }

        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setScale(0.32f);
        fitLabel(label, tabW - 28.f);
        label->setPosition({textX, tabH / 2.f + 1.f});
        label->setColor(active ? ccColor3B{255, 255, 255} : ccColor3B{175, 182, 200});
        if (!active) label->setOpacity(200);
        node->addChild(label, 1);

        return node;
    };

    auto toggler = CCMenuItemToggler::create(
        buildFace(false), buildFace(true), this, menu_selector(CommunityHubLayer::onTab));
    toggler->setUserObject(CCString::create(id));
    toggler->setPosition(pos);
    m_tabs.push_back(toggler);
    return toggler;
}

void CommunityHubLayer::playIntro() {
    if (!paimon::ui::motionEnabled()) return;
    if (m_title) {
        auto target = m_title->getPosition();
        m_title->setPosition({target.x, target.y + 34.f});
        m_title->setOpacity(0);
        m_title->runAction(CCEaseBackOut::create(CCMoveTo::create(0.5f, target)));
        m_title->runAction(CCFadeIn::create(0.3f));
    }

    for (size_t i = 0; i < m_tabs.size(); i++) {
        auto* tab = m_tabs[i];
        tab->setScale(0.f);
        tab->runAction(CCSequence::create(
            CCDelayTime::create(0.06f * static_cast<float>(i)),
            CCEaseBackOut::create(CCScaleTo::create(0.35f, 1.f)),
            nullptr));
    }

    if (m_card) {
        m_card->setOpacity(0);
        m_card->runAction(CCFadeIn::create(0.25f));
    }
    if (m_listFrame) {
        m_listFrame->setScale(0.96f);
        m_listFrame->runAction(CCEaseBackOut::create(CCScaleTo::create(0.4f, 1.f)));
    }
    if (m_searchBg) {
        m_searchBg->setOpacity(0);
        m_searchBg->runAction(CCSequence::create(
            CCDelayTime::create(0.1f), CCFadeTo::create(0.25f, 140), nullptr));
    }

    paimon::ui::animateIn(m_subtitle, 0.1f);
    paimon::ui::animateIn(m_footerLabel, 0.2f);

    if (m_listBorder) {
        m_listBorder->setOpacity(0);
        m_listBorder->runAction(CCSequence::create(
            CCDelayTime::create(0.2f), CCFadeTo::create(0.4f, kBorderOpacity), nullptr));
    }
}

void CommunityHubLayer::onExit() {
    m_isExiting = true;
    ++m_retryTag;
    this->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
    this->unschedule(schedule_selector(CommunityHubLayer::onIconTick));
    this->unscheduleUpdate();
    removeCaveEffect();
    CCLayer::onExit();
}

void CommunityHubLayer::onEnterTransitionDidFinish() {
    CCLayer::onEnterTransitionDidFinish();
    applyCaveEffect();
}

void CommunityHubLayer::update(float dt) {
    if (m_isExiting) return;
    if (!m_caveApplied) applyCaveEffect();

    dt = std::min(dt, 0.1f);
    updateAccent(dt);

    if (paimon::ui::motionEnabled() && m_fxLayer) {
        m_fxClock += dt;
        if (m_fxClock >= 0.5f) {
            m_fxClock = 0.f;
            if (static_cast<int>(m_fxLayer->getChildrenCount()) < kMaxParticles) spawnParticle();
        }
    }

    updateScrollTop();
    updateHover(dt);
}

void CommunityHubLayer::updateAccent(float dt) {
    m_accent = stepColor(m_accent, m_accentTarget, std::min(1.f, dt * 7.f));
    if (sameColor(m_accent, m_appliedAccent)) return;
    m_appliedAccent = m_accent;
    if (m_listBorder) m_listBorder->setColor(m_accent);
}

void CommunityHubLayer::spawnParticle() {
    if (!m_fxLayer) return;
    static char const* const frames[] = {
        "GJ_starsIcon_001.png", "particle_01_001.png", "particle_02_001.png"};
    auto* particle = paimon::SpriteHelper::safeCreateWithFrameName(frames[std::rand() % 3]);
    if (!particle) return;

    auto winSize = CCDirector::get()->getWinSize();
    particle->setColor(m_accent);
    particle->setScale(0.07f + static_cast<float>(std::rand() % 12) / 100.f);
    particle->setPosition({static_cast<float>(std::rand() % static_cast<int>(winSize.width)), -8.f});
    particle->setOpacity(0);
    m_fxLayer->addChild(particle);

    float duration = 6.f + static_cast<float>(std::rand() % 40) / 10.f;
    float drift = static_cast<float>((std::rand() % 100) - 50) * 0.6f;
    auto peak = static_cast<GLubyte>(60 + std::rand() % 80);
    particle->runAction(CCSpawn::create(
        CCMoveBy::create(duration, {drift, winSize.height + 16.f}),
        CCRotateBy::create(duration, (std::rand() % 2 ? 1.f : -1.f) * (30.f + static_cast<float>(std::rand() % 60))),
        CCSequence::create(
            CCFadeTo::create(0.8f, peak),
            CCDelayTime::create(duration - 2.3f),
            CCFadeTo::create(1.5f, 0),
            CCRemoveSelf::create(),
            nullptr),
        nullptr));
}

void CommunityHubLayer::updateScrollTop() {
    if (!m_topButton) return;
    bool want = m_scrollView && std::abs(m_scrollView->getScrollFromTop()) > kTopButtonThreshold;
    if (want == m_topShown) return;
    m_topShown = want;
    m_topButton->setEnabled(want);
    m_topButton->stopAllActions();
    if (!paimon::ui::motionEnabled()) {
        m_topButton->setScale(want ? 1.f : 0.f);
        return;
    }
    m_topButton->runAction(want
        ? static_cast<CCActionInterval*>(CCEaseBackOut::create(CCScaleTo::create(0.25f, 1.f)))
        : static_cast<CCActionInterval*>(CCEaseBackIn::create(CCScaleTo::create(0.2f, 0.f))));
}

void CommunityHubLayer::onScrollTop(CCObject*) {
    if (m_scrollView) m_scrollView->scrollToTop();
}

void CommunityHubLayer::registerHoverRow(CCNode* cell) {
    if (!cell) return;
    auto* glow = CCLayerColor::create(ccc4(m_accent.r, m_accent.g, m_accent.b, 0));
    glow->setContentSize(cell->getContentSize());
    glow->setCascadeOpacityEnabled(false);
    cell->addChild(glow, 2);
    m_hoverRows.push_back({glow, cell});
}

void CommunityHubLayer::updateHover(float dt) {
#if defined(GEODE_IS_DESKTOP)
    if (m_hoverRows.empty() || !m_listFrame) return;

    auto mouse = geode::cocos::getMousePos();
    auto local = m_listFrame->convertToNodeSpace(mouse);
    bool inList = local.x >= 0.f && local.x <= m_listW && local.y >= 0.f && local.y <= m_listH;

    float const speed = std::min(1.f, dt * 14.f);
    for (auto& row : m_hoverRows) {
        if (!row.cell || !row.glow || !row.cell->getParent()) continue;
        bool hot = false;
        if (inList) {
            auto p = row.cell->convertToNodeSpace(mouse);
            auto size = row.cell->getContentSize();
            hot = p.x >= 0.f && p.x <= size.width && p.y >= 0.f && p.y <= size.height;
        }
        int cur = row.glow->getOpacity();
        int target = hot ? 46 : 0;
        if (cur == target) continue;
        int step = static_cast<int>(std::lround(static_cast<float>(target - cur) * speed));
        if (step == 0) step = target > cur ? 1 : -1;
        row.glow->setColor(m_accent);
        row.glow->setOpacity(static_cast<GLubyte>(cur + step));
    }
#else
    (void)dt;
#endif
}

void CommunityHubLayer::keyDown(enumKeyCodes key, double timestamp) {
    auto* kb = CCDirector::get()->getKeyboardDispatcher();
    // dialogs and popups sit in the same scene; leave keys to them.
    if (auto* scene = CCDirector::get()->getRunningScene()) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (typeinfo_cast<FLAlertLayer*>(child)) {
                CCLayer::keyDown(key, timestamp);
                return;
            }
        }
    }
    bool typing = m_searchInput && m_searchInput->isFocused();

    if (kb && kb->getControlKeyPressed() && key == KEY_F && m_searchInput) {
        m_searchInput->focus();
        return;
    }
    if (typing) {
        CCLayer::keyDown(key, timestamp);
        return;
    }

    int current = static_cast<int>(m_currentTab);
    switch (key) {
        case KEY_Left: selectTab(static_cast<Tab>((current + 3) % 4)); return;
        case KEY_Right: selectTab(static_cast<Tab>((current + 1) % 4)); return;
        case KEY_One: selectTab(Tab::Moderators); return;
        case KEY_Two: selectTab(Tab::TopCreators); return;
        case KEY_Three: selectTab(Tab::TopThumbnails); return;
        case KEY_Four: selectTab(Tab::CompatibleMods); return;
        case KEY_F5: if (m_refreshButton && m_refreshButton->isVisible()) onRefresh(nullptr); return;
        default: break;
    }
    CCLayer::keyDown(key, timestamp);
}

void CommunityHubLayer::applyCaveEffect() {
    if (m_caveApplied) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return;
    if (engine->m_backgroundMusicChannel->getVolume(&m_savedBgVolume) != FMOD_OK) return;

    engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume * 0.55f);
    paimon::audio::attachCaveEffects(engine, m_lowpassDSP, m_reverbDSP);
    m_caveApplied = true;
}

void CommunityHubLayer::removeCaveEffect() {
    auto engine = FMODAudioEngine::sharedEngine();
    paimon::audio::releaseCaveEffects(engine, m_lowpassDSP, m_reverbDSP);
    if (m_caveApplied && engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(m_savedBgVolume);
    }
    m_caveApplied = false;
}

void CommunityHubLayer::onBack(CCObject*) {
    m_isExiting = true;
    ++m_retryTag;
    this->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
    this->unschedule(schedule_selector(CommunityHubLayer::onIconTick));
    this->unscheduleUpdate();
    removeCaveEffect();
    CCDirector::get()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

void CommunityHubLayer::keyBackClicked() {
    onBack(nullptr);
}

void CommunityHubLayer::onTab(CCObject* sender) {
    auto toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;
    auto typeObj = typeinfo_cast<CCString*>(toggler->getUserObject());
    if (!typeObj) return;
    std::string type = typeObj->getCString();

    Tab newTab = Tab::Moderators;
    if (type == "creators") newTab = Tab::TopCreators;
    else if (type == "thumbnails") newTab = Tab::TopThumbnails;
    else if (type == "compat_mods") newTab = Tab::CompatibleMods;

    if (m_currentTab == newTab) {
        toggler->toggle(true);
        return;
    }
    selectTab(newTab);
}

void CommunityHubLayer::selectTab(Tab newTab) {
    size_t index = static_cast<size_t>(newTab);
    if (m_currentTab == newTab || index >= m_tabs.size()) return;
    // tabs are disabled while a load is in flight.
    if (!m_tabs[index]->isEnabled()) return;
    auto* toggler = m_tabs[index];

    m_slideDir = newTab > m_currentTab ? 1 : -1;

    // leaving thumbnails tab: drop pending downloads to free loader slots.
    if (m_currentTab == Tab::TopThumbnails) {
        for (auto& entry : m_thumbnailEntries) {
            if (entry.levelId > 0) ThumbnailLoader::get().cancelLoad(entry.levelId);
        }
    }

    m_currentTab = newTab;
    m_sortMode = 0;
    m_searchFilter.clear();
    if (m_searchInput) m_searchInput->setString("");
    this->unschedule(schedule_selector(CommunityHubLayer::onIconTick));
    if (m_infoButton) m_infoButton->setTag(static_cast<int>(newTab));
    updateTabHeader();

    for (auto* tab : m_tabs) {
        bool active = (tab == toggler);
        tab->toggle(active);
        tab->setEnabled(false);
        tab->setZOrder(active ? 2 : 1);
        tab->stopAllActions();
        tab->setScale(1.f);
        if (active && paimon::ui::motionEnabled()) {
            tab->setScale(0.9f);
            tab->runAction(CCEaseBackOut::create(CCScaleTo::create(0.3f, 1.f)));
        }
    }

    // let the outgoing list shrink away instead of popping out of existence.
    if (m_listContainer) {
        auto* leaving = m_listContainer;
        m_listContainer = nullptr;
        m_scrollView = nullptr;
        m_iconSlots.clear();
        m_hoverRows.clear();
        leaving->runAction(CCSequence::create(
            CCEaseSineIn::create(CCScaleTo::create(0.16f, 0.92f)),
            CCRemoveSelf::create(),
            nullptr));
    }

    showLoading();
    loadTab(newTab);
}

void CommunityHubLayer::clearList() {
    if (m_listContainer) {
        m_listContainer->removeFromParent();
        m_listContainer = nullptr;
    }
    m_scrollView = nullptr;
    m_iconSlots.clear();
    m_hoverRows.clear();
}

void CommunityHubLayer::showLoading() {
    hideLoading();
    m_loadingSpinner = PaimonLoadingOverlay::create(
        Localization::get().getString("community.loading").c_str(), 40.f);
    if (m_loadingSpinner) m_loadingSpinner->show(this, 100);
}

void CommunityHubLayer::hideLoading() {
    if (m_loadingSpinner) {
        m_loadingSpinner->dismiss();
        m_loadingSpinner = nullptr;
    }
}

void CommunityHubLayer::finishTabLoad() {
    for (auto* tab : m_tabs) tab->setEnabled(true);
    setToolbarBusy(false);
}

CCNode* CommunityHubLayer::beginList() {
    clearList();

    auto winSize = CCDirector::get()->getWinSize();
    m_listContainer = CCNode::create();
    m_listContainer->setContentSize(winSize);
    m_listContainer->setAnchorPoint({0.5f, 0.5f});
    m_listContainer->setPosition(winSize / 2.f);
    this->addChild(m_listContainer, 5);
    return m_listContainer;
}

CCNode* CommunityHubLayer::addScrollList(float contentHeight) {
    float totalH = std::max(contentHeight, m_listH);

    auto scroll = ScrollLayer::create({m_listW, m_listH});
    scroll->setPosition({m_listCenter.x - m_listW / 2.f, m_listCenter.y - m_listH / 2.f});
    scroll->m_contentLayer->setContentSize({m_listW, totalH});
    scroll->m_contentLayer->setPositionY(m_listH - totalH);
    m_listContainer->addChild(scroll);
    m_scrollView = scroll;

    if (auto* bar = Scrollbar::create(scroll)) {
        bar->setContentSize({6.f, m_listH - 8.f});
        bar->setPosition({m_listCenter.x + m_listW / 2.f + kScrollGutter / 2.f, m_listCenter.y});
        m_listContainer->addChild(bar, 10);
    }

    return scroll->m_contentLayer;
}

CCLayerColor* CommunityHubLayer::addCell(CCNode* content, float height, int index, float totalHeight) {
    auto cell = CCLayerColor::create(ccc4(0, 0, 0, index % 2 == 0 ? 110 : 55));
    cell->setContentSize({m_listW, height});
    cell->setPosition({0.f, totalHeight - static_cast<float>(index + 1) * height});
    // only row background fades; labels/icons ride at full opacity so late arrivals never half-draw.
    cell->setCascadeOpacityEnabled(false);
    content->addChild(cell);
    return cell;
}

void CommunityHubLayer::animateCellIn(CCLayerColor* cell, int index) {
    if (!cell) return;

    GLubyte target = cell->getOpacity();
    float delay = std::min(index, 12) * 0.035f;
    auto pos = cell->getPosition();
    cell->setPosition({pos.x + 24.f * static_cast<float>(m_slideDir), pos.y});
    cell->setOpacity(0);
    cell->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCEaseSineOut::create(CCMoveTo::create(0.28f, pos)),
        nullptr));
    cell->runAction(CCSequence::create(
        CCDelayTime::create(delay),
        CCFadeTo::create(0.22f, target),
        nullptr));
}

void CommunityHubLayer::showEmptyState(bool error) {
    if (!m_listContainer) return;
    auto& loc = Localization::get();
    bool filtered = !m_searchFilter.empty();
    std::string detail = error ? issueMessage() : std::string();
    float cx = m_listCenter.x;
    float y = m_listCenter.y + (detail.empty() ? 34.f : 46.f);

    // a lock reads better than a tinted tab icon when the mod itself is holding requests back.
    CCSprite* icon = nullptr;
    if (error && m_issue == LoadIssue::Locked) icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_lock_001.png");
    if (!icon) icon = paimon::SpriteHelper::safeCreateWithFrameName(tabIconFrame(m_currentTab));
    if (icon) {
        float h = icon->getContentSize().height;
        if (h > 0.f) icon->setScale(32.f / h);
        icon->setColor(error ? paimon::ui::palette::danger : m_accentTarget);
        icon->setOpacity(0);
        icon->setPosition({cx, y});
        m_listContainer->addChild(icon, 10);
        icon->runAction(CCFadeTo::create(0.4f, 150));
        if (paimon::ui::motionEnabled()) {
            icon->runAction(CCRepeatForever::create(CCSequence::create(
                CCEaseSineInOut::create(CCMoveBy::create(1.1f, {0.f, 3.f})),
                CCEaseSineInOut::create(CCMoveBy::create(1.1f, {0.f, -3.f})),
                nullptr)));
        }
    }
    y -= 32.f;

    char const* key = error ? "community.error" : (filtered ? "community.no_results" : "community.no_data");
    auto lbl = CCLabelBMFont::create(loc.getString(key).c_str(), "bigFont.fnt");
    lbl->setScale(0.45f);
    lbl->setColor(error ? ccColor3B{255, 170, 160} : ccColor3B{255, 255, 255});
    lbl->setOpacity(0);
    lbl->setPosition({cx, y});
    fitLabel(lbl, m_listW - 40.f);
    m_listContainer->addChild(lbl, 10);
    lbl->runAction(CCFadeTo::create(0.35f, 230));
    y -= 14.f;

    if (!detail.empty()) {
        auto* text = paimon::ui::makeText(detail.c_str(), m_listW - 80.f, 0.5f,
            paimon::ui::palette::muted, kCCTextAlignmentCenter);
        text->setAnchorPoint({0.5f, 1.f});
        text->setPosition({cx, y});
        text->setOpacity(0);
        m_listContainer->addChild(text, 10);
        text->runAction(CCFadeTo::create(0.35f, 220));
        y -= text->getScaledContentSize().height + 4.f;
    }

    if (!error && !filtered) return;
    auto menu = CCMenu::create();
    menu->setPosition(cx, y - 14.f);
    m_listContainer->addChild(menu, 11);
    auto* button = error
        ? paimon::ui::makeButton(loc.getString("community.retry").c_str(),
              [this] { onRefresh(nullptr); }, paimon::ui::Btn::Green, 110.f, 0.6f)
        : paimon::ui::makeButton(loc.getString("community.clear_search").c_str(),
              [this] { if (m_searchInput) m_searchInput->setString("", true); },
              paimon::ui::Btn::Blue, 130.f, 0.6f);
    menu->addChild(button);
    paimon::ui::animateIn(menu, 0.1f);
}

CCMenu* CommunityHubLayer::addRowMenu(CCNode* content, float totalH) {
    auto* menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    menu->setContentSize({m_listW, totalH});
    content->addChild(menu, 20);
    return menu;
}

// full-width invisible hit area over a row; presses barely scale so rows don't bounce.
CCMenuItemSpriteExtra* CommunityHubLayer::addRowButton(CCMenu* menu, float totalH, float cellH,
    int row, int tag, SEL_MenuHandler handler) {
    auto* hit = CCLayerColor::create({0, 0, 0, 0}, m_listW, cellH);
    auto* btn = CCMenuItemSpriteExtra::create(hit, this, handler);
    btn->setTag(tag);
    btn->m_scaleMultiplier = 1.015f;
    btn->setPosition({m_listW / 2.f, totalH - (static_cast<float>(row) + 0.5f) * cellH});
    menu->addChild(btn);
    return btn;
}

// thin track with a filled part; the fill grows in when motion is on.
CCNode* CommunityHubLayer::makeMetricBar(float width, float fraction, ccColor3B color) {
    constexpr float kBarH = 4.f;
    fraction = std::clamp(fraction, 0.f, 1.f);
    auto* node = CCNode::create();
    node->setContentSize({width, kBarH});
    if (auto* track = paimon::SpriteHelper::createRoundedRect(width, kBarH, kBarH / 2.f,
            {0.f, 0.f, 0.f, 0.55f})) {
        node->addChild(track, 0);
    }
    float fillW = std::max(kBarH, width * fraction);
    if (auto* fill = paimon::SpriteHelper::createRoundedRect(fillW, kBarH, kBarH / 2.f,
            {color.r / 255.f, color.g / 255.f, color.b / 255.f, 0.95f})) {
        node->addChild(fill, 1);
        if (paimon::ui::motionEnabled()) {
            fill->setScaleX(0.f);
            fill->runAction(CCSequence::create(
                CCDelayTime::create(0.2f),
                CCEaseSineOut::create(CCScaleTo::create(0.5f, 1.f, 1.f)),
                nullptr));
        }
    }
    return node;
}

void CommunityHubLayer::loadTab(Tab tab) {
    // unschedule first: a stale retry timer would refire for the previous tab.
    this->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
    ++m_retryTag;
    m_loadFailed = false;
    m_compatLoadFailed = false;
    m_issue = LoadIssue::None;
    m_issueCode = 0;
    m_fromSnapshot = false;
    m_snapshotAt = 0;

    if (m_infoButton) m_infoButton->setTag(static_cast<int>(tab));

    switch (tab) {
        case Tab::Moderators: loadModerators(0); break;
        case Tab::TopCreators: loadTopCreators(0); break;
        case Tab::TopThumbnails: loadTopThumbnails(0); break;
        case Tab::CompatibleMods: loadCompatibleMods(); break;
    }
}

void CommunityHubLayer::retryLoadTab(Tab tab, int attempt) {
    if (m_isExiting || m_currentTab != tab) return;
    log::info("[CommunityHub] Retrying tab {} (attempt {})", static_cast<int>(tab), attempt);
    showLoading();
    switch (tab) {
        case Tab::Moderators: loadModerators(attempt); break;
        case Tab::TopCreators: loadTopCreators(attempt); break;
        case Tab::TopThumbnails: loadTopThumbnails(attempt); break;
        case Tab::CompatibleMods: loadCompatibleMods(); break;
    }
}

void CommunityHubLayer::scheduleRetry(Tab tab, int attempt) {
    m_pendingRetryTab = tab;
    m_pendingRetryAttempt = attempt;
    this->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
    this->scheduleOnce(schedule_selector(CommunityHubLayer::onRetryTimer), 1.5f * static_cast<float>(attempt));
}

void CommunityHubLayer::onRetryTimer(float) {
    retryLoadTab(m_pendingRetryTab, m_pendingRetryAttempt);
}

void CommunityHubLayer::loadModerators(int attempt) {
    if (attempt == 0 && s_modCacheValid && getCachedModScores() && getCachedModScores()->count() > 0) {
        if (std::time(nullptr) - s_modCacheTimestamp < k_modCacheTTL) {
            log::info("[CommunityHub] Moderators: instant load from cache");
            m_modEntries.clear();
            for (auto const& ce : s_cachedModEntries) {
                m_modEntries.push_back({ce.username, ce.role, ce.accountID});
            }
            m_modScores = getCachedModScores();
            m_lastLoadFromCache = true;
            hideLoading();
            sortModerators();
            buildModeratorsList();
            return;
        }
    }

    m_lastLoadFromCache = false;
    m_modEntries.clear();
    m_modScores = CCArray::create();
    m_iconStates.clear();

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    HttpClient::get().get("/api/moderators", [self, attempt, tag](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([self, attempt, tag, success, response]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;
            layer->handleTabResponse(Tab::Moderators, attempt, success, response);
        });
    });
}

void CommunityHubLayer::handleTabResponse(Tab tab, int attempt, bool success, std::string const& response) {
    LoadIssue issue = LoadIssue::BadData;
    int httpCode = 0;
    if (success) {
        auto parsed = matjson::parse(response);
        if (parsed.isOk() && parseTabData(tab, parsed.unwrap())) {
            saveSnapshot(tab, parsed.unwrap());
            if (tab == Tab::Moderators && !m_modEntries.empty()) cacheModerators();
            hideLoading();
            buildCurrentList();
            return;
        }
    } else {
        issue = classifyFailure(response, httpCode);
    }

    log::warn("[CommunityHub] tab {} failed (attempt {}, issue {}): {}", static_cast<int>(tab), attempt,
        static_cast<int>(issue), response.substr(0, 200));

    // a locked gate or a malformed body won't change in a few seconds; storage gets one more try.
    int maxRetries = 3;
    if (issue == LoadIssue::Locked || issue == LoadIssue::BadData) maxRetries = 0;
    else if (issue == LoadIssue::Storage) maxRetries = 1;
    if (attempt < maxRetries) {
        scheduleRetry(tab, attempt + 1);
        return;
    }

    m_issue = issue;
    m_issueCode = httpCode;
    std::time_t savedAt = 0;
    auto snapshot = loadSnapshot(tab, savedAt);
    if (snapshot && parseTabData(tab, *snapshot) && tabHasEntries(tab)) {
        log::info("[CommunityHub] tab {} showing the copy saved at {}", static_cast<int>(tab), formatStamp(savedAt, true));
        m_fromSnapshot = true;
        m_snapshotAt = savedAt;
        m_loadFailed = false;
    } else {
        clearTabData(tab);
        m_fromSnapshot = false;
        m_loadFailed = true;
    }
    hideLoading();
    buildCurrentList();
}

bool CommunityHubLayer::parseTabData(Tab tab, matjson::Value const& json) {
    switch (tab) {
        case Tab::Moderators:
            if (!parseModerators(json)) return false;
            prepareModeratorScores();
            return true;
        case Tab::TopCreators: return parseCreators(json);
        case Tab::TopThumbnails: return parseThumbnails(json);
        case Tab::CompatibleMods: return false;
    }
    return false;
}

bool CommunityHubLayer::parseModerators(matjson::Value const& json) {
    if (!json.isObject() || !json.contains("moderators") || !json["moderators"].isArray()) return false;
    auto arrRes = json["moderators"].asArray();
    if (!arrRes.isOk()) return false;
    m_modEntries.clear();
    for (auto const& item : arrRes.unwrap()) {
        ModEntry entry;
        entry.username = paimon::json::stringOr(item["username"]);
        entry.role = paimon::json::stringOr(item["role"], "mod");
        entry.accountID = paimon::json::integerOr<int>(item["accountID"]);
        if (!entry.username.empty()) m_modEntries.push_back(entry);
    }
    return true;
}

bool CommunityHubLayer::parseCreators(matjson::Value const& json) {
    if (!json.isObject() || !json.contains("creators") || !json["creators"].isArray()) return false;
    auto arrRes = json["creators"].asArray();
    if (!arrRes.isOk()) return false;
    m_creatorEntries.clear();
    for (auto const& item : arrRes.unwrap()) {
        CreatorEntry entry;
        entry.username = paimon::json::stringOr(item["username"], "Unknown");
        entry.accountID = paimon::json::integerOr<int>(item["accountID"]);
        entry.uploadCount = paimon::json::integerOr<int>(item["uploadCount"]);
        entry.avgRating = paimon::json::floatOr(item["avgRating"]);
        m_creatorEntries.push_back(entry);
    }
    return true;
}

bool CommunityHubLayer::parseThumbnails(matjson::Value const& json) {
    if (!json.isObject() || !json.contains("thumbnails") || !json["thumbnails"].isArray()) return false;
    auto arrRes = json["thumbnails"].asArray();
    if (!arrRes.isOk()) return false;
    m_thumbnailEntries.clear();
    for (auto const& item : arrRes.unwrap()) {
        ThumbnailEntry entry;
        entry.levelId = paimon::json::integerOr<int>(item["levelId"]);
        entry.rating = paimon::json::floatOr(item["rating"]);
        entry.count = paimon::json::integerOr<int>(item["count"]);
        entry.uploadedBy = paimon::json::stringOr(item["uploadedBy"], "Unknown");
        entry.accountID = paimon::json::integerOr<int>(item["accountID"]);
        if (entry.levelId > 0) m_thumbnailEntries.push_back(entry);
    }
    return true;
}

void CommunityHubLayer::prepareModeratorScores() {
    m_modScores = CCArray::create();
    m_iconStates.clear();
    for (auto const& entry : m_modEntries) {
        ModProfileCache::get().store(entry.username, entry.role);

        auto score = GJUserScore::create();
        int accountID = entry.accountID > 0 ? entry.accountID : rememberedAccountID(entry.username);
        fillPlaceholderScore(score, entry.username, entry.role == "admin", accountID);
        m_modScores->addObject(score);
        iconReadyNames().erase(geode::utils::string::toLower(entry.username));
    }
    sortModerators();
}

void CommunityHubLayer::clearTabData(Tab tab) {
    switch (tab) {
        case Tab::Moderators:
            m_modEntries.clear();
            m_modScores = CCArray::create();
            break;
        case Tab::TopCreators: m_creatorEntries.clear(); break;
        case Tab::TopThumbnails: m_thumbnailEntries.clear(); break;
        case Tab::CompatibleMods: m_compatMods.clear(); break;
    }
}

bool CommunityHubLayer::tabHasEntries(Tab tab) const {
    switch (tab) {
        case Tab::Moderators: return !m_modEntries.empty();
        case Tab::TopCreators: return !m_creatorEntries.empty();
        case Tab::TopThumbnails: return !m_thumbnailEntries.empty();
        case Tab::CompatibleMods: return !m_compatMods.empty();
    }
    return false;
}

CommunityHubLayer::LoadIssue CommunityHubLayer::classifyFailure(std::string const& response, int& httpCode) const {
    httpCode = 0;
    // HttpClient formats failures as "HTTP <code>: <body>".
    if (response.rfind("HTTP ", 0) == 0) {
        if (auto parsed = geode::utils::numFromString<int>(response.substr(5))) {
            httpCode = parsed.unwrap();
        }
    }
    if (response.find("STORAGE_ERROR") != std::string::npos ||
        response.find("download_cap_exceeded") != std::string::npos) {
        return LoadIssue::Storage;
    }
    if (httpCode <= 0) {
        // WebHelper answers with an empty response while BanGate denies the request.
        return paimon::ban::networkAuthorization() == 0 ? LoadIssue::Locked : LoadIssue::Network;
    }
    return LoadIssue::Server;
}

std::string CommunityHubLayer::issueMessage() const {
    auto& loc = Localization::get();
    switch (m_issue) {
        case LoadIssue::Locked: return loc.getString("community.issue_locked");
        case LoadIssue::Storage: return loc.getString("community.issue_storage");
        case LoadIssue::Network: return loc.getString("community.issue_network");
        case LoadIssue::Server:
            return m_issueCode > 0
                ? fmt::format("{} (HTTP {})", loc.getString("community.issue_server"), m_issueCode)
                : loc.getString("community.issue_server");
        case LoadIssue::BadData: return loc.getString("community.issue_data");
        case LoadIssue::None: break;
    }
    return "";
}

namespace {
    std::filesystem::path snapshotFile(char const* name) {
        return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "community-hub" / fmt::format("{}.json", name);
    }

    char const* snapshotName(int tab) {
        switch (tab) {
            case 0: return "moderators";
            case 1: return "top-creators";
            case 2: return "top-thumbnails";
            default: return nullptr;
        }
    }
}

void CommunityHubLayer::saveSnapshot(Tab tab, matjson::Value const& data) {
    char const* name = snapshotName(static_cast<int>(tab));
    if (!name) return;
    auto path = snapshotFile(name);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto doc = matjson::makeObject({
        {"savedAt", static_cast<int64_t>(std::time(nullptr))},
        {"data", data},
    });
    if (!paimon::file::writeAtomically(path, doc.dump(matjson::NO_INDENTATION))) {
        log::warn("[CommunityHub] could not save the {} snapshot", name);
    }
}

std::optional<matjson::Value> CommunityHubLayer::loadSnapshot(Tab tab, std::time_t& savedAt) const {
    savedAt = 0;
    char const* name = snapshotName(static_cast<int>(tab));
    if (!name) return std::nullopt;
    auto path = snapshotFile(name);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxSnapshotBytes) return std::nullopt;

    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto parsed = matjson::parse(text);
    if (!parsed.isOk()) return std::nullopt;
    auto doc = parsed.unwrap();
    if (!doc.isObject() || !doc.contains("data")) return std::nullopt;
    savedAt = static_cast<std::time_t>(doc["savedAt"].asInt().unwrapOr(0));
    return doc["data"];
}

void CommunityHubLayer::buildCurrentList() {
    switch (m_currentTab) {
        case Tab::Moderators: buildModeratorsList(); break;
        case Tab::TopCreators: buildCreatorsList(); break;
        case Tab::TopThumbnails: buildThumbnailsList(); break;
        case Tab::CompatibleMods: buildCompatibleModsList(); break;
    }
}

void CommunityHubLayer::sortModerators() {
    if (!m_modScores || m_modScores->count() == 0) return;

    std::vector<Ref<GJUserScore>> scores;
    scores.reserve(m_modScores->count());
    for (auto* obj : CCArrayExt<GJUserScore*>(m_modScores)) {
        if (obj) scores.push_back(obj);
    }

    std::stable_sort(scores.begin(), scores.end(), [](Ref<GJUserScore> const& a, Ref<GJUserScore> const& b) {
        return a->m_modBadge > b->m_modBadge;
    });

    m_modScores->removeAllObjects();
    for (auto& score : scores) m_modScores->addObject(score.data());
}

void CommunityHubLayer::buildModeratorsList() {
    finishTabLoad();
    beginList();

    if (!m_modScores || m_modScores->count() == 0) {
        showEmptyState(m_loadFailed);
        updateFooter(0, m_lastLoadFromCache);
        return;
    }

    std::vector<GJUserScore*> visible;
    for (auto* score : CCArrayExt<GJUserScore*>(m_modScores)) {
        if (!score) continue;
        if (rowMatchesFilter(std::string(score->m_userName))) visible.push_back(score);
    }
    if (visible.empty()) {
        showEmptyState();
        updateFooter(0, m_lastLoadFromCache);
        return;
    }

    float cellH = 48.f;
    float totalH = std::max(m_listH, cellH * static_cast<float>(visible.size()));
    auto* content = addScrollList(totalH);

    int i = 0;
    for (auto* score : visible) {
        std::string username = score->m_userName;
        std::string key = geode::utils::string::toLower(username);
        bool admin = score->m_modBadge == 2;
        float mid = cellH / 2.f;

        auto& state = m_iconStates.try_emplace(key, IconState{}).first->second;
        if (isIconReady(key)) state.done = true;

        auto* cell = addCell(content, cellH, i, totalH);
        registerHoverRow(cell);

        auto banner = CCNode::create();
        cell->addChild(banner, 1);

        auto accent = CCLayerColor::create(admin ? ccc4(255, 199, 62, 225) : ccc4(155, 116, 255, 205));
        accent->setContentSize({3.f, cellH});
        cell->addChild(accent, 4);

        // medal only in the natural order; a filtered subset has no meaningful podium.
        if (m_searchFilter.empty() && i < 3) {
            if (auto* medal = makeRankMedal(i + 1)) {
                float h = medal->getContentSize().height;
                if (h > 0.f) medal->setScale(20.f / h);
                medal->setPosition({19.f, mid});
                cell->addChild(medal, 7);
            }
        }
        auto posLbl = CCLabelBMFont::create(fmt::format("{}", i + 1).c_str(), "goldFont.fnt");
        posLbl->setScale(0.38f);
        posLbl->setPosition({19.f, mid});
        if (m_searchFilter.empty() && i < 3) posLbl->setVisible(false);
        cell->addChild(posLbl, 6);

        auto nameLbl = CCLabelBMFont::create(username.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.5f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        fitLabel(nameLbl, 150.f);
        float nameW = nameLbl->getScaledContentSize().width;

        float clickH = cellH - 6.f;
        auto iconSlot = CCNode::create();
        auto clickNode = CCNode::create();
        clickNode->setContentSize({34.f + nameW, clickH});
        iconSlot->setPosition({15.f, clickH / 2.f});
        clickNode->addChild(iconSlot, 5);
        nameLbl->setPosition({34.f, clickH / 2.f + 8.f});
        clickNode->addChild(nameLbl, 10);

        auto profileBtn = CCMenuItemSpriteExtra::create(
            clickNode, this, menu_selector(CommunityHubLayer::onModProfile));
        profileBtn->setTag(score->m_accountID);
        profileBtn->setAnchorPoint({0.f, 0.5f});
        profileBtn->setPosition({32.f, mid});
        profileBtn->m_scaleMultiplier = 1.04f;
        PaimonButtonHighlighter::registerButton(profileBtn);

        auto menu = CCMenu::create();
        menu->setPosition(CCPointZero);
        menu->setContentSize({m_listW, cellH});
        cell->addChild(menu, 6);
        menu->addChild(profileBtn);

        float badgeRight = 66.f + nameW;
        if (auto badge = CCSprite::create(admin ? "paim_Admin.png"_spr : "paim_Moderador.png"_spr)) {
            badge->setScale(15.f / badge->getContentSize().height);
            badge->setPosition({badgeRight + badge->getScaledContentSize().width / 2.f + 5.f, mid + 8.f});
            cell->addChild(badge, 8);
        }

        auto stats = CCNode::create();
        stats->setPosition({66.f, mid - 10.f});
        cell->addChild(stats, 8);

        auto rankSlot = CCNode::create();
        rankSlot->setPosition({m_listW - 12.f, mid});
        cell->addChild(rankSlot, 8);

        IconSlot slot;
        slot.score = score;
        slot.cell = cell;
        slot.icon = iconSlot;
        slot.banner = banner;
        slot.stats = stats;
        slot.rank = rankSlot;
        slot.button = profileBtn;
        slot.key = key;
        slot.username = username;
        m_iconSlots.push_back(slot);

        refreshSlot(key, false);
        applyBanner(key);
        queueBannerLoad(key);
        animateCellIn(cell, i);
        i++;
    }

    startIconPipeline();
    updateFooter(static_cast<int>(visible.size()), m_lastLoadFromCache);
}

void CommunityHubLayer::onModProfile(CCObject* sender) {
    int accountID = sender->getTag();
    if (accountID > 0) ProfilePage::create(accountID, false)->show();
}

CommunityHubLayer::IconSlot* CommunityHubLayer::findIconSlot(std::string const& key) {
    for (auto& slot : m_iconSlots) {
        if (slot.key == key) return &slot;
    }
    return nullptr;
}

// same build gjscorecell uses: icon-type frame, palette colors, glow per profile.
SimplePlayer* CommunityHubLayer::createIcon(GJUserScore* score, bool hasData) {
    if (!score) return nullptr;

    auto* gm = GameManager::sharedState();
    if (!hasData || !gm) {
        auto* placeholder = SimplePlayer::create(1);
        if (!placeholder) return nullptr;
        placeholder->setColor({105, 110, 125});
        placeholder->setSecondColor({60, 64, 76});
        placeholder->disableGlowOutline();
        float dim = std::max(placeholder->getContentSize().width, placeholder->getContentSize().height);
        if (dim > 0.f) placeholder->setScale(30.f / dim);
        return placeholder;
    }

    int iconID = score->m_iconID > 0 ? score->m_iconID : std::max(score->m_playerCube, 1);
    auto* player = SimplePlayer::create(iconID);
    if (!player) return nullptr;

    player->updatePlayerFrame(iconID, score->m_iconType);
    player->setColor(gm->colorForIdx(score->m_color1));
    player->setSecondColor(gm->colorForIdx(score->m_color2));
    if (score->m_glowEnabled) {
        player->setGlowOutline(gm->colorForIdx(score->m_color3 > 0 ? score->m_color3 : score->m_color2));
    } else {
        player->disableGlowOutline();
    }

    float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
    if (maxDim > 0.f) player->setScale(30.f / maxDim);
    return player;
}

void CommunityHubLayer::refreshSlot(std::string const& key, bool animated) {
    auto* slot = findIconSlot(key);
    if (!slot || !slot->cell || !slot->cell->getParent()) return;

    auto* score = slot->score.data();
    bool ready = isIconReady(key);

    if (slot->icon) {
        slot->icon->removeAllChildren();
        if (auto* player = createIcon(score, ready)) {
            slot->icon->addChild(player);
            float base = player->getScale();
            if (animated) {
                player->setScale(base * 0.4f);
                player->runAction(CCEaseBackOut::create(CCScaleTo::create(0.34f, base)));
            } else if (!ready) {
                player->runAction(CCRepeatForever::create(CCSequence::create(
                    CCEaseSineInOut::create(CCScaleTo::create(0.7f, base * 0.86f)),
                    CCEaseSineInOut::create(CCScaleTo::create(0.7f, base)),
                    nullptr)));
            }
        }
    }

    if (slot->button) slot->button->setTag(score->m_accountID);

    if (slot->rank) {
        slot->rank->removeAllChildren();
        if (ready && score->m_globalRank > 0) {
            auto rankLbl = CCLabelBMFont::create(
                fmt::format("#{}", score->m_globalRank).c_str(), "goldFont.fnt");
            rankLbl->setScale(0.36f);
            rankLbl->setAnchorPoint({1.f, 0.5f});
            slot->rank->addChild(rankLbl);
            if (animated) {
                rankLbl->setOpacity(0);
                rankLbl->runAction(CCFadeIn::create(0.3f));
            }
        }
    }

    if (!slot->stats) return;
    slot->stats->removeAllChildren();
    slot->stats->setScale(1.f);

    if (!ready) {
        auto spinner = LoadingSpinner::create(11.f);
        spinner->setPosition({7.f, 0.f});
        slot->stats->addChild(spinner);
        return;
    }

    struct StatEntry {
        int value;
        char const* frame;
    };
    StatEntry entries[] = {
        {score->m_stars, "GJ_starsIcon_001.png"},
        {score->m_moons, "GJ_moonsIcon_001.png"},
        {score->m_diamonds, "GJ_diamondsIcon_001.png"},
        {score->m_secretCoins, "GJ_coinsIcon_001.png"},
        {score->m_userCoins, "GJ_coinsIcon2_001.png"},
        {score->m_demons, "GJ_demonIcon_001.png"},
    };

    float x = 0.f;
    int shown = 0;
    for (auto const& entry : entries) {
        if (entry.value <= 0 || shown >= 5) continue;

        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(entry.frame)) {
            float h = icon->getContentSize().height;
            if (h > 0.f) icon->setScale(11.f / h);
            icon->setPosition({x + icon->getScaledContentSize().width / 2.f, 0.f});
            slot->stats->addChild(icon);
            x += icon->getScaledContentSize().width + 3.f;
        }

        auto lbl = CCLabelBMFont::create(fmt::format("{}", entry.value).c_str(), "chatFont.fnt");
        lbl->setScale(0.4f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setColor({225, 231, 245});
        lbl->setPosition({x, 0.f});
        slot->stats->addChild(lbl);
        x += lbl->getScaledContentSize().width + 9.f;
        shown++;
    }

    if (animated && shown > 0) {
        slot->stats->setScale(0.82f);
        slot->stats->runAction(CCEaseBackOut::create(CCScaleTo::create(0.3f, 1.f)));
    }
}

void CommunityHubLayer::applyBanner(std::string const& key) {
    auto* slot = findIconSlot(key);
    if (!slot || slot->bannerShown) return;
    if (!slot->banner || !slot->banner->getParent()) return;

    int accountID = slot->score->m_accountID;
    if (accountID <= 0) return;

    auto& thumbs = ProfileThumbs::get();
    auto config = thumbs.getProfileConfig(accountID);
    auto cached = thumbs.getCachedProfile(accountID);
    CCTexture2D* tex = cached ? cached->texture.data() : nullptr;
    std::string gifKey = config.gifKey;
    bool readyGif = !gifKey.empty() && AnimatedGIFSprite::isCached(gifKey);
    if (!readyGif && !tex) return;

    float cellH = slot->cell->getContentSize().height;
    CCSize target = {m_listW, cellH};

    CCSprite* bgNode = nullptr;
    if (readyGif) {
        if (auto bgGif = AnimatedGIFSprite::createFromCache(gifKey)) {
            if (auto shader = Shaders::getBlurCellShader()) bgGif->setShaderProgram(shader);
            bgGif->m_intensity = std::min(1.7f, (2.0f / 9.0f) * 2.5f);
            if (bgGif->getTexture()) bgGif->m_texSize = bgGif->getTexture()->getContentSizeInPixels();
            bgGif->play();
            bgNode = bgGif;
        }
    }
    if (!bgNode && tex) {
        bgNode = BlurSystem::getInstance()->createBlurredSprite(tex, target, 6.0f);
    }
    if (!bgNode) return;

    auto clipper = CCClippingNode::create(
        paimon::SpriteHelper::createRectStencil(m_listW, cellH));
    clipper->setContentSize(target);
    slot->banner->addChild(clipper);

    CCSize bgSize = bgNode->getContentSize();
    if (bgSize.width > 0.f && bgSize.height > 0.f) {
        bgNode->setScale(std::max(m_listW / bgSize.width, cellH / bgSize.height));
    }
    bgNode->setAnchorPoint({0.5f, 0.5f});
    bgNode->setPosition({m_listW / 2.f, cellH / 2.f});
    bgNode->setOpacity(0);
    clipper->addChild(bgNode, 0);
    bgNode->runAction(CCFadeIn::create(0.35f));

    float darkness = config.hasConfig ? config.darkness : 0.45f;
    if (darkness > 0.f) {
        auto shade = static_cast<GLubyte>(std::clamp(darkness, 0.f, 1.f) * 255.f);
        auto overlay = CCLayerColor::create({0, 0, 0, 0});
        overlay->setContentSize(target);
        overlay->setCascadeOpacityEnabled(false);
        clipper->addChild(overlay, 1);
        overlay->runAction(CCFadeTo::create(0.35f, shade));
    }

    slot->bannerShown = true;
}

void CommunityHubLayer::queueBannerLoad(std::string const& key) {
    auto* slot = findIconSlot(key);
    if (!slot || slot->bannerShown || slot->bannerQueued) return;

    int accountID = slot->score->m_accountID;
    if (accountID <= 0 || slot->username.empty()) return;

    slot->bannerQueued = true;
    auto& thumbs = ProfileThumbs::get();
    thumbs.notifyVisible(accountID);

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    thumbs.queueLoad(accountID, slot->username, [self, tag, key](bool, CCTexture2D*) {
        Loader::get()->queueInMainThread([self, tag, key]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;
            if (layer->m_currentTab != Tab::Moderators) return;
            layer->applyBanner(key);
        });
    });
}

void CommunityHubLayer::startIconPipeline() {
    this->unschedule(schedule_selector(CommunityHubLayer::onIconTick));
    if (m_isExiting || m_currentTab != Tab::Moderators) return;

    bool pending = false;
    for (auto const& slot : m_iconSlots) {
        auto it = m_iconStates.find(slot.key);
        if (it != m_iconStates.end() && !it->second.done) {
            pending = true;
            break;
        }
    }
    if (!pending) return;

    // m_iconclock runs for the layer lifetime; resetting would push backoffs forward on rebuild.
    this->schedule(schedule_selector(CommunityHubLayer::onIconTick), kIconTickInterval);
    this->onIconTick(0.f);
}

void CommunityHubLayer::onIconTick(float dt) {
    if (m_isExiting || m_currentTab != Tab::Moderators) {
        this->unschedule(schedule_selector(CommunityHubLayer::onIconTick));
        return;
    }

    m_iconClock += dt;

    std::vector<std::string> keys;
    keys.reserve(m_iconSlots.size());
    for (auto const& slot : m_iconSlots) keys.push_back(slot.key);

    auto inFlightCount = [this] {
        int count = 0;
        for (auto const& [key, state] : m_iconStates) {
            if (state.inFlight) count++;
        }
        return count;
    };

    bool pending = false;
    for (auto const& key : keys) {
        auto it = m_iconStates.find(key);
        if (it == m_iconStates.end() || it->second.done) continue;
        pending = true;
        if (it->second.inFlight || it->second.readyAt > m_iconClock) continue;
        // re-checked per iteration: disk hits finish synchronously and free slots.
        if (inFlightCount() >= kMaxIconsInFlight) continue;
        it->second.inFlight = true;
        it->second.attempts++;
        beginIconRequest(key);
    }

    if (!pending) {
        this->unschedule(schedule_selector(CommunityHubLayer::onIconTick));
    }
}

void CommunityHubLayer::beginIconRequest(std::string const& key) {
    auto* slot = findIconSlot(key);
    if (!slot || !slot->score) {
        finishIconRequest(key, false);
        return;
    }

    auto* score = slot->score.data();
    if (score->m_accountID <= 0) {
        if (int memo = rememberedAccountID(slot->username); memo > 0) {
            score->m_accountID = memo;
        }
    }

    // same source profilepage reads: reuse parsed account info, skip the round trip.
    if (score->m_accountID > 0) {
        auto* glm = GameLevelManager::get();
        auto* known = glm ? glm->userInfoForAccountID(score->m_accountID) : nullptr;
        if (known && known != score && known->m_userID > 0 && (known->m_color1 > 0 || known->m_color2 > 0 || known->m_playerCube > 1)) {
            applyGdScore(score, known);
            finishIconRequest(key, true);
            return;
        }
    }

    if (score->m_accountID > 0) {
        requestUserInfo(key, score->m_accountID);
        return;
    }

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    paimon::moderation::resolveUsername(slot->username,
        [self, tag, key](bool ok, int accountID, std::string const&) {
            if (paimon::isRuntimeShuttingDown()) return;
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;
            layer->onAccountIDResolved(key, ok, accountID);
        });
}

void CommunityHubLayer::onAccountIDResolved(std::string const& key, bool ok, int accountID) {
    auto* slot = findIconSlot(key);
    if (!ok || accountID <= 0 || !slot) {
        finishIconRequest(key, false);
        return;
    }

    slot->score->m_accountID = accountID;
    rememberAccountID(slot->username, accountID);
    if (slot->button) slot->button->setTag(accountID);
    queueBannerLoad(key);

    requestUserInfo(key, accountID);
}

void CommunityHubLayer::requestUserInfo(std::string const& key, int accountID) {
    auto body = fmt::format(
        "targetAccountID={}&secret={}&gameVersion=22&binaryVersion=42", accountID, kGdSecret);

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    paimon::gd::postCached("getGJUserInfo20.php", body,
        [self, tag, key, accountID](bool ok, std::string response) {
            if (paimon::isRuntimeShuttingDown()) return;
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;
            layer->onUserInfoResponse(key, ok, response, accountID);
        });
}

void CommunityHubLayer::onUserInfoResponse(std::string const& key, bool ok, std::string const& response, int accountID) {
    auto* slot = findIconSlot(key);
    if (!slot) {
        finishIconRequest(key, false);
        return;
    }

    Ref<GJUserScore> parsed;
    if (ok) parsed = parseUserInfo(response, accountID);
    if (!parsed) {
        log::warn("[CommunityHub] no GD profile for {} (accountID {})", slot->username, accountID);
        finishIconRequest(key, false);
        return;
    }

    applyGdScore(slot->score.data(), parsed.data());
    finishIconRequest(key, true);
}

void CommunityHubLayer::finishIconRequest(std::string const& key, bool success) {
    auto it = m_iconStates.find(key);
    if (it == m_iconStates.end()) return;

    auto& state = it->second;
    state.inFlight = false;

    if (success) {
        state.done = true;
        iconReadyNames().insert(key);
        refreshSlot(key, true);
        applyBanner(key);
        queueBannerLoad(key);
        cacheModerators();
        return;
    }

    if (state.attempts >= kMaxIconAttempts) {
        state.done = true;
        return;
    }

    // robtop rate-limits bursts, so back off instead of hammering.
    state.readyAt = m_iconClock + 0.9f * static_cast<float>(state.attempts);
}

void CommunityHubLayer::cacheModerators() {
    s_cachedModEntries.clear();
    for (auto const& entry : m_modEntries) {
        s_cachedModEntries.push_back({entry.username, entry.role, entry.accountID});
    }
    getCachedModScores() = m_modScores;
    s_modCacheTimestamp = std::time(nullptr);
    s_modCacheValid = true;
}

void CommunityHubLayer::loadTopCreators(int attempt) {
    m_creatorEntries.clear();

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    HttpClient::get().getTopCreators([self, attempt, tag](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([self, attempt, tag, success, response]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;
            layer->handleTabResponse(Tab::TopCreators, attempt, success, response);
        });
    });
}

void CommunityHubLayer::buildCreatorsList() {
    finishTabLoad();
    beginList();

    auto& loc = Localization::get();
    setSortLabel(m_sortMode == 0
        ? loc.getString("community.uploads")
        : loc.getString("community.avg_rating"));

    if (m_creatorEntries.empty()) {
        showEmptyState(m_loadFailed);
        updateFooter(0, false);
        return;
    }

    std::vector<int> visible;
    for (int i = 0; i < static_cast<int>(m_creatorEntries.size()); i++) {
        if (rowMatchesFilter(m_creatorEntries[i].username)) visible.push_back(i);
    }
    if (visible.empty()) {
        showEmptyState();
        updateFooter(0, false);
        return;
    }

    float cellH = 42.f;
    // podium only when unfiltered: the top-3 strip would be meaningless over a search subset.
    bool showPodium = m_searchFilter.empty() && m_creatorEntries.size() >= 3;
    float podH = showPodium ? 100.f : 0.f;
    float totalH = std::max(m_listH, podH + cellH * static_cast<float>(visible.size()));
    auto* content = addScrollList(totalH);

    if (showPodium) {
        std::vector<std::string> names;
        std::vector<CCNode*> faces;
        std::vector<std::string> subs;
        for (int i = 0; i < 3; i++) {
            auto& e = m_creatorEntries[i];
            names.push_back(e.username);
            faces.push_back(nullptr);
            subs.push_back(m_sortMode == 0
                ? fmt::format("{} {}", e.uploadCount, loc.getString("community.uploads"))
                : fmt::format("{:.1f}", e.avgRating));
        }
        auto* podium = buildPodium(names, faces, subs);
        if (podium) {
            podium->setPosition({m_listW / 2.f, totalH - podH / 2.f});
            content->addChild(podium, 20);
        }
    }

    int maxUploads = 1;
    float maxRating = 0.001f;
    for (auto const& e : m_creatorEntries) {
        maxUploads = std::max(maxUploads, e.uploadCount);
        maxRating = std::max(maxRating, e.avgRating);
    }

    auto* rowMenu = addRowMenu(content, totalH);

    int row = 0;
    for (int idx : visible) {
        auto& entry = m_creatorEntries[idx];
        auto* cell = addCell(content, cellH, 0, 0.f);
        cell->setPosition({0.f, totalH - podH - static_cast<float>(row + 1) * cellH});
        cell->setOpacity(idx % 2 == 0 ? 110 : 55);
        registerHoverRow(cell);
        float mid = cellH / 2.f;

        auto rankColor = idx == 0 ? ccColor3B{255, 206, 66}
                       : idx == 1 ? ccColor3B{206, 212, 224}
                       : idx == 2 ? ccColor3B{205, 140, 86}
                                  : m_accentTarget;
        auto accent = CCLayerColor::create(ccc4(rankColor.r, rankColor.g, rankColor.b, idx < 3 ? 225 : 120));
        accent->setContentSize({3.f, cellH});
        cell->addChild(accent, 4);
        if (auto* medal = makeRankMedal(idx + 1)) {
            float h = medal->getContentSize().height;
            if (h > 0.f) medal->setScale(24.f / h);
            medal->setPosition({22.f, mid});
            cell->addChild(medal, 11);
        } else {
            auto numLbl = CCLabelBMFont::create(fmt::format("{}", idx + 1).c_str(), "goldFont.fnt");
            numLbl->setScale(0.5f);
            numLbl->setAnchorPoint({0.5f, 0.5f});
            numLbl->setPosition({22.f, mid});
            cell->addChild(numLbl, 10);
        }

        auto nameLbl = CCLabelBMFont::create(entry.username.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.45f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        fitLabel(nameLbl, m_listW - 200.f);
        nameLbl->setPosition({42.f, mid + 7.f});
        cell->addChild(nameLbl, 10);

        auto statsLbl = CCLabelBMFont::create(fmt::format("{}: {}  |  {}: {:.1f}",
            loc.getString("community.uploads"), entry.uploadCount,
            loc.getString("community.avg_rating"), entry.avgRating).c_str(), "chatFont.fnt");
        statsLbl->setScale(0.45f);
        statsLbl->setAnchorPoint({0.f, 0.5f});
        statsLbl->setOpacity(210);
        statsLbl->setPosition({42.f, mid - 8.f});
        cell->addChild(statsLbl, 10);

        // right block: headline metric of the active sort mode plus a bar
        // relative to the best entry.
        constexpr float kBlockW = 66.f;
        float blockX = m_listW - 14.f - kBlockW;
        bool byUploads = m_sortMode == 0;
        auto metricLbl = CCLabelBMFont::create(byUploads
            ? fmt::format("{}", entry.uploadCount).c_str()
            : fmt::format("{:.1f}", entry.avgRating).c_str(), "goldFont.fnt");
        metricLbl->setScale(0.4f);
        metricLbl->setAnchorPoint({1.f, 0.5f});
        metricLbl->setPosition({blockX + kBlockW, mid + 6.f});
        cell->addChild(metricLbl, 10);
        if (!byUploads) {
            if (auto* star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
                float h = star->getContentSize().height;
                if (h > 0.f) star->setScale(11.f / h);
                star->setPosition({blockX + kBlockW - metricLbl->getScaledContentSize().width - 8.f, mid + 6.f});
                cell->addChild(star, 10);
            }
        }
        float fraction = byUploads
            ? static_cast<float>(entry.uploadCount) / static_cast<float>(maxUploads)
            : entry.avgRating / maxRating;
        if (auto* bar = makeMetricBar(kBlockW, fraction, rankColor)) {
            bar->setPosition({blockX, mid - 10.f});
            cell->addChild(bar, 10);
        }

        if (entry.accountID > 0) {
            addRowButton(rowMenu, totalH, cellH, 0, entry.accountID, menu_selector(CommunityHubLayer::onCreatorRow))
                ->setPositionY(totalH - podH - (static_cast<float>(row) + 0.5f) * cellH);
        }

        animateCellIn(cell, row);
        row++;
    }

    updateFooter(static_cast<int>(visible.size()), false);
}

void CommunityHubLayer::loadTopThumbnails(int attempt) {
    m_thumbnailEntries.clear();

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    HttpClient::get().getTopThumbnails([self, attempt, tag](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([self, attempt, tag, success, response]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;
            layer->handleTabResponse(Tab::TopThumbnails, attempt, success, response);
        });
    });
}

void CommunityHubLayer::buildThumbnailsList() {
    finishTabLoad();
    beginList();

    auto& loc = Localization::get();
    setSortLabel(m_sortMode == 0
        ? loc.getString("community.rating")
        : loc.getString("community.votes"));

    if (m_thumbnailEntries.empty()) {
        showEmptyState(m_loadFailed);
        updateFooter(0, false);
        return;
    }

    std::vector<int> visible;
    for (int i = 0; i < static_cast<int>(m_thumbnailEntries.size()); i++) {
        auto& e = m_thumbnailEntries[i];
        std::string hay = fmt::format("{} {}", e.uploadedBy, e.levelId);
        if (rowMatchesFilter(hay)) visible.push_back(i);
    }
    if (visible.empty()) {
        showEmptyState();
        updateFooter(0, false);
        return;
    }

    float cellH = 56.f;
    float totalH = std::max(m_listH, cellH * static_cast<float>(visible.size()));
    auto* content = addScrollList(totalH);
    auto* rowMenu = addRowMenu(content, totalH);

    int row = 0;
    for (int idx : visible) {
        auto& entry = m_thumbnailEntries[idx];
        auto* cell = addCell(content, cellH, 0, 0.f);
        cell->setPosition({0.f, totalH - static_cast<float>(row + 1) * cellH});
        cell->setOpacity(idx % 2 == 0 ? 110 : 55);
        registerHoverRow(cell);
        float mid = cellH / 2.f;

        float thumbH = cellH - 10.f;
        float thumbW = thumbH * 1.6f;
        float thumbX = 5.f;
        float thumbY = 5.f;

        auto thumbBg = CCLayerColor::create({0, 0, 0, 200});
        thumbBg->setContentSize({thumbW, thumbH});
        thumbBg->setPosition({thumbX, thumbY});
        cell->addChild(thumbBg, 1);

        auto thumbClipper = paimon::ScissorClipNode::create(
            paimon::SpriteHelper::createRectStencil(thumbW, thumbH));
        thumbClipper->setContentSize({thumbW, thumbH});
        thumbClipper->setPosition({thumbX, thumbY});
        cell->addChild(thumbClipper, 2);

        int levelID = entry.levelId;
        auto addThumb = [thumbW, thumbH](CCNode* parent, CCTexture2D* tex) {
            auto spr = CCSprite::createWithTexture(tex);
            if (!spr) return;
            spr->setScale(std::max(thumbW / spr->getContentSize().width, thumbH / spr->getContentSize().height));
            spr->setPosition({thumbW / 2.f, thumbH / 2.f});
            spr->setOpacity(0);
            parent->addChild(spr);
            spr->runAction(CCFadeIn::create(0.3f));
        };

        if (auto localTex = LocalThumbs::get().loadTexture(levelID)) {
            addThumb(thumbClipper, localTex);
        } else if (levelID > 0) {
            Ref<CCNode> safeClipper = thumbClipper;
            ThumbnailLoader::get().requestLoad(levelID, fmt::format("{}.png", levelID),
                [safeClipper, addThumb](CCTexture2D* tex, bool) {
                    if (!safeClipper || !safeClipper->getParent() || !tex) return;
                    addThumb(safeClipper, tex);
                });
        }

        float textX = thumbX + thumbW + 8.f;

        if (auto* medal = makeRankMedal(idx + 1)) {
            float h = medal->getContentSize().height;
            if (h > 0.f) medal->setScale(18.f / h);
            medal->setPosition({textX + 7.f, mid + 15.f});
            cell->addChild(medal, 11);
        } else {
            auto numLbl = CCLabelBMFont::create(fmt::format("#{}", idx + 1).c_str(), "goldFont.fnt");
            numLbl->setScale(0.4f);
            numLbl->setAnchorPoint({0.f, 0.5f});
            numLbl->setPosition({textX, mid + 15.f});
            cell->addChild(numLbl, 10);
        }

        auto saved = GameLevelManager::get()->getSavedLevel(levelID);
        std::string levelName = saved
            ? std::string(saved->m_levelName)
            : fmt::format("{} {}", loc.getString("community.level"), levelID);
        auto nameLbl = CCLabelBMFont::create(levelName.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.4f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        fitLabel(nameLbl, m_listW - textX - 16.f);
        nameLbl->setPosition({textX, mid + 1.f});
        cell->addChild(nameLbl, 10);

        auto infoLbl = CCLabelBMFont::create(fmt::format("{} {} | {}: {:.1f} ({} {})",
            loc.getString("community.by"), entry.uploadedBy,
            loc.getString("community.rating"), entry.rating,
            entry.count, loc.getString("community.votes")).c_str(), "chatFont.fnt");
        infoLbl->setScale(0.42f);
        infoLbl->setAnchorPoint({0.f, 0.5f});
        infoLbl->setOpacity(210);
        infoLbl->setPosition({textX, mid - 14.f});
        cell->addChild(infoLbl, 10);

        // rating chip pinned top-right, tinted by rank so the podium picks stand out.
        auto chipColor = idx == 0 ? ccColor3B{255, 206, 66}
                       : idx == 1 ? ccColor3B{206, 212, 224}
                       : idx == 2 ? ccColor3B{205, 140, 86}
                                  : ccColor3B{255, 255, 255};
        auto ratingLbl = CCLabelBMFont::create(fmt::format("{:.1f}", entry.rating).c_str(), "goldFont.fnt");
        ratingLbl->setScale(0.42f);
        ratingLbl->setAnchorPoint({1.f, 0.5f});
        ratingLbl->setColor(chipColor);
        ratingLbl->setPosition({m_listW - 12.f, mid + 15.f});
        cell->addChild(ratingLbl, 10);
        if (auto* star = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
            float h = star->getContentSize().height;
            if (h > 0.f) star->setScale(12.f / h);
            star->setPosition({m_listW - 12.f - ratingLbl->getScaledContentSize().width - 8.f, mid + 15.f});
            cell->addChild(star, 10);
        }

        addRowButton(rowMenu, totalH, cellH, 0, entry.levelId, menu_selector(CommunityHubLayer::onThumbRow))
            ->setPositionY(totalH - (static_cast<float>(row) + 0.5f) * cellH);

        animateCellIn(cell, row);
        row++;
    }

    updateFooter(static_cast<int>(visible.size()), false);
}

void CommunityHubLayer::loadCompatibleMods() {
    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    paimon::compat_mods::ModlyRepo::get().fetchCatalog(false, [self, tag](bool ok) {
        if (paimon::isRuntimeShuttingDown()) return;
        auto layer = self.lock();
        if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;

        layer->m_compatMods = ok ? paimon::compat_mods::ModlyRepo::get().mods() : std::vector<paimon::compat_mods::ModlyMod>{};
        layer->m_compatLoadFailed = !ok;
        if (!ok) {
            layer->m_issue = paimon::ban::networkAuthorization() == 0 ? LoadIssue::Locked : LoadIssue::Network;
        }
        layer->hideLoading();
        layer->buildCompatibleModsList();
    });
}

void CommunityHubLayer::buildCompatibleModsList() {
    using namespace paimon::compat_mods;

    finishTabLoad();
    beginList();

    auto& loc = Localization::get();

    if (m_compatMods.empty()) {
        showEmptyState(m_compatLoadFailed);
        if (!m_compatLoadFailed) {
            auto hint = CCLabelBMFont::create(loc.getString("community.compat_mods_source").c_str(), "chatFont.fnt");
            hint->setScale(0.45f);
            hint->setOpacity(0);
            hint->setPosition({m_listCenter.x, m_listCenter.y - 16.f});
            fitLabel(hint, m_listW - 20.f);
            m_listContainer->addChild(hint, 10);
            hint->runAction(CCFadeTo::create(0.35f, 140));
        }
        updateFooter(0, false);
        return;
    }

    auto& repo = ModlyRepo::get();

    std::vector<int> visible;
    for (int i = 0; i < static_cast<int>(m_compatMods.size()); i++) {
        std::string hay = fmt::format("{} {}", m_compatMods[i].name, m_compatMods[i].authorName);
        if (rowMatchesFilter(hay)) visible.push_back(i);
    }
    if (visible.empty()) {
        showEmptyState();
        updateFooter(0, false);
        return;
    }

    float cellH = 46.f;
    float totalH = std::max(m_listH, cellH * static_cast<float>(visible.size()));
    auto* content = addScrollList(totalH);

    // one menu over the content layer; each row's button opens its project anywhere tapped.
    auto* menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    menu->setContentSize({m_listW, totalH});
    content->addChild(menu, 20);

    int row = 0;
    for (int modIndex : visible) {
        auto const& mod = m_compatMods[modIndex];
        auto* cell = addCell(content, cellH, 0, 0.f);
        cell->setPosition({0.f, totalH - static_cast<float>(row + 1) * cellH});
        cell->setOpacity(modIndex % 2 == 0 ? 110 : 55);
        registerHoverRow(cell);
        float mid = cellH / 2.f;

        auto* logo = createAvatar(repo.logoUrl(mod), mod.hasLogo, mod.name, 32.f, 7.f);
        logo->setPosition({28.f, mid});
        cell->addChild(logo, 10);

        float textX = 50.f;
        float rightEdge = m_listW - 12.f;

        auto nameLbl = CCLabelBMFont::create(mod.name.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.44f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        fitLabel(nameLbl, m_listW - textX - 110.f);
        nameLbl->setPosition({textX, mid + 9.f});
        cell->addChild(nameLbl, 10);

        float pillX = textX + nameLbl->getScaledContentSize().width + 6.f;
        auto addPill = [&](std::string const& text, ccColor3B color) {
            auto* pill = createPill(text, color, 0.28f);
            if (pillX + pill->getContentSize().width > rightEdge - 60.f) return;
            pill->setPosition({pillX, mid + 9.f});
            cell->addChild(pill, 10);
            pillX += pill->getContentSize().width + 4.f;
        };

        if (mod.state == "alpha") addPill("ALPHA", {235, 120, 60});
        else if (mod.state == "beta") addPill("BETA", {120, 110, 235});
        if (!mod.gdps.empty()) addPill("GDPS", {60, 160, 180});
        if (mod.isPack()) addPill(loc.getString("modly.type_pack"), {180, 110, 190});

        auto metaLbl = CCLabelBMFont::create(
            fmt::format("v{}  |  {} {}  |  {} {}",
                mod.version,
                loc.getString("community.by"),
                mod.authorName.empty() ? loc.getString("modly.unknown_author") : mod.authorName,
                mod.downloads,
                loc.getString("modly.downloads")).c_str(),
            "chatFont.fnt");
        metaLbl->setScale(0.42f);
        metaLbl->setAnchorPoint({0.f, 0.5f});
        metaLbl->setOpacity(200);
        fitLabel(metaLbl, m_listW - textX - 30.f);
        metaLbl->setPosition({textX, mid - 10.f});
        cell->addChild(metaLbl, 10);

        auto arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        arrow->setFlipX(true);
        arrow->setScale(0.4f);
        arrow->setOpacity(160);
        arrow->setPosition({rightEdge - 6.f, mid});
        cell->addChild(arrow, 10);

        auto* hit = CCLayerColor::create({0, 0, 0, 0}, m_listW, cellH);
        auto* btn = CCMenuItemSpriteExtra::create(hit, this, menu_selector(CommunityHubLayer::onCompatMod));
        btn->setTag(modIndex);
        btn->m_scaleMultiplier = 1.015f;
        btn->setPosition({m_listW / 2.f, totalH - (static_cast<float>(row) + 0.5f) * cellH});
        menu->addChild(btn);

        animateCellIn(cell, row);
        row++;
    }

    updateFooter(static_cast<int>(visible.size()), false);
}

void CommunityHubLayer::onCompatMod(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    int index = btn->getTag();
    if (index < 0 || index >= static_cast<int>(m_compatMods.size())) return;
    if (auto* popup = paimon::compat_mods::ModlyModPopup::create(m_compatMods[index])) popup->show();
}

void CommunityHubLayer::onInfoButton(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;

    auto& loc = Localization::get();
    std::string title;
    std::string body;

    switch (static_cast<Tab>(btn->getTag())) {
        case Tab::TopCreators:
            title = loc.getString("community.info_creators_title");
            body = loc.getString("community.info_creators_body");
            break;
        case Tab::TopThumbnails:
            title = loc.getString("community.info_thumbs_title");
            body = loc.getString("community.info_thumbs_body");
            break;
        case Tab::CompatibleMods:
            title = loc.getString("community.info_compat_title");
            body = loc.getString("community.info_compat_body");
            break;
        case Tab::Moderators:
        default:
            title = loc.getString("community.info_mods_title");
            body = loc.getString("community.info_mods_body");
            break;
    }

#if defined(GEODE_IS_DESKTOP)
    body += "\n\n" + loc.getString("community.shortcuts");
#endif
    PopupManager::get().alert(title, body, "OK", nullptr, 350.f).showInstant();
}

bool CommunityHubLayer::rowMatchesFilter(std::string const& name) const {
    if (m_searchFilter.empty()) return true;
    return geode::utils::string::toLower(name).find(m_searchFilter) != std::string::npos;
}

void CommunityHubLayer::onSearchChanged(std::string const& text) {
    std::string next = geode::utils::string::toLower(text);
    if (next == m_searchFilter) return;
    m_searchFilter = next;
    // data is already in memory; rebuilding filters without any network hit.
    buildCurrentList();
}

void CommunityHubLayer::onSort(CCObject*) {
    if (m_currentTab != Tab::TopCreators && m_currentTab != Tab::TopThumbnails) return;
    m_sortMode = (m_sortMode + 1) % 2;

    if (m_currentTab == Tab::TopCreators) {
        std::stable_sort(m_creatorEntries.begin(), m_creatorEntries.end(),
            [this](CreatorEntry const& a, CreatorEntry const& b) {
                return m_sortMode == 0 ? a.uploadCount > b.uploadCount : a.avgRating > b.avgRating;
            });
        buildCreatorsList();
    } else {
        std::stable_sort(m_thumbnailEntries.begin(), m_thumbnailEntries.end(),
            [this](ThumbnailEntry const& a, ThumbnailEntry const& b) {
                return m_sortMode == 0 ? a.rating > b.rating : a.count > b.count;
            });
        buildThumbnailsList();
    }
    updateTabHeader();
}

void CommunityHubLayer::onRefresh(CCObject*) {
    // moderators serve from a 15 min cache; a manual refresh must bypass it.
    if (m_currentTab == Tab::Moderators) s_modCacheValid = false;
    setToolbarBusy(true);
    showLoading();
    loadTab(m_currentTab);
}

void CommunityHubLayer::onCreatorRow(CCObject* sender) {
    int accountID = sender->getTag();
    if (accountID > 0) ProfilePage::create(accountID, false)->show();
}

void CommunityHubLayer::onThumbRow(CCObject* sender) {
    int levelID = sender->getTag();
    if (levelID <= 0) return;
    if (auto* popup = LocalThumbnailViewPopup::create(levelID, false)) popup->show();
}

void CommunityHubLayer::setToolbarBusy(bool busy) {
    if (m_refreshButton) m_refreshButton->setVisible(!busy);
    if (busy && !m_refreshSpinner && m_refreshButton) {
        m_refreshSpinner = LoadingSpinner::create(kToolButton - 4.f);
        m_refreshSpinner->setPosition(m_refreshButton->getPosition());
        this->addChild(m_refreshSpinner, 13);
    } else if (!busy && m_refreshSpinner) {
        m_refreshSpinner->removeFromParent();
        m_refreshSpinner = nullptr;
    }
}

void CommunityHubLayer::updateFooter(int shown, bool fromCache) {
    if (!m_footerLabel) return;
    auto& loc = Localization::get();
    std::string label;
    switch (m_currentTab) {
        case Tab::Moderators:   label = fmt::format("{} {}", shown, loc.getString("community.unit_mods")); break;
        case Tab::TopCreators:  label = fmt::format("{} {}", shown, loc.getString("community.unit_creators")); break;
        case Tab::TopThumbnails:label = fmt::format("{} {}", shown, loc.getString("community.unit_thumbs")); break;
        case Tab::CompatibleMods:label = fmt::format("{} {}", shown, loc.getString("community.unit_compat")); break;
    }

    // the dot says where the rows came from: live, session cache, saved copy, or nothing.
    bool failed = m_currentTab == Tab::CompatibleMods ? m_compatLoadFailed : m_loadFailed;
    ccColor3B dot = paimon::ui::palette::success;
    if (failed) {
        label += fmt::format("  |  {}", loc.getString("community.status_error"));
        dot = paimon::ui::palette::danger;
    } else if (m_fromSnapshot) {
        label += fmt::format("  |  {} {}", loc.getString("community.status_saved"), formatStamp(m_snapshotAt, true));
        dot = paimon::ui::palette::warning;
    } else if (fromCache) {
        label += fmt::format("  |  {} {}", loc.getString("community.cached"), formatStamp(s_modCacheTimestamp, false));
        dot = paimon::ui::palette::info;
    } else {
        label += fmt::format("  |  {} {}", loc.getString("community.updated"), formatStamp(std::time(nullptr), false));
    }

    m_footerLabel->setString(label.c_str());
    m_footerLabel->setScale(0.42f);
    fitLabel(m_footerLabel, m_cardRect.size.width - 40.f);
    m_footerLabel->setOpacity(0);
    m_footerLabel->runAction(CCFadeTo::create(0.3f, 205));

    if (m_statusDot) m_statusDot->removeFromParent();
    m_statusDot = paimon::SpriteHelper::createRoundedRect(6.f, 6.f, 3.f,
        {dot.r / 255.f, dot.g / 255.f, dot.b / 255.f, 1.f});
    if (m_statusDot) {
        float labelLeft = m_footerLabel->getPositionX() - m_footerLabel->getScaledContentSize().width / 2.f;
        m_statusDot->setPosition({labelLeft - 10.f, m_footerLabel->getPositionY() - 3.f});
        this->addChild(m_statusDot, 10);
    }
}

CCSprite* CommunityHubLayer::makeRankMedal(int rank) {
    if (rank < 1 || rank > 3) return nullptr;
    auto* medal = paimon::SpriteHelper::safeCreateWithFrameName("rankIcon_1_001.png");
    if (!medal) return nullptr;
    ccColor3B tint = rank == 1 ? ccColor3B{255, 206, 66}
                   : rank == 2 ? ccColor3B{206, 212, 224}
                               : ccColor3B{205, 140, 86};
    medal->setColor(tint);
    return medal;
}

CCNode* CommunityHubLayer::buildPodium(std::vector<std::string> const& names,
    std::vector<CCNode*> const& faces, std::vector<std::string> const& subs) {
    if (names.empty()) return nullptr;

    float podH = 92.f;
    auto* node = CCNode::create();
    node->setContentSize({m_listW, podH});
    node->setAnchorPoint({0.5f, 0.5f});

    auto panel = paimon::ui::makeInset({m_listW - 8.f, podH - 6.f}, 120);
    panel->setPosition({4.f, 3.f});
    node->addChild(panel, 0);

    char const* crowns[] = {"gj_eventCrown_001.png", "gj_weeklyCrown_001.png", "gj_dailyCrown_001.png"};
    float slotX[] = {m_listW / 2.f, m_listW / 2.f - 108.f, m_listW / 2.f + 108.f};
    float slotY[] = {podH / 2.f + 2.f, podH / 2.f - 6.f, podH / 2.f - 6.f};
    float iconScale[] = {1.3f, 1.0f, 1.0f};

    int count = std::min<int>(3, static_cast<int>(names.size()));
    for (int i = 0; i < count; i++) {
        float cx = slotX[i];
        float cy = slotY[i];
        ccColor3B tint = i == 0 ? ccColor3B{255, 206, 66}
                       : i == 1 ? ccColor3B{206, 212, 224}
                                : ccColor3B{205, 140, 86};

        // each place is its own node so it can pop in; children use slot-local coordinates.
        auto* slot = CCNode::create();
        slot->setPosition({cx, cy});
        node->addChild(slot, 1);

        // soft halo behind the crown; the winner also gets a slow sunburst.
        if (auto* halo = paimon::SpriteHelper::safeCreateWithFrameName("d_circle_02_001.png")) {
            float w = halo->getContentSize().width;
            if (w > 0.f) halo->setScale((i == 0 ? 46.f : 34.f) / w);
            halo->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            halo->setColor(tint);
            halo->setOpacity(i == 0 ? 55 : 38);
            halo->setPosition({0.f, 17.f});
            slot->addChild(halo, 0);
            if (paimon::ui::motionEnabled()) {
                halo->runAction(CCRepeatForever::create(CCSequence::create(
                    CCEaseSineInOut::create(CCFadeTo::create(1.2f, 20)),
                    CCEaseSineInOut::create(CCFadeTo::create(1.2f, i == 0 ? 70 : 50)),
                    nullptr)));
            }
        }
        if (i == 0) {
            if (auto* burst = paimon::SpriteHelper::safeCreateWithFrameName("shineBurst_001.png")) {
                float w = burst->getContentSize().width;
                if (w > 0.f) burst->setScale(60.f / w);
                burst->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
                burst->setColor(tint);
                burst->setOpacity(80);
                burst->setPosition({0.f, 17.f});
                slot->addChild(burst, 0);
                if (paimon::ui::motionEnabled()) {
                    burst->runAction(CCRepeatForever::create(CCRotateBy::create(12.f, 360.f)));
                }
            }
        }

        if (auto* crown = paimon::SpriteHelper::safeCreateWithFrameName(crowns[i])) {
            // cap the crown at ~30px tall and keep its top inside the
            // podium panel, or the scroll stencil clips it.
            float crownH = crown->getContentSize().height;
            if (crownH > 0.f) crown->setScale(30.f / crownH * iconScale[i]);
            crown->setPosition({0.f, 17.f});
            slot->addChild(crown, 3);
            if (paimon::ui::motionEnabled()) {
                crown->runAction(CCRepeatForever::create(CCSequence::create(
                    CCDelayTime::create(0.25f * static_cast<float>(i)),
                    CCEaseSineInOut::create(CCMoveBy::create(1.1f, {0.f, 2.f})),
                    CCEaseSineInOut::create(CCMoveBy::create(1.1f, {0.f, -2.f})),
                    nullptr)));
            }
        }

        if (i < static_cast<int>(faces.size()) && faces[i]) {
            auto* face = faces[i];
            face->setScale(face->getScale() * iconScale[i]);
            face->setPosition({0.f, 6.f});
            slot->addChild(face, 2);
        }

        auto nameLbl = CCLabelBMFont::create(names[i].c_str(), "bigFont.fnt");
        nameLbl->setScale(0.34f);
        fitLabel(nameLbl, 92.f);
        nameLbl->setPosition({0.f, -16.f});
        slot->addChild(nameLbl, 4);

        if (i < static_cast<int>(subs.size()) && !subs[i].empty()) {
            auto subLbl = CCLabelBMFont::create(subs[i].c_str(), "chatFont.fnt");
            subLbl->setScale(0.32f);
            subLbl->setColor({255, 206, 90});
            fitLabel(subLbl, 92.f);
            subLbl->setPosition({0.f, -28.f});
            slot->addChild(subLbl, 4);
        }

        if (paimon::ui::motionEnabled()) {
            // winner lands last, like a reveal.
            static constexpr float kDelay[] = {0.3f, 0.12f, 0.2f};
            slot->setScale(0.f);
            slot->runAction(CCSequence::create(
                CCDelayTime::create(kDelay[i]),
                CCEaseBackOut::create(CCScaleTo::create(0.4f, 1.f)),
                nullptr));
        }
    }

    if (paimon::ui::motionEnabled()) {
        node->setScale(0.9f);
        node->runAction(CCEaseBackOut::create(CCScaleTo::create(0.35f, 1.f)));
    }
    return node;
}
