#include "SmoothUIConfigPopup.hpp"
#include "PaiConfigKit.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "../features/transitions/services/TransitionManager.hpp"
#include "../features/transitions/ui/TransitionConfigPopup.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>
#include <algorithm>

using namespace cocos2d;
using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

template<typename T>
T gset(const char* key) {
    if (Mod::get()->hasSetting(key)) return Mod::get()->getSettingValue<T>(key);
    return Mod::get()->getSavedValue<T>(key, T{});
}

template<typename T>
void sset(const char* key, T val) {
    if (Mod::get()->hasSetting(key)) Mod::get()->setSettingValue<T>(key, val);
    else Mod::get()->setSavedValue(key, val);
}

template<typename T>
T gsaved(const char* key, T def) {
    return Mod::get()->getSavedValue<T>(key, def);
}

template<typename T>
void ssaved(const char* key, T val) {
    Mod::get()->setSavedValue(key, val);
}

void setGlobalTransitionDuration(float duration) {
    auto& tm = TransitionManager::get();
    tm.loadConfig();
    auto cfg = tm.getGlobalConfig();
    cfg.duration = std::clamp(duration, 0.05f, 3.0f);
    tm.setGlobalConfig(cfg);
    tm.saveConfig();
}

// Ids internos y nombres visibles de los presets.
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

} // namespace

namespace paimon::ui {

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
        sset<bool>("smooth-scroll", false);
        ssaved<bool>("popup-blur-show-placeholder", true);
        TransitionManager::get().setEnabled(false);
        TransitionManager::get().saveConfig();
        return;
    }

    sset<bool>("dynamic-popup-enabled", true);
    sset<bool>("dynamic-exit-enabled", true);
    sset<bool>("smooth-scroll", true);
    ssaved<bool>("popup-blur-show-placeholder", true);

    if (preset == "subtle") {
        setBase(true, 1.25f, 0.65f);
        ssaved<std::string>("dynamic-popup-style", "zoom-fade");
        ssaved<double>("dynamic-popup-speed", 1.35);
        ssaved<double>("dynamic-exit-speed", 1.45);
        ssaved<double>("popup-blur-fade-duration", 0.12);
        ssaved<double>("smooth-scroll-smoothness", 0.75);
        ssaved<double>("smooth-scroll-sensitivity", 1.55);
        ssaved<double>("smooth-ui-button-press-scale", 0.97);
        ssaved<bool>("smooth-ui-button-release-bounce", false);
        setGlobalTransitionDuration(0.25f);
    } else if (preset == "silky") {
        setBase(true, 0.88f, 1.15f);
        ssaved<std::string>("dynamic-popup-style", "paimonUI");
        ssaved<double>("dynamic-popup-speed", 0.88);
        ssaved<double>("dynamic-exit-speed", 0.95);
        ssaved<double>("popup-blur-fade-duration", 0.24);
        ssaved<double>("smooth-scroll-smoothness", 1.85);
        ssaved<double>("smooth-scroll-sensitivity", 2.0);
        ssaved<double>("smooth-ui-button-press-scale", 0.93);
        ssaved<bool>("smooth-ui-button-release-bounce", true);
        setGlobalTransitionDuration(0.48f);
    } else if (preset == "bouncy") {
        setBase(true, 0.95f, 1.25f);
        ssaved<std::string>("dynamic-popup-style", "jelly");
        ssaved<double>("dynamic-popup-speed", 0.90);
        ssaved<double>("dynamic-exit-speed", 1.05);
        ssaved<double>("popup-blur-fade-duration", 0.18);
        ssaved<double>("smooth-scroll-smoothness", 1.35);
        ssaved<double>("smooth-scroll-sensitivity", 2.15);
        ssaved<double>("smooth-ui-button-press-scale", 0.90);
        ssaved<bool>("smooth-ui-button-release-bounce", true);
        setGlobalTransitionDuration(0.40f);
    } else if (preset == "cinematic") {
        setBase(true, 0.72f, 1.35f);
        ssaved<std::string>("dynamic-popup-style", "elastic-drop");
        ssaved<double>("dynamic-popup-speed", 0.78);
        ssaved<double>("dynamic-exit-speed", 0.85);
        ssaved<double>("popup-blur-fade-duration", 0.32);
        ssaved<double>("smooth-scroll-smoothness", 2.2);
        ssaved<double>("smooth-scroll-sensitivity", 1.8);
        ssaved<double>("smooth-ui-button-press-scale", 0.92);
        ssaved<bool>("smooth-ui-button-release-bounce", true);
        TransitionManager::get().setEnabled(true);
        setGlobalTransitionDuration(0.65f);
    } else {
        setBase(true, 1.0f, 1.0f);
        ssaved<std::string>("dynamic-popup-style", "paimonUI");
        ssaved<double>("dynamic-popup-speed", 1.0);
        ssaved<double>("dynamic-exit-speed", 1.0);
        ssaved<double>("popup-blur-fade-duration", 0.18);
        ssaved<double>("smooth-scroll-smoothness", 1.0);
        ssaved<double>("smooth-scroll-sensitivity", 2.0);
        ssaved<double>("smooth-ui-button-press-scale", 0.94);
        ssaved<bool>("smooth-ui-button-release-bounce", true);
        setGlobalTransitionDuration(0.35f);
    }
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
    if (!Popup::init(420.f, 300.f)) return false;
    this->setTitle("Smooth UI");
    paimon::markDynamicPopup(this);

    rebuild();
    return true;
}

void SmoothUIConfigPopup::scheduleRebuild() {
    // Reconstruir en el siguiente frame: el control que dispara el cambio
    // sigue vivo dentro del scroll actual y no se puede destruir aun.
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

    // Preset rapido (cambia varias opciones de golpe)
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
                {"paimonUI", "jelly", "spiral", "drop-bounce", "skew-pop", "elastic",
                 "bounce", "slide-up", "slide-down", "slide-left", "slide-right",
                 "zoom-fade", "flip", "fold", "pop-rotate", "elastic-drop",
                 "glitch-shake", "card-turn", "fly-spin"},
                [] {
                    auto cur = gsaved<std::string>("dynamic-popup-style", "paimonUI");
                    std::vector<std::string> styles = {
                        "paimonUI", "jelly", "spiral", "drop-bounce", "skew-pop", "elastic",
                        "bounce", "slide-up", "slide-down", "slide-left", "slide-right",
                        "zoom-fade", "flip", "fold", "pop-rotate", "elastic-drop",
                        "glitch-shake", "card-turn", "fly-spin"};
                    for (size_t i = 0; i < styles.size(); ++i) {
                        if (styles[i] == cur) return static_cast<int>(i);
                    }
                    return 0;
                }(),
                [](int idx) {
                    static std::vector<std::string> const styles = {
                        "paimonUI", "jelly", "spiral", "drop-bounce", "skew-pop", "elastic",
                        "bounce", "slide-up", "slide-down", "slide-left", "slide-right",
                        "zoom-fade", "flip", "fold", "pop-rotate", "elastic-drop",
                        "glitch-shake", "card-turn", "fly-spin"};
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
                "Anima el zoom del editor con la rueda.",
                gsaved<bool>("smooth-scroll-editor-zoom", true),
                [](bool v) { ssaved<bool>("smooth-scroll-editor-zoom", v); }),
            kit::makeSliderRow(innerW,
                "Suavidad del zoom", "Que tan gradual se siente.",
                gsaved<double>("smooth-scroll-editor-zoom-smoothness", 1.15), 0.25, 3.0, fmtPlain,
                [](double v) { ssaved<double>("smooth-scroll-editor-zoom-smoothness", v); }),
        }));

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
