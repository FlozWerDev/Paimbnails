#include "LevelEntryConfigPopup.hpp"

#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/LevelEntryEffects.hpp"

#include <fmt/format.h>

#include <utility>

using namespace cocos2d;
using namespace geode::prelude;

namespace {

namespace kit = paimon::configkit;

int styleIndex(paimon::transitions::LevelEntryStyle style) {
    auto const& styles = paimon::transitions::levelEntryStyles();
    for (size_t i = 0; i < styles.size(); ++i) {
        if (styles[i] == style) return static_cast<int>(i);
    }
    return 0;
}

int exitModeIndex(paimon::transitions::LevelExitMode mode) {
    auto const& modes = paimon::transitions::levelExitModes();
    for (size_t i = 0; i < modes.size(); ++i) {
        if (modes[i] == mode) return static_cast<int>(i);
    }
    return 0;
}

} // namespace

namespace paimon::transitions {

LevelEntryConfigPopup* LevelEntryConfigPopup::create() {
    auto* ret = new LevelEntryConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool LevelEntryConfigPopup::init() {
    if (!PaimonPopup::init(420.f, 300.f)) return false;
    setTitle("Smooth Level Transitions+");
    addInfoButton("Smooth Level Transitions+",
        "Coreografia la entrada y la salida de los niveles manteniendo ambas escenas vivas.\n\n"
        "Ajusta la <cy>curva</c>, la <cy>duracion</c> y que capas se animan. Todo se guarda al instante.");
    addCorners();
    paimon::markDynamicPopup(this);
    rebuild();
    return true;
}

void LevelEntryConfigPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    auto config = getLevelEntryEffectsConfig();

    // every row edits the stored config the same way: load, set one field, save.
    auto setFlag = [](bool LevelEntryEffectsConfig::* field, bool value) {
        auto next = getLevelEntryEffectsConfig();
        next.*field = value;
        saveLevelEntryEffectsConfig(next);
    };
    auto setFloat = [](float LevelEntryEffectsConfig::* field, double value) {
        auto next = getLevelEntryEffectsConfig();
        next.*field = static_cast<float>(value);
        saveLevelEntryEffectsConfig(next);
    };
    auto pickEnum = [](auto const& options, int index, auto field) {
        if (index < 0 || index >= static_cast<int>(options.size())) return;
        auto next = getLevelEntryEffectsConfig();
        next.*field = options[static_cast<size_t>(index)];
        saveLevelEntryEffectsConfig(next);
    };

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 44.f;
    float innerW = kit::cardInnerWidth(scrollW);

    std::vector<std::string> styleNames;
    for (auto style : levelEntryStyles()) {
        styleNames.push_back(levelEntryStyleName(style));
    }
    auto exitStyleNames = styleNames;
    std::vector<std::string> exitModeNames;
    for (auto mode : levelExitModes()) {
        exitModeNames.push_back(levelExitModeName(mode));
    }

    std::vector<CCNode*> items;
    items.push_back(kit::makeHeroToggle(
        scrollW,
        "Transicion real al nivel",
        "Mantiene ambas escenas vivas y coreografia el nivel antes de empezar.",
        config.enabled,
        [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::enabled, value); }
    ));

