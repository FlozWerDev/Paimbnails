#include "FrameInterpPopup.hpp"

#include "../services/FrameInterpolator.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
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

char const* tr(char const* english, char const* spanish) {
    return Localization::get().getLanguage() == Localization::Language::SPANISH
        ? spanish : english;
}

std::string pct(double v)   { return fmt::format("{}%", static_cast<int>(std::lround(v * 100.0))); }
std::string count(double v) { return fmt::format("{}", static_cast<int>(std::lround(v))); }
}

namespace paimon::frameinterp {

FrameInterpPopup* FrameInterpPopup::create() {
    auto* ret = new FrameInterpPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool FrameInterpPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    this->setID("frame-interp-popup"_spr);
    this->setTitle(tr("Frame Interpolation", "Interpolacion de Fotogramas"));
    this->addInfoButton(tr("Frame Interpolation", "Interpolacion de Fotogramas"),
        tr("Draws the game between physics ticks for smooth motion at any refresh rate. "
           "<cy>Smooth</c> adds one tick of delay and never guesses; <cy>Instant</c> has no "
           "delay but can overshoot on bounces.",
           "Dibuja el juego entre pasos de fisica para un movimiento fluido a cualquier tasa. "
           "<cy>Suave</c> anade un paso de retraso y nunca adivina; <cy>Sin retardo</c> no tiene "
           "retraso pero puede pasarse en los rebotes."));

    FrameInterpolator::get().init();
    rebuild();

    auto* resetSpr = ButtonSprite::create(tr("Reset", "Restaurar"), "goldFont.fnt",
                                          "GJ_button_06.png", 0.7f);
    if (resetSpr) resetSpr->setScale(0.55f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
        [this](CCMenuItemSpriteExtra*) {
            FrameInterpolator::get().resetToDefaults();
            m_dirty = false;
            scheduleRebuild();
            PaimonNotify::create(
                tr("Frame interpolation reset", "Interpolacion restablecida"),
                NotificationIcon::Success)->show();
        });
    resetBtn->setID("frame-interp-default-btn"_spr);
    resetBtn->setPosition({m_mainLayer->getContentSize().width - 62.f, 20.f});
    m_buttonMenu->addChild(resetBtn);

    m_statsLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statsLabel->setID("frame-interp-stats"_spr);
    m_statsLabel->setAnchorPoint({0.f, 0.5f});
    m_statsLabel->setScale(0.42f);
    m_statsLabel->setColor(kit::kDescColor);
    m_statsLabel->setPosition({14.f, 20.f});
    m_mainLayer->addChild(m_statsLabel);

    schedule(schedule_selector(FrameInterpPopup::refreshStats), 0.25f);
    schedule(schedule_selector(FrameInterpPopup::flush), 1.f);

    return true;
}

void FrameInterpPopup::onClose(CCObject* sender) {
    if (m_dirty) {
        FrameInterpolator::get().saveConfig();
        m_dirty = false;
    }
    Popup::onClose(sender);
}

void FrameInterpPopup::touched() {
    m_dirty = true;
}

void FrameInterpPopup::flush(float) {
    if (!m_dirty) return;
    FrameInterpolator::get().saveConfig();
    m_dirty = false;
}

void FrameInterpPopup::refreshStats(float) {
    if (!m_statsLabel) return;

    auto& interp = FrameInterpolator::get();
    if (!interp.config().enabled) {
        m_statsLabel->setString(tr("Off", "Apagado"));
        return;
    }
    if (!interp.isActive()) {
        m_statsLabel->setString(tr("Waiting (out of scope)", "En espera (fuera de ambito)"));
        return;
    }

    float const alpha = interp.lastAlpha();
    float const ticks = interp.stepsPerFrame();
    int const nodes = interp.trackedCount();
    auto const line = Localization::get().getLanguage() == Localization::Language::SPANISH
        ? fmt::format("mezcla {:.2f}  |  {:.1f} pasos/frame  |  {} nodos", alpha, ticks, nodes)
        : fmt::format("mix {:.2f}  |  {:.1f} ticks/frame  |  {} nodes", alpha, ticks, nodes);
    m_statsLabel->setString(line.c_str());
}

void FrameInterpPopup::scheduleRebuild() {
    Ref<FrameInterpPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (self && self->getParent()) self->rebuild();
    });
}

void FrameInterpPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    auto& cfg = FrameInterpolator::get().config();

    auto content = m_mainLayer->getContentSize();
    float const scrollW = content.width - 24.f;
    float const scrollH = content.height - 36.f - 38.f;
    float const innerW = kit::cardInnerWidth(scrollW);

    auto* hero = kit::makeHeroToggle(scrollW,
        tr("Frame Interpolation", "Interpolacion de Fotogramas"),
        tr("Draws the game between physics ticks so movement stays smooth at any refresh rate.",
           "Dibuja el juego entre pasos de fisica para que el movimiento sea fluido a cualquier "
           "tasa de refresco."),
        cfg.enabled,
        [this](bool v) {
            FrameInterpolator::get().setEnabled(v);
            scheduleRebuild();
        });

    std::vector<CCNode*> items = {hero};

    items.push_back(kit::makeCard(scrollW,
        tr("What gets interpolated", "Que se interpola"), {140, 200, 255}, {
        kit::makeToggleRow(innerW,
            tr("Camera", "Camara"),
            tr("Position, zoom and rotation of the level.",
               "Posicion, zoom y rotacion del nivel."),
            cfg.camera,
            [this](bool v) { FrameInterpolator::get().config().camera = v; touched(); }),
        kit::makeToggleRow(innerW,
            tr("Background and ground", "Fondo y suelo"),
            tr("Background, middleground and both ground layers.",
               "Fondo, plano medio y las dos capas de suelo."),
            cfg.scenery,
            [this](bool v) { FrameInterpolator::get().config().scenery = v; touched(); }),
        kit::makeToggleRow(innerW,
            tr("Players", "Jugadores"),
            tr("Icon position, rotation and scale. Trails still follow the tick.",
               "Posicion, rotacion y escala del icono. Las estelas siguen yendo por paso."),
            cfg.players,
            [this](bool v) { FrameInterpolator::get().config().players = v; touched(); }),
        kit::makeToggleRow(innerW,
            tr("Moving objects", "Objetos en movimiento"),
            tr("Experimental: also smooths platforms driven by move triggers.",
               "Experimental: suaviza tambien las plataformas que mueven los triggers."),
            cfg.movingObjects,
            [this](bool v) { FrameInterpolator::get().config().movingObjects = v; touched(); }),
        kit::makeSliderRow(innerW,
            tr("Object budget", "Limite de objetos"),
            tr("How many moving objects are smoothed at once.",
               "Cuantos objetos en movimiento se suavizan a la vez."),
            cfg.objectLimit, 0.0, 2000.0, count,
            [this](double v) {
                FrameInterpolator::get().config().objectLimit = static_cast<int>(std::lround(v));
                touched();
            }),
    }));

    items.push_back(kit::makeCard(scrollW, tr("Tuning", "Ajuste"), {255, 210, 120}, {
        kit::makeSelectRow(innerW,
            tr("Latency", "Latencia"),
            tr("How far behind the simulation the picture is allowed to be.",
               "Cuanto puede ir la imagen por detras de la simulacion."),
            {tr("Smooth", "Suave"), tr("Balanced", "Equilibrado"), tr("Instant", "Sin retardo")},
            std::clamp(cfg.latency, 0, 2),
            [this](int idx) {
                FrameInterpolator::get().config().latency = idx;
                touched();
            }),
        kit::makeSliderRow(innerW,
            tr("Strength", "Fuerza"),
            tr("0% draws exactly what the game would; 100% is full interpolation.",
               "0% dibuja lo mismo que el juego; 100% es interpolacion completa."),
            cfg.strength, 0.0, 1.0, pct,
            [this](double v) {
                FrameInterpolator::get().config().strength = static_cast<float>(v);
                touched();
            }),
    }));

    items.push_back(kit::makeCard(scrollW, tr("Where", "Donde se aplica"), {130, 240, 170}, {
        kit::makeToggleRow(innerW, tr("In levels", "En el nivel"), nullptr, cfg.inGameplay,
            [this](bool v) { FrameInterpolator::get().config().inGameplay = v; touched(); }),
        kit::makeToggleRow(innerW, tr("In the editor", "En el editor"), nullptr, cfg.inEditor,
            [this](bool v) { FrameInterpolator::get().config().inEditor = v; touched(); }),
    }));

    items.push_back(kit::makeHint(scrollW,
        tr("Smooth adds one physics tick of delay (about 4 ms) and never guesses. "
           "Instant draws the present by extending the last tick, which can overshoot "
           "on bounces.",
           "Suave anade un paso de fisica de retraso (unos 4 ms) y nunca adivina. "
           "Sin retardo dibuja el presente prolongando el ultimo paso, y puede pasarse "
           "en los rebotes.")));

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setID("frame-interp-scroll"_spr);
    m_scroll->setPosition({12.f, 38.f});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::frameinterp
