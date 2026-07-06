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
#include "../../../core/RuntimeLifecycle.hpp"
#include <Geode/utils/web.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <matjson.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../../../utils/Shaders.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/ScissorClipNode.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../foryou/services/ForYouTracker.hpp"
#include "../../foryou/services/ForYouEngine.hpp"
#include "../../foryou/services/LevelTagsIntegration.hpp"
#include "../../foryou/ui/ForYouPreferencesPopup.hpp"
#include "../../dynamic-songs/services/DynamicSongManager.hpp"
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

// Gets the underlying FMOD::Channel for the main music BG group, same approach
// as DynamicSongManager. Returns nullptr if nothing is currently playing.
static FMOD::Channel* lbGetMainBgChannel(FMODAudioEngine* engine) {
    if (!engine) return nullptr;
    if (auto* channel = engine->getActiveMusicChannel(0)) {
        return channel;
    }
    if (!engine->m_backgroundMusicChannel) return nullptr;

    int numCh = 0;
    engine->m_backgroundMusicChannel->getNumChannels(&numCh);
    if (numCh <= 0) return nullptr;
    FMOD::Channel* ch = nullptr;
    if (engine->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK) return nullptr;
    return ch;
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
    
    auto winSize = CCDirector::get()->getWinSize();

    auto bg = CCLayerColor::create(ccc4(12, 10, 20, 255));
    bg->setID("background"_spr);
    bg->setContentSize(winSize);
    bg->setZOrder(-10);
    this->addChild(bg);

    m_bgSprite = LeaderboardPaimonSprite::create(); 
    m_bgSprite->setPosition(winSize / 2);
    m_bgSprite->setVisible(false);
    m_bgSprite->setZOrder(-5);
    this->addChild(m_bgSprite);

    m_bgOverlay = CCLayerColor::create({0, 0, 0, 0});
    m_bgOverlay->setContentSize(winSize);
    m_bgOverlay->setZOrder(-4);
    this->addChild(m_bgOverlay);

    m_particleContainer = CCNode::create();
    m_particleContainer->setPosition({0, 0});
    m_particleContainer->setZOrder(-3);
    this->addChild(m_particleContainer);

    auto vignette = CCLayerColor::create({0, 0, 0, 140});
    vignette->setContentSize(winSize);
    vignette->setZOrder(-2);
    this->addChild(vignette);

    m_glowOverlay = CCLayerColor::create({255, 180, 50, 0});
    m_glowOverlay->setContentSize(winSize);
    m_glowOverlay->setZOrder(-1);
    this->addChild(m_glowOverlay);

    m_beatFlash = CCLayerColor::create({255, 255, 255, 0});
    m_beatFlash->setContentSize(winSize);
    m_beatFlash->setZOrder(-1);
    this->addChild(m_beatFlash);

    this->scheduleUpdate();

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

    m_loadingSpinner = PaimonLoadingOverlay::create("Loading...", 50.f);
    if (m_loadingSpinner) {
        m_loadingSpinner->show(this, 100);
    }

    this->setKeypadEnabled(true);

    // Hard-stop whatever is on the BG channel BEFORE startCaveMusic runs.
    // If the menu music stays at vol 0 it can come back any time the user
    // touches the volume slider (GD's volume scroll re-applies channel volume
    // from m_musicVolume), which is the bleed the user reported. Stopping the
    // channel guarantees no audio plays until we replace it with the cave song.
    {
        auto* dsm = DynamicSongManager::get();
        if (dsm && dsm->isActive()) {
            // suspendPlaybackForExternalAudio() also calls
            // m_backgroundMusicChannel->stop() internally and saves the
            // position so we can resume on onBack.
            dsm->suspendPlaybackForExternalAudio();
            m_didSuspendDynSong = true;
        } else {
            auto engine = FMODAudioEngine::sharedEngine();
            if (engine && engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->stop();
            }
        }
    }

    m_dataLoaded = false;
    m_thumbLoaded = false;
    m_listCreated = false;

    loadLeaderboard("daily");

    return true;
}

void LeaderboardLayer::onEnterTransitionDidFinish() {
    CCLayer::onEnterTransitionDidFinish();

    // Returning from a pushed scene (LevelInfoLayer, history). The pushed scene
    // overwrote our cave music on the BG channel; restart it from the saved
    // position so the user experiences a near-seamless return.
    if (m_caveMusicShouldRestore && !m_musicPlaying && !m_leavingForGood) {
        startCaveMusic();
    }

    m_goingToHistory = false;
}

