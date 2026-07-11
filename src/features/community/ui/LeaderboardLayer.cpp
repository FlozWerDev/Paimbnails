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
#include <Geode/binding/GJDifficultySprite.hpp>
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

    // Neon pill tabs built from GD assets: square02b fill + GJ_square07 border.
    // Dark capsule w/ accent outline when idle, glowing accent when active.
    auto createTab = [&](char const* text, char const* id, char const* iconFrame,
                         ccColor3B accent, CCPoint pos) -> CCMenuItemToggler* {
        float pillW = 116.f;
        float pillH = 34.f;

        auto buildPill = [&](bool active) -> CCNode* {
            auto node = CCNode::create();
            node->setContentSize({pillW, pillH});

            if (active) {
                if (auto* glow = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
                    glow->setContentSize({pillW + 14.f, pillH + 14.f});
                    glow->setAnchorPoint({0.f, 0.f});
                    glow->setPosition({-7.f, -7.f});
                    glow->setColor(accent);
                    glow->setOpacity(80);
                    node->addChild(glow, -2);
                }
            }

            ccColor3B bgCol = active
                ? ccColor3B{static_cast<GLubyte>(accent.r * 0.30f),
                            static_cast<GLubyte>(accent.g * 0.30f),
                            static_cast<GLubyte>(accent.b * 0.30f)}
                : ccColor3B{14, 16, 24};
            if (auto* bg = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
                bg->setContentSize({pillW, pillH});
                bg->setAnchorPoint({0.f, 0.f});
                bg->setColor(bgCol);
                bg->setOpacity(active ? 250 : 210);
                node->addChild(bg, -1);
            }
            if (auto* border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
                border->setContentSize({pillW, pillH});
                border->setAnchorPoint({0.f, 0.f});
                border->setColor(accent);
                border->setOpacity(active ? 255 : 80);
                node->addChild(border, 0);
            }

            float textLeft = 14.f;
            if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(iconFrame)) {
                float maxSide = std::max(icon->getContentSize().width, icon->getContentSize().height);
                if (maxSide > 0.f) icon->setScale(20.f / maxSide);
                icon->setPosition({17.f, pillH / 2.f});
                if (!active) {
                    icon->setColor({135, 140, 158});
                    icon->setOpacity(180);
                }
                node->addChild(icon, 2);
                textLeft = 30.f;
            }

            auto label = CCLabelBMFont::create(text, "bigFont.fnt");
            label->setScale(0.40f);
            float maxLabelW = pillW - textLeft - 10.f;
            if (label->getScaledContentSize().width > maxLabelW) {
                label->setScale(label->getScale() * (maxLabelW / label->getScaledContentSize().width));
            }
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({textLeft, pillH / 2.f + 1.f});
            if (active) {
                label->setColor(accent);
            } else {
                label->setColor({150, 155, 172});
            }
            node->addChild(label, 2);

            return node;
        };

        auto onSprite = buildPill(true);
        auto offSprite = buildPill(false);

        auto tab = CCMenuItemToggler::create(offSprite, onSprite, this, menu_selector(LeaderboardLayer::onTab));
        tab->setUserObject(CCString::create(id));
        tab->setPosition(pos);
        m_tabs.push_back(tab);

        return tab;
    };

    float topY = winSize.height - 24.f;
    float centerX = winSize.width / 2;
    float btnSpacing = 126.f;
    auto dailyBtn = createTab(Localization::get().getString("leaderboard.daily").c_str(), "daily",
        "GJ_dailyBtn_001.png", {255, 190, 60}, {centerX - btnSpacing, topY});
    dailyBtn->toggle(true);
    tabMenu->addChild(dailyBtn);

    auto weeklyBtn = createTab(Localization::get().getString("leaderboard.weekly").c_str(), "weekly",
        "GJ_weeklyBtn_001.png", {150, 110, 255}, {centerX, topY});
    tabMenu->addChild(weeklyBtn);

    auto forYouBtn = createTab(Localization::get().getString("foryou.tab").c_str(), "foryou",
        "GJ_heartOn_001.png", {255, 105, 150}, {centerX + btnSpacing, topY});
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

