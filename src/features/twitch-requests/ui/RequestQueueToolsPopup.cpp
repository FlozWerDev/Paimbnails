#include "RequestQueueToolsPopup.hpp"

#include "../TwitchRequestManager.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <memory>

using namespace geode::prelude;

namespace paimon::twitch {

RequestQueueToolsPopup* RequestQueueToolsPopup::create() {
    auto popup = std::make_unique<RequestQueueToolsPopup>();
    if (!popup->init()) return nullptr;
    popup->autorelease();
    return popup.release();
}

RequestQueueToolsPopup::~RequestQueueToolsPopup() {
    paimon::ui::detachGeodeTextInput(m_requestInput);
    paimon::ui::detachGeodeTextInput(m_requesterInput);
}

bool RequestQueueToolsPopup::init() {
    if (!PaimonPopup::init(390.f, 280.f)) return false;
    setTitle("Gestionar requests");
    addCorners();
    paimon::markDynamicPopup(this);
    addInfoButton("Gestionar requests",
        "Agrega un nivel con <cy>ID, descripcion y video opcionales</c>. "
        "La carga manual funciona con los requests cerrados y respeta "
        "los filtros de nivel, los duplicados y el limite de la cola.\n\n"
        "<cg>Copiar IDs pendientes</c> copia los niveles sin revisar de la cola "
        "seleccionada, incluidos los ocultos por filtros, sin repetir IDs.\n\n"
        "<cy>Limpiar revisados</c> libera espacio y conserva los pendientes.");

    m_queue = TwitchRequestManager::get().selectedQueue();
    auto const size = m_mainLayer->getContentSize();
    float const center = size.width / 2.f;
    float const inner = size.width - 36.f;

    auto addLabel = [&](std::string const& text, float y, ccColor3B color) {
        auto* label = paimon::ui::makeLabel(text.c_str(), inner, .4f, color);
        label->setPosition({center, y});
        m_mainLayer->addChild(label);
        return label;
    };
    addLabel(m_queue.empty() ? "Todas las colas / Carga manual: General" : "Cola: " + m_queue,
        size.height - 46.f, paimon::ui::palette::gold);
    addLabel("ID del nivel + descripcion o video (opcionales)", size.height - 68.f,
        paimon::ui::palette::muted);

    m_requestInput = TextInput::create(inner, "123456 descripcion https://...", "chatFont.fnt");
    if (!m_requestInput) return false;
    m_requestInput->setCommonFilter(CommonFilter::Text);
    m_requestInput->setMaxCharCount(1400);
    m_requestInput->setPosition({center, size.height - 90.f});
    m_mainLayer->addChild(m_requestInput);

    addLabel("Pedidor (opcional)", size.height - 115.f, paimon::ui::palette::muted);
    m_requesterInput = TextInput::create(inner, "Streamer", "chatFont.fnt");
    if (!m_requesterInput) return false;
    m_requesterInput->setCommonFilter(CommonFilter::Text);
    m_requesterInput->setMaxCharCount(64);
    m_requesterInput->setPosition({center, size.height - 136.f});
    m_mainLayer->addChild(m_requesterInput);

    auto* menu = paimon::ui::makeZeroMenu("request-queue-tools-menu");
    m_mainLayer->addChild(menu, 5);
    auto addButton = [&](char const* text, CCPoint position, std::function<void()> action,
                         paimon::ui::Btn skin) {
        auto* button = paimon::ui::makeButton(text, std::move(action), skin, 154.f, .5f);
        button->setPosition(position);
        menu->addChild(button);
    };
    addButton("Agregar nivel", {center, 112.f}, [this] { addManualRequest(); }, paimon::ui::Btn::Green);
    m_status = addLabel("Puedes agregar niveles con los requests cerrados", 88.f,
        paimon::ui::palette::muted);
    addButton("Copiar IDs pendientes", {size.width * .26f, 55.f},
        [this] { copyPendingIDs(); }, paimon::ui::Btn::Cyan);
    addButton("Limpiar revisados", {size.width * .74f, 55.f},
        [this] { confirmRemoveReviewed(); }, paimon::ui::Btn::Gray);
    addButton("Vaciar cola", {center, 24.f}, [this] { confirmClearQueue(); }, paimon::ui::Btn::Red);
    return true;
}

bool RequestQueueToolsPopup::queueStillSelected() const {
    if (TwitchRequestManager::get().selectedQueue() == m_queue) return true;
    PaimonNotify::show("La cola cambio; vuelve a abrir Gestionar", NotificationIcon::Info);
    return false;
}

void RequestQueueToolsPopup::showStatus(std::string const& text, bool success) {
    m_status->setString(text.c_str());
    m_status->setColor(success ? paimon::ui::palette::success : paimon::ui::palette::danger);
    m_status->limitLabelWidth(m_mainLayer->getContentSize().width - 36.f, .4f, .25f);
}

void RequestQueueToolsPopup::addManualRequest() {
    if (!queueStillSelected()) return;
    auto const error = TwitchRequestManager::get().addManualRequest(
        m_requestInput->getString(), m_requesterInput->getString());
    if (error.empty()) {
        m_requestInput->setString("");
        showStatus("Nivel agregado a la cola", true);
        return;
    }
    if (error == "duplicate") showStatus("Ese nivel ya esta en esta cola", false);
    else if (error == "full") showStatus("Cola llena; limpia los revisados para liberar espacio", false);
    else if (error == "filtered") showStatus("El nivel no pasa los filtros actuales", false);
    else showStatus("Escribe una ID valida al inicio (ejemplo: 123456)", false);
}

void RequestQueueToolsPopup::copyPendingIDs() {
    if (!queueStillSelected()) return;
    auto const ids = TwitchRequestManager::get().pendingLevelIDs();
    if (ids.empty()) {
        showStatus("No hay IDs pendientes en esta cola", false);
        return;
    }
    std::string text;
    for (int id : ids) {
        if (!text.empty()) text += '\n';
        text += std::to_string(id);
    }
    if (!geode::utils::clipboard::write(text)) {
        showStatus("No se pudieron copiar los IDs", false);
        return;
    }
    showStatus(fmt::format("{} IDs pendientes copiados", ids.size()), true);
}

void RequestQueueToolsPopup::confirmRemoveReviewed() {
    if (!queueStillSelected()) return;
    auto const count = TwitchRequestManager::get().reviewedCount();
    if (count == 0) {
        showStatus("No hay requests revisados para limpiar", false);
        return;
    }
    Ref<RequestQueueToolsPopup> self = this;
    geode::createQuickPopup("Limpiar revisados",
        fmt::format("Eliminar <cy>{}</c> requests revisados de <cy>{}</c>?\n"
                    "Los pendientes se conservan.", count, m_queue.empty() ? "todas las colas" : m_queue),
        "Cancelar", "Limpiar", [self](auto, bool confirmed) {
            if (!confirmed || !self->getParent() || !self->queueStillSelected()) return;
            auto const removed = TwitchRequestManager::get().removeReviewed();
            self->showStatus(fmt::format("{} requests revisados eliminados", removed), true);
        });
}

void RequestQueueToolsPopup::confirmClearQueue() {
    if (!queueStillSelected()) return;
    if (TwitchRequestManager::get().selectedRequestCount() == 0) {
        showStatus("La cola ya esta vacia", false);
        return;
    }
    Ref<RequestQueueToolsPopup> self = this;
    geode::createQuickPopup("Vaciar requests",
        m_queue.empty() ? "Eliminar <cr>todos</c> los niveles de todas las colas?"
            : fmt::format("Eliminar los niveles de la cola <cy>{}</c>?", m_queue),
        "Cancelar", "Vaciar", [self](auto, bool confirmed) {
            if (!confirmed || !self->getParent() || !self->queueStillSelected()) return;
            TwitchRequestManager::get().clear();
            self->showStatus("Cola vaciada", true);
        });
}

} // namespace paimon::twitch
