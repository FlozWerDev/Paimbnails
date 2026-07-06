#include "UserThumbnailsLayer.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;

UserThumbnailsLayer* UserThumbnailsLayer::create(std::string const& username, int accountID) {
    auto ret = new UserThumbnailsLayer();
    if (ret && ret->init(username, accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* UserThumbnailsLayer::scene(std::string const& username, int accountID) {
    auto layer = UserThumbnailsLayer::create(username, accountID);
    auto scene = CCScene::create();
    scene->addChild(layer);
    return scene;
}

bool UserThumbnailsLayer::init(std::string const& username, int accountID) {
    if (!CCLayer::init()) return false;
    
    m_username = username;
    m_accountID = accountID;
    
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    
    auto bg = CCSprite::create("GJ_gradientBG.png");
    if (bg) {
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition(winSize / 2);
        auto bgSize = bg->getContentSize();
        bg->setScaleX(winSize.width / bgSize.width);
        bg->setScaleY(winSize.height / bgSize.height);
        bg->setColor({40, 125, 255});
        bg->setZOrder(-2);
        this->addChild(bg);
    }
    
    auto topMenu = CCMenu::create();
    topMenu->setPosition({0, 0});
    
    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this,
        menu_selector(UserThumbnailsLayer::onBack)
    );
    backBtn->setPosition({25.f, winSize.height - 25.f});
    topMenu->addChild(backBtn);
    
    this->addChild(topMenu);
    
    m_titleLabel = CCLabelBMFont::create(
        fmt::format("{}'s Thumbnails", username).c_str(),
        "bigFont.fnt"
    );
    m_titleLabel->setPosition({winSize.width / 2, winSize.height - 30.f});
    m_titleLabel->setScale(0.7f);
    this->addChild(m_titleLabel);
    
    m_loadingLabel = CCLabelBMFont::create("Loading...", "bigFont.fnt");
    m_loadingLabel->setPosition(winSize / 2);
    m_loadingLabel->setScale(0.5f);
    this->addChild(m_loadingLabel);
    
    // hidden until a load error occurs
    m_errorLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_errorLabel->setPosition(winSize / 2);
    m_errorLabel->setScale(0.4f);
    m_errorLabel->setColor({255, 100, 100});
    m_errorLabel->setVisible(false);
    this->addChild(m_errorLabel);
    
    auto scrollSize = CCSize{winSize.width - 40.f, winSize.height - 100.f};
    m_scrollLayer = geode::ScrollLayer::create(scrollSize);
    m_scrollLayer->setPosition({20.f, 50.f});
    m_scrollLayer->setVisible(false);
    this->addChild(m_scrollLayer);
    
    loadUserThumbnails();
    
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);
    
    return true;
}

void UserThumbnailsLayer::onExit() {
    if (m_requestAlive) {
        m_requestAlive->store(false, std::memory_order_release);
    }
    CCLayer::onExit();
}

void UserThumbnailsLayer::loadUserThumbnails() {
    m_requestAlive = std::make_shared<std::atomic<bool>>(true);
    auto alive = m_requestAlive;
    WeakRef<UserThumbnailsLayer> safeSelf = this;

    ThumbnailAPI::get().getUserUploads(m_username, [safeSelf, alive](bool success, std::string const& response) {
        if (!alive || !alive->load(std::memory_order_acquire)) return;
        if (paimon::isRuntimeShuttingDown()) return;

        auto selfRef = safeSelf.lock();
        auto* self = selfRef.data();
        if (!self || !self->getParent()) return;

        self->m_loadingLabel->setVisible(false);
        
        if (!success) {
            self->showError("Failed to load thumbnails");
            return;
        }

        auto parsed = matjson::parse(response);
        if (!parsed.isOk()) {
            self->showError("Failed to parse response");
            return;
        }
        
        auto json = parsed.unwrap();
        std::vector<int> levelIds;
        
        if (json.contains("levelIds") && json["levelIds"].isArray()) {
            auto arrResult = json["levelIds"].asArray();
            if (arrResult.isOk()) {
                for (auto const& levelIdVal : arrResult.unwrap()) {
                    int levelId = levelIdVal.asInt().unwrapOr(0);
                    if (levelId > 0) {
                        levelIds.push_back(levelId);
                    }
                }
            }
        }
        else if (json.contains("levels") && json["levels"].isArray()) {
            auto arrResult = json["levels"].asArray();
            if (arrResult.isOk()) {
                for (auto const& level : arrResult.unwrap()) {
                    int levelId = 0;
                    auto intResult = level.asInt();
                    if (intResult.isOk()) {
                        levelId = intResult.unwrap();
                    } else if (level.isObject() && level.contains("levelId")) {
                        levelId = level["levelId"].asInt().unwrapOr(0);
                    }
                    if (levelId > 0) {
                        levelIds.push_back(levelId);
                    }
                }
            }
        }
        
        if (levelIds.empty()) {
            self->showError("No thumbnails found");
            return;
        }

        self->displayLevels(levelIds);
    });
}

void UserThumbnailsLayer::displayLevels(std::vector<int> const& levelIds) {
    if (!m_scrollLayer) return;
    
    m_scrollLayer->setVisible(true);
    
    m_levelListMenu = CCMenu::create();
    m_levelListMenu->setPosition({0, 0});
    
    float yPos = 0.f;
    float itemHeight = 50.f;
    float itemSpacing = 10.f;
    
    for (size_t i = 0; i < levelIds.size(); ++i) {
        int levelId = levelIds[i];
        
        auto levelBg = CCScale9Sprite::create("square02b_001.png");
        levelBg->setContentSize({m_scrollLayer->getContentSize().width - 20.f, itemHeight});
        levelBg->setOpacity(100);
        
        auto levelLabel = CCLabelBMFont::create(
            fmt::format("Level ID: {}", levelId).c_str(),
            "bigFont.fnt"
        );
        levelLabel->setScale(0.5f);
        levelLabel->setPosition(levelBg->getContentSize() / 2);
        levelBg->addChild(levelLabel);
        
        auto levelBtn = CCMenuItemSpriteExtra::create(
            levelBg,
            this,
            menu_selector(UserThumbnailsLayer::onLevelClicked)
        );
        levelBtn->setTag(levelId);
        levelBtn->setPosition({
            m_scrollLayer->getContentSize().width / 2,
            yPos - itemHeight / 2
        });
        
        m_levelListMenu->addChild(levelBtn);
        yPos -= (itemHeight + itemSpacing);
    }
    
    float contentHeight = levelIds.size() * (itemHeight + itemSpacing);
    m_levelListMenu->setContentSize({m_scrollLayer->getContentSize().width, contentHeight});
    m_scrollLayer->m_contentLayer->setContentSize({m_scrollLayer->getContentSize().width, contentHeight});
    m_scrollLayer->m_contentLayer->addChild(m_levelListMenu);
    m_scrollLayer->moveToTop();
}

void UserThumbnailsLayer::showError(std::string const& message) {
    if (m_errorLabel) {
        m_errorLabel->setString(message.c_str());
        m_errorLabel->setVisible(true);
    }
}

void UserThumbnailsLayer::onLevelClicked(CCObject* sender) {
    auto btn = static_cast<CCMenuItemSpriteExtra*>(sender);
    int levelId = btn->getTag();
    
    log::info("Level clicked: {}", levelId);
    
    auto glm = GameLevelManager::sharedState();
    if (!glm) return;
    
    glm->getOnlineLevels(GJSearchObject::create(SearchType::Search, std::to_string(levelId)));
    
    PaimonNotify::create(
        fmt::format("Opening level {}...", levelId).c_str(),
        NotificationIcon::Info
    )->show();
}

void UserThumbnailsLayer::onBack(CCObject*) {
    keyBackClicked();
}

void UserThumbnailsLayer::keyBackClicked() {
    CCDirector::sharedDirector()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}
