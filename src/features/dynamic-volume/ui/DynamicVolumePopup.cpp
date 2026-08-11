#include "DynamicVolumePopup.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 300.f;

constexpr float kCurveHeight = 96.f;

std::vector<std::string> const kModeNames = {"Adaptativo", "Fijo", "Personalizado"};

char const* modeDescription(paimon::dynvol::Mode mode) {
    switch (mode) {
        case paimon::dynvol::Mode::Adaptive:
            return "Si la cancion nueva entra mas fuerte, empieza al volumen de "
                   "la anterior y sube poco a poco hasta el normal.";
        case paimon::dynvol::Mode::Fixed:
            return "Todas las canciones suenan al mismo volumen percibido. No "
                   "sube despues: se queda fijo.";
        case paimon::dynvol::Mode::Custom:
            return "Tu decides el objetivo, la curva, cuanto baja y donde se "
                   "aplica.";
    }
    return "";
}

std::vector<std::string> const kCurveNames = {
    "Lineal", "Lenta al inicio", "Rapida al inicio", "Suave (S)", "Exponencial", "Escalones"
};

int curveIndex(paimon::dynvol::Curve c) {
    switch (c) {
        case paimon::dynvol::Curve::Linear:      return 0;
        case paimon::dynvol::Curve::EaseIn:      return 1;
        case paimon::dynvol::Curve::EaseOut:     return 2;
        case paimon::dynvol::Curve::SmoothStep:  return 3;
        case paimon::dynvol::Curve::Exponential: return 4;
        case paimon::dynvol::Curve::Steps:       return 5;
    }
    return 2;
}

paimon::dynvol::Curve curveFromIndex(int i) {
    using C = paimon::dynvol::Curve;
    switch (i) {
        case 0:  return C::Linear;
        case 1:  return C::EaseIn;
        case 3:  return C::SmoothStep;
        case 4:  return C::Exponential;
        case 5:  return C::Steps;
        default: return C::EaseOut;
    }
}

std::string formatSeconds(double v) { return fmt::format("{:.1f}s", v); }
std::string formatDb(double v)      { return fmt::format("{:+.1f} dB", v); }
std::string formatDbCut(double v)   { return fmt::format("{:.0f} dB", v); }
std::string formatLufs(double v)    { return fmt::format("{:.0f} LUFS", v); }

} // namespace

namespace paimon::dynvol {

DynamicVolumePopup* DynamicVolumePopup::create() {
    auto* ret = new DynamicVolumePopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool DynamicVolumePopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    setTitle("Volumen Dinamico");

    DynamicVolumeManager::get().loadConfig();
    m_cfg = DynamicVolumeManager::get().getConfig();

    rebuild();

    auto* resetSpr = ButtonSprite::create("Restaurar", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    if (resetSpr) resetSpr->setScale(0.55f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
        [this](CCMenuItemSpriteExtra*) {
            bool const wasEnabled = m_cfg.enabled;
            m_cfg = DynamicVolumeConfig{};
            m_cfg.enabled = wasEnabled;
            DynamicVolumeManager::get().setSafeDropEnabled(true);
            DynamicVolumeManager::get().saveConfig(m_cfg);
            DynamicVolumeManager::get().resetRuntimeState();
            scheduleRebuild();
            PaimonNotify::create("Volumen Dinamico restablecido", NotificationIcon::Success)->show();
        });
    resetBtn->setID("dynamic-volume-default-btn"_spr);
    resetBtn->setPosition({m_mainLayer->getContentSize().width / 2.f, 20.f});
    m_buttonMenu->addChild(resetBtn);

    this->scheduleUpdate();
    return true;
}

void DynamicVolumePopup::update(float dt) {
    m_uiClock += dt;

    if (m_uiClock - m_lastMeterRefresh >= 0.12f) {
        m_lastMeterRefresh = m_uiClock;
        refreshLiveMeter();
    }

    if (m_curveDraw) {
        auto const st = DynamicVolumeManager::get().liveState();
        float const head = (st.active && m_cfg.mode != Mode::Fixed && st.rampProgress < 1.f)
            ? st.rampProgress : -1.f;
        if (m_curveDirty || std::abs(head - m_lastCurveHead) > 0.004f) {
            m_lastCurveHead = head;
            m_curveDirty = false;
            redrawCurve();
        }
    }
}

void DynamicVolumePopup::scheduleRebuild() {
    Ref<DynamicVolumePopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (self && self->getParent()) self->rebuild();
    });
}

