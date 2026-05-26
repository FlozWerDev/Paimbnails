#include "QuickHubRadial.hpp"
#include "../services/QuickHubManager.hpp"
#include "../data/QuickHubCategories.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../blur/PopupBlurService.hpp"
#include "../../../layers/PaimonHubLayer.hpp"
#include "../../../layers/PaiConfigLayer.hpp"
#include "../../../layers/PaimonSupportLayer.hpp"
#include "../../../features/settings-panel/services/SettingsPanelManager.hpp"
#include "../../../features/backgrounds/ui/BackgroundConfigPopup.hpp"
#include "../../../features/discord-presence/ui/DiscordConfigPopup.hpp"
#include "../../../features/pet/ui/PetConfigPopup.hpp"
#include "../../../features/pet/ui/PaimonShopPopup.hpp"
#include "../../../features/cursor/ui/CursorConfigPopup.hpp"
#include "../../../features/progressbar/ui/ProgressBarConfigPopup.hpp"
#include "../../../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../../../features/custom-slider/ui/CustomSliderPopup.hpp"
#include "../../../features/profiles/ui/ProfilePicEditorPopup.hpp"
#include "../../../features/profile-music/ui/ProfileMusicPopup.hpp"
#include "../../../features/menu-music/ui/MenuMusicPopup.hpp"
#include "../../../features/menu-music/ui/MenuMusicLibraryPopup.hpp"
#include "../../../features/menu-music/ui/MenuMusicPlaylistsPopup.hpp"
#include "../../../features/paidraw/PaiDrawUI.hpp"

#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::quickhub {

QuickHubRadial* QuickHubRadial::s_instance = nullptr;

// ─── Static interface ────────────────────────────────────────────────────────

bool QuickHubRadial::isOpen() {
    return s_instance != nullptr;
}

void QuickHubRadial::openRadial() {
    if (s_instance) return;

    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    auto radial = QuickHubRadial::create();
    if (!radial) return;

    scene->addChild(radial, 99998);
    s_instance = radial;

    // Aplicar el mismo blur dinamico que usan los popups del mod (respeta
    // los settings del usuario: estilo gaussian/paimonblur, intensidad,
    // oscuridad y fade-in).
    bool blurApplied = paimon::popupblur::captureAndApply(radial);

    // Fallback: si el blur esta deshabilitado en settings, aplicar un overlay
    // oscuro para que el radial sea legible sobre el fondo.
    if (!blurApplied) {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto fallback = CCLayerColor::create({8, 10, 18, 0});
        fallback->setContentSize(winSize);
        fallback->setID("paimon-radial-fallback-overlay"_spr);
        radial->addChild(fallback, -1);
        fallback->runAction(CCFadeTo::create(0.25f, 200));
    }
}

void QuickHubRadial::closeRadial() {
    if (!s_instance) return;
    s_instance->animateClose();
}

// ─── Create ──────────────────────────────────────────────────────────────────

QuickHubRadial* QuickHubRadial::create() {
    auto ret = new QuickHubRadial();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// ─── Init ────────────────────────────────────────────────────────────────────

bool QuickHubRadial::init() {
    if (!CCLayer::init()) return false;

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-1000);
    this->setKeypadEnabled(true);

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    // El blur de fondo lo aplica openRadial() via paimon::popupblur::captureAndApply
    // (mismo sistema que usan los popups dinamicos del mod).
    m_blurBg = nullptr;
    m_darkOverlay = nullptr;

    // ── Center label (muestra nombre de opcion en hover) ──
    m_centerLabel = CCNode::create();
    m_centerLabel->setPosition(winSize / 2.f);
    this->addChild(m_centerLabel, 10);

    auto label = CCLabelBMFont::create("", "goldFont.fnt");
    label->setScale(0.7f);
    label->setTag(100);
    m_centerLabel->addChild(label);

    // ── Build radial items ──
    buildRadialItems();
    animateOpen();

    // Tracking del mouse por frame: actualiza el hover sin necesidad de
    // mantener presionado. Esto permite que el usuario suelte Ctrl, mueva
    // el mouse libremente sobre los iconos, y clickee con el mouse para
    // ejecutar la accion.
    this->scheduleUpdate();

    QuickHubManager::get().setRadialOpen(true);

    return true;
}

