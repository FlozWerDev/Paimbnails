#include "PaimonSupportLayer.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GameManager.hpp>
#include "../managers/ThumbnailAPI.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/PaimonShaderSprite.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../core/QualityConfig.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../utils/ThreadTracker.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <random>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

PaimonSupportLayer* PaimonSupportLayer::create() {
    auto ret = new PaimonSupportLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaimonSupportLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(PaimonSupportLayer::create());
    return scene;
}

bool PaimonSupportLayer::init() {
    if (!CCLayer::init()) return false;

    this->setKeypadEnabled(true);
    this->setID("PaimonSupportLayer");

    createBackground();
    createTitle();
    createBadgePanel();
    createBenefitsPanel();
    createThankYouSection();
    createButtons();

    return true;
}

void PaimonSupportLayer::createBackground() {
    auto winSize = CCDirector::get()->getWinSize();

    auto bg = CCLayerColor::create(ccc4(15, 10, 30, 255));
    bg->setContentSize(winSize);
    bg->setID("base-background");
    this->addChild(bg, -5);

    auto overlay = CCLayerColor::create({0, 0, 0, 100});
    overlay->setContentSize(winSize);
    overlay->setID("dark-overlay");
    this->addChild(overlay, -2);

    auto gradient = CCLayerGradient::create(
        ccc4(30, 15, 50, 90),
        ccc4(5, 5, 15, 120)
    );
    gradient->setContentSize(winSize);
    gradient->setVector({0, -1});
    gradient->setID("gradient-overlay");
    this->addChild(gradient, -1);

    auto bottomLeft = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    if (bottomLeft) {
        bottomLeft->setAnchorPoint({0, 0});
        bottomLeft->setPosition({-2, -2});
        bottomLeft->setOpacity(60);
        bottomLeft->setID("bottom-left-sideart");
        this->addChild(bottomLeft, 0);
    }
    auto bottomRight = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    if (bottomRight) {
        bottomRight->setAnchorPoint({1, 0});
        bottomRight->setPosition({winSize.width + 2, -2});
        bottomRight->setFlipX(true);
        bottomRight->setOpacity(60);
        bottomRight->setID("bottom-right-sideart");
        this->addChild(bottomRight, 0);
    }

    auto glow = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (glow) {
        glow->setScale(6.f);
        glow->setPosition(winSize / 2);
        glow->setColor({80, 40, 120});
        glow->setOpacity(40);
        glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        glow->setID("ambient-glow");
        this->addChild(glow, -4);

        glow->runAction(CCRepeatForever::create(CCRotateBy::create(20.f, 360.f)));
        auto pulse = CCSequence::create(
            CCFadeTo::create(4.0f, 60),
            CCFadeTo::create(4.0f, 20),
            nullptr
        );
        glow->runAction(CCRepeatForever::create(pulse));
        m_bgDiagonalGlow = glow;
    }

    loadShowcaseThumbnails();
}

void PaimonSupportLayer::loadShowcaseThumbnails() {
    auto cachePath = paimon::quality::cacheDir();
    
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec)) return;

    for (auto const& entry : std::filesystem::directory_iterator(cachePath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto ext = geode::utils::string::pathToString(entry.path().extension());
        // static formats only (no animated gifs for the background)
        if (ext != ".png" && ext != ".webp" && ext != ".jpg" && ext != ".jpeg" &&
            ext != ".qoi" && ext != ".jxl") continue;
        // skip tiny files (< 5kb, likely corrupt)
        std::error_code sizeEc;
        auto fsize = entry.file_size(sizeEc);
        if (sizeEc || fsize < 5000) continue;
        m_cachedThumbPaths.push_back(geode::utils::string::pathToString(entry.path()));
    }

    if (m_cachedThumbPaths.empty()) return;

    {
        static std::mt19937 rng(std::random_device{}());
        std::shuffle(m_cachedThumbPaths.begin(), m_cachedThumbPaths.end(), rng);
    }

    if (m_cachedThumbPaths.size() > 20) m_cachedThumbPaths.resize(20);

    m_currentThumbIndex = 0;

    cycleThumbnail(0.f);

    // unschedule first to avoid stacking
    this->unschedule(schedule_selector(PaimonSupportLayer::cycleThumbnail));
    this->schedule(schedule_selector(PaimonSupportLayer::cycleThumbnail), 5.0f);
}

void PaimonSupportLayer::onExit() {
    m_alive.store(false, std::memory_order_release);
    m_loadingThumb.store(false, std::memory_order_release);
    this->unschedule(schedule_selector(PaimonSupportLayer::cycleThumbnail));
    this->unschedule(schedule_selector(PaimonSupportLayer::spawnParticles));
    CCLayer::onExit();
}

