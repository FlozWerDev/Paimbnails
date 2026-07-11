#include "UserThumbnailsLayer.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GameLevelManager.hpp>
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
    auto scene = CCScene::create();
    scene->addChild(UserThumbnailsLayer::create(username, accountID));
    return scene;
}

bool UserThumbnailsLayer::init(std::string const& username, int accountID) {
    if (!CCLayer::init()) return false;

    m_username = username;
    m_accountID = accountID;
    auto win = CCDirector::sharedDirector()->getWinSize();

    if (auto bg = CCSprite::create("GJ_gradientBG.png")) {
        bg->setAnchorPoint({0.5f, 0.5f});
        bg->setPosition(win / 2);
        auto bgSize = bg->getContentSize();
        bg->setScaleX(win.width / bgSize.width);
        bg->setScaleY(win.height / bgSize.height);
        bg->setColor({40, 125, 255});
        bg->setZOrder(-2);
        this->addChild(bg);
    }

    auto topMenu = CCMenu::create();
    topMenu->setPosition({0.f, 0.f});
    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this, menu_selector(UserThumbnailsLayer::onBack)
    );
    backBtn->setPosition({25.f, win.height - 25.f});
    topMenu->addChild(backBtn);
    this->addChild(topMenu);

    m_titleLabel = CCLabelBMFont::create(
        fmt::format("{}'s Thumbnails", username).c_str(), "bigFont.fnt"
    );
    m_titleLabel->setPosition({win.width / 2, win.height - 30.f});
    m_titleLabel->setScale(0.7f);
    this->addChild(m_titleLabel);

    m_loadingLabel = CCLabelBMFont::create("Loading...", "bigFont.fnt");
    m_loadingLabel->setPosition(win / 2);
    m_loadingLabel->setScale(0.5f);
    this->addChild(m_loadingLabel);

    m_errorLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_errorLabel->setPosition(win / 2);
    m_errorLabel->setScale(0.4f);
    m_errorLabel->setColor({255, 100, 100});
    m_errorLabel->setVisible(false);
    this->addChild(m_errorLabel);

    auto scrollSize = CCSize{win.width - 40.f, win.height - 100.f};
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
    if (m_requestAlive) m_requestAlive->store(false, std::memory_order_release);
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

        auto pushId = [&](int id) {
            if (id > 0) levelIds.push_back(id);
        };

        if (json.contains("levelIds") && json["levelIds"].isArray()) {
            if (auto arr = json["levelIds"].asArray(); arr.isOk()) {
                for (auto const& v : arr.unwrap())
                    pushId(v.asInt().unwrapOr(0));
            }
        } else if (json.contains("levels") && json["levels"].isArray()) {
            if (auto arr = json["levels"].asArray(); arr.isOk()) {
                for (auto const& level : arr.unwrap()) {
                    if (auto asInt = level.asInt(); asInt.isOk()) {
                        pushId(asInt.unwrap());
                    } else if (level.isObject() && level.contains("levelId")) {
                        pushId(level["levelId"].asInt().unwrapOr(0));
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
    m_levelListMenu->setPosition({0.f, 0.f});

    constexpr float itemH = 50.f;
    constexpr float gap = 10.f;
    float y = 0.f;
    float listW = m_scrollLayer->getContentSize().width;

    for (int levelId : levelIds) {
        auto levelBg = CCScale9Sprite::create("square02b_001.png");
        levelBg->setContentSize({listW - 20.f, itemH});
        levelBg->setOpacity(100);

        auto label = CCLabelBMFont::create(fmt::format("Level ID: {}", levelId).c_str(), "bigFont.fnt");
        label->setScale(0.5f);
        label->setPosition(levelBg->getContentSize() / 2);
        levelBg->addChild(label);

        auto btn = CCMenuItemSpriteExtra::create(
            levelBg, this, menu_selector(UserThumbnailsLayer::onLevelClicked)
        );
        btn->setTag(levelId);
        btn->setPosition({listW / 2, y - itemH / 2});
        m_levelListMenu->addChild(btn);
        y -= itemH + gap;
    }

    float contentH = levelIds.size() * (itemH + gap);
    m_levelListMenu->setContentSize({listW, contentH});
    m_scrollLayer->m_contentLayer->setContentSize({listW, contentH});
    m_scrollLayer->m_contentLayer->addChild(m_levelListMenu);
    m_scrollLayer->moveToTop();
}

void UserThumbnailsLayer::showError(std::string const& message) {
    if (!m_errorLabel) return;
    m_errorLabel->setString(message.c_str());
    m_errorLabel->setVisible(true);
}

void UserThumbnailsLayer::onLevelClicked(CCObject* sender) {
    int levelId = static_cast<CCMenuItemSpriteExtra*>(sender)->getTag();
    log::info("Level clicked: {}", levelId);

    if (auto glm = GameLevelManager::sharedState()) {
        glm->getOnlineLevels(GJSearchObject::create(SearchType::Search, std::to_string(levelId)));
    }
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
