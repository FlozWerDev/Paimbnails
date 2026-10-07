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
    if (!PaimonPopup::init(kPopupWidth, kPopupHeight)) return false;
    setTitle("Overlay para OBS");
    addCorners();
    addInfoButton("Overlay para OBS",
        "Sirve una <cy>Browser Source</c> privada desde tu propia PC con la cola de "
        "requests, para ponerla en OBS.\n\n"
        "<cg>URL limpia</c>: pegala como Fuente de navegador de 1920x1080 y deja "
        "activado 'Actualizar al activar la escena'. <cj>Vista previa</c> abre una demo "
        "en el navegador aunque la cola este vacia y <cp>Galeria</c> muestra todos los estilos.\n\n"
        "Cada fuente puede forzar su propio look con parametros: "
        "<cy>/overlay?style=gd&layout=ticker&anim=bounce&scale=0.8</c>\n\n"
        "El servidor solo escucha en <co>localhost</c>: nadie fuera de tu PC puede abrirlo.");
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
    auto toggle = [this](bool StreamOverlayConfig::* member) {
        return [this, member](bool value) {
            this->apply([value, member](StreamOverlayConfig& config) { config.*member = value; });
        };
    };

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

    items.push_back(kit::makeCard(width, "Estilo", {255, 120, 200}, {
        kit::makeSelectRow(
            inner,
            "Estilo visual", "19 temas, incluido uno con las fuentes y caras reales de GD.",
            streamOverlayStyleNames(), static_cast<int>(m_config.style),
            [this](int index) {
                this->apply([index](StreamOverlayConfig& config) {
                    config.style = static_cast<StreamOverlayStyle>(index);
                });
            }
        ),
        kit::makeButtonRow(
            inner,
            "Galeria de estilos",
            "Todos los estilos animados a la vez, con su link para OBS.",
            "Abrir",
            [this] { this->openGallery(); }
        ),
        kit::makeToggleRow(
            inner, "Usar mis colores", "Reemplaza la paleta del estilo por la de abajo.",
            m_config.customColors,
            toggle(&StreamOverlayConfig::customColors)
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
            toggle(&StreamOverlayConfig::showAuthor)
        ),
        kit::makeToggleRow(
            inner, "Solicitante", "Muestra quien lo pidio en el chat o la web.",
            m_config.showRequester,
            toggle(&StreamOverlayConfig::showRequester)
        ),
        kit::makeToggleRow(
            inner, "ID del nivel", "Util para que el publico pueda buscarlo.",
            m_config.showLevelID,
            toggle(&StreamOverlayConfig::showLevelID)
        ),
        kit::makeToggleRow(
            inner, "Progreso en vivo", "Barra y porcentaje del nivel que estas jugando.",
            m_config.showProgress,
            toggle(&StreamOverlayConfig::showProgress)
        ),
        kit::makeToggleRow(
            inner, "Total de la cola", "Contador de requests pendientes en la cabecera.",
            m_config.showQueueCount,
            toggle(&StreamOverlayConfig::showQueueCount)
        ),
        kit::makeToggleRow(
            inner, "Dificultad y estrellas", "Cara de dificultad, estrellas y duracion.",
            m_config.showDifficulty,
            toggle(&StreamOverlayConfig::showDifficulty)
        ),
        kit::makeToggleRow(
            inner, "Plataforma", "Etiqueta de Twitch, YouTube, Kick, TikTok o web.",
            m_config.showPlatform,
            toggle(&StreamOverlayConfig::showPlatform)
        ),
        kit::makeToggleRow(
            inner, "Intentos y mejor marca", "Intento actual, modo practica y tu record.",
            m_config.showAttempts,
            toggle(&StreamOverlayConfig::showAttempts)
        ),
        kit::makeToggleRow(
            inner, "Estadisticas del directo", "Recibidos, jugados y espera media.",
            m_config.showStats,
            toggle(&StreamOverlayConfig::showStats)
        ),
    }));

    items.push_back(kit::makeCard(width, "Efectos y avisos", {255, 150, 90}, {
        kit::makeToggleRow(
            inner, "Aviso de nuevo request", "Notificacion animada cuando llega un pedido.",
            m_config.showAlerts,
            toggle(&StreamOverlayConfig::showAlerts)
        ),
        kit::makeToggleRow(
            inner, "Sonido del aviso", "Activa 'Controlar audio via OBS' en la fuente.",
            m_config.alertSound,
            toggle(&StreamOverlayConfig::alertSound)
        ),
        kit::makeToggleRow(
            inner, "Celebracion al completar", "Confeti al 100% y aviso de nuevo record.",
            m_config.showCelebration,
            toggle(&StreamOverlayConfig::showCelebration)
        ),
        kit::makeToggleRow(
            inner, "Particulas de fondo", "Nieve, brasas, estrellas... segun el estilo.",
            m_config.showParticles,
            toggle(&StreamOverlayConfig::showParticles)
        ),
        kit::makeToggleRow(
            inner, "Ocultar sin actividad", "Desaparece si no juegas nada y la cola esta vacia.",
            m_config.hideWhenIdle,
            toggle(&StreamOverlayConfig::hideWhenIdle)
        ),
    }));

    items.push_back(kit::makeCard(width, "Composicion y movimiento", {120, 255, 155}, {
        kit::makeSelectRow(
            inner,
            "Diseno", "Tarjetas, lateral, cinta, banner, esquina...",
            streamOverlayLayoutNames(), static_cast<int>(m_config.layout),
            [this](int index) {
                this->apply([index](StreamOverlayConfig& config) {
                    config.layout = static_cast<StreamOverlayLayout>(index);
                });
            }
        ),
        kit::makeSelectRow(
            inner,
            "Animacion", "Como entra el nivel actual y se reordena la cola.",
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
            m_config.scale, .5, 1.6,
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

    items.push_back(kit::makeCard(width, "Paleta personalizada", {255, 205, 61}, {
        kit::makeColorRow(
            inner, "Color principal", "Luces, progreso y detalles activos.",
            m_config.accent,
            [this](ccColor3B color) {
                this->apply([color](StreamOverlayConfig& config) { config.accent = color; });
            }
        ),
        kit::makeColorRow(
            inner, "Cristal", "Tono base de las tarjetas (con 'Usar mis colores').",
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

void StreamOverlayPopup::openOverlayPage(std::string const& url, char const* warn) {
    if (!StreamOverlayServer::get().isRunning()) {
        PaimonNotify::create(warn, NotificationIcon::Warning)->show();
        return;
    }
    geode::utils::web::openLinkInBrowser(url);
}

void StreamOverlayPopup::openPreview() {
    openOverlayPage(StreamOverlayServer::get().previewUrl(),
        "Enciende el overlay antes de abrir la vista previa");
}

void StreamOverlayPopup::openGallery() {
    openOverlayPage(StreamOverlayServer::get().galleryUrl(),
        "Enciende el overlay antes de abrir la galeria");
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
