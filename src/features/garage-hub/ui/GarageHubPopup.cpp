#include "GarageHubPopup.hpp"

#include "../GarageButtonHub.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/binding/GJGarageLayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::garage_hub::ui {

namespace {

constexpr float kCellWidth = 78.f;
constexpr float kCellHeight = 76.f;
constexpr int kMaxColumns = 4;
constexpr float kTopPad = 46.f;
constexpr float kBottomPad = 10.f;
// Distancia desde el borde superior de la celda al centro del icono y al texto.
constexpr float kIconDrop = 26.f;
constexpr float kLabelDrop = 60.f;

std::string tr(char const* key) {
    return Localization::get().getString(key);
}

CCLabelBMFont* makeLabel(std::string const& text) {
    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    if (!label) return nullptr;

    float const raw = std::max(label->getContentWidth(), 1.f);
    label->setScale(std::min(0.4f, (kCellWidth - 8.f) / raw));
    label->setColor({210, 210, 225});
    return label;
}

}  // anonymous namespace

GarageHubPopup* GarageHubPopup::create(GJGarageLayer* garage) {
    auto* popup = new GarageHubPopup();
    if (popup->init(garage)) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

GarageHubPopup::~GarageHubPopup() {
    giveButtonsBack();
}

bool GarageHubPopup::init(GJGarageLayer* garage) {
    if (!garage) return false;
    m_garage = garage;

    auto const items = garage_hub::entries(garage);
    int const count = static_cast<int>(items.size());
    int const columns = std::clamp(count, 1, kMaxColumns);
    int const rows = std::max(1, (count + kMaxColumns - 1) / kMaxColumns);

    float const width = std::max(260.f, columns * kCellWidth + 50.f);
    float const height = kTopPad + rows * kCellHeight + kBottomPad;

    if (!Popup::init(width, height)) return false;

    this->setTitle(tr("garage-hub.title"));
    this->setID("garage-hub-popup"_spr);
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();

    if (items.empty()) {
        if (auto* empty = makeLabel(tr("garage-hub.empty"))) {
            empty->setScale(0.5f);
            empty->setPosition({content.width / 2.f, content.height / 2.f - 8.f});
            m_mainLayer->addChild(empty);
        }
        return true;
    }

    float const gridWidth = columns * kCellWidth;
    float const left = (content.width - gridWidth) / 2.f;
    float const top = content.height - kTopPad;

    for (int index = 0; index < count; ++index) {
        int const column = index % kMaxColumns;
        int const row = index / kMaxColumns;
        float const x = left + kCellWidth * (column + 0.5f);
        float const cellTop = top - row * kCellHeight;

        borrow(items[index], {x, cellTop - kIconDrop});

        if (auto* label = makeLabel(garage_hub::labelOf(items[index]))) {
            label->setPosition({x, cellTop - kLabelDrop});
            m_mainLayer->addChild(label);
        }
    }

    return true;
}

void GarageHubPopup::borrow(CCMenuItem* btn, CCPoint const& spot) {
    if (!btn) return;

    // El Ref de la lista es lo que lo mantiene vivo mientras cambia de padre.
    m_borrowed.push_back({btn, btn->m_pListener, btn->m_pfnSelector});
    // El popup se queda de intermediario para poder cerrarse antes de que el
    // boton haga lo suyo; casi todos abren otra pantalla encima.
    btn->setTarget(this, menu_selector(GarageHubPopup::onEntry));

    btn->removeFromParentAndCleanup(false);
    btn->setPosition(spot);
    m_buttonMenu->addChild(btn);
}

void GarageHubPopup::giveButtonsBack() {
    auto* home = garage_hub::rail(m_garage);

    for (auto const& borrowed : m_borrowed) {
        auto* btn = borrowed.button.data();
        if (!btn) continue;

        btn->setTarget(borrowed.listener, borrowed.selector);
        if (!home) continue;
        btn->removeFromParentAndCleanup(false);
        home->addChild(btn);
    }
    m_borrowed.clear();
}

void GarageHubPopup::onClose(CCObject* sender) {
    giveButtonsBack();
    Popup::onClose(sender);
}

void GarageHubPopup::onEntry(CCObject* sender) {
    Ref<CCMenuItem> item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item) return;

    Ref<CCObject> listener;
    SEL_MenuHandler selector = nullptr;
    for (auto const& borrowed : m_borrowed) {
        if (borrowed.button.data() != item.data()) continue;
        listener = borrowed.listener;
        selector = borrowed.selector;
        break;
    }

    // A partir de aqui el popup ya puede estar destruido, asi que solo se tocan
    // las copias locales.
    this->onClose(nullptr);

    CCObject* target = listener;
    if (target && selector) (target->*selector)(item);
}

}  // namespace paimon::garage_hub::ui
