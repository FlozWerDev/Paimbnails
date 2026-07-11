#include "PaimonSupportLayer.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../core/QualityConfig.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/ThreadTracker.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

std::mt19937& rng() {
    static std::mt19937 g(std::random_device{}());
    return g;
}

void addSideArt(CCLayer* layer, CCSize win) {
    if (auto left = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png")) {
        left->setAnchorPoint({0, 0});
        left->setPosition({-2, -2});
        left->setOpacity(60);
        layer->addChild(left, 0);
    }
    if (auto right = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png")) {
        right->setAnchorPoint({1, 0});
        right->setPosition({win.width + 2, -2});
        right->setFlipX(true);
        right->setOpacity(60);
        layer->addChild(right, 0);
    }
}

CCNode* makePanel(float w, float h, float x, float y, ccColor3B borderColor) {
    auto root = CCNode::create();

    auto bg = paimon::SpriteHelper::createColorPanel(w, h, {15, 10, 32}, 205);
    bg->setPosition({x - w / 2, y - h / 2});
    root->addChild(bg, 1);

    if (auto border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
        border->setContentSize({w + 6.f, h + 6.f});
        border->setPosition({x, y});
        border->setColor(borderColor);
        root->addChild(border, 2);
    }
    return root;
}

CCSprite* makeParticle(int type, float baseScale, ccColor3B color) {
    char const* frame = "GJ_bigStar_001.png";
    if (type == 1) {
        frame = "gj_heartOn_001.png";
        baseScale = 0.14f;
        color = {255, 95, 135};
    } else if (type == 2) {
        frame = "GJ_starsIcon_001.png";
        baseScale = 0.10f;
        color = {100, 210, 255};
    } else {
        color = {255, 220, 70};
        baseScale = 0.12f;
    }
    auto* p = CCSprite::createWithSpriteFrameName(frame);
    if (!p) return nullptr;
    p->setScale(baseScale * std::uniform_real_distribution<float>(0.5f, 1.3f)(rng()));
    p->setOpacity(std::uniform_int_distribution<int>(25, 75)(rng()));
    p->setColor(color);
    return p;
}

