#include "RTXConfigLayer.hpp"

#include "../services/RTXManager.hpp"
#include "../services/RTXRenderer.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 300.f;

std::string pct(double v)     { return fmt::format("{}%", static_cast<int>(std::lround(v * 100.0))); }
std::string times(double v)   { return fmt::format("x{:.2f}", v); }
std::string count(double v)   { return fmt::format("{}", static_cast<int>(std::lround(v))); }
std::string signedF(double v) { return fmt::format("{:+.2f}", v); }
std::string plain(double v)   { return fmt::format("{:.2f}", v); }
}

namespace paimon::rtx {

RTXConfigLayer* RTXConfigLayer::create() {
    auto* ret = new RTXConfigLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool RTXConfigLayer::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    setTitle("Paimon RTX");
    this->addInfoButton("Paimon RTX",
        "Trazado de luz sobre la imagen del juego. Usa <cy>General</c> para calidad y presets, "
        "<cy>Luz</c> para el rebote y la oclusion, <cy>Reflejos</c>, <cy>Color</c> (tono, bloom, balance) "
        "y <cy>Lente</c> (grano, nitidez, ruido). Sube la <cy>resolucion del trazado</c> para mas calidad "
        "o baja los <co>rayos por pixel</c> si pierdes FPS. <cr>Restaurar</c> vuelve a los valores por defecto.");

    RTXManager::get().init();
    rebuild();

    auto* resetSpr = ButtonSprite::create("Restaurar", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    if (resetSpr) resetSpr->setScale(0.55f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
        [this](CCMenuItemSpriteExtra*) {
            RTXManager::get().resetToDefaults();
            m_dirty = false;
            scheduleRebuild();
            PaimonNotify::create("Paimon RTX restablecido", NotificationIcon::Success)->show();
        });
    resetBtn->setID("rtx-default-btn"_spr);
    resetBtn->setPosition({m_mainLayer->getContentSize().width - 62.f, 20.f});
    m_buttonMenu->addChild(resetBtn);

    m_statsLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statsLabel->setAnchorPoint({0.f, 0.5f});
    m_statsLabel->setScale(0.42f);
    m_statsLabel->setColor(kit::kDescColor);
    m_statsLabel->setPosition({14.f, 20.f});
    m_mainLayer->addChild(m_statsLabel);

    schedule(schedule_selector(RTXConfigLayer::refreshStats), 0.25f);
    schedule(schedule_selector(RTXConfigLayer::flush), 1.f);

    return true;
}

void RTXConfigLayer::onClose(CCObject* sender) {
    if (m_dirty) {
        RTXManager::get().saveConfig();
        m_dirty = false;
    }
    Popup::onClose(sender);
}

void RTXConfigLayer::flush(float) {
    if (!m_dirty) return;
    RTXManager::get().saveConfig();
    m_dirty = false;
}

void RTXConfigLayer::refreshStats(float) {
    if (!m_statsLabel) return;

    auto& renderer = RTXRenderer::get();
    if (renderer.isBroken()) {
        m_statsLabel->setString("RTX no disponible en esta GPU");
        m_statsLabel->setColor({255, 130, 130});
        return;
    }

    m_statsLabel->setColor(kit::kDescColor);
    if (!RTXManager::get().shouldRender()) {
        m_statsLabel->setString("En espera (fuera de ambito)");
        return;
    }

    float const ms = renderer.lastFrameMs();
    int const real = renderer.sceneWidth() > 0
        ? static_cast<int>(std::lround(100.0 * renderer.traceWidth() / renderer.sceneWidth()))
        : static_cast<int>(std::lround(renderer.activeScale() * 100.f));
    m_statsLabel->setString(fmt::format(
        "{:.1f} ms  ~{} FPS  |  trazado {}x{} ({}%)",
        ms, ms > 0.01f ? static_cast<int>(std::lround(1000.f / ms)) : 0,
        renderer.traceWidth(), renderer.traceHeight(), real).c_str());
}

void RTXConfigLayer::scheduleRebuild() {
    Ref<RTXConfigLayer> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (self && self->getParent()) self->rebuild();
    });
}

