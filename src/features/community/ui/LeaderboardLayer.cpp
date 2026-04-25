#include "LeaderboardLayer.hpp"
#include "LeaderboardHistoryLayer.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../transitions/services/TransitionManager.hpp"
#include <Geode/binding/CreatorLayer.hpp>
#include <Geode/binding/LevelSearchLayer.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/LevelTools.hpp>
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../thumbnails/services/LevelColors.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include <Geode/utils/web.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <matjson.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../../../utils/Shaders.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../foryou/services/ForYouTracker.hpp"
#include "../../foryou/services/ForYouEngine.hpp"
#include "../../foryou/services/LevelTagsIntegration.hpp"
#include "../../foryou/ui/ForYouPreferencesPopup.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include <random>
#include <cmath>

using namespace geode::prelude;
using namespace Shaders;

namespace {
    class LeaderboardPaimonSprite : public CCSprite {
    public:
        float m_intensity = 1.0f;
        float m_brightness = 1.0f;
        CCSize m_texSize = {0, 0};
        
        static LeaderboardPaimonSprite* create() {
            auto sprite = new LeaderboardPaimonSprite();
            if (sprite && sprite->init()) {
                sprite->autorelease();
                return sprite;
            }
            CC_SAFE_DELETE(sprite);
            return nullptr;
        }

        static LeaderboardPaimonSprite* createWithTexture(CCTexture2D* texture) {
            auto sprite = new LeaderboardPaimonSprite();
            if (sprite && sprite->initWithTexture(texture)) {
                sprite->autorelease();
                return sprite;
            }
            CC_SAFE_DELETE(sprite);
            return nullptr;
        }

        void draw() override {
            if (getShaderProgram()) {
                getShaderProgram()->use();
                getShaderProgram()->setUniformsForBuiltins();
                
                GLint intensityLoc = getShaderProgram()->getUniformLocationForName("u_intensity");
                if (intensityLoc != -1) {
                    getShaderProgram()->setUniformLocationWith1f(intensityLoc, m_intensity);
                }
                
                GLint brightLoc = getShaderProgram()->getUniformLocationForName("u_brightness");
                if (brightLoc != -1) {
                    getShaderProgram()->setUniformLocationWith1f(brightLoc, m_brightness);
                }

                GLint sizeLoc = getShaderProgram()->getUniformLocationForName("u_texSize");
                if (sizeLoc != -1) {
                    if (m_texSize.width == 0 && getTexture()) {
                        m_texSize = getTexture()->getContentSizeInPixels();
                    }
                    float w = m_texSize.width > 0 ? m_texSize.width : 1.0f;
                    float h = m_texSize.height > 0 ? m_texSize.height : 1.0f;
                    getShaderProgram()->setUniformLocationWith2f(sizeLoc, w, h);
                }
            }
            CCSprite::draw();
        }
    };
}

static LeaderboardPaimonSprite* createLeaderboardBlurredSprite(CCTexture2D* texture, CCSize const& targetSize, float blurRadius = 0.04f) {
    auto blurResult = BlurSystem::getInstance()->createBlurredSprite(texture, targetSize, blurRadius);
    if (!blurResult) return nullptr;
    auto finalSprite = LeaderboardPaimonSprite::createWithTexture(blurResult->getTexture());
    if (finalSprite) finalSprite->setFlipY(true);
    return finalSprite;
}

static FMOD::ChannelGroup* ensureLeaderboardAudioGroup(FMOD::System* system, FMOD::ChannelGroup*& group) {
    if (!system) return nullptr;
    if (group) {
        bool muted = false;
        if (group->getMute(&muted) == FMOD_OK) {
            return group;
        }
        group = nullptr;
    }

    FMOD_RESULT result = system->createChannelGroup("PaimonLeaderboardAudio", &group);
    if (result != FMOD_OK || !group) {
        return nullptr;
    }

    FMOD::ChannelGroup* master = nullptr;
    if (system->getMasterChannelGroup(&master) == FMOD_OK && master) {
        master->addGroup(group);
    }
    return group;
}

