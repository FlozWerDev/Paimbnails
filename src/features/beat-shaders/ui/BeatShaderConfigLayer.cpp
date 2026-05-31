#include "BeatShaderConfigLayer.hpp"

#include "../services/BeatShaderManager.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::beat_shaders {

constexpr float kPopupW = 460.f;
constexpr float kPopupH = 300.f;

BeatShaderConfigLayer* BeatShaderConfigLayer::create() {
    auto* ret = new BeatShaderConfigLayer();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BeatShaderConfigLayer::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    setTitle("Beat Shaders");

    m_cfg     = BeatShaderManager::get().getConfig();
    m_shaders = BeatShaderManager::get().availableShaders();

    m_layerKeys.clear();
    for (auto const& [key, _] : BeatShaderManager::get().availableLayers()) {
        m_layerKeys.push_back(key);
    }

    m_shaderIdx = 0;
    for (size_t i = 0; i < m_shaders.size(); ++i) {
        if (m_shaders[i].id == m_cfg.shaderName) {
            m_shaderIdx = static_cast<int>(i);
            break;
        }
    }

    buildHeader();
    buildShaderPicker();
    buildSensitivitySliders();
    buildIntensityRow();
    buildLayerToggles();
    buildFooter();

    return true;
}

void BeatShaderConfigLayer::buildHeader() {
    auto* desc = CCLabelBMFont::create(
        "Distorsiona el fondo actual con el ritmo de la musica.",
        "chatFont.fnt");
    desc->setScale(0.55f);
    desc->setOpacity(180);
    m_mainLayer->addChildAtPosition(desc, Anchor::Top, {0, -28});

    m_enabledToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(BeatShaderConfigLayer::onToggleEnabled), 0.7f);
    m_enabledToggle->toggle(m_cfg.enabled);

    auto* enabledLabel = CCLabelBMFont::create("Enabled", "bigFont.fnt");
    enabledLabel->setScale(0.4f);

    m_buttonMenu->addChildAtPosition(m_enabledToggle, Anchor::TopRight, {-30, -25});
    m_mainLayer->addChildAtPosition(enabledLabel, Anchor::TopRight, {-65, -25});
}

void BeatShaderConfigLayer::buildShaderPicker() {
    auto* leftSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    if (leftSpr) leftSpr->setScale(0.6f);
    auto* leftBtn = CCMenuItemSpriteExtra::create(
        leftSpr, this, menu_selector(BeatShaderConfigLayer::onPrevShader));
    leftBtn->setID("beat-shader-prev"_spr);
    m_buttonMenu->addChildAtPosition(leftBtn, Anchor::Top, {-110, -65});

    auto* rightSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    if (rightSpr) {
        rightSpr->setScale(0.6f);
        rightSpr->setFlipX(true);
    }
    auto* rightBtn = CCMenuItemSpriteExtra::create(
        rightSpr, this, menu_selector(BeatShaderConfigLayer::onNextShader));
    rightBtn->setID("beat-shader-next"_spr);
    m_buttonMenu->addChildAtPosition(rightBtn, Anchor::Top, {110, -65});

    m_shaderNameLabel = CCLabelBMFont::create("Glitch", "bigFont.fnt");
    m_shaderNameLabel->setScale(0.55f);
    m_mainLayer->addChildAtPosition(m_shaderNameLabel, Anchor::Top, {0, -60});

    m_shaderDescLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_shaderDescLabel->setScale(0.42f);
    m_shaderDescLabel->setOpacity(170);
    m_mainLayer->addChildAtPosition(m_shaderDescLabel, Anchor::Top, {0, -82});

    refreshShaderLabel();
}

void BeatShaderConfigLayer::buildSensitivitySliders() {
    auto makeSlider = [&](char const* label, SEL_MenuHandler cb,
                           float xOffset, float yOffset, float currentVal) -> Slider*
    {
        auto* lbl = CCLabelBMFont::create(label, "bigFont.fnt");
        lbl->setScale(0.42f);
        m_mainLayer->addChildAtPosition(lbl, Anchor::Center, {xOffset, yOffset + 18});

        auto* slider = Slider::create(this, cb, 0.55f);
        slider->setValue(std::clamp(currentVal / 3.0f, 0.f, 1.f));
        m_mainLayer->addChildAtPosition(slider, Anchor::Center, {xOffset, yOffset});
        return slider;
    };

    m_bassSlider   = makeSlider("Bass",   menu_selector(BeatShaderConfigLayer::onBassChanged),   -150, 10, m_cfg.bassMult);
    m_midSlider    = makeSlider("Mid",    menu_selector(BeatShaderConfigLayer::onMidChanged),     -50, 10, m_cfg.midMult);
    m_trebleSlider = makeSlider("Treble", menu_selector(BeatShaderConfigLayer::onTrebleChanged),   50, 10, m_cfg.trebleMult);
    m_beatSlider   = makeSlider("Beat",   menu_selector(BeatShaderConfigLayer::onBeatChanged),    150, 10, m_cfg.beatMult);
}

