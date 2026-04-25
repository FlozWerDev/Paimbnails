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
#include <filesystem>
#include <fstream>
#include <thread>
#include <random>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

// ── factory ──────────────────────────────────────────────

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

// ── init ─────────────────────────────────────────────────

bool PaimonSupportLayer::init() {
    if (!CCLayer::init()) return false;

    this->setKeypadEnabled(true);

    createBackground();
    createTitle();
    createBadgePanel();
    createBenefitsPanel();
    createThankYouSection();
    createButtons();
    createParticles();

    return true;
}

// ── background ───────────────────────────────────────────

void PaimonSupportLayer::createBackground() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // fondo base oscuro (se ve mientras cargan los thumbnails)
    auto bg = CCLayerColor::create(ccc4(15, 10, 30, 255));
    bg->setContentSize(winSize);
    this->addChild(bg, -5);

    // overlay oscuro para legibilidad (más pronunciado)
    auto overlay = CCLayerColor::create({0, 0, 0, 100});
    overlay->setContentSize(winSize);
    this->addChild(overlay, -2);

    // gradiente sutil de arriba (púrpura oscuro) a abajo (negro)
    auto gradient = CCLayerGradient::create(
        ccc4(30, 15, 50, 90),
        ccc4(5, 5, 15, 120)
    );
    gradient->setContentSize(winSize);
    gradient->setVector({0, -1});
    this->addChild(gradient, -1);

    // bordes decorativos GD
    auto bottomLeft = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    if (bottomLeft) {
        bottomLeft->setAnchorPoint({0, 0});
        bottomLeft->setPosition({-2, -2});
        bottomLeft->setOpacity(60);
        this->addChild(bottomLeft, 0);
    }
    auto bottomRight = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    if (bottomRight) {
        bottomRight->setAnchorPoint({1, 0});
        bottomRight->setPosition({winSize.width + 2, -2});
        bottomRight->setFlipX(true);
        bottomRight->setOpacity(60);
        this->addChild(bottomRight, 0);
    }

    // iniciar carga de thumbnails showcase
    loadShowcaseThumbnails();
}

// ── thumbnail background dinamico ────────────────────────

void PaimonSupportLayer::loadShowcaseThumbnails() {
    // escanear el cache local de thumbnails en disco
    auto cachePath = paimon::quality::cacheDir();
    
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec)) return;

    for (auto const& entry : std::filesystem::directory_iterator(cachePath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto ext = geode::utils::string::pathToString(entry.path().extension());
        // solo formatos estaticos soportados (no gifs animados para el fondo)
        if (ext != ".png" && ext != ".webp" && ext != ".jpg" && ext != ".jpeg" &&
            ext != ".qoi" && ext != ".jxl") continue;
        // ignorar archivos muy pequenos (< 5kb, posible error)
        std::error_code sizeEc;
        auto fsize = entry.file_size(sizeEc);
        if (sizeEc || fsize < 5000) continue;
        m_cachedThumbPaths.push_back(geode::utils::string::pathToString(entry.path()));
    }

    if (m_cachedThumbPaths.empty()) return;

    // mezclar aleatoriamente
    {
        static std::mt19937 rng(std::random_device{}());
        std::shuffle(m_cachedThumbPaths.begin(), m_cachedThumbPaths.end(), rng);
    }

    // limitar a 20 para no abusar
    if (m_cachedThumbPaths.size() > 20) m_cachedThumbPaths.resize(20);

    m_currentThumbIndex = 0;

    // cargar el primer thumbnail inmediatamente
    cycleThumbnail(0.f);

    // programar cambio cada 5 segundos (unschedule primero para evitar acumulacion)
    this->unschedule(schedule_selector(PaimonSupportLayer::cycleThumbnail));
    this->schedule(schedule_selector(PaimonSupportLayer::cycleThumbnail), 5.0f);
}

