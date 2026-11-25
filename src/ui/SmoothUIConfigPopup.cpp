#include "SmoothUIConfigPopup.hpp"
#include "PaiConfigKit.hpp"
#include "PaimonUI.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/transitions/ui/TransitionConfigPopup.hpp"
#include "../features/transitions/ui/DynamicTransitionConfigPopup.hpp"
#include "../platform/PaimSettings.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>
#include <algorithm>

using namespace cocos2d;
using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

template<typename T>
T gset(const char* key) { return paimon::platform::gset<T>(key); }

template<typename T>
void sset(const char* key, T val) { paimon::platform::sset<T>(key, val); }

template<typename T>
T gsaved(const char* key, T def) { return paimon::platform::gsaved<T>(key, def); }

template<typename T>
void ssaved(const char* key, T val) { paimon::platform::ssaved<T>(key, val); }

std::vector<std::string> const kPresetIds = {
    "balanced", "subtle", "silky", "bouncy", "cinematic", "off"
};
std::vector<std::string> const kPresetNames = {
    "Equilibrado", "Sutil", "Sedoso", "Con rebote", "Cinematico", "Apagado"
};

int presetIndexFromId(std::string const& id) {
    for (size_t i = 0; i < kPresetIds.size(); ++i) {
        if (kPresetIds[i] == id) return static_cast<int>(i);
    }
    return 0;
}

int popupStyleIndex(std::string const& id) {
    auto const& styles = paimon::ui::popupStyles();
    for (size_t i = 0; i < styles.size(); ++i) {
        if (styles[i] == id) return static_cast<int>(i);
    }
    return 0;
}

} // namespace