void BeatShaderConfigLayer::buildIntensityRow() {
    auto* intLbl = CCLabelBMFont::create("Intensity", "bigFont.fnt");
    intLbl->setScale(0.45f);
    m_mainLayer->addChildAtPosition(intLbl, Anchor::Center, {0, -25});

    m_intensitySlider = Slider::create(this, menu_selector(BeatShaderConfigLayer::onIntensityChanged), 0.6f);
    m_intensitySlider->setValue(std::clamp(m_cfg.intensity, 0.f, 1.f));
    m_mainLayer->addChildAtPosition(m_intensitySlider, Anchor::Center, {0, -45});
}

void BeatShaderConfigLayer::buildLayerToggles() {
    auto layers = BeatShaderManager::get().availableLayers();
    if (layers.empty()) return;

    auto* sectionLbl = CCLabelBMFont::create("Active Layers", "bigFont.fnt");
    sectionLbl->setScale(0.4f);
    sectionLbl->setOpacity(200);
    m_mainLayer->addChildAtPosition(sectionLbl, Anchor::Bottom, {0, 88});

    auto* row = CCMenu::create();
    row->setContentSize({kPopupW - 50.f, 50.f});
    row->setLayout(
        RowLayout::create()
            ->setGap(8.f)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setGrowCrossAxis(true)
    );
    m_mainLayer->addChildAtPosition(row, Anchor::Bottom, {0, 60});

    m_layerToggles.clear();
    for (size_t i = 0; i < layers.size(); ++i) {
        auto const& [key, name] = layers[i];

        auto* itemNode = CCNode::create();
        itemNode->setContentSize({54.f, 46.f});

        auto* toggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(BeatShaderConfigLayer::onToggleLayer), 0.45f);
        toggle->toggle(BeatShaderManager::get().isLayerEnabled(key));
        toggle->setTag(static_cast<int>(i));
        itemNode->addChildAtPosition(toggle, Anchor::Top, {0, -5});

        auto* lbl = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        lbl->setScale(0.35f);
        itemNode->addChildAtPosition(lbl, Anchor::Bottom, {0, 5});

        row->addChild(itemNode);
        m_layerToggles.push_back(toggle);
    }
    row->updateLayout();
}

void BeatShaderConfigLayer::buildFooter() {
    auto* resetSpr = ButtonSprite::create("Default", "goldFont.fnt", "GJ_button_04.png", .65f);
    if (resetSpr) resetSpr->setScale(0.6f);
    auto* resetBtn = CCMenuItemSpriteExtra::create(
        resetSpr, this, menu_selector(BeatShaderConfigLayer::onResetDefaults));
    resetBtn->setID("beat-shader-default-btn"_spr);
    m_buttonMenu->addChildAtPosition(resetBtn, Anchor::Bottom, {0, 22});
}

void BeatShaderConfigLayer::refreshShaderLabel() {
    if (m_shaders.empty()) return;
    if (m_shaderIdx < 0) m_shaderIdx = 0;
    if (m_shaderIdx >= static_cast<int>(m_shaders.size())) {
        m_shaderIdx = static_cast<int>(m_shaders.size()) - 1;
    }
    auto const& entry = m_shaders[m_shaderIdx];
    if (m_shaderNameLabel) m_shaderNameLabel->setString(entry.label.c_str());
    if (m_shaderDescLabel) m_shaderDescLabel->setString(entry.description.c_str());
    m_cfg.shaderName = entry.id;
}

void BeatShaderConfigLayer::persistAndRefresh(bool shaderChanged) {
    BeatShaderManager::get().saveConfig(m_cfg);
    // The slider/toggle callbacks below run synchronously inside the
    // CCTouchDispatcher's iteration loop. Mutating the scene graph
    // (rebuildBackgrounds removes/re-adds full-screen sprites and
    // VideoUpdate nodes) while the dispatcher is still iterating its
    // handler list leaves dangling pointers and causes a crash inside
    // ~CCTargetedTouchHandler on the next handleTouchesEnd. Defer the
    // expensive scene mutation to the next main-thread tick so the touch
    // dispatch can finish first.
    if (shaderChanged) {
        Loader::get()->queueInMainThread([] {
            BeatShaderManager::get().rebuildBackgrounds();
        });
    } else {
        // Just uniforms — push them onto live sprites without rebuilding.
        Loader::get()->queueInMainThread([] {
            BeatShaderManager::get().refreshLiveSpriteUniforms();
        });
    }
}

