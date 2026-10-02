#include "../../../ui/PaimonPopup.hpp"
#include "RequestSourcesPopup.hpp"

#include "../TwitchRequestManager.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/ui/TextInput.hpp>
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>

using namespace geode::prelude;
namespace kit = paimon::configkit;

namespace paimon::twitch {

namespace {

constexpr ccColor3B kAccent = {166, 112, 255};

void addButton(CCMenu* menu, char const* text, CCPoint position,
    std::function<void()> action, paimon::ui::Btn skin = paimon::ui::Btn::Green) {
    auto* sprite = paimon::ui::makeButtonSprite(text, skin, 0.f, .55f);
    if (!sprite) return;
    auto* button = CCMenuItemExt::createSpriteExtra(sprite,
        [action = std::move(action)](CCMenuItemSpriteExtra*) { action(); });
    button->setPosition(position);
    menu->addChild(button);
}

void addCaption(CCNode* parent, char const* text, CCPoint position) {
    auto* label = paimon::ui::makeLabel(text, 320.f, .36f, kit::kDescColor);
    label->setAnchorPoint({0.f, .5f});
    label->setPosition(position);
    parent->addChild(label);
}

bool hasPopupAbove(Popup* owner) {
    auto* parent = owner->getParent();
    if (!parent) return false;
    for (auto* child : CCArrayExt<CCNode*>(parent->getChildren())) {
        if (child != owner && typeinfo_cast<FLAlertLayer*>(child)
            && child->getZOrder() >= owner->getZOrder()) return true;
    }
    return false;
}

class RequestQueuePopup final : public PaimonPopup {
public:
    static RequestQueuePopup* create(std::string const& value, std::function<void(std::string)> callback) {
        auto* popup = new RequestQueuePopup;
        if (popup->init(value, std::move(callback))) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }

private:
    ~RequestQueuePopup() override { paimon::ui::detachGeodeTextInput(m_input); }

    bool init(std::string const& value, std::function<void(std::string)> callback) {
        if (!PaimonPopup::init(320.f, 160.f)) return false;
        setTitle("Cola de destino");
        addCorners();
        paimon::markDynamicPopup(this);
        m_actions = CCMenu::create();
        m_actions->setPosition({0.f, 0.f});
        m_actions->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        m_mainLayer->addChild(m_actions, 5);
        m_callback = std::move(callback);
        m_input = TextInput::create(276.f, "General", "chatFont.fnt");
        if (!m_input) return false;
        m_input->setMaxCharCount(48);
        m_input->setString(value);
        m_input->setPosition({160.f, 90.f});
        m_mainLayer->addChild(m_input);
        auto* hint = kit::makeHint(276.f, "El mismo nombre agrupa varios comandos, canjes o plataformas.");
        hint->setPosition({22.f, 40.f});
        m_mainLayer->addChild(hint);
        addButton(m_actions, "Guardar", {160.f, 23.f}, [this] {
            m_callback(normalizeQueueName(m_input->getString()));
            onClose(nullptr);
        });
        return true;
    }

    CCMenu* m_actions = nullptr;
    TextInput* m_input = nullptr;
    std::function<void(std::string)> m_callback;
};

class RequestRoutePopup final : public PaimonPopup {
public:
    static RequestRoutePopup* create(RequestRoute route, size_t index, std::function<void()> callback) {
        auto* popup = new RequestRoutePopup;
        if (popup->init(std::move(route), index, std::move(callback))) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }

private:
    ~RequestRoutePopup() override {
        paimon::ui::detachGeodeTextInput(m_key);
        paimon::ui::detachGeodeTextInput(m_name);
        paimon::ui::detachGeodeTextInput(m_queue);
    }

