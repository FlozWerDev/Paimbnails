#include "VolumeScrollManager.hpp"
#include "../../../utils/PaimonDrawNode.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::volscroll {

// ────────────────────────────────────────────────────────────────────────
// Diseño de dos fases — animacion BIDIRECCIONAL.
//
// Fase 1 — Chip compacto (~kPanelWidthMin × kPanelHeight):
//      ┌───────┐
//      │  MUS  │     anclado a la posicion central
//      └───────┘
//
// Fase 2 — Expandido (~kPanelWidthMax × kPanelHeight):
//      ┌──────────────────────────────────┐
//      │   75%  ▰▰▰▰░░░░░          MUS    │
//      └──────────────────────────────────┘
//
// El contenedor tiene anchor (0.5, 0) → su PUNTO INFERIOR-CENTRAL es el ancla
// fija en pantalla. Al expandirse el ancho, la pildora crece simetricamente
// hacia los DOS lados desde ese centro. El label MUS/SFX permanece anclado al
// extremo derecho del contenedor (su anchor es (1, 0.5)), por lo que se
// desplaza visualmente hacia la derecha mientras los extras (% + barra)
// aparecen en el espacio liberado a su izquierda.
//
// Importante: NO existe el label blanco "Music"/"SFX" — solo el chip MUS/SFX
// con color (azul para musica, naranja para sfx).
// ────────────────────────────────────────────────────────────────────────

namespace {
    // Dimensiones — chip vs expandido.
    constexpr float kPanelHeight   =  26.f;
    constexpr float kPanelWidthMin =  46.f;  // chip MUS/SFX
    constexpr float kPanelWidthMax = 138.f;  // expandido
    constexpr float kCornerRadius  = kPanelHeight * 0.5f;

    // Posicion en pantalla (desde esquina inferior-derecha).
    // 24 (margen base) + 130 (offset pedido por el usuario)
    constexpr float kMarginRight  = 154.f;
    constexpr float kMarginBottom =  22.f;

    // Animacion: tiempos — snappy entrada, orgánico expand, rápido salida
    constexpr float kSlideInTime  = 0.22f; // chip sube (rápido, snappy)
    constexpr float kExpandTime   = 0.38f; // chip se ensancha (más lento, orgánico)
    constexpr float kAutoHideTime = 1.30f; // tiempo visible expandido
    constexpr float kCollapseTime = 0.22f; // se contrae (rápido, no pesa)
    constexpr float kSlideOutTime = 0.25f; // chip baja (suave)

    // Distancia del slide vertical
    constexpr float kSlideOffset = 50.f;

    constexpr float kVolumeLerpSpeed = 22.f;

    constexpr int   kCornerSegments = 10;
    constexpr float kPi = 3.14159265358979323846f;

    // ── Curvas de easing rediseñadas ──────────────────────────────
    // easeOutQuart: desaceleración suave sin overshoot → slide-in fluido
    inline float easeOutQuart(float t) {
        t = std::clamp(t, 0.f, 1.f);
        float u = 1.f - t;
        return 1.f - u * u * u * u;
    }

    // easeOutQuint: desaceleración muy gradual → expansión suave
    inline float easeOutQuint(float t) {
        t = std::clamp(t, 0.f, 1.f);
        float u = 1.f - t;
        return 1.f - u * u * u * u * u;
    }

    // easeInQuad: aceleración suave → salida sin corte abrupto
    inline float easeInQuad(float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t * t;
    }

    // easeInOutCubic: simétrica → collapse suave (no frena de golpe al final)
    inline float easeInOutCubic(float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t < 0.5f ? 4.f * t * t * t : 1.f - 0.5f * (-2.f * t + 2.f) * (-2.f * t + 2.f) * (-2.f * t + 2.f);
    }

    // easeOutCubic: mantenida para extras fade (desfasado)
    inline float easeOutCubic(float t) {
        t = std::clamp(t, 0.f, 1.f);
        float u = 1.f - t;
        return 1.f - u * u * u;
    }