void QuickHubRadial::onExit() {
    // Safety net: si el radial se removio sin pasar por animateClose
    // (scene transition, removeFromParent externo, etc), limpiamos el blur
    // del popupblur. Si ya se limpio, esto es un no-op seguro.
    paimon::popupblur::cleanup(this);

    CCLayer::onExit();
    if (s_instance == this) {
        s_instance = nullptr;
    }
    QuickHubManager::get().setRadialOpen(false);
}

// ─── Mouse hover tracking (sin necesidad de mantener apretado) ──────────────
//
// Lee la posicion del mouse cada frame y actualiza el item resaltado. Esto
// se ejecuta SIEMPRE que el radial este vivo, incluso si el usuario solto
// Ctrl. Asi puedes mover el mouse libremente, ver los nombres en hover, y
// clickear con el boton izquierdo del mouse para ejecutar la opcion.

void QuickHubRadial::update(float dt) {
    // geode::cocos::getMousePos() es la API oficial de Geode para obtener
    // la posicion del mouse directamente en coordenadas cocos2d (design
    // space, bottom-left origin). Internamente normaliza por frameSize
    // (pixeles reales de ventana) y escala a winSize (design resolution),
    // aplicando tambien el flip de Y — justo lo que necesitamos para que
    // el hit-test coincida con la posicion visual de los items,
    // independientemente de la resolucion real de la ventana (fullscreen
    // 1920x1080 vs design ~569x320 o lo que GD use).
    CCPoint mouseWorld = geode::cocos::getMousePos();
    int hovered = getHoveredIndex(mouseWorld);
    updateHover(hovered);
}

// ─── Build Items ─────────────────────────────────────────────────────────────

void QuickHubRadial::buildRadialItems() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    CCPoint center = winSize / 2.f;

    auto activeIds = QuickHubManager::get().getActiveOptions();
    auto allOpts = getAllAvailableOptions();

    int count = static_cast<int>(activeIds.size());
    if (count == 0) return;

    float radius = 90.f;
    float angleStep = 360.f / static_cast<float>(count);

    for (int i = 0; i < count; i++) {
        std::string id = activeIds[i];

        // Buscar la definicion
        RadialOptionDef const* def = nullptr;
        for (auto const& opt : allOpts) {
            if (opt.id == id) { def = &opt; break; }
        }
        if (!def) continue;

        float angleDeg = -90.f + angleStep * static_cast<float>(i);
        float angleRad = angleDeg * (static_cast<float>(M_PI) / 180.f);
        float x = center.x + cosf(angleRad) * radius;
        float y = center.y + sinf(angleRad) * radius;

        // Contenedor del item (maneja posicion durante open/close)
        auto itemNode = CCNode::create();
        itemNode->setPosition(center); // empieza en el centro para la animacion
        this->addChild(itemNode, 5);

        // Inner node — hijo que se encarga SOLO de la escala (open + hover).
        // Separar el scale del move evita que stopAllActions() en hover mate
        // la animacion de movimiento de apertura: cuando se reabre el radial
        // y el mouse esta cerca del centro, el hover se disparaba en el primer
        // frame y los items quedaban apilados en el centro (bug conocido).
        auto inner = CCNode::create();
        inner->setAnchorPoint({0.5f, 0.5f});
        inner->setScale(0.f);
        itemNode->addChild(inner);

        // ── 1) Card oscura con borde azul-negro ──
        // Fondo negro semi-transparente con un borde del color azul muy
        // oscuro (casi negro). Da apariencia solida al boton.
        constexpr float kCardSize = 46.f;
        constexpr float kCardRadius = 23.f; // circulo perfecto
        cocos2d::ccColor4F cardFill   = {0.04f, 0.05f, 0.08f, 0.85f}; // negro semi-translucido
        cocos2d::ccColor4F cardBorder = {0.08f, 0.11f, 0.18f, 1.00f}; // azul oscuro tendiendo a negro

        auto card = paimon::SpriteHelper::createRoundedRect(
            kCardSize, kCardSize, kCardRadius,
            cardFill, cardBorder, 1.4f
        );
        if (card) {
            card->setPosition({-kCardSize * 0.5f, -kCardSize * 0.5f});
            inner->addChild(card, 0);
        }

        // ── 2) Aro fino del color de la categoria (alrededor del card) ──
        // Resalta cada boton con su color unico sin usar relleno.
        constexpr float kRingSize = 52.f;
        constexpr float kRingRadius = 26.f;
        cocos2d::ccColor4F ringCol = {
            def->color.r / 255.f,
            def->color.g / 255.f,
            def->color.b / 255.f,
            0.9f
        };
        auto ring = paimon::SpriteHelper::createRoundedRectOutline(
            kRingSize, kRingSize, kRingRadius,
            ringCol, 1.2f
        );
        if (ring) {
            ring->setPosition({-kRingSize * 0.5f, -kRingSize * 0.5f});
            inner->addChild(ring, 1);
        }

        // ── 3) Icono recortado a circulo (esconde el aro blanco del sprite GD) ──
        // CCClippingNode con stencil circular. El recorte esconde la
        // perimetria blanca que trae el sprite GJ_*Btn_001 y deja solo el
        // simbolo interno con sus colores originales.
        constexpr float kClipSize = 32.f;
        constexpr float kClipRadius = kClipSize * 0.5f;

        auto icon = CCSprite::createWithSpriteFrameName(def->icon.c_str());
        if (!icon) {
            icon = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        }
        if (icon) {
            // Sin setColor — preserva los colores originales del sprite GD.
            auto stencil = paimon::SpriteHelper::createRoundedRectStencil(
                kClipSize, kClipSize, kClipRadius);
            auto clip = stencil ? CCClippingNode::create(stencil) : nullptr;

            if (clip) {
                clip->setAlphaThreshold(0.05f);
                clip->setContentSize({kClipSize, kClipSize});
                clip->setAnchorPoint({0.5f, 0.5f});
                clip->setPosition({0.f, 0.f});

                icon->setAnchorPoint({0.5f, 0.5f});
                icon->setPosition({kClipSize * 0.5f, kClipSize * 0.5f});
                icon->setScale(0.72f);
                clip->addChild(icon, 0);

                inner->addChild(clip, 2);
            } else {
                icon->setAnchorPoint({0.5f, 0.5f});
                icon->setPosition({0.f, 0.f});
                icon->setScale(0.55f);
                inner->addChild(icon, 2);
            }
        }

        // item.glowNode queda en nullptr — la animacion de hover se limita
        // a escalar el item y mostrar el nombre en el label central dorado.

        RadialItem item;
        item.id = id;
        item.node = itemNode;
        item.inner = inner;
        item.glowNode = nullptr;
        item.position = ccp(x, y);
        item.angle = angleDeg;
        m_items.push_back(item);
    }
}