void PaimonSupportLayer::cycleThumbnail(float dt) {
    if (!m_alive.load(std::memory_order_acquire)) return;
    if (m_cachedThumbPaths.empty() || m_loadingThumb) return;
    if (paimon::isRuntimeShuttingDown()) return;

    m_loadingThumb = true;
    auto filePath = m_cachedThumbPaths[m_currentThumbIndex % m_cachedThumbPaths.size()];
    m_currentThumbIndex++;

    // load off-thread to avoid blocking the UI
    WeakRef<PaimonSupportLayer> safeSelf = this;

    paimon::ThreadTracker::get().spawn([safeSelf, filePath]() {
        geode::utils::thread::setName("SupportLayer BG Loader");
        if (paimon::isRuntimeShuttingDown()) return;

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            if (paimon::isRuntimeShuttingDown()) return;

            Loader::get()->queueInMainThread([safeSelf]() {
                if (paimon::isRuntimeShuttingDown()) return;

                auto selfRef = safeSelf.lock();
                if (!selfRef) return;
                auto* self = geode::cast::typeinfo_cast<PaimonSupportLayer*>(selfRef.data());
                if (!self) return;
                self->m_loadingThumb = false;
            });
            return;
        }

        auto size = file.tellg();
        if (size <= 0) {
            if (paimon::isRuntimeShuttingDown()) return;

            Loader::get()->queueInMainThread([safeSelf]() {
                if (paimon::isRuntimeShuttingDown()) return;

                auto selfRef = safeSelf.lock();
                if (!selfRef) return;
                auto* self = geode::cast::typeinfo_cast<PaimonSupportLayer*>(selfRef.data());
                if (!self) return;
                self->m_loadingThumb = false;
            });
            return;
        }
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), size);
        file.close();

        if (paimon::isRuntimeShuttingDown()) return;

        Loader::get()->queueInMainThread([safeSelf, data = std::move(data)]() {
            if (paimon::isRuntimeShuttingDown()) return;

            auto selfRef = safeSelf.lock();
            if (!selfRef) return;
            auto* self = geode::cast::typeinfo_cast<PaimonSupportLayer*>(selfRef.data());
            if (!self || !self->m_alive.load(std::memory_order_acquire)) return;
            if (!self->getParent()) {
                self->m_loadingThumb.store(false, std::memory_order_release);
                return;
            }
            auto image = new CCImage();
            if (image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
                auto tex = new CCTexture2D();
                if (tex->initWithImage(image)) {
                    image->release();
                    tex->autorelease();
                    self->applyThumbnailBackground(tex);
                    self->m_loadingThumb = false;
                    return;
                }
                tex->release();
            }
            image->release();
            self->m_loadingThumb = false;
        });
    });
}

