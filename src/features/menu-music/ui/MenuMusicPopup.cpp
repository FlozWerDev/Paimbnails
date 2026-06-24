#include "MenuMusicPopup.hpp"

#include "components/VinylDisc.hpp"
#include "components/CoverBlurBackground.hpp"
#include "components/CoverHero.hpp"
#include "MenuMusicLibraryPopup.hpp"
#include "MenuMusicAddPopup.hpp"
#include "MenuMusicPlaylistsPopup.hpp"
#include "ExternalSongsPopup.hpp"

#include "../services/MenuMusicCopy.hpp"
#include "../services/MenuMusicLibrary.hpp"
#include "../services/MenuMusicPlayer.hpp"
#include "../services/SongCoverCache.hpp"
#include "../services/MenuMusicCoverLog.hpp"

#include "../../menu-loop/services/MenuLoopManager.hpp"
#include "../../menu-loop/services/MenuLoopControl.hpp"

#include "components/NowPlayingToast.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../blur/BlurSystem.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/SliderTouchLogic.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/loader/Dirs.hpp>

#include <algorithm>
#include <filesystem>
#include <random>
#include <system_error>

using namespace geode::prelude;

namespace paimon::menumusic {

// Constants
//
// Popup mas compacto y rectangular (~2:1). El "background" (blur)
// ocupa todo el interior; los elementos a la derecha del hero ganan
// anchura para que quepa un seek slider libre y los textos ya no
// compiten con botones.
// H=225 permite acomodar 5 filas apiladas en la columna derecha
// (title / subtitle / transport / seek / quick-actions) encima de
// los 4 botones grandes del bottom bar.
static constexpr float kPopupW = 460.f;
static constexpr float kPopupH = 225.f;
// El hero ocupa ~44% del ancho (menos que antes para dejar mas espacio
// a la columna derecha con el seek libre y los quick actions).
static constexpr float kHeroWidthRatio = 0.44f;
static constexpr float kHeroSkew = 20.f;

// Helper: crea un CCMenuItemSpriteExtra a partir del primer sprite frame
// disponible de una lista. Esto nos blinda contra cambios de nombres
// entre versiones de GD (GJ_playBtn_001.png vs GJ_playBtn2_001.png, etc.).
// Si ninguno existe, genera un fallback geometrico con un triangulo/cuadrado
// para que el boton NUNCA quede 0x0 y sin visual.
static CCSprite* createIconSpriteWithFallback(
    std::initializer_list<const char*> frames,
    float fallbackSize,
    ccColor4F fallbackColor,
    bool fallbackTriangle = false,
    bool flipX = false
) {
    for (auto* name : frames) {
        if (auto spr = paimon::SpriteHelper::safeCreateWithFrameName(name)) {
            if (flipX) spr->setFlipX(true);
            return spr;
        }
    }
    // Fallback: generar un sprite con un icono dibujado.
    auto node = CCSprite::create();
    if (!node) return nullptr;
    node->setContentSize({fallbackSize, fallbackSize});

    auto draw = PaimonDrawNode::create();
    if (fallbackTriangle) {
        // Triangulo play (apuntando a la derecha si flipX=false)
        float x = flipX ? fallbackSize : 0.f;
        float w = flipX ? -fallbackSize : fallbackSize;
        CCPoint verts[3] = {
            ccp(x, 0.f),
            ccp(x, fallbackSize),
            ccp(x + w, fallbackSize / 2.f)
        };
        draw->drawPolygon(verts, 3, fallbackColor, 0, ccc4f(0, 0, 0, 0));
    } else {
        // Cuadrado (pause-like)
        CCPoint r[4] = {
            ccp(0, 0), ccp(fallbackSize, 0),
            ccp(fallbackSize, fallbackSize), ccp(0, fallbackSize)
        };
        draw->drawPolygon(r, 4, fallbackColor, 0, ccc4f(0, 0, 0, 0));
    }
    node->addChild(draw);
    return node;
}

MenuMusicPopup* MenuMusicPopup::create() {
    auto ret = new MenuMusicPopup();
    if (ret && ret->init(kPopupW, kPopupH)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MenuMusicPopup::init(float width, float height) {
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);

    MenuMusicLibrary::get().load();

    // The full-popup dark overlay (m_fullBg) was removed: the vanilla GD frame
    // (m_bgSprite) covers the exterior and the content clipper covers the interior
    // with the blurred image.

    // Backdrop fullscreen (blur de la portada ocupando toda la pantalla) —
    // se construye como hijo directo de `this` (el Popup), con zOrder
    // negativo para quedar detras de m_mainLayer. Imita el patron de
    // InfoLayer/LevelInfoLayer/Daily donde el fondo rebasa al popup.
    buildFullscreenBackdrop();

    // Mantener el fondo vanilla del popup visible para conservar el frame
    // marron clasico de GD. El blur del hero y el hero mismo se recortan
    // dentro de un master clipper que calca el frame interior del popup,
    // asi ningun hijo decorativo puede sobresalir los bordes.
    buildContentClipper();
    buildBlurBackground();
    buildTopBar();
    buildVinyl();         // construye el "hero" flush dentro del clipper
    buildInfoColumn();
    buildTransport();
    buildSeekBar();
    buildQuickActions();
    buildBottomBar();

    coverlog::info("[MenuMusicCover] popup opened — log file: {}",
        geode::utils::string::pathToString(coverlog::logFilePath()));
    refreshFromState();

    return true;
}

void MenuMusicPopup::onEnterTransitionDidFinish() {
    Popup::onEnterTransitionDidFinish();

    if (coverlog::isEnabled()) {
        static bool s_shownLogHint = false;
        if (!s_shownLogHint) {
            s_shownLogHint = true;
            auto const logPath = geode::utils::string::pathToString(coverlog::logFilePath());
            PaimonNotify::create(
                fmt::format("Cover debug log:\n{}", logPath),
                NotificationIcon::Info,
                4.f
            )->show();
        }
    }

    // Registrar listeners una vez el popup esta totalmente en escena.
    // Usamos un weak pattern via this-capture; si el popup se destruye,
    // removeListener se llama en onExit.
    m_libListenerToken = MenuMusicLibrary::get().addListener([this]() {
        this->onLibraryChanged();
    });
    m_playerListenerToken = MenuMusicPlayer::get().addListener(
        [this](const std::string& trackId) {
            this->onTrackChanged(trackId);
        });

    // Si el player ya tiene algo sonando, arrancar el disco decorativo.
    auto& player = MenuMusicPlayer::get();
    if (!player.isPaused() && player.currentTrack()) {
        if (m_hero) m_hero->startSpinning();
        if (m_disc) m_disc->startSpinning();
    }

    // Ticker para detectar cambios del menu-loop VANILLA (cuando el usuario
    // cambia de song con shuffle del menu principal mientras el popup sigue
    // abierto, o si venimos sin nuestro override activo). Cada 0.5s.
    this->schedule(
        schedule_selector(MenuMusicPopup::pollExternalSong), 0.5f);

    // Ticker del seek bar: 60ms da una UI fluida sin saturar el main thread.
    this->schedule(
        schedule_selector(MenuMusicPopup::tickSeekUpdate), 0.06f);

    this->schedule(
        schedule_selector(MenuMusicPopup::syncCoverChrome), 1.f);
}

void MenuMusicPopup::onExit() {
    SongCoverCache::get().cancelAllPending();
    m_pendingSongCoverID = 0;
    this->unschedule(schedule_selector(MenuMusicPopup::pollExternalSong));
    this->unschedule(schedule_selector(MenuMusicPopup::tickSeekUpdate));
    this->unschedule(schedule_selector(MenuMusicPopup::syncCoverChrome));
    if (m_libListenerToken) {
        MenuMusicLibrary::get().removeListener(m_libListenerToken);
        m_libListenerToken = 0;
    }
    if (m_playerListenerToken) {
        MenuMusicPlayer::get().removeListener(m_playerListenerToken);
        m_playerListenerToken = 0;
    }
    Popup::onExit();
}

// Build: full background (deshabilitado)
//
// Antes dibujaba un rectangulo negro casi opaco cubriendo todo el
// m_mainLayer para tapar huecos entre el clipper y el frame. El usuario
// pidio eliminarlo: ahora se deja como no-op para conservar la firma en
// el .hpp sin afectar el render.

void MenuMusicPopup::buildFullBackground() {
    // intencionalmente vacio.
}

// Build: content clipper
//
// Master clipper con esquinas redondeadas que calca el area interior del
// frame vanilla. Todo lo decorativo (blur, hero, gradiente del hero) se
// anide aqui para que nada pueda sobresalir los bordes del popup. El
// frame marron de GD (m_bgSprite) sigue siendo hijo directo de m_mainLayer
// detras de este clipper.

void MenuMusicPopup::buildContentClipper() {
    auto size = m_mainLayer->getContentSize();

    // Inset minimo: el background llena casi todo el interior del frame
    // vanilla. 3px por lado (antes 4) suma 1px extra en cada borde segun
    // pidio el usuario.
    const float inset = 3.f;
    // Radio de esquina minimo: el usuario pidio bordes casi en "L", bien
    // cerrados. 3px mantiene un anti-alias suavisimo para que no se vean
    // dientes de sierra sin abrir la curva.
    const float cornerRadius = 3.f;

    CCSize clipSize{size.width - inset * 2.f, size.height - inset * 2.f};

    auto stencil = paimon::SpriteHelper::createRoundedRectStencil(
        clipSize.width, clipSize.height, cornerRadius);
    m_contentClip = CCClippingNode::create();
    if (!m_contentClip) return;
    m_contentClip->setStencil(stencil);
    m_contentClip->setAlphaThreshold(0.05f);
    m_contentClip->setContentSize(clipSize);
    m_contentClip->setAnchorPoint({0.5f, 0.5f});
    // El frame de GD no esta perfectamente centrado en el contentSize del
    // mainLayer (hay 1px de asimetria por el borde inferior). Ajuste fino
    // pedido por el usuario: mover 1px a la izquierda respecto al centro.
    m_contentClip->setPosition({size.width / 2.f, size.height / 2.f});
    m_contentClip->setID("mm-content-clip"_spr);
    // zOrder 1 pone el contenido POR ENCIMA del m_bgSprite (default z=0)
    // pero por debajo del titulo (z>=5), los botones de transport (z=6) y
    // el boton de cerrar.
    m_mainLayer->addChild(m_contentClip, 1);
}

// Build: blur background

void MenuMusicPopup::buildBlurBackground() {
    if (!m_contentClip) return;
    auto size = m_contentClip->getContentSize();
    m_bg = CoverBlurBackground::create(size);
    if (!m_bg) return;
    m_bg->setAnchorPoint({0.5f, 0.5f});
    m_bg->setPosition(size / 2);
    // Sin overlay oscuro: el usuario queria ver la imagen limpia, solo
    // recortada por el content clipper redondeado del popup.
    // Anidamos dentro del clipper maestro. zOrder 0 dentro del clipper
    // (debajo del hero y del resto de decoraciones).
    m_contentClip->addChild(m_bg, 0);
}

// Build: fullscreen backdrop
//
// Fondo fullscreen (blur de la cover + dim) que vive como hijo directo
// del Popup (this), detras del m_mainLayer. Esto emula el look de
// LevelInfoLayer / InfoLayer / DailyLevelPage donde el thumbnail llena
// toda la pantalla y el popup interior (frame marron) se ve flotando
// encima. El blur concreto se aplica en applyFullscreenCover() cada
// vez que cambia la portada detectada.

void MenuMusicPopup::buildFullscreenBackdrop() {
    auto winSize = CCDirector::get()->getWinSize();

    // Contenedor que agrupa dim + sprite blurred para poder removerlo
    // y cambiarlo en conjunto.
    m_fullscreenBackdrop = CCNode::create();
    if (!m_fullscreenBackdrop) return;
    m_fullscreenBackdrop->setContentSize(winSize);
    m_fullscreenBackdrop->setAnchorPoint({0.f, 0.f});
    m_fullscreenBackdrop->setPosition({0.f, 0.f});
    m_fullscreenBackdrop->setID("mm-fullscreen-backdrop"_spr);
    // Posicion en m_mainLayer coords: el Popup centra m_mainLayer y
    // nosotros queremos cubrir la pantalla COMPLETA, asi que metemos el
    // backdrop como hijo directo de `this` (el Popup extiende CCLayer
    // que cubre toda la pantalla), no del m_mainLayer.
    this->addChild(m_fullscreenBackdrop, -1);

    // Sin dim oscuro fullscreen: el usuario quiere ver la imagen limpia
    // tambien en el fondo de pantalla detras del popup. El Popup base de
    // Geode sigue pintando su propio m_bgNode con fade-in, eso lo dejamos
    // para no romper la animacion de entrada.

    // El Popup base de Geode pinta su propio m_bgNode con un dim oscuro
    // cuando aparece; lo dejamos para mantener la animacion de entrada
    // (CCFadeIn del overlay negro).
}

// Apply fullscreen cover (blur)

void MenuMusicPopup::applyFullscreenCover(const std::string& coverPath) {
    if (!m_fullscreenBackdrop) return;

    // Incrementar generation para invalidar jobs async en vuelo.
    m_fullscreenBlurGen++;
    auto gen = m_fullscreenBlurGen;

    if (coverPath.empty()) {
        // Sin portada: limpiar sprite previo, dejar solo el dim.
        if (m_fullscreenBlur) {
            m_fullscreenBlur->removeFromParent();
            m_fullscreenBlur = nullptr;
        }
        m_lastCoverPath.clear();
        return;
    }

    // Misma portada que la ultima vez: no hace falta rehacer el blur.
    if (coverPath == m_lastCoverPath && m_fullscreenBlur) return;
    m_lastCoverPath = coverPath;

    auto* source = CCTextureCache::sharedTextureCache()->addImage(coverPath.c_str(), false);
    if (!source) return;

    auto winSize = CCDirector::get()->getWinSize();

    // Intensidad: la misma que el popup interior para look consistente.
    // Valor un poco mayor para que el fondo no compita con los elementos.
    float intensity = 7.f;
    std::string cacheKey = fmt::format(
        "menumusic_fullscreen::{}::{}x{}",
        coverPath,
        static_cast<int>(winSize.width),
        static_cast<int>(winSize.height));

    // Capturamos `this` pero usamos gen + existencia del backdrop para
    // descartar callbacks obsoletos. El popup se destruye en onExit y
    // fullscreenBackdrop sigue siendo hijo suyo, asi que si el popup muere
    // los callbacks quedaran sin parent y no hace falta remover listeners.
    BlurSystem::getInstance()->buildPaimonBlurPriority(
        source, winSize, intensity, cacheKey,
        [this, gen](CCSprite* blurred) {
            if (!blurred) return;
            if (!m_fullscreenBackdrop || !m_fullscreenBackdrop->getParent()) return;
            if (gen != m_fullscreenBlurGen) return; // portada cambio de nuevo

            auto win = CCDirector::get()->getWinSize();
            const CCSize ts = blurred->getContentSize();
            float sx = win.width / ts.width;
            float sy = win.height / ts.height;
            float scale = std::max(sx, sy);
            blurred->setScale(scale);
            blurred->setAnchorPoint({0.5f, 0.5f});
            blurred->setPosition({win.width / 2.f, win.height / 2.f});
            blurred->setOpacity(0);
            // zOrder 0 (debajo del dim en zOrder 1).
            m_fullscreenBackdrop->addChild(blurred, 0);

            // Crossfade.
            blurred->runAction(CCFadeTo::create(0.3f, 255));
            if (m_fullscreenBlur) {
                auto* old = m_fullscreenBlur;
                old->runAction(CCSequence::create(
                    CCFadeTo::create(0.3f, 0),
                    CCCallFunc::create(old, callfunc_selector(CCNode::removeFromParent)),
                    nullptr));
            }
            m_fullscreenBlur = blurred;
        });
}

// Build: top bar

void MenuMusicPopup::buildTopBar() {
    this->setTitle("Menu Music");
    // El titulo del Popup queda por defecto arriba-centro; solo bajamos
    // la Y un poco para que no se superponga con el boton close.
    if (m_title) {
        m_title->setPositionY(m_title->getPositionY() - 2.f);
    }

    auto size = m_mainLayer->getContentSize();

    // Indicador de modo eliminado a pedido del usuario: la barra superior
    // se queda solo con el titulo "Menu Music" para un look mas limpio.
    // m_modeLabel queda nullptr y refreshFromState lo ignora via null-check.

    // Engranaje: habilitar/deshabilitar Editor Music
    // Vive arriba-derecha del popup. El tinte refleja el estado on/off.
    if (auto cog = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png")) {
        cog->setScale(0.5f);
        const bool on = Mod::get()->getSettingValue<bool>("editorMusicEnable");
        cog->setColor(on ? ccColor3B{255, 255, 255} : ccColor3B{120, 120, 120});
        if (auto btn = CCMenuItemSpriteExtra::create(
                cog, this, menu_selector(MenuMusicPopup::onEditorMusicGear))) {
            btn->setID("editor-music-gear"_spr);
            auto gearMenu = CCMenu::create();
            gearMenu->setID("editor-music-gear-menu"_spr);
            gearMenu->setContentSize({0.f, 0.f});
            gearMenu->setPosition({size.width - 20.f, size.height - 20.f});
            gearMenu->addChild(btn);
            m_mainLayer->addChild(gearMenu, 20);
        }
    }
}

// Engranaje: toggle de Editor Music

void MenuMusicPopup::onEditorMusicGear(cocos2d::CCObject* sender) {
    const bool now = !Mod::get()->getSettingValue<bool>("editorMusicEnable");
    Mod::get()->setSettingValue<bool>("editorMusicEnable", now);

    if (auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
        if (auto* spr = typeinfo_cast<CCSprite*>(btn->getNormalImage())) {
            spr->setColor(now ? ccColor3B{255, 255, 255} : ccColor3B{120, 120, 120});
        }
    }
    Notification::create(
        now ? "Editor Music ON - press Ctrl+M inside the editor" : "Editor Music OFF",
        now ? NotificationIcon::Success : NotificationIcon::None, 2.f)
        ->show();
}

// Build: vinyl / hero cover
//
// Ahora en lugar de un disco circular centrado construimos un "hero"
// rectangular alto con el corte diagonal a la derecha (al estilo LevelCell).
// Dentro del hero flota un disco pequeno decorativo que se sincroniza con
// la portada.

void MenuMusicPopup::buildVinyl() {
    if (!m_contentClip) return;
    auto clipSize = m_contentClip->getContentSize();

    const float heroW = clipSize.width * kHeroWidthRatio;
    const float heroH = clipSize.height;

    m_hero = CoverHero::create({heroW, heroH}, kHeroSkew);
    if (!m_hero) return;
    m_hero->setAnchorPoint({0.f, 0.f});
    // Flush con los bordes del clipper (y por tanto del popup interior).
    m_hero->setPosition({0.f, 0.f});
    m_hero->setID("music-hero"_spr);
    m_contentClip->addChild(m_hero, 5);

    // Conectamos el disco pequeno como boton play/pause. El menu con el
    // boton vive FUERA del clipper para que el hit test funcione
    // correctamente aun cuando el disco se ubica dentro del hero (los
    // CCMenu no aceptan touch bien desde un CCClippingNode hijo).
    if (auto* disc = m_hero->getDisc()) {
        auto dummy = cocos2d::CCSprite::create();
        if (dummy) {
            dummy->setContentSize(disc->getContentSize());
            auto btn = CCMenuItemSpriteExtra::create(
                dummy, this, menu_selector(MenuMusicPopup::onPlayPause));
            if (btn) {
                btn->setContentSize(disc->getContentSize());
                btn->setAnchorPoint({0.5f, 0.5f});
                // Posicion del disco en coordenadas de m_mainLayer:
                // clipper bottom-left (world) + hero origin + disco pos
                CCPoint clipperBL =
                    m_contentClip->getPosition()
                    - CCPoint(m_contentClip->getContentSize().width / 2.f,
                              m_contentClip->getContentSize().height / 2.f);
                btn->setPosition(clipperBL + m_hero->getPosition() + disc->getPosition());

                auto menu = CCMenu::create();
                menu->setContentSize(m_mainLayer->getContentSize());
                menu->setPosition({0.f, 0.f});
                menu->setAnchorPoint({0.f, 0.f});
                menu->ignoreAnchorPointForPosition(false);
                menu->addChild(btn);
                menu->setID("hero-disc-menu"_spr);
                m_mainLayer->addChild(menu, 9);
            }
        }
    }

    m_disc = nullptr;
}

// Build: info column (title + subtitle)

void MenuMusicPopup::buildInfoColumn() {
    auto size = m_mainLayer->getContentSize();
    // El texto arranca justo a la derecha del hero (dejando espacio para
    // el corte diagonal). Calculamos el ancho del hero usando el mismo
    // ratio que buildVinyl.
    const float heroW = size.width * kHeroWidthRatio;
    const float colX = heroW + 12.f;
    const float colWidth = size.width - colX - 14.f;

    // Contenedor con clipping: el titulo puede ser muy largo (canciones
    // de YouTube con "(Official Music Video) [REMASTERED]" etc). Si lo
    // dejabamos escalarse con limitLabelWidth, en titulos cortos quedaba
    // gigante; en largos quedaba ilegible. La solucion rect-clip mantiene
    // el texto a un tamano de lectura y lo corta en el borde del popup.
    const float titleBoxW = colWidth;
    const float titleBoxH = 22.f;

    auto titleStencil = paimon::SpriteHelper::createRoundedRectStencil(
        titleBoxW, titleBoxH, 3.f);
    m_trackClip = cocos2d::CCClippingNode::create();
    m_trackClipWidth = titleBoxW;
    if (m_trackClip && titleStencil) {
        m_trackClip->setStencil(titleStencil);
        m_trackClip->setAlphaThreshold(0.05f);
        m_trackClip->setContentSize({titleBoxW, titleBoxH});
        m_trackClip->setAnchorPoint({0.f, 0.5f});
        // Ajuste fino: bajar 13px respecto al calculo % para que el
        // titulo no quede tocando el borde superior del popup.
        m_trackClip->setPosition({colX, size.height * 0.86f - 13.f});
        m_trackClip->setID("track-clip"_spr);
        m_mainLayer->addChild(m_trackClip, 6);
    }

    m_trackLabel = CCLabelBMFont::create("No track selected", "bigFont.fnt");
    if (m_trackLabel) {
        m_trackLabel->setAnchorPoint({0.f, 0.5f});
        // Dentro del clipper, coordenadas locales: 6px de padding izq.
        m_trackLabel->setPosition({6.f, titleBoxH / 2.f});
        m_trackLabel->setColor({255, 255, 255});
        m_trackLabel->setID("track-label"_spr);
        // Escala inicial uniforme. La refresh recalcula `setScale` si el
        // texto se pasa, reduciendo hasta un minimo legible.
        m_trackLabel->setScale(0.55f);
        if (m_trackClip) {
            m_trackClip->addChild(m_trackLabel, 1);
        } else {
            // Fallback sin clipping si la stencil fallo: colocamos el label
            // directo y confiamos en limitLabelWidth.
            m_trackLabel->limitLabelWidth(colWidth, 0.55f, 0.3f);
            m_trackLabel->setPosition({colX, size.height * 0.80f});
            m_mainLayer->addChild(m_trackLabel, 6);
        }
    }

    m_subtitleLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_subtitleLabel) {
        m_subtitleLabel->setScale(0.5f);
        m_subtitleLabel->setColor({215, 215, 235});
        m_subtitleLabel->setAnchorPoint({0.f, 0.5f});
        // Justo debajo del track clip. -6px extra para alinear con el
        // nuevo ajuste vertical del title clip.
        m_subtitleLabel->setPosition({colX, size.height * 0.74f - 6.f});
        m_subtitleLabel->setID("subtitle-label"_spr);
        m_mainLayer->addChild(m_subtitleLabel, 6);
    }
}

// Build: transport (prev / play-pause / next / shuffle)

void MenuMusicPopup::buildTransport() {
    auto size = m_mainLayer->getContentSize();
    auto menu = CCMenu::create();
    menu->setContentSize({size.width * 0.55f, 44.f});

    // Prev
    //
    // GJ_arrow_02_001.png apunta a la IZQUIERDA por defecto, asi que el
    // prev NO debe voltearse (queda apuntando a la izquierda).
    {
        auto spr = createIconSpriteWithFallback(
            {"GJ_arrow_02_001.png"},
            22.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/true, /*flipX*/false);
        if (spr) {
            spr->setScale(0.7f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(MenuMusicPopup::onPrev));
            if (btn) {
                btn->setID("prev-btn"_spr);
                menu->addChild(btn);
            }
        }
    }

    // Play / Pause
    //
    // Un unico boton con dos sprites hijos, uno visible a la vez.
    // Usamos fallback geometrico si los frames oficiales faltan para
    // que el boton NO quede 0x0 (causa clicks perdidos e inconsistentes).
    // Importante: el sprite de pause nativo (GJ_pauseBtn_001.png) tiene
    // un tamano distinto al de play, asi que lo escalamos para que
    // visualmente OCUPE el mismo area en pantalla (mismo width/height
    // renderizado) y quede perfectamente centrado.
    {
        m_playSprite = createIconSpriteWithFallback(
            {"GJ_playBtn2_001.png", "GJ_playBtn_001.png"},
            30.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/true);
        if (!m_playSprite) m_playSprite = CCSprite::create();
        const float playBaseScale = m_playSprite->getContentSize().width > 10.f ? 0.8f : 1.f;
        m_playSprite->setScale(playBaseScale);

        // Tamano visual efectivo del play (lo que mide en pantalla).
        const cocos2d::CCSize playVisual = {
            m_playSprite->getContentSize().width * playBaseScale,
            m_playSprite->getContentSize().height * playBaseScale
        };

        m_playBtn = CCMenuItemSpriteExtra::create(
            m_playSprite, this, menu_selector(MenuMusicPopup::onPlayPause));
        if (m_playBtn) {
            m_playBtn->setID("play-pause-btn"_spr);
            menu->addChild(m_playBtn);

            m_pauseSprite = createIconSpriteWithFallback(
                {"GJ_pauseBtn_001.png", "GJ_pauseEditorBtn_001.png"},
                22.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/false);
            if (m_pauseSprite) {
                // Escalamos el pause al mismo tamano visual que el play.
                // Usamos la dimension mayor del pause como referencia para
                // preservar aspect ratio sin deformarlo.
                const auto pauseCS = m_pauseSprite->getContentSize();
                const float pauseRef = std::max(pauseCS.width, pauseCS.height);
                const float playRef = std::max(playVisual.width, playVisual.height);
                if (pauseRef > 0.f) {
                    m_pauseSprite->setScale(playRef / pauseRef * 0.95f);
                }
                // El pauseSprite es hijo del playBtn (no del playSprite),
                // lo posicionamos en el centro del area clickable para que
                // quede alineado con el play.
                const auto btnCS = m_playBtn->getContentSize();
                m_pauseSprite->setAnchorPoint({0.5f, 0.5f});
                m_pauseSprite->setPosition({btnCS.width / 2.f, btnCS.height / 2.f});
                m_pauseSprite->setVisible(false);
                m_playBtn->addChild(m_pauseSprite, 1);
            }
        }
    }

    // Next
    //
    // Misma base pero volteada horizontalmente, asi apunta a la DERECHA.
    {
        auto spr = createIconSpriteWithFallback(
            {"GJ_arrow_02_001.png"},
            22.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/true, /*flipX*/true);
        if (spr) {
            spr->setScale(0.7f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(MenuMusicPopup::onNext));
            if (btn) {
                btn->setID("next-btn"_spr);
                menu->addChild(btn);
            }
        }
    }

    // Shuffle
    //
    // `GJ_chanceBtn_001.png` (dado) es el sprite oficial de GD para shuffle
    // aleatorio. Si no existe, caemos a GJ_reloadBtn_001.png (usado en
    // otros sitios del mod y confirmado disponible en 2.2). Sin setColor
    // para conservar los colores nativos del icono.
    {
        auto spr = createIconSpriteWithFallback(
            {"GJ_chanceBtn_001.png", "GJ_reloadBtn_001.png", "GJ_updateBtn_001.png"},
            22.f, ccc4f(1.f, 1.f, 1.f, 1.f), /*triangle*/false);
        if (spr) {
            spr->setScale(0.75f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(MenuMusicPopup::onShuffle));
            if (btn) {
                btn->setID("shuffle-btn"_spr);
                menu->addChild(btn);
            }
        }
    }

    menu->setLayout(RowLayout::create()
        ->setGap(14.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Center));
    menu->setID("transport-menu"_spr);
    // Centrado en la columna derecha (a la derecha del hero). En el
    // layout compacto (H=210): title ~86%, subtitle ~74%, transport 58%,
    // seek 42%, quick 26%, bottom 18px. Ajuste fino: -5px para alinear
    // con el descenso del title/subtitle.
    const float heroW = size.width * kHeroWidthRatio;
    menu->setPosition({
        heroW + (size.width - heroW) * 0.5f,
        size.height * 0.58f - 5.f});
    menu->updateLayout();
    m_mainLayer->addChild(menu, 6);
}

// Build: seek bar (playback progress + skip buttons)
//
// Barra con:
// * Tiempo actual (izquierda)    tiempo total (derecha)
//   * Slider nativo de Geode (drag libre): el usuario puede arrastrar
//     el thumb a cualquier punto y el audio salta a ese %.
//   * Dos botones etiquetados "-5s" / "+5s" para skips rapidos.
//
// Si el usuario desactiva `menuLoopShowPlaybackProgress`, la fila se
// oculta pero los elementos siguen construidos para reactivarse sin
// rebuild completo.

void MenuMusicPopup::buildSeekBar() {
    auto size = m_mainLayer->getContentSize();
    const float heroW = size.width * kHeroWidthRatio;
    const float colX = heroW + 14.f;
    const float colW = size.width - colX - 14.f;
    // Seek row baja 14px para despegarlo del transport tras los ajustes
    // verticales del popup.
    const float rowY = size.height * 0.42f - 14.f;

    m_seekRow = CCNode::create();
    if (!m_seekRow) return;
    m_seekRow->setContentSize({colW, 24.f});
    m_seekRow->setAnchorPoint({0.f, 0.5f});
    m_seekRow->setPosition({colX, rowY});
    m_seekRow->setID("seek-row"_spr);
    m_mainLayer->addChild(m_seekRow, 6);

    // Geometria: dos labels de tiempo a los lados del slider, dos botones
    // de skip a la derecha. En el layout compacto el slider es lo que mas
    // espacio recibe.
    const float buttonsZone = 64.f;
    const float leftLabelZone = 22.f;   // espacio para "0:00"
    const float rightLabelZone = 22.f;  // espacio para "0:00"
    const float sliderZoneX = leftLabelZone;
    const float sliderZoneW = colW - leftLabelZone - rightLabelZone - buttonsZone;
    const float barY = 12.f;

    // Slider de Geode: usa SliderTouchLogic asi que arrastrar el thumb
    // funciona de serie. La escala se calcula para que el ancho visible
    // del slider cubra `sliderZoneW`. El tamano base del groove en GD es
    // aprox 230px a escala 1.0.
    static constexpr float kSliderBaseWidth = 230.f;
    const float sliderScale = std::clamp(sliderZoneW / kSliderBaseWidth, 0.3f, 1.2f);

    m_seekSlider = Slider::create(this,
        menu_selector(MenuMusicPopup::onSeekSliderChanged),
        sliderScale);
    if (m_seekSlider) {
        m_seekSlider->setAnchorPoint({0.5f, 0.5f});
        m_seekSlider->setPosition({sliderZoneX + sliderZoneW / 2.f, barY});
        m_seekSlider->setValue(0.f);
        m_seekSlider->setID("seek-slider"_spr);
        // Sin esta llamada, el slider no recibe touch cuando esta dentro
        // de un popup con otros menus. El touch priority mas alto lo pone
        // delante del close button y del menu principal.
        m_seekSlider->setTouchEnabled(true);
        if (m_seekSlider->m_touchLogic) {
            m_seekSlider->m_touchLogic->setTouchPriority(-600);
        }
        m_seekRow->addChild(m_seekSlider, 1);
    }

    // Current / total time labels.
    m_seekCurLabel = CCLabelBMFont::create("0:00", "chatFont.fnt");
    if (m_seekCurLabel) {
        m_seekCurLabel->setScale(0.4f);
        m_seekCurLabel->setAnchorPoint({1.f, 0.5f});
        m_seekCurLabel->setPosition({sliderZoneX - 3.f, barY});
        m_seekCurLabel->setColor({235, 235, 250});
        m_seekRow->addChild(m_seekCurLabel, 2);
    }
    m_seekTotalLabel = CCLabelBMFont::create("0:00", "chatFont.fnt");
    if (m_seekTotalLabel) {
        m_seekTotalLabel->setScale(0.4f);
        m_seekTotalLabel->setAnchorPoint({0.f, 0.5f});
        m_seekTotalLabel->setPosition({sliderZoneX + sliderZoneW + 3.f, barY});
        m_seekTotalLabel->setColor({235, 235, 250});
        m_seekRow->addChild(m_seekTotalLabel, 2);
    }

    // Skip buttons etiquetados con texto ("-5s" / "+5s"). Usa ButtonSprite
    // para que sea obvio que son botones de seek y no otra cosa. La cantidad
    // se lee del ajuste `menuLoopSeekAmountMs` para que el label refleje
    // exactamente cuanto va a saltar (ej: "-3s" si el usuario lo cambio).
    auto skipMenu = CCMenu::create();
    skipMenu->setContentSize({buttonsZone, 24.f});
    skipMenu->setPosition({
        sliderZoneX + sliderZoneW + rightLabelZone + buttonsZone / 2.f,
        barY});
    skipMenu->ignoreAnchorPointForPosition(false);

    const int seekMs = std::clamp<int>(
        static_cast<int>(Mod::get()->getSavedValue<int>("menuLoopSeekAmountMs", 5000)),
        100, 30000);
    const int seekSec = std::max(1, (seekMs + 500) / 1000);
    const std::string bkwdLbl = fmt::format("-{}s", seekSec);
    const std::string fwrdLbl = fmt::format("+{}s", seekSec);

    auto makeLabeledBtn = [&](const std::string& label, SEL_MenuHandler selector)
        -> CCMenuItemSpriteExtra* {
        auto spr = ButtonSprite::create(
            label.c_str(), 28, true, "bigFont.fnt",
            "GJ_button_01.png", 14.f, 0.5f);
        if (!spr) return nullptr;
        spr->setScale(0.75f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, selector);
        return btn;
    };

    m_seekBkwdBtn = makeLabeledBtn(bkwdLbl, menu_selector(MenuMusicPopup::onSeekBackward));
    if (m_seekBkwdBtn) {
        m_seekBkwdBtn->setID("seek-bkwd-btn"_spr);
        skipMenu->addChild(m_seekBkwdBtn);
    }
    m_seekFwrdBtn = makeLabeledBtn(fwrdLbl, menu_selector(MenuMusicPopup::onSeekForward));
    if (m_seekFwrdBtn) {
        m_seekFwrdBtn->setID("seek-fwrd-btn"_spr);
        skipMenu->addChild(m_seekFwrdBtn);
    }
    skipMenu->setLayout(RowLayout::create()
        ->setGap(3.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Center));
    skipMenu->setID("seek-skip-menu"_spr);
    skipMenu->updateLayout();
    m_seekRow->addChild(skipMenu, 3);
}

// Build: quick actions row
//
// Favorite / Blacklist / Hold / Copy / Regen notif / Add-to-playlist.
// Reutiliza el sistema menu-loop (MenuLoopControl) asi que funciona
// tanto con canciones del MenuMusicLibrary como con canciones
// externas (vanilla menu loop, GD downloads, etc.).

void MenuMusicPopup::buildQuickActions() {
    auto size = m_mainLayer->getContentSize();
    const float heroW = size.width * kHeroWidthRatio;
    const float colX = heroW + 14.f;
    const float colW = size.width - colX - 14.f;

    auto menu = CCMenu::create();
    menu->setContentSize({colW, 28.f});
    menu->setAnchorPoint({0.5f, 0.5f});
    // Con H=210: title 86, subtitle 74, transport 58, seek 42, quick 24.
    menu->setPosition({colX + colW / 2.f, size.height * 0.24f});
    menu->setID("quick-actions-menu"_spr);

    struct ActionDef {
        std::initializer_list<const char*> frames;
        const char* id;
        SEL_MenuHandler selector;
    };
    const std::array<ActionDef, 7> actions = {{
        // Favorite: gold star (native yellow)
        {{"GJ_starBtn_001.png", "GJ_star_001.png"},
         "fav-btn", menu_selector(MenuMusicPopup::onFavorite)},
        // Blacklist: delete / trash
        {{"GJ_deleteBtn_001.png", "GJ_trashBtn_001.png", "GJ_cancelDownloadBtn_001.png"},
         "bl-btn", menu_selector(MenuMusicPopup::onBlacklist)},
        // Hold: pause icon
        {{"GJ_pauseBtn_001.png", "GJ_pauseEditorBtn_001.png"},
         "hold-btn", menu_selector(MenuMusicPopup::onHold)},
        // Copy: song ID if available, otherwise display name
        {{"GJ_copyBtn_001.png", "GJ_copyListBtn_001.png"},
         "copy-btn", menu_selector(MenuMusicPopup::onCopyName)},
        // Regen notification: reload icon
        {{"GJ_updateBtn_001.png", "GJ_reloadBtn_001.png"},
         "regen-btn", menu_selector(MenuMusicPopup::onRegenNotification)},
        // Add to playlist file: plus icon
        {{"GJ_plusBtn_001.png", "GJ_plus2Btn_001.png"},
         "add-playlist-btn", menu_selector(MenuMusicPopup::onAddToPlaylistFile)},
        // Open Songs popup: list icon
        {{"GJ_listBtn_001.png", "GJ_findBtn_001.png", "GJ_viewBtn_001.png"},
         "songs-btn", menu_selector(MenuMusicPopup::onOpenSongs)},
    }};

    for (const auto& act : actions) {
        cocos2d::CCSprite* spr = nullptr;
        for (auto name : act.frames) {
            if (auto s = paimon::SpriteHelper::safeCreateWithFrameName(name)) {
                spr = s; break;
            }
        }
        if (!spr) {
            spr = cocos2d::CCSprite::create();
            if (spr) spr->setContentSize({22.f, 22.f});
        }
        if (!spr) continue;
        spr->setScale(0.55f);
        // Sin setColor: respetamos los colores nativos del asset de GD.
        auto btn = CCMenuItemSpriteExtra::create(spr, this, act.selector);
        if (!btn) continue;
        btn->setID(act.id);
        menu->addChild(btn);
    }

    menu->setLayout(RowLayout::create()
        ->setGap(10.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setCrossAxisAlignment(AxisAlignment::Center)
        ->setDefaultScaleLimits(0.4f, 1.0f));
    menu->updateLayout();
    m_mainLayer->addChild(menu, 6);
}

// Build: bottom bar (library / playlists / add / random-all)

void MenuMusicPopup::buildBottomBar() {
    auto size = m_mainLayer->getContentSize();

    auto menu = CCMenu::create();
    // Popup compacto: bottom bar mas ancho y baja un poco.
    menu->setContentSize({size.width * 0.95f, 32.f});
    menu->setPosition({size.width / 2.f, 22.f});

    auto makeBtn = [&](const char* text, const char* bg, SEL_MenuHandler handler) -> CCMenuItemSpriteExtra* {
        auto spr = ButtonSprite::create(text, 70, true, "bigFont.fnt", bg, 18.f, 0.45f);
        if (!spr) return nullptr;
        return CCMenuItemSpriteExtra::create(spr, this, handler);
    };

    if (auto b = makeBtn("Library", "GJ_button_01.png", menu_selector(MenuMusicPopup::onOpenLibrary))) {
        b->setID("lib-btn"_spr); menu->addChild(b);
    }
    if (auto b = makeBtn("Playlists", "GJ_button_04.png", menu_selector(MenuMusicPopup::onOpenPlaylists))) {
        b->setID("pl-btn"_spr); menu->addChild(b);
    }
    if (auto b = makeBtn("Add", "GJ_button_05.png", menu_selector(MenuMusicPopup::onOpenAdd))) {
        b->setID("add-btn"_spr); menu->addChild(b);
    }
    // "Random All": selecciona UNA cancion al azar de todos los origenes
    // disponibles (MenuMusicLibrary + menu-loop songs + descargados de GD).
    // Reemplaza el antiguo boton "Vanilla" porque el usuario pedia una
    // forma explicita de sortear todas las canciones.
    if (auto b = makeBtn("Random All", "GJ_button_02.png", menu_selector(MenuMusicPopup::onShuffleAll))) {
        b->setID("random-all-btn"_spr); menu->addChild(b);
    }

    menu->setLayout(RowLayout::create()
        ->setGap(6.f)
        ->setAxisAlignment(AxisAlignment::Center)
        ->setDefaultScaleLimits(0.5f, 1.0f));
    menu->setID("bottom-menu"_spr);
    menu->updateLayout();
    m_mainLayer->addChild(menu, 6);
}

// Refresh
//
// El popup intenta reflejar LO QUE SUENA AHORA, no solo lo que el
// MenuMusicPlayer tiene seteado. Si el player no tiene track (p. ej. el
// usuario esta usando el menu loop vanilla o el reference mod tiene
// activado un shuffle independiente), caemos al MenuLoopManager y
// resolvemos el nombre via MusicDownloadManager para IDs tipo "DL_xxxxx".

MenuMusicPopup::DetectedSong MenuMusicPopup::detectActiveSong() const {
    DetectedSong out;

    // 1) Si el player del mod tiene track activo, es la fuente de verdad.
    auto& player = MenuMusicPlayer::get();
    if (auto* t = player.currentTrack()) {
        out.displayName = t->displayName.empty()
            ? geode::utils::string::pathToString(std::filesystem::path(t->audioPath).stem())
            : t->displayName;
        out.artist = t->artist.empty()
            ? (t->source == TrackSource::Downloaded ? "Downloaded track" : "Local track")
            : t->artist;
        out.coverPath = t->coverPath;
        if (!t->coverPath.empty()) out.coverPaths.push_back(t->coverPath);
        out.audioPath = t->audioPath;
        out.isPaimonTrack = true;
        out.hasAnything = true;
        if (out.songID <= 0) {
            if (auto songId = resolveGDSongId(t->audioPath, t->sourceUrl)) {
                out.songID = *songId;
            } else {
                coverlog::warn("[MenuMusicCover] could not resolve songID from paimon track path='{}' sourceUrl='{}'",
                    t->audioPath, t->sourceUrl);
            }
        }
        if (out.coverPaths.empty() && out.songID > 0) {
            out.coverPaths = SongCoverCache::get().getCachedCoverPaths(out.songID);
            if (!out.coverPaths.empty()) out.coverPath = out.coverPaths.front();
        }
        coverlog::info("[MenuMusicCover] detectActiveSong (paimon) songID={} covers={} path='{}'",
            out.songID, out.coverPaths.size(), out.audioPath);
        if (out.songID > 0) {
            if (auto* mdm = MusicDownloadManager::sharedState()) {
                if (auto* info = mdm->getSongInfoObject(out.songID)) {
                    if (out.displayName.empty() && !info->m_songName.empty()) {
                        out.displayName = info->m_songName;
                    }
                    if (out.artist.empty() && !info->m_artistName.empty()) {
                        out.artist = info->m_artistName;
                    }
                }
            }
        }
        return out;
    }

    // 2) Menu loop / override / getMenuMusicFile (lo que realmente suena).
    auto& loop = paimon::menuloop::MenuLoopManager::get();
    std::string current = resolveActiveMenuMusicPath();
    out.audioPath = current;

    if (current.empty() || isVanillaMenuLoopPath(current)) {
        if (!loop.isOverride()) {
            out.displayName = "Menu Loop (vanilla)";
            out.artist = "by RobTop";
            out.hasAnything = false;
            coverlog::info("[MenuMusicCover] detectActiveSong: vanilla menu loop");
            return out;
        }
        current = loop.getCurrentSong();
        out.audioPath = current;
    }

    std::string resolvedName = loop.getCurrentSongDisplayName();
    auto p = std::filesystem::path(current);

    if (out.songID <= 0) {
        if (auto songId = resolveGDSongId(current)) {
            out.songID = *songId;
        } else {
            coverlog::warn("[MenuMusicCover] could not resolve songID from menu path='{}'", current);
        }
    }
    if (out.coverPaths.empty() && out.songID > 0) {
        out.coverPaths = SongCoverCache::get().getCachedCoverPaths(out.songID);
        if (!out.coverPaths.empty()) out.coverPath = out.coverPaths.front();
    }
    coverlog::info("[MenuMusicCover] detectActiveSong (external) songID={} covers={} path='{}'",
        out.songID, out.coverPaths.size(), out.audioPath);
    if (out.songID > 0) {
        if (auto* mdm = MusicDownloadManager::sharedState()) {
            if (auto* info = mdm->getSongInfoObject(out.songID)) {
                if (out.displayName.empty() && !info->m_songName.empty()) {
                    out.displayName = info->m_songName;
                }
                if (out.artist.empty() && !info->m_artistName.empty()) {
                    out.artist = info->m_artistName;
                }
            }
        }
    }

    if (out.displayName.empty()) {
        out.displayName = !resolvedName.empty()
            ? resolvedName
            : geode::utils::string::pathToString(p.filename());
    }
    if (out.artist.empty()) {
        out.artist = loop.isOverride() ? "Override" : "External track";
    }

    out.hasAnything = !current.empty() && !isVanillaMenuLoopPath(current);
    return out;
}

void MenuMusicPopup::refreshFromState() {
    auto& lib = MenuMusicLibrary::get();
    auto& player = MenuMusicPlayer::get();

    // Modo
    const char* modeStr = "Disabled";
    switch (lib.mode()) {
        case PlaybackMode::Library:  modeStr = "Library Shuffle"; break;
        case PlaybackMode::Playlist: modeStr = "Playlist"; break;
        case PlaybackMode::Queue:    modeStr = "Single Track"; break;
        case PlaybackMode::Disabled: modeStr = "Vanilla (GD default)"; break;
    }
    if (m_modeLabel) m_modeLabel->setString(fmt::format("Mode: {}", modeStr).c_str());

    // Detectar la cancion activa (del player del mod o del menu-loop externo).
    auto detected = detectActiveSong();

    const std::string displayTitle = detected.displayName.empty()
        ? "No track selected" : detected.displayName;
    const std::string subtitle = detected.hasAnything
        ? detected.artist : "No custom track playing";

    if (m_trackLabel) {
        m_trackLabel->setString(displayTitle.c_str());
        // En el layout con clipping (m_trackClip): escalamos hacia abajo
        // solo si el texto desborda el ancho disponible. Si el clip no
        // se pudo construir caemos al limitLabelWidth clasico.
        if (m_trackClip && m_trackClipWidth > 0.f) {
            m_trackLabel->setScale(1.f);
            const float available = m_trackClipWidth - 12.f; // padding interior
            const float rawW = m_trackLabel->getContentSize().width;
            if (rawW > available) {
                // Clamp de escala para que textos largos no queden
                // ilegibles (min 0.35).
                const float s = std::max(0.35f, available / rawW);
                m_trackLabel->setScale(s);
            } else {
                m_trackLabel->setScale(0.55f);
            }
        } else {
            m_trackLabel->limitLabelWidth(
                m_mainLayer->getContentSize().width * 0.48f, 0.7f, 0.3f);
        }
    }
    if (m_subtitleLabel) m_subtitleLabel->setString(subtitle.c_str());

    // Portadas locales / cache permanente por song ID.
    std::vector<std::string> coverPaths = detected.coverPaths;
    if (coverPaths.empty() && !detected.coverPath.empty()) {
        coverPaths.push_back(detected.coverPath);
    }

    {
        std::error_code coverEc;
        std::size_t const beforeFilter = coverPaths.size();
        coverPaths.erase(
            std::remove_if(coverPaths.begin(), coverPaths.end(),
                [&](std::string const& p) {
                    if (p.empty()) return true;
                    if (!std::filesystem::exists(p, coverEc) || coverEc) {
                        coverlog::warn("[MenuMusicCover] cover file missing: '{}'", p);
                        coverEc.clear();
                        return true;
                    }
                    return false;
                }),
            coverPaths.end()
        );
        if (beforeFilter > coverPaths.size()) {
            coverlog::warn("[MenuMusicCover] filtered {} missing cover files songID={}",
                beforeFilter - coverPaths.size(), detected.songID);
        }
    }

    if (coverPaths.empty() && detected.songID > 0) {
        coverPaths = SongCoverCache::get().getCachedCoverPaths(detected.songID);
        if (!coverPaths.empty()) {
            coverlog::info("[MenuMusicCover] loaded {} covers from SongCoverCache songID={}",
                coverPaths.size(), detected.songID);
        }
    }

    if (coverPaths.empty() && detected.songID > 0) {
        if (m_pendingSongCoverID != detected.songID) {
            m_pendingSongCoverID = detected.songID;
            Ref<MenuMusicPopup> self = this;
            const int requestedSongID = detected.songID;
            coverlog::info("[MenuMusicCover] requesting covers songID={} title='{}'",
                requestedSongID, detected.displayName);
            SongCoverCache::get().requestCovers(requestedSongID,
                [self, requestedSongID](std::vector<std::string> const& paths, bool success) {
                    if (!(self->getParent() || self->isRunning())) {
                        coverlog::info("[MenuMusicCover] callback ignored: popup closed songID={}",
                            requestedSongID);
                        return;
                    }
                    if (success && !paths.empty()) {
                        coverlog::info("[MenuMusicCover] covers ready songID={} count={}",
                            requestedSongID, paths.size());
                        self->refreshFromState();
                        return;
                    }
                    coverlog::warn("[MenuMusicCover] cover request failed songID={} success={} paths={}",
                        requestedSongID, success, paths.size());
                    if (self->m_pendingSongCoverID == requestedSongID) {
                        self->m_pendingSongCoverID = 0;
                    }
                });
        } else {
            coverlog::info("[MenuMusicCover] cover request already pending songID={}", detected.songID);
        }
    } else if (detected.songID <= 0) {
        if (!detected.hasAnything) {
            coverlog::info("[MenuMusicCover] no custom track, skipping cover fetch");
        } else {
            coverlog::warn("[MenuMusicCover] active track has no songID path='{}'", detected.audioPath);
        }
        m_pendingSongCoverID = 0;
    } else if (!coverPaths.empty()) {
        coverlog::info("[MenuMusicCover] using {} local covers songID={}",
            coverPaths.size(), detected.songID);
    }

    applyCovers(coverPaths);

    if (m_hero) {
        const bool isPlaying = detected.hasAnything && !player.isPaused();
        m_hero->setPausedAppearance(!isPlaying);
    }
    if (m_disc) {
        if (detected.hasAnything && !player.isPaused()) {
            m_disc->startSpinning();
        } else {
            m_disc->stopSpinning();
        }
    }

    m_lastDetectedPath = detected.audioPath;

    // Play/Pause sprites (los del area de transport, ahora solo redundancia)
    const bool showPause = detected.hasAnything && !player.isPaused();
    if (m_playSprite) m_playSprite->setVisible(!showPause);
    if (m_pauseSprite) m_pauseSprite->setVisible(showPause);
}

void MenuMusicPopup::applyCovers(std::vector<std::string> const& coverPaths) {
    m_activeCoverPaths = coverPaths;

    if (m_hero) {
        m_hero->setCoversFromPaths(coverPaths);
    }

    std::string chromePath = coverPaths.empty()
        ? std::string{}
        : (m_hero ? m_hero->getCurrentCoverPath() : coverPaths.front());

    if (m_bg) m_bg->setCoverFromPath(chromePath);
    if (m_disc) m_disc->setCoverFromPath(chromePath);
    applyFullscreenCover(chromePath);
    m_lastChromeCoverPath = chromePath;
}

void MenuMusicPopup::syncCoverChrome(float) {
    if (!m_hero || m_activeCoverPaths.size() <= 1) return;

    std::string current = m_hero->getCurrentCoverPath();
    if (current.empty() || current == m_lastChromeCoverPath) return;

    m_lastChromeCoverPath = current;
    if (m_bg) m_bg->setCoverFromPath(current);
    if (m_disc) m_disc->setCoverFromPath(current);
    applyFullscreenCover(current);
}

void MenuMusicPopup::onTrackChanged(const std::string&) { refreshFromState(); }
void MenuMusicPopup::onLibraryChanged() { refreshFromState(); }

// Poll: detecta cambios del menu-loop externo
//
// Cuando el usuario cambia de track mientras el popup sigue abierto (por
// ejemplo con el boton de shuffle del menu principal), el
// MenuMusicPlayer no dispara su listener porque no es quien lo cambio.
// Aqui comparamos el path del menu-loop con el ultimo que procesamos y
// refrescamos si difiere. 0.5s es suficiente para que se sienta vivo
// sin recomputar blur constantemente.

void MenuMusicPopup::pollExternalSong(float) {
    auto detected = detectActiveSong();
    if (detected.audioPath == m_lastDetectedPath) {
        if (!detected.coverPaths.empty() || detected.songID <= 0) return;
        if (m_pendingSongCoverID == detected.songID) return;
    }

    refreshFromState();
}

// Button callbacks

void MenuMusicPopup::onPlayPause(CCObject*) {
    auto& player = MenuMusicPlayer::get();
    if (!player.currentTrack()) {
        // Nada sonando: intentar arrancar en modo Library.
        auto& lib = MenuMusicLibrary::get();
        if (!lib.hasTracks()) {
            Notification::create("Your music library is empty - use 'Add Music' first.",
                NotificationIcon::Info)->show();
            return;
        }
        if (lib.mode() == PlaybackMode::Disabled) lib.setMode(PlaybackMode::Library);
        player.playNext();
        return;
    }
    if (player.isPaused()) player.resume();
    else player.pause();
    refreshFromState();
}

void MenuMusicPopup::onPrev(CCObject*) {
    if (!MenuMusicPlayer::get().playPrevious()) {
        Notification::create("No previous track in history.", NotificationIcon::Info)->show();
    }
}

void MenuMusicPopup::onNext(CCObject*) {
    auto& lib = MenuMusicLibrary::get();
    if (!lib.hasTracks()) {
        Notification::create("Library is empty.", NotificationIcon::Warning)->show();
        return;
    }
    if (lib.mode() == PlaybackMode::Disabled) lib.setMode(PlaybackMode::Library);
    MenuMusicPlayer::get().playNext();
}

void MenuMusicPopup::onShuffle(CCObject*) {
    onNext(nullptr);
}

// Shuffle All: Library + menu-loop songs + GD downloaded
//
// El usuario queria un boton que mezclara TODAS las fuentes posibles:
//   * Canciones del MenuMusicLibrary (mod)
//   * Canciones del MenuLoopManager (carpeta config/*.mp3 + playlist)
//   * Canciones descargadas de GD (MusicDownloadManager, ubicadas en
//     el save dir de GD, con nombres como "12345.mp3")
//
// Se elige UNA al azar, excluyendo la que suena actualmente para que
// siempre haya un cambio perceptible.

static std::vector<std::string> collectAllExternalSongs() {
    std::vector<std::string> out;

    // 1) MenuMusicLibrary (las que maneja nuestro popup)
    for (const auto& t : paimon::menumusic::MenuMusicLibrary::get().tracks()) {
        if (!t.audioPath.empty()) out.push_back(t.audioPath);
    }

    // 2) MenuLoopManager (carpeta config/ + playlist custom)
    for (const auto& s : paimon::menuloop::MenuLoopManager::get().getSongs()) {
        if (!s.empty()) out.push_back(s);
    }

    // 3) GD downloaded songs (via MusicDownloadManager).
    //
    // Usamos la API de GD (getDownloadedSongs) en vez de escanear el
    // directorio a ciegas. Esto nos permite filtrar los "resource songs"
    // (efectos de sonido del juego) con isResourceSong(), igual que hace
    // el mod de referencia (menuloop_randomizer). Solo incluimos musica
    // real (Newgrounds / Music Library).
    auto* downloadManager = MusicDownloadManager::sharedState();
    if (downloadManager) {
        for (auto* song : geode::cocos::CCArrayExt<SongInfoObject*>(
                 downloadManager->getDownloadedSongs())) {
            if (!song) continue;
            // Filtrar efectos de sonido / resource songs del juego.
            if (downloadManager->isResourceSong(song->m_songID)) continue;

            std::string songPath = downloadManager->pathForSong(song->m_songID);
            if (songPath.empty()) continue;

            // Verificar que el archivo existe y tiene extension soportada.
            std::error_code ec2;
            if (!std::filesystem::exists(songPath, ec2) || ec2) continue;

            auto ext = geode::utils::string::toLower(
                geode::utils::string::pathToString(std::filesystem::path(songPath).extension()));
            if (ext == ".mp3" || ext == ".ogg" || ext == ".wav" || ext == ".flac"
                || ext == ".oga" || ext == ".m4a") {
                out.push_back(songPath);
            }
        }
    }

    // Dedup preservando orden.
    std::vector<std::string> dedup;
    dedup.reserve(out.size());
    for (auto& p : out) {
        if (std::find(dedup.begin(), dedup.end(), p) == dedup.end()) {
            dedup.push_back(std::move(p));
        }
    }
    return dedup;
}

void MenuMusicPopup::onShuffleAll(CCObject*) {
    auto all = collectAllExternalSongs();
    if (all.empty()) {
        Notification::create(
            "No songs found across Library, menu-loop folder, or GD downloads.",
            NotificationIcon::Warning)->show();
        return;
    }

    // Intentar no repetir la cancion actual.
    auto& loop = paimon::menuloop::MenuLoopManager::get();
    const std::string current = loop.getCurrentSong();

    static std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<std::size_t> dist(0, all.size() - 1);

    std::string pick = all[dist(rng)];
    for (int tries = 0; tries < 5 && pick == current && all.size() > 1; ++tries) {
        pick = all[dist(rng)];
    }

    std::error_code ec;
    if (!std::filesystem::exists(pick, ec) || ec) {
        Notification::create(
            fmt::format("Picked song not found on disk: {}",
                geode::utils::string::pathToString(std::filesystem::path(pick).filename())),
            NotificationIcon::Error)->show();
        return;
    }

    // Si la cancion viene de nuestra libreria, delegamos en
    // MenuMusicPlayer::playSpecific (gestiona historial, listeners, etc).
    auto& ml = MenuMusicLibrary::get();
    for (const auto& t : ml.tracks()) {
        if (t.audioPath == pick) {
            MenuMusicPlayer::get().playSpecific(t.id);
            Notification::create(
                fmt::format("Playing: {}", t.displayName.empty()
                    ? geode::utils::string::pathToString(std::filesystem::path(pick).stem())
                    : t.displayName),
                NotificationIcon::Info)->show();
            return;
        }
    }

    // Para canciones externas usamos el mismo mecanismo que
    // MenuMusicPlayer::applyOverrideAndPlay pero sin tocar el track state
    // del player (no es nuestra track, es del menu-loop externo).
    loop.setOverride(pick);
    loop.setCurrentSong(pick);
    loop.setCurrentSongDisplayName(geode::utils::string::pathToString(std::filesystem::path(pick).stem()));

    auto* fmod = FMODAudioEngine::sharedEngine();
    if (fmod && fmod->m_backgroundMusicChannel) {
        fmod->m_backgroundMusicChannel->stop();
    }
    GameManager::sharedState()->playMenuMusic();

    // Forzar refresh para que el titulo + cover (si es una DL con id) se
    // resuelva con MusicDownloadManager.
    refreshFromState();

    Notification::create(
        fmt::format("Shuffling all: {}", geode::utils::string::pathToString(std::filesystem::path(pick).stem())),
        NotificationIcon::Info)->show();
}

void MenuMusicPopup::onModeLibrary(CCObject*) {
    MenuMusicPlayer::get().setMode(PlaybackMode::Library, true);
}
void MenuMusicPopup::onModePlaylist(CCObject*) {
    auto& lib = MenuMusicLibrary::get();
    if (lib.playlists().empty()) {
        Notification::create("Create a playlist first.", NotificationIcon::Warning)->show();
        return;
    }
    if (lib.activePlaylistId().empty()) {
        lib.setActivePlaylistId(lib.playlists().front().id);
    }
    MenuMusicPlayer::get().setMode(PlaybackMode::Playlist, true);
}
void MenuMusicPopup::onModeDisabled(CCObject*) {
    MenuMusicPlayer::get().setMode(PlaybackMode::Disabled, false);
    refreshFromState();
}

void MenuMusicPopup::onOpenLibrary(CCObject*) {
    if (auto p = MenuMusicLibraryPopup::create()) p->show();
}
void MenuMusicPopup::onOpenPlaylists(CCObject*) {
    if (auto p = MenuMusicPlaylistsPopup::create()) p->show();
}
void MenuMusicPopup::onOpenAdd(CCObject*) {
    if (auto p = MenuMusicAddPopup::create()) p->show();
}

// Quick actions
//
// The quick-actions row favorites/blacklists/holds/etc whatever track
// is audible right now. The reference mod (Menu Loop Randomizer) builds
// these on top of a dense SongManager that always knows the list of
// menu loops; Paimbnails uses a more modular stack (MenuMusicPlayer +
// MenuLoopManager), so we write the state directly to the config
// files instead of calling into MenuLoopControl. This keeps the
// popup usable even when MenuLoopManager hasn't scanned a song folder
// or when the track comes from the MenuMusicLibrary.

namespace {
    // Helper: appends `songPath` as a new line to `file`, guarding
    // against duplicates. Returns true on success.
    static bool appendSongToTextFile(const std::filesystem::path& file,
                                     const std::string& songPath) {
        auto existing = geode::utils::file::readString(file).unwrapOr("");
        if (existing.find(songPath) != std::string::npos) return false;
        auto payload = existing;
        if (!payload.empty() && payload.back() != '\n') payload.push_back('\n');
        payload += songPath;
        payload.push_back('\n');
        auto res = geode::utils::file::writeString(file, payload);
        return res.isOk();
    }

    // Helper: resolves whatever track is playing and returns its on-disk
    // path. Library/Queue tracks go via MenuMusicPlayer; menu loops and
    // GD downloads via MenuLoopManager.
    static std::string resolveCurrentTrackPath(bool* isMenuLoopVanilla = nullptr) {
        auto& player = paimon::menumusic::MenuMusicPlayer::get();
        if (auto* t = player.currentTrack()) {
            if (isMenuLoopVanilla) *isMenuLoopVanilla = false;
            return t->audioPath;
        }
        if (auto path = resolveActiveMenuMusicPath(); !path.empty()) {
            if (isMenuLoopVanilla) *isMenuLoopVanilla = false;
            return path;
        }
        if (isMenuLoopVanilla) *isMenuLoopVanilla = true;
        return "";
    }
}

void MenuMusicPopup::onFavorite(CCObject*) {
    bool isVanilla = false;
    std::string path = resolveCurrentTrackPath(&isVanilla);
    if (isVanilla || path.empty()) {
        Notification::create(
            "Nothing to favorite — the vanilla GD menu loop is playing.",
            NotificationIcon::Info)->show();
        return;
    }
    auto favFile = Mod::get()->getConfigDir() / "favorites.txt";
    if (appendSongToTextFile(favFile, path)) {
        paimon::menuloop::MenuLoopManager::get().addToFavorites(path);
        Notification::create(
            fmt::format("Favorited: {}", geode::utils::string::pathToString(std::filesystem::path(path).stem())),
            NotificationIcon::Success)->show();
    } else {
        Notification::create("Already favorited.", NotificationIcon::Info)->show();
    }
    refreshFromState();
}

void MenuMusicPopup::onBlacklist(CCObject*) {
    bool isVanilla = false;
    std::string path = resolveCurrentTrackPath(&isVanilla);
    if (isVanilla || path.empty()) {
        Notification::create(
            "Nothing to blacklist — the vanilla GD menu loop is playing.",
            NotificationIcon::Info)->show();
        return;
    }
    auto blFile = Mod::get()->getConfigDir() / "blacklist.txt";
    if (appendSongToTextFile(blFile, path)) {
        auto& loop = paimon::menuloop::MenuLoopManager::get();
        loop.addToBlacklist(path);
        loop.removeSong(path);
        Notification::create(
            fmt::format("Blacklisted: {}", geode::utils::string::pathToString(std::filesystem::path(path).stem())),
            NotificationIcon::Success)->show();
        // Skip to next song so the blacklisted one stops playing.
        MenuMusicPlayer::get().playNext();
    } else {
        Notification::create("Already blacklisted.", NotificationIcon::Info)->show();
    }
    refreshFromState();
}

void MenuMusicPopup::onHold(CCObject*) {
    // Hold/swap: remember the current song; if something is already held,
    // switch to it. Mirrors the Tetris-style behaviour of the reference.
    auto& loop = paimon::menuloop::MenuLoopManager::get();
    bool isVanilla = false;
    std::string current = resolveCurrentTrackPath(&isVanilla);
    if (isVanilla || current.empty()) {
        Notification::create(
            "Nothing to hold — the vanilla GD menu loop is playing.",
            NotificationIcon::Info)->show();
        return;
    }
    const std::string former = loop.getHeldSong();
    if (current == former) {
        Notification::create("You're already holding that song!", NotificationIcon::Info)->show();
        return;
    }
    loop.setHeldSong(current);
    if (!former.empty()) {
        // Swap.
        auto* fmod = FMODAudioEngine::sharedEngine();
        if (fmod && fmod->m_backgroundMusicChannel) fmod->m_backgroundMusicChannel->stop();
        loop.setOverride(former);
        loop.setCurrentSong(former);
        loop.setCurrentSongDisplayName(geode::utils::string::pathToString(std::filesystem::path(former).stem()));
        GameManager::sharedState()->playMenuMusic();
        Notification::create(
            fmt::format("Swapped with held: {}",
                geode::utils::string::pathToString(std::filesystem::path(former).stem())),
            NotificationIcon::Info)->show();
    } else {
        Notification::create("Song held. Press again to swap.", NotificationIcon::Info)->show();
        MenuMusicPlayer::get().playNext();
    }
    refreshFromState();
}

void MenuMusicPopup::onCopyName(CCObject*) {
    auto toCopy = resolveActiveMenuMusicCopyValue();
    geode::utils::clipboard::write(toCopy);
    Notification::create(fmt::format("Copied: {}", toCopy), NotificationIcon::Success)->show();
}

void MenuMusicPopup::onRegenNotification(CCObject*) {
    NowPlayingToast::showForCurrent(this);
    refreshFromState();
}

void MenuMusicPopup::onAddToPlaylistFile(CCObject*) {
    bool isVanilla = false;
    std::string path = resolveCurrentTrackPath(&isVanilla);
    if (isVanilla || path.empty()) {
        Notification::create(
            "Nothing to add — the vanilla GD menu loop is playing.",
            NotificationIcon::Info)->show();
        return;
    }
    auto plStr = Mod::get()->getSavedValue<std::string>("menuLoopPlaylistFile", "");
    std::filesystem::path plPath = plStr.empty()
        ? (Mod::get()->getConfigDir() / "playlistOne.txt")
        : std::filesystem::path(plStr);
    if (plPath.extension() != ".txt") {
        Notification::create("Playlist file must be a .txt file.",
            NotificationIcon::Error)->show();
        return;
    }
    if (appendSongToTextFile(plPath, path)) {
        Notification::create(
            fmt::format("Added to {}", geode::utils::string::pathToString(plPath.filename())),
            NotificationIcon::Success)->show();
    } else {
        Notification::create("Song already in playlist.", NotificationIcon::Info)->show();
    }
}

void MenuMusicPopup::onOpenSongs(CCObject*) {
    // Song List view: all menu-loop + GD downloaded + library tracks with
    // search + per-row play. Mirrors Menu Loop Randomizer's Song List popup
    // while keeping Paimbnails' own Library popup reachable via the
    // bottom-bar "Library" button.
    if (auto p = ExternalSongsPopup::create()) p->show();
}

// Seek callbacks

void MenuMusicPopup::onSeekBackward(CCObject*) {
    if (!Mod::get()->getSavedValue<bool>("menuLoopShowPlaybackProgress", true)) return;
    paimon::menuloop::MenuLoopControl::skipBackward();
}

void MenuMusicPopup::onSeekForward(CCObject*) {
    if (!Mod::get()->getSavedValue<bool>("menuLoopShowPlaybackProgress", true)) return;
    paimon::menuloop::MenuLoopControl::skipForward();
}

// Callback del Slider de Geode. Se dispara CADA VEZ que el thumb se
// mueve (no solo al soltar), por lo que aqui se hace el seek real.
//
// Estrategia:
//   1. Marcar m_seekSliderDragging=true para que tickSeekUpdate no
//      pise el thumb mientras el usuario lo arrastra.
//   2. Calcular el porcentaje 0..100 del valor del slider.
//   3. Delegar a MenuLoopControl::setSongPercentage, que ya gestiona
//      pause-tracking y las posiciones internas del menu-loop.
//
// El "release" del arrastre se detecta con un flag que se resetea en
// tickSeekUpdate cuando pasa tiempo sin que el valor cambie — mas
// simple que meterse con ccTouchEnded.
void MenuMusicPopup::onSeekSliderChanged(CCObject*) {
    if (!m_seekSlider) return;
    if (!Mod::get()->getSavedValue<bool>("menuLoopShowPlaybackProgress", true)) return;

    m_seekSliderDragging = true;

    auto* thumb = m_seekSlider->getThumb();
    const float v = thumb ? thumb->getValue() : m_seekSlider->getValue();
    const int pct = static_cast<int>(std::round(std::clamp(v, 0.f, 1.f) * 100.f));
    paimon::menuloop::MenuLoopControl::setSongPercentage(pct);
}

void MenuMusicPopup::tickSeekUpdate(float) {
    const bool show = Mod::get()->getSavedValue<bool>("menuLoopShowPlaybackProgress", true);
    if (m_seekRow) m_seekRow->setVisible(show);
    if (!show) return;

    auto* fmod = FMODAudioEngine::sharedEngine();
    if (!fmod || !fmod->m_backgroundMusicChannel) return;

    auto& loop = paimon::menuloop::MenuLoopManager::get();
    const std::string& current = loop.getCurrentSong();

    auto resetVisuals = [&]() {
        if (m_seekCurLabel) m_seekCurLabel->setString("--:--");
        if (m_seekTotalLabel) m_seekTotalLabel->setString("--:--");
        if (m_seekSlider && !m_seekSliderDragging) m_seekSlider->setValue(0.f);
    };

    if (fmod->getActiveMusic(0) != current) { resetVisuals(); return; }

    const int pos = loop.getLastMenuLoopPosition();
    const int total = fmod->getMusicLengthMS(0);
    if (total <= 0) { resetVisuals(); return; }

    const float ratio = std::clamp(
        static_cast<float>(pos) / static_cast<float>(total), 0.f, 1.f);

    // Actualizar el slider SOLO si el usuario no esta arrastrando. Si
    // acaba de soltar, el drag flag se apaga aqui cuando el valor del
    // slider y el del audio ya estan suficientemente cerca.
    if (m_seekSlider) {
        if (m_seekSliderDragging) {
            auto* thumb = m_seekSlider->getThumb();
            const float thumbVal = thumb ? thumb->getValue() : m_seekSlider->getValue();
            if (std::abs(thumbVal - ratio) < 0.02f) {
                m_seekSliderDragging = false;
            }
        } else {
            m_seekSlider->setValue(ratio);
        }
    }

    auto fmtTime = [](int ms) {
        int secs = ms / 1000;
        return fmt::format("{}:{:02}", secs / 60, secs % 60);
    };
    if (m_seekCurLabel) m_seekCurLabel->setString(fmtTime(pos).c_str());
    if (m_seekTotalLabel) m_seekTotalLabel->setString(fmtTime(total).c_str());
}

// Keyboard shortcuts (YT/VLC/Spotify-like)
//
// Replica del SongControlMenu del mod de referencia:
//   Numrow / Numpad 0-9 → salto al 0%, 10%, ..., 90%
//   Shift+N / Ctrl+S / Ctrl+Right → shuffle
//   Shift+P / Ctrl+Left           → previous
//   Ctrl+R                         → restart current (0%)
//   Shift+Alt+B (win) / Shift+Ctrl+B (mac) → favorite
//   L / Right / ArrowRight → skip forward
//   J / Left / ArrowLeft   → skip backward
//   Escape → close popup

void MenuMusicPopup::keyDown(cocos2d::enumKeyCodes key, double p1) {
    using cocos2d::enumKeyCodes;
    if (key == enumKeyCodes::KEY_Escape) { this->onClose(nullptr); return; }
    if (key == enumKeyCodes::KEY_Space) return;

    const bool shortcutsEnabled =
        Mod::get()->getSavedValue<bool>("menuLoopEnableKeyboardShortcuts", true);
    if (!shortcutsEnabled) { Popup::keyDown(key, p1); return; }

#ifdef GEODE_IS_DESKTOP
    auto* kd = cocos2d::CCKeyboardDispatcher::get();
    if (!kd) { Popup::keyDown(key, p1); return; }
    const bool isShift = kd->getShiftKeyPressed();
#ifdef GEODE_IS_MACOS
    const bool isCmd = kd->getCommandKeyPressed();
    const bool isCtrl = kd->getControlKeyPressed();
    const bool isAlt = isCtrl; // mac uses ctrl for the "alt" role
    const bool ctrlOrCmd = isCmd;
#else
    const bool isCmd = false;
    const bool isCtrl = kd->getControlKeyPressed();
    const bool isAlt = kd->getAltKeyPressed();
    const bool ctrlOrCmd = isCtrl;
#endif

    // 0-9 → percentage jump
    if (key >= enumKeyCodes::KEY_Zero && key <= enumKeyCodes::KEY_Nine) {
        int pct = 10 * (static_cast<int>(key) - static_cast<int>(enumKeyCodes::KEY_Zero));
        paimon::menuloop::MenuLoopControl::setSongPercentage(pct);
        return;
    }
    if (key >= enumKeyCodes::KEY_NumPad0 && key <= enumKeyCodes::KEY_NumPad9) {
        int pct = 10 * (static_cast<int>(key) - static_cast<int>(enumKeyCodes::KEY_NumPad0));
        paimon::menuloop::MenuLoopControl::setSongPercentage(pct);
        return;
    }
    // Ctrl+R → restart
    if (ctrlOrCmd && key == enumKeyCodes::KEY_R) {
        paimon::menuloop::MenuLoopControl::setSongPercentage(0);
        return;
    }
    // Shuffle
    if ((ctrlOrCmd && key == enumKeyCodes::KEY_S) ||
        (isShift && key == enumKeyCodes::KEY_N) ||
        (ctrlOrCmd && (key == enumKeyCodes::KEY_ArrowRight || key == enumKeyCodes::KEY_Right))) {
        paimon::menuloop::MenuLoopControl::shuffleSong();
        refreshFromState();
        return;
    }
    // Previous
    if ((isShift && key == enumKeyCodes::KEY_P) ||
        (ctrlOrCmd && (key == enumKeyCodes::KEY_ArrowLeft || key == enumKeyCodes::KEY_Left))) {
        paimon::menuloop::MenuLoopControl::previousSong();
        refreshFromState();
        return;
    }
    // Favorite
    if (isShift && isAlt && key == enumKeyCodes::KEY_B) {
        paimon::menuloop::MenuLoopControl::favoriteSong();
        return;
    }
#else
    (void)key;
#endif
    // Playback progress (L/Right/J/Left)
    if (Mod::get()->getSavedValue<bool>("menuLoopShowPlaybackProgress", true)) {
        if (key == enumKeyCodes::KEY_Right || key == enumKeyCodes::KEY_ArrowRight ||
            key == enumKeyCodes::KEY_L) {
            paimon::menuloop::MenuLoopControl::skipForward();
            return;
        }
        if (key == enumKeyCodes::KEY_Left || key == enumKeyCodes::KEY_ArrowLeft ||
            key == enumKeyCodes::KEY_J) {
            paimon::menuloop::MenuLoopControl::skipBackward();
            return;
        }
    }
    Popup::keyDown(key, p1);
}

} // namespace paimon::menumusic