LeaderboardLayer* LeaderboardLayer::create(BackTarget backTarget) {
    auto ret = new LeaderboardLayer();
    if (ret && ret->init()) {
        ret->m_backTarget = backTarget;
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* LeaderboardLayer::scene(BackTarget backTarget) {
    auto scene = CCScene::create();
    auto layer = LeaderboardLayer::create(backTarget);
    scene->addChild(layer);
    return scene;
}

bool LeaderboardLayer::init() {
    if (!CCLayer::init()) return false;
    log::info("[PaimonLeaderboard] init");
    
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // fondo base oscuro
    auto bg = CCLayerColor::create(ccc4(12, 10, 20, 255));
    bg->setID("background"_spr);
    bg->setContentSize(winSize);
    bg->setZOrder(-10);
    this->addChild(bg);

    // fondo dinamico encima
    m_bgSprite = LeaderboardPaimonSprite::create(); 
    m_bgSprite->setPosition(winSize / 2);
    m_bgSprite->setVisible(false);
    m_bgSprite->setZOrder(-5);
    this->addChild(m_bgSprite);

    // capa negra para transiciones
    m_bgOverlay = CCLayerColor::create({0, 0, 0, 0});
    m_bgOverlay->setContentSize(winSize);
    m_bgOverlay->setZOrder(-4);
    this->addChild(m_bgOverlay);

    // contenedor de particulas
    m_particleContainer = CCNode::create();
    m_particleContainer->setPosition({0, 0});
    m_particleContainer->setZOrder(-3);
    this->addChild(m_particleContainer);

    // vignette oscura en los bordes
    auto vignette = CCLayerColor::create({0, 0, 0, 50});
    vignette->setContentSize(winSize);
    vignette->setZOrder(-2);
    this->addChild(vignette);

    // glow overlay (pulsa con la musica — color tematico suave)
    m_glowOverlay = CCLayerColor::create({255, 180, 50, 0});
    m_glowOverlay->setContentSize(winSize);
    m_glowOverlay->setZOrder(-1);
    this->addChild(m_glowOverlay);

    // beat flash (destello blanco en beats fuertes)
    m_beatFlash = CCLayerColor::create({255, 255, 255, 0});
    m_beatFlash->setContentSize(winSize);
    m_beatFlash->setZOrder(-1);
    this->addChild(m_beatFlash);

    this->scheduleUpdate();

    // boton de volver
    auto menu = CCMenu::create();
    menu->setPosition(0, 0);
    menu->setZOrder(20);
    this->addChild(menu);

    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this,
        menu_selector(LeaderboardLayer::onBack)
    );
    backBtn->setPosition(25, winSize.height - 25);
    menu->addChild(backBtn);

    // pestanas de arriba
    auto tabMenu = CCMenu::create();
    tabMenu->setPosition(0, 0);
    tabMenu->setZOrder(10);
    this->addChild(tabMenu);
    m_tabsMenu = tabMenu;

    auto createTab = [&](char const* text, char const* id, CCPoint pos) -> CCMenuItemToggler* {
        auto createBtn = [&](char const* frameName) -> CCNode* {
            auto sprite = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName(frameName);
            sprite->setContentSize({110.f, 32.f});
            auto label = CCLabelBMFont::create(text, "goldFont.fnt");
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

        auto tab = CCMenuItemToggler::create(offSprite, onSprite, this, menu_selector(LeaderboardLayer::onTab));
        tab->setUserObject(CCString::create(id));
        tab->setPosition(pos);
        m_tabs.push_back(tab);

        return tab;
    };

    float topY = winSize.height - 22.f;
    float centerX = winSize.width / 2;
    float btnSpacing = 110.f;
    auto dailyBtn = createTab(Localization::get().getString("leaderboard.daily").c_str(), "daily", {centerX - btnSpacing, topY});
    dailyBtn->toggle(true); 
    tabMenu->addChild(dailyBtn);

    auto weeklyBtn = createTab(Localization::get().getString("leaderboard.weekly").c_str(), "weekly", {centerX, topY});
    tabMenu->addChild(weeklyBtn);

    auto forYouBtn = createTab(Localization::get().getString("foryou.tab").c_str(), "foryou", {centerX + btnSpacing, topY});
    tabMenu->addChild(forYouBtn);

    // boton historial (icono de lista)
    auto historySpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_menuBtn_001.png");
    if (!historySpr) historySpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plainBtn_001.png");
    if (historySpr) {
        historySpr->setScale(0.45f);
        auto historyBtn = CCMenuItemSpriteExtra::create(
            historySpr, this, menu_selector(LeaderboardLayer::onHistory));
        historyBtn->setPosition({winSize.width - 30.f, winSize.height - 25.f});
        m_historyButton = historyBtn;
        menu->addChild(historyBtn);

        auto histLabel = CCLabelBMFont::create("H", "bigFont.fnt");
        histLabel->setScale(0.9f);
        histLabel->setPosition(historySpr->getContentSize() / 2);
        historySpr->addChild(histLabel, 10);
    }

    // spinner centrado
    m_loadingSpinner = PaimonLoadingOverlay::create("Loading...", 50.f);
    if (m_loadingSpinner) {
        m_loadingSpinner->show(this, 100);
    }

    this->setKeypadEnabled(true);

    // fade-out suave de la musica de menu al entrar
    fadeOutMenuMusic();

    // reset flags
    m_dataLoaded = false;
    m_thumbLoaded = false;
    m_listCreated = false;

    loadLeaderboard("daily");

    return true;
}

void LeaderboardLayer::onEnterTransitionDidFinish() {
    CCLayer::onEnterTransitionDidFinish();
    // si volvemos de un push (ej: LevelInfoLayer o HistoryLayer)
    // y la musica cueva ya estaba lista, reanudarla con fade-in
    if (m_levelMusicChannel && !m_musicPlaying && !m_leavingForGood) {
        // canal existe pero esta pausado — fade-in
        bool isPaused = false;
        m_levelMusicChannel->getPaused(&isPaused);
        if (isPaused) {
            m_levelMusicChannel->setPaused(false);
            m_musicPlaying = true;
            auto engine = FMODAudioEngine::sharedEngine();
            float target = engine ? engine->m_musicVolume * 0.55f : 0.4f;
            m_levelMusicChannel->setVolume(0.f);
            m_isFadingCaveIn = true;
            m_isFadingCaveOut = false;
            executeCaveFade(0, AUDIO_FADE_STEPS, 0.f, target, false);
        }
    }
    m_goingToHistory = false;
    // silenciar BG inmediatamente
    ensureBgSilenced();
    // DynamicSongManager::stopSong() restaura BG en un thread con delay,
    // asi que re-silenciamos varias veces para ganarle
    this->scheduleOnce(schedule_selector(LeaderboardLayer::delaySilenceBg), 0.3f);
    this->scheduleOnce(schedule_selector(LeaderboardLayer::delaySilenceBg2), 0.7f);
}

void LeaderboardLayer::onExit() {
    ++m_lifecycleToken;
    m_isFadingCaveIn = false;
    m_isFadingCaveOut = false;

    this->unscheduleUpdate();
    this->unschedule(schedule_selector(LeaderboardLayer::spawnThemeParticle));
    this->unschedule(schedule_selector(LeaderboardLayer::delaySilenceBg));
    this->unschedule(schedule_selector(LeaderboardLayer::delaySilenceBg2));
    clearParticles();

    if (GameLevelManager::get()->m_levelManagerDelegate == this) {
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
    }

    killCaveMusic();
    CCLayer::onExit();
}

void LeaderboardLayer::onExitTransitionDidStart() {
    CCLayer::onExitTransitionDidStart();
    // si es un push a nivel, pausar la cueva. Si es historial, dejarla sonando.
    if (!m_leavingForGood && !m_goingToHistory && m_musicPlaying && m_levelMusicChannel) {
        // fade-out rapido y pausar
        m_isFadingCaveIn = false;
        m_isFadingCaveOut = false;
        float currentVol = 0.f;
        m_levelMusicChannel->getVolume(&currentVol);
        m_levelMusicChannel->setVolume(0.f);
        m_levelMusicChannel->setPaused(true);
        m_musicPlaying = false;
    }
}

void LeaderboardLayer::onBack(CCObject*) {
    m_leavingForGood = true;
    killCaveMusic();
    fadeInMenuMusic();
    if (GameLevelManager::get()->m_levelManagerDelegate == this) {
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
    }
    CCScene* backScene = nullptr;
    if (m_backTarget == BackTarget::LevelSearchLayer) {
        backScene = LevelSearchLayer::scene(0);
    } else {
        backScene = CreatorLayer::scene();
    }
    TransitionManager::get().replaceScene(backScene);
}

void LeaderboardLayer::keyBackClicked() {
    onBack(nullptr);
}

void LeaderboardLayer::onTab(CCObject* sender) {
    auto toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;
    auto typeObj = typeinfo_cast<CCString*>(toggler->getUserObject());
    if (!typeObj) return;
    auto type = std::string(typeObj->getCString());

    if (m_currentType == type) {
        toggler->toggle(true);
        return;
    }

    if (type == "foryou" && !Mod::get()->getSettingValue<bool>("enable-for-you")) {
        PaimonNotify::create(
            Localization::get().getString("foryou.coming_soon").c_str(),
            NotificationIcon::Info)->show();
        for (auto tab : m_tabs) {
            auto obj = typeinfo_cast<CCString*>(tab->getUserObject());
            tab->toggle(obj && std::string(obj->getCString()) == m_currentType);
        }
        return;
    }

    m_currentType = type;

    for (auto tab : m_tabs) {
        tab->toggle(tab == toggler);
        tab->setEnabled(false);
    }
    m_isLoadingTab = true;

    if (m_historyButton) {
        m_historyButton->setVisible(type != "foryou");
    }

    // limpia la lista vieja
    if (this->getChildByTag(999)) {
        this->removeChildByTag(999);
    }
    m_scroll = nullptr;
    m_listMenu = nullptr;

    // reset flags
    m_dataLoaded = false;
    m_thumbLoaded = false;
    m_listCreated = false;

    // muestra el spinner
    if (m_loadingSpinner) {
        m_loadingSpinner->setVisible(true);
    }

    // limpiar particulas y musica del tab anterior
    clearParticles();
    killCaveMusic();

    if (type == "foryou") {
        loadForYou();
    } else {
        m_forYouActive = false;
        loadLeaderboard(type);
    }
}

void LeaderboardLayer::loadLeaderboard(std::string type) {
    m_featuredLevel = nullptr;
    m_featuredExpiresAt = 0;

    auto requestToken = ++m_requestGeneration;
    m_pendingLevelGeneration = requestToken;
    WeakRef<LeaderboardLayer> self = this;
    HttpClient::get().get("/api/" + type + "/current", [self, type, requestToken](bool success, std::string const& json) {
        auto layer = self.lock();
        if (!layer) return;
        if (layer->m_requestGeneration != requestToken) return;
        if (layer->m_currentType != type) return;
        if (layer->m_forYouActive) return;

        if (success) {
            auto dataRes = matjson::parse(json);
            if (dataRes.isOk()) {
                auto data = dataRes.unwrap();
                if (data["success"].asBool().unwrapOr(false)) {
                    auto levelData = data["data"];
                    int levelID = levelData["levelID"].asInt().unwrapOr(0);
                    layer->m_featuredExpiresAt = (long long)levelData["expiresAt"].asDouble().unwrapOr(0);

                    if (levelID > 0) {
                        auto level = GJGameLevel::create();
                        level->m_levelID = levelID;
                        level->m_levelName = Localization::get().getString("leaderboard.loading");
                        level->m_creatorName = "";

                        auto saved = GameLevelManager::get()->getSavedLevel(levelID);
                        if (saved) {
                            level->m_levelName = saved->m_levelName;
                            level->m_creatorName = saved->m_creatorName;
                            level->m_stars = saved->m_stars;
                            level->m_difficulty = saved->m_difficulty;
                            level->m_demon = saved->m_demon;
                            level->m_demonDifficulty = saved->m_demonDifficulty;
                            level->m_songID = saved->m_songID;
                            level->m_audioTrack = saved->m_audioTrack;
                            level->m_levelString = saved->m_levelString;
                        }

                        layer->m_featuredLevel = level;

                        auto searchObj = GJSearchObject::create(SearchType::MapPackOnClick, std::to_string(levelID));
                        auto glm = GameLevelManager::get();
                        glm->m_levelManagerDelegate = layer.data();
                        glm->getOnlineLevels(searchObj);
                    }
                }
            }
        }

        if (layer->m_requestGeneration != requestToken) return;
        if (layer->m_currentType != type) return;
        if (layer->m_forYouActive) return;

        layer->m_dataLoaded = true;

        if (layer->m_featuredLevel) {
            layer->updateBackground(layer->m_featuredLevel->m_levelID);
        } else {
            layer->updateBackground(0);
            layer->m_thumbLoaded = true;
        }

        if (!layer->m_listCreated) {
            layer->m_listCreated = true;
            layer->createList(type);
        }

        layer->checkLoadingComplete();
    });
}

void LeaderboardLayer::createList(std::string type) {
    static int LIST_CONTAINER_TAG = 999;

    this->removeChildByTag(LIST_CONTAINER_TAG);

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    auto container = CCNode::create();
    container->setTag(LIST_CONTAINER_TAG);
    this->addChild(container);

    if (!m_featuredLevel) {
        auto lbl = CCLabelBMFont::create("No Featured Level", "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        container->addChild(lbl);
        m_thumbLoaded = true;
        checkLoadingComplete();
        return;
    }

    GJGameLevel* level = m_featuredLevel;
    int levelID = level->m_levelID;

    // ── dimensiones de la tarjeta ────────────────────────
    float cardW = 440.f;
    float cardH = 210.f;
    float cardY = winSize.height / 2 - 10.f;

    auto card = CCNode::create();
    card->setContentSize({cardW, cardH});
    card->setAnchorPoint({0.5f, 0.5f});
    card->setPosition({winSize.width / 2, cardY});
    container->addChild(card, 5);

    // animacion de entrada
    card->setScale(0.85f);
    card->runAction(CCEaseBackOut::create(CCScaleTo::create(0.5f, 1.0f)));

    // ── sombra difusa (profundidad) ──────────────────────
    float cardRadius = 16.f;
    auto shadow = paimon::SpriteHelper::createRoundedRect(
        cardW + 14.f, cardH + 16.f, cardRadius + 2.f,
        {0.f, 0.f, 0.f, 0.34f}
    );
    shadow->setPosition({-7.f, -8.f});
    card->addChild(shadow, -2);

    // ── borde exterior redondeado ────────────────────────
    auto border = paimon::SpriteHelper::createRoundedRect(
        cardW, cardH, cardRadius,
        {0.04f, 0.04f, 0.05f, 0.97f},
        {0.20f, 0.20f, 0.22f, 0.92f},
        1.5f
    );
    border->setPosition({0.f, 0.f});
    card->addChild(border, -1);

    // ── fondo de la tarjeta ──────────────────────────────
    auto cardBg = paimon::SpriteHelper::createRoundedRect(
        cardW - 2.f, cardH - 2.f, cardRadius - 1.f,
        {0.06f, 0.06f, 0.07f, 0.98f}
    );
    cardBg->setPosition({1.f, 1.f});
    card->addChild(cardBg, 0);

    // ── badge DAILY / WEEKLY (pildora oscura) ────────────
    bool isDaily = (type == "daily");
    float thumbPad = 10.f;
    float thumbW = 194.f;
    float thumbH = cardH - thumbPad * 2.f;
    float infoX = thumbPad + thumbW + 12.f;
    float infoW = cardW - infoX - thumbPad;
    float infoH = cardH - thumbPad * 2.f;

    auto infoPanel = CCNode::create();
    infoPanel->setContentSize({infoW, infoH});
    infoPanel->setPosition({infoX, thumbPad});
    card->addChild(infoPanel, 3);

    auto infoBg = paimon::SpriteHelper::createRoundedRect(
        infoW, infoH, 13.f,
        {0.09f, 0.09f, 0.10f, 0.96f},
        {0.15f, 0.15f, 0.17f, 0.86f},
        1.1f
    );
    infoBg->setPosition({0.f, 0.f});
    infoPanel->addChild(infoBg, 0);

    auto badgeBg = paimon::SpriteHelper::createRoundedRect(
        82.f, 22.f, 11.f,
        isDaily ? ccColor4F{0.13f, 0.13f, 0.14f, 1.f} : ccColor4F{0.11f, 0.11f, 0.13f, 1.f},
        isDaily ? ccColor4F{0.34f, 0.34f, 0.38f, 0.85f} : ccColor4F{0.28f, 0.28f, 0.32f, 0.85f},
        1.0f
    );
    badgeBg->setPosition({infoW - 94.f, infoH - 30.f});
    infoPanel->addChild(badgeBg, 10);

    auto badgeLbl = CCLabelBMFont::create(isDaily ? "DAILY" : "WEEKLY", "bigFont.fnt");
    badgeLbl->setScale(0.26f);
    badgeLbl->setColor({205, 205, 212});
    badgeLbl->setPosition({41.f, 11.f});
    badgeBg->addChild(badgeLbl);
    badgeLbl->setTag(TAG_BADGE_LABEL);

    // ── thumbnail (lado izquierdo ~50%) ──────────────────
    auto clipper = CCClippingNode::create();
    clipper->setContentSize({thumbW, thumbH});
    clipper->setAnchorPoint({0, 0});
    clipper->setPosition({thumbPad, thumbPad});

    auto stencil = paimon::SpriteHelper::createRoundedRectStencil(thumbW, thumbH, 14.f);
    clipper->setStencil(stencil);
    card->addChild(clipper, 2);

    // borde sutil alrededor del thumbnail (detras del clipper)
    auto thumbBorder = paimon::SpriteHelper::createRoundedRect(
        thumbW + 2.f, thumbH + 2.f, 15.f,
        {0.08f, 0.08f, 0.09f, 1.f},
        {0.18f, 0.18f, 0.20f, 0.88f},
        1.1f
    );
    thumbBorder->setPosition({thumbPad - 1.f, thumbPad - 1.f});
    card->addChild(thumbBorder, 1);

    // placeholder oscuro + spinner
    auto thumbPlaceholder = paimon::SpriteHelper::createRoundedRect(
        thumbW, thumbH, 14.f,
        {0.10f, 0.10f, 0.11f, 1.f}
    );
    thumbPlaceholder->setTag(101);
    clipper->addChild(thumbPlaceholder, 0);

    auto thumbGrad = CCLayerGradient::create({0, 0, 0, 0}, {8, 8, 10, 235}, {1, 0});
    thumbGrad->setContentSize({thumbW * 0.42f, thumbH});
    thumbGrad->setPosition({thumbW - thumbW * 0.42f, 0.f});
    clipper->addChild(thumbGrad, 10);

    // cargar thumbnail
    Ref<LeaderboardLayer> self = this;
    auto createThumbSprite = [clipper](CCTexture2D* tex) {
        if (!tex || !clipper) return;

        // quitar placeholder y spinner
        clipper->removeChildByTag(101);

        auto sprite = CCSprite::createWithTexture(tex);
        if (!sprite) return;
        CCSize cs = clipper->getContentSize();
        float sx = cs.width / sprite->getContentSize().width;
        float sy = cs.height / sprite->getContentSize().height;
        sprite->setScale(std::max(sx, sy));
        sprite->setPosition(cs / 2);
        sprite->setOpacity(0);
        sprite->runAction(CCFadeIn::create(0.4f));
        clipper->addChild(sprite, 1);
    };

    auto localTex = LocalThumbs::get().loadTexture(levelID);
    if (localTex) {
        createThumbSprite(localTex);
        m_thumbLoaded = true;
        checkLoadingComplete();
    } else if (levelID > 0) {
        std::string fileName = fmt::format("{}.png", levelID);
        Ref<CCClippingNode> safeClipper = clipper;

        auto requestToken = m_requestGeneration;
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, safeClipper, createThumbSprite, requestToken](CCTexture2D* tex, bool) {
            geode::Loader::get()->queueInMainThread([self, safeClipper, tex, createThumbSprite, requestToken] {
                if (!self->getParent()) return;
                if (self->m_requestGeneration != requestToken) return;
                if (self->m_forYouActive || self->m_currentType == "foryou") return;
                if (safeClipper->getParent() && tex) {
                    createThumbSprite(tex);
                }
                self->m_thumbLoaded = true;
                self->checkLoadingComplete();
            });
        });
    } else {
        m_thumbLoaded = true;
        checkLoadingComplete();
    }

    // ── menu para click en la tarjeta ────────────────────
    auto cellMenu = CCMenu::create();
    cellMenu->setPosition({0, 0});
    cellMenu->setContentSize({cardW, cardH});
    card->addChild(cellMenu, 50);

    // boton invisible sobre toda la tarjeta
    if (level) {
        auto hitArea = CCSprite::create();
        if (hitArea) {
            hitArea->setTextureRect(CCRect(0, 0, 1, 1));
            hitArea->setScaleX(cardW);
            hitArea->setScaleY(cardH);
            hitArea->setOpacity(0);

            auto playBtn = CCMenuItemSpriteExtra::create(hitArea, self, menu_selector(LeaderboardLayer::onViewLevel));
            playBtn->setUserObject(level);
            playBtn->setPosition({cardW / 2, cardH / 2});
            PaimonButtonHighlighter::registerButton(playBtn);
            cellMenu->addChild(playBtn, 100);
        }
    }

    // ── area derecha (info) ─────────────────────────────
    float textX = 14.f;
    float textMaxW = infoW - 28.f;

    // nombre del nivel
    std::string nameStr = level->m_levelName;
    auto nameLbl = CCLabelBMFont::create(nameStr.c_str(), "bigFont.fnt");
    nameLbl->setScale(0.62f);
    nameLbl->setColor({232, 232, 236});
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setPosition({textX, infoH - 56.f});
    nameLbl->setTag(TAG_NAME_LABEL);
    if (nameLbl->getScaledContentSize().width > textMaxW) {
        nameLbl->setScale(nameLbl->getScale() * (textMaxW / nameLbl->getScaledContentSize().width));
    }
    infoPanel->addChild(nameLbl, 10);

    // creador
    std::string creatorStr = level->m_creatorName.size() > 0
        ? "by " + std::string(level->m_creatorName)
        : "";
    auto creatorLbl = CCLabelBMFont::create(creatorStr.c_str(), "chatFont.fnt");
    creatorLbl->setScale(0.50f);
    creatorLbl->setColor({126, 126, 132});
    creatorLbl->setAnchorPoint({0.f, 0.5f});
    creatorLbl->setPosition({textX, infoH - 80.f});
    creatorLbl->setTag(TAG_CREATOR_LABEL);
    if (creatorLbl->getScaledContentSize().width > textMaxW) {
        creatorLbl->setScale(creatorLbl->getScale() * (textMaxW / creatorLbl->getScaledContentSize().width));
    }
    infoPanel->addChild(creatorLbl, 10);

    // separador oscuro
    auto sep = paimon::SpriteHelper::createRoundedRect(
        textMaxW, 1.5f, 0.75f,
        {0.24f, 0.24f, 0.27f, 0.38f}
    );
    sep->setPosition({textX, infoH - 96.f});
    infoPanel->addChild(sep, 10);

    // cuenta regresiva
    if (m_featuredExpiresAt > 0) {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        long long diff = m_featuredExpiresAt - now;

        if (diff > 0) {
            int hours = (int)(diff / (1000LL * 60 * 60));
            int mins = (int)((diff % (1000LL * 60 * 60)) / (1000LL * 60));

            auto timeBg = paimon::SpriteHelper::createRoundedRect(
                120.f, 20.f, 10.f,
                {0.10f, 0.10f, 0.11f, 1.f},
                {0.18f, 0.18f, 0.20f, 0.82f},
                1.0f
            );
            timeBg->setPosition({textX, infoH - 126.f});
            infoPanel->addChild(timeBg, 10);

            auto timeLbl = CCLabelBMFont::create(
                fmt::format("Ends in {}h {}m", hours, mins).c_str(), "chatFont.fnt");
            timeLbl->setScale(0.43f);
            timeLbl->setColor({156, 156, 162});
            timeLbl->setPosition({60.f, 10.f});
            timeLbl->setTag(TAG_TIME_LABEL);
            timeBg->addChild(timeLbl, 11);
        }
    }

    auto playMenu = CCMenu::create();
    playMenu->setPosition({0, 0});
    infoPanel->addChild(playMenu, 15);

    if (level) {
        float btnW = textMaxW;
        float btnH = 36.f;

        auto playNode = CCNode::create();
        playNode->setContentSize({btnW, btnH});

        auto playShadow = paimon::SpriteHelper::createRoundedRect(
            btnW + 4.f, btnH + 6.f, 12.f,
            {0.f, 0.f, 0.f, 0.18f}
        );
        playShadow->setPosition({-2.f, -3.f});
        playNode->addChild(playShadow, -2);

        auto playBg = paimon::SpriteHelper::createRoundedRect(
            btnW, btnH, 11.f,
            {0.12f, 0.12f, 0.13f, 1.f},
            {0.28f, 0.28f, 0.31f, 0.92f},
            1.2f
        );
        playBg->setPosition({0.f, 0.f});
        playNode->addChild(playBg, 0);

        auto playStrip = paimon::SpriteHelper::createRoundedRect(
            btnW - 18.f, 2.f, 1.f,
            {0.62f, 0.62f, 0.66f, 0.18f}
        );
        playStrip->setPosition({9.f, btnH - 6.f});
        playNode->addChild(playStrip, 1);

        auto playText = CCLabelBMFont::create("PLAY", "bigFont.fnt");
        playText->setScale(0.56f);
        playText->setColor({216, 216, 222});
        playText->setPosition({btnW / 2, btnH / 2});
        playNode->addChild(playText, 2);

        auto playBtnVis = CCMenuItemSpriteExtra::create(playNode, self, menu_selector(LeaderboardLayer::onViewLevel));
        playBtnVis->setUserObject(level);
        playBtnVis->setPosition({textX + textMaxW / 2, 24.f});
        playBtnVis->m_scaleMultiplier = 1.02f;
        PaimonButtonHighlighter::registerButton(playBtnVis);
        playMenu->addChild(playBtnVis);
    }
}

void LeaderboardLayer::loadForYou() {
    m_forYouActive = true;
    ++m_requestGeneration;
    m_pendingLevelGeneration = m_requestGeneration;
    m_forYouResults.clear();
    m_forYouQueryQueue.clear();
    m_forYouQueryIndex = 0;
    m_featuredLevel = nullptr;

    auto& tracker = paimon::foryou::ForYouTracker::get();

    int minLevels = Mod::get()->getSettingValue<int64_t>("for-you-min-levels");
    if (!tracker.hasEnoughData(minLevels)) {
        static int LIST_CONTAINER_TAG = 999;
        this->removeChildByTag(LIST_CONTAINER_TAG);

        m_dataLoaded = true;
        m_thumbLoaded = true;
        if (m_loadingSpinner) m_loadingSpinner->setVisible(false);

        Ref<LeaderboardLayer> self = this;
        auto popup = paimon::foryou::ForYouPreferencesPopup::create([self]() {
            if (self->getParent()) {
                self->loadForYou();
            }
        });
        if (popup) popup->show();
        if (m_isLoadingTab) {
            m_isLoadingTab = false;
            for (auto tab : m_tabs) {
                tab->setEnabled(true);
            }
        }
        return;
    }

    // One-time Level-Tags download prompt (opens native Geode mod popup)
    if (!paimon::compat::ModCompat::isLevelTagsLoaded() &&
        !Mod::get()->getSavedValue<bool>("foryou-tags-prompt-shown", false)) {
        Mod::get()->setSavedValue("foryou-tags-prompt-shown", true);
        geode::openInfoPopup("kampwski.level_tags");
    }

    // Generate queries
    m_forYouQueryQueue = paimon::foryou::ForYouEngine::get().generateQueries(3);
    if (m_forYouQueryQueue.empty()) {
        m_dataLoaded = true;
        m_thumbLoaded = true;
        if (m_loadingSpinner) m_loadingSpinner->setVisible(false);
        return;
    }

    // Fire first query
    GameLevelManager::get()->m_levelManagerDelegate = this;
    fireNextForYouQuery();
}

void LeaderboardLayer::fireNextForYouQuery() {
    if (!m_forYouActive || m_currentType != "foryou") return;
    if (m_forYouQueryIndex >= static_cast<int>(m_forYouQueryQueue.size())) return;

    auto& query = m_forYouQueryQueue[m_forYouQueryIndex];

    GameLevelManager::get()->m_levelManagerDelegate = this;
    if (query.searchObj) {
        GameLevelManager::get()->getOnlineLevels(query.searchObj);
    }
}

void LeaderboardLayer::createForYouList() {
    static int LIST_CONTAINER_TAG = 999;

    this->removeChildByTag(LIST_CONTAINER_TAG);

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    auto container = CCNode::create();
    container->setTag(LIST_CONTAINER_TAG);
    this->addChild(container);

    auto topMenu = CCMenu::create();
    topMenu->setPosition({0, 0});
    container->addChild(topMenu, 20);

    auto refreshSpr = CCSprite::createWithSpriteFrameName("GJ_updateBtn_001.png");
    if (refreshSpr) {
        refreshSpr->setScale(0.7f);
        auto refreshBtn = CCMenuItemSpriteExtra::create(refreshSpr, this,
            menu_selector(LeaderboardLayer::onForYouRefresh));
        refreshBtn->setPosition({winSize.width - 28.f, winSize.height - 28.f});
        refreshBtn->m_scaleMultiplier = 1.02f;
        PaimonButtonHighlighter::registerButton(refreshBtn);
        topMenu->addChild(refreshBtn);
    }

    auto prefsSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    if (prefsSpr) {
        prefsSpr->setScale(0.45f);
        auto prefsBtn = CCMenuItemSpriteExtra::create(prefsSpr, this,
            menu_selector(LeaderboardLayer::onForYouPreferences));
        prefsBtn->setPosition({winSize.width - 60.f, winSize.height - 28.f});
        prefsBtn->m_scaleMultiplier = 1.02f;
        PaimonButtonHighlighter::registerButton(prefsBtn);
        topMenu->addChild(prefsBtn);
    }

    if (m_forYouResults.empty()) {
        auto lbl = CCLabelBMFont::create(
            Localization::get().getString("foryou.no_results").c_str(), "chatFont.fnt");
        lbl->setScale(0.6f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        container->addChild(lbl);
        return;
    }

    // Scrollable list of level cards
    float scrollH = winSize.height - 60.f;
    float scrollW = winSize.width;
    auto scroll = ScrollLayer::create({scrollW, scrollH});
    scroll->setPosition({0, 10.f});
    container->addChild(scroll);
    m_scroll = scroll;

    float cardW = 400.f;
    float cardH = 110.f;
    float cardSpacing = 8.f;
    float totalH = m_forYouResults.size() * (cardH + cardSpacing);
    if (totalH < scrollH) totalH = scrollH;

    auto content = scroll->m_contentLayer;
    content->setContentSize({scrollW, totalH});

    Ref<LeaderboardLayer> self = this;
    bool useTags = paimon::compat::ModCompat::isLevelTagsLoaded() &&
                   Mod::get()->getSettingValue<bool>("for-you-use-tags");

    for (size_t i = 0; i < m_forYouResults.size(); i++) {
        GJGameLevel* level = m_forYouResults[i];
        int levelID = level->m_levelID;

        float cardY = totalH - (i + 1) * (cardH + cardSpacing);

        auto card = CCNode::create();
        card->setContentSize({cardW, cardH});
        card->setAnchorPoint({0.5f, 0.f});
        card->setPosition({scrollW / 2, cardY});
        content->addChild(card, 5);

        card->setScale(0.9f);
        card->runAction(CCSequence::create(
            CCDelayTime::create(i * 0.08f),
            CCEaseBackOut::create(CCScaleTo::create(0.3f, 1.0f)),
            nullptr
        ));

        auto cardBg = paimon::SpriteHelper::createColorPanel(cardW, cardH, {14, 14, 22}, 220);
        cardBg->setPosition({0, 0});
        card->addChild(cardBg, 0);

        auto border = paimon::SpriteHelper::createColorPanel(cardW + 2, cardH + 2, {50, 50, 70}, 80);
        border->setPosition({-1, -1});
        card->addChild(border, -1);

        float thumbW = cardW * 0.38f;
        float thumbH = cardH;
        float thumbPad = 3.f;

        auto clipper = CCClippingNode::create();
        clipper->setContentSize({thumbW - thumbPad, thumbH - thumbPad * 2});
        clipper->setAnchorPoint({0, 0});
        clipper->setPosition({thumbPad, thumbPad});

        auto stencil = PaimonDrawNode::create();
        {
            auto cs = clipper->getContentSize();
            CCPoint rect[4] = { ccp(0,0), ccp(cs.width,0), ccp(cs.width,cs.height), ccp(0,cs.height) };
            ccColor4F white = {1,1,1,1};
            stencil->drawPolygon(rect, 4, white, 0, white);
        }
        clipper->setStencil(stencil);
        card->addChild(clipper, 2);

        auto thumbPlaceholder = CCLayerColor::create({20, 18, 28, 255});
        thumbPlaceholder->setContentSize(clipper->getContentSize());
        thumbPlaceholder->setTag(101);
        clipper->addChild(thumbPlaceholder, 0);

        auto thumbGrad = CCLayerGradient::create({0, 0, 0, 0}, {14, 14, 22, 220}, {1, 0});
        thumbGrad->setContentSize({thumbW * 0.3f, thumbH - thumbPad * 2});
        thumbGrad->setPosition({thumbW - thumbPad - thumbW * 0.3f, 0});
        clipper->addChild(thumbGrad, 10);

        auto createThumbSprite = [clipper](CCTexture2D* tex) {
            if (!tex || !clipper) return;
            clipper->removeChildByTag(101);

            auto sprite = CCSprite::createWithTexture(tex);
            if (!sprite) return;
            CCSize cs = clipper->getContentSize();
            float sx = cs.width / sprite->getContentSize().width;
            float sy = cs.height / sprite->getContentSize().height;
            sprite->setScale(std::max(sx, sy));
            sprite->setPosition(cs / 2);
            sprite->setOpacity(0);
            sprite->runAction(CCFadeIn::create(0.4f));
            clipper->addChild(sprite, 1);
        };

        auto localTex = LocalThumbs::get().loadTexture(levelID);
        if (localTex) {
            createThumbSprite(localTex);
        } else if (levelID > 0) {
            std::string fileName = fmt::format("{}.png", levelID);
            Ref<CCClippingNode> safeClipper = clipper;
            ThumbnailLoader::get().requestLoad(levelID, fileName, [self, safeClipper, createThumbSprite](CCTexture2D* tex, bool) {
                geode::Loader::get()->queueInMainThread([self, safeClipper, tex, createThumbSprite] {
                    if (!self->getParent()) return;
                    if (safeClipper->getParent() && tex) createThumbSprite(tex);
                });
            });
        }

        auto cellMenu = CCMenu::create();
        cellMenu->setPosition({0, 0});
        cellMenu->setContentSize({cardW, cardH});
        card->addChild(cellMenu, 50);

        auto hitArea = CCSprite::create();
        if (hitArea) {
            hitArea->setTextureRect(CCRect(0, 0, 1, 1));
            hitArea->setScaleX(cardW);
            hitArea->setScaleY(cardH);
            hitArea->setOpacity(0);

            auto playBtn = CCMenuItemSpriteExtra::create(hitArea, self, menu_selector(LeaderboardLayer::onForYouPlayLevel));
            playBtn->setUserObject(level);
            playBtn->setPosition({cardW / 2, cardH / 2});
            PaimonButtonHighlighter::registerButton(playBtn);
            cellMenu->addChild(playBtn, 100);
        }

        float textX = thumbW + 10.f;
        float textMaxW = cardW - textX - 10.f;

        auto nameLbl = CCLabelBMFont::create(level->m_levelName.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.45f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({textX, cardH - 22.f});
        if (nameLbl->getScaledContentSize().width > textMaxW) {
            nameLbl->setScale(nameLbl->getScale() * (textMaxW / nameLbl->getScaledContentSize().width));
        }
        card->addChild(nameLbl, 10);

        std::string creatorStr = level->m_creatorName.size() > 0
            ? "by " + std::string(level->m_creatorName)
            : "";
        auto creatorLbl = CCLabelBMFont::create(creatorStr.c_str(), "chatFont.fnt");
        creatorLbl->setScale(0.5f);
        creatorLbl->setColor({120, 200, 255});
        creatorLbl->setAnchorPoint({0.f, 0.5f});
        creatorLbl->setPosition({textX, cardH - 40.f});
        card->addChild(creatorLbl, 10);

        if (useTags) {
            auto tags = paimon::foryou::LevelTagsIntegration::get().getCachedTags(levelID);
            float tagX = textX;
            float tagY = 15.f;
            int maxTags = 3;
            for (int t = 0; t < std::min<int>(maxTags, static_cast<int>(tags.size())); t++) {
                auto tagBg = paimon::SpriteHelper::createColorPanel(
                    static_cast<float>(tags[t].size()) * 5.f + 12.f, 16.f,
                    {80, 60, 140}, 180);
                tagBg->setPosition({tagX, tagY});
                card->addChild(tagBg, 10);

                auto tagLbl = CCLabelBMFont::create(tags[t].c_str(), "chatFont.fnt");
                tagLbl->setScale(0.35f);
                tagLbl->setPosition({tagBg->getContentSize().width / 2, tagBg->getContentSize().height / 2});
                tagBg->addChild(tagLbl);

                tagX += tagBg->getContentSize().width + 4.f;
            }
        }

        auto playMenu = CCMenu::create();
        playMenu->setPosition({0, 0});
        card->addChild(playMenu, 15);

        auto playSpr = ButtonSprite::create("Play", 60, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.7f);
        playSpr->setScale(0.7f);
        auto playBtnVis = CCMenuItemSpriteExtra::create(playSpr, self, menu_selector(LeaderboardLayer::onForYouPlayLevel));
        playBtnVis->setUserObject(level);
        playBtnVis->setPosition({textX + textMaxW - 30.f, cardH / 2});
        playBtnVis->m_scaleMultiplier = 1.02f;
        PaimonButtonHighlighter::registerButton(playBtnVis);
        playMenu->addChild(playBtnVis);
    }

    if (useTags) {
        std::vector<int> ids;
        for (auto& lvl : m_forYouResults) {
            ids.push_back(lvl->m_levelID);
        }
        paimon::foryou::LevelTagsIntegration::get().fetchTagsForLevels(ids, [](auto) {
        });
    }

    auto const& profile = paimon::foryou::ForYouTracker::get().getProfile();
    if (!profile.favoriteLevels.empty()) {
        auto favTitle = CCLabelBMFont::create(
            Localization::get().getString("foryou.fav_levels_title").c_str(), "bigFont.fnt");
        favTitle->setScale(0.35f);
        favTitle->setColor({255, 200, 80});
        favTitle->setPosition({winSize.width / 2, 32.f});
        container->addChild(favTitle, 20);

        float favStartX = winSize.width / 2 - 90.f;
        float favY = 14.f;
        int count = 0;
        auto const& levels = paimon::foryou::ForYouTracker::get().getLevels();
        for (int lid : profile.favoriteLevels) {
            if (count >= 5) break;
            auto it = levels.find(lid);
            if (it == levels.end()) continue;

            auto savedLevel = GameLevelManager::get()->getSavedLevel(lid);
            std::string name = savedLevel ? std::string(savedLevel->m_levelName) :
                               fmt::format("Level {}", lid);

            auto favLbl = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
            favLbl->setScale(0.4f);
            favLbl->setAnchorPoint({0.f, 0.5f});
            favLbl->setPosition({favStartX + (count % 3) * 70.f, favY - (count / 3) * 14.f});
            favLbl->setColor({255, 220, 120});
            if (favLbl->getScaledContentSize().width > 65.f) {
                favLbl->setScale(favLbl->getScale() * (65.f / favLbl->getScaledContentSize().width));
            }
            container->addChild(favLbl, 20);
            count++;
        }
    }
}

void LeaderboardLayer::onForYouRefresh(CCObject*) {
    loadForYou();
}

void LeaderboardLayer::onForYouPreferences(CCObject*) {
    Ref<LeaderboardLayer> self = this;
    auto popup = paimon::foryou::ForYouPreferencesPopup::create([self]() {
        if (self->getParent()) {
            self->loadForYou();
        }
    });
    if (popup) popup->show();
}

void LeaderboardLayer::onForYouPlayLevel(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto level = typeinfo_cast<GJGameLevel*>(btn->getUserObject());
    if (!level) return;

    auto savedLevel = GameLevelManager::get()->getSavedLevel(level->m_levelID);
    GJGameLevel* levelToUse = savedLevel ? savedLevel : level;

    auto layer = LevelInfoLayer::create(levelToUse, false);
    auto infoScene = CCScene::create();
    infoScene->addChild(layer);
    TransitionManager::get().pushScene(infoScene);
}

void LeaderboardLayer::onViewLevel(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto level = typeinfo_cast<GJGameLevel*>(btn->getUserObject());
    if (level) {
        // nivel + musica desde cache
        auto savedLevel = GameLevelManager::get()->getSavedLevel(level->m_levelID);
        GJGameLevel* levelToUse = level;

        if (savedLevel) {
            // nivel guardado con musica
            levelToUse = savedLevel;
        } else {
            // no nivel guardado -> copiar info que tengamos
            // levelinfolayer descarga resto
        }

        // la musica cueva se pausara automaticamente en onExitTransitionDidStart
        auto layer = LevelInfoLayer::create(levelToUse, false);
        auto infoScene = CCScene::create();
        infoScene->addChild(layer);
        TransitionManager::get().pushScene(infoScene);
    }
}

float LeaderboardLayer::getAudioBassLevel() {
    if (!m_fftDSP || !m_musicPlaying) return 0.f;

    FMOD_DSP_PARAMETER_FFT* fftData = nullptr;
    FMOD_RESULT result = m_fftDSP->getParameterData(
        FMOD_DSP_FFT_SPECTRUMDATA, (void**)&fftData, nullptr, nullptr, 0);
    
    if (result != FMOD_OK || !fftData || fftData->numchannels < 1) return 0.f;

    // promediar bins de bajos (0-8 de 256 bins = ~0-350 Hz)
    float bassSum = 0.f;
    int bassBins = std::min(8, fftData->length);
    for (int i = 0; i < bassBins; i++) {
        bassSum += fftData->spectrum[0][i];
    }
    float bassAvg = (bassBins > 0) ? bassSum / bassBins : 0.f;

    // promediar mids (8-32 = ~350-1400 Hz)
    float midSum = 0.f;
    int midStart = std::min(8, fftData->length);
    int midEnd = std::min(32, fftData->length);
    for (int i = midStart; i < midEnd; i++) {
        midSum += fftData->spectrum[0][i];
    }
    float midAvg = (midEnd > midStart) ? midSum / (midEnd - midStart) : 0.f;

    // combinar: bajos pesan mas para el beat
    return bassAvg * 0.7f + midAvg * 0.3f;
}

void LeaderboardLayer::updateAudioReactive(float dt) {
    m_audioReactTime += dt;

    float rawBass = getAudioBassLevel();
    
    // normalizar (las FFT values son tipicamente 0-0.1)
    float normalizedBass = std::min(1.f, rawBass * 12.f);
    
    // deteccion de beat: si hay un salto repentino de energia
    float delta = normalizedBass - m_prevBassLevel;
    m_prevBassLevel = normalizedBass;
    
    float beatThreshold = 0.15f;
    if (delta > beatThreshold) {
        // beat detectado — pulso fuerte
        m_beatPulse = std::min(1.f, m_beatPulse + delta * 2.5f);
    }
    
    // decay suave del beat
    m_beatPulse = std::max(0.f, m_beatPulse - dt * 3.5f);
    
    // glow suave sigue la energia general
    float targetGlow = normalizedBass * 0.6f;
    m_glowPulse += (targetGlow - m_glowPulse) * std::min(1.f, dt * 8.f);
    
    // bg brightness pulse
    m_bgPulse += (normalizedBass * 0.4f - m_bgPulse) * std::min(1.f, dt * 6.f);
    
    // particle boost en beats
    m_particleBoost = std::max(0.f, m_particleBoost - dt * 2.f);
    if (delta > beatThreshold * 1.2f) {
        m_particleBoost = std::min(1.f, m_particleBoost + 0.5f);
    }
    
    // ── aplicar efectos visuales ──
    
    // 1. glow overlay — pulsa con color tematico
    if (m_glowOverlay) {
        // mezclar color tematico
        float t = (std::sin(m_audioReactTime * 0.5f) + 1.f) * 0.5f;
        GLubyte r = (GLubyte)(m_themeColorA.r + (m_themeColorB.r - m_themeColorA.r) * t);
        GLubyte g = (GLubyte)(m_themeColorA.g + (m_themeColorB.g - m_themeColorA.g) * t);
        GLubyte b = (GLubyte)(m_themeColorA.b + (m_themeColorB.b - m_themeColorA.b) * t);
        m_glowOverlay->setColor({r, g, b});
        
        // opacidad basada en glow + beat
        float glowAlpha = m_glowPulse * 18.f + m_beatPulse * 25.f;
        m_glowOverlay->setOpacity((GLubyte)std::min(45.f, glowAlpha));
    }
    
    // 2. beat flash — destello blanco en beats fuertes
    if (m_beatFlash) {
        float flashAlpha = m_beatPulse * 35.f;
        m_beatFlash->setOpacity((GLubyte)std::min(30.f, flashAlpha));
    }
    
    // 3. bg sprite brightness pulses
    if (m_bgSprite) {
        if (auto paimonSprite = typeinfo_cast<LeaderboardPaimonSprite*>(m_bgSprite)) {
            float baseBrightness = 1.0f + m_bgPulse * 0.3f + m_beatPulse * 0.15f;
            paimonSprite->m_brightness = baseBrightness;
        }
    }
    
    // 4. bg overlay pulsa (oscurece menos en beats)
    if (m_bgOverlay) {
        float baseOverlay = 100.f;
        float overlayReduction = m_beatPulse * 30.f + m_glowPulse * 15.f;
        m_bgOverlay->setOpacity((GLubyte)std::max(40.f, baseOverlay - overlayReduction));
    }
    
    // 5. particulas extra en beats
    if (m_particleBoost > 0.3f && m_particleContainer) {
        spawnThemeParticle(0.f);
    }
}

void LeaderboardLayer::update(float dt) {
    m_blurTime += dt;
    
    // forzar BG del menu silenciado CADA FRAME — ningun thread puede restaurarlo
    if (!m_leavingForGood) {
        ensureBgSilenced();
    }
    
    if (m_bgSprite) {
        if (auto paimonSprite = typeinfo_cast<LeaderboardPaimonSprite*>(m_bgSprite)) {
             float intensity = 0.75f + std::sin(m_blurTime * 1.0f) * 0.75f;
             paimonSprite->m_intensity = intensity;
        }
    }
    
    // efectos reactivos a la musica
    if (m_musicPlaying && m_levelMusicChannel) {
        updateAudioReactive(dt);
    } else {
        // sin musica: decay los efectos
        if (m_glowOverlay) m_glowOverlay->setOpacity(0);
        if (m_beatFlash) m_beatFlash->setOpacity(0);
        m_beatPulse = 0.f;
        m_glowPulse = 0.f;
        m_bgPulse = 0.f;
        m_particleBoost = 0.f;
    }
}

void LeaderboardLayer::applyBackground(CCTexture2D* texture) {
    if (!texture) return;

    log::info("[LeaderboardLayer] Applying background texture: {}", texture);

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    // nuevo sprite blur
    // blur +40%
    auto newSprite = createLeaderboardBlurredSprite(texture, winSize, 0.095f);
    
    if (newSprite) {
        newSprite->setPosition(winSize / 2);
        newSprite->setZOrder(-5); // = m_bgSprite
        newSprite->setOpacity(0);
        
        // shader atmosfera — prefer .glsl-based from BlurSystem
        auto shader = BlurSystem::getInstance()->getRealtimeBlurShader();
        if (!shader) shader = getOrCreateShader("paimon_atmosphere", vertexShaderCell, fragmentShaderAtmosphere);
        if (shader) {
            newSprite->setShaderProgram(shader);
            newSprite->m_intensity = 0.0f; // comenzar en 0
            newSprite->m_texSize = newSprite->getTexture()->getContentSizeInPixels();
        }
        
        this->addChild(newSprite);
        
        // transicion
        float duration = 0.5f;
        newSprite->runAction(CCFadeIn::create(duration));
        
        // anim atmosfera
        auto breathe = CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(6.0f, 1.05f),
            CCScaleTo::create(6.0f, 1.0f),
            nullptr
        ));
        newSprite->runAction(breathe);
        
        // fade sprite viejo
        if (m_bgSprite) {
            m_bgSprite->stopAllActions();
            m_bgSprite->runAction(CCSequence::create(
                CCFadeOut::create(duration),
                CCCallFunc::create(m_bgSprite, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
        }
        
        m_bgSprite = newSprite;
    }
    
    // fade overlay si hace falta
    if (m_bgOverlay) {
        m_bgOverlay->stopAllActions();
        m_bgOverlay->runAction(CCFadeTo::create(0.5f, 100)); // negro semi
    }
}

void LeaderboardLayer::updateBackground(int levelID) {
    log::info("[LeaderboardLayer] Updating background for levelID: {}", levelID);
    int requestToken = static_cast<int>(m_requestGeneration);

    if (levelID <= 0) {
        // fade a default
        if (m_bgSprite) {
            m_bgSprite->stopAllActions();
            m_bgSprite->runAction(CCSequence::create(
                CCFadeOut::create(0.5f),
                CCCallFunc::create(m_bgSprite, callfunc_selector(CCNode::removeFromParent)),
                nullptr
            ));
            m_bgSprite = nullptr;
        }
        if (m_bgOverlay) {
            m_bgOverlay->stopAllActions();
            m_bgOverlay->runAction(CCFadeTo::create(0.5f, 0));
        }
        return;
    }

    auto texture = LocalThumbs::get().loadTexture(levelID);
    if (texture) {
        if (static_cast<int>(m_requestGeneration) != requestToken) return;
        applyBackground(texture);
    } else {
        // solicita descarga
        std::string fileName = fmt::format("{}.png", levelID);
        Ref<LeaderboardLayer> self = this;
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, requestToken](CCTexture2D* tex, bool) {
            if (!(self->getParent() || self->isRunning())) return;
            if (static_cast<int>(self->m_requestGeneration) != requestToken) return;
            if (self->m_forYouActive || self->m_currentType == "foryou") return;
            if (tex) {
                self->applyBackground(tex);
            }
        });
    }
}

void LeaderboardLayer::loadLevelsFinished(CCArray* levels, char const* key) {
    if (!levels) return;

    // ForYou mode: collect results and fire next query
    if (m_forYouActive) {
        if (m_currentType != "foryou") return;
        if (m_pendingLevelGeneration != m_requestGeneration) return;
        for (auto* level : CCArrayExt<GJGameLevel*>(levels)) {
            if (!level) continue;
            // Skip already-tracked levels
            if (!paimon::foryou::ForYouTracker::get().isLevelTracked(level->m_levelID)) {
                m_forYouResults.push_back(level);
            }
        }
        // Fire next query or finalize
        m_forYouQueryIndex++;
        if (m_forYouQueryIndex < static_cast<int>(m_forYouQueryQueue.size())) {
            fireNextForYouQuery();
        } else {
            // All queries done — score, sort, and display
            paimon::foryou::ForYouEngine::get().scoreAndSortResults(m_forYouResults);
            // Limit to 5 results
            if (m_forYouResults.size() > 5) {
                m_forYouResults.resize(5);
            }
            m_dataLoaded = true;
            m_thumbLoaded = true; // ForYou cards load thumbs individually
            createForYouList();
            checkLoadingComplete();
        }
        return;
    }

    if (m_currentType != "daily" && m_currentType != "weekly") return;
    if (m_pendingLevelGeneration != m_requestGeneration) return;

    // Normal daily/weekly mode
    for (auto* downloadedLevel : CCArrayExt<GJGameLevel*>(levels)) {
        if (!downloadedLevel) continue;
        
        if (m_featuredLevel && m_featuredLevel->m_levelID == downloadedLevel->m_levelID) {
            m_featuredLevel->m_levelName = downloadedLevel->m_levelName;
            m_featuredLevel->m_creatorName = downloadedLevel->m_creatorName;
            m_featuredLevel->m_stars = downloadedLevel->m_stars;
            m_featuredLevel->m_difficulty = downloadedLevel->m_difficulty;
            m_featuredLevel->m_demon = downloadedLevel->m_demon;
            m_featuredLevel->m_demonDifficulty = downloadedLevel->m_demonDifficulty;
            m_featuredLevel->m_userID = downloadedLevel->m_userID;
            m_featuredLevel->m_accountID = downloadedLevel->m_accountID;
            m_featuredLevel->m_levelString = downloadedLevel->m_levelString;
            m_featuredLevel->m_songID = downloadedLevel->m_songID;
            m_featuredLevel->m_audioTrack = downloadedLevel->m_audioTrack;
            m_featuredLevel->m_songIDs = downloadedLevel->m_songIDs;
            m_featuredLevel->m_sfxIDs = downloadedLevel->m_sfxIDs;
        }
    }
    
    // solo actualizar labels, NO recrear la lista (fix doble animacion)
    updateLevelInfo();
}

void LeaderboardLayer::loadLevelsFailed(char const* key) {
    if (m_forYouActive) {
        if (m_currentType != "foryou") return;
        if (m_pendingLevelGeneration != m_requestGeneration) return;
        // Move to next query even on failure
        m_forYouQueryIndex++;
        if (m_forYouQueryIndex < static_cast<int>(m_forYouQueryQueue.size())) {
            fireNextForYouQuery();
        } else {
            paimon::foryou::ForYouEngine::get().scoreAndSortResults(m_forYouResults);
            if (m_forYouResults.size() > 5) m_forYouResults.resize(5);
            m_dataLoaded = true;
            m_thumbLoaded = true;
            createForYouList();
            checkLoadingComplete();
        }
        return;
    }

    if (m_currentType != "daily" && m_currentType != "weekly") return;
    if (m_pendingLevelGeneration != m_requestGeneration) return;
}

void LeaderboardLayer::setupPageInfo(gd::string, char const*) {
    // no necesario
}

// ── actualizar labels sin recrear la lista ──────────────
void LeaderboardLayer::updateLevelInfo() {
    if (!m_featuredLevel) return;

    auto container = this->getChildByTag(999);
    if (!container) return;

    // buscar recursivamente los labels por tag
    auto findByTag = [&](auto const& self, CCNode* parent, int tag) -> CCNode* {
        if (!parent) return nullptr;
        auto children = parent->getChildren();
        if (!children) return nullptr;
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!child) continue;
            if (child->getTag() == tag) return child;
            auto found = self(self, child, tag);
            if (found) return found;
        }
        return nullptr;
    };

    // actualizar nombre
    if (auto nameLbl = typeinfo_cast<CCLabelBMFont*>(findByTag(findByTag, container, TAG_NAME_LABEL))) {
        nameLbl->setScale(0.62f);
        nameLbl->setString(m_featuredLevel->m_levelName.c_str());
        float maxNameW = 188.f;
        if (nameLbl->getScaledContentSize().width > maxNameW) {
            nameLbl->setScale(nameLbl->getScale() * (maxNameW / nameLbl->getScaledContentSize().width));
        }
    }

    // actualizar creador
    if (auto creatorLbl = typeinfo_cast<CCLabelBMFont*>(findByTag(findByTag, container, TAG_CREATOR_LABEL))) {
        std::string creatorStr = m_featuredLevel->m_creatorName.size() > 0
            ? "by " + std::string(m_featuredLevel->m_creatorName) 
            : "";
        creatorLbl->setScale(0.50f);
        creatorLbl->setString(creatorStr.c_str());
        float maxCreatorW = 188.f;
        if (creatorLbl->getScaledContentSize().width > maxCreatorW) {
            creatorLbl->setScale(creatorLbl->getScale() * (maxCreatorW / creatorLbl->getScaledContentSize().width));
        }
    }
}