void PaimonSupportLayer::applyThumbnailBackground(CCTexture2D* texture) {
    if (!texture) return;
    if (!m_alive.load(std::memory_order_acquire) || !getParent()) return;
    if (paimon::isRuntimeShuttingDown()) return;

    auto* director = CCDirector::get();
    if (!director) return;
    auto winSize = director->getWinSize();

    auto blurred = BlurSystem::getInstance()->createBlurredSprite(texture, winSize, 0.10f);
    if (!blurred) return;

    // texture comes from a RenderTexture, so it needs flipY
    auto newBg = CCSprite::createWithTexture(blurred->getTexture());
    if (!newBg) return;

    newBg->setFlipY(true);
    newBg->setPosition(winSize / 2);

    auto texSize = newBg->getContentSize();
    float scaleX = winSize.width / texSize.width;
    float scaleY = winSize.height / texSize.height;
    float scale = std::max(scaleX, scaleY);
    newBg->setScale(scale);

    newBg->setOpacity(0);
    newBg->setColor({170, 160, 210});
    this->addChild(newBg, -3);

    // crossfade new in, old out
    float fadeDuration = 1.2f;
    newBg->runAction(CCFadeTo::create(fadeDuration, 200));

    auto breatheIn = CCFadeTo::create(2.0f, 220);
    auto breatheOut = CCFadeTo::create(2.0f, 160);
    auto breathe = CCSequence::create(breatheIn, breatheOut, nullptr);
    newBg->runAction(CCRepeatForever::create(breathe));

    if (m_bgThumb) {
        auto oldBg = m_bgThumb;
        oldBg->stopAllActions();
        oldBg->runAction(CCSequence::create(
            CCFadeTo::create(fadeDuration, 0),
            CCCallFunc::create(oldBg, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }

    m_bgThumb = newBg;
}

void PaimonSupportLayer::createTitle() {
    auto winSize = CCDirector::get()->getWinSize();
    float topY = winSize.height - 24.f;

    auto titleContainer = CCNode::create();
    titleContainer->setPosition({0, 60.f}); // starts above the screen, drops into place
    titleContainer->setID("title-container");
    this->addChild(titleContainer, 2);

    auto starL = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (starL) {
        starL->setScale(0.4f);
        starL->setPosition({winSize.width / 2 - 120.f, topY});
        starL->setColor({255, 215, 0});
        starL->setID("left-star");
        titleContainer->addChild(starL, 2);

        starL->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, -180.f)));
    }

    auto title = CCLabelBMFont::create("Support Paimbnails", "goldFont.fnt");
    title->setPosition({winSize.width / 2, topY});
    title->setScale(0.85f);
    title->setID("main-title");
    titleContainer->addChild(title, 2);

    auto starR = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (starR) {
        starR->setScale(0.4f);
        starR->setPosition({winSize.width / 2 + 120.f, topY});
        starR->setColor({255, 215, 0});
        starR->setID("right-star");
        titleContainer->addChild(starR, 2);

        starR->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, 180.f)));
    }

    auto subtitle = CCLabelBMFont::create("Help keep the mod alive and growing!", "chatFont.fnt");
    subtitle->setPosition({winSize.width / 2, topY - 20.f});
    subtitle->setScale(0.55f);
    subtitle->setColor({200, 180, 255});
    subtitle->setOpacity(0); // fades in
    subtitle->setID("subtitle");
    titleContainer->addChild(subtitle, 2);

    titleContainer->runAction(CCEaseBackOut::create(CCMoveTo::create(0.8f, {0, 0})));

    subtitle->runAction(CCSequence::create(
        CCDelayTime::create(0.6f),
        CCFadeTo::create(0.4f, 255),
        nullptr
    ));
}

