#include "CommunityHubLayer.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/ModProfileCache.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../transitions/services/TransitionManager.hpp"
#include "../../backgrounds/services/LayerBackgroundManager.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../profiles/services/ProfileThumbs.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <matjson.hpp>
#include <ctime>
#include <atomic>
#include <Geode/binding/FLAlertLayer.hpp>
#include "../../../utils/WebHelper.hpp"

using namespace geode::prelude;

namespace {
    WeakRef<CommunityHubLayer> s_activeCommunityHubLayer;

    // Session cache for moderators tab (persists until game closes or TTL expires)
    struct CachedModEntry {
        std::string username;
        std::string role;
        int accountID = 0;
    };
    static std::vector<CachedModEntry> s_cachedModEntries;
    static Ref<CCArray> s_cachedModScores;
    static std::time_t s_modCacheTimestamp = 0;
    static bool s_modCacheValid = false;
    static constexpr std::time_t k_modCacheTTL = 900; // 15 minutes

    std::string toLowerCopy(std::string const& value) {
        return geode::utils::string::toLower(value);
    }

    GJUserScore* findModeratorScore(cocos2d::CCArray* scores, std::string const& username) {
        if (!scores || username.empty()) {
            return nullptr;
        }

        auto wanted = toLowerCopy(username);
        for (auto* score : CCArrayExt<GJUserScore*>(scores)) {
            if (!score) {
                continue;
            }

            if (toLowerCopy(static_cast<std::string>(score->m_userName)) == wanted) {
                return score;
            }
        }

        return nullptr;
    }

    void applyNativeScoreToModerator(GJUserScore* target, GJUserScore* source) {
        if (!target || !source) {
            return;
        }

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

        if (target->m_iconID <= 0) {
            switch (target->m_iconType) {
                case IconType::Cube: target->m_iconID = source->m_playerCube; break;
                case IconType::Ship: target->m_iconID = source->m_playerShip; break;
                case IconType::Ball: target->m_iconID = source->m_playerBall; break;
                case IconType::Ufo: target->m_iconID = source->m_playerUfo; break;
                case IconType::Wave: target->m_iconID = source->m_playerWave; break;
                case IconType::Robot: target->m_iconID = source->m_playerRobot; break;
                case IconType::Spider: target->m_iconID = source->m_playerSpider; break;
                case IconType::Swing: target->m_iconID = source->m_playerSwing; break;
                case IconType::Jetpack: target->m_iconID = source->m_playerJetpack; break;
                default: target->m_iconID = source->m_playerCube; break;
            }
        }

        if (target->m_iconID <= 0) {
            target->m_iconID = 1;
        }

        target->m_modBadge = existingModBadge;
    }

    Ref<GJUserScore> createUserSearchScoreFromSegment(GameLevelManager* glm, std::string const& segment, bool keyedResponse) {
        if (!glm || segment.empty() || segment == "-1") {
            return nullptr;
        }

        auto* dict = GameLevelManager::responseToDict(segment, keyedResponse);
        if (!dict) {
            return nullptr;
        }

        auto* score = GJUserScore::create(dict);
        if (!score) {
            return nullptr;
        }

        if (score->m_accountID <= 0 && score->m_userID > 0) {
            int resolvedAccountID = glm->accountIDForUserID(score->m_userID);
            if (resolvedAccountID > 0) {
                score->m_accountID = resolvedAccountID;
            }
        }

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

        if (score->m_iconID <= 0) {
            score->m_iconID = 1;
        }

        return score;
    }

    struct NativeModeratorLookupResult {
        bool matched = false;
        Ref<GJUserScore> score;
        int accountID = 0;
    };

    NativeModeratorLookupResult lookupModeratorFromUserSearchResponse(GameLevelManager* glm, std::string const& response, std::string const& wantedUsername) {
        NativeModeratorLookupResult result;
        if (!glm || wantedUsername.empty() || response.empty() || response == "-1") {
            return result;
        }

        auto usersBlock = response;
        auto hashPos = usersBlock.find('#');
        if (hashPos != std::string::npos) {
            usersBlock = usersBlock.substr(0, hashPos);
        }

        auto wantedLower = toLowerCopy(wantedUsername);
        size_t start = 0;

        while (start < usersBlock.size()) {
            auto end = usersBlock.find('|', start);
            auto segment = usersBlock.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = (end == std::string::npos) ? usersBlock.size() : end + 1;

            if (segment.empty() || segment == "-1") {
                continue;
            }

            Ref<GJUserScore> parsedScore;
            for (bool keyedResponse : { true, false }) {
                auto candidate = createUserSearchScoreFromSegment(glm, segment, keyedResponse);
                if (!candidate) {
                    continue;
                }

                if (toLowerCopy(static_cast<std::string>(candidate->m_userName)) != wantedLower) {
                    continue;
                }

                parsedScore = candidate;
                break;
            }

            if (!parsedScore) {
                continue;
            }

            result.matched = true;
            result.accountID = parsedScore->m_accountID;
            result.score = parsedScore;
            return result;
        }

        return result;
    }
}

class $modify(PaimonCommunityHubGameLevelManager, GameLevelManager) {
    $override
    void onGetUsersCompleted(gd::string response, gd::string tag) {
        GameLevelManager::onGetUsersCompleted(response, tag);

        auto layerRef = s_activeCommunityHubLayer.lock();
        auto* layer = static_cast<CommunityHubLayer*>(layerRef.data());
        if (!layer || layer->m_isExiting || layer->m_currentTab != CommunityHubLayer::Tab::Moderators) {
            return;
        }

        if (layer->m_activeNativeModeratorSearchUsername.empty()) {
            return;
        }

        auto username = layer->m_activeNativeModeratorSearchUsername;
        auto rawResponse = static_cast<std::string>(response);
        auto lookup = lookupModeratorFromUserSearchResponse(this, rawResponse, username);
        if (!lookup.matched && rawResponse != "-1" && !rawResponse.empty()) {
            return;
        }

        Loader::get()->queueInMainThread([username, accountID = lookup.accountID, nativeScore = lookup.score]() {
            auto layerRef2 = s_activeCommunityHubLayer.lock();
            auto* layer2 = static_cast<CommunityHubLayer*>(layerRef2.data());
            if (!layer2 || layer2->m_isExiting || layer2->m_currentTab != CommunityHubLayer::Tab::Moderators) {
                return;
            }

            if (auto* targetScore = findModeratorScore(layer2->m_modScores, username); targetScore && nativeScore) {
                applyNativeScoreToModerator(targetScore, nativeScore.data());
                layer2->requestModeratorsListRebuild();
            }

            layer2->onNativeModeratorSearchCompleted(username, accountID);
        });
    }
};

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
    auto scene = CCScene::create();
    scene->addChild(CommunityHubLayer::create());
    return scene;
}

CommunityHubLayer::~CommunityHubLayer() {
    log::info("[CommunityHub] destroyed");
    this->unscheduleUpdate();
    this->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
    this->unschedule(schedule_selector(CommunityHubLayer::onDeferredModeratorsRebuild));
    hideLoading();
    for (auto& entry : m_thumbnailEntries) {
        if (entry.levelId > 0) {
            ThumbnailLoader::get().cancelLoad(entry.levelId);
        }
    }
    clearList();
    clearPendingNativeModeratorRequests();
    removeCaveEffect();
}

