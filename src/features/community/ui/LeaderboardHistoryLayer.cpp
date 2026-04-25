#include "LeaderboardHistoryLayer.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include <asp/time.hpp>
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../transitions/services/TransitionManager.hpp"
#include <Geode/binding/CreatorLayer.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>

using namespace geode::prelude;

LeaderboardHistoryLayer* LeaderboardHistoryLayer::create() {
    auto ret = new LeaderboardHistoryLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* LeaderboardHistoryLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(LeaderboardHistoryLayer::create());
    return scene;
}

bool LeaderboardHistoryLayer::init() {
    if (!CCLayer::init()) return false;
    log::info("[LeaderboardHistory] init");

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // fondo
    auto bg = CCLayerColor::create(ccc4(15, 12, 25, 255));
    bg->setContentSize(winSize);
    bg->setZOrder(-10);
    this->addChild(bg);

    // titulo
    auto title = CCLabelBMFont::create("Featured History", "bigFont.fnt");
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
        menu_selector(LeaderboardHistoryLayer::onBack)
    );
    backBtn->setPosition(25, winSize.height - 25);
    menu->addChild(backBtn);

    // tabs daily / weekly
    auto tabMenu = CCMenu::create();
    tabMenu->setPosition(0, 0);
    tabMenu->setZOrder(10);
    this->addChild(tabMenu);
    m_tabsMenu = tabMenu;

    auto createTab = [&](char const* text, char const* id, CCPoint pos) -> CCMenuItemToggler* {
        auto createBtn = [&](char const* frameName) -> CCNode* {
            auto sprite = cocos2d::extension::CCScale9Sprite::createWithSpriteFrameName(frameName);
            sprite->setContentSize({100.f, 28.f});
            auto label = CCLabelBMFont::create(text, "goldFont.fnt");
            label->setScale(0.5f);
            label->setPosition(sprite->getContentSize() / 2);
            sprite->addChild(label);
            return sprite;
        };

        auto onSprite = createBtn("GJ_longBtn01_001.png");
        auto offSprite = createBtn("GJ_longBtn02_001.png");

        auto tab = CCMenuItemToggler::create(offSprite, onSprite, this, menu_selector(LeaderboardHistoryLayer::onTab));
        tab->setUserObject(CCString::create(id));
        tab->setPosition(pos);
        m_tabs.push_back(tab);
        return tab;
    };

    float topY = winSize.height - 45.f;
    float centerX = winSize.width / 2;

    auto dailyBtn = createTab("Daily", "daily", {centerX - 55.f, topY});
    dailyBtn->toggle(true);
    tabMenu->addChild(dailyBtn);

    auto weeklyBtn = createTab("Weekly", "weekly", {centerX + 55.f, topY});
    tabMenu->addChild(weeklyBtn);

    // botones de paginacion
    m_pageMenu = CCMenu::create();
    m_pageMenu->setPosition(0, 0);
    m_pageMenu->setZOrder(15);
    this->addChild(m_pageMenu);

    auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    prevSpr->setScale(0.65f);
    m_prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(LeaderboardHistoryLayer::onPrevPage));
    m_prevBtn->setPosition({winSize.width - 35.f, winSize.height / 2});
    m_pageMenu->addChild(m_prevBtn);

    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    nextSpr->setFlipX(true);
    nextSpr->setScale(0.65f);
    m_nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(LeaderboardHistoryLayer::onNextPage));
    m_nextBtn->setPosition({35.f, winSize.height / 2});
    m_pageMenu->addChild(m_nextBtn);

    m_pageLbl = CCLabelBMFont::create("1 / 1", "chatFont.fnt");
    m_pageLbl->setScale(0.55f);
    m_pageLbl->setPosition({winSize.width / 2, 10.f});
    m_pageLbl->setOpacity(180);
    this->addChild(m_pageLbl, 15);

    // spinner
    m_loadingSpinner = PaimonLoadingOverlay::create("Loading...", 40.f);
    m_loadingSpinner->show(this, 100);

    this->setKeypadEnabled(true);
#if defined(GEODE_IS_WINDOWS)
    this->setMouseEnabled(true);