void flyParticle(CCSprite* p, CCPoint from, CCPoint to, float duration) {
    p->setPosition(from);
    p->runAction(CCSequence::create(
        CCSpawn::create(
            CCMoveTo::create(duration, to),
            CCFadeTo::create(duration * 0.8f, 0),
            nullptr
        ),
        CCCallFunc::create(p, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
    p->runAction(CCRepeatForever::create(
        CCRotateBy::create(2.f, std::uniform_real_distribution<float>(20.f, 80.f)(rng()))
    ));
}

} // namespace

PaimonSupportLayer* PaimonSupportLayer::create() {
    auto ret = new PaimonSupportLayer();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
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
    buildUI();
    return true;
}

void PaimonSupportLayer::buildUI() {
    auto win = CCDirector::get()->getWinSize();
    float cx = win.width / 2.f;

    auto bg = CCLayerColor::create(ccc4(15, 10, 30, 255));
    bg->setContentSize(win);
    this->addChild(bg, -5);

    auto overlay = CCLayerColor::create({0, 0, 0, 100});
    overlay->setContentSize(win);
    this->addChild(overlay, -2);

    auto grad = CCLayerGradient::create(ccc4(30, 15, 50, 90), ccc4(5, 5, 15, 120));
    grad->setContentSize(win);
    grad->setVector({0, -1});
    this->addChild(grad, -1);

    addSideArt(this, win);

    if (auto glow = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png")) {
        glow->setScale(6.f);
        glow->setPosition(win / 2);
        glow->setColor({80, 40, 120});
        glow->setOpacity(40);
        glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        this->addChild(glow, -4);
        glow->runAction(CCRepeatForever::create(CCRotateBy::create(20.f, 360.f)));
        glow->runAction(CCRepeatForever::create(CCSequence::create(
            CCFadeTo::create(4.f, 60), CCFadeTo::create(4.f, 20), nullptr
        )));
    }

    loadShowcaseThumbnails();

    float topY = win.height - 24.f;
    auto title = CCLabelBMFont::create("Support Paimbnails", "goldFont.fnt");
    title->setPosition({cx, topY});
    title->setScale(0.85f);
    this->addChild(title, 2);

    auto sub = CCLabelBMFont::create("Help keep the mod alive and growing!", "chatFont.fnt");
    sub->setPosition({cx, topY - 20.f});
    sub->setScale(0.55f);
    sub->setColor({200, 180, 255});
    this->addChild(sub, 2);

    float badgeX = win.width * 0.22f;
    float panelY = win.height * 0.52f;
    auto badgeRoot = makePanel(150.f, 150.f, badgeX, panelY, {255, 205, 50});
    this->addChild(badgeRoot, 3);

    auto badgeTitle = CCLabelBMFont::create("Supporter Badge", "goldFont.fnt");
    badgeTitle->setScale(0.35f);
    badgeTitle->setPosition({badgeX, panelY + 61.f});
    badgeRoot->addChild(badgeTitle, 4);

    if (auto star = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png")) {
        star->setScale(0.7f);
        star->setColor({255, 215, 0});
        star->setPosition({badgeX, panelY + 10.f});
        badgeRoot->addChild(star, 4);
        star->runAction(CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(1.f, 0.74f), CCScaleTo::create(1.f, 0.66f), nullptr
        )));
    }

    auto exclusive = CCLabelBMFont::create("Exclusive", "bigFont.fnt");
    exclusive->setScale(0.3f);
    exclusive->setColor({255, 205, 100});
    exclusive->setPosition({badgeX, panelY - 30.f});
    badgeRoot->addChild(exclusive, 4);

    auto badgeDesc = CCLabelBMFont::create("Shown on your profile", "chatFont.fnt");
    badgeDesc->setScale(0.35f);
    badgeDesc->setColor({180, 160, 220});
    badgeDesc->setPosition({badgeX, panelY - 48.f});
    badgeRoot->addChild(badgeDesc, 4);

    float benX = win.width * 0.68f;
    auto benRoot = makePanel(220.f, 150.f, benX, panelY, {255, 120, 180});
    this->addChild(benRoot, 3);

    auto benTitle = CCLabelBMFont::create("Supporter Benefits", "goldFont.fnt");
    benTitle->setScale(0.38f);
    benTitle->setPosition({benX, panelY + 61.f});
    benRoot->addChild(benTitle, 4);

    struct Benefit { char const* icon; char const* text; ccColor3B color; };
    Benefit benefits[] = {
        {"GJ_bigStar_001.png",       "Exclusive Supporter Badge",  {255, 215, 0}},
        {"GJ_completesIcon_001.png", "Priority for Your Ideas",    {100, 255, 100}},
        {"GJ_starsIcon_001.png",     "Your Name on the VIP List",  {255, 180, 100}},
        {"GJ_sMagicIcon_001.png",    "Use GIFs for Profile & More",{100, 200, 255}},
        {"GJ_lock_001.png",          "Greater Customization",      {200, 150, 255}},
        {"gj_heartOn_001.png",       "Early Access Before Public", {255, 100, 150}},
    };

    float startY = panelY + 43.f;
    float leftX = benX - 92.f;
    for (int i = 0; i < 6; i++) {
        float rowY = startY - i * 19.f;
        if (auto icon = CCSprite::createWithSpriteFrameName(benefits[i].icon)) {
            icon->setScale(0.32f);
            icon->setPosition({leftX, rowY});
            icon->setColor(benefits[i].color);
            benRoot->addChild(icon, 4);
        }
        auto lbl = CCLabelBMFont::create(benefits[i].text, "chatFont.fnt");
        lbl->setScale(0.42f);
        lbl->setAnchorPoint({0, 0.5f});
        lbl->setPosition({leftX + 14.f, rowY});
        lbl->setColor({220, 220, 240});
        benRoot->addChild(lbl, 4);
    }

    float sectionY = win.height * 0.20f;
    auto sep = CCLayerColor::create({255, 120, 180, 45});
    sep->setContentSize({win.width * 0.6f, 1.5f});
    sep->setPosition({win.width * 0.2f, sectionY + 22.f});
    this->addChild(sep, 2);

    if (auto heart = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png")) {
        heart->setScale(0.4f);
        heart->setPosition({cx, sectionY + 30.f});
        heart->setColor({255, 80, 120});
        this->addChild(heart, 3);
        heart->runAction(CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(0.18f, 0.52f),
            CCScaleTo::create(0.18f, 0.38f),
            CCScaleTo::create(0.18f, 0.48f),
            CCScaleTo::create(0.68f, 0.40f),
            nullptr
        )));
    }

    auto msg = CCLabelBMFont::create(
        "Every donation helps me dedicate more time\nto improving Paimbnails for the community.",
        "chatFont.fnt"
    );
    msg->setScale(0.48f);
    msg->setAlignment(kCCTextAlignmentCenter);
    msg->setPosition({cx, sectionY});
    msg->setColor({200, 190, 230});
    this->addChild(msg, 2);

    auto donateMenu = CCMenu::create();
    donateMenu->setPosition({cx, 28.f});
    this->addChild(donateMenu, 5);

    auto donateSpr = ButtonSprite::create("Donate", 120, true, "bigFont.fnt", "GJ_button_01.png", 35.f, 0.7f);
    donateSpr->setScale(0.9f);
    auto donateBtn = CCMenuItemSpriteExtra::create(
        donateSpr, this, menu_selector(PaimonSupportLayer::onDonate)
    );
    donateBtn->setID("donate-btn"_spr);
    donateMenu->addChild(donateBtn);

    if (auto heartIcon = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png")) {
        heartIcon->setScale(0.35f);
        heartIcon->setPosition({donateSpr->getContentWidth() - 22.f, donateSpr->getContentHeight() / 2});
        heartIcon->setColor({255, 100, 130});
        donateSpr->addChild(heartIcon, 10);
    }

    this->schedule(schedule_selector(PaimonSupportLayer::spawnParticles), 3.5f);
    spawnParticles(0.f);

    auto backMenu = CCMenu::create();
    backMenu->setPosition({cx, win.height / 2});
    this->addChild(backMenu, 5);

    auto backBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png"),
        this, menu_selector(PaimonSupportLayer::onBack)
    );
    backBtn->setID("back-btn"_spr);
    backBtn->setPosition({-win.width / 2 + 25.f, win.height / 2 - 25.f});
    backMenu->addChild(backBtn);
}