void PaimonSupportLayer::createBadgePanel() {
    auto winSize = CCDirector::get()->getWinSize();

    float panelW = 150.f;
    float panelH = 150.f;
    float panelX = winSize.width * 0.22f;
    float panelY = winSize.height * 0.52f;

    m_badgePanelContainer = CCNode::create();
    m_badgePanelContainer->setPosition({-panelX - panelW, 0}); // starts off-screen left
    m_badgePanelContainer->setID("badge-panel-container");
    this->addChild(m_badgePanelContainer, 3);

    auto panelBg = paimon::SpriteHelper::createColorPanel(panelW, panelH, {15, 10, 32}, 205);
    panelBg->setPosition({panelX - panelW / 2, panelY - panelH / 2});
    panelBg->setID("panel-bg");
    m_badgePanelContainer->addChild(panelBg, 1);

    auto neonGlow = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (neonGlow) {
        neonGlow->setContentSize({panelW + 8.f, panelH + 8.f});
        neonGlow->setPosition({panelX, panelY});
        neonGlow->setColor({255, 215, 0});
        neonGlow->setOpacity(80);
        neonGlow->setID("neon-glow");
        m_badgePanelContainer->addChild(neonGlow, 2);

        auto pulse = CCSequence::create(
            CCSpawn::create(
                CCScaleTo::create(1.5f, 1.03f),
                CCFadeTo::create(1.5f, 160),
                nullptr
            ),
            CCSpawn::create(
                CCScaleTo::create(1.5f, 0.97f),
                CCFadeTo::create(1.5f, 60),
                nullptr
            ),
            nullptr
        );
        neonGlow->runAction(CCRepeatForever::create(pulse));
    }


    auto border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (border) {
        border->setContentSize({panelW + 6.f, panelH + 6.f});
        border->setPosition({panelX, panelY});
        border->setColor({255, 205, 50});
        border->setID("border");
        m_badgePanelContainer->addChild(border, 3);
    }

    auto badgeTitle = CCLabelBMFont::create("Supporter Badge", "goldFont.fnt");
    badgeTitle->setScale(0.35f);
    badgeTitle->setPosition({panelX, panelY + panelH / 2 - 14.f});
    badgeTitle->setID("badge-title");
    m_badgePanelContainer->addChild(badgeTitle, 4);

    auto badgeGroup = CCNode::create();
    badgeGroup->setPosition({panelX, panelY + 10.f});
    badgeGroup->setID("badge-group");
    m_badgePanelContainer->addChild(badgeGroup, 4);

    auto crownIcon = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (crownIcon) {
        crownIcon->setScale(0.7f);
        crownIcon->setColor({255, 215, 0});
        crownIcon->setID("crown-icon");
        badgeGroup->addChild(crownIcon, 3);

        auto innerPulse = CCSequence::create(
            CCScaleTo::create(1.0f, 0.74f),
            CCScaleTo::create(1.0f, 0.66f),
            nullptr
        );
        crownIcon->runAction(CCRepeatForever::create(innerPulse));

        auto iconGlow = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
        if (iconGlow) {
            iconGlow->setScale(0.95f);
            iconGlow->setColor({255, 185, 0});
            iconGlow->setOpacity(70);
            iconGlow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            iconGlow->setID("icon-glow");
            badgeGroup->addChild(iconGlow, 2);

            auto glowPulse = CCSequence::create(
                CCFadeTo::create(1.2f, 110),
                CCFadeTo::create(1.2f, 40),
                nullptr
            );
            iconGlow->runAction(CCRepeatForever::create(glowPulse));
            iconGlow->runAction(CCRepeatForever::create(CCRotateBy::create(4.f, -120.f)));
        }
    }

    auto orbitNode = CCNode::create();
    orbitNode->setID("orbit-node");
    badgeGroup->addChild(orbitNode, 4);

    orbitNode->runAction(CCRepeatForever::create(CCRotateBy::create(5.0f, 360.f)));

    float orbitRadius = 35.f;
    for (int i = 0; i < 4; i++) {
        float angle = i * (static_cast<float>(M_PI) / 2.f);
        auto oStar = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
        if (oStar) {
            oStar->setScale(0.12f);
            oStar->setColor({255, 235, 100});
            oStar->setPosition({cosf(angle) * orbitRadius, sinf(angle) * orbitRadius});
            oStar->setID(fmt::format("orbit-star-{}", i));
            orbitNode->addChild(oStar);

            oStar->runAction(CCRepeatForever::create(CCRotateBy::create(1.5f, -360.f)));
        }
    }

    auto floatAction = CCSequence::create(
        CCMoveBy::create(1.8f, {0, 4.f}),
        CCMoveBy::create(1.8f, {0, -4.f}),
        nullptr
    );
    badgeGroup->runAction(CCRepeatForever::create(floatAction));

    auto exclusiveLbl = CCLabelBMFont::create("Exclusive", "bigFont.fnt");
    exclusiveLbl->setScale(0.3f);
    exclusiveLbl->setColor({255, 205, 100});
    exclusiveLbl->setPosition({panelX, panelY - 30.f});
    exclusiveLbl->setID("exclusive-label");
    m_badgePanelContainer->addChild(exclusiveLbl, 4);

    auto textPulse = CCSequence::create(
        CCScaleTo::create(1.2f, 0.315f),
        CCScaleTo::create(1.2f, 0.285f),
        nullptr
    );
    exclusiveLbl->runAction(CCRepeatForever::create(textPulse));

    auto badgeDesc = CCLabelBMFont::create("Shown on your profile", "chatFont.fnt");
    badgeDesc->setScale(0.35f);
    badgeDesc->setColor({180, 160, 220});
    badgeDesc->setPosition({panelX, panelY - 48.f});
    badgeDesc->setID("badge-description");
    m_badgePanelContainer->addChild(badgeDesc, 4);

    m_badgePanelContainer->runAction(CCEaseBackOut::create(CCMoveTo::create(1.0f, {0, 0})));
}

