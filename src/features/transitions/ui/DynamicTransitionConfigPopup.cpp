#include "DynamicTransitionConfigPopup.hpp"
#include "DynamicTransitionScene.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::transitions::dynamic {
namespace {
namespace kit = paimon::configkit;

char const* text(char const* spanish, char const* english) {
    return Localization::get().getLanguage() == Localization::Language::SPANISH ? spanish : english;
}

std::vector<std::string> presetNames() {
    return {text("Equilibrado", "Balanced"), text("Rapido", "Snappy"),
        text("Sedoso", "Silky"), text("Con resorte", "Spring"),
        text("Cinematico", "Cinematic"), text("Personalizado", "Custom")};
}

CCNode* demoScreen(CCSize size, bool destination) {
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->addChild(CCLayerColor::create(destination ? ccColor4B{32, 48, 95, 255} :
        ccColor4B{18, 24, 43, 255}, size.width, size.height));
    auto* title = CCLabelBMFont::create(destination ? text("Nuevo layer", "New layer") :
        text("Menu", "Menu"), "goldFont.fnt");
    title->setScale(.45f);
    title->setPosition({size.width / 2.f, size.height - 20.f});
    root->addChild(title);
    for (int i = 0; i < 3; ++i) {
        auto* row = CCLayerColor::create(destination ? ccColor4B{80, 116, 182, 255} :
            ccColor4B{50, 65, 95, 255}, size.width - 100.f, 14.f);
        row->setPosition({78.f, 24.f + i * 25.f});
        root->addChild(row);
    }
    auto* icon = CCLayerColor::create({115, 210, 255, 255}, 40.f, 40.f);
    icon->setPosition({18.f, 18.f});
    root->addChild(icon);
    auto* label = CCLabelBMFont::create(destination ? "OK" : ">", "bigFont.fnt");
    label->setScale(.45f);
    label->setPosition({38.f, 38.f});
    root->addChild(label);
    return root;
}

class PreviewPopup : public Popup {
public:
    static PreviewPopup* create() {
        auto* popup = new PreviewPopup();
        if (popup->init()) { popup->autorelease(); return popup; }
        delete popup;
        return nullptr;
    }

    void update(float dt) override {
        if (!m_playing || !m_visual) return;
        m_elapsed = std::min(m_duration, m_elapsed + limit(dt, 0.f, 0.f, 2.f));
        m_visual->setProgress(m_elapsed / m_duration);
        if (m_elapsed >= m_duration) {
            m_playing = false;
            unscheduleUpdate();
        }
    }

protected:
    bool init() override {
        if (!Popup::init(380.f, 254.f)) return false;
        setTitle(text("Vista previa", "Preview"));
        paimon::markDynamicPopup(this);
        m_stage = CCClippingNode::create(CCLayerColor::create({255, 255, 255, 255}, 340.f, 146.f));
        m_stage->setContentSize({340.f, 146.f});
        m_stage->setPosition({20.f, 65.f});
        m_mainLayer->addChild(m_stage);
        auto* hint = CCLabelBMFont::create(text("Prueba abrir y volver con tus ajustes.",
            "Try opening and returning with your settings."), "chatFont.fnt");
        hint->limitLabelWidth(340.f, .5f, .2f);
        hint->setPosition({190.f, 47.f});
        m_mainLayer->addChild(hint);
        addButton(text("Abrir", "Open"), 125.f, false);
        addButton(text("Volver", "Back"), 255.f, true);
        play(false);
        return true;
    }

private:
    void addButton(char const* label, float x, bool backwards) {
        auto* sprite = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", .6f);
        sprite->setScale(.55f);
        auto* button = CCMenuItemExt::createSpriteExtra(sprite,
            [this, backwards](CCMenuItemSpriteExtra*) { play(backwards); });
        button->setPosition({x, 24.f});
        m_buttonMenu->addChild(button);
    }

