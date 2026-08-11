#include "MenuMusicSettingsPopup.hpp"
#include "components/SpatialStagePreview.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 300.f;

std::vector<std::string> const kPresetNames = {
    "Custom", "Original", "Slow + Reverb", "Dreamy", "Bass Boost",
    "Nightcore", "Underwater", "Concert Hall", "Lo-Fi",
};

std::vector<std::string> const kNotificationPrefixes = {
    "Now Playing", "Current Song", "Looping", "Song",
    "Music", "Playing", "[Empty]",
};

std::vector<std::string> const kSpatialPresetNames = {
    "Custom", "Apagado", "Estudio", "Cine", "Arena", "Orbita", "Dreamwave",
};

std::vector<std::string> const kSpatialMotionNames = {
    "Fijo", "Orbita", "Vaiven",
};

char const* presetDescription(paimon::menumusic::MusicEffectsPreset preset) {
    using P = paimon::menumusic::MusicEffectsPreset;
    switch (preset) {
        case P::Custom:      return "Tus valores actuales. Cualquier slider cambia el preset a Custom.";
        case P::Original:    return "Sonido original, sin efectos ni cambios de velocidad.";
        case P::SlowReverb:  return "Mas lento, grave y espacioso, con reverb larga.";
        case P::Dreamy:      return "Ambiente suave con reverb amplia y un eco ligero.";
        case P::BassBoost:   return "Graves fuertes con margen extra para evitar saturacion.";
        case P::Nightcore:   return "Mas rapido, agudo y brillante.";
        case P::Underwater:  return "Filtro cerrado, graves y una cola de reverb corta.";
        case P::ConcertHall: return "Sala grande con reverb densa y reflexiones suaves.";
        case P::Lofi:        return "Mas lento, calido y con menos agudos.";
        case P::Count:       break;
    }
    return "";
}

char const* spatialPresetDescription(paimon::menumusic::SpatialPreset preset) {
    using P = paimon::menumusic::SpatialPreset;
    switch (preset) {
        case P::Custom:    return "Tu escenario actual, listo para ajustar al detalle.";
        case P::Off:       return "Audio directo, sin escenario virtual.";
        case P::Studio:    return "Frontal y preciso, con una sala corta y controlada.";
        case P::Cinema:    return "Escena ancha con profundidad suave para auriculares y parlantes.";
        case P::Arena:     return "Campo envolvente amplio y una sala grande.";
        case P::Orbit:     return "La escena gira lentamente alrededor del oyente.";
        case P::Dreamwave: return "Campo 360 con vaiven lento y ambiente profundo.";
        case P::Count:     break;
    }
    return "";
}

std::string formatSpeed(double value) {
    return fmt::format("{:.2f}x", value);
}

std::string formatDb(double value) {
    return fmt::format("{:+.1f} dB", value);
}

std::string formatPercent(double value) {
    return fmt::format("{:.0f}%", value);
}

std::string formatSeconds(double value) {
    return fmt::format("{:.1f}s", value);
}

std::string formatMilliseconds(double value) {
    return fmt::format("{:.0f} ms", value);
}

std::string formatHz(double value) {
    return value >= 1000.0
        ? fmt::format("{:.1f} kHz", value / 1000.0)
        : fmt::format("{:.0f} Hz", value);
}

std::string formatPan(double value) {
    if (std::abs(value) < 0.01) return "Centro";
    return fmt::format("{} {:.0f}%", value < 0.0 ? "Izq" : "Der", std::abs(value) * 100.0);
}

std::string formatDegrees(double value) {
    return fmt::format("{:.0f} deg", value);
}

std::string formatDirection(double value) {
    if (std::abs(value) < 1.0) return "Frente";
    if (std::abs(value) > 179.0) return "Atras";
    return fmt::format("{} {:.0f} deg", value < 0.0 ? "Izq" : "Der", std::abs(value));
}

std::string formatMotionSpeed(double value) {
    return fmt::format("{:.0f} deg/s", value);
}

template <class T>
T setting(char const* key) {
    return Mod::get()->getSettingValue<T>(key);
}

template <class T>
void setSetting(char const* key, T value) {
    Mod::get()->setSettingValue<T>(key, value);
}