void DynamicVolumePopup::persist() {
    DynamicVolumeManager::get().saveConfig(m_cfg);
    // saveConfig normalises the mode presets; mirror them back so the sliders
    // don't show values the manager already discarded.
    m_cfg = DynamicVolumeManager::get().getConfig();
    m_curveDirty = true;
}


CCNode* DynamicVolumePopup::makeLiveMeter(float width) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, 34.f});

    auto* main = CCLabelBMFont::create("--", "bigFont.fnt");
    main->setAnchorPoint({0.f, 1.f});
    main->setScale(0.4f);
    main->setPosition({12.f, 30.f});
    main->setColor(kit::kValueColor);
    row->addChild(main);
    m_meterMain = main;

    auto* sub = CCLabelBMFont::create("", "chatFont.fnt");
    sub->setAnchorPoint({0.f, 1.f});
    sub->setScale(0.44f);
    sub->setPosition({12.f, 14.f});
    sub->setColor(kit::kDescColor);
    row->addChild(sub);
    m_meterSub = sub;

    refreshLiveMeter();
    return row;
}

void DynamicVolumePopup::refreshLiveMeter() {
    if (!m_meterMain || !m_meterSub) return;

    auto const st = DynamicVolumeManager::get().liveState();

    if (!st.active) {
        m_meterMain->setString("Inactivo");
        m_meterMain->setColor(kit::kOffColor);
        m_meterSub->setString(m_cfg.enabled
            ? "Esperando musica..."
            : "Activa el modo para empezar a medir.");
        return;
    }

    m_meterMain->setColor(kit::kValueColor);
    m_meterMain->setString(fmt::format("Ganancia {:+.1f} dB", st.appliedGainDb).c_str());

    std::string sub;
    if (isValidLufs(st.songLufs)) {
        sub = fmt::format("Cancion {:.1f} LUFS", st.songLufs);
    } else {
        sub = "Midiendo...";
    }
    if (isValidLufs(st.referenceDb)) {
        sub += fmt::format("  |  Objetivo {:.1f} LUFS", st.referenceDb);
    }
    if (st.analyzing) {
        sub += "  |  analizando";
    } else if (m_cfg.mode != Mode::Fixed && st.rampProgress < 1.f) {
        sub += fmt::format("  |  subiendo {:.0f}%", st.rampProgress * 100.f);
    }
    if (st.safeDropActive && st.safeDropGainDb < -0.05f) {
        sub += fmt::format("  |  Safe Drop {:+.1f} dB", st.safeDropGainDb);
    }
    m_meterSub->setString(sub.c_str());
}


CCNode* DynamicVolumePopup::makeCurvePreview(float width) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kCurveHeight});

    m_curveArea = CCSize{width - 34.f, kCurveHeight - 30.f};

    auto* draw = CCDrawNode::create();
    draw->setPosition({22.f, 20.f});
    row->addChild(draw);
    m_curveDraw = draw;

    auto* caption = CCLabelBMFont::create("", "chatFont.fnt");
    caption->setAnchorPoint({0.f, 0.f});
    caption->setScale(0.42f);
    caption->setPosition({12.f, 5.f});
    caption->setColor(kit::kDescColor);
    row->addChild(caption);
    m_curveCaption = caption;

    auto* top = CCLabelBMFont::create("100%", "chatFont.fnt");
    top->setAnchorPoint({1.f, 0.5f});
    top->setScale(0.34f);
    top->setPosition({20.f, 20.f + m_curveArea.height});
    top->setColor(kit::kDescColor);
    row->addChild(top);

    auto* bottom = CCLabelBMFont::create("bajo", "chatFont.fnt");
    bottom->setAnchorPoint({1.f, 0.5f});
    bottom->setScale(0.34f);
    bottom->setPosition({20.f, 20.f});
    bottom->setColor(kit::kDescColor);
    row->addChild(bottom);

    redrawCurve();
    return row;
}

