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
    this->setID("PaimonSupportLayer");

    createBackground();
    createTitle();
    createBadgePanel();
    createBenefitsPanel();
    createThankYouSection();
    createButtons();

    return true;
}

// ── background ───────────────────────────────────────────

void PaimonSupportLayer::createBackground() {
    auto winSize = CCDirector::get()->getWinSize();

    // fondo base oscuro (se ve mientras cargan los thumbnails)
    auto bg = CCLayerColor::create(ccc4(15, 10, 30, 255));
    bg->setContentSize(winSize);
    bg->setID("base-background");
    this->addChild(bg, -5);

    // overlay oscuro para legibilidad (más pronunciado)
    auto overlay = CCLayerColor::create({0, 0, 0, 100});
    overlay->setContentSize(winSize);
    overlay->setID("dark-overlay");
    this->addChild(overlay, -2);

    // gradiente sutil de arriba (púrpura oscuro) a abajo (negro)
    auto gradient = CCLayerGradient::create(
        ccc4(30, 15, 50, 90),
        ccc4(5, 5, 15, 120)
    );
    gradient->setContentSize(winSize);
    gradient->setVector({0, -1});
    gradient->setID("gradient-overlay");
    this->addChild(gradient, -1);

    // bordes decorativos GD
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

    // fondo diagonal glow decorativo lento y mágico
    auto glow = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (glow) {
        glow->setScale(6.f);
        glow->setPosition(winSize / 2);
        glow->setColor({80, 40, 120});
        glow->setOpacity(40);
        glow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        glow->setID("ambient-glow");
        this->addChild(glow, -4);
        
        // Rotar lentamente
        glow->runAction(CCRepeatForever::create(CCRotateBy::create(20.f, 360.f)));
        // Pulsar suavemente
        auto pulse = CCSequence::create(
            CCFadeTo::create(4.0f, 60),
            CCFadeTo::create(4.0f, 20),
            nullptr
        );
        glow->runAction(CCRepeatForever::create(pulse));
        m_bgDiagonalGlow = glow;
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
    });
}

void PaimonSupportLayer::applyThumbnailBackground(CCTexture2D* texture) {
    if (!texture) return;

    auto winSize = CCDirector::get()->getWinSize();

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
    auto winSize = CCDirector::get()->getWinSize();
    float topY = winSize.height - 24.f;

    // Crear un contenedor de título para animar toda la sección junta
    auto titleContainer = CCNode::create();
    titleContainer->setPosition({0, 60.f}); // Empezar arriba de la pantalla
    titleContainer->setID("title-container");
    this->addChild(titleContainer, 2);

    // estrella izquierda
    auto starL = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (starL) {
        starL->setScale(0.4f);
        starL->setPosition({winSize.width / 2 - 120.f, topY});
        starL->setColor({255, 215, 0});
        starL->setID("left-star");
        titleContainer->addChild(starL, 2);

        // Rotación lenta continua izquierda
        starL->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, -180.f)));
    }

    // titulo principal
    auto title = CCLabelBMFont::create("Support Paimbnails", "goldFont.fnt");
    title->setPosition({winSize.width / 2, topY});
    title->setScale(0.85f);
    title->setID("main-title");
    titleContainer->addChild(title, 2);

    // estrella derecha
    auto starR = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (starR) {
        starR->setScale(0.4f);
        starR->setPosition({winSize.width / 2 + 120.f, topY});
        starR->setColor({255, 215, 0});
        starR->setID("right-star");
        titleContainer->addChild(starR, 2);

        // Rotación lenta continua derecha
        starR->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, 180.f)));
    }

    // subtitulo
    auto subtitle = CCLabelBMFont::create("Help keep the mod alive and growing!", "chatFont.fnt");
    subtitle->setPosition({winSize.width / 2, topY - 20.f});
    subtitle->setScale(0.55f);
    subtitle->setColor({200, 180, 255});
    subtitle->setOpacity(0); // Empezar invisible para fade-in
    subtitle->setID("subtitle");
    titleContainer->addChild(subtitle, 2);

    // Animación del contenedor de título: Cae con rebote
    titleContainer->runAction(CCEaseBackOut::create(CCMoveTo::create(0.8f, {0, 0})));

    // Animación de aparición gradual del subtítulo
    subtitle->runAction(CCSequence::create(
        CCDelayTime::create(0.6f),
        CCFadeTo::create(0.4f, 255),
        nullptr
    ));
}