template <class T>
T saved(char const* key, T fallback) {
    return Mod::get()->getSavedValue<T>(key, fallback);
}

template <class T>
void setSaved(char const* key, T value) {
    Mod::get()->setSavedValue(key, value);
}

int optionIndex(std::vector<std::string> const& options, std::string const& value) {
    auto it = std::find(options.begin(), options.end(), value);
    return it == options.end() ? 0 : static_cast<int>(std::distance(options.begin(), it));
}

} // namespace

namespace paimon::menumusic {

MenuMusicSettingsPopup* MenuMusicSettingsPopup::create() {
    auto* ret = new MenuMusicSettingsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

MenuMusicSettingsPopup::~MenuMusicSettingsPopup() {
    MenuMusicEffects::get().setAuditionBypassed(false);
}

bool MenuMusicSettingsPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    setTitle("Configuracion de Musica");

    auto& effects = MenuMusicEffects::get();
    effects.setAuditionBypassed(false);
    effects.loadConfig();
    m_cfg = effects.config();
    rebuild();

    auto* resetSpr = ButtonSprite::create("Restaurar FX", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    if (resetSpr) resetSpr->setScale(0.55f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
        [this](CCMenuItemSpriteExtra*) {
            MenuMusicEffects::get().applyPreset(MusicEffectsPreset::Original);
            m_cfg = MenuMusicEffects::get().config();
            scheduleRebuild();
            PaimonNotify::create("Efectos restaurados", NotificationIcon::Success)->show();
        });
    resetBtn->setID("menu-music-effects-reset-btn"_spr);
    resetBtn->setPosition({m_mainLayer->getContentSize().width / 2.f, 20.f});
    m_buttonMenu->addChild(resetBtn);
    return true;
}

void MenuMusicSettingsPopup::scheduleRebuild() {
    Ref<MenuMusicSettingsPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (self && self->getParent()) self->rebuild();
    });
}

void MenuMusicSettingsPopup::persist() {
    m_cfg.preset = MusicEffectsPreset::Custom;
    MenuMusicEffects::get().saveConfig(m_cfg);
    m_cfg = MenuMusicEffects::get().config();
}

void MenuMusicSettingsPopup::persistSpatial() {
    m_cfg.preset = MusicEffectsPreset::Custom;
    m_cfg.spatialPreset = SpatialPreset::Custom;
    if (m_cfg.spatialEnabled) m_cfg.enabled = true;
    MenuMusicEffects::get().saveConfig(m_cfg);
    m_cfg = MenuMusicEffects::get().config();
}

void MenuMusicSettingsPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    auto const content = m_mainLayer->getContentSize();
    float const scrollW = content.width - 24.f;
    float const scrollH = content.height - 36.f - 38.f;
    float const innerW = kit::cardInnerWidth(scrollW);

    auto* hero = kit::makeHeroToggle(scrollW,
        "Efectos de Musica",
        "Se aplican en vivo a las canciones reproducidas por Menu Music.",
        m_cfg.enabled,
        [this](bool enabled) {
            m_cfg.enabled = enabled;
            persist();
        });

    auto* tabs = kit::makeTabBar(scrollW,
        {"Presets", "Sonido", "Espacial", "Ambiente", "Player"}, m_tab,
        [this](int tab) {
            m_tab = tab;
            scheduleRebuild();
        });

    std::vector<CCNode*> items = {hero, tabs};

    if (m_tab == 0) {
        items.push_back(kit::makeCard(scrollW, "Presets", {255, 190, 100}, {
            kit::makeSelectRow(innerW,
                "Estilo", "Se aplica al instante y puedes editarlo despues.",
                kPresetNames, static_cast<int>(m_cfg.preset),
                [this](int index) {
                    auto preset = static_cast<MusicEffectsPreset>(
                        std::clamp(index, 0, static_cast<int>(MusicEffectsPreset::Count) - 1));
                    MenuMusicEffects::get().applyPreset(preset);
                    m_cfg = MenuMusicEffects::get().config();
                    scheduleRebuild();
                }),
        }));
        items.push_back(kit::makeHint(scrollW, presetDescription(m_cfg.preset)));
        items.push_back(kit::makeHint(scrollW,
            "Prueba Slow + Reverb para el efecto lento y espacial. Los controles "
            "de Sonido y Ambiente convierten el preset en Custom."));
    } else if (m_tab == 1) {
        items.push_back(kit::makeCard(scrollW, "Tiempo y Mezcla", {120, 210, 255}, {
            kit::makeSliderRow(innerW,
                "Velocidad / tono", "0.5x es una octava mas grave; 1.0x no cambia nada.",
                m_cfg.speed, 0.5, 1.5, formatSpeed,
                [this](double value) { m_cfg.speed = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Ganancia", "Volumen propio del efecto, sin tocar el slider del juego.",
                m_cfg.gainDb, -12.0, 6.0, formatDb,
                [this](double value) { m_cfg.gainDb = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Balance", "Mueve la musica entre izquierda y derecha.",
                m_cfg.pan, -1.0, 1.0, formatPan,
                [this](double value) { m_cfg.pan = static_cast<float>(value); persist(); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Ecualizador", {255, 150, 210}, {
            kit::makeSliderRow(innerW,
                "Graves", nullptr, m_cfg.bassDb, -12.0, 10.0, formatDb,
                [this](double value) { m_cfg.bassDb = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Medios", nullptr, m_cfg.midDb, -12.0, 10.0, formatDb,
                [this](double value) { m_cfg.midDb = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Agudos", nullptr, m_cfg.trebleDb, -12.0, 10.0, formatDb,
                [this](double value) { m_cfg.trebleDb = static_cast<float>(value); persist(); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Filtros", {150, 235, 170}, {
            kit::makeSliderRow(innerW,
                "Corte de agudos", "Bajalo para un sonido apagado o underwater.",
                m_cfg.lowpassHz, 500.0, 22000.0, formatHz,
                [this](double value) { m_cfg.lowpassHz = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Corte de graves", "Subelo para quitar retumbe y frecuencias bajas.",
                m_cfg.highpassHz, 20.0, 2500.0, formatHz,
                [this](double value) { m_cfg.highpassHz = static_cast<float>(value); persist(); }),
        }));
    } else if (m_tab == 2) {
        items.push_back(kit::makeCard(scrollW, "Escena Espacial", {100, 220, 255}, {
            kit::makeSelectRow(innerW,
                "Escena", "Cambia amplitud, ambiente y movimiento de una vez.",
                kSpatialPresetNames, static_cast<int>(m_cfg.spatialPreset),
                [this](int index) {
                    auto preset = static_cast<SpatialPreset>(
                        std::clamp(index, 0, static_cast<int>(SpatialPreset::Count) - 1));
                    MenuMusicEffects::get().setAuditionBypassed(false);
                    MenuMusicEffects::get().applySpatialPreset(preset);
                    m_cfg = MenuMusicEffects::get().config();
                    scheduleRebuild();
                }),
            kit::makeToggleRow(innerW,
                "Audio espacial", "Activa el escenario virtual solo para Menu Music.",
                m_cfg.spatialEnabled,
                [this](bool enabled) {
                    m_cfg.spatialEnabled = enabled;
                    m_cfg.spatialPreset = enabled
                        ? SpatialPreset::Custom : SpatialPreset::Off;
                    persistSpatial();
                }),
        }));

        if (auto* preview = SpatialStagePreview::create(innerW)) {
            items.push_back(kit::makeCard(scrollW, "Radar en Vivo", {255, 125, 205}, {
                preview,
            }));
        }
        items.push_back(kit::makeHint(scrollW, spatialPresetDescription(m_cfg.spatialPreset)));

        items.push_back(kit::makeCard(scrollW, "Geometria", {135, 170, 255}, {
            kit::makeSliderRow(innerW,
                "Amplitud", "0 deg concentra la musica; 360 deg llena todo el campo.",
                m_cfg.spatialWidth, 0.0, 360.0, formatDegrees,
                [this](double value) {
                    m_cfg.spatialWidth = static_cast<float>(value);
                    persistSpatial();
                }),
            kit::makeSliderRow(innerW,
                "Direccion", "Coloca el centro de la escena alrededor del oyente.",
                m_cfg.spatialAngle, -180.0, 180.0, formatDirection,
                [this](double value) {
                    m_cfg.spatialAngle = static_cast<float>(value);
                    persistSpatial();
                }),
            kit::makeSliderRow(innerW,
                "Profundidad", "Reflexiones propias del escenario, separadas de Reverb.",
                m_cfg.spatialRoom, 0.0, 100.0, formatPercent,
                [this](double value) {
                    m_cfg.spatialRoom = static_cast<float>(value);
                    persistSpatial();
                }),
        }));

        items.push_back(kit::makeCard(scrollW, "Movimiento", {180, 145, 255}, {
            kit::makeSelectRow(innerW,
                "Trayectoria", "Fija, vuelta completa o vaiven frontal.",
                kSpatialMotionNames, static_cast<int>(m_cfg.spatialMotion),
                [this](int index) {
                    m_cfg.spatialMotion = static_cast<SpatialMotion>(
                        std::clamp(index, 0, static_cast<int>(SpatialMotion::Count) - 1));
                    persistSpatial();
                }),
            kit::makeSliderRow(innerW,
                "Velocidad", "Movimiento lento para no cansar al escuchar.",
                m_cfg.spatialMotionSpeed, 1.0, 60.0, formatMotionSpeed,
                [this](double value) {
                    m_cfg.spatialMotionSpeed = static_cast<float>(value);
                    persistSpatial();
                }),
            kit::makeToggleRow(innerW,
                "A/B sonido original", "Bypass temporal; tus ajustes no se pierden.",
                MenuMusicEffects::get().isAuditionBypassed(),
                [](bool bypassed) {
                    MenuMusicEffects::get().setAuditionBypassed(bypassed);
                }),
        }));

        items.push_back(kit::makeHint(scrollW,
            "Usa el panner espacial de FMOD: aprovecha surround cuando existe y "
            "mantiene una mezcla compatible en salidas estereo."));
    } else if (m_tab == 3) {
        items.push_back(kit::makeCard(scrollW, "Reverb", {180, 150, 255}, {
            kit::makeSliderRow(innerW,
                "Mezcla", "Cantidad de sonido reverberado.",
                m_cfg.reverbMix, 0.0, 100.0, formatPercent,
                [this](double value) { m_cfg.reverbMix = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Duracion", "Tiempo que tarda en apagarse la cola de reverb.",
                m_cfg.reverbDecay, 0.1, 10.0, formatSeconds,
                [this](double value) { m_cfg.reverbDecay = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Tamano de sala", "Controla densidad y difusion del espacio.",
                m_cfg.reverbRoom, 10.0, 100.0, formatPercent,
                [this](double value) { m_cfg.reverbRoom = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Brillo de reverb", "Corte de agudos solo para la cola de reverb.",
                m_cfg.reverbHighCut, 1000.0, 20000.0, formatHz,
                [this](double value) { m_cfg.reverbHighCut = static_cast<float>(value); persist(); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Eco", {100, 220, 255}, {
            kit::makeSliderRow(innerW,
                "Mezcla", "Cantidad de repeticion que vuelve a la mezcla.",
                m_cfg.echoMix, 0.0, 100.0, formatPercent,
                [this](double value) { m_cfg.echoMix = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Retraso", "Tiempo entre cada repeticion.",
                m_cfg.echoDelay, 10.0, 1500.0, formatMilliseconds,
                [this](double value) { m_cfg.echoDelay = static_cast<float>(value); persist(); }),
            kit::makeSliderRow(innerW,
                "Feedback", "Cuanto del eco vuelve a repetirse.",
                m_cfg.echoFeedback, 0.0, 90.0, formatPercent,
                [this](double value) { m_cfg.echoFeedback = static_cast<float>(value); persist(); }),
        }));
    } else {
        items.push_back(kit::makeCard(scrollW, "Reproduccion", {120, 210, 255}, {
            kit::makeToggleRow(innerW,
                "Menu Music Player", "Muestra el boton y activa la biblioteca de musica.",
                setting<bool>("menuMusicEnable"),
                [](bool value) { setSetting("menuMusicEnable", value); }),
            kit::makeToggleRow(innerW,
                "Autoplay al iniciar", nullptr,
                saved<bool>("menuMusicAutoplayOnBoot", false),
                [](bool value) { setSaved("menuMusicAutoplayOnBoot", value); }),
            kit::makeToggleRow(innerW,
                "Seguir cancion actual", "Hace scroll hasta la cancion que esta sonando.",
                saved<bool>("menuLoopAutoScrollCurrent", true),
                [](bool value) { setSaved("menuLoopAutoScrollCurrent", value); }),
            kit::makeToggleRow(innerW,
                "Barra de progreso", nullptr,
                setting<bool>("menuLoopShowPlaybackProgress"),
                [](bool value) { setSetting("menuLoopShowPlaybackProgress", value); }),
            kit::makeToggleRow(innerW,
                "Atajos de teclado", nullptr,
                setting<bool>("menuLoopEnableKeyboardShortcuts"),
                [](bool value) { setSetting("menuLoopEnableKeyboardShortcuts", value); }),
            kit::makeSliderRow(innerW,
                "Salto adelante / atras", nullptr,
                static_cast<double>(setting<std::int64_t>("menuLoopSeekAmountMs")),
                100.0, 30000.0, formatMilliseconds,
                [](double value) {
                    setSetting<std::int64_t>("menuLoopSeekAmountMs",
                        static_cast<std::int64_t>(std::round(value)));
                }),
        }));

        items.push_back(kit::makeCard(scrollW, "Cambio de Cancion", {255, 160, 210}, {
            kit::makeToggleRow(innerW,
                "Shuffle continuo", nullptr,
                setting<bool>("menuLoopConstantShuffle"),
                [](bool value) { setSetting("menuLoopConstantShuffle", value); }),
            kit::makeToggleRow(innerW,
                "Recordar ultima cancion", nullptr,
                setting<bool>("menuLoopSaveSongOnGameClose"),
                [](bool value) { setSetting("menuLoopSaveSongOnGameClose", value); }),
            kit::makeToggleRow(innerW,
                "Nueva cancion al salir de nivel", nullptr,
                setting<bool>("menuLoopRandomizeOnLevelExit"),
                [](bool value) { setSetting("menuLoopRandomizeOnLevelExit", value); }),
            kit::makeToggleRow(innerW,
                "Restaurar al salir de nivel", nullptr,
                setting<bool>("menuLoopRestoreOnLevelExit"),
                [](bool value) { setSetting("menuLoopRestoreOnLevelExit", value); }),
            kit::makeToggleRow(innerW,
                "Nueva cancion al salir del editor", nullptr,
                setting<bool>("menuLoopRandomizeOnEditorExit"),
                [](bool value) { setSetting("menuLoopRandomizeOnEditorExit", value); }),
            kit::makeToggleRow(innerW,
                "Restaurar al salir del editor", nullptr,
                setting<bool>("menuLoopRestoreOnEditorExit"),
                [](bool value) { setSetting("menuLoopRestoreOnEditorExit", value); }),
        }));

        auto prefix = setting<std::string>("menuLoopCustomPrefix");
        items.push_back(kit::makeCard(scrollW, "Notificaciones", {255, 210, 100}, {
            kit::makeToggleRow(innerW,
                "Now Playing", nullptr,
                setting<bool>("menuLoopEnableNotification"),
                [](bool value) { setSetting("menuLoopEnableNotification", value); }),
            kit::makeSliderRow(innerW,
                "Duracion", nullptr,
                setting<double>("menuLoopNotificationTime"),
                0.5, 5.0, formatSeconds,
                [](double value) { setSetting("menuLoopNotificationTime", value); }),
            kit::makeSelectRow(innerW,
                "Prefijo", nullptr,
                kNotificationPrefixes, optionIndex(kNotificationPrefixes, prefix),
                [](int index) {
                    setSetting("menuLoopCustomPrefix",
                        kNotificationPrefixes[std::clamp(index, 0,
                            static_cast<int>(kNotificationPrefixes.size()) - 1)]);
                }),
        }));
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 38.f});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::menumusic