void BeatShaderConfigLayer::onToggleEnabled(CCObject*) {
    if (!m_enabledToggle) return;
    m_cfg.enabled = !m_enabledToggle->isToggled();
    persistAndRefresh(true);
    PaimonNotify::create(
        m_cfg.enabled ? "Beat Shaders activado" : "Beat Shaders desactivado",
        m_cfg.enabled ? NotificationIcon::Success : NotificationIcon::None
    )->show();
}

void BeatShaderConfigLayer::onPrevShader(CCObject*) {
    if (m_shaders.empty()) return;
    --m_shaderIdx;
    if (m_shaderIdx < 0) m_shaderIdx = static_cast<int>(m_shaders.size()) - 1;
    refreshShaderLabel();
    persistAndRefresh(true);
}

void BeatShaderConfigLayer::onNextShader(CCObject*) {
    if (m_shaders.empty()) return;
    ++m_shaderIdx;
    if (m_shaderIdx >= static_cast<int>(m_shaders.size())) m_shaderIdx = 0;
    refreshShaderLabel();
    persistAndRefresh(true);
}

void BeatShaderConfigLayer::onIntensityChanged(CCObject* sender) {
    auto* thumb = static_cast<SliderThumb*>(sender);
    m_cfg.intensity = thumb->getValue();
    persistAndRefresh(false);
}

void BeatShaderConfigLayer::onBassChanged(CCObject* sender) {
    auto* thumb = static_cast<SliderThumb*>(sender);
    m_cfg.bassMult = thumb->getValue() * 3.0f;
    persistAndRefresh(false);
}

void BeatShaderConfigLayer::onMidChanged(CCObject* sender) {
    auto* thumb = static_cast<SliderThumb*>(sender);
    m_cfg.midMult = thumb->getValue() * 3.0f;
    persistAndRefresh(false);
}

void BeatShaderConfigLayer::onTrebleChanged(CCObject* sender) {
    auto* thumb = static_cast<SliderThumb*>(sender);
    m_cfg.trebleMult = thumb->getValue() * 3.0f;
    persistAndRefresh(false);
}

void BeatShaderConfigLayer::onBeatChanged(CCObject* sender) {
    auto* thumb = static_cast<SliderThumb*>(sender);
    m_cfg.beatMult = thumb->getValue() * 3.0f;
    persistAndRefresh(false);
}

void BeatShaderConfigLayer::onToggleLayer(CCObject* sender) {
    auto* tog = static_cast<CCMenuItemToggler*>(sender);
    int idx = tog->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_layerKeys.size())) return;
    bool newState = !tog->isToggled();
    BeatShaderManager::get().setLayerEnabled(m_layerKeys[idx], newState);
    // Defer scene mutation until the touch dispatcher finishes — see
    // persistAndRefresh() above for the full rationale.
    Loader::get()->queueInMainThread([] {
        BeatShaderManager::get().rebuildBackgrounds();
    });
}

void BeatShaderConfigLayer::onResetDefaults(CCObject*) {
    m_cfg = BeatShaderConfig{};
    m_shaderIdx = 0;
    BeatShaderManager::get().saveConfig(m_cfg);

    for (auto const& key : m_layerKeys) {
        BeatShaderManager::get().setLayerEnabled(key, true);
    }

    if (m_enabledToggle)   m_enabledToggle->toggle(m_cfg.enabled);
    if (m_intensitySlider) m_intensitySlider->setValue(m_cfg.intensity);
    if (m_bassSlider)      m_bassSlider->setValue(m_cfg.bassMult / 3.0f);
    if (m_midSlider)       m_midSlider->setValue(m_cfg.midMult / 3.0f);
    if (m_trebleSlider)    m_trebleSlider->setValue(m_cfg.trebleMult / 3.0f);
    if (m_beatSlider)      m_beatSlider->setValue(m_cfg.beatMult / 3.0f);
    for (auto* tog : m_layerToggles) if (tog) tog->toggle(true);

    refreshShaderLabel();
    // Defer scene mutation until the touch dispatcher finishes — see
    // persistAndRefresh() above for the full rationale.
    Loader::get()->queueInMainThread([] {
        BeatShaderManager::get().rebuildBackgrounds();
    });

    PaimonNotify::create("Beat Shaders restablecido", NotificationIcon::Success)->show();
}

} // namespace paimon::beat_shaders