bool CommunityHubLayer::init() {
    if (!CCLayer::init()) return false;
    log::info("[CommunityHub] init");

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // fondo
    if (!LayerBackgroundManager::get().applyBackground(this, "community_hub")) {
        auto bg = CCLayerColor::create(ccc4(15, 12, 25, 255));
        bg->setContentSize(winSize);
        bg->setZOrder(-10);
        this->addChild(bg);
    }

    // titulo
    auto& loc = Localization::get();
    auto title = CCLabelBMFont::create(loc.getString("community.title").c_str(), "bigFont.fnt");
    title->setScale(0.65f);
    title->setPosition({winSize.width / 2, winSize.height - 20.f});
    this->addChild(title, 10);

    // boton volver
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

    // tabs: Moderadores | Top Creadores | Top Miniaturas
    auto tabMenu = CCMenu::create();
    tabMenu->setPosition(0, 0);
    tabMenu->setZOrder(10);
    this->addChild(tabMenu);
    m_tabsMenu = tabMenu;

    auto createTab = [&](std::string const& text, char const* id, CCPoint pos) -> CCMenuItemToggler* {
        auto createBtn = [&](char const* frameName) -> CCNode* {
            auto sprite = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName(frameName);
            sprite->setContentSize({90.f, 28.f});
            auto label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
            label->setScale(0.45f);
            float maxLabelW = sprite->getContentSize().width - 8.f;
            if (label->getScaledContentSize().width > maxLabelW) {
                label->setScale(label->getScale() * (maxLabelW / label->getScaledContentSize().width));
            }
            label->setPosition(sprite->getContentSize() / 2);
            sprite->addChild(label);
            return sprite;
        };

        auto onSprite = createBtn("GJ_longBtn01_001.png");
        auto offSprite = createBtn("GJ_longBtn02_001.png");

        auto tab = CCMenuItemToggler::create(offSprite, onSprite, this, menu_selector(CommunityHubLayer::onTab));
        tab->setUserObject(CCString::create(id));
        tab->setPosition(pos);
        m_tabs.push_back(tab);
        return tab;
    };

    float topY = winSize.height - 45.f;
    float centerX = winSize.width / 2;

    auto modsTab = createTab(loc.getString("community.tab_mods"), "mods", {centerX - 165.f, topY});
    modsTab->toggle(true);
    tabMenu->addChild(modsTab);

    auto creatorsTab = createTab(loc.getString("community.tab_creators"), "creators", {centerX - 55.f, topY});
    tabMenu->addChild(creatorsTab);

    auto thumbsTab = createTab(loc.getString("community.tab_thumbnails"), "thumbnails", {centerX + 55.f, topY});
    tabMenu->addChild(thumbsTab);

    auto modsCompatTab = createTab(loc.getString("community.tab_compat_mods"), "compat_mods", {centerX + 165.f, topY});
    modsCompatTab->toggle(false);
    tabMenu->addChild(modsCompatTab);

    // spinner inicial
    showLoading();

    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(0);
#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
#endif

    applyCaveEffect();
    this->scheduleUpdate();

    loadTab(Tab::Moderators);
    return true;
}

void CommunityHubLayer::onEnterTransitionDidFinish() {
    log::info("[CommunityHub] onEnterTransitionDidFinish");
    CCLayer::onEnterTransitionDidFinish();
    applyCaveEffect();
}

void CommunityHubLayer::update(float dt) {
    if (m_isExiting) return;
    // Verificar que el efecto cueva sigue aplicado (por si el canal cambio)
    if (!m_caveApplied) {
        applyCaveEffect();
    }
}

void CommunityHubLayer::applyCaveEffect() {
    log::debug("[CommunityHub] applyCaveEffect");
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return;
    if (m_caveApplied) return;

    // Guardar volumen original y reducir al 55% para efecto de lejania
    engine->m_backgroundMusicChannel->getVolume(&m_savedBgVolume);
    float caveVol = engine->m_musicVolume * 0.55f;
    engine->m_backgroundMusicChannel->setVolume(caveVol);

    // Lowpass filter — simula paredes de cueva
    if (!m_lowpassDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &m_lowpassDSP);
        if (m_lowpassDSP) {
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 1200.f);
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 2.0f);
        }
    }

    // Reverb sutil — eco de cueva
    if (!m_reverbDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_SFXREVERB, &m_reverbDSP);
        if (m_reverbDSP) {
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_DECAYTIME, 2500.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_EARLYDELAY, 20.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_LATEDELAY, 40.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_HFREFERENCE, 3000.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_DRYLEVEL, -4.f);
            m_reverbDSP->setParameterFloat(FMOD_DSP_SFXREVERB_WETLEVEL, -8.f);
        }
    }

    if (m_lowpassDSP) engine->m_backgroundMusicChannel->addDSP(0, m_lowpassDSP);
    if (m_reverbDSP) engine->m_backgroundMusicChannel->addDSP(1, m_reverbDSP);
    m_caveApplied = true;
}

void CommunityHubLayer::removeCaveEffect() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        if (m_lowpassDSP) engine->m_backgroundMusicChannel->removeDSP(m_lowpassDSP);
        if (m_reverbDSP) engine->m_backgroundMusicChannel->removeDSP(m_reverbDSP);
        // Restaurar el volumen real que tenia el canal antes del efecto cueva
        engine->m_backgroundMusicChannel->setVolume(m_savedBgVolume);
    }
    if (m_lowpassDSP) { m_lowpassDSP->release(); m_lowpassDSP = nullptr; }
    if (m_reverbDSP) { m_reverbDSP->release(); m_reverbDSP = nullptr; }
    m_caveApplied = false;
}

bool CommunityHubLayer::ccMouseScroll(float x, float y) {
#if !defined(GEODE_IS_WINDOWS) && !defined(GEODE_IS_MACOS)
    return false;
#else
    if (!m_scrollView) return false;

    CCPoint mousePos = geode::cocos::getMousePos();

    CCRect scrollRect = m_scrollView->boundingBox();
    scrollRect.origin = m_scrollView->getParent()->convertToWorldSpace(scrollRect.origin);

    if (!scrollRect.containsPoint(mousePos)) return false;

    CCPoint offset = ccp(0, m_scrollView->m_contentLayer->getPositionY());
    CCSize viewSize = m_scrollView->getContentSize();
    CCSize contentSize = m_scrollView->m_contentLayer->getContentSize();

    float scrollAmount = y * 30.f;
    float newY = offset.y + scrollAmount;

    float minY = viewSize.height - contentSize.height;
    float maxY = 0.f;
    if (minY > maxY) minY = maxY;

    newY = std::max(minY, std::min(maxY, newY));
    m_scrollView->m_contentLayer->setPositionY(newY);
    return true;
#endif
}

void CommunityHubLayer::onBack(CCObject*) {
    m_isExiting = true;
    ++m_retryTag;
    m_moderatorsRebuildQueued = false;
    this->unschedule(schedule_selector(CommunityHubLayer::onDeferredModeratorsRebuild));
    this->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
    this->unscheduleUpdate();
    removeCaveEffect();
    clearPendingNativeModeratorRequests();
    CCDirector::sharedDirector()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
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

    Tab newTab;
    if (type == "mods") newTab = Tab::Moderators;
    else if (type == "creators") newTab = Tab::TopCreators;
    else if (type == "thumbnails") newTab = Tab::TopThumbnails;
    else if (type == "compat_mods") newTab = Tab::CompatibleMods;
    else newTab = Tab::TopThumbnails;

    if (m_currentTab == newTab) {
        toggler->toggle(true);
        return;
    }

    // Cancelar descargas de thumbnails individuales que quedaron pendientes
    // en ThumbnailLoader al salir de la pestaña Top Thumbnails.
    // Sin esto, esos workers siguen corriendo y bloquean los slots concurrentes
    // (m_maxConcurrentTasks=4), retrasando cualquier carga de la nueva pestaña.
    if (m_currentTab == Tab::TopThumbnails) {
        for (auto& entry : m_thumbnailEntries) {
            if (entry.levelId > 0) {
                ThumbnailLoader::get().cancelLoad(entry.levelId);
            }
        }
    }

    m_currentTab = newTab;

    for (auto tab : m_tabs) {
        tab->toggle(tab == toggler);
        tab->setEnabled(false);
    }
    m_isLoadingTab = true;

    clearList();
    showLoading();

    loadTab(newTab);
}

