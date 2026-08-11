#include "SmoothScrollConfigPopup.hpp"
#include "../../../core/Settings.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
namespace ss = paimon::settings::smoothscroll;
namespace kit = paimon::configkit;
}

namespace paimon::smoothscroll {

SmoothScrollConfigPopup* SmoothScrollConfigPopup::create() {
    auto* ret = new SmoothScrollConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SmoothScrollConfigPopup::init() {
    if (!Popup::init(420.f, 300.f)) return false;
    this->setTitle("Scroll Suave");
    paimon::markDynamicPopup(this);

    rebuild();

    // Boton fijo abajo: restaurar valores por defecto
    auto* resetSpr = ButtonSprite::create("Restaurar", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    resetSpr->setScale(0.55f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
        [this](CCMenuItemSpriteExtra*) {
            (void)Mod::get()->setSavedValue<double>("smooth-scroll-sensitivity", ss::kSensitivityDefault);
            (void)Mod::get()->setSavedValue<double>("smooth-scroll-smoothness", ss::kSmoothnessDefault);
            (void)Mod::get()->setSavedValue<bool>("smooth-scroll-editor-zoom", true);
            (void)Mod::get()->setSavedValue<bool>("smooth-scroll-fix-editor", true);
            (void)Mod::get()->setSavedValue<double>(
                "smooth-scroll-editor-zoom-sensitivity", ss::kEditorZoomSensitivityDefault);
            (void)Mod::get()->setSavedValue<double>(
                "smooth-scroll-editor-zoom-smoothness", ss::kEditorZoomSmoothnessDefault);
            this->rebuild();
        });
    resetBtn->setPosition({m_mainLayer->getContentSize().width / 2.f, 20.f});
    m_buttonMenu->addChild(resetBtn);

    return true;
}

void SmoothScrollConfigPopup::scheduleRebuild() {
    // Diferido al siguiente tick para no destruir el control que disparo
    // el cambio mientras el touch dispatcher lo sigue usando.
    Ref<SmoothScrollConfigPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self && self->getParent()) self->rebuild();
    });
}

void SmoothScrollConfigPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 36.f - 38.f; // titulo arriba, boton abajo
    float innerW = kit::cardInnerWidth(scrollW);

    auto fmtTimes = [](double v) { return fmt::format("x{:.2f}", v); };
    auto fmtPlain = [](double v) { return fmt::format("{:.2f}", v); };

    auto* hero = kit::makeHeroToggle(scrollW,
        "Scroll suave",
        "Desplaza los menus con inercia suave, como en el celular.",
        ss::enabled(),
        [](bool v) { Mod::get()->setSettingValue<bool>("smooth-scroll", v); });

    auto* tabs = kit::makeTabBar(scrollW, {"Basico", "Avanzado"}, m_tab,
        [this](int i) {
            m_tab = i;
            scheduleRebuild();
        });

    // Tarjeta: comportamiento en menus
    auto* menusCard = kit::makeCard(scrollW, "En los menus", {120, 210, 255}, {
        kit::makeSliderRow(innerW,
            "Velocidad",
            "Cuanto avanza cada giro de la rueda.",
            ss::sensitivity(), ss::kSensitivityMin, ss::kSensitivityMax, fmtTimes,
            [](double v) { (void)Mod::get()->setSavedValue<double>("smooth-scroll-sensitivity", v); }),
        kit::makeSliderRow(innerW,
            "Suavidad",
            "Mas alto = frena mas lento y se siente mas fluido.",
            ss::smoothness(), ss::kSmoothnessMin, ss::kSmoothnessMax, fmtPlain,
            [](double v) { (void)Mod::get()->setSavedValue<double>("smooth-scroll-smoothness", v); }),
    });

    // Tarjeta: comportamiento en el editor
    auto* editorCard = kit::makeCard(scrollW, "En el editor", {130, 240, 170}, {
        kit::makeToggleRow(innerW,
            "Zoom suave",
            "Anima el zoom del editor al usar la rueda.",
            ss::editorZoomEnabled(),
            [](bool v) { (void)Mod::get()->setSavedValue<bool>("smooth-scroll-editor-zoom", v); }),
        kit::makeSliderRow(innerW,
            "Velocidad del zoom",
            "Cuanto acerca o aleja cada giro.",
            ss::editorZoomSensitivity(), ss::kEditorZoomSensitivityMin, ss::kEditorZoomSensitivityMax, fmtTimes,
            [](double v) { (void)Mod::get()->setSavedValue<double>("smooth-scroll-editor-zoom-sensitivity", v); }),
        kit::makeSliderRow(innerW,
            "Suavidad del zoom",
            "Que tan gradual se siente el acercamiento.",
            ss::editorZoomSmoothness(), ss::kEditorZoomSmoothnessMin, ss::kEditorZoomSmoothnessMax, fmtPlain,
            [](double v) { (void)Mod::get()->setSavedValue<double>("smooth-scroll-editor-zoom-smoothness", v); }),
        kit::makeToggleRow(innerW,
            "Frenar zoom al soltar Ctrl",
            "Corta la inercia del zoom apenas sueltas Ctrl.",
            ss::fixEditorScroll(),
            [](bool v) { (void)Mod::get()->setSavedValue<bool>("smooth-scroll-fix-editor", v); }),
    });

    std::vector<CCNode*> items = {hero, tabs};
    if (m_tab == 0) {
        items.push_back(menusCard);
        items.push_back(kit::makeHint(scrollW,
            "En Avanzado: zoom suave y ajustes del editor de niveles."));
        editorCard->removeAllChildren();
    } else {
        items.push_back(editorCard);
        menusCard->removeAllChildren();
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 38.f});
    m_mainLayer->addChild(m_scroll);
}

} // namespace paimon::smoothscroll