// ── chequear si toda la carga termino ───────────────────
void LeaderboardLayer::checkLoadingComplete() {
    if (m_dataLoaded && m_thumbLoaded) {
        if (m_loadingSpinner) {
            m_loadingSpinner->setVisible(false);
        }

        if (m_isLoadingTab) {
            m_isLoadingTab = false;
            for (auto tab : m_tabs) {
                tab->setEnabled(true);
            }
        }

        // iniciar musica y particulas tematicas
        if (m_featuredLevel) {
            startCaveMusic();
            // obtener colores del nivel
            auto colors = LevelColors::get().getPair(m_featuredLevel->m_levelID);
            if (colors.has_value()) {
                m_themeColorA = colors->a;
                m_themeColorB = colors->b;
            } else {
                // colores default segun tipo
                if (m_currentType == "daily") {
                    m_themeColorA = {255, 200, 50};
                    m_themeColorB = {255, 130, 30};
                } else {
                    m_themeColorA = {130, 100, 255};
                    m_themeColorB = {80, 60, 200};
                }
            }
            createThemeParticles();
        } else if (m_forYouActive && !m_forYouResults.empty()) {
            // ForYou tab theme colors
            m_themeColorA = {255, 100, 120};
            m_themeColorB = {200, 60, 100};
            createThemeParticles();
        }
    }
}