// Difficulty value understood by GJDifficultySprite (7-10 = demon tiers, -1 = auto).
static int lbDifficultySpriteValue(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_autoLevel) return -1;
    if (level->m_demon) {
        switch (static_cast<int>(level->m_demonDifficulty)) {
            case 3: return 7;
            case 4: return 8;
            case 5: return 9;
            case 6: return 10;
            default: return 6;
        }
    }
    int diff = level->getAverageDifficulty();
    if (level->m_levelType == GJLevelType::Main) {
        diff = static_cast<int>(level->m_difficulty);
    }
    return diff;
}

// (Re)builds the difficulty + stars chip contents. Called on creation and again
// from updateLevelInfo once the real level data arrives from the server.
static void lbFillDiffChip(CCNode* chip, GJGameLevel* level) {
    if (!chip || !level) return;
    chip->removeAllChildren();

    CCSize cs = chip->getContentSize();
    if (auto* bg = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
        bg->setContentSize(cs);
        bg->setAnchorPoint({0.f, 0.f});
        bg->setColor({10, 11, 18});
        bg->setOpacity(215);
        chip->addChild(bg, 0);
    }
    if (auto* border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
        border->setContentSize(cs);
        border->setAnchorPoint({0.f, 0.f});
        border->setColor({255, 255, 255});
        border->setOpacity(70);
        chip->addChild(border, 1);
    }

    if (auto diffSpr = GJDifficultySprite::create(lbDifficultySpriteValue(level), GJDifficultyName::Short)) {
        diffSpr->setScale(0.62f);
        diffSpr->setPosition({22.f, cs.height / 2.f});
        chip->addChild(diffSpr, 2);
    }

    int stars = level->m_stars.value();
    if (stars > 0) {
        auto starLbl = CCLabelBMFont::create(fmt::format("{}", stars).c_str(), "bigFont.fnt");
        starLbl->setScale(0.42f);
        starLbl->setAnchorPoint({0.f, 0.5f});
        starLbl->setPosition({42.f, cs.height / 2.f});
        chip->addChild(starLbl, 2);

        if (auto* starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
            starIcon->setScale(0.55f);
            starIcon->setAnchorPoint({0.f, 0.5f});
            starIcon->setPosition({45.f + starLbl->getScaledContentSize().width, cs.height / 2.f});
            chip->addChild(starIcon, 2);
        }
    }
}