    bool init(RequestRoute route, size_t index, std::function<void()> callback) {
        if (!PaimonPopup::init(400.f, 300.f)) return false;
        m_route = std::move(route);
        m_index = index;
        m_callback = std::move(callback);
        setTitle(m_route.reward ? "Vincular canje de Twitch" : "Destino de un comando");
        addCorners();
        addInfoButton(m_route.reward ? "Vincular canje de Twitch" : "Destino de un comando",
            m_route.reward
                ? "Activa <cy>Pedir texto</c> en el canje de Twitch. El espectador escribe: "
                  "ID descripcion. Usa <cg>Detectar siguiente</c> y haz el canje, o pega su "
                  "ID a mano. El mod no confirma ni reembolsa puntos."
                : "Manda un comando a una cola concreta. Ejemplo: <cy>!req</c> a General y "
                  "<cy>!vip</c> a Prioridad. Una regla con plataforma concreta gana a la de "
                  "Todos. El comando se acepta aunque no este en la lista del chat.");
        paimon::markDynamicPopup(this);
        m_actions = CCMenu::create();
        m_actions->setPosition({0.f, 0.f});
        m_actions->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        m_mainLayer->addChild(m_actions, 5);

        if (!m_route.reward) {
            std::vector<std::string> platforms{"Todos", "Twitch", "YouTube", "Kick", "TikTok"};
            int selected = m_route.platform == "*" ? 0 : static_cast<int>(platformFromKey(m_route.platform)) + 1;
            auto* selector = kit::makeSelectRow(356.f, "Plataforma", "Una regla especifica tiene prioridad.",
                platforms, selected, [this](int value) {
                    m_route.platform = value == 0 ? "*" : platformKey(platformFromIndex(value - 1));
                });
            selector->setPosition({22.f, 209.f});
            m_mainLayer->addChild(selector);
        } else {
            addButton(m_actions, "Ultimo canje", {108.f, 239.f}, [this] { useLastReward(); },
                paimon::ui::Btn::Cyan);
            addButton(m_actions, "Detectar siguiente", {281.f, 239.f}, [this] {
                m_captureRevision = TwitchRequestManager::get().rewardDetectionRevision();
                m_capturing = true;
                m_hint->setString("Haz un canje con texto en el canal conectado; se vinculara su ID.");
            }, paimon::ui::Btn::Cyan);
        }

        auto input = [this](char const* caption, char const* placeholder, std::string const& value,
            float y, int limit) {
            addCaption(m_mainLayer, caption, {22.f, y + 21.f});
            auto* field = TextInput::create(356.f, placeholder, "chatFont.fnt");
            if (!field) return static_cast<TextInput*>(nullptr);
            field->setMaxCharCount(limit);
            field->setString(value);
            field->setPosition({200.f, y});
            m_mainLayer->addChild(field);
            return field;
        };
        m_key = input(m_route.reward ? "ID del canje" : "Comando", m_route.reward ? "UUID del canje" : "!req",
            m_route.key, 184.f, 80);
        m_name = input("Nombre para reconocerlo (opcional)", "Prioridad, normales...", m_route.name, 135.f, 48);
        m_queue = input("Cola de destino", "General", m_route.queue, 86.f, 48);

        if (!m_key || !m_name || !m_queue) return false;

        m_hint = CCLabelBMFont::create(m_route.reward
            ? "Activa Pedir texto en Twitch. El espectador escribe: ID descripcion."
            : "El comando tambien se acepta si no esta en la lista del chat.",
            "chatFont.fnt", 356.f / .36f, kCCTextAlignmentCenter);
        m_hint->setScale(.36f);
        m_hint->setColor(kit::kDescColor);
        m_hint->setPosition({200.f, 50.f});
        m_mainLayer->addChild(m_hint);

        addButton(m_actions, "Guardar", {m_index == SIZE_MAX ? 200.f : 125.f, 23.f}, [this] { save(); });
        if (m_index != SIZE_MAX) addButton(m_actions, "Eliminar regla", {275.f, 23.f}, [this] {
            auto& manager = TwitchRequestManager::get();
            auto config = manager.routing();
            if (m_index < config.routes.size()) config.routes.erase(config.routes.begin() + static_cast<std::ptrdiff_t>(m_index));
            manager.setRouting(std::move(config));
            m_callback();
            onClose(nullptr);
        }, paimon::ui::Btn::Red);
        scheduleUpdate();
        return true;
    }