// ─── Animations ──────────────────────────────────────────────────────────────

void QuickHubRadial::animateOpen() {
    // El fade-in del blur lo gestiona paimon::popupblur::captureAndApply.

    // Animar items desde el centro hacia sus posiciones.
    // Move va en `node`, scale en `inner` — asi el hover (que pega
    // stopAllActions sobre `inner`) no puede matar el movimiento.
    for (size_t i = 0; i < m_items.size(); i++) {
        auto& item = m_items[i];
        float delay = 0.03f * static_cast<float>(i);

        if (item.node) {
            item.node->stopAllActions();
            item.node->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCEaseBackOut::create(CCMoveTo::create(0.3f, item.position)),
                nullptr
            ));
        }
        if (item.inner) {
            item.inner->stopAllActions();
            item.inner->setScale(0.f);
            item.inner->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCEaseBackOut::create(CCScaleTo::create(0.3f, 1.0f)),
                nullptr
            ));
        }
    }
}

void QuickHubRadial::animateClose() {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    CCPoint center = winSize / 2.f;

    // Pedir al PopupBlurService que haga fade-out del blur en sync con la
    // animacion de cierre. Usamos la misma duracion total que las items.
    paimon::popupblur::cleanupWithFade(this, 0.25f);

    // Animar items de vuelta al centro
    float maxDelay = 0.f;
    for (size_t i = 0; i < m_items.size(); i++) {
        auto& item = m_items[i];
        float delay = 0.02f * static_cast<float>(i);
        maxDelay = std::max(maxDelay, delay + 0.2f);

        if (item.node) {
            item.node->stopAllActions();
            item.node->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCEaseBackIn::create(CCMoveTo::create(0.2f, center)),
                nullptr
            ));
        }
        if (item.inner) {
            item.inner->stopAllActions();
            item.inner->runAction(CCSequence::create(
                CCDelayTime::create(delay),
                CCScaleTo::create(0.2f, 0.f),
                nullptr
            ));
        }
    }

    // Remover despues de la animacion
    this->runAction(CCSequence::create(
        CCDelayTime::create(maxDelay + 0.05f),
        CCCallFunc::create(this, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

// ─── Touch handling ──────────────────────────────────────────────────────────
//
// El hover lo gestiona update() leyendo el mouse cada frame.
// Para el click usamos el flow natural: ccTouchBegan acepta el touch, y
// la accion se ejecuta en ccTouchEnded — asi mantener presionado mientras
// pasas sobre items NO ejecuta nada hasta soltar. Sigue mostrando el label
// dorado en el centro como simple hover.

bool QuickHubRadial::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    return true; // consumir el touch, ejecucion se decide al soltar
}

void QuickHubRadial::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    // En mobile/touch, el dedo arrastrandose sobre los items actualiza el
    // hover. En desktop el hover ya lo hace update() por mouse.
    auto worldPos = touch->getLocation();
    int hovered = getHoveredIndex(worldPos);
    updateHover(hovered);
}

void QuickHubRadial::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    auto worldPos = touch->getLocation();
    int hovered = getHoveredIndex(worldPos);

    if (hovered >= 0) {
        executeOption(hovered);
    } else {
        // Click/touch fuera de cualquier item — cerrar
        animateClose();
    }
}