void CommunityHubLayer::clearList() {
    if (m_listContainer) {
        m_listContainer->removeFromParent();
        m_listContainer = nullptr;
    }
    m_scrollView = nullptr;
}

void CommunityHubLayer::showLoading() {
    hideLoading();
    m_loadingSpinner = PaimonLoadingOverlay::create("Loading...", 40.f);
    if (m_loadingSpinner) m_loadingSpinner->show(this, 100);
}

void CommunityHubLayer::hideLoading() {
    if (m_loadingSpinner) {
        m_loadingSpinner->dismiss();
        m_loadingSpinner = nullptr;
    }
}

void CommunityHubLayer::loadTab(Tab tab) {
    // Cancelar explícitamente el timer de retry: ++m_retryTag invalida callbacks HTTP
    // pendientes, pero scheduleOnce sigue corriendo hasta que alguien lo desregistre.
    // Sin este unschedule, si el usuario navega a otra sección y REGRESA a la misma tab
    // antes de que el timer dispare, el timer viejo ejecuta retryLoadTab() con
    // m_currentTab correcto y m_retryTag actual → lanza una petición HTTP duplicada +
    // llama showLoading() encima de la carga fresca → "bucle" de spinner / lista.
    this->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
    ++m_retryTag; // cancel any pending callbacks from previous tab
    if (tab != Tab::Moderators) {
        clearPendingNativeModeratorRequests();
    }
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

void CommunityHubLayer::onRetryTimer(float) {
    retryLoadTab(m_pendingRetryTab, m_pendingRetryAttempt);
}

// ==================== MODERATORS TAB ====================

void CommunityHubLayer::loadModerators(int attempt) {
    // Use session cache on first attempt if still fresh
    if (attempt == 0 && s_modCacheValid && s_cachedModScores && s_cachedModScores->count() > 0) {
        std::time_t now = std::time(nullptr);
        if (now - s_modCacheTimestamp < k_modCacheTTL) {
            log::info("[CommunityHub] Moderators: instant load from cache");
            m_modEntries.clear();
            for (auto const& ce : s_cachedModEntries) {
                ModEntry e;
                e.username = ce.username;
                e.role = ce.role;
                e.accountID = ce.accountID;
                m_modEntries.push_back(e);
            }
            m_modScores = s_cachedModScores;
            m_moderatorsRebuildQueued = false;
            m_requestedModeratorProfiles.clear();
            clearPendingNativeModeratorRequests();
            hideLoading();
            onAllProfilesFetched();
            return;
        }
    }
    m_modEntries.clear();
    m_modScores = CCArray::create();
    m_requestedModeratorProfiles.clear();
    m_moderatorsRebuildQueued = false;
    clearPendingNativeModeratorRequests();
    this->unschedule(schedule_selector(CommunityHubLayer::onDeferredModeratorsRebuild));

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    HttpClient::get().get("/api/moderators", [self, attempt, tag](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([self, attempt, tag, success, response]() {
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;

            if (!success) {
                log::warn("[CommunityHub] loadModerators failed (attempt {}): {}", attempt, response);
                if (attempt < 3) {
                    float delay = 1.5f * (attempt + 1);
                    int nextAttempt = attempt + 1;
                    layer->m_pendingRetryTab = Tab::Moderators;
                    layer->m_pendingRetryAttempt = nextAttempt;
                    layer->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
                    layer->scheduleOnce(schedule_selector(CommunityHubLayer::onRetryTimer), delay);
                    return;
                }
                layer->hideLoading();
                layer->buildModeratorsList();
                return;
            }

            auto res = matjson::parse(response);
            if (!res.isOk()) {
                layer->hideLoading();
                layer->buildModeratorsList();
                return;
            }

            auto json = res.unwrap();
            if (json.contains("moderators") && json["moderators"].isArray()) {
                auto arrRes = json["moderators"].asArray();
                if (arrRes.isOk()) {
                    for (auto const& item : arrRes.unwrap()) {
                        ModEntry entry;
                        entry.username = item["username"].asString().unwrapOr("");
                        entry.role = item["role"].asString().unwrapOr("mod");
                        entry.accountID = item["accountID"].asInt().unwrapOr(0);
                        if (!entry.username.empty()) {
                            layer->m_modEntries.push_back(entry);
                        }
                    }
                }
            }

            if (layer->m_modEntries.empty()) {
                layer->hideLoading();
                layer->buildModeratorsList();
                return;
            }

            layer->m_pendingNativeModeratorLookupQueue.reserve(layer->m_modEntries.size());
            std::vector<std::pair<int, std::string>> accountIDsToFetch; // accountID, username
            for (auto const& entry : layer->m_modEntries) {
                ModProfileCache::get().store(entry.username, entry.role);

                auto score = GJUserScore::create();
                score->m_userName = entry.username;
                score->m_accountID = entry.accountID;
                score->m_iconID = 1;
                score->m_playerCube = 1;
                score->m_iconType = IconType::Cube;
                score->m_modBadge = (entry.role == "admin") ? 2 : 1;
                layer->m_modScores->addObject(score);

                if (entry.accountID > 0) {
                    accountIDsToFetch.push_back({entry.accountID, entry.username});
                } else {
                    layer->m_pendingNativeModeratorLookupQueue.push_back(entry.username);
                }
            }

            // Update session cache
            {
                s_cachedModEntries.clear();
                for (auto const& e : layer->m_modEntries) {
                    CachedModEntry ce;
                    ce.username = e.username;
                    ce.role = e.role;
                    ce.accountID = e.accountID;
                    s_cachedModEntries.push_back(ce);
                }
                s_cachedModScores = layer->m_modScores;
                s_modCacheTimestamp = std::time(nullptr);
                s_modCacheValid = true;
            }
            layer->onAllProfilesFetched();

            // Peticiones paralelas al GD API (getGJUserInfo20) para mods con accountID
            if (!accountIDsToFetch.empty()) {
                auto pendingCount = std::make_shared<std::atomic<int>>(static_cast<int>(accountIDsToFetch.size()));

                for (auto const& [accID, username] : accountIDsToFetch) {
                    auto req = web::WebRequest();
                    req.timeout(std::chrono::seconds(10));
                    req.acceptEncoding("gzip, deflate");
                    req.header("Content-Type", "application/x-www-form-urlencoded");
                    req.bodyString(
                        "targetAccountID=" + std::to_string(accID)
                        + "&secret=Wmfd2893gb7"
                        + "&gameVersion=22"
                        + "&binaryVersion=42"
                    );

                    WeakRef<CommunityHubLayer> weakSelf = layer.data();
                    int capturedTag = tag;

                    WebHelper::dispatch(std::move(req), "POST",
                        "https://www.boomlings.com/database/getGJUserInfo20.php",
                        [weakSelf, capturedTag, accID, username, pendingCount](web::WebResponse res) {
                            auto lyr = weakSelf.lock();
                            if (!lyr || lyr->m_isExiting || lyr->m_retryTag != capturedTag) {
                                pendingCount->fetch_sub(1, std::memory_order_acq_rel);
                                return;
                            }

                            bool applied = false;
                            if (res.ok()) {
                                auto body = res.string().unwrapOr("");
                                // Limpiar whitespace del response (GD puede incluir \r\n)
                                while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) {
                                    body.pop_back();
                                }
                                if (!body.empty() && body != "-1") {
                                    // getGJUserInfo20 devuelve formato key:value separado por ':'
                                    auto* dict = GameLevelManager::responseToDict(body, true);
                                    if (!dict) {
                                        // Fallback: intentar sin keyed (por si cambia el formato)
                                        dict = GameLevelManager::responseToDict(body, false);
                                    }
                                    if (dict) {
                                        auto* parsed = GJUserScore::create(dict);
                                        if (parsed) {
                                            // Asegurar accountID correcto
                                            if (parsed->m_accountID <= 0) {
                                                parsed->m_accountID = accID;
                                            }
                                            // Resolver iconID si esta en 0
                                            if (parsed->m_iconID <= 0) {
                                                switch (parsed->m_iconType) {
                                                    case IconType::Cube: parsed->m_iconID = parsed->m_playerCube; break;
                                                    case IconType::Ship: parsed->m_iconID = parsed->m_playerShip; break;
                                                    case IconType::Ball: parsed->m_iconID = parsed->m_playerBall; break;
                                                    case IconType::Ufo: parsed->m_iconID = parsed->m_playerUfo; break;
                                                    case IconType::Wave: parsed->m_iconID = parsed->m_playerWave; break;
                                                    case IconType::Robot: parsed->m_iconID = parsed->m_playerRobot; break;
                                                    case IconType::Spider: parsed->m_iconID = parsed->m_playerSpider; break;
                                                    case IconType::Swing: parsed->m_iconID = parsed->m_playerSwing; break;
                                                    case IconType::Jetpack: parsed->m_iconID = parsed->m_playerJetpack; break;
                                                    default: parsed->m_iconID = parsed->m_playerCube; break;
                                                }
                                            }
                                            if (parsed->m_iconID <= 0) parsed->m_iconID = 1;

                                            // Buscar por accountID O por username (mas robusto)
                                            auto wantedLower = toLowerCopy(username);
                                            for (auto* obj : CCArrayExt<GJUserScore*>(lyr->m_modScores)) {
                                                if (!obj) continue;
                                                if (obj->m_accountID == accID ||
                                                    toLowerCopy(static_cast<std::string>(obj->m_userName)) == wantedLower) {
                                                    applyNativeScoreToModerator(obj, parsed);
                                                    applied = true;
                                                    log::info("[CommunityHub] GD data aplicada para {} (accountID={})", username, accID);
                                                    break;
                                                }
                                            }
                                        } else {
                                            log::warn("[CommunityHub] GJUserScore::create fallo para accountID {}", accID);
                                        }
                                    } else {
                                        log::warn("[CommunityHub] responseToDict fallo para accountID {} (body size={})", accID, body.size());
                                    }
                                } else {
                                    log::warn("[CommunityHub] GD API devolvio respuesta vacia/-1 para accountID {}", accID);
                                }
                            } else {
                                log::warn("[CommunityHub] GD API fallo para accountID {}: HTTP {}", accID, res.code());
                            }

                            int remaining = pendingCount->fetch_sub(1, std::memory_order_acq_rel) - 1;
                            if (remaining <= 0) {
                                s_cachedModScores = lyr->m_modScores;
                                s_modCacheTimestamp = std::time(nullptr);
                                s_modCacheValid = true;
                                lyr->requestModeratorsListRebuild();
                            }
                        });
                }
            }

            // Buscar secuencialmente los que NO tienen accountID (fallback por username)
            if (!layer->m_pendingNativeModeratorLookupQueue.empty()) {
                layer->requestNativeModeratorLookup(layer->m_pendingNativeModeratorLookupQueue.front());
            }
        });
    });
}