// ── particulas tematicas ────────────────────────────────
void LeaderboardLayer::clearParticles() {
    this->unschedule(schedule_selector(LeaderboardLayer::spawnThemeParticle));
    if (m_particleContainer) {
        m_particleContainer->removeAllChildren();
    }
}

void LeaderboardLayer::createThemeParticles() {
    clearParticles();
    
    // spawnear particulas periodicamente
    this->schedule(schedule_selector(LeaderboardLayer::spawnThemeParticle), 0.4f);
    
    // algunas particulas iniciales
    for (int i = 0; i < 8; i++) {
        spawnThemeParticle(0.f);
    }
}

void LeaderboardLayer::spawnThemeParticle(float dt) {
    if (!m_particleContainer) return;
    
    // limitar cantidad
    if (m_particleContainer->getChildrenCount() > 25) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // elegir color aleatorio entre los dos tematicos
    float t = (rand() % 100) / 100.f;
    ccColor3B color = {
        (GLubyte)(m_themeColorA.r + (m_themeColorB.r - m_themeColorA.r) * t),
        (GLubyte)(m_themeColorA.g + (m_themeColorB.g - m_themeColorA.g) * t),
        (GLubyte)(m_themeColorA.b + (m_themeColorB.b - m_themeColorA.b) * t),
    };

    // estrella/destello usando sprites GD
    char const* spriteNames[] = {
        "GJ_starsIcon_001.png",
        "GJ_bigStar_001.png",
        "particle_01_001.png",
    };
    int idx = rand() % 3;
    
    CCSprite* particle = nullptr;
    particle = paimon::SpriteHelper::safeCreateWithFrameName(spriteNames[idx]);
    if (!particle) {
        particle = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
    }
    if (!particle) return;

    particle->setColor(color);
    
    // tamano aleatorio
    float baseScale = 0.08f + (rand() % 15) / 100.f;
    particle->setScale(baseScale);
    
    // posicion aleatoria en X, abajo de la pantalla
    float startX = (rand() % (int)winSize.width);
    float startY = -10.f;
    particle->setPosition({startX, startY});
    particle->setOpacity(0);

    m_particleContainer->addChild(particle);

    // movimiento: sube suavemente con drift horizontal
    float duration = 6.f + (rand() % 40) / 10.f;
    float driftX = ((rand() % 100) - 50) * 0.8f;
    float endY = winSize.height + 20.f;

    // fade in -> hold -> fade out
    float fadeIn = 0.8f;
    float fadeOut = 1.5f;
    float holdOpacity = 80 + rand() % 100;

    particle->runAction(CCSpawn::create(
        CCMoveBy::create(duration, {driftX, endY + 10.f}),
        CCSequence::create(
            CCFadeTo::create(fadeIn, (GLubyte)holdOpacity),
            CCDelayTime::create(duration - fadeIn - fadeOut),
            CCFadeTo::create(fadeOut, 0),
            CCCallFunc::create(particle, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ),
        CCRotateBy::create(duration, (rand() % 2 == 0 ? 1.f : -1.f) * (30.f + rand() % 60)),
        nullptr
    ));
}

// ── historial ───────────────────────────────────────────
void LeaderboardLayer::onHistory(CCObject*) {
    // la musica cueva NO se pausa al ir al historial — sigue sonando
    m_goingToHistory = true;
    auto scene = LeaderboardHistoryLayer::scene();
    TransitionManager::get().pushScene(scene);
}

// ═══════════════════════════════════════════════════════════
//  SISTEMA DE AUDIO ROBUSTO — musica cueva + control de menu
// ═══════════════════════════════════════════════════════════

void LeaderboardLayer::startCaveMusic() {
    if (!m_featuredLevel) return;
    if (m_musicPlaying) return;
    if (m_leavingForGood) return;

    // buscar path de la cancion
    std::string songPath;
    if (m_featuredLevel->m_songID > 0) {
        if (MusicDownloadManager::sharedState()->isSongDownloaded(m_featuredLevel->m_songID)) {
            songPath = MusicDownloadManager::sharedState()->pathForSong(m_featuredLevel->m_songID);
        }
    } else {
        std::string filename = LevelTools::getAudioFileName(m_featuredLevel->m_audioTrack);
        songPath = CCFileUtils::sharedFileUtils()->fullPathForFilename(filename.c_str(), false);
        if (songPath.empty()) songPath = filename;
    }

    if (songPath.empty()) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;

    // asegurar BG silenciado
    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(0.f);
    }

    auto* audioGroup = ensureLeaderboardAudioGroup(engine->m_system, m_levelAudioGroup);
    if (!audioGroup) return;

    // crear sonido
    FMOD::Sound* sound = nullptr;
    FMOD_RESULT result = engine->m_system->createSound(
        songPath.c_str(), FMOD_CREATESTREAM | FMOD_LOOP_NORMAL, nullptr, &sound);
    if (result != FMOD_OK || !sound) return;
    m_levelMusicSound = sound;

    // reproducir (pausado para configurar)
    FMOD::Channel* channel = nullptr;
    result = engine->m_system->playSound(m_levelMusicSound, audioGroup, true, &channel);
    if (result != FMOD_OK || !channel) {
        m_levelMusicSound->release();
        m_levelMusicSound = nullptr;
        return;
    }
    m_levelMusicChannel = channel;

    // seek aleatorio
    unsigned int lengthMs = 0;
    m_levelMusicSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
    if (lengthMs > 10000) {
        static std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<unsigned int> dist(
            (unsigned int)(lengthMs * 0.1f), (unsigned int)(lengthMs * 0.8f));
        m_levelMusicChannel->setPosition(dist(gen), FMOD_TIMEUNIT_MS);
    }

    // Desactivado: no aplicar DSP "cueva" en daily/weekly.

    // FFT DSP para analisis de audio (visuales reactivos)
    if (!m_fftDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_FFT, &m_fftDSP);
        if (m_fftDSP) {
            m_fftDSP->setParameterInt(FMOD_DSP_FFT_WINDOWSIZE, 512);
        }
    }
    if (m_fftDSP) {
        m_levelMusicChannel->addDSP(2, m_fftDSP);
    }

    // fade-in desde 0
    float gameVol = engine->m_musicVolume;
    float targetVol = gameVol * 0.55f;
    m_levelMusicChannel->setVolume(0.f);
    m_levelMusicChannel->setPaused(false);
    m_musicPlaying = true;

    m_isFadingCaveIn = true;
    m_isFadingCaveOut = false;
    executeCaveFade(0, AUDIO_FADE_STEPS, 0.f, targetVol, false);
}