    // easeInOutQuad: transición simétrica suave para opacidad
    inline float easeInOutQuad(float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t < 0.5f ? 2.f * t * t : 1.f - 0.5f * (2.f * t - 2.f) * (2.f * t - 2.f);
    }

    // Construye el contorno de un rounded rect [0..w] × [0..h] con radio r.
    // Si 2r > min(w,h) clampamos para evitar artefactos.
    std::vector<CCPoint> buildPillOutline(float w, float h, float r) {
        r = std::min({r, w * 0.5f, h * 0.5f});
        std::vector<CCPoint> pts;
        pts.reserve(4 * kCornerSegments);

        auto addArc = [&](float cx, float cy, float startAngle) {
            for (int i = 0; i < kCornerSegments; ++i) {
                float a = startAngle + (kPi * 0.5f) *
                          (static_cast<float>(i) / static_cast<float>(kCornerSegments));
                pts.push_back({cx + cosf(a) * r, cy + sinf(a) * r});
            }
        };
        // BL, BR, TR, TL (CCW)
        addArc(r,     r,     kPi);
        addArc(w - r, r,     kPi * 1.5f);
        addArc(w - r, h - r, 0.f);
        addArc(r,     h - r, kPi * 0.5f);
        return pts;
    }
}

// ────────────────────────────────────────────────────────────────────────
// Singleton
// ────────────────────────────────────────────────────────────────────────

VolumeScrollManager& VolumeScrollManager::get() {
    static VolumeScrollManager s_instance;
    return s_instance;
}

void VolumeScrollManager::init() {
    m_state = State::Hidden;
    m_animProgress = 0.f;
    m_expandProgress = 0.f;
    m_visibleTimer = 0.f;
    m_clock = 0.f;
    m_lastUseClock = -100.f;
}

// ────────────────────────────────────────────────────────────────────────
// Construccion del overlay (lazy)
// ────────────────────────────────────────────────────────────────────────

void VolumeScrollManager::ensureOverlayBuilt() {
    if (m_overlay) return;

    auto container = CCLayerRGBA::create();
    container->setContentSize({kPanelWidthMin, kPanelHeight});
    container->setAnchorPoint({0.5f, 0.f}); // bottom-center → expansion bidireccional
    container->setID("paimon-volume-scroll-overlay"_spr);
    container->setZOrder(99999);
    container->setCascadeOpacityEnabled(true);
    container->setCascadeColorEnabled(true);
    container->setTouchEnabled(false);

    // ── Pildora de fondo (PaimonDrawNode, ancho dinamico) ───────
    auto pill = PaimonDrawNode::create();
    pill->setPosition({0.f, 0.f});
    pill->setID("paimon-vs-pill"_spr);
    container->addChild(pill, 0);
    m_pillNode = pill;

    // ── Chip MUS/SFX — UNICO label de tipo, con color ───────────
    // Anchor (1, 0.5): se ancla al borde derecho del contenedor. Al
    // expandirse el contenedor (que crece simetricamente hacia ambos
    // lados desde el centro), el label se desplaza visualmente hacia
    // la derecha en pantalla, dejando espacio a su izquierda para
    // que aparezcan los extras (% y barra).
    auto icon = CCLabelBMFont::create("MUS", "bigFont.fnt");
    icon->setScale(0.34f);
    icon->setAnchorPoint({1.f, 0.5f});
    icon->setPosition({kPanelWidthMin - 12.f, kPanelHeight * 0.5f});
    icon->setColor({160, 220, 255});
    icon->setID("paimon-vs-icon"_spr);
    container->addChild(icon, 3);
    m_iconLabel = icon;

    // (No hay label blanco "Music"/"SFX" — m_kindLabel queda nullptr)
    m_kindLabel = nullptr;

    // ── Mini barra (rounded, dos PaimonDrawNode: bg + fill) ─────
    auto barBg = PaimonDrawNode::create();
    barBg->setID("paimon-vs-bar-bg"_spr);
    barBg->setOpacity(0);
    barBg->setVisible(false);
    container->addChild(barBg, 1);
    m_barBg = barBg;

    auto barFill = PaimonDrawNode::create();
    barFill->setID("paimon-vs-bar-fill"_spr);
    barFill->setOpacity(0);
    barFill->setVisible(false);
    container->addChild(barFill, 2);
    m_barFill = barFill;

    // ── Label del porcentaje (solo visible expandido) ───────────
    auto pctLabel = CCLabelBMFont::create("0%", "bigFont.fnt");
    pctLabel->setScale(0.28f);
    pctLabel->setAnchorPoint({1.f, 0.5f});
    pctLabel->setColor({225, 225, 235});
    pctLabel->setID("paimon-vs-pct"_spr);
    pctLabel->setOpacity(0);
    pctLabel->setVisible(false);
    container->addChild(pctLabel, 2);
    m_label = pctLabel;

    // Estado inicial: invisible
    container->setOpacity(0);
    container->setVisible(false);

    m_overlay = container;
    redrawPill(); // render inicial con ancho min
    redrawBar();
}