void QuickHubRadial::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    // No-op
}

void QuickHubRadial::keyBackClicked() {
    animateClose();
}

// ─── Hover detection ─────────────────────────────────────────────────────────

int QuickHubRadial::getHoveredIndex(CCPoint const& worldPos) {
    float hitRadius = 30.f; // radio de deteccion por item

    for (size_t i = 0; i < m_items.size(); i++) {
        auto& item = m_items[i];
        CCPoint itemWorld = item.node->getParent()->convertToWorldSpace(item.node->getPosition());
        float dist = ccpDistance(worldPos, itemWorld);
        if (dist <= hitRadius) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void QuickHubRadial::updateHover(int index) {
    if (index == m_hoveredIndex) return;
    m_hoveredIndex = index;

    // Animacion de hover por item: solo escala. Se aplica sobre `inner`
    // para no interferir con la animacion de move (open/close) que vive en
    // `node`.
    for (size_t i = 0; i < m_items.size(); i++) {
        auto& item = m_items[i];
        if (!item.inner) continue;
        bool isHovered = (static_cast<int>(i) == index);

        item.inner->stopAllActions();
        if (isHovered) {
            item.inner->runAction(CCEaseSineOut::create(CCScaleTo::create(0.12f, 1.2f)));
        } else {
            item.inner->runAction(CCEaseSineOut::create(CCScaleTo::create(0.12f, 1.0f)));
        }
    }

    // El label dorado central muestra el nombre del item bajo el cursor.
    if (m_centerLabel) {
        auto label = typeinfo_cast<CCLabelBMFont*>(m_centerLabel->getChildByTag(100));
        if (label) {
            if (index >= 0 && index < static_cast<int>(m_items.size())) {
                auto allOpts = getAllAvailableOptions();
                for (auto const& opt : allOpts) {
                    if (opt.id == m_items[index].id) {
                        label->setString(opt.name.c_str());
                        label->setColor(opt.color);
                        break;
                    }
                }
            } else {
                label->setString("");
            }
        }
    }
}

// ─── Execute option ──────────────────────────────────────────────────────────
//
// Cada id corresponde a una opcion definida en QuickHubCategories.hpp. Si
// agregas un nuevo id alli y olvidas mapearlo aqui, el radial mostrara la
// opcion pero no hara nada al ejecutarla.

void QuickHubRadial::executeOption(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;

    std::string id = m_items[index].id;

    // Cerrar el radial primero
    animateClose();

    // Ejecutar la accion despues de un breve delay para que el cierre se vea bien.
    Loader::get()->queueInMainThread([id]() {
        // ── Settings panel categorias ──
        // Indices coinciden con paimon::settings_ui::getAllGroups():
        //   0 general, 1 thumbnails, 2 levelinfo, 3 audio,
        //   4 backgrounds, 5 extras, 6 discord
        if (id == "settings-general")          { SettingsPanelManager::get().open(0); return; }
        if (id == "settings-thumbnails")       { SettingsPanelManager::get().open(1); return; }
        if (id == "settings-levelinfo")        { SettingsPanelManager::get().open(2); return; }
        if (id == "settings-audio")            { SettingsPanelManager::get().open(3); return; }
        if (id == "settings-backgrounds")      { SettingsPanelManager::get().open(4); return; }
        if (id == "settings-extras")           { SettingsPanelManager::get().open(5); return; }
        if (id == "settings-discord")          { SettingsPanelManager::get().open(6); return; }

        // ── Aliases legacy: ids antiguos guardados en saved values ──
        // Si algun usuario tenia la config v1 guardada, los redirigimos al
        // panel correspondiente para no romper su layout.
        if (id == "general")      { SettingsPanelManager::get().open(0); return; }
        if (id == "thumbnails")   { SettingsPanelManager::get().open(1); return; }
        if (id == "level")        { SettingsPanelManager::get().open(2); return; }
        if (id == "audio")        { SettingsPanelManager::get().open(3); return; }
        if (id == "extras")       { SettingsPanelManager::get().open(5); return; }
        if (id == "quick-toggle") { SettingsPanelManager::get().open(0); return; }
        if (id == "backgrounds")  {
            if (auto popup = BackgroundConfigPopup::create()) popup->show();
            return;
        }
        if (id == "discord")      {
            if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show();
            return;
        }

        // ── Popups directos ──
        if (id == "backgrounds-editor") {
            if (auto popup = BackgroundConfigPopup::create()) popup->show();
            return;
        }
        if (id == "transitions") {
            if (auto popup = TransitionConfigPopup::create()) popup->show();
            return;
        }
        if (id == "discord-config") {
            if (auto popup = paimon::discord::DiscordConfigPopup::create()) popup->show();
            return;
        }
        if (id == "pet-config") {
            if (auto popup = PetConfigPopup::create()) popup->show();
            return;
        }
        if (id == "cursor-config") {
            if (auto popup = CursorConfigPopup::create()) popup->show();
            return;
        }
        if (id == "slider-config") {
            if (auto popup = paimon::slider::CustomSliderPopup::create()) popup->show();
            return;
        }
        if (id == "progressbar-config") {
            if (auto popup = ProgressBarConfigPopup::create()) popup->show();
            return;
        }
        if (id == "profile-pic-editor") {
            if (auto popup = ProfilePicEditorPopup::create()) popup->show();
            return;
        }

        // ── Popups de musica ──
        if (id == "menu-music") {
            if (auto popup = paimon::menumusic::MenuMusicPopup::create()) popup->show();
            return;
        }
        if (id == "menu-music-library") {
            if (auto popup = paimon::menumusic::MenuMusicLibraryPopup::create()) popup->show();
            return;
        }
        if (id == "menu-music-playlists") {
            if (auto popup = paimon::menumusic::MenuMusicPlaylistsPopup::create()) popup->show();
            return;
        }
        if (id == "profile-music") {
            auto* acc = GJAccountManager::sharedState();
            int accountID = acc ? acc->m_accountID : 0;
            if (accountID > 0) {
                if (auto popup = ProfileMusicPopup::create(accountID)) popup->show();
            } else {
                PaimonNotify::create("Necesitas iniciar sesion.", NotificationIcon::Warning)->show();
            }
            return;
        }

        // ── Pet shop ──
        if (id == "pet-shop") {
            if (auto popup = PaimonShopPopup::create()) popup->show();
            return;
        }

        // ── Layers / scenes ──
        if (id == "hub") {
            if (auto scene = PaimonHubLayer::scene()) {
                CCDirector::sharedDirector()->pushScene(scene);
            }
            return;
        }
        if (id == "paidraw") {
            if (auto scene = paidraw::PaiDrawLobbyLayer::scene()) {
                CCDirector::sharedDirector()->pushScene(scene);
            }
            return;
        }
        if (id == "support") {
            if (auto scene = PaimonSupportLayer::scene()) {
                CCDirector::sharedDirector()->pushScene(scene);
            }
            return;
        }
        if (id == "full-config") {
            if (auto scene = PaiConfigLayer::scene()) {
                CCDirector::sharedDirector()->pushScene(scene);
            }
            return;
        }

        // Si llega aqui es porque hay un id en QuickHubCategories sin handler.
        log::warn("QuickHubRadial: id sin accion mapeada: '{}'", id);
    });
}

} // namespace paimon::quickhub
