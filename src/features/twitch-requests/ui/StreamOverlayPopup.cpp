#include "StreamOverlayPopup.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/ui/ScrollLayer.hpp>

#include <cmath>

using namespace geode::prelude;

namespace kit = paimon::configkit;

namespace paimon::twitch {

namespace {

constexpr char const* kModuleID = "paimbnails.streamoverlay.menu";
constexpr float kPopupWidth = 420.f;
constexpr float kPopupHeight = 300.f;
constexpr ccColor3B kAccent = {166, 112, 255};

} // namespace

StreamOverlayPopup* StreamOverlayPopup::create() {
    auto* ret = new StreamOverlayPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool StreamOverlayPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;
    setTitle("Overlay para OBS");
    paimon::markDynamicPopup(this);
    m_config = streamOverlayConfig();

    auto const content = m_mainLayer->getContentSize();
    float const width = content.width - 24.f;

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt", width / .42f,
        kCCTextAlignmentCenter);
    m_statusLabel->setScale(.42f);
    m_statusLabel->setPosition({content.width / 2.f, content.height - 45.f});
    m_mainLayer->addChild(m_statusLabel);

    float const scrollHeight = content.height - 68.f;
    float const inner = kit::cardInnerWidth(width);
    std::vector<CCNode*> items;

    items.push_back(kit::makeCard(width, "Servidor local", kAccent, {
        kit::makeHeroToggle(
            inner,
            "Overlay para OBS",
            "Sirve una Browser Source privada en tu propia PC.",
            paimon::modules::isEnabled(kModuleID),
            [this](bool enabled) { this->setEnabled(enabled); }
        ),
        kit::makeButtonRow(
            inner,
            "URL limpia para OBS",
            "Fondo transparente; pegala como Browser Source.",
            "Copiar",
            [this] { this->copyOverlayUrl(); }
        ),
        kit::makeButtonRow(
            inner,
            "Vista previa animada",
            "Abre una demo en el navegador aunque la cola este vacia.",
            "Abrir",
            [this] { this->openPreview(); }
        ),
    }));

    items.push_back(kit::makeCard(width, "Contenido", {105, 210, 255}, {
        kit::makeSliderRow(
            inner,
            "Proximos niveles", "Cuantos pedidos aparecen debajo del actual.",
            m_config.nextCount, 1.0, 8.0,
            [](double value) { return fmt::format("{:.0f}", value); },
            [this](double value) {
                this->apply([value](StreamOverlayConfig& config) {
                    config.nextCount = static_cast<int>(std::round(value));
                });
            }
        ),
        kit::makeToggleRow(
            inner, "Creador", "Muestra quien creo cada nivel.",
            m_config.showAuthor,
            [this](bool value) {
                this->apply([value](StreamOverlayConfig& config) { config.showAuthor = value; });
            }
        ),
        kit::makeToggleRow(
            inner, "Solicitante", "Muestra quien lo pidio en el chat o la web.",
            m_config.showRequester,
            [this](bool value) {
                this->apply([value](StreamOverlayConfig& config) {
                    config.showRequester = value;
                });
            }
        ),
        kit::makeToggleRow(
            inner, "ID del nivel", "Util para que el publico pueda buscarlo.",
            m_config.showLevelID,
            [this](bool value) {
                this->apply([value](StreamOverlayConfig& config) { config.showLevelID = value; });
            }
        ),
        kit::makeToggleRow(
            inner, "Progreso en vivo", "Barra y porcentaje del nivel que estas jugando.",
            m_config.showProgress,
            [this](bool value) {
                this->apply([value](StreamOverlayConfig& config) { config.showProgress = value; });
            }
        ),
        kit::makeToggleRow(
            inner, "Total de la cola", "Contador de requests pendientes en la cabecera.",
            m_config.showQueueCount,
            [this](bool value) {
                this->apply([value](StreamOverlayConfig& config) {
                    config.showQueueCount = value;
                });
            }
        ),
    }));