namespace paimon::ui {

void setGlobalTransitionDuration(float duration) {
    auto& tm = TransitionManager::get();
    tm.loadConfig();
    auto cfg = tm.getGlobalConfig();
    cfg.duration = std::clamp(duration, 0.05f, 3.0f);
    tm.setGlobalConfig(cfg);
    tm.saveConfig();
}

void applySmoothUIPreset(std::string const& preset) {
    ssaved<std::string>("smooth-ui-preset", preset);

    auto setBase = [](bool enabled, float globalSpeed, float strength) {
        sset<bool>("smooth-ui-enabled", enabled);
        ssaved<double>("smooth-ui-global-speed", globalSpeed);
        ssaved<double>("smooth-ui-motion-strength", strength);
        ssaved<bool>("smooth-ui-animate-buttons", enabled);
        ssaved<std::string>("smooth-ui-button-scope", enabled ? "all" : "off");
        ssaved<bool>("smooth-ui-reduced-motion", !enabled);
    };

    if (preset == "off") {
        setBase(false, 1.0f, 0.0f);
        sset<bool>("dynamic-popup-enabled", false);
        sset<bool>("dynamic-exit-enabled", false);
        sset<bool>("dynamic-transition-enabled", false);
#if defined(GEODE_IS_DESKTOP)
        sset<bool>("smooth-scroll", false);
#endif
        ssaved<bool>("popup-blur-show-placeholder", true);
        TransitionManager::get().setEnabled(false);
        TransitionManager::get().saveConfig();
        return;
    }

    sset<bool>("dynamic-popup-enabled", true);
    sset<bool>("dynamic-exit-enabled", true);
    sset<bool>("dynamic-transition-enabled", true);
#if defined(GEODE_IS_DESKTOP)
    sset<bool>("smooth-scroll", true);
#endif
    ssaved<bool>("popup-blur-show-placeholder", true);

    // every non-off preset writes the same nine keys; only the values differ.
    struct SmoothPreset {
        char const* id; // nullptr: fallback for unknown preset names
        float globalSpeed;
        float strength;
        char const* popupStyle;
        double popupSpeed;
        double exitSpeed;
        double blurFade;
        double scrollSmoothness;
        double scrollSensitivity;
        double pressScale;
        bool releaseBounce;
        float transitionDuration;
        bool enableTransitionManager; // only cinematic pokes the manager directly
    };
    constexpr SmoothPreset kPresets[] = {
        {"subtle",    1.25f, 0.65f, "zoom-fade",    1.35, 1.45, 0.12, 0.75, 1.55, 0.97, false, 0.25f, false},
        {"silky",     0.88f, 1.15f, "paimonUI",     0.88, 0.95, 0.24, 1.85, 2.0,  0.93, true,  0.48f, false},
        {"bouncy",    0.95f, 1.25f, "jelly",        0.90, 1.05, 0.18, 1.35, 2.15, 0.90, true,  0.40f, false},
        {"cinematic", 0.72f, 1.35f, "elastic-drop", 0.78, 0.85, 0.32, 2.2,  1.8,  0.92, true,  0.65f, true},
    };
    constexpr SmoothPreset kDefaultPreset{
        nullptr, 1.0f, 1.0f, "paimonUI", 1.0, 1.0, 0.18, 1.0, 2.0, 0.94, true, 0.35f, false};

    SmoothPreset const* chosen = &kDefaultPreset;
    for (auto const& candidate : kPresets) {
        if (preset == candidate.id) {
            chosen = &candidate;
            break;
        }
    }

    setBase(true, chosen->globalSpeed, chosen->strength);
    ssaved<std::string>("dynamic-popup-style", chosen->popupStyle);
    ssaved<double>("dynamic-popup-speed", chosen->popupSpeed);
    ssaved<double>("dynamic-exit-speed", chosen->exitSpeed);
    ssaved<double>("popup-blur-fade-duration", chosen->blurFade);
    ssaved<double>("smooth-scroll-smoothness", chosen->scrollSmoothness);
    ssaved<double>("smooth-scroll-sensitivity", chosen->scrollSensitivity);
    ssaved<double>("smooth-ui-button-press-scale", chosen->pressScale);
    ssaved<bool>("smooth-ui-button-release-bounce", chosen->releaseBounce);
    if (chosen->enableTransitionManager) TransitionManager::get().setEnabled(true);
    setGlobalTransitionDuration(chosen->transitionDuration);
}

SmoothUIConfigPopup* SmoothUIConfigPopup::create() {
    auto* ret = new SmoothUIConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SmoothUIConfigPopup::init() {
    if (!PaimonPopup::init(420.f, 300.f)) return false;
    this->setTitle("Smooth UI");
    this->addInfoButton("Smooth UI",
        "Controla el movimiento de toda la interfaz del mod.\n\n"
        "Elige un <cg>preset</c> para ajustar todo de golpe, o entra en "
        "<cy>Avanzado</c> para afinar popups, botones, scroll y blur por separado.\n\n"
        "<co>Reducir movimiento</c> minimiza animaciones si te marean.");

    rebuild();
    return true;
}

void SmoothUIConfigPopup::scheduleRebuild() {
// the triggering control still lives in the current scroll, so rebuild next frame.
    this->retain();
    Loader::get()->queueInMainThread([this] {
        if (this->getParent()) this->rebuild();
        this->release();
    });
}

void SmoothUIConfigPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    TransitionManager::get().loadConfig();

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 44.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto fmtTimes = [](double v) { return fmt::format("x{:.2f}", v); };
    auto fmtPlain = [](double v) { return fmt::format("{:.2f}", v); };
    auto fmtSecs  = [](double v) { return fmt::format("{:.2f}s", v); };

    auto* hero = kit::makeHeroToggle(scrollW,
        "Animaciones suaves",
        "Popups, botones y scroll con movimiento fluido.",
        gset<bool>("smooth-ui-enabled"),
        [](bool v) { sset<bool>("smooth-ui-enabled", v); });

    auto* presetCard = kit::makeCard(scrollW, "Preset rapido", {255, 200, 100}, {
        kit::makeSelectRow(innerW,
            "Estilo general",
            "Ajusta todo de golpe. Luego puedes afinar en Avanzado.",
            kPresetNames,
            presetIndexFromId(gsaved<std::string>("smooth-ui-preset", "balanced")),
            [this](int idx) {
                if (idx >= 0 && idx < static_cast<int>(kPresetIds.size())) {
                    applySmoothUIPreset(kPresetIds[static_cast<size_t>(idx)]);
                    scheduleRebuild();
                }
            }),
    });

    auto* tabs = kit::makeTabBar(scrollW, {"Basico", "Avanzado"}, m_tab,
        [this](int i) {
            m_tab = i;
            scheduleRebuild();
        });

    std::vector<CCNode*> items = {hero, presetCard, tabs};
    items.push_back(kit::makeCard(scrollW, "Dynamic Transition", {115, 210, 255}, {
        kit::makeToggleRow(innerW,
            "Abrir layers como apps", "El layer se expande desde el boton pulsado.",
            gset<bool>("dynamic-transition-enabled"),
            [](bool value) { sset<bool>("dynamic-transition-enabled", value); }),
        kit::makeButtonRow(innerW,
            "Configuracion y vista previa", "Estilo, curvas, duracion, fondo y regreso.",
            "Configurar", [] {
                if (auto* popup = paimon::transitions::dynamic::DynamicTransitionConfigPopup::create()) popup->show();
            }),
    }));

    if (m_tab == 0) {
        items.push_back(kit::makeCard(scrollW, "Lo esencial", {120, 210, 255}, {
            kit::makeSliderRow(innerW,
                "Velocidad general",
                "Mas alto = animaciones mas rapidas.",
                gsaved<double>("smooth-ui-global-speed", 1.0), 0.35, 2.5, fmtTimes,
                [](double v) { ssaved<double>("smooth-ui-global-speed", v); }),
            kit::makeSliderRow(innerW,
                "Fuerza del movimiento",
                "Cuanto se nota cada animacion.",
                gsaved<double>("smooth-ui-motion-strength", 1.0), 0.0, 2.0, fmtPlain,
                [](double v) { ssaved<double>("smooth-ui-motion-strength", v); }),
            kit::makeToggleRow(innerW,
                "Reducir movimiento",
                "Minimiza las animaciones si te distraen o marean.",
                gsaved<bool>("smooth-ui-reduced-motion", false),
                [](bool v) { ssaved<bool>("smooth-ui-reduced-motion", v); }),
        }));
        items.push_back(kit::makeHint(scrollW,
            "Con el preset y estos tres ajustes basta para la mayoria. "
            "En Avanzado puedes afinar popups, botones, scroll y blur."));
    } else {
        items.push_back(kit::makeCard(scrollW, "Popups", {120, 210, 255}, {
            kit::makeToggleRow(innerW,
                "Animar al abrir",
                "Los popups entran con animacion.",
                gset<bool>("dynamic-popup-enabled"),
                [](bool v) { sset<bool>("dynamic-popup-enabled", v); }),
            kit::makeSelectRow(innerW,
                "Estilo de entrada", "Como aparece cada popup.",
                paimon::ui::popupStyles(),
                popupStyleIndex(gsaved<std::string>("dynamic-popup-style", "paimonUI")),
                [](int idx) {
                    auto const& styles = paimon::ui::popupStyles();
                    if (idx >= 0 && idx < static_cast<int>(styles.size())) {
                        ssaved<std::string>("dynamic-popup-style", styles[static_cast<size_t>(idx)]);
                    }
                }),
            kit::makeSliderRow(innerW,
                "Velocidad de entrada", "Mas alto = abre mas rapido.",
                gsaved<double>("dynamic-popup-speed", 1.0), 0.3, 3.0, fmtTimes,
                [](double v) { ssaved<double>("dynamic-popup-speed", v); }),
            kit::makeToggleRow(innerW,
                "Animar al cerrar",
                "Los popups salen con animacion.",
                gset<bool>("dynamic-exit-enabled"),
                [](bool v) { sset<bool>("dynamic-exit-enabled", v); }),
            kit::makeSliderRow(innerW,
                "Velocidad de salida", "Mas alto = cierra mas rapido.",
                gsaved<double>("dynamic-exit-speed", 1.0), 0.3, 3.0, fmtTimes,
                [](double v) { ssaved<double>("dynamic-exit-speed", v); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Botones", {130, 240, 170}, {
            kit::makeToggleRow(innerW,
                "Animar botones",
                "Los botones se encogen al presionarlos.",
                gsaved<bool>("smooth-ui-animate-buttons", true),
                [](bool v) { ssaved<bool>("smooth-ui-animate-buttons", v); }),
            kit::makeSelectRow(innerW,
                "Donde aplica", "Todos, solo los del mod, o ninguno.",
                {"Todos", "Solo del mod", "Ninguno"},
                [] {
                    auto cur = gsaved<std::string>("smooth-ui-button-scope", "all");
                    if (cur == "paimon") return 1;
                    if (cur == "off") return 2;
                    return 0;
                }(),
                [](int idx) {
                    char const* scopes[] = {"all", "paimon", "off"};
                    if (idx >= 0 && idx < 3) {
                        ssaved<std::string>("smooth-ui-button-scope", scopes[idx]);
                    }
                }),
            kit::makeSliderRow(innerW,
                "Encogimiento al presionar", "Mas bajo = se hunde mas.",
                gsaved<double>("smooth-ui-button-press-scale", 0.94), 0.80, 1.05, fmtTimes,
                [](double v) { ssaved<double>("smooth-ui-button-press-scale", v); }),
            kit::makeToggleRow(innerW,
                "Rebotar al soltar",
                "Pequeno rebote al soltar el boton.",
                gsaved<bool>("smooth-ui-button-release-bounce", true),
                [](bool v) { ssaved<bool>("smooth-ui-button-release-bounce", v); }),
        }));

#if defined(GEODE_IS_DESKTOP)
        items.push_back(kit::makeCard(scrollW, "Scroll y zoom del editor", {255, 170, 120}, {
            kit::makeToggleRow(innerW,
                "Scroll suave",
                "Los menus se desplazan con inercia.",
                gset<bool>("smooth-scroll"),
                [](bool v) { sset<bool>("smooth-scroll", v); }),
            kit::makeSliderRow(innerW,
                "Velocidad del scroll", "Cuanto avanza cada giro de la rueda.",
                gsaved<double>("smooth-scroll-sensitivity", 2.0), 0.25, 5.0, fmtTimes,
                [](double v) { ssaved<double>("smooth-scroll-sensitivity", v); }),
            kit::makeSliderRow(innerW,
                "Suavidad del scroll", "Mas alto = frena mas lento.",
                gsaved<double>("smooth-scroll-smoothness", 1.0), 0.25, 3.0, fmtPlain,
                [](double v) { ssaved<double>("smooth-scroll-smoothness", v); }),
            kit::makeToggleRow(innerW,
                "Zoom suave en el editor",
                "Filtra cada giro para evitar saltos de zoom.",
                gsaved<bool>("smooth-scroll-editor-zoom", true),
                [](bool v) { ssaved<bool>("smooth-scroll-editor-zoom", v); }),
            kit::makeSliderRow(innerW,
                "Velocidad del zoom", "Cuanto acerca o aleja cada giro.",
                gsaved<double>("smooth-scroll-editor-zoom-sensitivity", 1.0), 0.25, 3.0, fmtTimes,
                [](double v) { ssaved<double>("smooth-scroll-editor-zoom-sensitivity", v); }),
            kit::makeSliderRow(innerW,
                "Suavidad del zoom", "Que tan gradual se siente.",
                gsaved<double>("smooth-scroll-editor-zoom-smoothness", 1.15), 0.25, 3.0, fmtPlain,
                [](double v) { ssaved<double>("smooth-scroll-editor-zoom-smoothness", v); }),
        }));
#endif

        items.push_back(kit::makeCard(scrollW, "Blur y transiciones", {200, 180, 255}, {
            kit::makeToggleRow(innerW,
                "Desenfocar el fondo",
                "Difumina lo que hay detras de cada popup.",
                gset<bool>("popup-blur-enabled"),
                [](bool v) { sset<bool>("popup-blur-enabled", v); }),
            kit::makeSliderRow(innerW,
                "Aparicion del blur", "Segundos que tarda en difuminarse.",
                gsaved<double>("popup-blur-fade-duration", 0.18), 0.0, 0.8, fmtSecs,
                [](double v) { ssaved<double>("popup-blur-fade-duration", v); }),
            kit::makeToggleRow(innerW,
                "Transiciones entre pantallas",
                "Animaciones al cambiar de escena.",
                TransitionManager::get().isEnabled(),
                [](bool v) {
                    TransitionManager::get().setEnabled(v);
                    TransitionManager::get().saveConfig();
                }),
            kit::makeSliderRow(innerW,
                "Duracion de la transicion", "Segundos entre pantalla y pantalla.",
                static_cast<double>(TransitionManager::get().getGlobalConfig().duration),
                0.05, 1.5, fmtSecs,
                [](double v) { setGlobalTransitionDuration(static_cast<float>(v)); }),
            kit::makeButtonRow(innerW,
                "Editor de transiciones",
                "Configura cada transicion por separado.",
                "Abrir",
                [] { if (auto* p = TransitionConfigPopup::create()) p->show(); }),
        }));
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 8.f});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::ui
