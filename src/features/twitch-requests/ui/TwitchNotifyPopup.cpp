#include "TwitchNotifyPopup.hpp"

#include "../TwitchRequestManager.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/ScissorClipNode.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

#include <fmt/format.h>

#include <algorithm>

using namespace cocos2d;
using namespace geode::prelude;

namespace kit = paimon::configkit;

namespace paimon::twitch {

namespace {

// 420 es lo mas ancho que cabe en 4:3, donde la pantalla del juego mide 426.
constexpr float kPopupWidth = 420.f;
constexpr float kPopupHeight = 300.f;
constexpr float kStripHeight = 88.f;
constexpr ccColor3B kAccent = {145, 90, 255};
constexpr ccColor3B kInfoColor = {171, 197, 232};

int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

std::string optionName(std::vector<std::string> const& names, int index) {
    if (index < 0 || index >= static_cast<int>(names.size())) return {};
    return names[static_cast<size_t>(index)];
}

// Fondo de la pantalla de mentira: el mismo degradado del juego, apagado para
// que la tarjeta se lea encima.
CCNode* makeFakeScreen(CCSize size) {
    if (auto* bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setScaleX(size.width / std::max(bg->getContentSize().width, 1.f));
        bg->setScaleY(size.height / std::max(bg->getContentSize().height, 1.f));
        bg->setColor({44, 28, 84});
        return bg;
    }
    auto* flat = paimon::SpriteHelper::createColorPanel(
        size.width, size.height, {28, 20, 56}, 255, 4.f);
    if (flat) flat->setAnchorPoint({0.f, 0.f});
    return flat;
}

CCLabelBMFont* makeInfoLine() {
    auto* label = CCLabelBMFont::create("", "chatFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setColor(kInfoColor);
    label->setScale(0.42f);
    return label;
}

} // namespace

TwitchNotifyPopup* TwitchNotifyPopup::create() {
    auto* ret = new TwitchNotifyPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool TwitchNotifyPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;
    setTitle("Avisos de request");
    paimon::markDynamicPopup(this);

    m_config = notifyConfig();

    auto const content = m_mainLayer->getContentSize();
    float const width = content.width - 24.f;
    float const stripY = content.height - 42.f - kStripHeight;

    buildPreview({12.f, stripY}, {width, kStripHeight});
    buildOptions({12.f, 8.f}, {width, stripY - 14.f});

    rebuildCard(false);
    return true;
}

void TwitchNotifyPopup::buildPreview(CCPoint origin, CCSize size) {
    auto* panel = CCNode::create();
    panel->setContentSize(size);
    panel->setPosition(origin);
    m_mainLayer->addChild(panel);

    if (auto* bg = paimon::SpriteHelper::createColorPanel(
            size.width, size.height, kit::kCardColor, kit::kCardAlpha, 7.f)) {
        bg->setAnchorPoint({0.f, 0.f});
        panel->addChild(bg, -1);
    }

    // La pantalla de mentira guarda la forma de la de verdad, asi que el hueco
    // que ves aqui es el que va a ocupar el aviso en el stream.
    auto const win = CCDirector::get()->getWinSize();
    float const screenHeight = size.height - 12.f;
    float const screenWidth = screenHeight * (win.width / std::max(win.height, 1.f));
    m_ratio = screenWidth / std::max(win.width, 1.f);

    // Recortado, para que la animacion de entrada no se salga del recuadro.
    CCNode* screen = paimon::ScissorClipNode::create(
        paimon::SpriteHelper::createRectStencil(screenWidth, screenHeight));
    if (!screen) screen = CCNode::create();
    screen->setContentSize({screenWidth, screenHeight});
    screen->setPosition({6.f, 6.f});
    panel->addChild(screen);
    m_screen = screen;

    if (auto* fake = makeFakeScreen({screenWidth, screenHeight})) {
        fake->setPosition({0.f, 0.f});
        screen->addChild(fake, -1);
    }

    if (auto* frame = paimon::SpriteHelper::createRoundedRectOutline(
            screenWidth, screenHeight, 3.f, ccc4FFromccc3B({120, 140, 190}), 1.f)) {
        frame->setPosition({6.f, 6.f});
        panel->addChild(frame, 3);
    }

    float const textLeft = screenWidth + 16.f;
    float const textWidth = size.width - textLeft - 10.f;

    auto* caption = CCLabelBMFont::create("Vista previa", "bigFont.fnt");
    caption->setAnchorPoint({0.f, 0.5f});
    caption->setScale(0.44f);
    caption->setPosition({textLeft, size.height - 12.f});
    panel->addChild(caption);

    m_infoWidth = textWidth;

    float y = size.height - 27.f;
    auto addInfo = [&](CCLabelBMFont*& slot) {
        slot = makeInfoLine();
        slot->setPosition({textLeft, y});
        panel->addChild(slot);
        y -= 13.f;
    };
    addInfo(m_spotLabel);
    addInfo(m_sizeLabel);
    addInfo(m_animLabel);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    panel->addChild(menu, 5);

    if (auto* sprite = ButtonSprite::create(
            "Probar en pantalla", "goldFont.fnt", "GJ_button_01.png", 0.8f)) {
        sprite->setScale(std::min(0.58f,
            textWidth / std::max(sprite->getContentSize().width, 1.f)));
        auto* button = CCMenuItemExt::createSpriteExtra(sprite,
            [this](CCMenuItemSpriteExtra*) { this->onTest(); });
        button->setPosition({textLeft + textWidth / 2.f, 15.f});
        menu->addChild(button);
    }
}

void TwitchNotifyPopup::buildOptions(CCPoint origin, CCSize size) {
    float const innerW = kit::cardInnerWidth(size.width);

    std::vector<CCNode*> items;

    items.push_back(kit::makeCard(size.width, "Aviso de request nuevo", kAccent, {
        kit::makeHeroToggle(
            innerW,
            "Avisar en pantalla",
            "Cuando tu chat pide un nivel, estes donde estes.",
            m_config.enabled,
            [this](bool value) {
                this->apply([value](NotifyConfig& config) { config.enabled = value; },
                    false, value);
            }
        ),
        kit::makeToggleRow(
            innerW,
            "Tambien con esta lista abierta", "Aqui la cola ya se ve sola.",
            m_config.overLayer,
            [this](bool value) {
                this->apply([value](NotifyConfig& config) { config.overLayer = value; },
                    false, false);
            }
        ),
    }));

    items.push_back(kit::makeCard(size.width, "Donde sale", {115, 215, 255}, {
        kit::makeSelectRow(
            innerW,
            "Sitio", "Las nueve esquinas y lados de la pantalla.",
            notifySpotNames(), static_cast<int>(m_config.spot),
            [this](int index) {
                this->apply([index](NotifyConfig& config) {
                    config.spot = static_cast<NotifySpot>(
                        std::clamp(index, 0, kNotifySpotCount - 1));
                }, false, true);
            }
        ),
        kit::makeSliderRow(
            innerW,
            "Ajuste horizontal", "Mueve la tarjeta sin cambiar de esquina.",
            m_config.offsetX, -kNotifyMaxOffsetX, kNotifyMaxOffsetX,
            [](double value) { return fmt::format("{:+.0f} px", value); },
            [this](double value) {
                this->apply([value](NotifyConfig& config) {
                    config.offsetX = static_cast<float>(value);
                }, false, false);
            }
        ),
        kit::makeSliderRow(
            innerW,
            "Ajuste vertical", "",
            m_config.offsetY, -kNotifyMaxOffsetY, kNotifyMaxOffsetY,
            [](double value) { return fmt::format("{:+.0f} px", value); },
            [this](double value) {
                this->apply([value](NotifyConfig& config) {
                    config.offsetY = static_cast<float>(value);
                }, false, false);
            }
        ),
    }));

    items.push_back(kit::makeCard(size.width, "Como se ve", {255, 205, 61}, {
        kit::makeSliderRow(
            innerW,
            "Tamano", "Lo grande que sale la tarjeta.",
            m_config.scale, kNotifyMinScale, kNotifyMaxScale,
            [](double value) { return fmt::format("{:.0f}%", value * 100.0); },
            [this](double value) {
                this->apply([value](NotifyConfig& config) {
                    config.scale = static_cast<float>(value);
                }, false, false);
            }
        ),
        kit::makeSliderRow(
            innerW,
            "Segundos en pantalla", "Sin contar la entrada ni la salida.",
            m_config.seconds, kNotifyMinSeconds, kNotifyMaxSeconds,
            [](double value) { return fmt::format("{:.1f} s", value); },
            [this](double value) {
                this->apply([value](NotifyConfig& config) {
                    config.seconds = static_cast<float>(value);
                }, false, false);
            }
        ),
        kit::makeToggleRow(
            innerW,
            "Nombre del nivel", "Si todavia no se conoce, la tarjeta ensena la ID.",
            m_config.showLevel,
            [this](bool value) {
                this->apply([value](NotifyConfig& config) { config.showLevel = value; },
                    true, true);
            }
        ),
        kit::makeToggleRow(
            innerW,
            "Quien lo pidio", "El nick del chat, con el color de su plataforma.",
            m_config.showRequester,
            [this](bool value) {
                this->apply([value](NotifyConfig& config) { config.showRequester = value; },
                    true, true);
            }
        ),
    }));

    items.push_back(kit::makeCard(size.width, "Animacion y sonido", {120, 255, 140}, {
        kit::makeSelectRow(
            innerW,
            "Entrada", "Como aparece la tarjeta.",
            notifyEnterNames(), static_cast<int>(m_config.enter),
            [this](int index) {
                this->apply([index](NotifyConfig& config) {
                    config.enter = static_cast<NotifyEnter>(
                        std::clamp(index, 0, kNotifyEnterCount - 1));
                }, false, true);
            }
        ),
        kit::makeSelectRow(
            innerW,
            "Salida", "Como se va cuando se acaba el tiempo.",
            notifyExitNames(), static_cast<int>(m_config.exit),
            [this](int index) {
                this->apply([index](NotifyConfig& config) {
                    config.exit = static_cast<NotifyExit>(
                        std::clamp(index, 0, kNotifyExitCount - 1));
                }, false, false);
                this->replayExit();
            }
        ),
        kit::makeSelectRow(
            innerW,
            "Sonido", "Suena al aparecer, con el volumen de efectos del juego.",
            notifySoundNames(), static_cast<int>(m_config.sound),
            [this](int index) {
                this->apply([index](NotifyConfig& config) {
                    config.sound = static_cast<NotifySound>(
                        std::clamp(index, 0, kNotifySoundCount - 1));
                }, false, false);
                playNotifySound(m_config);
            }
        ),
    }));

    items.push_back(kit::makeHint(
        size.width,
        "El aviso vive por encima de todo, asi que sale igual en el menu, jugando o en el editor. "
        "El nombre del nivel solo aparece si ya se habia cargado antes; si no, la tarjeta ensena la ID."
    ));

    m_scroll = kit::makeScrollStack(size, items);
    m_scroll->setPosition(origin);
    m_mainLayer->addChild(m_scroll);
}

void TwitchNotifyPopup::apply(
    std::function<void(NotifyConfig&)> const& change,
    bool rebuild,
    bool replay
) {
    change(m_config);
    setNotifyConfig(m_config);
    // setNotifyConfig recorta lo que se pasa de la raya; sigue con lo guardado.
    m_config = notifyConfig();

    if (rebuild) {
        rebuildCard(replay);
    } else {
        syncCard(replay);
    }
}

void TwitchNotifyPopup::rebuildCard(bool replayEnter) {
    if (!m_screen) return;

    if (m_card) {
        m_card->removeFromParent();
        m_card = nullptr;
    }

    m_card = buildNotifyCard(
        m_config,
        TwitchRequestManager::get().selected(),
        "Nivel de ejemplo", 12345, "tu_chat");
    if (m_card) m_screen->addChild(m_card, 2);
    syncCard(replayEnter);
}

CCPoint TwitchNotifyPopup::cardRestPoint() const {
    if (!m_card) return {0.f, 0.f};
    return notifyRestPoint(m_config, m_card->getContentSize(), 0) * m_ratio;
}

void TwitchNotifyPopup::syncCard(bool replayEnter) {
    auto setInfo = [this](CCLabelBMFont* label, std::string text) {
        if (!label) return;
        label->setString(text.c_str());
        label->limitLabelWidth(m_infoWidth, 0.42f, 0.24f);
    };
    setInfo(m_spotLabel, fmt::format("Sitio: {}",
        optionName(notifySpotNames(), static_cast<int>(m_config.spot))));
    setInfo(m_sizeLabel, fmt::format("Tamano {:.0f}%   -   {:.1f} s en pantalla",
        m_config.scale * 100.f, m_config.seconds));
    setInfo(m_animLabel, fmt::format("{}  ->  {}",
        optionName(notifyEnterNames(), static_cast<int>(m_config.enter)),
        optionName(notifyExitNames(), static_cast<int>(m_config.exit))));

    if (!m_card) return;

    m_card->stopAllActions();
    m_card->setScale(m_config.scale * m_ratio);
    m_card->setRotation(0.f);
    m_card->setOpacity(255);
    auto const rest = cardRestPoint();
    m_card->setPosition(rest);

    if (replayEnter) runNotifyEnter(m_card, m_config, rest);
}

// La salida se ensena aqui dentro y la tarjeta vuelve a entrar sola, para no
// llenar la pantalla de avisos de prueba cada vez que tocas la flecha.
void TwitchNotifyPopup::replayExit() {
    if (!m_card) return;

    Ref<TwitchNotifyPopup> self = this;
    runNotifyExit(m_card, m_config, cardRestPoint(), [self] {
        if (!self) return;
        // Fuera del callback: la accion que acaba de terminar sigue viva.
        Loader::get()->queueInMainThread([self] {
            if (self && self->getParent()) self->syncCard(true);
        });
    });
}

void TwitchNotifyPopup::onTest() {
    showNotifyDemo();
}

} // namespace paimon::twitch