void PaimonSupportLayer::cycleThumbnail(float dt) {
    if (m_cachedThumbPaths.empty() || m_loadingThumb) return;
    if (paimon::isRuntimeShuttingDown()) return;

    m_loadingThumb = true;
    auto filePath = m_cachedThumbPaths[m_currentThumbIndex % m_cachedThumbPaths.size()];
    m_currentThumbIndex++;

    // cargar la imagen desde disco en un thread para no trabar UI
    WeakRef<PaimonSupportLayer> safeSelf = this;

    std::thread([safeSelf, filePath]() {
        geode::utils::thread::setName("SupportLayer BG Loader");
        if (paimon::isRuntimeShuttingDown()) return;

        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            if (paimon::isRuntimeShuttingDown()) return;

            Loader::get()->queueInMainThread([safeSelf]() {
                if (paimon::isRuntimeShuttingDown()) return;

                auto selfRef = safeSelf.lock();
                auto* self = static_cast<PaimonSupportLayer*>(selfRef);
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
                auto* self = static_cast<PaimonSupportLayer*>(selfRef);
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
            auto* self = static_cast<PaimonSupportLayer*>(selfRef);
            if (!self) return;
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
    }).detach();
}

void PaimonSupportLayer::applyThumbnailBackground(CCTexture2D* texture) {
    if (!texture) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // Blur offline multi-pass (Gaussian 2-pass, robusto y probado)
    auto blurred = BlurSystem::getInstance()->createBlurredSprite(texture, winSize, 0.10f);
    if (!blurred) return;

    // Sprite desde la textura blurreada (proviene de RenderTexture, necesita flipY)
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
    newBg->setColor({170, 160, 210}); // tinte púrpura frío
    this->addChild(newBg, -3);

    // transición suave: fade in el nuevo, fade out el viejo
    float fadeDuration = 1.2f;
    newBg->runAction(CCFadeTo::create(fadeDuration, 200));

    // animación breathing: opacidad pulsa suavemente
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

// ── title ────────────────────────────────────────────────

void PaimonSupportLayer::createTitle() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float topY = winSize.height - 24.f;

    // estrella izquierda
    auto starL = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (starL) {
        starL->setScale(0.4f);
        starL->setPosition({winSize.width / 2 - 120.f, topY});
        starL->setColor({255, 215, 0});
        this->addChild(starL, 2);
    }

    // titulo principal
    auto title = CCLabelBMFont::create("Support Paimbnails", "goldFont.fnt");
    title->setPosition({winSize.width / 2, topY});
    title->setScale(0.85f);
    this->addChild(title, 2);

    // estrella derecha
    auto starR = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (starR) {
        starR->setScale(0.4f);
        starR->setPosition({winSize.width / 2 + 120.f, topY});
        starR->setColor({255, 215, 0});
        this->addChild(starR, 2);
    }

    // subtitulo
    auto subtitle = CCLabelBMFont::create("Help keep the mod alive and growing!", "chatFont.fnt");
    subtitle->setPosition({winSize.width / 2, topY - 20.f});
    subtitle->setScale(0.55f);
    subtitle->setColor({200, 180, 255});
    this->addChild(subtitle, 2);
}

// ── badge panel (izquierda) ──────────────────────────────

void PaimonSupportLayer::createBadgePanel() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    float panelW = 150.f;
    float panelH = 150.f;
    float panelX = winSize.width * 0.22f;
    float panelY = winSize.height * 0.52f;

    // fondo panel badge
    auto panelBg = paimon::SpriteHelper::createColorPanel(panelW, panelH, {40, 20, 70}, 180);
    panelBg->setPosition({panelX - panelW / 2, panelY - panelH / 2});
    this->addChild(panelBg, 1);

    // borde dorado
    auto border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (border) {
        border->setContentSize({panelW + 6.f, panelH + 6.f});
        border->setPosition({panelX, panelY});
        border->setColor({255, 200, 50});
        this->addChild(border, 2);
    }

    // titulo del panel
    auto badgeTitle = CCLabelBMFont::create("Supporter Badge", "goldFont.fnt");
    badgeTitle->setScale(0.35f);
    badgeTitle->setPosition({panelX, panelY + panelH / 2 - 14.f});
    this->addChild(badgeTitle, 3);

    // icono de badge — corona dorada
    auto crownIcon = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (crownIcon) {
        crownIcon->setScale(0.7f);
        crownIcon->setPosition({panelX, panelY + 10.f});
        crownIcon->setColor({255, 215, 0});
        this->addChild(crownIcon, 3);

        // animacion de brillo pulsante
        auto pulse = CCSequence::create(
            CCScaleTo::create(1.2f, 0.78f),
            CCScaleTo::create(1.2f, 0.65f),
            nullptr
        );
        crownIcon->runAction(CCRepeatForever::create(pulse));

        // glow detras del icono
        auto glow = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
        if (glow) {
            glow->setScale(0.9f);
            glow->setPosition({panelX, panelY + 10.f});
            glow->setColor({255, 180, 0});
            glow->setOpacity(50);
            glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE}); // additive blend
            this->addChild(glow, 2);

            auto glowPulse = CCSequence::create(
                CCFadeTo::create(1.5f, 90),
                CCFadeTo::create(1.5f, 30),
                nullptr
            );
            glow->runAction(CCRepeatForever::create(glowPulse));
        }
    }

    // etiqueta "Exclusive"
    auto exclusiveLbl = CCLabelBMFont::create("Exclusive", "bigFont.fnt");
    exclusiveLbl->setScale(0.3f);
    exclusiveLbl->setColor({255, 200, 100});
    exclusiveLbl->setPosition({panelX, panelY - 30.f});
    this->addChild(exclusiveLbl, 3);

    // texto decorativo bajo el badge
    auto badgeDesc = CCLabelBMFont::create("Shown on your profile", "chatFont.fnt");
    badgeDesc->setScale(0.35f);
    badgeDesc->setColor({180, 160, 220});
    badgeDesc->setPosition({panelX, panelY - 48.f});
    this->addChild(badgeDesc, 3);
}