// ────────────────────────────────────────────────────────────────────────
// Dibujado de la pildora (fondo) con el ancho actual del contenedor.
// ────────────────────────────────────────────────────────────────────────

void VolumeScrollManager::redrawPill() {
    if (!m_pillNode || !m_overlay) return;

    auto* pill = static_cast<PaimonDrawNode*>(m_pillNode.data());
    pill->clear();

    const float w = m_overlay->getContentSize().width;
    const float h = m_overlay->getContentSize().height;
    const float r = kCornerRadius;

    auto pts = buildPillOutline(w, h, r);

    // Fondo siempre oscuro frio para legibilidad.
    const ccColor4F fill = ccc4FFromccc4B({18, 20, 32, 230});

    // Borde tintado segun el tipo activo, asi musica y sfx se distinguen
    // claramente sin tener que leer el chip. Mismos colores que m_iconLabel
    // pero con menos opacidad para que el trazo sea fino y discreto.
    const ccColor4F stroke = (m_currentKind == VolumeKind::Music)
        ? ccc4FFromccc4B({160, 220, 255, 160})  // azul (igual que chip Music)
        : ccc4FFromccc4B({255, 200, 140, 160}); // naranja (igual que chip SFX)

    pill->drawPolygon(
        pts.data(),
        static_cast<unsigned int>(pts.size()),
        fill,
        0.6f,
        stroke
    );
}

// ────────────────────────────────────────────────────────────────────────
// Dibujado de la barra de volumen — rounded rect bg + rounded rect fill.
// Posicion / dimensiones se calculan en applyExpandProgress: aqui solo
// renderizamos a partir del ContentSize de cada nodo.
// ────────────────────────────────────────────────────────────────────────

void VolumeScrollManager::redrawBar() {
    if (!m_barBg || !m_barFill) return;

    // Fondo: pildora oscura
    {
        auto* bg = static_cast<PaimonDrawNode*>(m_barBg.data());
        bg->clear();
        const auto sz = bg->getContentSize();
        if (sz.width > 1.f && sz.height > 0.5f) {
            float r = std::min(sz.height * 0.5f, sz.width * 0.5f);
            auto pts = buildPillOutline(sz.width, sz.height, r);
            const ccColor4F fillC = ccc4FFromccc4B({60, 60, 80, 220});
            bg->drawPolygon(
                pts.data(),
                static_cast<unsigned int>(pts.size()),
                fillC,
                0.f,
                ccc4f(0,0,0,0)
            );
        }
    }
    // Fill: pildora con tinte segun nivel
    {
        auto* fill = static_cast<PaimonDrawNode*>(m_barFill.data());
        fill->clear();
        const auto sz = fill->getContentSize();
        if (sz.width > 1.f && sz.height > 0.5f) {
            float r = std::min(sz.height * 0.5f, sz.width * 0.5f);
            auto pts = buildPillOutline(sz.width, sz.height, r);

            ccColor3B c = {120, 200, 255};
            if (m_displayedVolume < 0.15f) c = {255, 130, 130};
            else if (m_displayedVolume > 0.85f) c = {180, 255, 180};

            const ccColor4F fillC = ccc4FFromccc4B({c.r, c.g, c.b, 240});
            fill->drawPolygon(
                pts.data(),
                static_cast<unsigned int>(pts.size()),
                fillC,
                0.f,
                ccc4f(0,0,0,0)
            );
        }
    }
}