    void useLastReward() {
        auto const& id = TwitchRequestManager::get().lastRewardID();
        if (id.empty()) {
            PaimonNotify::show("Conecta Twitch y haz un canje que pida texto", NotificationIcon::Info);
            return;
        }
        m_key->setString(id);
        m_capturing = false;
        m_hint->setString("Canje detectado. Elige un nombre y su cola de destino.");
    }

    void update(float) override {
        if (m_capturing && m_captureRevision != TwitchRequestManager::get().rewardDetectionRevision()) useLastReward();
    }

    void save() {
        m_route.key = std::string(m_key->getString());
        m_route.name = std::string(m_name->getString());
        m_route.queue = std::string(m_queue->getString());
        if (!normalizeRoute(m_route)) {
            PaimonNotify::show(m_route.reward ? "Usa un ID de canje valido o detectalo desde Twitch"
                : "Escribe un solo comando, por ejemplo !req", NotificationIcon::Warning);
            return;
        }
        auto& manager = TwitchRequestManager::get();
        auto config = manager.routing();
        if (m_index < config.routes.size()) config.routes.erase(config.routes.begin() + static_cast<std::ptrdiff_t>(m_index));
        std::erase_if(config.routes, [this](RequestRoute const& route) {
            return route.platform == m_route.platform && route.reward == m_route.reward && route.key == m_route.key;
        });
        if (config.routes.size() >= 64) {
            PaimonNotify::show("Limite de 64 reglas; edita o elimina una", NotificationIcon::Warning);
            return;
        }
        config.routes.push_back(m_route);
        manager.setRouting(std::move(config));
        m_callback();
        onClose(nullptr);
    }

    CCMenu* m_actions = nullptr;
    RequestRoute m_route;
    size_t m_index = SIZE_MAX;
    TextInput* m_key = nullptr;
    TextInput* m_name = nullptr;
    TextInput* m_queue = nullptr;
    CCLabelBMFont* m_hint = nullptr;
    std::function<void()> m_callback;
    uint64_t m_captureRevision = 0;
    bool m_capturing = false;
};

class RequestQueueSelectorPopup final : public PaimonPopup {
public:
    static RequestQueueSelectorPopup* create() {
        auto* popup = new RequestQueueSelectorPopup;
        if (popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }

private:
    bool init() override {
        if (!PaimonPopup::init(360.f, 290.f)) return false;
        setTitle("Elegir cola");
        addCorners();
        paimon::markDynamicPopup(this);
        auto& manager = TwitchRequestManager::get();
        auto names = manager.queueNames();
        names.insert(names.begin(), "");
        auto const requests = manager.requests();
        std::vector<CCNode*> rows;
        for (auto const& name : names) {
            size_t count = 0;
            size_t pending = 0;
            for (auto const& request : requests) {
                if (!name.empty() && request.queue != name) continue;
                ++count;
                if (!request.played) ++pending;
            }
            auto description = fmt::format("{} pendientes / {} guardados", pending, count);
            rows.push_back(kit::makeButtonRow(336.f, name.empty() ? "Todas las colas" : name.c_str(),
                description.c_str(), manager.selectedQueue() == name ? "Actual" : "Elegir", [this, name] {
                    TwitchRequestManager::get().selectQueue(name);
                    onClose(nullptr);
                }));
        }
        m_scroll = kit::makeScrollStack({336.f, 238.f}, rows);
        m_scroll->setPosition({12.f, 12.f});
        m_mainLayer->addChild(m_scroll);
        m_scroll->enableScrollWheel();
        scheduleUpdate();
        return true;
    }

    void update(float dt) override {
        kit::stepWheelScroll(m_scroll, m_wheelTargetY, m_wheelTargetSet, dt);
    }

    void scrollWheel(float x, float y) override {
        if (kit::queueWheelScroll(m_scroll, x, y, m_wheelTargetY, m_wheelTargetSet)) return;
        Popup::scrollWheel(x, y);
    }

    ScrollLayer* m_scroll = nullptr;
    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;
};

} // namespace

Popup* createRequestQueueSelector() {
    return RequestQueueSelectorPopup::create();
}

RequestSourcesPopup* RequestSourcesPopup::create() {
    auto* popup = new RequestSourcesPopup;
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool RequestSourcesPopup::init() {
    if (!PaimonPopup::init(440.f, 300.f)) return false;
    setTitle("Origenes y colas de requests");
    addCorners();
    addInfoButton("Origenes y colas",
        "Configura de donde llegan los requests y a que cola van.\n\n"
        "Para juntar varios origenes usa el <cy>mismo nombre de cola</c>. Las reglas "
        "especificas ganan a las de Todos. Los pedidos guardados conservan su destino.\n\n"
        "Los <co>canjes</c> se leen mientras Twitch esta conectado y solo si Pedir texto "
        "esta activo; los no vinculados se ignoran. YouTube, Kick y TikTok usan sus chats.");
    paimon::markDynamicPopup(this);
    m_platform = TwitchRequestManager::get().selected();
    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setPosition({220.f, 256.f});
    m_mainLayer->addChild(m_status);
    rebuild();
    scheduleUpdate();
    return true;
}

void RequestSourcesPopup::scheduleRebuild() {
    Ref<RequestSourcesPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self && self->getParent()) self->rebuild();
    });
}

