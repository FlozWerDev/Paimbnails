#include "GameplayPerformancePopup.hpp"

#include "../GameplayPerformance.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>

#include <array>

using namespace geode::prelude;

namespace paimon::gameplayperf {

namespace {
constexpr float kPopupWidth = 480.f;
constexpr float kPopupHeight = 330.f;

constexpr std::array kOptions = {
    kNativeModeModuleId,
    kGlowModuleId,
    kBackgroundEffectsModuleId,
    kGameplayEffectsModuleId,
    kPlayerEffectsModuleId,
    kDynamicVolumeModuleId,
    kModVisualsModuleId,
    kAutoPreviewModuleId,
    kTransitionsModuleId,
    kBackgroundModuleId,
    kGroundModuleId,
    kDecorationModuleId,
    kGradientsModuleId,
    kShadersModuleId,
    kParticlesModuleId,
    kLevelEffectsModuleId,
};
constexpr size_t kSafeOptionCount = 9;

bool isSpanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

char const* translated(char const* english, char const* spanish) {
    return isSpanish() ? spanish : english;
}

void addHeader(CCNode* parent, char const* text, CCPoint position, ccColor3B color) {
    auto* label = CCLabelBMFont::create(text, "goldFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setScale(0.48f);
    label->setColor(color);
    label->setPosition(position);
    parent->addChild(label);
}
}

GameplayPerformancePopup* GameplayPerformancePopup::create() {
    auto* ret = new GameplayPerformancePopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool GameplayPerformancePopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;
    paimon::markDynamicPopup(this);
    this->setID("gameplay-performance-popup"_spr);

    if (auto* module = modules::find(kModuleId)) {
        this->setTitle(modules::localizedName(*module), "goldFont.fnt", 0.75f);
    }

    addHeader(
        m_mainLayer,
        translated("Safe optimizations", "Optimizaciones seguras"),
        {24.f, 278.f},
        {120, 255, 150}
    );
    addHeader(
        m_mainLayer,
        translated("Visual cuts", "Recortes visuales"),
        {254.f, 278.f},
        {255, 180, 90}
    );

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setID("performance-options-menu"_spr);
    m_mainLayer->addChild(menu);

    auto addOption = [this, menu](char const* id, int tag, float x, float y) {
        auto* module = modules::find(id);
        if (!module) return;

        auto* off = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        auto* on = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        if (!off || !on) return;
        off->setScale(0.55f);
        on->setScale(0.55f);

        auto* toggle = CCMenuItemToggler::create(
            off, on, this, menu_selector(GameplayPerformancePopup::onToggle)
        );
        toggle->setTag(tag);
        toggle->toggle(modules::isSelfEnabled(*module));
        toggle->setPosition({x, y});
        menu->addChild(toggle);

        auto* label = CCLabelBMFont::create(modules::localizedName(*module), "bigFont.fnt");
        label->setAnchorPoint({0.f, 0.5f});
        label->setScale(0.34f);
        label->limitLabelWidth(184.f, 0.34f, 0.22f);
        label->setPosition({x + 16.f, y});
        m_mainLayer->addChild(label);
    };

    constexpr float kFirstRowY = 248.f;
    constexpr float kRowGap = 25.f;
    for (size_t i = 0; i < kOptions.size(); ++i) {
        bool visualCut = i >= kSafeOptionCount;
        auto row = visualCut ? i - kSafeOptionCount : i;
        addOption(kOptions[i], static_cast<int>(i),
                  visualCut ? 262.f : 32.f, kFirstRowY - kRowGap * row);
    }

    auto* note = CCLabelBMFont::create(
        translated(
            "Changes apply when entering the next level.",
            "Los cambios se aplican al entrar al siguiente nivel."
        ),
        "chatFont.fnt"
    );
    note->setScale(0.5f);
    note->setColor({175, 190, 210});
    note->setPosition({kPopupWidth / 2.f, 24.f});
    m_mainLayer->addChild(note);

    return true;
}

void GameplayPerformancePopup::onToggle(CCObject* sender) {
    auto* toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;

    auto tag = toggle->getTag();
    if (tag < 0 || tag >= static_cast<int>(kOptions.size())) return;

    auto* module = modules::find(kOptions[tag]);
    if (!module) return;
    modules::setEnabled(*module, !modules::isSelfEnabled(*module));
}

} // namespace paimon::gameplayperf