// ── benefits panel (derecha) ─────────────────────────────

void PaimonSupportLayer::createBenefitsPanel() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    float panelW = 220.f;
    float panelH = 150.f;
    float panelX = winSize.width * 0.68f;
    float panelY = winSize.height * 0.52f;

    // fondo panel beneficios
    auto panelBg = paimon::SpriteHelper::createColorPanel(panelW, panelH, {20, 15, 50}, 180);
    panelBg->setPosition({panelX - panelW / 2, panelY - panelH / 2});
    this->addChild(panelBg, 1);

    // borde
    auto border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (border) {
        border->setContentSize({panelW + 6.f, panelH + 6.f});
        border->setPosition({panelX, panelY});
        this->addChild(border, 2);
    }

    // titulo
    auto benefitsTitle = CCLabelBMFont::create("Supporter Benefits", "goldFont.fnt");
    benefitsTitle->setScale(0.38f);
    benefitsTitle->setPosition({panelX, panelY + panelH / 2 - 14.f});
    this->addChild(benefitsTitle, 3);

    // lista de beneficios
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

        // icono
        auto icon = CCSprite::createWithSpriteFrameName(benefits[i].icon);
        if (icon) {
            icon->setScale(0.32f);
            icon->setPosition({leftX, rowY});
            icon->setColor(benefits[i].color);
            this->addChild(icon, 3);
        }

        // texto
        auto lbl = CCLabelBMFont::create(benefits[i].text, "chatFont.fnt");
        lbl->setScale(0.42f);
        lbl->setAnchorPoint({0, 0.5f});
        lbl->setPosition({leftX + 14.f, rowY});
        lbl->setColor({220, 220, 240});
        this->addChild(lbl, 3);
    }
}

// ── thank you section ────────────────────────────────────

void PaimonSupportLayer::createThankYouSection() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    float sectionY = winSize.height * 0.15f;

    // linea separadora sutil
    auto separator = CCLayerColor::create({255, 200, 50, 30});
    separator->setContentSize({winSize.width * 0.6f, 1.5f});
    separator->setPosition({winSize.width * 0.2f, sectionY + 22.f});
    this->addChild(separator, 2);

    // mensaje principal
    auto msg = CCLabelBMFont::create(
        "Every donation helps me dedicate more time\nto improving Paimbnails for the community.",
        "chatFont.fnt"
    );
    msg->setScale(0.48f);
    msg->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
    msg->setPosition({winSize.width / 2, sectionY});
    msg->setColor({200, 190, 230});
    this->addChild(msg, 2);

    // corazoncito
    auto heart = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
    if (heart) {
        heart->setScale(0.4f);
        heart->setPosition({winSize.width / 2, sectionY + 30.f});
        heart->setColor({255, 80, 120});
        this->addChild(heart, 3);

        // latido
        auto beat = CCSequence::create(
            CCScaleTo::create(0.3f, 0.48f),
            CCScaleTo::create(0.3f, 0.38f),
            CCScaleTo::create(0.3f, 0.45f),
            CCScaleTo::create(0.6f, 0.40f),
            nullptr
        );
        heart->runAction(CCRepeatForever::create(beat));
    }
}