void RequestSourcesPopup::rebuild() {
    if (m_scroll) m_scroll->removeFromParent();
    m_scroll = nullptr;
    m_wheelTargetSet = false;
    auto& manager = TwitchRequestManager::get();
    auto const& config = manager.routing();
    auto const& platform = config.platforms[static_cast<size_t>(m_platform)];
    constexpr float width = 416.f;
    float const inner = kit::cardInnerWidth(width);
    std::vector<CCNode*> items;
    std::vector<CCNode*> sourceRows;
    sourceRows.push_back(kit::makeSelectRow(inner, "Origen", "Configura cada chat o tu pagina web.",
        {"Twitch", "YouTube", "Kick", "TikTok", "Web"}, static_cast<int>(m_platform), [this](int value) {
            m_platform = platformFromIndex(value);
            scheduleRebuild();
        }));
    if (m_platform != Platform::Web) sourceRows.push_back(kit::makeToggleRow(inner,
        "Aceptar comandos", "Apagalo en Twitch para recibir solo canjes vinculados.",
        platform.commandsEnabled, [this](bool value) {
            auto& manager = TwitchRequestManager::get();
            auto config = manager.routing();
            config.platforms[static_cast<size_t>(m_platform)].commandsEnabled = value;
            manager.setRouting(std::move(config));
        }));
    sourceRows.push_back(kit::makeButtonRow(inner, "Cola predeterminada", platform.queue.c_str(),
        "Elegir", [this] { editQueue(); }));
    items.push_back(kit::makeCard(width, "Por plataforma", kAccent, sourceRows));
    items.push_back(kit::makeCard(width, "Puntos de Twitch", {255, 205, 61}, {
        kit::makeToggleRow(inner, "Aceptar canjes vinculados", "Cada canje se identifica por su ID y pide texto al espectador.",
            config.pointsEnabled, [](bool value) {
                auto& manager = TwitchRequestManager::get();
                auto config = manager.routing();
                config.pointsEnabled = value;
                manager.setRouting(std::move(config));
            }),
        kit::makeButtonRow(inner, "Vincular un canje", "Detecta el canje desde el chat y elige su cola.", "Vincular",
            [this] { editRoute(true); }),
        kit::makeButtonRow(inner, "Destino de un comando", "Ejemplo: !req a General y !vip a Prioridad.", "Agregar",
            [this] { editRoute(false); }),
    }));
    for (size_t index = 0; index < config.routes.size(); ++index) {
        auto const& route = config.routes[index];
        auto title = route.name.empty() ? (route.reward ? "Canje de Twitch" : route.key) : route.name;
        auto description = fmt::format("{} / {} -> {}", route.platform == "*" ? "Todos" : route.platform,
            route.reward ? "canje" : route.key, route.queue);
        items.push_back(kit::makeButtonRow(width, title.c_str(), description.c_str(), "Editar",
            [this, index, reward = route.reward] { editRoute(reward, index); }));
    }
    items.push_back(kit::makeHint(width,
        "Para juntar origenes, usa el mismo nombre de cola. Las reglas especificas ganan a las de Todos. "
        "Los pedidos guardados conservan su destino. El limite de almacenamiento es compartido; "
        "duplicados, espera y limite por usuario se controlan en cada cola. Los canjes no vinculados se ignoran."));
    items.push_back(kit::makeHint(width,
        "Los canjes se leen mientras Twitch esta conectado y solo si Pedir texto esta activado. "
        "El mod no confirma ni reembolsa puntos en Twitch. Los comandos de YouTube, Kick y TikTok usan sus chats actuales."));
    m_scroll = kit::makeScrollStack({width, 228.f}, items);
    m_scroll->setPosition({12.f, 10.f});
    m_mainLayer->addChild(m_scroll);
}