    items.push_back(kit::makeCard(width, "Composicion y movimiento", {120, 255, 155}, {
        kit::makeSelectRow(
            inner,
            "Diseno", "Tarjetas, compacto o cinta inferior.",
            streamOverlayLayoutNames(), static_cast<int>(m_config.layout),
            [this](int index) {
                this->apply([index](StreamOverlayConfig& config) {
                    config.layout = static_cast<StreamOverlayLayout>(index);
                });
            }
        ),
        kit::makeSelectRow(
            inner,
            "Animacion", "Como entra el nivel actual y se renueva la cola.",
            streamOverlayAnimationNames(), static_cast<int>(m_config.animation),
            [this](int index) {
                this->apply([index](StreamOverlayConfig& config) {
                    config.animation = static_cast<StreamOverlayAnimation>(index);
                });
            }
        ),
        kit::makeSliderRow(
            inner,
            "Escala", "Tamano general dentro de la Browser Source.",
            m_config.scale, .7, 1.6,
            [](double value) { return fmt::format("{:.0f}%", value * 100.0); },
            [this](double value) {
                this->apply([value](StreamOverlayConfig& config) {
                    config.scale = static_cast<float>(value);
                });
            }
        ),
        kit::makeSliderRow(
            inner,
            "Opacidad del cristal", "El resto del overlay sigue transparente.",
            m_config.opacity, .15, 1.0,
            [](double value) { return fmt::format("{:.0f}%", value * 100.0); },
            [this](double value) {
                this->apply([value](StreamOverlayConfig& config) {
                    config.opacity = static_cast<float>(value);
                });
            }
        ),
        kit::makeSliderRow(
            inner,
            "Bordes redondeados", "Desde recto hasta una tarjeta muy suave.",
            m_config.roundness, 0.0, 34.0,
            [](double value) { return fmt::format("{:.0f} px", value); },
            [this](double value) {
                this->apply([value](StreamOverlayConfig& config) {
                    config.roundness = static_cast<float>(value);
                });
            }
        ),
    }));

    items.push_back(kit::makeCard(width, "Paleta", {255, 205, 61}, {
        kit::makeColorRow(
            inner, "Color principal", "Luces, progreso y detalles activos.",
            m_config.accent,
            [this](ccColor3B color) {
                this->apply([color](StreamOverlayConfig& config) { config.accent = color; });
            }
        ),
        kit::makeColorRow(
            inner, "Cristal", "Tono base de las tarjetas.",
            m_config.background,
            [this](ccColor3B color) {
                this->apply([color](StreamOverlayConfig& config) { config.background = color; });
            }
        ),
        kit::makeColorRow(
            inner, "Texto", "Color principal de nombres y datos.",
            m_config.text,
            [this](ccColor3B color) {
                this->apply([color](StreamOverlayConfig& config) { config.text = color; });
            }
        ),
    }));

    items.push_back(kit::makeHint(
        width,
        "En OBS crea una Fuente de navegador de 1920x1080, pega el link limpio y deja "
        "activado 'Actualizar el navegador cuando la escena se active'. El servidor solo "
        "escucha en localhost: nadie fuera de tu PC puede abrirlo."
    ));

    m_scroll = kit::makeScrollStack({width, scrollHeight}, items);
    m_scroll->setPosition({12.f, 8.f});
    m_mainLayer->addChild(m_scroll);

    scheduleUpdate();
    refreshStatus();
    return true;
}

void StreamOverlayPopup::apply(std::function<void(StreamOverlayConfig&)> const& change) {
    change(m_config);
    setStreamOverlayConfig(m_config);
}

void StreamOverlayPopup::setEnabled(bool enabled) {
    paimon::modules::setEnabled(kModuleID, enabled);
    StreamOverlayServer::get().restart();
    refreshStatus();
}

void StreamOverlayPopup::copyOverlayUrl() {
    auto const url = StreamOverlayServer::get().overlayUrl();
    geode::utils::clipboard::write(url);
    PaimonNotify::create("Link de OBS copiado", NotificationIcon::Success)->show();
}

void StreamOverlayPopup::openPreview() {
    auto& server = StreamOverlayServer::get();
    if (!server.isRunning()) {
        PaimonNotify::create(
            "Enciende el overlay antes de abrir la vista previa",
            NotificationIcon::Warning)->show();
        return;
    }
    geode::utils::web::openLinkInBrowser(server.previewUrl());
}

void StreamOverlayPopup::refreshStatus() {
    auto& server = StreamOverlayServer::get();
    std::string text = paimon::modules::isEnabled(kModuleID)
        ? server.statusText() + "  -  " + server.overlayUrl()
        : "Apagado  -  activalo para usar el link local";
    if (text == m_lastStatus || !m_statusLabel) return;
    m_lastStatus = text;
    m_statusLabel->setString(text.c_str());
    m_statusLabel->setColor(server.isRunning()
        ? ccColor3B{120, 255, 150}
        : ccColor3B{190, 195, 210});
}

void StreamOverlayPopup::update(float dt) {
    kit::stepWheelScroll(m_scroll, m_wheelTargetY, m_wheelTargetSet, dt);
    refreshStatus();
}

void StreamOverlayPopup::scrollWheel(float x, float y) {
    kit::queueWheelScroll(m_scroll, x, y, m_wheelTargetY, m_wheelTargetSet);
}

} // namespace paimon::twitch