void LeaderboardLayer::fadeOutCaveMusic() {
    if (!m_musicPlaying || !m_levelMusicChannel) return;

    m_isFadingCaveIn = false;

    float currentVol = 0.f;
    m_levelMusicChannel->getVolume(&currentVol);
    if (currentVol <= 0.001f) {
        killCaveMusic();
        return;
    }

    m_isFadingCaveOut = true;
    executeCaveFade(0, AUDIO_FADE_STEPS, currentVol, 0.f, true);
}

void LeaderboardLayer::killCaveMusic() {
    m_isFadingCaveIn = false;
    m_isFadingCaveOut = false;

    removeCaveEffect();

    if (m_levelMusicChannel) {
        m_levelMusicChannel->stop();
        m_levelMusicChannel = nullptr;
    }
    if (m_levelMusicSound) {
        m_levelMusicSound->release();
        m_levelMusicSound = nullptr;
    }
    if (m_levelAudioGroup) {
        m_levelAudioGroup->stop();
        m_levelAudioGroup->release();
        m_levelAudioGroup = nullptr;
    }
    m_musicPlaying = false;
}

void LeaderboardLayer::executeCaveFade(int step, int totalSteps, float from, float to, bool fadeOut) {
    if (step > totalSteps) {
        if (fadeOut) {
            // fade-out terminado: limpiar todo
            killCaveMusic();
        } else {
            // fade-in terminado: fijar volumen final
            if (m_levelMusicChannel) m_levelMusicChannel->setVolume(to);
            m_isFadingCaveIn = false;
        }
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);
    float vol = from + (to - from) * eT;

    if (m_levelMusicChannel) {
        m_levelMusicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));
    }

    float stepDelay = (AUDIO_FADE_MS / static_cast<float>(totalSteps)) / 1000.f;
    int next = step + 1;
    int token = m_lifecycleToken;

    Ref<LeaderboardLayer> safeRef = this;
    paimon::scheduleMainThreadDelay(stepDelay, [safeRef, next, totalSteps, from, to, fadeOut, token]() {
        if (!safeRef->getParent()) return;
        if (safeRef->m_lifecycleToken != token) return;
        if (fadeOut && !safeRef->m_isFadingCaveOut) return;
        if (!fadeOut && !safeRef->m_isFadingCaveIn) return;
        safeRef->executeCaveFade(next, totalSteps, from, to, fadeOut);
    });
}