#endif
    
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, 0, false);

    // aplicar efecto cueva sobre musica de menu
    applyCaveEffect();
    this->scheduleUpdate();

    loadHistory("daily");
    return true;
}

void LeaderboardHistoryLayer::delaySilenceBg(float dt) {
    applyCaveEffect();
}

void LeaderboardHistoryLayer::onEnterTransitionDidFinish() {
    CCLayer::onEnterTransitionDidFinish();
    applyCaveEffect();
}

void LeaderboardHistoryLayer::onExitTransitionDidStart() {
    this->unscheduleUpdate();
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    CCLayer::onExitTransitionDidStart();
}

void LeaderboardHistoryLayer::update(float dt) {
    // Verificar que el efecto cueva sigue aplicado
    if (!m_caveApplied) {
        applyCaveEffect();
    }
}

void LeaderboardHistoryLayer::applyCaveEffect() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return;
    if (m_caveApplied) return;

    // Guardar volumen original y reducir al 55% para efecto de lejania
    engine->m_backgroundMusicChannel->getVolume(&m_savedBgVolume);
    float caveVol = engine->m_musicVolume * 0.55f;
    engine->m_backgroundMusicChannel->setVolume(caveVol);

    // Lowpass filter
    if (!m_lowpassDSP) {
        engine->m_system->createDSPByType(FMOD_DSP_TYPE_LOWPASS, &m_lowpassDSP);
        if (m_lowpassDSP) {
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_CUTOFF, 1200.f);
            m_lowpassDSP->setParameterFloat(FMOD_DSP_LOWPASS_RESONANCE, 2.0f);
        }
    }

    // Reverb sutil
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

void LeaderboardHistoryLayer::removeCaveEffect() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine && engine->m_backgroundMusicChannel) {
        if (m_lowpassDSP) engine->m_backgroundMusicChannel->removeDSP(m_lowpassDSP);
        if (m_reverbDSP) engine->m_backgroundMusicChannel->removeDSP(m_reverbDSP);
        engine->m_backgroundMusicChannel->setVolume(m_savedBgVolume);
    }
    if (m_lowpassDSP) { m_lowpassDSP->release(); m_lowpassDSP = nullptr; }
    if (m_reverbDSP) { m_reverbDSP->release(); m_reverbDSP = nullptr; }
    m_caveApplied = false;
}