void LeaderboardLayer::onExit() {
    ++m_lifecycleToken;
    ++m_requestGeneration;
    m_isFadingCaveIn = false;
    m_isFadingCaveOut = false;

    this->unscheduleUpdate();
    this->unschedule(schedule_selector(LeaderboardLayer::spawnThemeParticle));
    clearParticles();

    if (GameLevelManager::get()->m_levelManagerDelegate == this) {
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
    }

    // Only kill cave music if we're truly leaving (back/destroyed). Pushing a
    // scene leaves m_leavingForGood=false; for that path, onExitTransitionDidStart
    // saved the position and we keep the FFT DSP intact for the brief window.
    if (m_leavingForGood) {
        killCaveMusic();
    }

    CCLayer::onExit();
}

void LeaderboardLayer::onExitTransitionDidStart() {
    CCLayer::onExitTransitionDidStart();

    // Pushed to another scene (LevelInfoLayer / history). Save the playback
    // position so we can resume on the return trip. We do NOT stop the channel
    // — the next scene's playMusic call will replace it.
    if (!m_leavingForGood && m_musicPlaying) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (engine) {
            if (auto* bgCh = lbGetMainBgChannel(engine)) {
                unsigned int posMs = 0;
                if (bgCh->getPosition(&posMs, FMOD_TIMEUNIT_MS) == FMOD_OK) {
                    m_savedCaveMusicPosMs = posMs;
                }
            }
        }
        // Stop the fades, but don't kill the audio (next scene takes the channel).
        m_isFadingCaveIn = false;
        m_isFadingCaveOut = false;
        m_musicPlaying = false;
        // m_caveMusicShouldRestore stays true so we restart on the way back.
    }
}