// ────────────────────────────────────────────────────────────────────────
// applyExpandProgress
//
// Interpola entre el chip (kPanelWidthMin) y el panel expandido
// (kPanelWidthMax) usando m_expandProgress ∈ [0..1]. Tambien ajusta:
//   * ancho del contenedor → el ancla esta en (1,0), por lo que crece
//     hacia la izquierda (tipo "popup que sale del chip")
//   * opacidad de los hijos extra (kindLabel, barra, %), con un fade
//     desfasado: aparecen cuando el ancho ya esta al ~30% del recorrido.
//   * posicion del chip MUS/SFX: pegado al borde derecho del contenedor.
//   * posicion / tamaño de la barra y label %.
// ────────────────────────────────────────────────────────────────────────

void VolumeScrollManager::applyExpandProgress() {
    if (!m_overlay) return;

    const float t = std::clamp(m_expandProgress, 0.f, 1.f);
    // Expand: easeOutQuint (desaceleración gradual)
    // Collapse: easeInOutCubic (simétrica, no frena de golpe al encogerse)
    const float eased = (m_state == State::Collapsing) ? easeInOutCubic(t) : easeOutQuint(t);

    // Ancho actual del contenedor. Crece hacia los DOS lados gracias al
    // anchor (0.5, 0).
    const float w = kPanelWidthMin + (kPanelWidthMax - kPanelWidthMin) * eased;
    m_overlay->setContentSize({w, kPanelHeight});

    // El chip MUS/SFX siempre pegado al borde derecho del contenedor.
    // Como el contenedor crece simetricamente, en coordenadas de pantalla
    // el chip se desplaza hacia la derecha.
    if (m_iconLabel) {
        m_iconLabel->setPosition({w - 12.f, kPanelHeight * 0.5f});
    }

    // Opacidad de los extras (% + barra): aparecen cuando el ancho ya esta
    // al ~30% del recorrido — primero se "abre" el panel, despues aparecen
    // los extras en el espacio liberado a la izquierda del chip.
    const float extraT = std::clamp((t - 0.30f) / 0.70f, 0.f, 1.f);
    const float extraEased = easeOutQuint(extraT);
    const GLubyte extraOpacity = static_cast<GLubyte>(extraEased * 255.f);
    const bool extrasVisible = extraT > 0.f;

    // Layout del estado expandido — todo a la IZQUIERDA del chip:
    //
    //   [ %  ▰▰▰▰░░░░                             MUS ]
    //
    //   kPctLeftPad: distancia del % al borde izquierdo del contenedor.
    //   barra justo a la derecha del %, con un pequeño gap.
    //   chip MUS/SFX siempre en w-12 (ya posicionado arriba).
    constexpr float kPctLeftPad   = 12.f;
    constexpr float kBarLeftAfter = 38.f;  // x donde empieza la barra
    constexpr float kBarWMax      = 56.f;
    constexpr float kBarH         = 5.f;
    const float barLeftX = kBarLeftAfter;
    const float barY     = (kPanelHeight - kBarH) * 0.5f;

    if (m_label) {
        m_label->setVisible(extrasVisible);
        m_label->setOpacity(extraOpacity);
        // % anclado a la izquierda
        m_label->setAnchorPoint({0.f, 0.5f});
        m_label->setPosition({kPctLeftPad, kPanelHeight * 0.5f});
    }

    if (m_barBg) {
        auto* bg = m_barBg.data();
        bg->setVisible(extrasVisible);
        bg->setOpacity(extraOpacity);
        bg->setContentSize({kBarWMax, kBarH});
        bg->setPosition({barLeftX, barY});
    }

    if (m_barFill) {
        auto* fillNode = m_barFill.data();
        fillNode->setVisible(extrasVisible);
        fillNode->setOpacity(extraOpacity);
        const float fillW = kBarWMax * std::clamp(m_displayedVolume, 0.f, 1.f);
        fillNode->setContentSize({fillW, kBarH});
        fillNode->setPosition({barLeftX, barY});
    }
}