void CommunityHubLayer::requestNativeModeratorLookup(std::string const& username) {
    if (m_isExiting || m_currentTab != Tab::Moderators) {
        return;
    }

    if (username.empty()) {
        onNativeModeratorSearchCompleted(username, 0);
        return;
    }

    if (auto* existingScore = findModeratorScore(m_modScores, username); existingScore && existingScore->m_accountID > 0) {
        onNativeModeratorSearchCompleted(username, existingScore->m_accountID);
        return;
    }

    auto* glm = GameLevelManager::get();
    auto* searchObj = GJSearchObject::create(SearchType::Users, username);
    if (!glm || !searchObj) {
        onNativeModeratorSearchCompleted(username, 0);
        return;
    }

    s_activeCommunityHubLayer = this;
    m_activeNativeModeratorSearchUsername = username;
    glm->getUsers(searchObj);
}

void CommunityHubLayer::requestNativeModeratorUserInfo(std::string const& username, int accountID) {
    if (m_isExiting || m_currentTab != Tab::Moderators) {
        return;
    }

    if (username.empty() || accountID <= 0) {
        onNativeModeratorUserInfoCompleted(username, accountID);
        return;
    }

    auto* glm = GameLevelManager::get();
    if (!glm) {
        onNativeModeratorUserInfoCompleted(username, accountID);
        return;
    }

    m_activeNativeModeratorUserInfoUsername = username;
    m_activeNativeModeratorUserInfoAccountID = accountID;
    glm->m_userInfoDelegate = this;

    if (auto* cached = glm->userInfoForAccountID(accountID); cached && cached->m_accountID > 0) {
        onNativeModeratorUserInfoCompleted(username, accountID);
        return;
    }

    glm->getGJUserInfo(accountID);
}

void CommunityHubLayer::onNativeModeratorSearchCompleted(std::string const& username, int accountID) {
    if (m_isExiting || m_currentTab != Tab::Moderators) {
        return;
    }

    m_activeNativeModeratorSearchUsername.clear();

    if (auto* score = findModeratorScore(m_modScores, username); score && accountID > 0) {
        score->m_accountID = accountID;
        requestModeratorsListRebuild();
    }

    if (accountID > 0) {
        requestNativeModeratorUserInfo(username, accountID);
        return;
    }

    ++m_nextNativeModeratorLookupIndex;
    if (m_nextNativeModeratorLookupIndex < m_pendingNativeModeratorLookupQueue.size()) {
        requestNativeModeratorLookup(m_pendingNativeModeratorLookupQueue[m_nextNativeModeratorLookupIndex]);
        return;
    }

    clearPendingNativeModeratorRequests();
}

void CommunityHubLayer::onNativeModeratorUserInfoCompleted(std::string const& username, int accountID) {
    if (m_isExiting || m_currentTab != Tab::Moderators) {
        return;
    }

    auto* score = findModeratorScore(m_modScores, username);
    if (score && accountID > 0) {
        score->m_accountID = accountID;
    }

    m_activeNativeModeratorUserInfoUsername.clear();
    m_activeNativeModeratorUserInfoAccountID = 0;
    requestModeratorsListRebuild();

    ++m_nextNativeModeratorLookupIndex;
    if (m_nextNativeModeratorLookupIndex < m_pendingNativeModeratorLookupQueue.size()) {
        requestNativeModeratorLookup(m_pendingNativeModeratorLookupQueue[m_nextNativeModeratorLookupIndex]);
        return;
    }

    clearPendingNativeModeratorRequests();
}

void CommunityHubLayer::clearPendingNativeModeratorRequests() {
    m_pendingNativeModeratorLookupQueue.clear();
    m_nextNativeModeratorLookupIndex = 0;
    m_activeNativeModeratorSearchUsername.clear();
    m_activeNativeModeratorUserInfoUsername.clear();
    m_activeNativeModeratorUserInfoAccountID = 0;

    auto* glm = GameLevelManager::get();
    if (glm && glm->m_userInfoDelegate == this) {
        glm->m_userInfoDelegate = nullptr;
    }

    auto activeLayerRef = s_activeCommunityHubLayer.lock();
    auto* activeLayer = static_cast<CommunityHubLayer*>(activeLayerRef.data());
    if (activeLayer == this) {
        s_activeCommunityHubLayer = WeakRef<CommunityHubLayer>();
    }
}