void LeaderboardLayer::applyCaveEffect() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !m_levelMusicChannel) return;

    // lowpass filter — simula paredes de cueva
    if (!m_lowpassDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &m_lowpassDSP);
        if (m_lowpassDSP) {
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 1200.f);
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 2.0f);
        }
    }

    // reverb sutil — eco de cueva
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

    if (m_lowpassDSP) m_levelMusicChannel->addDSP(0, m_lowpassDSP);
    if (m_reverbDSP) m_levelMusicChannel->addDSP(1, m_reverbDSP);
}

void LeaderboardLayer::removeCaveEffect() {
    if (m_levelMusicChannel) {
        if (m_lowpassDSP) m_levelMusicChannel->removeDSP(m_lowpassDSP);
        if (m_reverbDSP) m_levelMusicChannel->removeDSP(m_reverbDSP);
        if (m_fftDSP) m_levelMusicChannel->removeDSP(m_fftDSP);
    }
    if (m_lowpassDSP) { m_lowpassDSP->release(); m_lowpassDSP = nullptr; }
    if (m_reverbDSP) { m_reverbDSP->release(); m_reverbDSP = nullptr; }
    if (m_fftDSP) { m_fftDSP->release(); m_fftDSP = nullptr; }
}

// ── control de musica de menu (fade suave) ──────────────
void LeaderboardLayer::fadeOutMenuMusic() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return;

    float currentVol = 0.f;
    engine->m_backgroundMusicChannel->getVolume(&currentVol);
    if (currentVol <= 0.001f) return;

    executeMenuFade(0, AUDIO_FADE_STEPS, currentVol, 0.f);
}