// ── buttons ──────────────────────────────────────────────

void PaimonSupportLayer::createButtons() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // ── boton Donate (principal, grande, dorado) ──
    auto donateMenu = CCMenu::create();
    donateMenu->setPosition({winSize.width / 2, 28.f});
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

    // icono de corazon al lado del texto
    auto heartIcon = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
    if (heartIcon) {
        heartIcon->setScale(0.35f);
        heartIcon->setPosition({donateSpr->getContentWidth() - 22.f, donateSpr->getContentHeight() / 2});
        heartIcon->setColor({255, 100, 130});
        donateSpr->addChild(heartIcon, 10);
    }

    // ── boton Back ──
    auto backMenu = CCMenu::create();
    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(
        backSpr, this, menu_selector(PaimonSupportLayer::onBack)
    );
    backBtn->setID("back-btn"_spr);
    backBtn->setPosition({-winSize.width / 2 + 25.f, winSize.height / 2 - 25.f});
    backMenu->addChild(backBtn);
    backMenu->setPosition({winSize.width / 2, winSize.height / 2});
    this->addChild(backMenu, 5);
}

// ── particles ────────────────────────────────────────────

void PaimonSupportLayer::createParticles() {
    // crear primera tanda y programar las siguientes
    spawnParticles(0.f);
    this->unschedule(schedule_selector(PaimonSupportLayer::spawnParticles));
    this->schedule(schedule_selector(PaimonSupportLayer::spawnParticles), 5.0f);
}

void PaimonSupportLayer::spawnParticles(float dt) {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    static std::mt19937 rng(std::random_device{}());

    // estrellitas doradas flotantes (estilo premium)
    for (int i = 0; i < 8; i++) {
        auto star = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
        if (!star) continue;

        std::uniform_real_distribution<float> scaleDist(0.06f, 0.18f);
        std::uniform_int_distribution<int> opacityDist(25, 74);
        std::uniform_real_distribution<float> xDist(0.f, winSize.width);
        std::uniform_real_distribution<float> durDist(5.f, 10.f);
        std::uniform_real_distribution<float> driftDist(-30.f, 30.f);
        std::uniform_real_distribution<float> rotDist(30.f, 90.f);

        float scale = scaleDist(rng);
        star->setScale(scale);
        star->setOpacity(opacityDist(rng));
        star->setColor({255, 215, 0});

        float startX = xDist(rng);
        float startY = -10.f;
        star->setPosition({startX, startY});
        this->addChild(star, 1);

        // flotan hacia arriba suavemente
        float duration = durDist(rng);
        float driftX = driftDist(rng);
        float targetY = winSize.height + 20.f;

        auto move = CCMoveTo::create(duration, {startX + driftX, targetY});
        auto fadeOut = CCFadeTo::create(duration * 0.8f, 0);
        auto spawn = CCSpawn::create(move, fadeOut, nullptr);
        auto cleanup = CCCallFunc::create(star, callfunc_selector(CCNode::removeFromParent));
        star->runAction(CCSequence::create(spawn, cleanup, nullptr));

        // rotacion lenta
        float rotSpeed = rotDist(rng);
        star->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, rotSpeed)));
    }
}

// ── navigation ───────────────────────────────────────────

void PaimonSupportLayer::onBack(CCObject*) {
    CCDirector::sharedDirector()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

void PaimonSupportLayer::keyBackClicked() {
    onBack(nullptr);
}

void PaimonSupportLayer::onDonate(CCObject*) {
    geode::Loader::get()->queueInMainThread([]() {
        geode::utils::web::openLinkInBrowser("https://ko-fi.com/flozwer");
    });
}