void RequestSourcesPopup::editRoute(bool reward, size_t index) {
    auto const& routes = TwitchRequestManager::get().routing().routes;
    RequestRoute route;
    if (index < routes.size()) route = routes[index];
    else {
        route.reward = reward;
        route.platform = reward ? "twitch" : (m_platform == Platform::Web ? "*" : platformKey(m_platform));
        route.queue = reward ? "Canjes" : TwitchRequestManager::get().routing().platforms[static_cast<size_t>(m_platform)].queue;
    }
    Ref<RequestSourcesPopup> self = this;
    if (auto* popup = RequestRoutePopup::create(std::move(route), index, [self] {
        if (self && self->getParent()) self->scheduleRebuild();
    })) kit::showAbove(popup, this);
}

void RequestSourcesPopup::editQueue() {
    auto const platform = m_platform;
    auto const& name = TwitchRequestManager::get().routing().platforms[static_cast<size_t>(platform)].queue;
    Ref<RequestSourcesPopup> self = this;
    if (auto* popup = RequestQueuePopup::create(name, [self, platform](std::string name) {
        auto& manager = TwitchRequestManager::get();
        auto config = manager.routing();
        config.platforms[static_cast<size_t>(platform)].queue = std::move(name);
        manager.setRouting(std::move(config));
        if (self && self->getParent()) self->scheduleRebuild();
    })) kit::showAbove(popup, this);
}

void RequestSourcesPopup::update(float dt) {
    auto const& status = TwitchRequestManager::get().lastIntakeStatus();
    if (m_lastStatus != status) {
        m_lastStatus = status;
        m_status->setString(status.c_str());
        m_status->limitLabelWidth(400.f, .4f, .25f);
    }
    bool const covered = hasPopupAbove(this);
    if (m_scroll) m_scroll->enableScrollWheel(!covered);
    if (!covered) kit::stepWheelScroll(m_scroll, m_wheelTargetY, m_wheelTargetSet, dt);
}

void RequestSourcesPopup::scrollWheel(float x, float y) {
    if (hasPopupAbove(this)) return;
    if (kit::queueWheelScroll(m_scroll, x, y, m_wheelTargetY, m_wheelTargetSet)) return;
    Popup::scrollWheel(x, y);
}

} // namespace paimon::twitch
