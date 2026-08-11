#include "DynamicSongPopup.hpp"

#include "../services/DynamicSongManager.hpp"
#include "../services/DynamicSongSubmerge.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <vector>

using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 300.f;

std::vector<std::string> const kStartModeNames = {"Al azar", "Desde el principio", "Continuar"};
std::vector<std::string> const kRotationNames  = {"Rotar", "Al azar", "Solo la principal"};
std::vector<std::string> const kPresetNames    = {
    "Personalizado", "Submarino", "Amortiguado", "Profundo", "Radio"
};

char const* startModeDescription(paimon::dynsong::StartMode mode) {
    switch (mode) {
        case paimon::dynsong::StartMode::Random:
            return "Cae en un punto cualquiera de la cancion, dentro de la zona que elijas.";
        case paimon::dynsong::StartMode::Beginning:
            return "Siempre desde el segundo cero.";
        case paimon::dynsong::StartMode::Resume:
            return "Sigue donde la dejaste en ese nivel. La primera vez cae al azar.";
        case paimon::dynsong::StartMode::Count:
            break;
    }
    return "";
}

char const* presetDescription(paimon::dynsong::SubmergePreset preset) {
    switch (preset) {
        case paimon::dynsong::SubmergePreset::Underwater:
            return "Como escuchar la cancion desde debajo del agua.";
        case paimon::dynsong::SubmergePreset::Muffled:
            return "Como si sonara detras de una puerta. Sin cambio de tono.";
        case paimon::dynsong::SubmergePreset::Deep:
            return "Mas hondo y mas lento. El corte se nota mucho.";
        case paimon::dynsong::SubmergePreset::Radio:
            return "Recortada por arriba y por abajo, como un altavoz pequeno.";
        case paimon::dynsong::SubmergePreset::Custom:
            return "Los controles de la pestana Avanzado mandan.";
        case paimon::dynsong::SubmergePreset::Count:
            break;
    }
    return "";
}

std::string formatSeconds(double v) { return fmt::format("{:.2f}s", v); }
std::string formatPercent(double v) { return fmt::format("{:.0f}%", v); }
std::string formatHz(double v)      { return fmt::format("{:.0f} Hz", v); }
std::string formatDb(double v)      { return fmt::format("{:+.1f} dB", v); }
std::string formatPitch(double v)   { return fmt::format("x{:.2f}", v); }

} // namespace

namespace paimon::dynsong {

DynamicSongPopup* DynamicSongPopup::create() {
    auto* ret = new DynamicSongPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool DynamicSongPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    setTitle("Cancion Dinamica");

    loadConfig();
    m_cfg = config();

    rebuild();

    auto* resetSpr = ButtonSprite::create("Restaurar", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    if (resetSpr) resetSpr->setScale(0.55f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
        [this](CCMenuItemSpriteExtra*) {
            m_cfg = DynamicSongConfig{};
            saveConfig(m_cfg);
            m_cfg = config();
            scheduleRebuild();
            PaimonNotify::create("Cancion Dinamica restablecida", NotificationIcon::Success)->show();
        });
    resetBtn->setID("dynamic-song-default-btn"_spr);
    resetBtn->setPosition({m_mainLayer->getContentSize().width / 2.f, 20.f});
    m_buttonMenu->addChild(resetBtn);

    this->scheduleUpdate();
    return true;
}

void DynamicSongPopup::onExit() {
    this->unschedule(schedule_selector(DynamicSongPopup::previewSurface));
    // A preview left half way down would leave the menu music muffled.
    if (m_previewing) {
        m_previewing = false;
        SubmergeEffect::get().rampTo(0.f, m_cfg.submerge.surfaceSeconds);
    }
    Popup::onExit();
}

void DynamicSongPopup::update(float dt) {
    m_uiClock += dt;
    kit::stepWheelScroll(m_scroll, m_scrollTargetY, m_scrollTargetSet, dt);

    if (m_uiClock - m_lastStatusRefresh >= 0.15f) {
        m_lastStatusRefresh = m_uiClock;
        refreshStatus();
    }
}

void DynamicSongPopup::scrollWheel(float x, float y) {
    kit::queueWheelScroll(m_scroll, x, y, m_scrollTargetY, m_scrollTargetSet);
}

void DynamicSongPopup::scheduleRebuild() {
    Ref<DynamicSongPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (self && self->getParent()) self->rebuild();
    });
}