    items.push_back(kit::makeCard(scrollW, "Movimiento", {255, 202, 105}, {
        kit::makeSelectRow(
            innerW,
            "Curva de entrada",
            "Smooth+ es fiel al mod original, con una llegada mas limpia.",
            std::move(styleNames),
            styleIndex(config.style),
            [pickEnum](int index) { pickEnum(levelEntryStyles(), index, &LevelEntryEffectsConfig::style); }
        ),
        kit::makeSliderRow(
            innerW,
            "Duracion",
            "Un segundo reproduce el ritmo del original.",
            config.duration, .35, 1.8,
            [](double value) { return fmt::format("{:.2f}s", value); },
            [setFloat](double value) { setFloat(&LevelEntryEffectsConfig::duration, value); }
        ),
        kit::makeSliderRow(
            innerW,
            "Intensidad",
            "Ajusta distancias, giro y profundidad sin cambiar la duracion.",
            config.intensity, .25, 1.5,
            [](double value) { return fmt::format("x{:.2f}", value); },
            [setFloat](double value) { setFloat(&LevelEntryEffectsConfig::intensity, value); }
        ),
        kit::makeToggleRow(
            innerW,
            "Cascada de objetos",
            "Escalona los bloques visibles de izquierda a derecha.",
            config.staggerObjects,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::staggerObjects, value); }
        ),
    }));

    items.push_back(kit::makeCard(scrollW, "Salida del nivel", {255, 155, 130}, {
        kit::makeSelectRow(
            innerW,
            "Al salir",
            "Puede invertir la entrada o usar una personalidad independiente.",
            std::move(exitModeNames),
            exitModeIndex(config.exitMode),
            [pickEnum](int index) { pickEnum(levelExitModes(), index, &LevelEntryEffectsConfig::exitMode); }
        ),
        kit::makeSelectRow(
            innerW,
            "Estilo diferente",
            "Se usa cuando Al salir esta en Estilo diferente.",
            std::move(exitStyleNames),
            styleIndex(config.exitStyle),
            [pickEnum](int index) { pickEnum(levelEntryStyles(), index, &LevelEntryEffectsConfig::exitStyle); }
        ),
        kit::makeSliderRow(
            innerW,
            "Duracion de salida",
            "Solo afecta al estilo diferente.",
            config.exitDuration, .25, 1.8,
            [](double value) { return fmt::format("{:.2f}s", value); },
            [setFloat](double value) { setFloat(&LevelEntryEffectsConfig::exitDuration, value); }
        ),
        kit::makeSliderRow(
            innerW,
            "Intensidad de salida",
            "Solo afecta al estilo diferente.",
            config.exitIntensity, .25, 1.5,
            [](double value) { return fmt::format("x{:.2f}", value); },
            [setFloat](double value) { setFloat(&LevelEntryEffectsConfig::exitIntensity, value); }
        ),
    }));

    items.push_back(kit::makeCard(scrollW, "Escena", {115, 215, 255}, {
        kit::makeToggleRow(
            innerW,
            "Pantalla anterior",
            "Saca controles y decoraciones al entrar, y recibe el pause y la pagina de destino al salir.",
            config.animatePage,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::animatePage, value); }
        ),
        kit::makeToggleRow(
            innerW,
            "Fondo",
            "Revela el fondo durante la segunda mitad.",
            config.animateBackground,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::animateBackground, value); }
        ),
        kit::makeToggleRow(
            innerW,
            "Suelos opuestos",
            "Los dos suelos entran desde direcciones contrarias.",
            config.animateGround,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::animateGround, value); }
        ),
        kit::makeToggleRow(
            innerW,
            "Middleground",
            "Trae la decoracion intermedia desde la derecha.",
            config.animateMiddleground,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::animateMiddleground, value); }
        ),
        kit::makeToggleRow(
            innerW,
            "Gradientes",
            "Hace aparecer gradualmente los gradientes del nivel.",
            config.animateGradients,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::animateGradients, value); }
        ),
    }));

    items.push_back(kit::makeCard(scrollW, "Gameplay congelado", {170, 235, 155}, {
        kit::makeToggleRow(
            innerW,
            "HUD",
            "Escala controles y desliza intento, progreso e informacion.",
            config.animateHud,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::animateHud, value); }
        ),
        kit::makeToggleRow(
            innerW,
            "Jugador",
            "El icono entra desde fuera de pantalla segun su modo.",
            config.animatePlayer,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::animatePlayer, value); }
        ),
        kit::makeToggleRow(
            innerW,
            "Objetos visibles",
            "Bloques activos suben, giran, escalan y recuperan su opacidad.",
            config.animateObjects,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::animateObjects, value); }
        ),
        kit::makeToggleRow(
            innerW,
            "Movimiento reducido",
            "Respeta el ajuste global y deja solo un revelado corto.",
            config.respectReducedMotion,
            [setFlag](bool value) { setFlag(&LevelEntryEffectsConfig::respectReducedMotion, value); }
        ),
    }));

    items.push_back(kit::makeHint(
        scrollW,
        "La entrada mantiene la fisica detenida hasta terminar. Reflejar entrada reproduce la misma "
        "personalidad en sentido inverso al abandonar el nivel: el pause o la pantalla de final se "
        "despiden con el nivel y la pagina de destino se arma detras. Al desactivar la coreografia, "
        "vuelve a usarse el tipo de transicion seleccionado en la pantalla anterior."
    ));

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 8.f});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::transitions