void RTXConfigLayer::touched(bool leavesPreset) {
    if (leavesPreset) {
        RTXManager::get().config().preset = static_cast<int>(Preset::Custom);
    }
    m_dirty = true;
}

void RTXConfigLayer::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    auto& cfg = RTXManager::get().config();

    auto content = m_mainLayer->getContentSize();
    float const scrollW = content.width - 24.f;
    float const scrollH = content.height - 36.f - 38.f;
    float const innerW = kit::cardInnerWidth(scrollW);

    auto* hero = kit::makeHeroToggle(scrollW,
        "Paimon RTX",
        "Luz rebotada, oclusion y reflejos trazados sobre la imagen del juego.",
        cfg.enabled,
        [this](bool v) {
            RTXManager::get().setEnabled(v);
        });

    auto* tabs = kit::makeTabBar(scrollW, {"General", "Luz", "Reflejos", "Color", "Lente"}, m_tab,
        [this](int i) {
            m_tab = i;
            scheduleRebuild();
        });

    std::vector<CCNode*> items = {hero, tabs};

    if (m_tab == 0) {
        std::vector<std::string> presets;
        for (int i = 0; i <= static_cast<int>(Preset::Custom); ++i) presets.push_back(presetName(i));

        items.push_back(kit::makeCard(scrollW, "Calidad", {140, 200, 255}, {
            kit::makeSliderRow(innerW,
                "Intensidad", "Cuanto se mezcla el resultado con la imagen original.",
                cfg.intensity, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().intensity = static_cast<float>(v); touched(false); }),
            kit::makeSelectRow(innerW,
                "Preset", "Ajusta de golpe resolucion, rayos y pases.",
                presets, std::clamp(cfg.preset, 0, 4),
                [this](int idx) {
                    applyPreset(RTXManager::get().config(), static_cast<Preset>(idx));
                    m_dirty = true;
                    scheduleRebuild();
                }),
            kit::makeSliderRow(innerW,
                "Resolucion del trazado", "Porcentaje de la pantalla al que se traza la luz.",
                cfg.renderScale, 0.20, 1.0, pct,
                [this](double v) { RTXManager::get().config().renderScale = static_cast<float>(v); touched(true); }),
            kit::makeToggleRow(innerW,
                "Calidad adaptativa", "Baja la resolucion sola si no llega a los FPS objetivo.",
                cfg.adaptive,
                [this](bool v) { RTXManager::get().config().adaptive = v; touched(false); }),
            kit::makeSliderRow(innerW,
                "FPS objetivo", "Presupuesto que persigue la calidad adaptativa.",
                cfg.targetFps, 30.0, 240.0, count,
                [this](double v) { RTXManager::get().config().targetFps = static_cast<int>(std::lround(v)); touched(false); }),
            kit::makeSliderRow(innerW,
                "Saltar fotogramas", "Traza uno de cada N+1 y ajusta la luz al movimiento actual.",
                cfg.frameSkip, 0.0, 3.0, count,
                [this](double v) { RTXManager::get().config().frameSkip = static_cast<int>(std::lround(v)); touched(true); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Donde se aplica", {130, 240, 170}, {
            kit::makeToggleRow(innerW, "En el nivel", nullptr, cfg.inGameplay,
                [this](bool v) { RTXManager::get().config().inGameplay = v; touched(false); }),
            kit::makeToggleRow(innerW, "En los menus", nullptr, cfg.inMenus,
                [this](bool v) { RTXManager::get().config().inMenus = v; touched(false); }),
            kit::makeToggleRow(innerW, "En el editor", "Apagado por defecto: estorba para construir.",
                cfg.inEditor,
                [this](bool v) { RTXManager::get().config().inEditor = v; touched(false); }),
            kit::makeToggleRow(innerW, "Pausar en pausa", "Deja de trazar mientras el nivel esta pausado.",
                cfg.skipWhenPaused,
                [this](bool v) { RTXManager::get().config().skipWhenPaused = v; touched(false); }),
        }));

        items.push_back(kit::makeHint(scrollW,
            "El trazado es lo unico que corre a resolucion reducida; el color y "
            "la lente siempre van a pantalla completa."));
    } else if (m_tab == 1) {
        items.push_back(kit::makeCard(scrollW, "Rebote de luz", {255, 210, 120}, {
            kit::makeSliderRow(innerW, "Fuerza", "Cuanta luz rebotada se suma a la escena.",
                cfg.giStrength, 0.0, 4.0, times,
                [this](double v) { RTXManager::get().config().giStrength = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Color del rebote", "0 = luz blanca, 1 = tinte del objeto que la refleja.",
                cfg.giSaturation, 0.0, 2.0, times,
                [this](double v) { RTXManager::get().config().giSaturation = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Umbral de luz", "Brillo a partir del cual un pixel emite.",
                cfg.lightThreshold, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().lightThreshold = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Rango del umbral", "Suavidad del corte entre lo que emite y lo que no.",
                cfg.lightRange, 0.01, 1.0, pct,
                [this](double v) { RTXManager::get().config().lightRange = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Caida", "Cuanto se apaga la luz con la distancia.",
                cfg.bounceFalloff, 0.1, 10.0, plain,
                [this](double v) { RTXManager::get().config().bounceFalloff = static_cast<float>(v); touched(false); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Trazado", {140, 200, 255}, {
            kit::makeSliderRow(innerW, "Rayos por pixel", "Mas rayos = menos ruido y mas coste.",
                cfg.rayCount, 1.0, 16.0, count,
                [this](double v) { RTXManager::get().config().rayCount = static_cast<int>(std::lround(v)); touched(true); }),
            kit::makeSliderRow(innerW, "Pasos por rayo", "Cuantas muestras avanza cada rayo antes de rendirse.",
                cfg.raySteps, 4.0, 32.0, count,
                [this](double v) { RTXManager::get().config().raySteps = static_cast<int>(std::lround(v)); touched(true); }),
            kit::makeSliderRow(innerW, "Alcance", "Longitud del rayo en fraccion de pantalla.",
                cfg.rayDistance, 0.02, 1.0, pct,
                [this](double v) { RTXManager::get().config().rayDistance = static_cast<float>(v); touched(true); }),
            kit::makeSliderRow(innerW, "Crecimiento del paso",
                "1.00 reparte los pasos por igual; mas alto los junta cerca y los separa lejos.",
                cfg.stepGrowth, 1.0, 1.5, plain,
                [this](double v) { RTXManager::get().config().stepGrowth = static_cast<float>(v); touched(true); }),
            kit::makeSliderRow(innerW, "Relieve", "Cuanto relieve se deduce del contraste de la imagen.",
                cfg.normalStrength, 0.5, 24.0, plain,
                [this](double v) { RTXManager::get().config().normalStrength = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Grosor", "Margen antes de dar un impacto por valido.",
                cfg.thickness, 0.01, 2.0, plain,
                [this](double v) { RTXManager::get().config().thickness = static_cast<float>(v); touched(false); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Oclusion", {200, 160, 255}, {
            kit::makeSliderRow(innerW, "Fuerza", "Sombra de contacto en esquinas y huecos.",
                cfg.aoStrength, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().aoStrength = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Radio", "Hasta donde cuenta un vecino como oclusor.",
                cfg.aoRadius, 0.01, 1.0, pct,
                [this](double v) { RTXManager::get().config().aoRadius = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Dureza", "Curva de la sombra: bajo = dura, alto = suave.",
                cfg.aoPower, 0.2, 4.0, plain,
                [this](double v) { RTXManager::get().config().aoPower = static_cast<float>(v); touched(false); }),
        }));
    } else if (m_tab == 2) {
        items.push_back(kit::makeCard(scrollW, "Reflejos", {120, 230, 255}, {
            kit::makeSliderRow(innerW, "Fuerza", "Reflejo en espacio de pantalla sobre las superficies.",
                cfg.reflectStrength, 0.0, 2.0, times,
                [this](double v) { RTXManager::get().config().reflectStrength = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Rugosidad", "Dispersa el rayo reflejado: 0 = espejo.",
                cfg.reflectRoughness, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().reflectRoughness = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Fresnel", "Cuanto crece el reflejo en angulos rasantes.",
                cfg.reflectFresnel, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().reflectFresnel = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Desvanecido", "Distancia a la que el reflejo se apaga.",
                cfg.reflectFade, 0.01, 1.0, pct,
                [this](double v) { RTXManager::get().config().reflectFade = static_cast<float>(v); touched(false); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Rayos de luz", {255, 220, 140}, {
            kit::makeSliderRow(innerW, "Fuerza", "Haces volumetricos que salen de las zonas brillantes.",
                cfg.godRayStrength, 0.0, 2.0, times,
                [this](double v) { RTXManager::get().config().godRayStrength = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Decaimiento", "Cuanto se apaga el haz a lo largo del recorrido.",
                cfg.godRayDecay, 0.5, 0.995, plain,
                [this](double v) { RTXManager::get().config().godRayDecay = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Densidad", "Longitud del abanico de rayos.",
                cfg.godRayDensity, 0.05, 2.0, plain,
                [this](double v) { RTXManager::get().config().godRayDensity = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Foco X", "Posicion horizontal del origen de los haces.",
                cfg.godRayX, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().godRayX = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Foco Y", "Posicion vertical del origen de los haces.",
                cfg.godRayY, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().godRayY = static_cast<float>(v); touched(false); }),
        }));
    } else if (m_tab == 3) {
        std::vector<std::string> tonemaps;
        for (int i = 0; i <= static_cast<int>(Tonemap::Uncharted2); ++i) tonemaps.push_back(tonemapName(i));

        items.push_back(kit::makeCard(scrollW, "Tono", {255, 180, 200}, {
            kit::makeSelectRow(innerW, "Mapeo de tonos", "Como se comprime el rango alto tras sumar la luz.",
                tonemaps, std::clamp(cfg.tonemap, 0, 4),
                [this](int idx) { RTXManager::get().config().tonemap = idx; touched(false); }),
            kit::makeSliderRow(innerW, "Exposicion", "Paradas de luz antes del mapeo de tonos.",
                cfg.exposure, -2.0, 2.0, signedF,
                [this](double v) { RTXManager::get().config().exposure = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Contraste", nullptr,
                cfg.contrast, 0.5, 2.0, times,
                [this](double v) { RTXManager::get().config().contrast = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Saturacion", nullptr,
                cfg.saturation, 0.0, 2.0, times,
                [this](double v) { RTXManager::get().config().saturation = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Gamma", nullptr,
                cfg.gamma, 0.5, 2.0, plain,
                [this](double v) { RTXManager::get().config().gamma = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Rango alto",
                "Cuantas veces mas brillante que el blanco puede pesar un reflejo al calcular la luz.",
                cfg.hdrRange, 1.0, 16.0, times,
                [this](double v) { RTXManager::get().config().hdrRange = static_cast<float>(v); touched(false); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Exposicion automatica", {255, 200, 120}, {
            kit::makeToggleRow(innerW, "Activada",
                "Ajusta el brillo segun lo oscura que este la pantalla, como el ojo.",
                cfg.adaptEnabled,
                [this](bool v) { RTXManager::get().config().adaptEnabled = v; touched(false); }),
            kit::makeSliderRow(innerW, "Gris medio", "Brillo al que intenta llevar la escena.",
                cfg.adaptKey, 0.04, 0.60, plain,
                [this](double v) { RTXManager::get().config().adaptKey = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Velocidad", "Cuanto tarda en acostumbrarse a un cambio de luz.",
                cfg.adaptSpeed, 0.1, 6.0, plain,
                [this](double v) { RTXManager::get().config().adaptSpeed = static_cast<float>(v); touched(false); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Balance", {160, 220, 255}, {
            kit::makeSliderRow(innerW, "Temperatura", "Negativo = frio, positivo = calido.",
                cfg.temperature, -1.0, 1.0, signedF,
                [this](double v) { RTXManager::get().config().temperature = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Matiz", "Desplaza el verde contra el magenta.",
                cfg.tint, -1.0, 1.0, signedF,
                [this](double v) { RTXManager::get().config().tint = static_cast<float>(v); touched(false); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Bloom", {255, 235, 150}, {
            kit::makeSliderRow(innerW, "Fuerza", "Halo alrededor de lo que brilla.",
                cfg.bloomStrength, 0.0, 3.0, times,
                [this](double v) { RTXManager::get().config().bloomStrength = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Umbral", "Brillo minimo que entra al bloom.",
                cfg.bloomThreshold, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().bloomThreshold = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Rodilla",
                "0 = corte seco en el umbral, 1 = lo que ronda el umbral entra poco a poco.",
                cfg.bloomSoftKnee, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().bloomSoftKnee = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Radio", "Separacion de las muestras al recomponer la cadena.",
                cfg.bloomRadius, 0.5, 6.0, plain,
                [this](double v) { RTXManager::get().config().bloomRadius = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Anchura", "Cuanto pesa el nivel ancho frente al fino en cada mezcla.",
                cfg.bloomBlend, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().bloomBlend = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Anamorfico", "Estira el halo en horizontal, como una lente de cine.",
                cfg.bloomAnamorphic, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().bloomAnamorphic = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Pases", "Niveles de la cadena: mas pases = halo mas ancho.",
                cfg.bloomPasses, 1.0, 5.0, count,
                [this](double v) { RTXManager::get().config().bloomPasses = static_cast<int>(std::lround(v)); touched(true); }),
        }));

        items.push_back(kit::makeHint(scrollW,
            "La luz se suma en <cy>espacio lineal</c>. Para conservar el aspecto original, "
            "deja exposicion en cero, contraste, saturacion y gamma en uno, y desactiva "
            "los efectos de luz y lente."));
    } else {
        items.push_back(kit::makeCard(scrollW, "Lente", {255, 170, 170}, {
            kit::makeSliderRow(innerW, "Aberracion cromatica", "Separacion de canales hacia los bordes.",
                cfg.chromatic, 0.0, 2.0, times,
                [this](double v) { RTXManager::get().config().chromatic = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Vineteado", "Oscurecido de las esquinas.",
                cfg.vignette, 0.0, 2.0, times,
                [this](double v) { RTXManager::get().config().vignette = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Grano", "Ruido de pelicula sobre la imagen final.",
                cfg.grain, 0.0, 1.0, pct,
                [this](double v) { RTXManager::get().config().grain = static_cast<float>(v); touched(false); }),
            kit::makeSliderRow(innerW, "Nitidez", "Realce suave de los bordes de la imagen.",
                cfg.sharpen, 0.0, 2.0, times,
                [this](double v) { RTXManager::get().config().sharpen = static_cast<float>(v); touched(false); }),
        }));

        items.push_back(kit::makeCard(scrollW, "Ruido del trazado", {180, 200, 255}, {
            kit::makeSliderRow(innerW, "Pasadas del filtro",
                "Cada pasada dobla su alcance: 3 cubren 15x15 pixeles.",
                cfg.atrousPasses, 0.0, 5.0, count,
                [this](double v) { RTXManager::get().config().atrousPasses = static_cast<int>(std::lround(v)); touched(true); }),
            kit::makeSliderRow(innerW, "Suavizado",
                "Cuanto puede mezclarse la luz a traves de un borde del dibujo.",
                cfg.denoise, 0.0, 4.0, plain,
                [this](double v) { RTXManager::get().config().denoise = static_cast<float>(v); touched(true); }),
            kit::makeSliderRow(innerW, "Acumulacion temporal",
                "Reutiliza el fotograma anterior reproyectado; es lo que mas ruido quita.",
                cfg.temporal, 0.0, 0.97, pct,
                [this](double v) { RTXManager::get().config().temporal = static_cast<float>(v); touched(true); }),
            kit::makeToggleRow(innerW, "Recorte anti-estela",
                "Descarta el historial que se salga del vecindario actual.",
                cfg.ghostClamp,
                [this](bool v) { RTXManager::get().config().ghostClamp = v; touched(false); }),
            kit::makeSliderRow(innerW, "Dureza del recorte",
                "Desviaciones tipicas que se le permiten al historial. Bajo = sin estela, mas ruido.",
                cfg.clampSigma, 0.0, 3.0, plain,
                [this](double v) { RTXManager::get().config().clampSigma = static_cast<float>(v); touched(false); }),
        }));

        items.push_back(kit::makeHint(scrollW,
            "El historial acompana a la camara y se descarta cuando cambia la imagen. "
            "Si aun ves rastros, "
            "baja la <cy>dureza del recorte</c> antes que la acumulacion."));
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 38.f});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::rtx