// Keeps very dark thumbnail-derived accents visible against the dark card.
static ccColor3B lbBrightenAccent(ccColor3B c) {
    int maxC = std::max({static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b)});
    if (maxC == 0) return {255, 195, 60};
    if (maxC < 110) {
        float f = 110.f / static_cast<float>(maxC);
        return {
            static_cast<GLubyte>(std::min(255.f, c.r * f)),
            static_cast<GLubyte>(std::min(255.f, c.g * f)),
            static_cast<GLubyte>(std::min(255.f, c.b * f)),
        };
    }
    return c;
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

    // Theme accent: real level colors when cached, otherwise gold for daily /
    // violet for weekly.
    ccColor3B accA, accB;
    if (auto colors = LevelColors::get().getPair(levelID); colors.has_value()) {
        accA = lbBrightenAccent(colors->a);
        accB = lbBrightenAccent(colors->b);
    } else if (isDaily) {
        accA = {255, 195, 60};
        accB = {255, 120, 40};
    } else {
        accA = {150, 110, 255};
        accB = {90, 70, 220};
    }

    // ============ HERO CARD ============
    // Full-bleed cinematic banner: the thumbnail IS the card, with gradients,
    // a pulsing accent glow, a shine sweep and staggered element entrances.
    float cardW = std::min(510.f, winSize.width - 56.f);
    float cardH = 234.f;
    float cardR = 14.f;
    float cardY = winSize.height / 2 - 14.f;

    auto card = CCNode::create();
    card->setContentSize({cardW, cardH});
    card->setAnchorPoint({0.5f, 0.5f});
    card->setPosition({winSize.width / 2, cardY});
    container->addChild(card, 5);

    // Pop-in animation
    card->setScale(0.85f);
    card->runAction(CCEaseBackOut::create(CCScaleTo::create(0.5f, 1.0f)));

    // Breathing outer glow, two layers of accent color.
    if (auto* glowOuter = paimon::SpriteHelper::createColorPanel(cardW + 44.f, cardH + 44.f, accB, 45, 22.f)) {
        glowOuter->setPosition({-22.f, -22.f});
        card->addChild(glowOuter, -3);
        glowOuter->runAction(CCRepeatForever::create(CCSequence::create(
            CCFadeTo::create(1.6f, 20), CCFadeTo::create(1.6f, 55), nullptr)));
    }
    if (auto* glowInner = paimon::SpriteHelper::createColorPanel(cardW + 18.f, cardH + 18.f, accA, 80, 16.f)) {
        glowInner->setPosition({-9.f, -9.f});
        card->addChild(glowInner, -2);
        glowInner->runAction(CCRepeatForever::create(CCSequence::create(
            CCFadeTo::create(1.1f, 45), CCFadeTo::create(1.1f, 95), nullptr)));
    }

    // Dark base under the thumbnail while it loads.
    if (auto* base = paimon::SpriteHelper::createColorPanel(cardW, cardH, {10, 11, 18}, 255, cardR)) {
        card->addChild(base, -1);
    }

    // Accent outline on top of everything inside the card (GD's GJ_square07).
    if (auto* border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
        border->setContentSize({cardW, cardH});
        border->setAnchorPoint({0.f, 0.f});
        border->setColor(accA);
        border->setOpacity(200);
        card->addChild(border, 6);
    }

    // ============ FULL-BLEED THUMBNAIL ============
    float thumbW = cardW;
    float thumbH = cardH;

    auto clipper = CCClippingNode::create();
    clipper->setContentSize({thumbW, thumbH});
    clipper->setAnchorPoint({0, 0});
    clipper->setPosition({0.f, 0.f});
    clipper->setStencil(paimon::SpriteHelper::createRoundedRectStencil(thumbW, thumbH, cardR));
    card->addChild(clipper, 1);

    auto thumbPlaceholder = CCLayerColor::create({14, 15, 24, 255});
    thumbPlaceholder->setContentSize({thumbW, thumbH});
    thumbPlaceholder->setTag(101);
    clipper->addChild(thumbPlaceholder, 0);

    // Cinematic gradients: heavy at the bottom (text zone), light at the top
    // (badge zone).
    auto bottomGrad = CCLayerGradient::create({5, 6, 11, 245}, {0, 0, 0, 0}, {0, 1});
    bottomGrad->setContentSize({thumbW, 128.f});
    bottomGrad->setPosition({0.f, 0.f});
    clipper->addChild(bottomGrad, 10);

    auto topGrad = CCLayerGradient::create({0, 0, 0, 0}, {8, 8, 14, 170}, {0, 1});
    topGrad->setContentSize({thumbW, 64.f});
    topGrad->setPosition({0.f, thumbH - 64.f});
    clipper->addChild(topGrad, 10);

    // Periodic shine sweep across the artwork.
    if (auto shine = CCSprite::create()) {
        shine->setTextureRect(CCRect(0, 0, 1, 1));
        shine->setScaleX(46.f);
        shine->setScaleY(cardH * 1.7f);
        shine->setRotation(18.f);
        shine->setOpacity(0);
        ccBlendFunc additive = {GL_SRC_ALPHA, GL_ONE};
        shine->setBlendFunc(additive);
        shine->setPosition({-70.f, cardH / 2.f});
        clipper->addChild(shine, 12);
        shine->runAction(CCRepeatForever::create(CCSequence::create(
            CCDelayTime::create(1.0f),
            CCPlace::create({-70.f, cardH / 2.f}),
            CCFadeTo::create(0.f, 55),
            CCEaseSineInOut::create(CCMoveTo::create(1.3f, {cardW + 70.f, cardH / 2.f})),
            CCFadeTo::create(0.f, 0),
            CCDelayTime::create(3.2f),
            nullptr)));
    }

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

    // ============ RIBBON BADGE (DAILY / WEEKLY) ============
    {
        float bW = 128.f;
        float bH = 32.f;
        float badgeX = 18.f;
        float badgeY = cardH - 18.f - bH;

        auto badge = CCNode::create();
        badge->setContentSize({bW, bH});
        badge->setRotation(-2.f);
        card->addChild(badge, 8);

        if (auto* glow = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
            glow->setContentSize({bW + 12.f, bH + 12.f});
            glow->setAnchorPoint({0.f, 0.f});
            glow->setPosition({-6.f, -6.f});
            glow->setColor(accA);
            glow->setOpacity(85);
            badge->addChild(glow, -1);
        }
        if (auto* bg = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
            bg->setContentSize({bW, bH});
            bg->setAnchorPoint({0.f, 0.f});
            bg->setColor({static_cast<GLubyte>(accA.r * 0.28f),
                          static_cast<GLubyte>(accA.g * 0.28f),
                          static_cast<GLubyte>(accA.b * 0.28f)});
            bg->setOpacity(245);
            badge->addChild(bg, 0);
        }
        if (auto* bgBorder = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
            bgBorder->setContentSize({bW, bH});
            bgBorder->setAnchorPoint({0.f, 0.f});
            bgBorder->setColor(accA);
            bgBorder->setOpacity(255);
            badge->addChild(bgBorder, 1);
        }

        float labelLeft = 14.f;
        auto badgeIcon = paimon::SpriteHelper::safeCreateWithFrameName(
            isDaily ? "GJ_dailyBtn_001.png" : "GJ_weeklyBtn_001.png");
        if (badgeIcon) {
            float maxSide = std::max(badgeIcon->getContentSize().width, badgeIcon->getContentSize().height);
            if (maxSide > 0.f) badgeIcon->setScale(34.f / maxSide);
            badgeIcon->setPosition({16.f, bH / 2.f + 2.f});
            badge->addChild(badgeIcon, 2);
            labelLeft = 34.f;
        }

        auto badgeLbl = CCLabelBMFont::create(isDaily ? "DAILY" : "WEEKLY", "bigFont.fnt");
        badgeLbl->setScale(0.48f);
        float maxLblW = bW - labelLeft - 10.f;
        if (badgeLbl->getScaledContentSize().width > maxLblW) {
            badgeLbl->setScale(badgeLbl->getScale() * (maxLblW / badgeLbl->getScaledContentSize().width));
        }
        badgeLbl->setAnchorPoint({0.f, 0.5f});
        badgeLbl->setPosition({labelLeft, bH / 2.f});
        badgeLbl->setColor(accA);
        badge->addChild(badgeLbl, 2);
        badgeLbl->setTag(TAG_BADGE_LABEL);

        // Drop-in bounce entrance.
        badge->setPosition({badgeX, badgeY + 46.f});
        badge->runAction(CCSequence::create(
            CCDelayTime::create(0.15f),
            CCEaseBounceOut::create(CCMoveTo::create(0.55f, {badgeX, badgeY})),
            nullptr));
    }

    // ============ DIFFICULTY + STARS CHIP ============
    {
        float chipW = 96.f;
        float chipH = 34.f;
        float chipX = cardW - chipW - 16.f;
        float chipY = cardH - chipH - 17.f;

        auto chip = CCNode::create();
        chip->setContentSize({chipW, chipH});
        chip->setTag(TAG_DIFF_SPRITE);
        card->addChild(chip, 8);
        lbFillDiffChip(chip, level);

        chip->setPosition({chipX + 40.f, chipY});
        chip->runAction(CCSequence::create(
            CCDelayTime::create(0.20f),
            CCEaseBackOut::create(CCMoveTo::create(0.45f, {chipX, chipY})),
            nullptr));
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

    // ============ TITLE BLOCK (NAME / ACCENT LINE / CREATOR) ============
    float textX = 22.f;
    float nameY = 96.f;
    float nameMaxW = cardW - 44.f;

    auto nameLbl = CCLabelBMFont::create(level->m_levelName.c_str(), "bigFont.fnt");
    nameLbl->setScale(0.82f);
    nameLbl->setColor({255, 255, 255});
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setTag(TAG_NAME_LABEL);
    if (nameLbl->getScaledContentSize().width > nameMaxW) {
        nameLbl->setScale(nameLbl->getScale() * (nameMaxW / nameLbl->getScaledContentSize().width));
    }
    card->addChild(nameLbl, 8);
    nameLbl->setOpacity(0);
    nameLbl->setPosition({textX - 26.f, nameY});
    nameLbl->runAction(CCSequence::create(
        CCDelayTime::create(0.10f),
        CCSpawn::create(
            CCFadeIn::create(0.35f),
            CCEaseSineOut::create(CCMoveTo::create(0.35f, {textX, nameY})),
            nullptr),
        nullptr));

    // Accent line growing between the name and the creator (1px GD sprite).
    if (auto line = CCSprite::create()) {
        line->setTextureRect(CCRect(0, 0, 1, 1));
        line->setColor(accA);
        line->setAnchorPoint({0.f, 0.5f});
        line->setPosition({textX + 1.f, 80.f});
        line->setScaleY(3.5f);
        line->setScaleX(0.f);
        card->addChild(line, 8);
        line->runAction(CCSequence::create(
            CCDelayTime::create(0.25f),
            CCEaseSineOut::create(CCScaleTo::create(0.4f, 56.f, 3.5f)),
            nullptr));
    }

    std::string creatorStr = level->m_creatorName.size() > 0
        ? "by " + std::string(level->m_creatorName) : "";
    auto creatorLbl = CCLabelBMFont::create(creatorStr.c_str(), "goldFont.fnt");
    creatorLbl->setScale(0.50f);
    creatorLbl->setAnchorPoint({0.f, 0.5f});
    creatorLbl->setTag(TAG_CREATOR_LABEL);
    if (creatorLbl->getScaledContentSize().width > nameMaxW) {
        creatorLbl->setScale(creatorLbl->getScale() * (nameMaxW / creatorLbl->getScaledContentSize().width));
    }
    card->addChild(creatorLbl, 8);
    creatorLbl->setOpacity(0);
    creatorLbl->setPosition({textX - 25.f, 66.f});
    creatorLbl->runAction(CCSequence::create(
        CCDelayTime::create(0.18f),
        CCSpawn::create(
            CCFadeIn::create(0.35f),
            CCEaseSineOut::create(CCMoveTo::create(0.35f, {textX + 1.f, 66.f})),
            nullptr),
        nullptr));

    // ============ COUNTDOWN PROGRESS BAR ============
    if (m_featuredExpiresAt > 0) {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        long long diff = m_featuredExpiresAt - now;

        if (diff > 0) {
            int hours = (int)(diff / (1000LL * 60 * 60));
            int mins = (int)((diff % (1000LL * 60 * 60)) / (1000LL * 60));

            long long totalMs = (isDaily ? 24LL : 24LL * 7) * 60LL * 60LL * 1000LL;
            float frac = static_cast<float>((double)diff / (double)totalMs);
            frac = std::max(0.02f, std::min(1.f, frac));
            bool urgent = diff < 3LL * 60 * 60 * 1000;
            ccColor3B barCol = urgent ? ccColor3B{255, 90, 90} : accA;

            float barX = 22.f;
            float barY = 24.f;
            float barW = cardW - 44.f - 168.f;
            float barH = 8.f;

            // Clock icon + label above the bar.
            float labelY = barY + 20.f;
            float labelX = barX;
            if (auto timeIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_timeIcon_001.png")) {
                timeIcon->setScale(0.45f);
                timeIcon->setColor({215, 220, 232});
                timeIcon->setAnchorPoint({0.f, 0.5f});
                timeIcon->setPosition({labelX, labelY});
                card->addChild(timeIcon, 8);
                labelX += 15.f;
            }
            auto timeLbl = CCLabelBMFont::create(
                fmt::format("Ends in {}h {}m", hours, mins).c_str(), "chatFont.fnt");
            timeLbl->setScale(0.55f);
            timeLbl->setColor(urgent ? ccColor3B{255, 130, 130} : ccColor3B{222, 226, 240});
            timeLbl->setAnchorPoint({0.f, 0.5f});
            timeLbl->setPosition({labelX, labelY});
            timeLbl->setTag(TAG_TIME_LABEL);
            card->addChild(timeLbl, 8);
            if (urgent) {
                timeLbl->runAction(CCRepeatForever::create(CCSequence::create(
                    CCFadeTo::create(0.5f, 140), CCFadeTo::create(0.5f, 255), nullptr)));
            }

            // Track + animated fill showing the remaining fraction (GD square02b).
            if (auto* track = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
                track->setContentSize({barW, barH});
                track->setAnchorPoint({0.f, 0.f});
                track->setColor({35, 38, 52});
                track->setOpacity(220);
                track->setPosition({barX, barY});
                card->addChild(track, 8);
            }
            float fillW = std::max(barH, barW * frac);
            if (auto* fill = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
                fill->setContentSize({fillW, barH});
                fill->setAnchorPoint({0.f, 0.f});
                fill->setColor(barCol);
                fill->setPosition({barX, barY});
                card->addChild(fill, 9);
                fill->setScaleX(0.f);
                fill->runAction(CCSequence::create(
                    CCDelayTime::create(0.35f),
                    CCEaseSineOut::create(CCScaleTo::create(0.7f, 1.f, 1.f)),
                    nullptr));
            }
        }
    }

    // ============ PLAY BUTTON + PULSING GLOW RING ============
    auto playMenu = CCMenu::create();
    playMenu->setPosition({0.f, 0.f});
    card->addChild(playMenu, 15);

    {
        CCPoint playPos = {cardW - 92.f, 46.f};

        auto playSpr = ButtonSprite::create("PLAY", 130, true, "bigFont.fnt", "GJ_button_01.png", 42.f, 0.9f);
        if (playSpr) {
            auto playBtnVis = CCMenuItemSpriteExtra::create(playSpr, self, menu_selector(LeaderboardLayer::onViewLevel));
            playBtnVis->setUserObject(level);
            playBtnVis->setPosition(playPos);
            playBtnVis->m_scaleMultiplier = 1.12f;
            PaimonButtonHighlighter::registerButton(playBtnVis);
            playMenu->addChild(playBtnVis);

            playBtnVis->setScale(0.f);
            playBtnVis->runAction(CCSequence::create(
                CCDelayTime::create(0.30f),
                CCEaseElasticOut::create(CCScaleTo::create(0.6f, 1.f), 0.7f),
                nullptr));

            // Subtle breathing pulse on the sprite (not the menu item, so the
            // click-scale animation stays intact).
            playSpr->runAction(CCRepeatForever::create(CCSequence::create(
                CCEaseSineInOut::create(CCScaleTo::create(0.9f, playSpr->getScale() * 1.05f)),
                CCEaseSineInOut::create(CCScaleTo::create(0.9f, playSpr->getScale())),
                nullptr)));
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

    // Same geometry as createList's hero card.
    auto winSize = CCDirector::get()->getWinSize();
    float cardW = std::min(510.f, winSize.width - 56.f);
    float nameMaxW = cardW - 44.f;

    if (auto nameLbl = typeinfo_cast<CCLabelBMFont*>(findByTag(findByTag, container, TAG_NAME_LABEL))) {
        nameLbl->setScale(0.82f);
        nameLbl->setString(m_featuredLevel->m_levelName.c_str());
        if (nameLbl->getScaledContentSize().width > nameMaxW) {
            nameLbl->setScale(nameLbl->getScale() * (nameMaxW / nameLbl->getScaledContentSize().width));
        }
    }

    if (auto creatorLbl = typeinfo_cast<CCLabelBMFont*>(findByTag(findByTag, container, TAG_CREATOR_LABEL))) {
        std::string creatorStr = m_featuredLevel->m_creatorName.size() > 0
            ? "by " + std::string(m_featuredLevel->m_creatorName)
            : "";
        creatorLbl->setScale(0.50f);
        creatorLbl->setString(creatorStr.c_str());
        if (creatorLbl->getScaledContentSize().width > nameMaxW) {
            creatorLbl->setScale(creatorLbl->getScale() * (nameMaxW / creatorLbl->getScaledContentSize().width));
        }
    }

    // Rebuild the difficulty/stars chip with the freshly downloaded data.
    if (auto chip = findByTag(findByTag, container, TAG_DIFF_SPRITE)) {
        lbFillDiffChip(chip, m_featuredLevel);
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