void CommunityHubLayer::getUserInfoFinished(GJUserScore* score) {
    if (m_activeNativeModeratorUserInfoUsername.empty()) {
        return;
    }

    // Apply the native GD score data directly to the moderator entry
    if (score && score->m_accountID > 0) {
        if (auto* modScore = findModeratorScore(m_modScores, m_activeNativeModeratorUserInfoUsername)) {
            applyNativeScoreToModerator(modScore, score);
            requestModeratorsListRebuild();
        }
    }

    int accountID = score && score->m_accountID > 0 ? score->m_accountID : m_activeNativeModeratorUserInfoAccountID;
    onNativeModeratorUserInfoCompleted(m_activeNativeModeratorUserInfoUsername, accountID);
}

void CommunityHubLayer::getUserInfoFailed(int type) {
    log::warn("[CommunityHub] Native moderator user info failed: {}", type);
    if (m_activeNativeModeratorUserInfoUsername.empty()) {
        return;
    }

    onNativeModeratorUserInfoCompleted(m_activeNativeModeratorUserInfoUsername, m_activeNativeModeratorUserInfoAccountID);
}

void CommunityHubLayer::userInfoChanged(GJUserScore* score) {}

void CommunityHubLayer::onAllProfilesFetched() {
    hideLoading();

    // Sort by original server order (admins first)
    if (m_modScores && m_modScores->count() > 0) {
        auto toVec = std::vector<Ref<GJUserScore>>();
        for (auto* obj : CCArrayExt<GJUserScore*>(m_modScores)) {
            if (obj) toVec.push_back(obj);
        }

        auto toLower = [](std::string const& str) {
            return geode::utils::string::toLower(str);
        };

        std::stable_sort(toVec.begin(), toVec.end(), [&](Ref<GJUserScore> const& a, Ref<GJUserScore> const& b) {
            // Admins (modBadge=2) first, then mods (modBadge=1)
            return a->m_modBadge > b->m_modBadge;
        });

        m_modScores->removeAllObjects();
        for (auto& s : toVec) {
            m_modScores->addObject(s.data());
        }
    }

    buildModeratorsList();
}

void CommunityHubLayer::requestModeratorsListRebuild() {
    if (m_isExiting || m_currentTab != Tab::Moderators) {
        return;
    }

    m_moderatorsRebuildQueued = true;
    this->unschedule(schedule_selector(CommunityHubLayer::onDeferredModeratorsRebuild));
    this->scheduleOnce(schedule_selector(CommunityHubLayer::onDeferredModeratorsRebuild), 0.35f);
}

void CommunityHubLayer::onDeferredModeratorsRebuild(float) {
    m_moderatorsRebuildQueued = false;
    if (m_isExiting || m_currentTab != Tab::Moderators) {
        return;
    }

    buildModeratorsList();
}