void DynamicSongPopup::persist() {
    saveConfig(m_cfg);
    // saveConfig normalises and lets a named preset overwrite its tone knobs;
    // mirror the result back so the sliders never show a discarded value.
    m_cfg = config();
}


void DynamicSongPopup::previewDive() {
    if (!m_cfg.submerge.enabled) {
        PaimonNotify::create("Activa el efecto para probarlo", NotificationIcon::Warning)->show();
        return;
    }
    if (DynamicSongManager::get()->isHandingOff()) return;

    m_previewing = true;
    SubmergeEffect::get().bindTarget(nullptr);
    SubmergeEffect::get().rampTo(1.f, m_cfg.submerge.diveSeconds);

    this->unschedule(schedule_selector(DynamicSongPopup::previewSurface));
    this->scheduleOnce(schedule_selector(DynamicSongPopup::previewSurface),
                       m_cfg.submerge.diveSeconds + 0.45f);
}

void DynamicSongPopup::previewSurface(float) {
    if (!m_previewing) return;
    m_previewing = false;
    SubmergeEffect::get().rampTo(0.f, m_cfg.submerge.surfaceSeconds);
}


CCNode* DynamicSongPopup::makeStatusRow(float width) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, 34.f});

    auto* main = CCLabelBMFont::create("--", "bigFont.fnt");
    main->setAnchorPoint({0.f, 1.f});
    main->setScale(0.4f);
    main->setPosition({12.f, 30.f});
    main->setColor(kit::kValueColor);
    row->addChild(main);
    m_statusMain = main;

    auto* sub = CCLabelBMFont::create("", "chatFont.fnt");
    sub->setAnchorPoint({0.f, 1.f});
    sub->setScale(0.44f);
    sub->setPosition({12.f, 14.f});
    sub->setColor(kit::kDescColor);
    row->addChild(sub);
    m_statusSub = sub;

    refreshStatus();
    return row;
}

void DynamicSongPopup::refreshStatus() {
    if (!m_statusMain || !m_statusSub) return;

    auto* dsm = DynamicSongManager::get();
    float const wet = SubmergeEffect::get().wetness();

    char const* state = "Inactivo";
    switch (dsm->getState()) {
        case DynState::Idle:      state = dsm->isStreamingPreviewPending() ? "Cargando" : "Inactivo"; break;
        case DynState::FadingIn:  state = "Entrando"; break;
        case DynState::Playing:   state = "Sonando"; break;
        case DynState::FadingOut: state = "Saliendo"; break;
        case DynState::Suspended: state = "En pausa"; break;
        case DynState::Handoff:   state = "Bajo el agua"; break;
    }

    m_statusMain->setString(state);
    m_statusMain->setColor(dsm->isActive() ? kit::kValueColor : kit::kOffColor);

    std::string sub;
    if (dsm->getCurrentPlayingLevelID() != 0) {
        sub = fmt::format("Nivel {}", dsm->getCurrentPlayingLevelID());
    } else {
        sub = "Sin nivel";
    }
    if (dsm->isStreamingPreview()) sub += "  |  streaming";
    if (wet > 0.005f) sub += fmt::format("  |  filtro {:.0f}%", wet * 100.f);
    m_statusSub->setString(sub.c_str());
}


void DynamicSongPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }
    m_scrollTargetSet = false;
    m_statusMain = nullptr;
    m_statusSub  = nullptr;

    auto const content = m_mainLayer->getContentSize();
    float const scrollW = content.width - 24.f;
    float const scrollH = content.height - 36.f - 38.f;
    float const innerW  = kit::cardInnerWidth(scrollW);

    auto* hero = kit::makeHeroToggle(scrollW,
        "Cancion Dinamica",
        "Suena la cancion del nivel mientras miras su informacion.",
        Mod::get()->getSettingValue<bool>("dynamic-song"),
        [](bool v) { Mod::get()->setSettingValue<bool>("dynamic-song", v); });

    auto* tabs = kit::makeTabBar(scrollW, {"Basico", "Buceo", "Avanzado"}, m_tab,
        [this](int i) {
            m_tab = i;
            scheduleRebuild();
        });

    std::vector<CCNode*> items = {hero, tabs};

    if (m_tab == 0) {
        items.push_back(kit::makeCard(scrollW, "Reproduccion", {120, 210, 255}, {
            kit::makeSelectRow(innerW,
                "Punto de inicio", startModeDescription(m_cfg.startMode),
                kStartModeNames, static_cast<int>(m_cfg.startMode),
                [this](int idx) {
                    m_cfg.startMode = static_cast<StartMode>(std::clamp(idx, 0, 2));
                    persist();
                    scheduleRebuild();
                }),
            kit::makeSelectRow(innerW,
                "Si el nivel tiene varias", "Que cancion toca en cada visita.",
                kRotationNames, static_cast<int>(m_cfg.rotationMode),
                [this](int idx) {
                    m_cfg.rotationMode = static_cast<RotationMode>(std::clamp(idx, 0, 2));
                    persist();
                }),
            kit::makeSliderRow(innerW,
                "Volumen", "Respecto al volumen de musica del juego.",
                m_cfg.volumePct, 20.0, 120.0, formatPercent,
                [this](double v) { m_cfg.volumePct = static_cast<int>(v); persist(); }),
            kit::makeSliderRow(innerW,
                "Fundido", "Cuanto tarda en entrar y en salir.",
                m_cfg.fadeSeconds, 0.05, 3.0, formatSeconds,
                [this](double v) { m_cfg.fadeSeconds = static_cast<float>(v); persist(); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Donde suena", {130, 240, 170}, {
            kit::makeToggleRow(innerW,
                "Niveles oficiales", "Tambien en el selector de niveles principales.",
                m_cfg.inLevelSelect,
                [this](bool v) { m_cfg.inLevelSelect = v; persist(); }),
            kit::makeToggleRow(innerW,
                "Vista previa online",
                "Si la cancion no esta descargada, la escucha en streaming.",
                m_cfg.streamPreview,
                [this](bool v) { m_cfg.streamPreview = v; persist(); }),
        }));

        items.push_back(kit::makeCard(scrollW, "En vivo", {255, 210, 100},
                                      {makeStatusRow(innerW)}));
    } else if (m_tab == 1) {
        auto& sub = m_cfg.submerge;

        items.push_back(kit::makeCard(scrollW, "Al darle play", {170, 190, 255}, {
            kit::makeToggleRow(innerW,
                "No cortar la musica",
                "En vez de callarse, la cancion se hunde bajo un filtro hasta "
                "que el nivel arranca.",
                sub.enabled,
                [this](bool v) { m_cfg.submerge.enabled = v; persist(); scheduleRebuild(); }),
            kit::makeSelectRow(innerW,
                "Estilo", presetDescription(sub.preset),
                kPresetNames, static_cast<int>(sub.preset),
                [this](int idx) {
                    auto preset = static_cast<SubmergePreset>(std::clamp(idx, 0, 4));
                    if (preset != SubmergePreset::Custom) {
                        bool const wasEnabled = m_cfg.submerge.enabled;
                        auto const timing = m_cfg.submerge;
                        m_cfg.submerge = submergePresetConfig(preset);
                        m_cfg.submerge.enabled        = wasEnabled;
                        m_cfg.submerge.diveSeconds    = timing.diveSeconds;
                        m_cfg.submerge.surfaceSeconds = timing.surfaceSeconds;
                        m_cfg.submerge.onLevelExit    = timing.onLevelExit;
                        m_cfg.submerge.holdSeconds    = timing.holdSeconds;
                    } else {
                        m_cfg.submerge.preset = preset;
                    }
                    persist();
                    scheduleRebuild();
                }),
        }));

        if (sub.enabled) {
            items.push_back(kit::makeCard(scrollW, "Tiempos", {255, 165, 210}, {
                kit::makeSliderRow(innerW,
                    "Entrada al agua", "Lo que tarda en hundirse al pulsar play.",
                    sub.diveSeconds, 0.05, 4.0, formatSeconds,
                    [this](double v) { m_cfg.submerge.diveSeconds = static_cast<float>(v); persist(); }),
                kit::makeSliderRow(innerW,
                    "Salida del agua", "Lo que tarda en volver a sonar limpia.",
                    sub.surfaceSeconds, 0.05, 6.0, formatSeconds,
                    [this](double v) { m_cfg.submerge.surfaceSeconds = static_cast<float>(v); persist(); }),
                kit::makeSliderRow(innerW,
                    "Espera antes de volver",
                    "Si cierras el popup y no entras al nivel, cuanto espera "
                    "antes de sacar la cancion del agua.",
                    sub.holdSeconds, 0.1, 5.0, formatSeconds,
                    [this](double v) { m_cfg.submerge.holdSeconds = static_cast<float>(v); persist(); }),
                kit::makeToggleRow(innerW,
                    "Al salir del nivel",
                    "Al volver de jugar, la cancion sale del agua en vez de "
                    "aparecer de golpe.",
                    sub.onLevelExit,
                    [this](bool v) { m_cfg.submerge.onLevelExit = v; persist(); }),
                kit::makeButtonRow(innerW,
                    "Probar", "Hunde y saca lo que este sonando ahora mismo.",
                    "Probar", [this]() { previewDive(); }),
            }));

            if (sub.preset != SubmergePreset::Custom) {
                items.push_back(kit::makeHint(scrollW,
                    "El estilo fija el corte, el volumen, la reverb y el tono. "
                    "Cambia a Personalizado para editarlos en Avanzado."));
            }
        } else {
            items.push_back(kit::makeHint(scrollW,
                "Con el efecto apagado, al darle play la cancion se apaga con "
                "un fundido normal."));
        }
    } else {
        items.push_back(kit::makeCard(scrollW, "Zona aleatoria", {120, 210, 255}, {
            kit::makeSliderRow(innerW,
                "Desde", "Punto mas temprano donde puede caer.",
                m_cfg.randomMinPct, 0.0, 90.0, formatPercent,
                [this](double v) { m_cfg.randomMinPct = static_cast<int>(v); persist(); }),
            kit::makeSliderRow(innerW,
                "Hasta", "Punto mas tardio donde puede caer.",
                m_cfg.randomMaxPct, 5.0, 100.0, formatPercent,
                [this](double v) { m_cfg.randomMaxPct = static_cast<int>(v); persist(); }),
        }));

        if (m_cfg.submerge.preset == SubmergePreset::Custom) {
            auto const& sub = m_cfg.submerge;
            items.push_back(kit::makeCard(scrollW, "Filtro personalizado", {255, 140, 220}, {
                kit::makeSliderRow(innerW,
                    "Corte de agudos", "Mas a la izquierda = mas tapada.",
                    sub.cutoffHz, 120.0, 6000.0, formatHz,
                    [this](double v) { m_cfg.submerge.cutoffHz = static_cast<float>(v); persist(); }),
                kit::makeSliderRow(innerW,
                    "Corte de graves", "Subelo para quitarle cuerpo, tipo radio.",
                    sub.highpassHz, 20.0, 1600.0, formatHz,
                    [this](double v) { m_cfg.submerge.highpassHz = static_cast<float>(v); persist(); }),
                kit::makeSliderRow(innerW,
                    "Volumen bajo el agua", "Cuanto baja mientras esta hundida.",
                    sub.duckDb, -24.0, 0.0, formatDb,
                    [this](double v) { m_cfg.submerge.duckDb = static_cast<float>(v); persist(); }),
                kit::makeSliderRow(innerW,
                    "Reverb", "Cuanto eco tiene el fondo.",
                    sub.reverbMix, 0.0, 100.0, formatPercent,
                    [this](double v) { m_cfg.submerge.reverbMix = static_cast<float>(v); persist(); }),
                kit::makeSliderRow(innerW,
                    "Tono", "Por debajo de x1.00 la cancion se arrastra.",
                    sub.pitch, 0.5, 1.5, formatPitch,
                    [this](double v) { m_cfg.submerge.pitch = static_cast<float>(v); persist(); }),
                kit::makeButtonRow(innerW,
                    "Probar", "Escucha el filtro sobre la musica actual.",
                    "Probar", [this]() { previewDive(); }),
            }));
        } else {
            items.push_back(kit::makeHint(scrollW,
                "El filtro lo manda el estilo elegido en la pestana Buceo. "
                "Elige Personalizado para editarlo aqui."));
        }
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 38.f});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::dynsong