void PaimonSupportLayer::createBenefitsPanel() {
    auto winSize = CCDirector::get()->getWinSize();

    float panelW = 220.f;
    float panelH = 150.f;
    float panelX = winSize.width * 0.68f;
    float panelY = winSize.height * 0.52f;

    m_benefitsPanelContainer = CCNode::create();
    m_benefitsPanelContainer->setPosition({winSize.width - panelX + panelW, 0}); // starts off-screen right
    m_benefitsPanelContainer->setID("benefits-panel-container");
    this->addChild(m_benefitsPanelContainer, 3);

    auto panelBg = paimon::SpriteHelper::createColorPanel(panelW, panelH, {15, 10, 32}, 205);
    panelBg->setPosition({panelX - panelW / 2, panelY - panelH / 2});
    panelBg->setID("panel-bg");
    m_benefitsPanelContainer->addChild(panelBg, 1);

    auto neonGlow = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (neonGlow) {
        neonGlow->setContentSize({panelW + 8.f, panelH + 8.f});
        neonGlow->setPosition({panelX, panelY});
        neonGlow->setColor({255, 110, 180});
        neonGlow->setOpacity(80);
        neonGlow->setID("neon-glow");
        m_benefitsPanelContainer->addChild(neonGlow, 2);

        auto pulse = CCSequence::create(
            CCSpawn::create(
                CCScaleTo::create(1.5f, 1.03f),
                CCFadeTo::create(1.5f, 160),
                nullptr
            ),
            CCSpawn::create(
                CCScaleTo::create(1.5f, 0.97f),
                CCFadeTo::create(1.5f, 60),
                nullptr
            ),
            nullptr
        );
        neonGlow->runAction(CCRepeatForever::create(pulse));
    }


    auto border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (border) {
        border->setContentSize({panelW + 6.f, panelH + 6.f});
        border->setPosition({panelX, panelY});
        border->setColor({255, 120, 180});
        border->setID("border");
        m_benefitsPanelContainer->addChild(border, 3);
    }

    auto benefitsTitle = CCLabelBMFont::create("Supporter Benefits", "goldFont.fnt");
    benefitsTitle->setScale(0.38f);
    benefitsTitle->setPosition({panelX, panelY + panelH / 2 - 14.f});
    benefitsTitle->setID("benefits-title");
    m_benefitsPanelContainer->addChild(benefitsTitle, 4);

    struct Benefit {
        char const* icon;
        char const* text;
        ccColor3B color;
    };

    std::vector<Benefit> benefits = {
        {"GJ_bigStar_001.png",       "Exclusive Supporter Badge",       {255, 215, 0}},
        {"GJ_completesIcon_001.png",  "Priority for Your Ideas",        {100, 255, 100}},
        {"GJ_starsIcon_001.png",      "Your Name on the VIP List",      {255, 180, 100}},
        {"GJ_sMagicIcon_001.png",     "Use GIFs for Profile & More",    {100, 200, 255}},
        {"GJ_lock_001.png",           "Greater Customization Options",  {200, 150, 255}},
        {"gj_heartOn_001.png",        "Early Access Before Public",     {255, 100, 150}},
    };

    float startY = panelY + panelH / 2 - 32.f;
    float rowH = 19.f;
    float leftX = panelX - panelW / 2 + 18.f;

    for (size_t i = 0; i < benefits.size(); i++) {
        float rowY = startY - (float)i * rowH;

        auto rowNode = CCNode::create();
        rowNode->setPosition({15.f, 0}); // starts shifted right, slides into place
        rowNode->setID(fmt::format("benefit-row-{}", i));
        m_benefitsPanelContainer->addChild(rowNode, 4);

        auto icon = CCSprite::createWithSpriteFrameName(benefits[i].icon);
        if (icon) {
            icon->setScale(0.32f);
            icon->setPosition({leftX, rowY});
            icon->setColor(benefits[i].color);
            icon->setID("icon");
            rowNode->addChild(icon, 3);

            if (i == 0) {
                icon->runAction(CCRepeatForever::create(CCRotateBy::create(2.5f, 360.f)));
            }
            else if (i == 1) {
                auto chkPulse = CCSequence::create(
                    CCScaleTo::create(0.8f, 0.36f),
                    CCScaleTo::create(0.8f, 0.28f),
                    nullptr
                );
                icon->runAction(CCRepeatForever::create(chkPulse));
            }
            else if (i == 2) {
                auto glowPulse = CCSequence::create(
                    CCFadeTo::create(0.9f, 255),
                    CCFadeTo::create(0.9f, 100),
                    nullptr
                );
                icon->runAction(CCRepeatForever::create(glowPulse));
            }
            else if (i == 3) {
                auto wiggle = CCSequence::create(
                    CCRotateTo::create(0.12f, 15.f),
                    CCRotateTo::create(0.24f, -15.f),
                    CCRotateTo::create(0.12f, 0.f),
                    CCDelayTime::create(1.5f),
                    nullptr
                );
                icon->runAction(CCRepeatForever::create(wiggle));
            }
            else if (i == 4) {
                auto bob = CCSequence::create(
                    CCMoveBy::create(0.8f, {0, 2.5f}),
                    CCMoveBy::create(0.8f, {0, -2.5f}),
                    nullptr
                );
                icon->runAction(CCRepeatForever::create(bob));
            }
            else if (i == 5) {
                auto beat = CCSequence::create(
                    CCScaleTo::create(0.15f, 0.42f),
                    CCScaleTo::create(0.15f, 0.32f),
                    CCScaleTo::create(0.15f, 0.39f),
                    CCScaleTo::create(0.55f, 0.32f),
                    nullptr
                );
                icon->runAction(CCRepeatForever::create(beat));
            }
        }

        auto lbl = CCLabelBMFont::create(benefits[i].text, "chatFont.fnt");
        lbl->setScale(0.42f);
        lbl->setAnchorPoint({0, 0.5f});
        lbl->setPosition({leftX + 14.f, rowY});
        lbl->setColor({220, 220, 240});
        lbl->setID("label");
        rowNode->addChild(lbl, 3);

        // staggered: each row slides/fades in with a progressive delay
        rowNode->setVisible(false);
        rowNode->runAction(CCSequence::create(
            CCDelayTime::create(0.4f + static_cast<float>(i) * 0.12f),
            CCShow::create(),
            CCSpawn::create(
                CCEaseBackOut::create(CCMoveTo::create(0.45f, {0, 0})),
                nullptr
            ),
            nullptr
        ));
    }

    m_benefitsPanelContainer->runAction(CCEaseBackOut::create(CCMoveTo::create(1.0f, {0, 0})));
}