void CommunityHubLayer::buildModeratorsList() {
    m_moderatorsRebuildQueued = false;
    m_isLoadingTab = false;
    for (auto tab : m_tabs) tab->setEnabled(true);
    clearList();
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& loc = Localization::get();

    if (!m_modScores || m_modScores->count() == 0) {
        m_listContainer = CCNode::create();
        this->addChild(m_listContainer, 5);
        auto lbl = CCLabelBMFont::create(loc.getString("community.no_data").c_str(), "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        m_listContainer->addChild(lbl);
        addInfoButton(Tab::Moderators);
        return;
    }

    m_listContainer = CCNode::create();
    this->addChild(m_listContainer, 5);

    float listW = 380.f;
    float cellH = 50.f;
    float listH = winSize.height - 90.f;
    int count = m_modScores->count();
    float totalH = std::max(listH, cellH * static_cast<float>(count));

    auto scrollView = geode::ScrollLayer::create({listW, listH});
    scrollView->setPosition({winSize.width / 2 - listW / 2, 20.f});

    auto content = CCLayer::create();
    content->setContentSize({listW, totalH});
    content->setPosition({0, 0});
    scrollView->m_contentLayer->addChild(content);
    scrollView->m_contentLayer->setContentSize({listW, totalH});

    m_listContainer->addChild(scrollView);
    m_scrollView = scrollView;

    auto* gm = GameManager::sharedState();
    auto& profileThumbs = ProfileThumbs::get();

    int i = 0;
    for (auto* score : CCArrayExt<GJUserScore*>(m_modScores)) {
        if (!score) { i++; continue; }

        if (score->m_accountID > 0) {
            profileThumbs.notifyVisible(score->m_accountID);
        }

        float y = totalH - (i + 0.5f) * cellH;

        auto cell = CCNode::create();
        cell->setContentSize({listW, cellH - 2.f});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({listW / 2, y});
        content->addChild(cell);

        float cellInnerH = cellH - 2.f;

        // --- ScoreCell-style banner background ---
        bool hasBanner = false;
        auto config = profileThumbs.getProfileConfig(score->m_accountID);
        auto* cachedEntry = profileThumbs.getCachedProfile(score->m_accountID);
        CCTexture2D* thumbTex = cachedEntry ? cachedEntry->texture.data() : nullptr;
        std::string gifKey = config.gifKey;
        bool hasReadyGif = !gifKey.empty() && AnimatedGIFSprite::isCached(gifKey);
        bool hasStableRenderableProfile = hasReadyGif || (thumbTex && gifKey.empty());

        if (hasStableRenderableProfile) {
            m_requestedModeratorProfiles.erase(score->m_accountID);
        }

        bool shouldQueueProfile = score->m_accountID > 0 && !score->m_userName.empty() &&
            (!thumbTex || (!gifKey.empty() && !hasReadyGif));
        bool queuedNow = shouldQueueProfile && m_requestedModeratorProfiles.insert(score->m_accountID).second;
        if (queuedNow) {
            WeakRef<CommunityHubLayer> safeLayer = this;
            profileThumbs.queueLoad(score->m_accountID, score->m_userName, [safeLayer, accountID = score->m_accountID](bool success, CCTexture2D*) {
                auto layerRef = safeLayer.lock();
                auto* layer = static_cast<CommunityHubLayer*>(layerRef.data());
                if (!layer || layer->m_isExiting || layer->m_currentTab != Tab::Moderators) return;

                auto* refreshed = ProfileThumbs::get().getCachedProfile(accountID);
                bool hasRenderableProfile = refreshed && (refreshed->texture ||
                    (!refreshed->gifKey.empty() && AnimatedGIFSprite::isCached(refreshed->gifKey)));
                if (!success && !hasRenderableProfile) return;

                Loader::get()->queueInMainThread([safeLayer]() {
                    auto layerRef2 = safeLayer.lock();
                    auto* layer2 = static_cast<CommunityHubLayer*>(layerRef2.data());
                    if (!layer2 || layer2->m_isExiting || layer2->m_currentTab != Tab::Moderators) return;
                    layer2->requestModeratorsListRebuild();
                });
            });
        }

        if (thumbTex || hasReadyGif) {
            CCNode* bgNode = nullptr;
            CCSize targetSize = {listW, cellInnerH};

            if (hasReadyGif) {
                auto bgGif = AnimatedGIFSprite::createFromCache(gifKey);
                if (bgGif) {
                    float scaleX = targetSize.width / bgGif->getContentSize().width;
                    float scaleY = targetSize.height / bgGif->getContentSize().height;
                    float sc = std::max(scaleX, scaleY);
                    bgGif->setScale(sc);
                    bgGif->setAnchorPoint({0.5f, 0.5f});
                    bgGif->setPosition(targetSize * 0.5f);
                    auto shader = Shaders::getBlurCellShader();
                    if (shader) bgGif->setShaderProgram(shader);
                    float norm = 2.0f / 9.0f;
                    bgGif->m_intensity = std::min(1.7f, norm * 2.5f);
                    if (bgGif->getTexture()) bgGif->m_texSize = bgGif->getTexture()->getContentSizeInPixels();
                    bgGif->play();
                    bgNode = bgGif;
                }
            }

            if (!bgNode && thumbTex) {
                auto blurred = BlurSystem::getInstance()->createBlurredSprite(thumbTex, targetSize, 6.0f);
                if (blurred) {
                    blurred->setPosition(targetSize * 0.5f);
                    bgNode = blurred;
                }
            }

            if (bgNode) {
                auto stencil = PaimonDrawNode::create();
                CCPoint rect[4] = {
                    ccp(0, 0), ccp(listW, 0),
                    ccp(listW, cellInnerH), ccp(0, cellInnerH)
                };
                ccColor4F white = {1, 1, 1, 1};
                stencil->drawPolygon(rect, 4, white, 0, white);

                auto clipper = CCClippingNode::create(stencil);
                clipper->setContentSize({listW, cellInnerH});
                clipper->setPosition({0, 0});
                cell->addChild(clipper, -2);

                CCSize bgSize = bgNode->getContentSize();
                if (bgSize.width > 0 && bgSize.height > 0) {
                    float scaleToFitX = listW / bgSize.width;
                    float scaleToFitY = cellInnerH / bgSize.height;
                    float finalScale = std::max(scaleToFitX, scaleToFitY);
                    bgNode->setScale(finalScale);
                }
                bgNode->setAnchorPoint({0.5f, 0.5f});
                bgNode->setPosition({listW / 2, cellInnerH / 2});
                clipper->addChild(bgNode);

                float darkness = config.hasConfig ? config.darkness : 0.35f;
                if (darkness > 0.0f) {
                    auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(darkness * 255)});
                    overlay->setContentSize({listW, cellInnerH});
                    overlay->setPosition({0, 0});
                    cell->addChild(overlay, -1);
                }
                hasBanner = true;
            }
        }

        if (!hasBanner) {
            // fallback: alternating dark background
            auto cellBg = paimon::SpriteHelper::createColorPanel(
                listW, cellInnerH,
                i % 2 == 0 ? ccColor3B{18, 18, 28} : ccColor3B{22, 22, 32}, 200);
            cellBg->setPosition({0, 0});
            cell->addChild(cellBg, 0);
        }

        // --- SimplePlayer icon ---
        float iconAreaW = 42.f;
        int iconID = score->m_iconID > 0 ? score->m_iconID : std::max(score->m_playerCube, 1);
        auto player = SimplePlayer::create(iconID);
        if (player && gm) {
            if (score->m_iconType != IconType::Cube) {
                player->updatePlayerFrame(iconID, score->m_iconType);
            }
            auto col1 = gm->colorForIdx(score->m_color1);
            auto col2 = gm->colorForIdx(score->m_color2);
            player->setColor(col1);
            player->setSecondColor(col2);
            if (score->m_glowEnabled) {
                player->setGlowOutline(col2);
            } else {
                player->disableGlowOutline();
            }
            float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
            if (maxDim > 0) player->setScale(30.f / maxDim);
        }

        float textX = iconAreaW + 8.f;

        // --- Username ---
        auto nameLbl = CCLabelBMFont::create(score->m_userName.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.4f);
        nameLbl->setAnchorPoint({0, 0.5f});
        float maxNameW = 160.f;
        if (nameLbl->getScaledContentSize().width > maxNameW) {
            nameLbl->setScale(nameLbl->getScale() * (maxNameW / nameLbl->getScaledContentSize().width));
        }
        float nameW = nameLbl->getScaledContentSize().width;

        // --- Clickable area (icon + name -> opens profile) ---
        float clickW = textX + nameW;
        auto clickNode = CCNode::create();
        clickNode->setContentSize({clickW, cellInnerH});
        clickNode->setAnchorPoint({0, 0.5f});

        if (player) {
            player->setPosition({iconAreaW / 2 + 4.f, cellInnerH / 2});
            clickNode->addChild(player, 5);
        }
        nameLbl->setPosition({textX, cellInnerH / 2});
        clickNode->addChild(nameLbl, 10);

        auto profileBtn = CCMenuItemSpriteExtra::create(
            clickNode, this, menu_selector(CommunityHubLayer::onModProfile));
        profileBtn->setTag(score->m_accountID);
        profileBtn->setAnchorPoint({0, 0.5f});
        profileBtn->setPosition({0, cellInnerH / 2});
        // subtle press: barely noticeable scale
        profileBtn->m_scaleMultiplier = 1.02f;
        PaimonButtonHighlighter::registerButton(profileBtn);

        auto menu = CCMenu::create();
        menu->setPosition(CCPointZero);
        menu->setContentSize({listW, cellInnerH});
        cell->addChild(menu, 5);
        menu->addChild(profileBtn);

        // --- Paimbnails badge ---
        bool isAdmin = (score->m_modBadge == 2);
        auto badge = CCSprite::create(
            isAdmin ? "paim_Admin.png"_spr : "paim_Moderador.png"_spr);
        if (badge) {
            float targetH = 15.5f;
            float badgeScale = targetH / badge->getContentSize().height;
            badge->setScale(badgeScale);
            badge->setPosition({textX + nameW + badge->getScaledContentSize().width / 2 + 6.f, cellInnerH / 2});
            cell->addChild(badge, 10);
        }

        // --- Global rank (right side) ---
        if (score->m_globalRank > 0) {
            auto rankStr = fmt::format("#{}", score->m_globalRank);
            auto rankLbl = CCLabelBMFont::create(rankStr.c_str(), "chatFont.fnt");
            rankLbl->setScale(0.4f);
            rankLbl->setColor({255, 200, 50});
            rankLbl->setAnchorPoint({1, 0.5f});
            rankLbl->setPosition({listW - 10.f, cellInnerH / 2});
            cell->addChild(rankLbl, 10);
        }
        i++;
    }

    // Scroll to top
    scrollView->m_contentLayer->setPositionY(listH - totalH);
    addInfoButton(Tab::Moderators);
}

void CommunityHubLayer::onModProfile(CCObject* sender) {
    int accountID = sender->getTag();
    if (accountID > 0) {
        ProfilePage::create(accountID, false)->show();
    }
}

// ==================== TOP CREATORS TAB ====================

void CommunityHubLayer::loadTopCreators(int attempt) {
    m_creatorEntries.clear();

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    HttpClient::get().getTopCreators([self, attempt, tag](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([self, attempt, tag, success, response]() {
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;

            if (!success) {
                log::warn("[CommunityHub] loadTopCreators failed (attempt {}): {}", attempt, response);
                if (attempt < 3) {
                    float delay = 1.5f * (attempt + 1);
                    int nextAttempt = attempt + 1;
                    layer->m_pendingRetryTab = Tab::TopCreators;
                    layer->m_pendingRetryAttempt = nextAttempt;
                    layer->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
                    layer->scheduleOnce(schedule_selector(CommunityHubLayer::onRetryTimer), delay);
                    return;
                }
            }

            if (success) {
                auto res = matjson::parse(response);
                if (res.isOk()) {
                    auto json = res.unwrap();
                    if (json.contains("creators") && json["creators"].isArray()) {
                        auto arrRes = json["creators"].asArray();
                        if (arrRes.isOk()) {
                            for (auto const& item : arrRes.unwrap()) {
                                CreatorEntry entry;
                                entry.username = item["username"].asString().unwrapOr("Unknown");
                                entry.accountID = item["accountID"].asInt().unwrapOr(0);
                                entry.uploadCount = item["uploadCount"].asInt().unwrapOr(0);
                                entry.avgRating = (float)item["avgRating"].asDouble().unwrapOr(0.0);
                                layer->m_creatorEntries.push_back(entry);
                            }
                        }
                    }
                }
            }

            layer->hideLoading();
            layer->buildCreatorsList();
        });
    });
}