// ── badge panel (izquierda) ──────────────────────────────

void PaimonSupportLayer::createBadgePanel() {
    auto winSize = CCDirector::get()->getWinSize();

    float panelW = 150.f;
    float panelH = 150.f;
    float panelX = winSize.width * 0.22f;
    float panelY = winSize.height * 0.52f;

    // Contenedor para el panel izquierdo (para animarlo desde fuera de la pantalla)
    m_badgePanelContainer = CCNode::create();
    m_badgePanelContainer->setPosition({-panelX - panelW, 0}); // Empezar fuera a la izquierda
    m_badgePanelContainer->setID("badge-panel-container");
    this->addChild(m_badgePanelContainer, 3);

    // fondo panel badge - Estilo glassmorphism profundo
    auto panelBg = paimon::SpriteHelper::createColorPanel(panelW, panelH, {15, 10, 32}, 205);
    panelBg->setPosition({panelX - panelW / 2, panelY - panelH / 2});
    panelBg->setID("panel-bg");
    m_badgePanelContainer->addChild(panelBg, 1);

    // Borde de neón dorado pulsante (brillo aditivo de fondo) usando standard safeCreateScale9
    auto neonGlow = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (neonGlow) {
        neonGlow->setContentSize({panelW + 8.f, panelH + 8.f});
        neonGlow->setPosition({panelX, panelY});
        neonGlow->setColor({255, 215, 0});
        neonGlow->setOpacity(80);
        neonGlow->setID("neon-glow");
        m_badgePanelContainer->addChild(neonGlow, 2);

        // Animación de pulso continuo de escala y opacidad
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


    // Borde dorado principal
    auto border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (border) {
        border->setContentSize({panelW + 6.f, panelH + 6.f});
        border->setPosition({panelX, panelY});
        border->setColor({255, 205, 50});
        border->setID("border");
        m_badgePanelContainer->addChild(border, 3);
    }

    // titulo del panel
    auto badgeTitle = CCLabelBMFont::create("Supporter Badge", "goldFont.fnt");
    badgeTitle->setScale(0.35f);
    badgeTitle->setPosition({panelX, panelY + panelH / 2 - 14.f});
    badgeTitle->setID("badge-title");
    m_badgePanelContainer->addChild(badgeTitle, 4);

    // Grupo para animar el Badge e Iconos Orbitantes juntos
    auto badgeGroup = CCNode::create();
    badgeGroup->setPosition({panelX, panelY + 10.f});
    badgeGroup->setID("badge-group");
    m_badgePanelContainer->addChild(badgeGroup, 4);

    // icono de badge — corona dorada
    auto crownIcon = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (crownIcon) {
        crownIcon->setScale(0.7f);
        crownIcon->setColor({255, 215, 0});
        crownIcon->setID("crown-icon");
        badgeGroup->addChild(crownIcon, 3);

        // Brillo interior pulsante propio del icono
        auto innerPulse = CCSequence::create(
            CCScaleTo::create(1.0f, 0.74f),
            CCScaleTo::create(1.0f, 0.66f),
            nullptr
        );
        crownIcon->runAction(CCRepeatForever::create(innerPulse));

        // Glow aditivo detrás del icono
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

    // ── Estrellas Orbitantes (Efecto premium interactivo) ──
    auto orbitNode = CCNode::create();
    orbitNode->setID("orbit-node");
    badgeGroup->addChild(orbitNode, 4);

    // Rotación infinita del nodo de órbita
    orbitNode->runAction(CCRepeatForever::create(CCRotateBy::create(5.0f, 360.f)));

    // Añadir 4 estrellitas a intervalos de 90 grados
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

            // Cada estrella rota en sí misma en sentido opuesto
            oStar->runAction(CCRepeatForever::create(CCRotateBy::create(1.5f, -360.f)));
        }
    }

    // Flotación arriba/abajo de todo el badgeGroup (Badge + Órbita + Brillo)
    auto floatAction = CCSequence::create(
        CCMoveBy::create(1.8f, {0, 4.f}),
        CCMoveBy::create(1.8f, {0, -4.f}),
        nullptr
    );
    badgeGroup->runAction(CCRepeatForever::create(floatAction));

    // etiqueta "Exclusive"
    auto exclusiveLbl = CCLabelBMFont::create("Exclusive", "bigFont.fnt");
    exclusiveLbl->setScale(0.3f);
    exclusiveLbl->setColor({255, 205, 100});
    exclusiveLbl->setPosition({panelX, panelY - 30.f});
    exclusiveLbl->setID("exclusive-label");
    m_badgePanelContainer->addChild(exclusiveLbl, 4);

    // Animación de pulso para la etiqueta Exclusive
    auto textPulse = CCSequence::create(
        CCScaleTo::create(1.2f, 0.315f),
        CCScaleTo::create(1.2f, 0.285f),
        nullptr
    );
    exclusiveLbl->runAction(CCRepeatForever::create(textPulse));

    // texto decorativo bajo el badge
    auto badgeDesc = CCLabelBMFont::create("Shown on your profile", "chatFont.fnt");
    badgeDesc->setScale(0.35f);
    badgeDesc->setColor({180, 160, 220});
    badgeDesc->setPosition({panelX, panelY - 48.f});
    badgeDesc->setID("badge-description");
    m_badgePanelContainer->addChild(badgeDesc, 4);

    // Animación de entrada con frenado elástico
    m_badgePanelContainer->runAction(CCEaseBackOut::create(CCMoveTo::create(1.0f, {0, 0})));
}