    void play(bool backwards) {
        m_stage->removeAllChildrenWithCleanup(true);
        m_visual = nullptr;
        m_playing = false;
        unscheduleUpdate();
        auto config = getConfig();
        CCSize size{340.f, 146.f};
        Rect origin = config.origin == Origin::Button ? Rect{18.f, 18.f, 40.f, 40.f} :
            fallbackOrigin(size.width, size.height, config.origin);
        if (!config.enabled || (backwards && !config.animateBack)) {
            m_stage->addChild(demoScreen(size, !backwards));
            return;
        }
        if (paimon::settings::smoothui::reducedMotion()) {
            auto* screen = demoScreen(size, !backwards);
            m_stage->addChild(screen);
            if (config.reducedMotion == ReducedMotion::Fade) {
                auto* cover = CCLayerColor::create({0, 0, 0, 255}, size.width, size.height);
                m_stage->addChild(cover, 1);
                cover->runAction(CCFadeTo::create(.14f, 0));
            }
            return;
        }
        config = animationConfig(config);
        m_duration = backwards ? config.backDuration : config.duration;
        auto* from = demoScreen(size, backwards);
        auto* to = demoScreen(size, !backwards);
        m_visual = Visual::create(from, to, size, config, origin, backwards, config.origin == Origin::Button);
        if (!m_visual) {
            m_stage->addChild(to);
            return;
        }
        m_stage->addChild(m_visual);
        m_elapsed = 0.f;
        m_playing = true;
        scheduleUpdate();
    }

    CCNode* m_stage = nullptr;
    Visual* m_visual = nullptr;
    float m_elapsed = 0.f, m_duration = .42f;
    bool m_playing = false;
};

}

DynamicTransitionConfigPopup* DynamicTransitionConfigPopup::create() {
    auto* popup = new DynamicTransitionConfigPopup();
    if (popup->init()) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

bool DynamicTransitionConfigPopup::init() {
    if (!Popup::init(430.f, 300.f)) return false;
    setTitle("Dynamic Transition");
    paimon::markDynamicPopup(this);
    m_config = getConfig();
    rebuild();
    auto addFooterButton = [this](char const* label, float x, auto callback) {
        auto* sprite = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", .6f);
        sprite->setScale(.52f);
        auto* button = CCMenuItemExt::createSpriteExtra(sprite, callback);
        button->setPosition({x, 18.f});
        m_buttonMenu->addChild(button);
    };
    addFooterButton(text("Vista previa", "Preview"), 135.f, [this](CCMenuItemSpriteExtra*) {
        if (auto* preview = PreviewPopup::create()) kit::showAbove(preview, this);
    });
    addFooterButton(text("Restaurar", "Reset"), 295.f, [this](CCMenuItemSpriteExtra*) {
        resetConfig();
        m_config = getConfig();
        Mod::get()->setSavedValue("dynamic-transition-preset", 0);
        scheduleRebuild();
    });
    return true;
}

void DynamicTransitionConfigPopup::persist() {
    saveConfig(m_config);
    m_config = getConfig();
    Mod::get()->setSavedValue("dynamic-transition-preset", 5);
    if (m_presetLabel) m_presetLabel->setString(text("Personalizado", "Custom"));
}

void DynamicTransitionConfigPopup::scheduleRebuild() {
    Ref<DynamicTransitionConfigPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (!paimon::isRuntimeShuttingDown() && self->getParent()) self->rebuild();
    });
}