void DynamicVolumePopup::redrawCurve() {
    if (!m_curveDraw) return;

    auto const w = m_curveArea.width;
    auto const h = m_curveArea.height;
    if (w <= 1.f || h <= 1.f) return;

    m_curveDraw->clear();

    ccColor4F const grid   = {1.f, 1.f, 1.f, 0.14f};
    ccColor4F const line   = {0.47f, 0.82f, 1.f, 1.f};
    ccColor4F const marker = {1.f, 0.87f, 0.47f, 1.f};

    m_curveDraw->drawSegment({0.f, 0.f}, {w, 0.f}, 0.6f, grid);
    m_curveDraw->drawSegment({0.f, 0.f}, {0.f, h}, 0.6f, grid);
    m_curveDraw->drawSegment({0.f, h}, {w, h}, 0.6f, grid);

    bool const flat = m_cfg.mode == Mode::Fixed;

    constexpr int kSamples = 64;
    CCPoint prev = {0.f, flat ? h : 0.f};
    for (int i = 1; i <= kSamples; ++i) {
        float const t = static_cast<float>(i) / kSamples;
        float const p = flat ? 1.f : curveProgress(m_cfg.curve, t, m_cfg.curveStrength);
        CCPoint const cur = {t * w, p * h};
        m_curveDraw->drawSegment(prev, cur, 1.1f, line);
        prev = cur;
    }

    // Playhead: where the current song sits on this curve right now.
    auto const st = DynamicVolumeManager::get().liveState();
    if (st.active && !flat && st.rampProgress < 1.f) {
        float const p = std::clamp(st.rampProgress, 0.f, 1.f);
        // rampProgress is the curve output; invert visually by sampling time.
        float t = 0.f;
        for (int i = 0; i <= kSamples; ++i) {
            float const tt = static_cast<float>(i) / kSamples;
            if (curveProgress(m_cfg.curve, tt, m_cfg.curveStrength) >= p) { t = tt; break; }
            t = tt;
        }
        m_curveDraw->drawSegment({t * w, 0.f}, {t * w, h}, 0.8f, marker);
        m_curveDraw->drawDot({t * w, p * h}, 2.6f, marker);
    }

    if (m_curveCaption) {
        std::string caption = flat
            ? "Sin subida: la correccion se mantiene mientras suena la cancion."
            : fmt::format("De {:.0f} dB hasta el volumen normal en {:.1f}s.",
                          m_cfg.initialDuckDb, m_cfg.rampSeconds);
        m_curveCaption->setString(caption.c_str());
    }
}


void DynamicVolumePopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }
    m_modeDescLabel = nullptr;
    m_curveDraw     = nullptr;
    m_curveCaption  = nullptr;
    m_meterMain     = nullptr;
    m_meterSub      = nullptr;

    auto const content = m_mainLayer->getContentSize();
    float const scrollW = content.width - 24.f;
    float const scrollH = content.height - 36.f - 38.f;
    float const innerW  = kit::cardInnerWidth(scrollW);

    auto* hero = kit::makeHeroToggle(scrollW,
        "Volumen Dinamico",
        "Iguala canciones y protege de subidas repentinas.",
        m_cfg.enabled,
        [this](bool v) {
            m_cfg.enabled = v;
            persist();
            if (!v) DynamicVolumeManager::get().resetRuntimeState();
        });

    auto* tabs = kit::makeTabBar(scrollW, {"Basico", "Curva", "Avanzado"}, m_tab,
        [this](int i) {
            m_tab = i;
            scheduleRebuild();
        });

    std::vector<CCNode*> items = {hero, tabs};

    if (m_tab == 0) {
        auto* modeRow = kit::makeSelectRow(innerW,
            "Modo", nullptr,
            kModeNames, static_cast<int>(m_cfg.mode),
            [this](int idx) {
                m_cfg.mode = static_cast<Mode>(std::clamp(idx, 0, 2));
                if (m_modeDescLabel) m_modeDescLabel->setString(modeDescription(m_cfg.mode));
                persist();
                DynamicVolumeManager::get().resetRuntimeState();
                scheduleRebuild();
            });

        auto* descRow = CCNode::create();
        descRow->setAnchorPoint({0.f, 0.f});
        descRow->setContentSize({innerW, 30.f});
        {
            auto* lbl = CCLabelBMFont::create(modeDescription(m_cfg.mode), "chatFont.fnt",
                (innerW - 24.f) / 0.46f, kCCTextAlignmentLeft);
            lbl->setScale(0.46f);
            lbl->setAnchorPoint({0.f, 1.f});
            lbl->setColor(kit::kDescColor);
            lbl->setPosition({12.f, 28.f});
            descRow->addChild(lbl);
            m_modeDescLabel = lbl;
        }

        items.push_back(kit::makeCard(scrollW, "Modo", {255, 165, 210}, {modeRow, descRow}));

        auto& manager = DynamicVolumeManager::get();
        items.push_back(kit::makeCard(scrollW, "Proteccion", {255, 195, 95}, {
            kit::makeToggleRow(innerW, "Safe Drop",
                "Frena picos y cambios bruscos de efectos antes de devolver el volumen.",
                manager.isSafeDropEnabled(),
                [](bool enabled) { DynamicVolumeManager::get().setSafeDropEnabled(enabled); }),
        }));

        std::vector<CCNode*> tuning;
        if (m_cfg.mode == Mode::Fixed) {
            tuning.push_back(kit::makeSliderRow(innerW,
                "Volumen objetivo", "Nivel al que se iguala cada cancion. Mas a la "
                                    "izquierda = todo mas bajo.",
                m_cfg.targetLufs, -30.0, -6.0, formatLufs,
                [this](double v) { m_cfg.targetLufs = static_cast<float>(v); persist(); }));
        } else {
            tuning.push_back(kit::makeSliderRow(innerW,
                "Tiempo de subida", "Cuanto tarda en volver al volumen normal.",
                m_cfg.rampSeconds, 1.0, 60.0, formatSeconds,
                [this](double v) { m_cfg.rampSeconds = static_cast<float>(v); persist(); }));
        }
        tuning.push_back(kit::makeSliderRow(innerW,
            "Baja como maximo", "Limite de cuanto puede bajar una cancion fuerte.",
            m_cfg.maxCutDb, 0.0, 30.0, formatDbCut,
            [this](double v) { m_cfg.maxCutDb = static_cast<float>(v); persist(); }));
        items.push_back(kit::makeCard(scrollW, "Ajuste", {120, 210, 255}, tuning));

        items.push_back(kit::makeCard(scrollW, "En vivo", {130, 240, 170},
                                      {makeLiveMeter(innerW)}));

        items.push_back(kit::makeCard(scrollW, "Donde se aplica", {130, 240, 170}, {
            kit::makeToggleRow(innerW, "Menus", nullptr, m_cfg.inMenus,
                [this](bool v) { m_cfg.inMenus = v; persist(); }),
            kit::makeToggleRow(innerW, "Jugando", nullptr, m_cfg.inGameplay,
                [this](bool v) { m_cfg.inGameplay = v; persist(); }),
            kit::makeToggleRow(innerW, "Editor", nullptr, m_cfg.inEditor,
                [this](bool v) { m_cfg.inEditor = v; persist(); }),
            kit::makeToggleRow(innerW, "Repetir al reintentar",
                "Vuelve a bajar el volumen cada vez que reinicias el mismo nivel.",
                m_cfg.reduckSameSong,
                [this](bool v) { m_cfg.reduckSameSong = v; persist(); }),
        }));

        if (m_cfg.mode != Mode::Custom) {
            items.push_back(kit::makeHint(scrollW,
                "El modo Personalizado desbloquea la curva, la bajada inicial, "
                "los margenes y el tiempo de medida."));
        }
    } else if (m_tab == 1) {
        // Only Custom keeps these: the other modes pin them, and a slider that
        // snaps back on release is worse than no slider.
        if (m_cfg.mode == Mode::Custom) {
            items.push_back(kit::makeCard(scrollW, "Curva de volumen", {170, 190, 255}, {
                kit::makeSelectRow(innerW,
                    "Curva", "Forma en la que sube el volumen.",
                    kCurveNames, curveIndex(m_cfg.curve),
                    [this](int idx) { m_cfg.curve = curveFromIndex(idx); persist(); }),
                kit::makeSliderRow(innerW,
                    "Dureza", "Que tan marcada es la forma de la curva.",
                    m_cfg.curveStrength, 1.0, 6.0,
                    [](double v) { return fmt::format("x{:.1f}", v); },
                    [this](double v) { m_cfg.curveStrength = static_cast<float>(v); persist(); }),
                kit::makeSliderRow(innerW,
                    "Tiempo de subida", "Duracion completa de la subida.",
                    m_cfg.rampSeconds, 1.0, 60.0, formatSeconds,
                    [this](double v) { m_cfg.rampSeconds = static_cast<float>(v); persist(); }),
            }));
        }

        items.push_back(kit::makeCard(scrollW, "Vista previa", {255, 210, 100},
                                      {makeCurvePreview(innerW)}));

        if (m_cfg.mode == Mode::Fixed) {
            items.push_back(kit::makeHint(scrollW,
                "El modo Fijo no sube: la curva solo se usa en Adaptativo y "
                "Personalizado."));
        } else if (m_cfg.mode != Mode::Custom) {
            items.push_back(kit::makeHint(scrollW,
                "El modo Adaptativo elige la curva por ti. Cambia a Personalizado "
                "para editarla."));
        }
    } else if (m_cfg.mode != Mode::Custom) {
        items.push_back(kit::makeHint(scrollW,
            "Estos ajustes son parte del modo. Cambia a Personalizado en la "
            "pestana Basico para editarlos."));
    } else {
        items.push_back(kit::makeCard(scrollW, "Entrada", {255, 140, 220}, {
            kit::makeSliderRow(innerW,
                "Bajada inicial", "Cuanto baja de golpe justo al empezar una cancion, "
                                  "antes de medirla.",
                m_cfg.initialDuckDb, -24.0, 0.0, formatDb,
                [this](double v) { m_cfg.initialDuckDb = static_cast<float>(v); persist(); }),
            kit::makeSliderRow(innerW,
                "Tiempo de medida", "Cuanto escucha antes de fijar el nivel de la cancion.",
                m_cfg.analysisSeconds, 0.4, 5.0, formatSeconds,
                [this](double v) { m_cfg.analysisSeconds = static_cast<float>(v); persist(); }),
            kit::makeSliderRow(innerW,
                "Suavizado", "Que tan lento se mueve la correccion. Mas alto = menos brusco.",
                m_cfg.smoothingSeconds, 0.05, 2.0, formatSeconds,
                [this](double v) { m_cfg.smoothingSeconds = static_cast<float>(v); persist(); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Limites", {120, 210, 255}, {
            kit::makeSliderRow(innerW,
                "Volumen objetivo", "Nivel al que iguala el modo Fijo.",
                m_cfg.targetLufs, -30.0, -6.0, formatLufs,
                [this](double v) { m_cfg.targetLufs = static_cast<float>(v); persist(); }),
            kit::makeSliderRow(innerW,
                "Baja como maximo", "Limite de atenuacion.",
                m_cfg.maxCutDb, 0.0, 30.0, formatDbCut,
                [this](double v) { m_cfg.maxCutDb = static_cast<float>(v); persist(); }),
            kit::makeSliderRow(innerW,
                "Sube como maximo", "Deja en 0 para no subir nunca las canciones bajas.",
                m_cfg.maxBoostDb, 0.0, 12.0, formatDbCut,
                [this](double v) { m_cfg.maxBoostDb = static_cast<float>(v); persist(); }),
            kit::makeSliderRow(innerW,
                "Margen", "Diferencias mas pequenas que esto se dejan tal cual.",
                m_cfg.thresholdDb, 0.0, 12.0, formatDbCut,
                [this](double v) { m_cfg.thresholdDb = static_cast<float>(v); persist(); }),
        }));
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 38.f});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::dynvol