void CommunityHubLayer::buildCreatorsList() {
    m_isLoadingTab = false;
    for (auto tab : m_tabs) tab->setEnabled(true);
    clearList();
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& loc = Localization::get();

    m_listContainer = CCNode::create();
    this->addChild(m_listContainer, 5);

    if (m_creatorEntries.empty()) {
        auto lbl = CCLabelBMFont::create(loc.getString("community.no_data").c_str(), "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        m_listContainer->addChild(lbl);
        addInfoButton(Tab::TopCreators);
        return;
    }

    float listW = 380.f;
    float cellH = 40.f;
    float listH = winSize.height - 90.f;
    float totalH = std::max(listH, cellH * (float)m_creatorEntries.size());

    auto scrollView = geode::ScrollLayer::create({listW, listH});
    scrollView->setPosition({winSize.width / 2 - listW / 2, 20.f});

    auto content = CCLayer::create();
    content->setContentSize({listW, totalH});
    content->setPosition({0, 0});
    scrollView->m_contentLayer->addChild(content);
    scrollView->m_contentLayer->setContentSize({listW, totalH});

    m_listContainer->addChild(scrollView);
    m_scrollView = scrollView;

    for (int i = 0; i < (int)m_creatorEntries.size(); i++) {
        auto& entry = m_creatorEntries[i];
        float y = totalH - (i + 0.5f) * cellH;

        auto cell = CCNode::create();
        cell->setContentSize({listW, cellH - 2.f});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({listW / 2, y});
        content->addChild(cell);

        // alternating background
        auto cellBg = paimon::SpriteHelper::createColorPanel(
            listW, cellH - 2.f,
            i % 2 == 0 ? ccColor3B{18, 18, 28} : ccColor3B{22, 22, 32}, 200);
        cellBg->setPosition({0, 0});
        cell->addChild(cellBg, 0);

        float textX = 10.f;
        float cellMidY = (cellH - 2.f) / 2;

        // rank number
        auto numLbl = CCLabelBMFont::create(fmt::format("#{}", i + 1).c_str(), "chatFont.fnt");
        numLbl->setScale(0.5f);
        numLbl->setColor({255, 200, 50});
        numLbl->setAnchorPoint({0, 0.5f});
        numLbl->setPosition({textX, cellMidY});
        cell->addChild(numLbl, 10);

        // username
        float nameX = 45.f;
        auto nameLbl = CCLabelBMFont::create(entry.username.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.4f);
        nameLbl->setAnchorPoint({0, 0.5f});
        nameLbl->setPosition({nameX, cellMidY + 6.f});
        float maxNameW = 180.f;
        if (nameLbl->getScaledContentSize().width > maxNameW) {
            nameLbl->setScale(nameLbl->getScale() * (maxNameW / nameLbl->getScaledContentSize().width));
        }
        cell->addChild(nameLbl, 10);

        // stats line
        auto statsStr = fmt::format("{}: {}  |  {}: {:.1f}",
            loc.getString("community.uploads"), entry.uploadCount,
            loc.getString("community.avg_rating"), entry.avgRating);
        auto statsLbl = CCLabelBMFont::create(statsStr.c_str(), "chatFont.fnt");
        statsLbl->setScale(0.38f);
        statsLbl->setColor({180, 200, 220});
        statsLbl->setAnchorPoint({0, 0.5f});
        statsLbl->setPosition({nameX, cellMidY - 7.f});
        cell->addChild(statsLbl, 10);
    }

    // Scroll to top
    scrollView->m_contentLayer->setPositionY(listH - totalH);
    addInfoButton(Tab::TopCreators);
}

// ==================== TOP THUMBNAILS TAB ====================

void CommunityHubLayer::loadTopThumbnails(int attempt) {
    m_thumbnailEntries.clear();

    WeakRef<CommunityHubLayer> self = this;
    int tag = m_retryTag;
    HttpClient::get().getTopThumbnails([self, attempt, tag](bool success, std::string const& response) {
        Loader::get()->queueInMainThread([self, attempt, tag, success, response]() {
            auto layer = self.lock();
            if (!layer || layer->m_isExiting || layer->m_retryTag != tag) return;

            if (!success) {
                log::warn("[CommunityHub] loadTopThumbnails failed (attempt {}): {}", attempt, response);
                if (attempt < 3) {
                    float delay = 1.5f * (attempt + 1);
                    int nextAttempt = attempt + 1;
                    layer->m_pendingRetryTab = Tab::TopThumbnails;
                    layer->m_pendingRetryAttempt = nextAttempt;
                    layer->unschedule(schedule_selector(CommunityHubLayer::onRetryTimer));
                    layer->scheduleOnce(schedule_selector(CommunityHubLayer::onRetryTimer), delay);
                    return;
                }
            }

            if (success) {
                auto res = matjson::parse(response);
                if (res.isOk()) {
                    auto json = res.unwrap();
                    if (json.contains("thumbnails") && json["thumbnails"].isArray()) {
                        auto arrRes = json["thumbnails"].asArray();
                        if (arrRes.isOk()) {
                            for (auto const& item : arrRes.unwrap()) {
                                ThumbnailEntry entry;
                                entry.levelId = item["levelId"].asInt().unwrapOr(0);
                                entry.rating = (float)item["rating"].asDouble().unwrapOr(0.0);
                                entry.count = item["count"].asInt().unwrapOr(0);
                                entry.uploadedBy = item["uploadedBy"].asString().unwrapOr("Unknown");
                                entry.accountID = item["accountID"].asInt().unwrapOr(0);
                                if (entry.levelId > 0) {
                                    layer->m_thumbnailEntries.push_back(entry);
                                }
                            }
                        }
                    }
                }
            }

            layer->hideLoading();
            layer->buildThumbnailsList();
        });
    });
}

void CommunityHubLayer::buildThumbnailsList() {
    m_isLoadingTab = false;
    for (auto tab : m_tabs) tab->setEnabled(true);
    clearList();
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& loc = Localization::get();

    m_listContainer = CCNode::create();
    this->addChild(m_listContainer, 5);

    if (m_thumbnailEntries.empty()) {
        auto lbl = CCLabelBMFont::create(loc.getString("community.no_data").c_str(), "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        m_listContainer->addChild(lbl);
        addInfoButton(Tab::TopThumbnails);
        return;
    }

    float listW = 380.f;
    float cellH = 55.f;
    float listH = winSize.height - 90.f;
    float totalH = std::max(listH, cellH * (float)m_thumbnailEntries.size());

    auto scrollView = geode::ScrollLayer::create({listW, listH});
    scrollView->setPosition({winSize.width / 2 - listW / 2, 20.f});

    auto content = CCLayer::create();
    content->setContentSize({listW, totalH});
    content->setPosition({0, 0});
    scrollView->m_contentLayer->addChild(content);
    scrollView->m_contentLayer->setContentSize({listW, totalH});

    m_listContainer->addChild(scrollView);
    m_scrollView = scrollView;

    for (int i = 0; i < (int)m_thumbnailEntries.size(); i++) {
        auto& entry = m_thumbnailEntries[i];
        float y = totalH - (i + 0.5f) * cellH;

        auto cell = CCNode::create();
        cell->setContentSize({listW, cellH - 2.f});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({listW / 2, y});
        content->addChild(cell);

        // alternating background
        auto cellBg = paimon::SpriteHelper::createColorPanel(
            listW, cellH - 2.f,
            i % 2 == 0 ? ccColor3B{18, 18, 28} : ccColor3B{22, 22, 32}, 200);
        cellBg->setPosition({0, 0});
        cell->addChild(cellBg, 0);

        // thumbnail preview
        float thumbSize = cellH - 8.f;
        float thumbW = thumbSize * 1.6f;
        float thumbH = thumbSize;
        float thumbX = 4.f;
        float thumbY = (cellH - 2.f - thumbH) / 2.f;

        // Dark background (always visible behind the image)
        auto thumbBg = CCLayerColor::create({30, 28, 40, 255});
        thumbBg->setContentSize({thumbW, thumbH});
        thumbBg->setPosition({thumbX, thumbY});
        cell->addChild(thumbBg, 1);

        // Clipping node to contain the sprite exactly within thumb bounds
        auto thumbStencil = paimon::SpriteHelper::createRectStencil(thumbW, thumbH);
        auto thumbClipper = CCClippingNode::create(thumbStencil);
        thumbClipper->setContentSize({thumbW, thumbH});
        thumbClipper->setPosition({thumbX, thumbY});
        cell->addChild(thumbClipper, 2);

        int levelID = entry.levelId;
        auto localTex = LocalThumbs::get().loadTexture(levelID);

        if (localTex) {
            auto spr = CCSprite::createWithTexture(localTex);
            if (spr) {
                float scale = std::max(thumbW / spr->getContentSize().width, thumbH / spr->getContentSize().height);
                spr->setScale(scale);
                spr->setPosition({thumbW / 2.f, thumbH / 2.f});
                thumbClipper->addChild(spr);
            }
        } else if (levelID > 0) {
            std::string fileName = fmt::format("{}.png", levelID);
            Ref<CCNode> safeClipper = thumbClipper;
            ThumbnailLoader::get().requestLoad(levelID, fileName, [safeClipper, thumbW, thumbH](CCTexture2D* tex, bool) {
                if (!safeClipper || !safeClipper->getParent() || !tex) return;
                auto spr = CCSprite::createWithTexture(tex);
                if (!spr) return;
                float scale = std::max(thumbW / spr->getContentSize().width, thumbH / spr->getContentSize().height);
                spr->setScale(scale);
                spr->setPosition({thumbW / 2.f, thumbH / 2.f});
                safeClipper->addChild(spr);
            });
        }

        float textX = thumbSize * 1.6f + 12.f;
        float cellMidY = (cellH - 2.f) / 2;

        // rank number
        auto numLbl = CCLabelBMFont::create(fmt::format("#{}", i + 1).c_str(), "chatFont.fnt");
        numLbl->setScale(0.4f);
        numLbl->setColor({255, 200, 50});
        numLbl->setAnchorPoint({0, 0.5f});
        numLbl->setPosition({textX, cellMidY + 14.f});
        cell->addChild(numLbl, 10);

        // level name
        auto saved = GameLevelManager::get()->getSavedLevel(levelID);
        std::string levelName = saved ? std::string(saved->m_levelName) : fmt::format("{} {}", loc.getString("community.level"), levelID);
        auto nameLbl = CCLabelBMFont::create(levelName.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.35f);
        nameLbl->setAnchorPoint({0, 0.5f});
        nameLbl->setPosition({textX, cellMidY + 2.f});
        float maxNameW = listW - textX - 80.f;
        if (nameLbl->getScaledContentSize().width > maxNameW) {
            nameLbl->setScale(nameLbl->getScale() * (maxNameW / nameLbl->getScaledContentSize().width));
        }
        cell->addChild(nameLbl, 10);

        // creator + stats
        auto infoStr = fmt::format("{} {} | {}: {:.1f} ({} {})",
            loc.getString("community.by"), entry.uploadedBy,
            loc.getString("community.rating"), entry.rating,
            entry.count, loc.getString("community.votes"));
        auto infoLbl = CCLabelBMFont::create(infoStr.c_str(), "chatFont.fnt");
        infoLbl->setScale(0.35f);
        infoLbl->setColor({180, 200, 220});
        infoLbl->setAnchorPoint({0, 0.5f});
        infoLbl->setPosition({textX, cellMidY - 10.f});
        cell->addChild(infoLbl, 10);
    }

    // Scroll to top
    scrollView->m_contentLayer->setPositionY(listH - totalH);
    addInfoButton(Tab::TopThumbnails);
}

void CommunityHubLayer::addInfoButton(Tab tab) {
    if (!m_listContainer) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto infoMenu = CCMenu::create();
    infoMenu->setPosition(0, 0);
    infoMenu->setZOrder(30);

    auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoBtn_001.png");
    if (infoSpr) infoSpr->setScale(0.65f);

    if (!infoSpr) {
        // fallback: small question-mark label
        auto fallback = CCLabelBMFont::create("?", "bigFont.fnt");
        fallback->setScale(0.5f);
        auto infoBtn = CCMenuItemSpriteExtra::create(
            fallback, this, menu_selector(CommunityHubLayer::onInfoButton));
        infoBtn->setTag(static_cast<int>(tab));
        infoBtn->setPosition({22.f, 23.f});
        infoMenu->addChild(infoBtn);
    } else {
        auto infoBtn = CCMenuItemSpriteExtra::create(
            infoSpr, this, menu_selector(CommunityHubLayer::onInfoButton));
        infoBtn->setTag(static_cast<int>(tab));
        infoBtn->setPosition({22.f, 23.f});
        infoMenu->addChild(infoBtn);
    }

    m_listContainer->addChild(infoMenu, 30);
}

void CommunityHubLayer::onInfoButton(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;

    auto tab = static_cast<Tab>(btn->getTag());
    auto& loc = Localization::get();

    std::string title, body;
    switch (tab) {
        case Tab::Moderators:
            title = loc.getString("community.info_mods_title");
            body  = loc.getString("community.info_mods_body");
            break;
        case Tab::TopCreators:
            title = loc.getString("community.info_creators_title");
            body  = loc.getString("community.info_creators_body");
            break;
        case Tab::TopThumbnails:
            title = loc.getString("community.info_thumbs_title");
            body  = loc.getString("community.info_thumbs_body");
            break;
        case Tab::CompatibleMods:
            title = loc.getString("community.info_compat_title");
            body  = loc.getString("community.info_compat_body");
            break;
        default:
            title = "Info";
            body  = "";
            break;
    }

    FLAlertLayer::create(nullptr, title.c_str(), body, "OK", nullptr, 350.f)->show();
}

void CommunityHubLayer::loadCompatibleMods() {
    hideLoading();
    buildCompatibleModsList();
}

void CommunityHubLayer::buildCompatibleModsList() {
    m_isLoadingTab = false;
    for (auto tab : m_tabs) tab->setEnabled(true);
    clearList();

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto& loc = Localization::get();

    m_listContainer = CCNode::create();
    this->addChild(m_listContainer, 5);

    // Title label
    auto titleLbl = CCLabelBMFont::create(
        loc.getString("community.compat_mods_title").c_str(), "bigFont.fnt");
    titleLbl->setScale(0.5f);
    titleLbl->setPosition({winSize.width / 2, winSize.height / 2 + 60.f});
    m_listContainer->addChild(titleLbl, 10);

    // Version badge
    auto versionLbl = CCLabelBMFont::create(
        loc.getString("community.compat_mods_version").c_str(), "goldFont.fnt");
    versionLbl->setScale(0.55f);
    versionLbl->setColor({255, 200, 50});
    versionLbl->setPosition({winSize.width / 2, winSize.height / 2 + 20.f});
    m_listContainer->addChild(versionLbl, 10);

    // Description
    auto descLbl = CCLabelBMFont::create(
        loc.getString("community.compat_mods_desc").c_str(), "chatFont.fnt");
    descLbl->setScale(0.55f);
    descLbl->setOpacity(200);
    descLbl->setAlignment(kCCTextAlignmentCenter);
    descLbl->setPosition({winSize.width / 2, winSize.height / 2 - 20.f});
    m_listContainer->addChild(descLbl, 10);

    // Coming soon label
    auto soonLbl = CCLabelBMFont::create(
        loc.getString("community.compat_mods_soon").c_str(), "chatFont.fnt");
    soonLbl->setScale(0.5f);
    soonLbl->setOpacity(150);
    soonLbl->setColor({180, 180, 180});
    soonLbl->setPosition({winSize.width / 2, winSize.height / 2 - 55.f});
    m_listContainer->addChild(soonLbl, 10);
    addInfoButton(Tab::CompatibleMods);
}