void PaimonSupportLayer::loadShowcaseThumbnails() {
    auto cachePath = paimon::quality::cacheDir();
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec)) return;

    for (auto const& entry : std::filesystem::directory_iterator(cachePath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto ext = geode::utils::string::pathToString(entry.path().extension());
        if (ext != ".png" && ext != ".webp" && ext != ".jpg" && ext != ".jpeg" &&
            ext != ".qoi" && ext != ".jxl") continue;
        std::error_code sizeEc;
        auto fsize = entry.file_size(sizeEc);
        if (sizeEc || fsize < 5000) continue;
        m_cachedThumbPaths.push_back(geode::utils::string::pathToString(entry.path()));
    }
    if (m_cachedThumbPaths.empty()) return;

    std::shuffle(m_cachedThumbPaths.begin(), m_cachedThumbPaths.end(), rng());
    if (m_cachedThumbPaths.size() > 20) m_cachedThumbPaths.resize(20);

    m_currentThumbIndex = 0;
    cycleThumbnail(0.f);
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

void PaimonSupportLayer::cycleThumbnail(float) {
    if (!m_alive.load(std::memory_order_acquire)) return;
    if (m_cachedThumbPaths.empty() || m_loadingThumb) return;
    if (paimon::isRuntimeShuttingDown()) return;

    m_loadingThumb = true;
    auto filePath = m_cachedThumbPaths[m_currentThumbIndex % m_cachedThumbPaths.size()];
    m_currentThumbIndex++;

    WeakRef<PaimonSupportLayer> safeSelf = this;
    paimon::ThreadTracker::get().spawn([safeSelf, filePath]() {
        geode::utils::thread::setName("SupportLayer BG Loader");
        if (paimon::isRuntimeShuttingDown()) return;

        auto clearLoading = [safeSelf]() {
            Loader::get()->queueInMainThread([safeSelf]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (auto self = safeSelf.lock()) {
                    if (auto* layer = typeinfo_cast<PaimonSupportLayer*>(self.data()))
                        layer->m_loadingThumb = false;
                }
            });
        };

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) { clearLoading(); return; }
        auto size = file.tellg();
        if (size <= 0) { clearLoading(); return; }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), size);
        file.close();
        if (paimon::isRuntimeShuttingDown()) return;

        Loader::get()->queueInMainThread([safeSelf, data = std::move(data)]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto selfRef = safeSelf.lock();
            auto* self = selfRef ? typeinfo_cast<PaimonSupportLayer*>(selfRef.data()) : nullptr;
            if (!self || !self->m_alive.load(std::memory_order_acquire) || !self->getParent()) {
                if (self) self->m_loadingThumb.store(false, std::memory_order_release);
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
    if (!texture || !m_alive.load(std::memory_order_acquire) || !getParent()) return;
    if (paimon::isRuntimeShuttingDown()) return;

    auto* director = CCDirector::get();
    if (!director) return;
    auto win = director->getWinSize();

    auto blurred = BlurSystem::getInstance()->createBlurredSprite(texture, win, 0.10f);
    if (!blurred) return;

    auto newBg = CCSprite::createWithTexture(blurred->getTexture());
    if (!newBg) return;

    newBg->setFlipY(true);
    newBg->setPosition(win / 2);
    auto texSize = newBg->getContentSize();
    newBg->setScale(std::max(win.width / texSize.width, win.height / texSize.height));
    newBg->setOpacity(0);
    newBg->setColor({170, 160, 210});
    this->addChild(newBg, -3);

    constexpr float fade = 1.2f;
    newBg->runAction(CCFadeTo::create(fade, 200));
    newBg->runAction(CCRepeatForever::create(CCSequence::create(
        CCFadeTo::create(2.f, 220), CCFadeTo::create(2.f, 160), nullptr
    )));

    if (m_bgThumb) {
        auto oldBg = m_bgThumb;
        oldBg->stopAllActions();
        oldBg->runAction(CCSequence::create(
            CCFadeTo::create(fade, 0),
            CCCallFunc::create(oldBg, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }
    m_bgThumb = newBg;
}

void PaimonSupportLayer::spawnParticles(float) {
    if (!m_alive.load(std::memory_order_acquire) || !getParent()) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto win = director->getWinSize();

    for (int i = 0; i < 6; i++) {
        auto* p = makeParticle(std::uniform_int_distribution<int>(0, 2)(rng()), 0.12f, {255, 255, 255});
        if (!p) continue;
        float x = std::uniform_real_distribution<float>(0.f, win.width)(rng());
        float dur = std::uniform_real_distribution<float>(5.f, 10.f)(rng());
        float drift = std::uniform_real_distribution<float>(-40.f, 40.f)(rng());
        this->addChild(p, 1);
        flyParticle(p, {x, -15.f}, {x + drift, win.height + 20.f}, dur);
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