void DynamicTransitionConfigPopup::applyPreset(int preset) {
    if (preset < 0 || preset > 4) return;
    Config config;
    config.enabled = m_config.enabled;
    config.animateBack = m_config.animateBack;
    config.animateKeyboardBack = m_config.animateKeyboardBack;
    config.animatePopups = m_config.animatePopups;
    config.animateDropdowns = m_config.animateDropdowns;
    config.animateBlockingLayers = m_config.animateBlockingLayers;
    config.animateDialogs = m_config.animateDialogs;
    config.animateEditorPanels = m_config.animateEditorPanels;
    config.animateGameplayPanels = m_config.animateGameplayPanels;
    config.animateOtherMods = m_config.animateOtherMods;
    config.includeInstant = m_config.includeInstant;
    config.onlyFromButton = m_config.onlyFromButton;
    config.syncSmoothUI = m_config.syncSmoothUI;
    config.quality = m_config.quality;
    config.reducedMotion = m_config.reducedMotion;
    if (preset == 1) {
        config.duration = .26f; config.backDuration = .22f;
        config.dim = .14f; config.backgroundScale = .98f;
    } else if (preset == 2) {
        config.curve = Curve::Soft;
        config.duration = .55f; config.backDuration = .42f;
        config.backgroundScale = .98f;
    } else if (preset == 3) {
        config.curve = Curve::Spring;
        config.duration = .48f; config.backDuration = .38f; config.spring = .065f;
    } else if (preset == 4) {
        config.style = Style::Card; config.curve = Curve::Soft;
        config.duration = .65f; config.backDuration = .5f;
        config.dim = .4f; config.backgroundScale = .92f; config.cornerRadius = 28.f;
    }
    saveConfig(config);
    m_config = getConfig();
    Mod::get()->setSavedValue("dynamic-transition-preset", preset);
    scheduleRebuild();
}