void PaimonSupportLayer::createThankYouSection() {
    auto winSize = CCDirector::get()->getWinSize();
    float sectionY = winSize.height * 0.20f;

    auto thankYouContainer = CCNode::create();
    thankYouContainer->setID("thank-you-container");
    this->addChild(thankYouContainer, 2);

    auto separator = CCLayerColor::create({255, 120, 180, 45});
    separator->setContentSize({winSize.width * 0.6f, 1.5f});
    separator->setPosition({winSize.width * 0.2f, sectionY + 22.f});
    separator->setScaleX(0); // expands horizontally on entrance
    separator->setID("separator");
    thankYouContainer->addChild(separator, 2);

    auto msg = CCLabelBMFont::create(
        "Every donation helps me dedicate more time\nto improving Paimbnails for the community.",
        "chatFont.fnt"
    );
    msg->setScale(0.48f);
    msg->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
    msg->setPosition({winSize.width / 2, sectionY - 8.f}); // starts slightly lower, slides up
    msg->setColor({200, 190, 230});
    msg->setOpacity(0); // fades in
    msg->setID("thank-you-message");
    thankYouContainer->addChild(msg, 2);

    auto heart = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
    if (heart) {
        heart->setScale(0.4f);
        heart->setPosition({winSize.width / 2, sectionY + 30.f});
        heart->setColor({255, 80, 120});
        heart->setOpacity(0); // fades in
        heart->setID("heart-icon");
        thankYouContainer->addChild(heart, 3);

        auto beat = CCSequence::create(
            CCScaleTo::create(0.18f, 0.52f),
            CCScaleTo::create(0.18f, 0.38f),
            CCScaleTo::create(0.18f, 0.48f),
            CCScaleTo::create(0.68f, 0.40f),
            nullptr
        );
        heart->runAction(CCRepeatForever::create(beat));
        
        auto heartGlow = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
        if (heartGlow) {
            heartGlow->setScale(0.45f);
            heartGlow->setPosition({winSize.width / 2, sectionY + 30.f});
            heartGlow->setColor({255, 50, 100});
            heartGlow->setOpacity(0);
            heartGlow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            heartGlow->setID("heart-glow");
            thankYouContainer->addChild(heartGlow, 2);

            auto glowPulse = CCSequence::create(
                CCSpawn::create(
                    CCScaleTo::create(1.2f, 0.75f),
                    CCFadeTo::create(1.2f, 80),
                    nullptr
                ),
                CCSpawn::create(
                    CCScaleTo::create(0.1f, 0.42f),
                    CCFadeTo::create(0.1f, 0),
                    nullptr
                ),
                CCDelayTime::create(0.5f),
                nullptr
            );
            heartGlow->runAction(CCRepeatForever::create(glowPulse));
        }
    }

    separator->runAction(CCSequence::create(
        CCDelayTime::create(0.7f),
        CCEaseBackOut::create(CCScaleTo::create(0.8f, 1.0f, 1.0f)),
        nullptr
    ));

    msg->runAction(CCSequence::create(
        CCDelayTime::create(0.9f),
        CCSpawn::create(
            CCEaseBackOut::create(CCMoveTo::create(0.6f, {winSize.width / 2, sectionY})),
            CCFadeTo::create(0.5f, 255),
            nullptr
        ),
        nullptr
    ));

    if (heart) {
        heart->runAction(CCSequence::create(
            CCDelayTime::create(0.8f),
            CCFadeTo::create(0.4f, 255),
            nullptr
        ));
        
        auto heartGlow = thankYouContainer->getChildByID("heart-glow");
        if (heartGlow) {
            heartGlow->runAction(CCSequence::create(
                CCDelayTime::create(0.8f),
                CCFadeTo::create(0.4f, 40),
                nullptr
            ));
        }
    }
}

