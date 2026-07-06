#include "BeatShaderConfigLayer.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/BeatShaderManager.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <fmt/format.h>

#include <algorithm>

using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;
}

namespace paimon::beat_shaders {

constexpr float kPopupW = 440.f;
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
    paimon::markDynamicPopup(this);

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

    rebuild();

    // Boton fijo abajo: restaurar valores por defecto
    auto* resetSpr = ButtonSprite::create("Restaurar", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    if (resetSpr) resetSpr->setScale(0.55f);
    auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
        [this](CCMenuItemSpriteExtra*) {
            m_cfg = BeatShaderConfig{};
            m_shaderIdx = 0;
            BeatShaderManager::get().saveConfig(m_cfg);
            for (auto const& key : m_layerKeys) {
                BeatShaderManager::get().setLayerEnabled(key, true);
            }
            // Reconstruimos la UI y el fondo fuera del touch dispatcher (ver
            // persistAndRefresh para la explicacion del crash).
            Ref<BeatShaderConfigLayer> self = this;
            Loader::get()->queueInMainThread([self] {
                if (paimon::isRuntimeShuttingDown()) return;
                BeatShaderManager::get().rebuildBackgrounds();
                if (self && self->getParent()) self->rebuild();
            });
            PaimonNotify::create("Beat Shaders restablecido", NotificationIcon::Success)->show();
        });
    resetBtn->setID("beat-shader-default-btn"_spr);
    resetBtn->setPosition({m_mainLayer->getContentSize().width / 2.f, 20.f});
    m_buttonMenu->addChild(resetBtn);

    return true;
}

void BeatShaderConfigLayer::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
        m_shaderDescLabel = nullptr;
    }

    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 36.f - 38.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto fmtPercent = [](double v) { return fmt::format("{}%", static_cast<int>(v * 100.0)); };
    auto fmtTimes   = [](double v) { return fmt::format("x{:.1f}", v); };

    // Interruptor principal
    auto* hero = kit::makeHeroToggle(scrollW,
        "Fondos al ritmo",
        "El fondo del menu se mueve siguiendo la musica.",
        m_cfg.enabled,
        [this](bool v) {
            m_cfg.enabled = v;
            persistAndRefresh(true);
        });

    // Tarjeta: estilo del efecto
    std::vector<std::string> shaderNames;
    shaderNames.reserve(m_shaders.size());
    for (auto const& s : m_shaders) shaderNames.push_back(s.label);

    auto* styleRow = kit::makeSelectRow(innerW,
        "Estilo", nullptr,
        shaderNames, m_shaderIdx,
        [this](int idx) {
            if (idx < 0 || idx >= static_cast<int>(m_shaders.size())) return;
            m_shaderIdx = idx;
            m_cfg.shaderName = m_shaders[static_cast<size_t>(idx)].id;
            if (m_shaderDescLabel) {
                m_shaderDescLabel->setString(
                    m_shaders[static_cast<size_t>(idx)].description.c_str());
            }
            persistAndRefresh(true);
        });

    // Descripcion del estilo actual (se actualiza al cambiar de estilo).
    // Nodo propio con altura fija para dos lineas.
    auto* descRow = CCNode::create();
    descRow->setAnchorPoint({0.f, 0.f});
    descRow->setContentSize({innerW, 22.f});
    {
        std::string initial = (m_shaderIdx >= 0 && m_shaderIdx < static_cast<int>(m_shaders.size()))
            ? m_shaders[static_cast<size_t>(m_shaderIdx)].description : "";
        auto* lbl = CCLabelBMFont::create(initial.c_str(), "chatFont.fnt",
            (innerW - 24.f) / 0.46f, kCCTextAlignmentLeft);
        lbl->setScale(0.46f);
        lbl->setAnchorPoint({0.f, 1.f});
        lbl->setColor(kit::kDescColor);
        lbl->setPosition({12.f, 20.f});
        descRow->addChild(lbl);
        m_shaderDescLabel = lbl;
    }

    auto* styleCard = kit::makeCard(scrollW, "Estilo del efecto", {255, 140, 220},
                                    {styleRow, descRow});

    // Tarjeta: reaccion al audio
    auto* audioCard = kit::makeCard(scrollW, "Reaccion a la musica", {120, 210, 255}, {
        kit::makeSliderRow(innerW,
            "Intensidad", "Fuerza general del efecto.",
            m_cfg.intensity, 0.0, 1.0, fmtPercent,
            [this](double v) { m_cfg.intensity = static_cast<float>(v); persistAndRefresh(false); }),
        kit::makeSliderRow(innerW,
            "Graves", "Reaccion a los bajos y al bombo.",
            m_cfg.bassMult, 0.0, 3.0, fmtTimes,
            [this](double v) { m_cfg.bassMult = static_cast<float>(v); persistAndRefresh(false); }),
        kit::makeSliderRow(innerW,
            "Medios", "Reaccion a voces e instrumentos.",
            m_cfg.midMult, 0.0, 3.0, fmtTimes,
            [this](double v) { m_cfg.midMult = static_cast<float>(v); persistAndRefresh(false); }),
        kit::makeSliderRow(innerW,
            "Agudos", "Reaccion a platillos y detalles.",
            m_cfg.trebleMult, 0.0, 3.0, fmtTimes,
            [this](double v) { m_cfg.trebleMult = static_cast<float>(v); persistAndRefresh(false); }),
        kit::makeSliderRow(innerW,
            "Golpe del beat", "Impulso extra en cada golpe del ritmo.",
            m_cfg.beatMult, 0.0, 3.0, fmtTimes,
            [this](double v) { m_cfg.beatMult = static_cast<float>(v); persistAndRefresh(false); }),
    });

    // Tarjeta: pantallas donde se aplica
    std::vector<CCNode*> layerRows;
    auto layers = BeatShaderManager::get().availableLayers();
    for (size_t i = 0; i < layers.size(); ++i) {
        auto const& [key, name] = layers[i];
        std::string keyCopy = key;
        layerRows.push_back(kit::makeToggleRow(innerW,
            name.c_str(), nullptr,
            BeatShaderManager::get().isLayerEnabled(key),
            [keyCopy](bool v) {
                BeatShaderManager::get().setLayerEnabled(keyCopy, v);
                Loader::get()->queueInMainThread([] {
                    if (paimon::isRuntimeShuttingDown()) return;
                    BeatShaderManager::get().rebuildBackgrounds();
                });
            }));
    }

    std::vector<CCNode*> items = {hero, styleCard, audioCard};
    if (!layerRows.empty()) {
        items.push_back(kit::makeCard(scrollW, "Donde se aplica", {130, 240, 170}, layerRows));
    }

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 38.f});
    m_mainLayer->addChild(m_scroll);
}

void BeatShaderConfigLayer::persistAndRefresh(bool shaderChanged) {
    BeatShaderManager::get().saveConfig(m_cfg);
    // Defer scene mutation to the next tick: these callbacks run inside the touch
    // dispatcher's loop, and mutating the scene graph mid-iteration crashes
    // ~CCTargetedTouchHandler on the next handleTouchesEnd.
    if (shaderChanged) {
        Loader::get()->queueInMainThread([] {
            if (paimon::isRuntimeShuttingDown()) return;
            BeatShaderManager::get().rebuildBackgrounds();
        });
    } else {
        // Just uniforms — push them onto live sprites without rebuilding.
        Loader::get()->queueInMainThread([] {
            if (paimon::isRuntimeShuttingDown()) return;
            BeatShaderManager::get().refreshLiveSpriteUniforms();
        });
    }
}

} // namespace paimon::beat_shaders