// ── benefits panel (derecha) ─────────────────────────────

void PaimonSupportLayer::createBenefitsPanel() {
    auto winSize = CCDirector::get()->getWinSize();

    float panelW = 220.f;
    float panelH = 150.f;
    float panelX = winSize.width * 0.68f;
    float panelY = winSize.height * 0.52f;

    // Contenedor para el panel derecho (para animarlo desde fuera de la pantalla)
    m_benefitsPanelContainer = CCNode::create();
    m_benefitsPanelContainer->setPosition({winSize.width - panelX + panelW, 0}); // Empezar fuera a la derecha
    m_benefitsPanelContainer->setID("benefits-panel-container");
    this->addChild(m_benefitsPanelContainer, 3);

    // fondo panel beneficios - Estilo glassmorphism profundo
    auto panelBg = paimon::SpriteHelper::createColorPanel(panelW, panelH, {15, 10, 32}, 205);
    panelBg->setPosition({panelX - panelW / 2, panelY - panelH / 2});
    panelBg->setID("panel-bg");
    m_benefitsPanelContainer->addChild(panelBg, 1);

    // Borde de neón rosado/violeta pulsante usando standard safeCreateScale9
    auto neonGlow = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (neonGlow) {
        neonGlow->setContentSize({panelW + 8.f, panelH + 8.f});
        neonGlow->setPosition({panelX, panelY});
        neonGlow->setColor({255, 110, 180});
        neonGlow->setOpacity(80);
        neonGlow->setID("neon-glow");
        m_benefitsPanelContainer->addChild(neonGlow, 2);

        // Animación de pulso continuo de escala y opacidad
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


    // Borde principal
    auto border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png");
    if (border) {
        border->setContentSize({panelW + 6.f, panelH + 6.f});
        border->setPosition({panelX, panelY});
        border->setColor({255, 120, 180}); // Color a juego con el neón rosado
        border->setID("border");
        m_benefitsPanelContainer->addChild(border, 3);
    }

    // titulo
    auto benefitsTitle = CCLabelBMFont::create("Supporter Benefits", "goldFont.fnt");
    benefitsTitle->setScale(0.38f);
    benefitsTitle->setPosition({panelX, panelY + panelH / 2 - 14.f});
    benefitsTitle->setID("benefits-title");
    m_benefitsPanelContainer->addChild(benefitsTitle, 4);

    // Estructura de fila de beneficio
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

        // Contenedor para la fila entera para aplicar la animación staggered (en cascada)
        auto rowNode = CCNode::create();
        rowNode->setPosition({15.f, 0}); // Empezar desplazado a la derecha
        rowNode->setID(fmt::format("benefit-row-{}", i));
        m_benefitsPanelContainer->addChild(rowNode, 4);

        // icono
        auto icon = CCSprite::createWithSpriteFrameName(benefits[i].icon);
        if (icon) {
            icon->setScale(0.32f);
            icon->setPosition({leftX, rowY});
            icon->setColor(benefits[i].color);
            icon->setID("icon");
            rowNode->addChild(icon, 3);

            // ── Micro-animaciones Únicas e Interactivas por Icono ──
            if (i == 0) {
                // Fila 0 (Badge Star): Rotación lenta continua
                icon->runAction(CCRepeatForever::create(CCRotateBy::create(2.5f, 360.f)));
            }
            else if (i == 1) {
                // Fila 1 (Priority Checkmark): Escala pulsante elástica
                auto chkPulse = CCSequence::create(
                    CCScaleTo::create(0.8f, 0.36f),
                    CCScaleTo::create(0.8f, 0.28f),
                    nullptr
                );
                icon->runAction(CCRepeatForever::create(chkPulse));
            }
            else if (i == 2) {
                // Fila 2 (VIP List Star): Parpadeo de brillo (opacidad suave)
                auto glowPulse = CCSequence::create(
                    CCFadeTo::create(0.9f, 255),
                    CCFadeTo::create(0.9f, 100),
                    nullptr
                );
                icon->runAction(CCRepeatForever::create(glowPulse));
            }
            else if (i == 3) {
                // Fila 3 (GIF Magic): Rotación rápida oscilante (efecto wiggle)
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
                // Fila 4 (Lock Customization): Balanceo vertical
                auto bob = CCSequence::create(
                    CCMoveBy::create(0.8f, {0, 2.5f}),
                    CCMoveBy::create(0.8f, {0, -2.5f}),
                    nullptr
                );
                icon->runAction(CCRepeatForever::create(bob));
            }
            else if (i == 5) {
                // Fila 5 (Early Access Heart): Latido realístico de doble pulso (lub-dub)
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

        // texto
        auto lbl = CCLabelBMFont::create(benefits[i].text, "chatFont.fnt");
        lbl->setScale(0.42f);
        lbl->setAnchorPoint({0, 0.5f});
        lbl->setPosition({leftX + 14.f, rowY});
        lbl->setColor({220, 220, 240});
        lbl->setID("label");
        rowNode->addChild(lbl, 3);

        // Animación staggered: cada fila tiene un retardo progresivo, aparece deslizándose y con fade
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

    // Animación de entrada del panel con frenado elástico
    m_benefitsPanelContainer->runAction(CCEaseBackOut::create(CCMoveTo::create(1.0f, {0, 0})));
}

// ── thank you section ────────────────────────────────────

void PaimonSupportLayer::createThankYouSection() {
    auto winSize = CCDirector::get()->getWinSize();
    float sectionY = winSize.height * 0.20f;

    // Crear un contenedor para la sección de agradecimiento
    auto thankYouContainer = CCNode::create();
    thankYouContainer->setID("thank-you-container");
    this->addChild(thankYouContainer, 2);

    // linea separadora sutil dorada/rosa
    auto separator = CCLayerColor::create({255, 120, 180, 45});
    separator->setContentSize({winSize.width * 0.6f, 1.5f});
    separator->setPosition({winSize.width * 0.2f, sectionY + 22.f});
    separator->setScaleX(0); // Empezar encogida horizontalmente
    separator->setID("separator");
    thankYouContainer->addChild(separator, 2);

    // mensaje principal
    auto msg = CCLabelBMFont::create(
        "Every donation helps me dedicate more time\nto improving Paimbnails for the community.",
        "chatFont.fnt"
    );
    msg->setScale(0.48f);
    msg->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
    msg->setPosition({winSize.width / 2, sectionY - 8.f}); // Empezar un poco desplazado abajo
    msg->setColor({200, 190, 230});
    msg->setOpacity(0); // Empezar invisible
    msg->setID("thank-you-message");
    thankYouContainer->addChild(msg, 2);

    // corazoncito
    auto heart = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
    if (heart) {
        heart->setScale(0.4f);
        heart->setPosition({winSize.width / 2, sectionY + 30.f});
        heart->setColor({255, 80, 120});
        heart->setOpacity(0); // Empezar invisible
        heart->setID("heart-icon");
        thankYouContainer->addChild(heart, 3);

        // latido lub-dub cinemático
        auto beat = CCSequence::create(
            CCScaleTo::create(0.18f, 0.52f),
            CCScaleTo::create(0.18f, 0.38f),
            CCScaleTo::create(0.18f, 0.48f),
            CCScaleTo::create(0.68f, 0.40f),
            nullptr
        );
        heart->runAction(CCRepeatForever::create(beat));
        
        // Halo de brillo detrás del corazón
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

    // Animación de entrada de la sección
    // 1. Expansión horizontal de la barra divisoria
    separator->runAction(CCSequence::create(
        CCDelayTime::create(0.7f),
        CCEaseBackOut::create(CCScaleTo::create(0.8f, 1.0f, 1.0f)),
        nullptr
    ));

    // 2. Aparición con fade y desplazamiento hacia arriba del mensaje
    msg->runAction(CCSequence::create(
        CCDelayTime::create(0.9f),
        CCSpawn::create(
            CCEaseBackOut::create(CCMoveTo::create(0.6f, {winSize.width / 2, sectionY})),
            CCFadeTo::create(0.5f, 255),
            nullptr
        ),
        nullptr
    ));

    // 3. Aparición del corazoncito
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

// ── buttons ──────────────────────────────────────────────

void PaimonSupportLayer::createButtons() {
    auto winSize = CCDirector::get()->getWinSize();

    // ── Menu contenedor del botón Donate (principal, grande, dorado) ──
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

    // Icono de corazón al lado del texto
    auto heartIcon = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
    if (heartIcon) {
        heartIcon->setScale(0.35f);
        heartIcon->setPosition({donateSpr->getContentWidth() - 22.f, donateSpr->getContentHeight() / 2});
        heartIcon->setColor({255, 100, 130});
        heartIcon->setID("heart-icon");
        donateSpr->addChild(heartIcon, 10);

        // Latido rápido para incitar
        auto quickBeat = CCSequence::create(
            CCScaleTo::create(0.12f, 0.44f),
            CCScaleTo::create(0.12f, 0.32f),
            CCScaleTo::create(0.12f, 0.40f),
            CCScaleTo::create(0.42f, 0.35f),
            nullptr
        );
        heartIcon->runAction(CCRepeatForever::create(quickBeat));
    }

    // Brillo aditivo pulsante del botón (Ripple Effect)
    auto btnGlow = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
    if (btnGlow) {
        btnGlow->setPosition({winSize.width / 2, 28.f});
        btnGlow->setColor({255, 180, 0});
        btnGlow->setOpacity(0);
        btnGlow->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        btnGlow->setID("donate-ripple-glow");
        this->addChild(btnGlow, 4);

        // Ripple infinito: expande y desvanece
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

    // Animación de entrada elástica de Donate (pop-in con retraso)
    donateBtn->setScale(0);
    donateBtn->runAction(CCSequence::create(
        CCDelayTime::create(1.1f),
        CCEaseElasticOut::create(CCScaleTo::create(0.9f, 1.0f)),
        CCCallFunc::create(this, callfunc_selector(PaimonSupportLayer::createParticles)), // Iniciar partículas después
        nullptr
    ));

    // Latido suave y constante del botón completo
    auto btnBreathe = CCSequence::create(
        CCScaleTo::create(1.2f, 1.03f),
        CCScaleTo::create(1.2f, 0.97f),
        nullptr
    );
    // Ejecutar después de la animación de entrada
    donateBtn->runAction(CCSequence::create(
        CCDelayTime::create(2.0f),
        CCRepeatForever::create(btnBreathe),
        nullptr
    ));

    // ── Botón Back ──
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

    // Animación de entrada de Back (se desliza desde la izquierda)
    backBtn->setPosition({-winSize.width / 2 - 50.f, winSize.height / 2 - 25.f}); // Empezar fuera
    backBtn->runAction(CCSequence::create(
        CCDelayTime::create(0.3f),
        CCEaseBackOut::create(CCMoveTo::create(0.7f, {-winSize.width / 2 + 25.f, winSize.height / 2 - 25.f})),
        nullptr
    ));
}

// ── particles ────────────────────────────────────────────

void PaimonSupportLayer::createParticles() {
    auto winSize = CCDirector::get()->getWinSize();
    static std::mt19937 rng(std::random_device{}());

    // Generar 16 partículas iniciales distribuidas por la pantalla para que no empiece vacía
    for (int i = 0; i < 16; i++) {
        // Elegir tipo de partícula aleatoria
        std::uniform_int_distribution<int> typeDist(0, 2);
        int type = typeDist(rng);

        cocos2d::CCSprite* particle = nullptr;
        cocos2d::ccColor3B color = {255, 255, 255};
        float baseScale = 0.1f;

        if (type == 0) {
            particle = CCSprite::createWithSpriteFrameName("GJ_bigStar_001.png");
            color = {255, 220, 70}; // Dorado
            baseScale = 0.12f;
        } else if (type == 1) {
            particle = CCSprite::createWithSpriteFrameName("gj_heartOn_001.png");
            color = {255, 95, 135}; // Rosa
            baseScale = 0.14f;
        } else {
            particle = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png");
            color = {100, 210, 255}; // Celeste / Sparkle
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

        // Flotan hacia arriba suavemente
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

        // Rotación continua
        float rotSpeed = rotDist(rng);
        particle->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, rotSpeed)));
    }

    // Programar la generación regular de partículas cada 3.5 segundos (más frecuente y dinámico)
    this->unschedule(schedule_selector(PaimonSupportLayer::spawnParticles));
    this->schedule(schedule_selector(PaimonSupportLayer::spawnParticles), 3.5f);
}

void PaimonSupportLayer::spawnParticles(float dt) {
    auto winSize = CCDirector::get()->getWinSize();
    static std::mt19937 rng(std::random_device{}());

    // Spawnea 6 partículas nuevas desde el fondo cada intervalo
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

        // Flotan hacia arriba suavemente
        float duration = durDist(rng);
        float driftX = driftDist(rng);
        float targetY = winSize.height + 20.f;

        auto move = CCMoveTo::create(duration, {startX + driftX, targetY});
        auto fadeOut = CCFadeTo::create(duration * 0.8f, 0);
        auto spawn = CCSpawn::create(move, fadeOut, nullptr);
        auto cleanup = CCCallFunc::create(particle, callfunc_selector(CCNode::removeFromParent));
        particle->runAction(CCSequence::create(spawn, cleanup, nullptr));

        // Rotación continua
        float rotSpeed = rotDist(rng);
        particle->runAction(CCRepeatForever::create(CCRotateBy::create(2.f, rotSpeed)));
    }
}

// ── navigation ───────────────────────────────────────────

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