void PaimonSupportLayer::createButtons() {
    auto winSize = CCDirector::get()->getWinSize();

    auto donateMenu = CCMenu::create();
    donateMenu->setPosition({winSize.width / 2, 28.f});
    donateMenu->setID("donate-menu");
    this->addChild(donateMenu, 5);

    auto donateSpr = ButtonSprite::create(
        "Donate", 120, true, "bigFont.fnt", "GJ_button_01.png", 35.f, 0.7f
    );
    donateSpr->setScale(0.9f);
    
    auto donateBtn = CCMenuItemSpriteExtra::create(
        donateSpr, this, menu_selector(PaimonSupportLayer::onDonate)
    );
    donateBtn->setID("donate-btn"_spr);
    donateMenu->addChild(donateBtn);

    auto heartIcon = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
    if (heartIcon) {
        heartIcon->setScale(0.35f);
        heartIcon->setPosition({donateSpr->getContentWidth() - 22.f, donateSpr->getContentHeight() / 2});
        heartIcon->setColor({255, 100, 130});
        heartIcon->setID("heart-icon");
        donateSpr->addChild(heartIcon, 10);

        auto quickBeat = CCSequence::create(
            CCScaleTo::create(0.12f, 0.44f),
            CCScaleTo::create(0.12f, 0.32f),
            CCScaleTo::create(0.12f, 0.40f),
            CCScaleTo::create(0.42f, 0.35f),
            nullptr
        );
        heartIcon->runAction(CCRepeatForever::create(quickBeat));
    }

    auto btnGlow = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (btnGlow) {
        btnGlow->setPosition({winSize.width / 2, 28.f});
        btnGlow->setColor({255, 180, 0});
        btnGlow->setOpacity(0);
        btnGlow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        btnGlow->setID("donate-ripple-glow");
        this->addChild(btnGlow, 4);

        auto ripple = CCSequence::create(
            CCSpawn::create(
                CCScaleTo::create(2.0f, 1.8f),
                CCFadeTo::create(2.0f, 65),
                nullptr
            ),
            CCSpawn::create(
                CCScaleTo::create(0.1f, 0.8f),
                CCFadeTo::create(0.1f, 0),
                nullptr
            ),
            CCDelayTime::create(0.4f),
            nullptr
        );
        btnGlow->runAction(CCRepeatForever::create(ripple));
    }

    donateBtn->setScale(0);
    donateBtn->runAction(CCSequence::create(
        CCDelayTime::create(1.1f),
        CCEaseElasticOut::create(CCScaleTo::create(0.9f, 1.0f)),
        CCCallFunc::create(this, callfunc_selector(PaimonSupportLayer::createParticles)),
        nullptr
    ));

    auto btnBreathe = CCSequence::create(
        CCScaleTo::create(1.2f, 1.03f),
        CCScaleTo::create(1.2f, 0.97f),
        nullptr
    );
    donateBtn->runAction(CCSequence::create(
        CCDelayTime::create(2.0f),
        CCRepeatForever::create(btnBreathe),
        nullptr
    ));

    auto backMenu = CCMenu::create();
    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(
        backSpr, this, menu_selector(PaimonSupportLayer::onBack)
    );
    backBtn->setID("back-btn"_spr);
    backBtn->setPosition({-winSize.width / 2 + 25.f, winSize.height / 2 - 25.f});
    backMenu->addChild(backBtn);
    backMenu->setPosition({winSize.width / 2, winSize.height / 2});
    backMenu->setID("back-menu");
    this->addChild(backMenu, 5);

    backBtn->setPosition({-winSize.width / 2 - 50.f, winSize.height / 2 - 25.f}); // starts off-screen, slides in
    backBtn->runAction(CCSequence::create(
        CCDelayTime::create(0.3f),
        CCEaseBackOut::create(CCMoveTo::create(0.7f, {-winSize.width / 2 + 25.f, winSize.height / 2 - 25.f})),
        nullptr
    ));
}