void VolumeScrollManager::attachToRunningScene() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    if (m_attachedScene == scene && m_overlay && m_overlay->getParent() == scene) return;

    ensureOverlayBuilt();
    if (!m_overlay) return;

    if (m_overlay->getParent()) {
        m_overlay->removeFromParent();
    }

    auto winSize = CCDirector::get()->getWinSize();
    m_overlay->setPosition({winSize.width - kMarginRight, kMarginBottom});
    scene->addChild(m_overlay.data(), 99999);
    m_attachedScene = scene;
}

void VolumeScrollManager::detachFromScene() {
    if (m_overlay && m_overlay->getParent()) {
        m_overlay->removeFromParent();
    }
    m_attachedScene = nullptr;
}

void VolumeScrollManager::onSceneChange() {
    if (m_state != State::Hidden) {
        attachToRunningScene();
    } else if (m_overlay && m_overlay->getParent()) {
        m_overlay->removeFromParent();
        m_attachedScene = nullptr;
    }
}

void VolumeScrollManager::releaseSharedResources() {
    detachFromScene();
    m_overlay = nullptr;
    m_iconLabel = nullptr;
    m_kindLabel = nullptr;
    m_label = nullptr;
    m_barBg = nullptr;
    m_barFill = nullptr;
    m_pillNode = nullptr;
    m_state = State::Hidden;
}

// ────────────────────────────────────────────────────────────────────────
// Volumen: lectura/escritura mediante FMODAudioEngine
// ────────────────────────────────────────────────────────────────────────

float VolumeScrollManager::readVolume(VolumeKind kind) const {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return 0.f;
    if (kind == VolumeKind::Music) {
        return std::clamp(engine->m_musicVolume, 0.f, 1.f);
    }
    return std::clamp(engine->m_sfxVolume, 0.f, 1.f);
}

void VolumeScrollManager::writeVolume(VolumeKind kind, float value) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;
    value = std::clamp(value, 0.f, 1.f);

    if (kind == VolumeKind::Music) {
        engine->m_musicVolume = value;
        engine->setBackgroundMusicVolume(value);
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(value);
        }
    } else {
        engine->m_sfxVolume = value;
        engine->setEffectsVolume(value);
    }
}

// ────────────────────────────────────────────────────────────────────────
// Update: avanza la maquina de estados.
// ────────────────────────────────────────────────────────────────────────

void VolumeScrollManager::update(float dt) {
    m_clock += dt;

    // Lerp del valor mostrado.
    float diff = m_targetVolume - m_displayedVolume;
    m_displayedVolume += diff * std::clamp(kVolumeLerpSpeed * dt, 0.f, 1.f);

    if (m_state == State::Hidden) return;
    if (!m_overlay) {
        m_state = State::Hidden;
        return;
    }

    // ── Maquina de estados ──────────────────────────────────────────
    switch (m_state) {
        case State::SlidingIn:
            m_animProgress += dt / kSlideInTime;
            if (m_animProgress >= 1.f) {
                m_animProgress = 1.f;
                m_state = State::Expanding;
                m_expandProgress = 0.f;
            }
            break;

        case State::Expanding:
            m_expandProgress += dt / kExpandTime;
            if (m_expandProgress >= 1.f) {
                m_expandProgress = 1.f;
                m_state = State::Visible;
                m_visibleTimer = kAutoHideTime;
            }
            break;

        case State::Visible:
            m_visibleTimer -= dt;
            if (m_visibleTimer <= 0.f) {
                m_state = State::Collapsing;
            }
            break;

        case State::Collapsing:
            m_expandProgress -= dt / kCollapseTime;
            if (m_expandProgress <= 0.f) {
                m_expandProgress = 0.f;
                m_state = State::SlidingOut;
            }
            break;

        case State::SlidingOut:
            m_animProgress -= dt / kSlideOutTime;
            if (m_animProgress <= 0.f) {
                m_animProgress = 0.f;
                m_state = State::Hidden;
                m_overlay->setVisible(false);
                detachFromScene();
                return;
            }
            break;

        default: break;
    }

    // ── Aplicar el ancho/contenido segun expandProgress ──
    applyExpandProgress();
    redrawPill();
    if (m_expandProgress > 0.f) {
        redrawBar();
    }

    // ── Aplicar el slide vertical + fade del contenedor ──
    // Entrada: easeOutQuart (desaceleración suave, sin freno seco)
    // Salida: easeInQuad (aceleración suave)
    const float slide = (m_state == State::SlidingOut)
                        ? easeInQuad(m_animProgress)
                        : easeOutQuart(m_animProgress);

    // Opacidad separada: easeInOutQuad para fade más suave que el slide
    const float opacity = easeInOutQuad(m_animProgress);

    const float y = kMarginBottom - kSlideOffset * (1.f - std::clamp(slide, 0.f, 1.f));
    auto winSize = CCDirector::get()->getWinSize();
    m_overlay->setPosition({winSize.width - kMarginRight, y});
    m_overlay->setOpacity(static_cast<GLubyte>(std::clamp(opacity, 0.f, 1.f) * 255.f));
    m_overlay->setVisible(true);

    // Update del label %
    if (m_label) {
        int pct = static_cast<int>(std::round(m_displayedVolume * 100.f));
        std::string s = std::to_string(pct) + "%";
        m_label->setString(s.c_str());
    }
}