void LeaderboardLayer::fadeInMenuMusic() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) return;

    float targetVol = engine->m_musicVolume;
    float currentVol = 0.f;
    engine->m_backgroundMusicChannel->getVolume(&currentVol);

    // despausar si estaba pausado
    bool isPaused = false;
    engine->m_backgroundMusicChannel->getPaused(&isPaused);
    if (isPaused) {
        engine->m_backgroundMusicChannel->setPaused(false);
    }

    executeMenuFade(0, AUDIO_FADE_STEPS, currentVol, targetVol);
}

void LeaderboardLayer::executeMenuFade(int step, int totalSteps, float from, float to) {
    if (step > totalSteps) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine && engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(to);
        }
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);
    float vol = from + (to - from) * eT;

    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));
    }

    float stepDelay = (AUDIO_FADE_MS / static_cast<float>(totalSteps)) / 1000.f;
    int next = step + 1;
    int token = m_lifecycleToken;

    Ref<LeaderboardLayer> safeRef = this;
    paimon::scheduleMainThreadDelay(stepDelay, [safeRef, next, totalSteps, from, to, token]() {
        if (!safeRef->getParent()) return;
        if (safeRef->m_lifecycleToken != token) return;
        safeRef->executeMenuFade(next, totalSteps, from, to);
    });
}

void LeaderboardLayer::ensureBgSilenced() {
    if (m_leavingForGood) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(0.f);
    }
}

void LeaderboardLayer::delaySilenceBg(float dt) {
    ensureBgSilenced();
}

void LeaderboardLayer::delaySilenceBg2(float dt) {
    ensureBgSilenced();
}

LeaderboardLayer::~LeaderboardLayer() {
    // restaurar BG inmediatamente en destructor (safety net)
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
        bool isPaused = false;
        engine->m_backgroundMusicChannel->getPaused(&isPaused);
        if (isPaused) engine->m_backgroundMusicChannel->setPaused(false);
    }
    if (m_levelAudioGroup) {
        m_levelAudioGroup->release();
        m_levelAudioGroup = nullptr;
    }
    m_featuredLevel = nullptr;
}