bool LeaderboardHistoryLayer::ccMouseScroll(float x, float y) {
#if !defined(GEODE_IS_WINDOWS) && !defined(GEODE_IS_MACOS)
    // En no-Windows/macOS no hay mouse scroll, CCScrollView maneja touch nativo
    return false;
#else
    if (!m_scrollView) return false;

    CCPoint mousePos = geode::cocos::getMousePos();

    CCRect scrollRect = m_scrollView->boundingBox();
    scrollRect.origin = m_scrollView->getParent()->convertToWorldSpace(scrollRect.origin);
    
    if (!scrollRect.containsPoint(mousePos)) {
        return false;
    }

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

void LeaderboardHistoryLayer::onBack(CCObject*) {
    if (GameLevelManager::get()->m_levelManagerDelegate == this) {
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
    }
    removeCaveEffect();
    CCDirector::sharedDirector()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

void LeaderboardHistoryLayer::keyBackClicked() {
    onBack(nullptr);
}

void LeaderboardHistoryLayer::onTab(CCObject* sender) {
    auto toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;
    auto typeObj = typeinfo_cast<CCString*>(toggler->getUserObject());
    if (!typeObj) return;
    std::string type = typeObj->getCString();

    if (m_currentType == type) {
        toggler->toggle(true);
        return;
    }
    m_currentType = type;
    m_currentPage = 0;

    for (auto tab : m_tabs) {
        tab->toggle(tab == toggler);
    }

    if (m_listContainer) {
        m_listContainer->removeFromParent();
        m_listContainer = nullptr;
    }

    if (m_loadingSpinner) m_loadingSpinner->setVisible(true);

    loadHistory(type);
}

void LeaderboardHistoryLayer::onNextPage(CCObject*) {
    int totalPages = std::max(1, (m_totalServerItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
    if (m_currentPage < totalPages - 1) {
        m_currentPage++;
        if (m_listContainer) { m_listContainer->removeFromParent(); m_listContainer = nullptr; }
        if (m_loadingSpinner) m_loadingSpinner->setVisible(true);
        loadHistory(m_currentType);
    }
}

void LeaderboardHistoryLayer::onPrevPage(CCObject*) {
    if (m_currentPage > 0) {
        m_currentPage--;
        if (m_listContainer) { m_listContainer->removeFromParent(); m_listContainer = nullptr; }
        if (m_loadingSpinner) m_loadingSpinner->setVisible(true);
        loadHistory(m_currentType);
    }
}

void LeaderboardHistoryLayer::updatePageButtons() {
    int totalPages = std::max(1, (m_totalServerItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
    if (m_prevBtn) m_prevBtn->setVisible(m_currentPage > 0);
    if (m_nextBtn) m_nextBtn->setVisible(m_currentPage < totalPages - 1);
    if (m_pageLbl) {
        m_pageLbl->setString(fmt::format("{} / {}", m_currentPage + 1, totalPages).c_str());
    }
}

void LeaderboardHistoryLayer::loadHistory(std::string type) {
    m_entries.clear();

    int offset = m_currentPage * ITEMS_PER_PAGE;
    int limit = ITEMS_PER_PAGE;

    WeakRef<LeaderboardHistoryLayer> self = this;
    std::string url = fmt::format("/api/featured/history?type={}&offset={}&limit={}", type, offset, limit);
    HttpClient::get().get(url, [self, type](bool success, std::string const& json) {
        auto layer = self.lock();
        if (!layer) return;

        if (success) {
            auto dataRes = matjson::parse(json);
            if (dataRes.isOk()) {
                auto data = dataRes.unwrap();
                if (data["success"].asBool().unwrapOr(false)) {
                    auto items = data["items"];
                    if (items.isArray()) {
                        for (auto& item : items.asArray().unwrap()) {
                            HistoryEntry entry;
                            entry.levelID = item["levelID"].asInt().unwrapOr(0);
                            entry.setAt = (long long)item["setAt"].asDouble().unwrapOr(0);
                            entry.setBy = item["setBy"].asString().unwrapOr("");
                            
                            if (entry.levelID > 0) {
                                auto saved = GameLevelManager::get()->getSavedLevel(entry.levelID);
                                if (saved) {
                                    entry.levelName = saved->m_levelName;
                                    entry.creatorName = saved->m_creatorName;
                                } else {
                                    entry.levelName = fmt::format("Level {}", entry.levelID);
                                    entry.creatorName = "";
                                }
                                layer->m_entries.push_back(entry);
                            }
                        }
                    }
                    // total del servidor para paginacion
                    int total = data["total"].asInt().unwrapOr(0);
                    if (total > 0) {
                        layer->m_totalServerItems = total;
                    } else if (layer->m_totalServerItems == 0) {
                        layer->m_totalServerItems = (int)layer->m_entries.size();
                    }
                }
            }
        }

        if (layer->m_loadingSpinner) layer->m_loadingSpinner->setVisible(false);
        layer->createList();

        // resolver nombres desde GD server en batch
        if (!layer->m_entries.empty()) {
            std::string idList;
            for (auto& e : layer->m_entries) {
                if (e.creatorName.empty()) {
                    if (!idList.empty()) idList += ",";
                    idList += std::to_string(e.levelID);
                }
            }
            if (!idList.empty()) {
                auto searchObj = GJSearchObject::create(SearchType::MapPackOnClick, idList);
                auto glm = GameLevelManager::get();
                glm->m_levelManagerDelegate = layer.data();
                glm->getOnlineLevels(searchObj);
            }
        }
    });
}

void LeaderboardHistoryLayer::createList() {
    if (m_listContainer) {
        m_listContainer->removeFromParent();
        m_listContainer = nullptr;
    }
    m_scrollView = nullptr;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    m_listContainer = CCNode::create();
    m_listContainer->setTag(800);
    this->addChild(m_listContainer, 5);

    updatePageButtons();

    if (m_entries.empty()) {
        auto lbl = CCLabelBMFont::create("No history found", "chatFont.fnt");
        lbl->setScale(0.7f);
        lbl->setOpacity(150);
        lbl->setPosition(winSize / 2);
        m_listContainer->addChild(lbl);
        return;
    }

    // los entries ya son solo los de esta pagina (cargados con offset/limit)
    int pageCount = (int)m_entries.size();

    // area de scroll
    float listW = 380.f;
    float cellH = 55.f;
    float listH = winSize.height - 90.f;
    float totalH = std::max(listH, cellH * (float)pageCount);

    auto scrollView = geode::ScrollLayer::create({listW, listH});
    scrollView->setPosition({winSize.width / 2 - listW / 2, 20.f});

    auto content = CCLayer::create();
    content->setContentSize({listW, totalH});
    content->setPosition({0, 0});
    scrollView->m_contentLayer->addChild(content);
    scrollView->m_contentLayer->setContentSize({listW, totalH});

    m_listContainer->addChild(scrollView);
    m_scrollView = scrollView;

    LeaderboardHistoryLayer* self = this;

    for (int p = 0; p < pageCount; p++) {
        int i = p;
        int globalIdx = m_currentPage * ITEMS_PER_PAGE + p;
        auto& entry = m_entries[i];
        float y = totalH - (p + 0.5f) * cellH;

        auto cell = CCNode::create();
        cell->setContentSize({listW, cellH - 2.f});
        cell->setAnchorPoint({0.5f, 0.5f});
        cell->setPosition({listW / 2, y});
        content->addChild(cell);

        // fondo de celda alternado
        auto cellBg = paimon::SpriteHelper::createColorPanel(
            listW, cellH - 2.f,
            p % 2 == 0 ? ccColor3B{18, 18, 28} : ccColor3B{22, 22, 32}, 200);
        cellBg->setPosition({0, 0});
        cell->addChild(cellBg, 0);

        // miniatura — con clipping para que aspect-fill no se desborde
        float thumbSize = cellH - 8.f;
        float tw = thumbSize * 1.6f;
        float th = thumbSize;

        // fondo oscuro debajo del clipper
        auto thumbBg = CCLayerColor::create({30, 28, 40, 255});
        thumbBg->setContentSize({tw, th});
        thumbBg->setPosition({4.f, (cellH - 2.f - th) / 2});
        cell->addChild(thumbBg, 1);

        // clipper encima del fondo
        auto stencil = paimon::SpriteHelper::createRoundedRectStencil(tw, th, 4.f);
        auto clipper = CCClippingNode::create(stencil);
        clipper->setContentSize({tw, th});
        clipper->setAnchorPoint({0.f, 0.f});
        clipper->setPosition({4.f, (cellH - 2.f - th) / 2});
        cell->addChild(clipper, 2);

        int levelID = entry.levelID;
        auto localTex = LocalThumbs::get().loadTexture(levelID);
        if (localTex) {
            auto spr = CCSprite::createWithTexture(localTex);
            if (spr) {
                spr->setScale(std::max(tw / spr->getContentSize().width, th / spr->getContentSize().height));
                spr->setPosition({tw / 2, th / 2});
                clipper->addChild(spr);
            }
        } else if (levelID > 0) {
            std::string fileName = fmt::format("{}.png", levelID);
            Ref<CCClippingNode> safeClipper = clipper;
            ThumbnailLoader::get().requestLoad(levelID, fileName, [safeClipper, tw, th](CCTexture2D* tex, bool) {
                if (!safeClipper || !safeClipper->getParent() || !tex) return;

                auto spr = CCSprite::createWithTexture(tex);
                if (!spr) return;

                spr->setScale(std::max(tw / spr->getContentSize().width, th / spr->getContentSize().height));
                spr->setPosition({tw / 2, th / 2});
                safeClipper->addChild(spr);
            });
        }

        float textX = thumbSize * 1.6f + 12.f;

        // numero de orden
        auto numLbl = CCLabelBMFont::create(fmt::format("#{}", globalIdx + 1).c_str(), "chatFont.fnt");
        numLbl->setScale(0.4f);
        numLbl->setColor({255, 200, 50});
        numLbl->setAnchorPoint({0, 0.5f});
        numLbl->setPosition({textX, (cellH - 2.f) / 2 + 14.f});
        cell->addChild(numLbl, 10);

        // nombre del nivel
        auto nameLbl = CCLabelBMFont::create(entry.levelName.c_str(), "bigFont.fnt");
        nameLbl->setScale(0.38f);
        nameLbl->setAnchorPoint({0, 0.5f});
        nameLbl->setPosition({textX, (cellH - 2.f) / 2 + 1.f});
        float maxNameW = listW - textX - 100.f;
        if (nameLbl->getScaledContentSize().width > maxNameW) {
            nameLbl->setScale(nameLbl->getScale() * (maxNameW / nameLbl->getScaledContentSize().width));
        }
        cell->addChild(nameLbl, 10);

        // creador
        std::string creatorStr = entry.creatorName.empty() ? "" : "by " + entry.creatorName;
        auto creatorLbl = CCLabelBMFont::create(creatorStr.c_str(), "chatFont.fnt");
        creatorLbl->setScale(0.42f);
        creatorLbl->setColor({120, 200, 255});
        creatorLbl->setAnchorPoint({0, 0.5f});
        creatorLbl->setPosition({textX, (cellH - 2.f) / 2 - 12.f});
        cell->addChild(creatorLbl, 10);

        // fecha
        if (entry.setAt > 0) {
            time_t seconds = entry.setAt / 1000;
            auto tmBuf = asp::localtime(seconds);
            {
                char dateBuf[32];
                strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tmBuf);

                auto dateLbl = CCLabelBMFont::create(dateBuf, "chatFont.fnt");
            dateLbl->setScale(0.4f);
            dateLbl->setColor({180, 180, 180});
                dateLbl->setAnchorPoint({1.f, 0.5f});
                dateLbl->setPosition({listW - 8.f, (cellH - 2.f) / 2});
                cell->addChild(dateLbl, 10);
            }
        }

        // boton click
        auto cellMenu = CCMenu::create();
        cellMenu->setPosition({0, 0});
        cellMenu->setContentSize(cell->getContentSize());
        cell->addChild(cellMenu, 50);

        auto hitArea = CCSprite::create();
        if (hitArea) {
            hitArea->setTextureRect(CCRect(0, 0, 1, 1));
            hitArea->setScaleX(listW);
            hitArea->setScaleY(cellH - 2.f);
            hitArea->setOpacity(0);

            auto btn = CCMenuItemSpriteExtra::create(hitArea, self, menu_selector(LeaderboardHistoryLayer::onCellClick));
            btn->setPosition(cell->getContentSize() / 2);
            btn->setTag(levelID);
            cellMenu->addChild(btn);
        }
    }
}

void LeaderboardHistoryLayer::onCellClick(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;

    int levelID = btn->getTag();
    if (levelID <= 0) return;

    GJGameLevel* levelToUse = nullptr;
    auto saved = GameLevelManager::get()->getSavedLevel(levelID);
    if (saved) {
        levelToUse = saved;
    } else {
        auto level = GJGameLevel::create();
        level->m_levelID = levelID;
        for (auto& e : m_entries) {
            if (e.levelID == levelID) {
                level->m_levelName = e.levelName;
                level->m_creatorName = e.creatorName;
                break;
            }
        }
        levelToUse = level;
    }

    auto layer = LevelInfoLayer::create(levelToUse, false);
    auto infoScene = CCScene::create();
    infoScene->addChild(layer);
    TransitionManager::get().pushScene(infoScene);
}

void LeaderboardHistoryLayer::loadLevelsFinished(CCArray* levels, char const* key) {
    if (!levels) return;

    for (auto* level : CCArrayExt<GJGameLevel*>(levels)) {
        if (!level) continue;
        for (auto& entry : m_entries) {
            if (entry.levelID == level->m_levelID) {
                entry.levelName = level->m_levelName;
                entry.creatorName = level->m_creatorName;
            }
        }
    }

    createList();
}

void LeaderboardHistoryLayer::loadLevelsFailed(char const* key) {
}

void LeaderboardHistoryLayer::setupPageInfo(gd::string, char const*) {
}

LeaderboardHistoryLayer::~LeaderboardHistoryLayer() {
    removeCaveEffect();
    
    if (GameLevelManager::get()->m_levelManagerDelegate == this) {
        GameLevelManager::get()->m_levelManagerDelegate = nullptr;
    }
}