void LeaderboardLayer::onBack(CCObject*) {
    m_leavingForGood = true;
    killCaveMusic();

    // Resume the dynamic song (if we suspended it on entry) or restart the
    // menu music ourselves. Both paths use engine->playMusic, since the BG
    // channel was hard-stopped on entry and a simple volume fade-in wouldn't
    // produce audible output.
    if (m_didSuspendDynSong) {
        auto* dsm = DynamicSongManager::get();
        if (dsm && dsm->hasSuspendedPlayback()) {
            dsm->resumeSuspendedPlayback();
        }
        m_didSuspendDynSong = false;
    } else {
        auto engine = FMODAudioEngine::sharedEngine();
        auto gm = GameManager::get();
        if (engine && gm && !gm->getGameVariable("0122") && engine->m_musicVolume > 0.f) {
            engine->playMusic(gm->getMenuMusicFile(), true, 0.0f, 0);
            if (engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setVolume(engine->m_musicVolume);
            }
        }
    }

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

    if (type == "foryou" && !Mod::get()->getSavedValue<bool>("enable-for-you", false)) {
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

    if (auto* oldList = this->getChildByID("paimon-leaderboard-list"_spr)) {
        oldList->removeFromParent();
    }
    m_scroll = nullptr;
    m_listMenu = nullptr;

    m_dataLoaded = false;
    m_thumbLoaded = false;
    m_listCreated = false;

    if (m_loadingSpinner) {
        m_loadingSpinner->setVisible(true);
    }

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

// GD-native panel helper.
// Tries NineSlice -> CCScale9Sprite -> paimondraw fallback. Returns the node added.
static CCNode* lbAddPanel(CCNode* parent, char const* frame, float w, float h,
                          CCPoint pos, ccColor3B color, GLubyte opacity, int z) {
    if (auto* ns = paimon::SpriteHelper::safeCreateNineSlice(frame, {6.f, 6.f, 6.f, 6.f})) {
        ns->setContentSize({w, h});
        ns->setAnchorPoint({0, 0});
        ns->setPosition(pos);
        ns->setColor(color);
        ns->setOpacity(opacity);
        parent->addChild(ns, z);
        return ns;
    }
    if (auto* s9 = paimon::SpriteHelper::safeCreateScale9WithFrameName(frame)) {
        s9->setContentSize({w, h});
        s9->setAnchorPoint({0, 0});
        s9->setPosition(pos);
        s9->setColor(color);
        s9->setOpacity(opacity);
        parent->addChild(s9, z);
        return s9;
    }
    // Last-resort paimondraw fallback (only used if GD assets are missing).
    ccColor4F fill = { color.r / 255.f, color.g / 255.f, color.b / 255.f, opacity / 255.f };
    if (auto* fb = paimon::SpriteHelper::createRoundedRect(w, h, 8.f, fill)) {
        fb->setPosition(pos);
        parent->addChild(fb, z);
        return fb;
    }
    return nullptr;
}

void LeaderboardLayer::createList(std::string type) {
    this->removeChildByID("paimon-leaderboard-list"_spr);

    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setID("paimon-leaderboard-list"_spr);
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
    bool isDaily = (type == "daily");

    // ============ CARD ============
    float cardW = 460.f;
    float cardH = 220.f;
    float cardY = winSize.height / 2 - 12.f;

    auto card = CCNode::create();
    card->setContentSize({cardW, cardH});
    card->setAnchorPoint({0.5f, 0.5f});
    card->setPosition({winSize.width / 2, cardY});
    container->addChild(card, 5);

    // Pop-in animation
    card->setScale(0.85f);
    card->runAction(CCEaseBackOut::create(CCScaleTo::create(0.5f, 1.0f)));

    // Main card background: GD's beveled GJ_square01 panel, tinted dark.
    lbAddPanel(card, "GJ_square01.png", cardW, cardH, {0.f, 0.f}, {30, 32, 42}, 245, -1);

    // ============ THUMBNAIL ============
    float pad = 12.f;
    float thumbW = 204.f;
    float thumbH = cardH - pad * 2.f;
    float thumbX = pad;
    float thumbY = pad;

    // Thumbnail frame using GJ_square03 (lighter accent).
    lbAddPanel(card, "GJ_square03.png", thumbW + 4.f, thumbH + 4.f,
               {thumbX - 2.f, thumbY - 2.f}, {72, 78, 96}, 230, 0);

    auto clipper = CCClippingNode::create();
    clipper->setContentSize({thumbW, thumbH});
    clipper->setAnchorPoint({0, 0});
    clipper->setPosition({thumbX, thumbY});
    // Rectangular stencil is fine: thumbnail sits inside a GD-square frame.
    clipper->setStencil(paimon::SpriteHelper::createRectStencil(thumbW, thumbH));
    card->addChild(clipper, 2);

    auto thumbPlaceholder = CCLayerColor::create({22, 24, 32, 255});
    thumbPlaceholder->setContentSize({thumbW, thumbH});
    thumbPlaceholder->setTag(101);
    clipper->addChild(thumbPlaceholder, 0);

    // Right-edge gradient for readability where text sits behind the thumb.
    auto thumbGrad = CCLayerGradient::create({0, 0, 0, 0}, {30, 32, 42, 200}, {1, 0});
    thumbGrad->setContentSize({thumbW * 0.30f, thumbH});
    thumbGrad->setPosition({thumbW - thumbW * 0.30f, 0.f});
    clipper->addChild(thumbGrad, 10);

    Ref<LeaderboardLayer> self = this;
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
        m_thumbLoaded = true;
        checkLoadingComplete();
    } else if (levelID > 0) {
        std::string fileName = fmt::format("{}.png", levelID);
        Ref<CCClippingNode> safeClipper = clipper;

        auto requestToken = m_requestGeneration;
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, safeClipper, createThumbSprite, requestToken](CCTexture2D* tex, bool) {
            geode::Loader::get()->queueInMainThread([self, safeClipper, tex, createThumbSprite, requestToken] {
                if (paimon::isRuntimeShuttingDown()) return;
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

    // ============ INFO PANEL ============
    float infoX = thumbX + thumbW + 10.f;
    float infoW = cardW - infoX - pad;
    float infoH = thumbH;

    auto infoPanel = CCNode::create();
    infoPanel->setContentSize({infoW, infoH});
    infoPanel->setPosition({infoX, pad});
    card->addChild(infoPanel, 3);

    // Info panel background: GJ_square05 tinted slightly darker than card.
    lbAddPanel(infoPanel, "GJ_square05.png", infoW, infoH, {0.f, 0.f}, {22, 24, 32}, 230, 0);

    // ============ BADGE (DAILY / WEEKLY) ============
    {
        char const* badgeFrame = isDaily ? "GJ_longBtn01_001.png" : "GJ_longBtn02_001.png";
        auto badgeBg = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName(badgeFrame);
        if (badgeBg) {
            float badgeW = 108.f;
            float badgeH = 28.f;
            badgeBg->setContentSize({badgeW, badgeH});
            badgeBg->setAnchorPoint({1.f, 1.f});
            badgeBg->setPosition({infoW - 6.f, infoH - 6.f});
            infoPanel->addChild(badgeBg, 10);

            // Real GD icon (Daily / Weekly).
            auto badgeIcon = paimon::SpriteHelper::safeCreateWithFrameName(
                isDaily ? "GJ_dailyBtn_001.png" : "GJ_weeklyBtn_001.png");
            float labelLeft = 10.f;
            if (badgeIcon) {
                badgeIcon->setScale(0.36f);
                badgeIcon->setAnchorPoint({0.f, 0.5f});
                badgeIcon->setPosition({8.f, badgeH / 2.f});
                badgeBg->addChild(badgeIcon, 1);
                labelLeft = 26.f;
            }

            auto badgeLbl = CCLabelBMFont::create(isDaily ? "DAILY" : "WEEKLY", "goldFont.fnt");
            badgeLbl->setScale(0.45f);
            float maxLblW = badgeW - labelLeft - 10.f;
            if (badgeLbl->getScaledContentSize().width > maxLblW) {
                badgeLbl->setScale(badgeLbl->getScale() * (maxLblW / badgeLbl->getScaledContentSize().width));
            }
            badgeLbl->setAnchorPoint({0.f, 0.5f});
            badgeLbl->setPosition({labelLeft, badgeH / 2.f});
            badgeBg->addChild(badgeLbl, 2);
            badgeLbl->setTag(TAG_BADGE_LABEL);
        }
    }

    // ============ CARD HIT AREA (entire card opens level info) ============
    auto cellMenu = CCMenu::create();
    cellMenu->setPosition({0.f, 0.f});
    cellMenu->setContentSize({cardW, cardH});
    card->addChild(cellMenu, 50);

    {
        auto hitArea = CCSprite::create();
        if (hitArea) {
            hitArea->setTextureRect(CCRect(0, 0, 1, 1));
            hitArea->setScaleX(cardW);
            hitArea->setScaleY(cardH);
            hitArea->setOpacity(0);

            auto playBtn = CCMenuItemSpriteExtra::create(hitArea, self, menu_selector(LeaderboardLayer::onViewLevel));
            playBtn->setUserObject(level);
            playBtn->setPosition({cardW / 2.f, cardH / 2.f});
            PaimonButtonHighlighter::registerButton(playBtn);
            cellMenu->addChild(playBtn, 100);
        }
    }

    // ============ TEXT (NAME / CREATOR / SEPARATOR) ============
    float textX = 14.f;
    float nameY = infoH - 46.f;
    float textMaxW = infoW - 28.f;

    auto nameLbl = CCLabelBMFont::create(level->m_levelName.c_str(), "bigFont.fnt");
    nameLbl->setScale(0.70f);
    nameLbl->setColor({240, 240, 248});
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setPosition({textX, nameY});
    nameLbl->setTag(TAG_NAME_LABEL);
    if (nameLbl->getScaledContentSize().width > textMaxW) {
        nameLbl->setScale(nameLbl->getScale() * (textMaxW / nameLbl->getScaledContentSize().width));
    }
    infoPanel->addChild(nameLbl, 10);

    std::string creatorStr = level->m_creatorName.size() > 0
        ? "by " + std::string(level->m_creatorName) : "";
    auto creatorLbl = CCLabelBMFont::create(creatorStr.c_str(), "goldFont.fnt");
    creatorLbl->setScale(0.45f);
    creatorLbl->setAnchorPoint({0.f, 0.5f});
    creatorLbl->setPosition({textX, nameY - 22.f});
    creatorLbl->setTag(TAG_CREATOR_LABEL);
    if (creatorLbl->getScaledContentSize().width > textMaxW) {
        creatorLbl->setScale(creatorLbl->getScale() * (textMaxW / creatorLbl->getScaledContentSize().width));
    }
    infoPanel->addChild(creatorLbl, 10);

    // Thin separator using a 1px stretched CCSprite (no paimondraw).
    {
        auto sep = CCSprite::create();
        if (sep) {
            sep->setTextureRect(CCRect(0, 0, 1, 1));
            sep->setScaleX(textMaxW);
            sep->setScaleY(1.5f);
            sep->setColor({80, 85, 100});
            sep->setOpacity(160);
            sep->setAnchorPoint({0.f, 0.5f});
            sep->setPosition({textX, nameY - 40.f});
            infoPanel->addChild(sep, 10);
        }
    }

    // ============ TIMER CHIP ============
    if (m_featuredExpiresAt > 0) {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        long long diff = m_featuredExpiresAt - now;

        if (diff > 0) {
            int hours = (int)(diff / (1000LL * 60 * 60));
            int mins = (int)((diff % (1000LL * 60 * 60)) / (1000LL * 60));

            float chipW = 158.f;
            float chipH = 24.f;
            float chipX = textX;
            float chipY = nameY - 66.f;

            // Chip background using GJ_square05 tinted.
            lbAddPanel(infoPanel, "GJ_square05.png", chipW, chipH,
                       {chipX, chipY}, {40, 44, 56}, 220, 10);

            // Time icon (real GD asset).
            float iconX = chipX + 10.f;
            auto timeIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_timeIcon_001.png");
            float labelOffset = 0.f;
            if (timeIcon) {
                timeIcon->setScale(0.42f);
                timeIcon->setColor({210, 215, 225});
                timeIcon->setAnchorPoint({0.f, 0.5f});
                timeIcon->setPosition({iconX, chipY + chipH / 2.f});
                infoPanel->addChild(timeIcon, 11);
                labelOffset = 14.f;
            }

            auto timeLbl = CCLabelBMFont::create(
                fmt::format("Ends in {}h {}m", hours, mins).c_str(), "chatFont.fnt");
            timeLbl->setScale(0.50f);
            timeLbl->setColor({200, 205, 220});
            timeLbl->setAnchorPoint({0.f, 0.5f});
            timeLbl->setPosition({iconX + labelOffset, chipY + chipH / 2.f});
            timeLbl->setTag(TAG_TIME_LABEL);
            infoPanel->addChild(timeLbl, 11);
        }
    }

    // ============ PLAY BUTTON (native GD ButtonSprite) ============
    auto playMenu = CCMenu::create();
    playMenu->setPosition({0.f, 0.f});
    infoPanel->addChild(playMenu, 15);

    {
        auto playSpr = ButtonSprite::create("PLAY", 130, true, "bigFont.fnt", "GJ_button_01.png", 38.f, 0.85f);
        if (playSpr) {
            auto playBtnVis = CCMenuItemSpriteExtra::create(playSpr, self, menu_selector(LeaderboardLayer::onViewLevel));
            playBtnVis->setUserObject(level);
            playBtnVis->setPosition({infoW / 2.f, 26.f});
            playBtnVis->m_scaleMultiplier = 1.1f;
            PaimonButtonHighlighter::registerButton(playBtnVis);
            playMenu->addChild(playBtnVis);
        }
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

    int minLevels = Mod::get()->getSavedValue<int64_t>("for-you-min-levels", 5);
    if (!tracker.hasEnoughData(minLevels)) {
        this->removeChildByID("paimon-leaderboard-list"_spr);

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

    if (!paimon::compat::ModCompat::isLevelTagsLoaded() &&
        !Mod::get()->getSavedValue<bool>("foryou-tags-prompt-shown", false)) {
        Mod::get()->setSavedValue("foryou-tags-prompt-shown", true);
        geode::openInfoPopup("kampwski.level_tags");
    }

    m_forYouQueryQueue = paimon::foryou::ForYouEngine::get().generateQueries(3);
    if (m_forYouQueryQueue.empty()) {
        m_dataLoaded = true;
        m_thumbLoaded = true;
        if (m_loadingSpinner) m_loadingSpinner->setVisible(false);
        return;
    }

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
    this->removeChildByID("paimon-leaderboard-list"_spr);

    auto winSize = CCDirector::get()->getWinSize();

    auto container = CCNode::create();
    container->setID("paimon-leaderboard-list"_spr);
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
                   Mod::get()->getSavedValue<bool>("for-you-use-tags", true);

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

        auto clipper = paimon::ScissorClipNode::create();
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
            auto requestToken = self->m_requestGeneration;
            ThumbnailLoader::get().requestLoad(levelID, fileName, [self, safeClipper, createThumbSprite, requestToken](CCTexture2D* tex, bool) {
                geode::Loader::get()->queueInMainThread([self, safeClipper, tex, createThumbSprite, requestToken] {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (!self->getParent()) return;
                    if (self->m_requestGeneration != requestToken) return;
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
        auto savedLevel = GameLevelManager::get()->getSavedLevel(level->m_levelID);
        GJGameLevel* levelToUse = level;

        if (savedLevel) {
            levelToUse = savedLevel;
        }

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

    float bassSum = 0.f;
    int bassBins = std::min(8, fftData->length);
    for (int i = 0; i < bassBins; i++) {
        bassSum += fftData->spectrum[0][i];
    }
    float bassAvg = (bassBins > 0) ? bassSum / bassBins : 0.f;

    float midSum = 0.f;
    int midStart = std::min(8, fftData->length);
    int midEnd = std::min(32, fftData->length);
    for (int i = midStart; i < midEnd; i++) {
        midSum += fftData->spectrum[0][i];
    }
    float midAvg = (midEnd > midStart) ? midSum / (midEnd - midStart) : 0.f;

    return bassAvg * 0.7f + midAvg * 0.3f;
}

void LeaderboardLayer::updateAudioReactive(float dt) {
    m_audioReactTime += dt;

    float rawBass = getAudioBassLevel();
    
    float normalizedBass = std::min(1.f, rawBass * 12.f);
    
    float delta = normalizedBass - m_prevBassLevel;
    m_prevBassLevel = normalizedBass;
    
    float beatThreshold = 0.15f;
    if (delta > beatThreshold) {
        m_beatPulse = std::min(1.f, m_beatPulse + delta * 2.5f);
    }
    
    m_beatPulse = std::max(0.f, m_beatPulse - dt * 3.5f);
    
    float targetGlow = normalizedBass * 0.6f;
    m_glowPulse += (targetGlow - m_glowPulse) * std::min(1.f, dt * 8.f);
    
    m_bgPulse += (normalizedBass * 0.4f - m_bgPulse) * std::min(1.f, dt * 6.f);
    
    m_particleBoost = std::max(0.f, m_particleBoost - dt * 2.f);
    if (delta > beatThreshold * 1.2f) {
        m_particleBoost = std::min(1.f, m_particleBoost + 0.5f);
    }
    
    if (m_glowOverlay) {
        float t = (std::sin(m_audioReactTime * 0.5f) + 1.f) * 0.5f;
        GLubyte r = (GLubyte)(m_themeColorA.r + (m_themeColorB.r - m_themeColorA.r) * t);
        GLubyte g = (GLubyte)(m_themeColorA.g + (m_themeColorB.g - m_themeColorA.g) * t);
        GLubyte b = (GLubyte)(m_themeColorA.b + (m_themeColorB.b - m_themeColorA.b) * t);
        m_glowOverlay->setColor({r, g, b});
        
        float glowAlpha = m_glowPulse * 18.f + m_beatPulse * 25.f;
        m_glowOverlay->setOpacity((GLubyte)std::min(45.f, glowAlpha));
    }
    
    if (m_beatFlash) {
        float flashAlpha = m_beatPulse * 35.f;
        m_beatFlash->setOpacity((GLubyte)std::min(30.f, flashAlpha));
    }
    
    if (m_bgSprite) {
        if (auto paimonSprite = typeinfo_cast<LeaderboardPaimonSprite*>(m_bgSprite)) {
            float baseBrightness = 1.0f + m_bgPulse * 0.3f + m_beatPulse * 0.15f;
            paimonSprite->m_brightness = baseBrightness;
        }
    }
    
    if (m_bgOverlay) {
        float baseOverlay = 100.f;
        float overlayReduction = m_beatPulse * 30.f + m_glowPulse * 15.f;
        m_bgOverlay->setOpacity((GLubyte)std::max(40.f, baseOverlay - overlayReduction));
    }
    
    if (m_particleBoost > 0.3f && m_particleContainer) {
        spawnThemeParticle(0.f);
    }
}

void LeaderboardLayer::update(float dt) {
    m_blurTime += dt;
    
    if (m_bgSprite) {
        if (!m_bgSpriteCastCached) {
            m_bgSpriteCastCached = true;
            m_cachedPaimonSprite = typeinfo_cast<LeaderboardPaimonSprite*>(m_bgSprite);
        }
        if (m_cachedPaimonSprite) {
             float intensity = 0.75f + std::sin(m_blurTime * 1.0f) * 0.75f;
             static_cast<LeaderboardPaimonSprite*>(m_cachedPaimonSprite)->m_intensity = intensity;
        }
    }
    
    if (m_musicPlaying && m_levelMusicChannel) {
        updateAudioReactive(dt);
    } else {
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

    auto winSize = CCDirector::get()->getWinSize();
    
    auto newSprite = createLeaderboardBlurredSprite(texture, winSize, 0.095f);
    
    if (newSprite) {
        newSprite->setPosition(winSize / 2);
        newSprite->setZOrder(-5);
        newSprite->setOpacity(0);
        
        auto shader = BlurSystem::getInstance()->getRealtimeBlurShader();
        if (!shader) shader = paimon::shaders::getBlurSinglePassShader();
        if (shader) {
            newSprite->setShaderProgram(shader);
            newSprite->m_intensity = 0.0f;
            newSprite->m_texSize = newSprite->getTexture()->getContentSizeInPixels();
        }
        
        this->addChild(newSprite);
        
        float duration = 0.5f;
        newSprite->runAction(CCFadeIn::create(duration));
        
        auto breathe = CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(6.0f, 1.05f),
            CCScaleTo::create(6.0f, 1.0f),
            nullptr
        ));
        newSprite->runAction(breathe);
        
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
    
    if (m_bgOverlay) {
        m_bgOverlay->stopAllActions();
        m_bgOverlay->runAction(CCFadeTo::create(0.5f, 100));
    }
}

void LeaderboardLayer::updateBackground(int levelID) {
    log::info("[LeaderboardLayer] Updating background for levelID: {}", levelID);
    int requestToken = static_cast<int>(m_requestGeneration);

    if (levelID <= 0) {
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

    if (m_forYouActive) {
        if (m_currentType != "foryou") return;
        if (m_pendingLevelGeneration != m_requestGeneration) return;
        for (auto* level : CCArrayExt<GJGameLevel*>(levels)) {
            if (!level) continue;
            if (!paimon::foryou::ForYouTracker::get().isLevelTracked(level->m_levelID)) {
                m_forYouResults.push_back(level);
            }
        }
        m_forYouQueryIndex++;
        if (m_forYouQueryIndex < static_cast<int>(m_forYouQueryQueue.size())) {
            fireNextForYouQuery();
        } else {
            paimon::foryou::ForYouEngine::get().scoreAndSortResults(m_forYouResults);
            if (m_forYouResults.size() > 5) {
                m_forYouResults.resize(5);
            }
            m_dataLoaded = true;
            m_thumbLoaded = true;
            createForYouList();
            checkLoadingComplete();
        }
        return;
    }

    if (m_currentType != "daily" && m_currentType != "weekly") return;
    if (m_pendingLevelGeneration != m_requestGeneration) return;

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
}

void LeaderboardLayer::updateLevelInfo() {
    if (!m_featuredLevel) return;

    auto container = this->getChildByID("paimon-leaderboard-list"_spr);
    if (!container) return;

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

    if (auto nameLbl = typeinfo_cast<CCLabelBMFont*>(findByTag(findByTag, container, TAG_NAME_LABEL))) {
        nameLbl->setScale(0.70f);
        nameLbl->setString(m_featuredLevel->m_levelName.c_str());
        float maxNameW = 194.f;
        if (nameLbl->getScaledContentSize().width > maxNameW) {
            nameLbl->setScale(nameLbl->getScale() * (maxNameW / nameLbl->getScaledContentSize().width));
        }
    }

    if (auto creatorLbl = typeinfo_cast<CCLabelBMFont*>(findByTag(findByTag, container, TAG_CREATOR_LABEL))) {
        std::string creatorStr = m_featuredLevel->m_creatorName.size() > 0
            ? "by " + std::string(m_featuredLevel->m_creatorName) 
            : "";
        creatorLbl->setScale(0.45f);
        creatorLbl->setString(creatorStr.c_str());
        float maxCreatorW = 194.f;
        if (creatorLbl->getScaledContentSize().width > maxCreatorW) {
            creatorLbl->setScale(creatorLbl->getScale() * (maxCreatorW / creatorLbl->getScaledContentSize().width));
        }
    }
}

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

        // start music and themed particles
        if (m_featuredLevel) {
            startCaveMusic();
            auto colors = LevelColors::get().getPair(m_featuredLevel->m_levelID);
            if (colors.has_value()) {
                m_themeColorA = colors->a;
                m_themeColorB = colors->b;
            } else {
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
            m_themeColorA = {255, 100, 120};
            m_themeColorB = {200, 60, 100};
            createThemeParticles();
        }
    }
}

void LeaderboardLayer::clearParticles() {
    this->unschedule(schedule_selector(LeaderboardLayer::spawnThemeParticle));
    if (m_particleContainer) {
        m_particleContainer->removeAllChildren();
    }
}

void LeaderboardLayer::createThemeParticles() {
    clearParticles();
    
    this->schedule(schedule_selector(LeaderboardLayer::spawnThemeParticle), 0.4f);
    
    for (int i = 0; i < 8; i++) {
        spawnThemeParticle(0.f);
    }
}

void LeaderboardLayer::spawnThemeParticle(float dt) {
    if (!m_particleContainer) return;
    
    if (m_particleContainer->getChildrenCount() > 25) return;

    auto winSize = CCDirector::get()->getWinSize();

    float t = (rand() % 100) / 100.f;
    ccColor3B color = {
        (GLubyte)(m_themeColorA.r + (m_themeColorB.r - m_themeColorA.r) * t),
        (GLubyte)(m_themeColorA.g + (m_themeColorB.g - m_themeColorA.g) * t),
        (GLubyte)(m_themeColorA.b + (m_themeColorB.b - m_themeColorA.b) * t),
    };

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
    
    float baseScale = 0.08f + (rand() % 15) / 100.f;
    particle->setScale(baseScale);
    
    float startX = (rand() % (int)winSize.width);
    float startY = -10.f;
    particle->setPosition({startX, startY});
    particle->setOpacity(0);

    m_particleContainer->addChild(particle);

    float duration = 6.f + (rand() % 40) / 10.f;
    float driftX = ((rand() % 100) - 50) * 0.8f;
    float endY = winSize.height + 20.f;

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

void LeaderboardLayer::onHistory(CCObject*) {
    m_goingToHistory = true;
    auto scene = LeaderboardHistoryLayer::scene();
    TransitionManager::get().pushScene(scene);
}

void LeaderboardLayer::startCaveMusic() {
    if (!m_featuredLevel) return;
    if (m_musicPlaying) return;
    if (m_leavingForGood) return;

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

    // Take over the main BG channel via GD's native playMusic — same as
    // DynamicSongManager / LevelInfoLayer. This replaces whatever was on the
    // channel (menu music, dyn song) so there is no bleed when the user
    // changes the music volume slider.
    engine->playMusic(songPath, true, 0.0f, 0);

    // Seek: either resume from a saved position (after returning from a
    // pushed scene) or pick a random offset for variety.
    auto* bgCh = lbGetMainBgChannel(engine);
    if (bgCh) {
        if (m_savedCaveMusicPosMs > 0) {
            bgCh->setPosition(m_savedCaveMusicPosMs, FMOD_TIMEUNIT_MS);
            m_savedCaveMusicPosMs = 0;
        } else {
            FMOD::Sound* currentSound = nullptr;
            bgCh->getCurrentSound(&currentSound);
            if (currentSound) {
                unsigned int lengthMs = 0;
                currentSound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
                if (lengthMs > 10000) {
                    static std::mt19937 gen(std::random_device{}());
                    std::uniform_int_distribution<unsigned int> dist(
                        (unsigned int)(lengthMs * 0.1f), (unsigned int)(lengthMs * 0.8f));
                    bgCh->setPosition(dist(gen), FMOD_TIMEUNIT_MS);
                }
            }
        }
    }

    // FFT for audio-reactive visuals goes on the BG group.
    if (!m_fftDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_FFT, &m_fftDSP);
        if (m_fftDSP) {
            m_fftDSP->setParameterInt(FMOD_DSP_FFT_WINDOWSIZE, 512);
        }
    }
    if (m_fftDSP && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->addDSP(2, m_fftDSP);
    }

    // Fade in volume from 0 -> game music volume * cave factor.
    float gameVol = engine->m_musicVolume;
    float targetVol = gameVol * 0.55f;
    if (engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(0.f);
    }
    m_musicPlaying = true;
    m_caveMusicShouldRestore = true;

    m_isFadingCaveIn = true;
    m_isFadingCaveOut = false;
    executeCaveFade(0, AUDIO_FADE_STEPS, 0.f, targetVol, false);
}

void LeaderboardLayer::fadeOutCaveMusic() {
    if (!m_musicPlaying) return;

    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_backgroundMusicChannel) {
        killCaveMusic();
        return;
    }

    m_isFadingCaveIn = false;

    float currentVol = 0.f;
    engine->m_backgroundMusicChannel->getVolume(&currentVol);
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

    // Stop the BG channel hard so the menu music doesn't suddenly become
    // audible when the user adjusts the volume slider. The next scene
    // (CreatorLayer or resumed DynSong) will restart whatever it wants.
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->stop();
    }

    m_musicPlaying = false;
    m_caveMusicShouldRestore = false;
}

void LeaderboardLayer::executeCaveFade(int step, int totalSteps, float from, float to, bool fadeOut) {
    auto engine = FMODAudioEngine::sharedEngine();

    if (step > totalSteps) {
        if (fadeOut) {
            killCaveMusic();
        } else {
            if (engine && engine->m_backgroundMusicChannel) {
                engine->m_backgroundMusicChannel->setVolume(to);
            }
            m_isFadingCaveIn = false;
        }
        return;
    }

    float t = static_cast<float>(step) / static_cast<float>(totalSteps);
    float eT = (t < 0.5f) ? (2.f * t * t) : (1.f - std::pow(-2.f * t + 2.f, 2.f) / 2.f);
    float vol = from + (to - from) * eT;

    if (engine && engine->m_backgroundMusicChannel) {
        engine->m_backgroundMusicChannel->setVolume(std::max(0.f, std::min(1.f, vol)));
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
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return;

    if (!m_lowpassDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &m_lowpassDSP);
        if (m_lowpassDSP) {
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 1200.f);
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 2.0f);
        }
    }

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
}

void LeaderboardLayer::removeCaveEffect() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        if (m_lowpassDSP) engine->m_backgroundMusicChannel->removeDSP(m_lowpassDSP);
        if (m_reverbDSP) engine->m_backgroundMusicChannel->removeDSP(m_reverbDSP);
        if (m_fftDSP) engine->m_backgroundMusicChannel->removeDSP(m_fftDSP);
    }
    if (m_lowpassDSP) { m_lowpassDSP->release(); m_lowpassDSP = nullptr; }
    if (m_reverbDSP) { m_reverbDSP->release(); m_reverbDSP = nullptr; }
    if (m_fftDSP) { m_fftDSP->release(); m_fftDSP = nullptr; }
}

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
    // No-op after refactor. The cave music now plays on the main BG channel
    // directly via engine->playMusic, so there is no "menu music in parallel"
    // to silence anymore. Keep the symbol to satisfy the existing header /
    // scheduler bindings.
}

void LeaderboardLayer::delaySilenceBg(float) {
    // No-op (see ensureBgSilenced).
}

void LeaderboardLayer::delaySilenceBg2(float) {
    // No-op (see ensureBgSilenced).
}

LeaderboardLayer::~LeaderboardLayer() {
    // Safety net: if for some reason we exited without going through onBack
    // (e.g. external scene replacement), resume the dynamic song now.
    if (m_didSuspendDynSong) {
        auto* dsm = DynamicSongManager::get();
        if (dsm && dsm->hasSuspendedPlayback()) {
            dsm->resumeSuspendedPlayback();
        }
        m_didSuspendDynSong = false;
    }

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