void VolumeScrollManager::startSlideOut() {
    // Solo se llama desde rutas de cancelacion; el flujo normal pasa por
    // Collapsing → SlidingOut. Lo dejamos por compatibilidad.
    if (m_state == State::SlidingOut || m_state == State::Hidden) return;
    m_state = State::Collapsing;
}

void VolumeScrollManager::resetAutoHideTimer() {
    m_visibleTimer = kAutoHideTime;
}

void VolumeScrollManager::rebuildContent() {
    if (!m_overlay) return;
    if (m_iconLabel) {
        m_iconLabel->setString(m_currentKind == VolumeKind::Music ? "MUS" : "SFX");
        m_iconLabel->setColor(m_currentKind == VolumeKind::Music
                              ? ccColor3B{160, 220, 255}
                              : ccColor3B{255, 200, 140});
    }
    if (m_kindLabel) {
        m_kindLabel->setString(m_currentKind == VolumeKind::Music ? "Music" : "SFX");
    }
}

// ────────────────────────────────────────────────────────────────────────
// onScroll
// ────────────────────────────────────────────────────────────────────────

bool VolumeScrollManager::onScroll(VolumeKind kind, float delta) {
    float current = readVolume(kind);
    float next = std::clamp(current + delta, 0.f, 1.f);
    writeVolume(kind, next);
    m_targetVolume = next;
    if (m_state == State::Hidden) {
        m_displayedVolume = current;
    }

    if (kind != m_currentKind) {
        m_currentKind = kind;
        m_displayedVolume = next;
    }

    attachToRunningScene();
    rebuildContent();

    // Despertar al overlay segun el estado actual.
    switch (m_state) {
        case State::Hidden:
            m_state = State::SlidingIn;
            m_animProgress = 0.f;
            m_expandProgress = 0.f;
            break;

        case State::SlidingOut:
            // Re-engancha el slide-in desde donde estaba (no resetea
            // animProgress) y el chip vuelve a expandirse.
            m_state = State::SlidingIn;
            break;

        case State::Collapsing:
            // Estaba colapsando — vuelve a expandirse desde el progreso actual.
            m_state = State::Expanding;
            break;

        case State::Visible:
            // Reinicia el contador de auto-hide.
            resetAutoHideTimer();
            break;

        case State::SlidingIn:
        case State::Expanding:
            // Ya esta entrando / expandiendo. No tocar.
            break;
    }

    m_lastUseClock = m_clock;
    return true;
}

bool VolumeScrollManager::wasRecentlyUsed(float withinSeconds) const {
    return (m_clock - m_lastUseClock) < withinSeconds;
}

} // namespace paimon::volscroll