void PaimonSupportLayer::createParticles() {
    auto winSize = CCDirector::get()->getWinSize();
    static std::mt19937 rng(std::random_device{}());

    // seed particles across the screen so it doesn't start empty
    for (int i = 0; i < 16; i++) {
        std::uniform_int_distribution<int> typeDist(0, 2);
        int type = typeDist(rng);

        cocos2d::CCSprite* particle = nullptr;
        cocos2d::ccColor3B color = {255, 255, 255};
        float baseScale = 0.1f;

        if (type == 0) {
            particle = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
            color = {255, 220, 70};
            baseScale = 0.12f;
        } else if (type == 1) {
            particle = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
            color = {255, 95, 135};
            baseScale = 0.14f;
        } else {
            particle = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
            color = {100, 210, 255};
            baseScale = 0.10f;
        }

        if (!particle) continue;

        std::uniform_real_distribution<float> scaleDist(0.5f, 1.3f);
        std::uniform_int_distribution<int> opacityDist(25, 75);
        std::uniform_real_distribution<float> xDist(10.f, winSize.width - 10.f);
        std::uniform_real_distribution<float> yDist(10.f, winSize.height - 20.f);
        std::uniform_real_distribution<float> durDist(4.f, 9.f);
        std::uniform_real_distribution<float> driftDist(-40.f, 40.f);
        std::uniform_real_distribution<float> rotDist(20.f, 80.f);

        float scale = baseScale * scaleDist(rng);
        particle->setScale(scale);
        particle->setOpacity(opacityDist(rng));
        particle->setColor(color);
        particle->setID("ambient-particle");

        float startX = xDist(rng);
        float startY = yDist(rng);
        particle->setPosition({startX, startY});
        this->addChild(particle, 1);

        float targetY = winSize.height + 20.f;
        float remainingDist = targetY - startY;
        float totalDist = targetY + 10.f;
        float speedRatio = remainingDist / totalDist;
        float duration = durDist(rng) * speedRatio;
        if (duration < 0.5f) duration = 0.5f;

        float driftX = driftDist(rng) * speedRatio;

        auto move = CCMoveTo::create(duration, {startX + driftX, targetY});
        auto fadeOut = CCFadeTo::create(duration * 0.8f, 0);
        auto spawn = CCSpawn::create(move, fadeOut, nullptr);
        auto cleanup = CCCallFunc::create(particle, callfunc_selector(CCNode::removeFromParent));
        particle->runAction(CCSequence::create(spawn, cleanup, nullptr));

        float rotSpeed = rotDist(rng);
        particle->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, rotSpeed)));
    }

    this->unschedule(schedule_selector(PaimonSupportLayer::spawnParticles));
    this->schedule(schedule_selector(PaimonSupportLayer::spawnParticles), 3.5f);
}

void PaimonSupportLayer::spawnParticles(float dt) {
    if (!m_alive.load(std::memory_order_acquire) || !getParent()) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto winSize = director->getWinSize();
    static std::mt19937 rng(std::random_device{}());

    // spawn new particles from the bottom each interval
    for (int i = 0; i < 6; i++) {
        std::uniform_int_distribution<int> typeDist(0, 2);
        int type = typeDist(rng);

        cocos2d::CCSprite* particle = nullptr;
        cocos2d::ccColor3B color = {255, 255, 255};
        float baseScale = 0.1f;

        if (type == 0) {
            particle = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
            color = {255, 220, 70};
            baseScale = 0.12f;
        } else if (type == 1) {
            particle = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
            color = {255, 95, 135};
            baseScale = 0.14f;
        } else {
            particle = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
            color = {100, 210, 255};
            baseScale = 0.10f;
        }

        if (!particle) continue;

        std::uniform_real_distribution<float> scaleDist(0.5f, 1.3f);
        std::uniform_int_distribution<int> opacityDist(25, 75);
        std::uniform_real_distribution<float> xDist(0.f, winSize.width);
        std::uniform_real_distribution<float> durDist(5.f, 10.f);
        std::uniform_real_distribution<float> driftDist(-40.f, 40.f);
        std::uniform_real_distribution<float> rotDist(20.f, 80.f);

        float scale = baseScale * scaleDist(rng);
        particle->setScale(scale);
        particle->setOpacity(opacityDist(rng));
        particle->setColor(color);
        particle->setID("ambient-particle");

        float startX = xDist(rng);
        float startY = -15.f;
        particle->setPosition({startX, startY});
        this->addChild(particle, 1);

        float duration = durDist(rng);
        float driftX = driftDist(rng);
        float targetY = winSize.height + 20.f;

        auto move = CCMoveTo::create(duration, {startX + driftX, targetY});
        auto fadeOut = CCFadeTo::create(duration * 0.8f, 0);
        auto spawn = CCSpawn::create(move, fadeOut, nullptr);
        auto cleanup = CCCallFunc::create(particle, callfunc_selector(CCNode::removeFromParent));
        particle->runAction(CCSequence::create(spawn, cleanup, nullptr));

        float rotSpeed = rotDist(rng);
        particle->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, rotSpeed)));
    }
}

void PaimonSupportLayer::onBack(CCObject*) {
    CCDirector::get()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

void PaimonSupportLayer::keyBackClicked() {
    onBack(nullptr);
}

void PaimonSupportLayer::onDonate(CCObject*) {
    geode::Loader::get()->queueInMainThread([]() {
        geode::utils::web::openLinkInBrowser("https://ko-fi.com/flozwer");
    });
}