void DynamicTransitionConfigPopup::rebuild() {
    m_presetLabel = nullptr;
    if (m_scroll) { m_scroll->removeFromParent(); m_scroll = nullptr; }
    float width = 406.f, inner = kit::cardInnerWidth(width);
    auto seconds = [](double value) { return fmt::format("{:.2f}s", value); };
    auto percent = [](double value) { return fmt::format("{:.0f}%", value * 100.); };
    std::vector<CCNode*> items;
    items.push_back(kit::makeHeroToggle(width, "Dynamic Transition",
        text("Cada layer se abre como una app desde su boton.", "Open each layer like an app from its button."),
        m_config.enabled, [this](bool value) { m_config.enabled = value; persist(); }));
    items.push_back(kit::makeTabBar(width, {text("Estilo", "Style"),
        text("Movimiento", "Motion"), text("Alcance", "Scope"), text("Paneles", "Panels")}, m_tab,
        [this](int value) { m_tab = value; scheduleRebuild(); }));

    if (m_tab == 0) {
        items.push_back(kit::makeCard(width, text("Apariencia", "Appearance"), {115, 210, 255}, {
            kit::makeSelectRow(inner, text("Preset", "Preset"), text("Un punto de partida para tus ajustes.", "A starting point for your settings."),
                presetNames(), std::clamp(Mod::get()->getSavedValue<int>("dynamic-transition-preset", 0), 0, 5),
                [this](int value) { applyPreset(value); }, &m_presetLabel),
            kit::makeSelectRow(inner, text("Estilo", "Style"), text("Expansion, tarjeta, deslizamiento o zoom.", "Expansion, card, slide or zoom."),
                {text("App desde boton", "App from button"), text("Tarjeta", "Card"), text("Deslizar", "Slide"), "Zoom"},
                static_cast<int>(m_config.style), [this](int value) { m_config.style = static_cast<Style>(value); persist(); }),
            kit::makeSelectRow(inner, text("Origen", "Origin"), text("Punto desde el que aparece el nuevo layer.", "Where the new layer starts opening."),
                {text("Boton pulsado", "Pressed button"), text("Centro", "Center"), text("Abajo", "Bottom"), text("Izquierda", "Left")},
                static_cast<int>(m_config.origin), [this](int value) { m_config.origin = static_cast<Origin>(value); persist(); }),
            kit::makeSliderRow(inner, text("Esquinas redondeadas", "Rounded corners"), text("Se suavizan hasta llenar la pantalla.", "Corners straighten as the layer fills the screen."),
                m_config.cornerRadius, 0., 48., [](double value) { return fmt::format("{:.0f}", value); },
                [this](double value) { m_config.cornerRadius = value; persist(); }),
            kit::makeSliderRow(inner, text("Fusion del boton", "Button blend"), text("Cuanto tarda el boton en dar paso al layer.", "How long the button takes to blend into the layer."),
                m_config.buttonBlend, 0., .6, percent, [this](double value) { m_config.buttonBlend = value; persist(); }),
        }));
        items.push_back(kit::makeCard(width, text("Fondo", "Background"), {190, 165, 255}, {
            kit::makeSliderRow(inner, text("Oscurecimiento", "Dimming"), text("Oscurece la pantalla anterior durante la apertura.", "Dims the previous screen while opening."),
                m_config.dim, 0., .65, percent, [this](double value) { m_config.dim = value; persist(); }),
            kit::makeSliderRow(inner, text("Escala del fondo", "Background scale"), text("Aleja suavemente la pantalla anterior.", "Gently moves the previous screen back."),
                m_config.backgroundScale, .85, 1., percent, [this](double value) { m_config.backgroundScale = value; persist(); }),
        }));
    } else if (m_tab == 1) {
        items.push_back(kit::makeCard(width, text("Tiempo y curva", "Timing and curve"), {130, 235, 175}, {
            kit::makeSliderRow(inner, text("Duracion de apertura", "Opening duration"), text("Tiempo para entrar al nuevo layer.", "Time to enter the new layer."),
                m_config.duration, .12, 1.5, seconds, [this](double value) { m_config.duration = value; persist(); }),
            kit::makeSliderRow(inner, text("Duracion de regreso", "Return duration"), text("Tiempo para volver a la pantalla anterior.", "Time to return to the previous screen."),
                m_config.backDuration, .12, 1.5, seconds, [this](double value) { m_config.backDuration = value; persist(); }),
            kit::makeSelectRow(inner, text("Curva", "Curve"), text("Controla la aceleracion y el frenado.", "Controls acceleration and deceleration."),
                {text("Rapida y fluida", "Smooth ease out"), text("Resorte", "Spring"), text("Suave (S)", "Soft (S)"), text("Lineal", "Linear")},
                static_cast<int>(m_config.curve), [this](int value) { m_config.curve = static_cast<Curve>(value); persist(); }),
            kit::makeSliderRow(inner, text("Fuerza del resorte", "Spring strength"), text("Pequeno impulso al terminar, con la curva Resorte.", "A small overshoot when using the Spring curve."),
                m_config.spring, 0., .12, percent, [this](double value) { m_config.spring = value; persist(); }),
            kit::makeToggleRow(inner, text("Sincronizar con Smooth UI", "Sync with Smooth UI"), text("Usa su velocidad general y fuerza del movimiento.", "Uses its global speed and motion strength."),
                m_config.syncSmoothUI, [this](bool value) { m_config.syncSmoothUI = value; persist(); }),
        }));
        items.push_back(kit::makeHint(width, text("Los cambios se guardan al instante. La vista previa usa tus ajustes actuales.",
            "Changes are saved immediately. Preview uses your current settings.")));
    } else if (m_tab == 2) {
        items.push_back(kit::makeCard(width, text("Navegacion", "Navigation"), {255, 205, 125}, {
            kit::makeToggleRow(inner, text("Animar regreso", "Animate return"), text("Recoge el layer hacia el boton que lo abrio.", "Folds the layer toward the button that opened it."),
                m_config.animateBack, [this](bool value) { m_config.animateBack = value; persist(); }),
            kit::makeToggleRow(inner, text("Escape y Volver", "Escape and Back"), text("Anima el regreso con Escape o el boton Volver de Android.", "Animates returns with Escape or Android's Back button."),
                m_config.animateKeyboardBack, [this](bool value) { m_config.animateKeyboardBack = value; persist(); }),
            kit::makeToggleRow(inner, text("Navegacion instantanea", "Instant navigation"), text("Anima tambien los layers que abren sin transicion.", "Also animates layers that open without a transition."),
                m_config.includeInstant, [this](bool value) { m_config.includeInstant = value; persist(); }),
            kit::makeToggleRow(inner, text("Solo desde botones", "Only from buttons"), text("Omite aperturas automaticas o por teclado sin origen.", "Skips automatic or keyboard openings without an origin."),
                m_config.onlyFromButton, [this](bool value) { m_config.onlyFromButton = value; persist(); }),
            kit::makeSelectRow(inner, text("Movimiento reducido", "Reduced motion"), text("Cuando esta activo en Smooth UI, reemplaza la expansion.", "Replaces expansion when reduced motion is enabled in Smooth UI."),
                {text("Fundido corto", "Short fade"), text("Cambio instantaneo", "Instant switch")},
                static_cast<int>(m_config.reducedMotion), [this](int value) { m_config.reducedMotion = static_cast<ReducedMotion>(value); persist(); }),
            kit::makeSelectRow(inner, text("Calidad", "Quality"), text("Reduce la resolucion de la animacion en equipos lentos.", "Lowers animation resolution on slower devices."),
                {text("Alta", "High"), text("Equilibrada", "Balanced"), text("Rendimiento", "Performance")},
                m_config.quality > .875f ? 0 : m_config.quality > .625f ? 1 : 2,
                [this](int value) { m_config.quality = value == 0 ? 1.f : value == 1 ? .75f : .5f; persist(); }),
        }));
        items.push_back(kit::makeHint(width, text("Escape, la flecha, la X y los botones de cierre usan el origen recordado. Los cambios de escena de niveles y editor conservan su configuracion.",
            "Escape, arrows, X and close buttons use the remembered origin. Level and editor scene changes keep their settings.")));
    } else {
        items.push_back(kit::makeCard(width, text("Tipos de panel", "Panel types"), {150, 210, 255}, {
            kit::makeToggleRow(inner, text("Popups del juego", "Game popups"), text("Avisos, perfiles, comentarios, opciones y ventanas de edicion.", "Alerts, profiles, comments, options and editing windows."),
                m_config.animatePopups, [this](bool value) { m_config.animatePopups = value; persist(); }),
            kit::makeToggleRow(inner, text("Paneles deslizantes", "Sliding panels"), text("Opciones, resultados y otros paneles desplegables.", "Options, results and other dropdown panels."),
                m_config.animateDropdowns, [this](bool value) { m_config.animateDropdowns = value; persist(); }),
            kit::makeToggleRow(inner, text("Paneles de pausa", "Blocking panels"), text("Pausa y paneles que bloquean los controles del fondo.", "Pause and panels that block the controls behind them."),
                m_config.animateBlockingLayers, [this](bool value) { m_config.animateBlockingLayers = value; persist(); }),
            kit::makeToggleRow(inner, text("Dialogos", "Dialogs"), text("Entrada y salida de conversaciones con personajes.", "Opening and closing character conversations."),
                m_config.animateDialogs, [this](bool value) { m_config.animateDialogs = value; persist(); }),
        }));
        items.push_back(kit::makeCard(width, text("Donde animar", "Where to animate"), {190, 165, 255}, {
            kit::makeToggleRow(inner, text("Dentro del editor", "Inside the editor"), text("Anima sus ventanas de ajustes y triggers.", "Animates its settings and trigger windows."),
                m_config.animateEditorPanels, [this](bool value) { m_config.animateEditorPanels = value; persist(); }),
            kit::makeToggleRow(inner, text("Durante niveles", "During levels"), text("Anima pausa, resultados y avisos sobre el nivel.", "Animates pause, results and alerts over the level."),
                m_config.animateGameplayPanels, [this](bool value) { m_config.animateGameplayPanels = value; persist(); }),
            kit::makeToggleRow(inner, text("Popups de otros mods", "Other mods' popups"), text("Permite el efecto en ventanas creadas con Geode.", "Allows the effect in windows created with Geode."),
                m_config.animateOtherMods, [this](bool value) { m_config.animateOtherMods = value; persist(); }),
        }));
        items.push_back(kit::makeHint(width, text("Apertura y cierre comparten tus ajustes de movimiento. Los popups propios de Paimon usan Dynamic Popups.",
            "Opening and closing share your motion settings. Paimon's own popups use Dynamic Popups.")));
    }
    m_scroll = kit::makeScrollStack({width, 224.f}, items);
    m_scroll->setPosition({12.f, 38.f});
    m_mainLayer->addChild(m_scroll);
}

}
