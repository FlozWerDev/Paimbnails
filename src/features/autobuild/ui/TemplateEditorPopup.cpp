#include "TemplateEditorPopup.hpp"

#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/SaveString.hpp"
#include "../services/TemplateEdit.hpp"
#include "../services/TemplateStore.hpp"
#include "AutobuildPopup.hpp"
#include "PreviewNode.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/SetTextPopup.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <memory>
#include <set>

using namespace geode::prelude;

namespace {
namespace kit = paimon::configkit;

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 300.f;
constexpr float kScrollTop = 236.f;
constexpr float kScrollBottom = 56.f;

constexpr ccColor3B kOk    = {120, 255, 140};
constexpr ccColor3B kWarn  = {255, 190, 90};

// Where each neighbour direction sits in the 3x3 pad.
struct SideSlot {
    int direction;
    int column;
    int row;
    char const* label;
};
constexpr SideSlot kSideSlots[] = {
    {7, 0, 0, "NO"}, {0, 1, 0, "N"},  {4, 2, 0, "NE"},
    {3, 0, 1, "O"},                   {2, 2, 1, "E"},
    {5, 0, 2, "SO"}, {1, 1, 2, "S"},  {6, 2, 2, "SE"},
};

} // namespace

namespace paimon::autobuild {

TemplateEditorPopup* TemplateEditorPopup::create(int index) {
    auto* ret = new TemplateEditorPopup();
    if (ret && ret->setup(index)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TemplateEditorPopup::setup(int index) {
    auto const& store = TemplateStore::get();
    if (index < 0 || index >= static_cast<int>(store.all().size())) return false;
    m_index = index;
    m_draft = store.all()[index];
    return init();
}

bool TemplateEditorPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    setTitle("Editar plantilla");
    setID("autobuild-editor-popup"_spr);
    rebuild();
    return true;
}

void TemplateEditorPopup::onClose(CCObject* sender) {
    AutobuildPopup::refreshOpenPanel();
    Popup::onClose(sender);
}

void TemplateEditorPopup::scheduleRebuild() {
    Ref<TemplateEditorPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self && self->getParent()) self->rebuild();
    });
}

void TemplateEditorPopup::setStatus(std::string text, ccColor3B color) {
    m_status = std::move(text);
    m_statusColor = color;
}

void TemplateEditorPopup::touch(std::string status) {
    m_dirty = true;
    m_piece = std::clamp(m_piece, 0, std::max(0, static_cast<int>(m_draft.pieces.size()) - 1));
    setStatus(std::move(status), kOk);
    scheduleRebuild();
}

void TemplateEditorPopup::rebuild() {
    if (m_content) {
        m_content->removeFromParent();
        m_content = nullptr;
    }
    m_actions = nullptr;

    m_content = CCNode::create();
    m_content->setContentSize(m_mainLayer->getContentSize());
    m_mainLayer->addChild(m_content);

    float const width = kPopupW - 24.f;
    float const inner = kit::cardInnerWidth(width);

    auto* topMenu = CCMenu::create();
    topMenu->setPosition({0.f, 0.f});
    topMenu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    m_content->addChild(topMenu, 5);
    if (auto* helpSpr = SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
        helpSpr->setScale(0.62f);
        auto* helpBtn = CCMenuItemExt::createSpriteExtra(
            helpSpr, [this](CCMenuItemSpriteExtra*) { showHelp(); });
        helpBtn->setPosition({kPopupW - 22.f, kPopupH - 22.f});
        topMenu->addChild(helpBtn);
    }

    auto* tabs = kit::makeTabBar(width, {"Plantilla", "Piezas", "Colores"}, m_tab,
                                 [this](int index) {
                                     m_tab = index;
                                     scheduleRebuild();
                                 });
    tabs->setPosition({12.f, kScrollTop + 4.f});
    m_content->addChild(tabs);

    std::vector<CCNode*> items;
    switch (m_tab) {
        case 0:  items = templateTab(width, inner); break;
        case 1:  items = piecesTab(width, inner); break;
        default: items = colorsTab(width, inner); break;
    }

    auto* scroll = kit::makeScrollStack({width, kScrollTop - kScrollBottom}, items);
    scroll->setPosition({12.f, kScrollBottom});
    m_content->addChild(scroll);

    if (!m_status.empty()) {
        auto* label = CCLabelBMFont::create(m_status.c_str(), "chatFont.fnt",
                                            (width - 20.f) / 0.42f, kCCTextAlignmentCenter);
        label->setScale(0.42f);
        label->setColor(m_statusColor);
        label->setAnchorPoint({0.5f, 0.5f});
        label->setPosition({kPopupW / 2.f, kScrollBottom - 12.f});
        m_content->addChild(label);
    }

    buildActions();
}

void TemplateEditorPopup::addAction(char const* text, char const* sprite, bool enabled,
                                    std::function<void()> onPress) {
    if (!m_actions) return;
    auto* spr = ButtonSprite::create(text, "bigFont.fnt", sprite, 0.7f);
    if (!spr) return;
    spr->setScale(0.62f);
    if (!enabled) spr->setColor({110, 110, 110});

    auto* button = CCMenuItemExt::createSpriteExtra(
        spr, [enabled, cb = std::move(onPress)](CCMenuItemSpriteExtra*) {
            if (enabled && cb) cb();
        });
    if (!enabled) button->setOpacity(140);
    m_actions->addChild(button);
}

void TemplateEditorPopup::buildActions() {
    m_actions = CCMenu::create();
    m_actions->setPosition({kPopupW / 2.f, 26.f});
    m_actions->setContentSize({kPopupW - 40.f, 34.f});
    m_actions->setLayout(RowLayout::create()->setGap(8.f));
    m_content->addChild(m_actions);

    addAction("Guardar", "GJ_button_01.png", m_dirty && m_draft.valid(), [this] { save(); });
    addAction("Descartar", "GJ_button_06.png", m_dirty, [this] {
        auto const& store = TemplateStore::get();
        if (m_index >= 0 && m_index < static_cast<int>(store.all().size())) {
            m_draft = store.all()[m_index];
        }
        m_dirty = false;
        m_piece = 0;
        setStatus("Cambios descartados.", kWarn);
        scheduleRebuild();
    });
    addAction("Renombrar", "GJ_button_04.png", true, [this] {
        // A cancelled remap leaves its channel behind, and the next text popup
        // would be read as that remap instead of a rename.
        m_remapFrom = 0;
        auto* popup = SetTextPopup::create(m_draft.name, "Nombre", 30, "Renombrar",
                                           "Guardar", true, 0.f);
        if (!popup) return;
        popup->m_delegate = this;
        kit::showAbove(popup, this);
    });
    m_actions->updateLayout();
}

std::vector<CCNode*> TemplateEditorPopup::templateTab(float width, float inner) {
    std::vector<CCNode*> items;

    auto* header = CCNode::create();
    header->setAnchorPoint({0.f, 0.f});
    header->setContentSize({inner, 110.f});
    {
        auto* preview = PreviewNode::create({inner - 20.f, 76.f});
        if (preview) {
            preview->setPosition({10.f, 28.f});
            preview->showTemplate(m_draft);
            header->addChild(preview);
        }
        auto* name = CCLabelBMFont::create(m_draft.name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 1.f});
        name->limitLabelWidth(inner - 20.f, 0.42f, 0.1f);
        name->setPosition({10.f, 22.f});
        header->addChild(name);

        auto* info = CCLabelBMFont::create(m_draft.summary().c_str(), "chatFont.fnt");
        info->setAnchorPoint({0.f, 1.f});
        info->setScale(0.4f);
        info->setColor(kit::kDescColor);
        info->setPosition({10.f, 6.f});
        header->addChild(info);
    }
    items.push_back(kit::makeCard(width, "Vista", {255, 210, 100}, {header}));

    std::vector<CCNode*> shape;
    shape.push_back(kit::makeSelectRow(inner, "Modo",
        "Onda teje las piezas por vecindad; sellos suelta la pieza entera.",
        {"Onda", "Sellos"}, m_draft.mode == Mode::Wave ? 0 : 1,
        [this](int index) {
            auto const wanted = index == 1 ? Mode::Stamp : Mode::Wave;
            if (wanted == m_draft.mode) return;
            m_draft.mode = wanted;
            if (wanted == Mode::Wave) edit::rebuildLinks(m_draft);
            touch(fmt::format("Modo cambiado a {}.", modeName(wanted)));
        }));
    if (m_draft.mode == Mode::Wave) {
        shape.push_back(kit::makeSliderRow(inner, "Tamano de celda",
            "Rejilla con la que la onda coloca las piezas.",
            m_draft.cell, 10.0, 120.0,
            [](double v) { return fmt::format("{:.0f}", v); },
            [this](double v) {
                m_draft.cell = static_cast<float>(std::round(v));
                m_dirty = true;
            }));
        shape.push_back(kit::makeButtonRow(inner, "Rehacer reglas",
            "Vuelve a deducir que pieza puede ir al lado de cual desde las muestras.",
            "Rehacer", [this] {
                edit::rebuildLinks(m_draft);
                touch("Reglas de vecindad rehechas.");
            }));
    }
    items.push_back(kit::makeCard(width, "Forma", {150, 220, 255}, shape));

    std::vector<CCNode*> filters;
    auto const kinds = edit::countKinds(m_draft);
    for (auto const& entry : kinds) {
        filters.push_back(kit::makeButtonRow(inner, kindName(entry.kind),
            fmt::format("{} objetos en la plantilla.", entry.objects).c_str(), "Quitar",
            [this, kind = entry.kind] {
                int const removed = edit::removeKind(m_draft, kind);
                if (removed == 0) {
                    setStatus("No quedaba ninguno.", kWarn);
                    scheduleRebuild();
                    return;
                }
                if (m_draft.mode == Mode::Wave) edit::rebuildLinks(m_draft);
                touch(fmt::format("{} objetos quitados.", removed));
            }));
    }
    if (filters.empty()) {
        filters.push_back(kit::makeHint(inner, "La plantilla se quedo sin objetos."));
    } else {
        filters.push_back(kit::makeButtonRow(inner, "Solo lo solido",
            "Deja bloques y rampas, quita adornos, pinchos y todo lo demas.", "Filtrar",
            [this] {
                int const removed = edit::keepOnlyKinds(
                    m_draft, {ObjectKind::Solid, ObjectKind::Slope});
                if (!m_draft.valid()) {
                    auto const& store = TemplateStore::get();
                    m_draft = store.all()[m_index];
                    setStatus("Eso habria dejado la plantilla vacia.", kWarn);
                    scheduleRebuild();
                    return;
                }
                if (m_draft.mode == Mode::Wave) edit::rebuildLinks(m_draft);
                touch(fmt::format("{} objetos quitados.", removed));
            }));
    }
    items.push_back(kit::makeCard(width, "Que se copia", {130, 240, 170}, filters));

    return items;
}

CCNode* TemplateEditorPopup::sidesGrid(float width) {
    constexpr float kCellW = 34.f;
    constexpr float kCellH = 20.f;
    auto* node = CCNode::create();
    node->setAnchorPoint({0.f, 0.f});
    node->setContentSize({width, kCellH * 3.f + 16.f});

    auto* title = CCLabelBMFont::create("Bordes abiertos", "bigFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->setScale(0.36f);
    title->setPosition({10.f, kCellH * 3.f + 6.f});
    node->addChild(title);

    auto* hint = CCLabelBMFont::create(
        "Marca los lados donde la pieza puede quedarse sin vecino.", "chatFont.fnt");
    hint->setAnchorPoint({1.f, 0.5f});
    hint->setScale(0.32f);
    hint->setColor(kit::kDescColor);
    hint->setPosition({width - 10.f, kCellH * 3.f + 6.f});
    node->addChild(hint);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    node->addChild(menu, 5);

    if (m_piece < 0 || m_piece >= static_cast<int>(m_draft.links.size())) return node;
    for (auto const& slot : kSideSlots) {
        bool const open = m_draft.links[m_piece].open[slot.direction];
        auto* spr = ButtonSprite::create(slot.label, 30, true, "bigFont.fnt",
                                         open ? "GJ_button_02.png" : "GJ_button_04.png", 18.f, 0.40f);
        if (!spr) continue;
        auto* button = CCMenuItemExt::createSpriteExtra(
            spr, [this, direction = slot.direction](CCMenuItemSpriteExtra*) {
                auto& open = m_draft.links[m_piece].open[direction];
                open = !open;
                touch(open ? "Borde abierto." : "Borde cerrado.");
            });
        button->setPosition({12.f + kCellW / 2.f + slot.column * kCellW,
                             kCellH * 2.5f - slot.row * kCellH});
        menu->addChild(button);
    }
    return node;
}

CCNode* TemplateEditorPopup::pieceDetail(float width) {
    auto* node = CCNode::create();
    node->setAnchorPoint({0.f, 0.f});

    bool const wave = m_draft.mode == Mode::Wave;
    float const height = wave ? 190.f : 120.f;
    node->setContentSize({width, height});

    auto const& piece = m_draft.pieces[m_piece];
    if (auto* preview = PreviewNode::create({86.f, 74.f})) {
        preview->setPosition({10.f, height - 82.f});
        preview->showPiece(piece);
        node->addChild(preview);
    }

    auto* name = CCLabelBMFont::create(fmt::format("Pieza {}", m_piece + 1).c_str(),
                                       "bigFont.fnt");
    name->setAnchorPoint({0.f, 1.f});
    name->setScale(0.42f);
    name->setPosition({106.f, height - 8.f});
    node->addChild(name);

    auto const detail = fmt::format("{} objetos - {:.0f}x{:.0f}", piece.objects.size(),
                                    piece.width, piece.height);
    auto* info = CCLabelBMFont::create(detail.c_str(), "chatFont.fnt");
    info->setAnchorPoint({0.f, 1.f});
    info->setScale(0.4f);
    info->setColor(kit::kDescColor);
    info->setPosition({106.f, height - 26.f});
    node->addChild(info);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    node->addChild(menu, 5);

    struct Action {
        char const* text;
        int width;
        char const* sprite;
        std::function<void()> run;
    };
    std::vector<Action> actions = {
        {"Duplicar", 66, "GJ_button_04.png", [this] {
            if (!edit::duplicatePiece(m_draft, m_piece)) return;
            m_piece = static_cast<int>(m_draft.pieces.size()) - 1;
            touch("Pieza duplicada.");
        }},
        {"Borrar", 54, "GJ_button_06.png", [this] {
            if (!edit::removePiece(m_draft, m_piece)) {
                setStatus("Una plantilla necesita al menos una pieza.", kWarn);
                scheduleRebuild();
                return;
            }
            touch("Pieza borrada.");
        }},
    };
    float x = 118.f;
    for (auto& action : actions) {
        auto* spr = ButtonSprite::create(action.text, action.width, true, "bigFont.fnt",
                                         action.sprite, 18.f, 0.40f);
        if (!spr) continue;
        auto* button = CCMenuItemExt::createSpriteExtra(
            spr, [run = action.run](CCMenuItemSpriteExtra*) { run(); });
        button->setPosition({x, height - 60.f});
        menu->addChild(button);
        x += 62.f;
    }

    auto* weight = stepperRow(width, "Peso",
        "Cuanto se repite frente a las demas piezas.", piece.weight, 1, 9999,
        [this](int value) {
            edit::setWeight(m_draft, m_piece, value);
            m_dirty = true;
        });
    weight->setPosition({0.f, wave ? 86.f : 4.f});
    node->addChild(weight);

    if (wave) {
        auto* sides = sidesGrid(width);
        sides->setPosition({0.f, 6.f});
        node->addChild(sides);
    }
    return node;
}

CCNode* TemplateEditorPopup::pieceRow(float width, int index) {
    auto const& piece = m_draft.pieces[index];
    bool const selected = index == m_piece;

    constexpr float kRowH = 40.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    if (auto* panel = SpriteHelper::createColorPanel(
            width, kRowH, selected ? ccColor3B{26, 44, 70} : kit::kCardColor,
            selected ? 210 : kit::kCardAlpha, 6.f)) {
        panel->setAnchorPoint({0.f, 0.f});
        row->addChild(panel, -1);
    }

    if (auto* preview = PreviewNode::create({46.f, kRowH - 8.f})) {
        preview->setPosition({6.f, 4.f});
        preview->showPiece(piece);
        row->addChild(preview);
    }

    auto* name = CCLabelBMFont::create(fmt::format("Pieza {}", index + 1).c_str(),
                                       "bigFont.fnt");
    name->setAnchorPoint({0.f, 1.f});
    name->setScale(0.38f);
    name->setColor(selected ? kOk : kit::kTitleColor);
    name->setPosition({60.f, kRowH - 6.f});
    row->addChild(name);

    auto const detail = fmt::format("{} objetos - peso {}", piece.objects.size(), piece.weight);
    auto* info = CCLabelBMFont::create(detail.c_str(), "chatFont.fnt");
    info->setAnchorPoint({0.f, 1.f});
    info->setScale(0.36f);
    info->setColor(kit::kDescColor);
    info->setPosition({60.f, kRowH - 22.f});
    row->addChild(info);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    row->addChild(menu, 5);

    auto* spr = ButtonSprite::create(selected ? "Abierta" : "Abrir", 60, true, "bigFont.fnt",
                                     selected ? "GJ_button_02.png" : "GJ_button_01.png", 18.f, 0.40f);
    auto* button = CCMenuItemExt::createSpriteExtra(
        spr, [this, index](CCMenuItemSpriteExtra*) {
            m_piece = index;
            scheduleRebuild();
        });
    button->setPosition({width - 38.f, kRowH / 2.f});
    menu->addChild(button);

    return row;
}

std::vector<CCNode*> TemplateEditorPopup::piecesTab(float width, float inner) {
    std::vector<CCNode*> items;
    if (m_draft.pieces.empty()) {
        items.push_back(kit::makeHint(width, "La plantilla se quedo sin piezas."));
        return items;
    }

    m_piece = std::clamp(m_piece, 0, static_cast<int>(m_draft.pieces.size()) - 1);
    items.push_back(kit::makeCard(width, "Pieza seleccionada", {150, 220, 255},
                                  {pieceDetail(inner)}));

    std::vector<CCNode*> rows;
    for (int i = 0; i < static_cast<int>(m_draft.pieces.size()) && i < 120; ++i) {
        rows.push_back(pieceRow(inner, i));
    }
    if (m_draft.pieces.size() > 120) {
        rows.push_back(kit::makeHint(inner,
            "Solo se listan las primeras 120 piezas.\n"
            "Los filtros de arriba actuan sobre todas."));
    }
    items.push_back(kit::makeCard(width, "Piezas", {130, 240, 170}, rows));
    return items;
}

std::vector<CCNode*> TemplateEditorPopup::colorsTab(float width, float inner) {
    std::vector<CCNode*> items;

    std::set<int> used;
    for (auto const& piece : m_draft.pieces) {
        for (auto const& object : piece.objects) collectColorIds(object.save, used);
    }

    items.push_back(kit::makeCard(width, "Todos los canales", {255, 165, 210}, {
        stepperRow(inner, "Desplazar", "Suma este numero a los canales 1-999 de la plantilla.",
                   m_shift, -999, 999,
                   [this](int value) { m_shift = value; }),
        kit::makeButtonRow(inner, "Aplicar desplazamiento",
            "Usa el valor de arriba. Los canales fijos (1000+) no se tocan.", "Aplicar",
            [this] {
                int const changed = edit::shiftChannels(m_draft, m_shift);
                touch(fmt::format("{} objetos repintados.", changed));
            }),
    }));

    std::vector<CCNode*> rows;
    for (int channel : used) {
        auto const title = channel >= 1000 ? fmt::format("Canal {} (fijo)", channel)
                                           : fmt::format("Canal {}", channel);
        rows.push_back(kit::makeButtonRow(inner, title.c_str(),
            "Cambiar todos los objetos que lo usan a otro canal.", "Cambiar",
            [this, channel] {
                m_remapFrom = channel;
                auto* popup = SetTextPopup::create("", "Canal destino", 4,
                                                   fmt::format("Canal {} pasa a", channel),
                                                   "Cambiar", false, 0.f);
                if (!popup) return;
                popup->m_delegate = this;
                kit::showAbove(popup, this);
            }));
        if (rows.size() >= 60) break;
    }
    if (rows.empty()) {
        rows.push_back(kit::makeHint(inner,
            "Ningun objeto de la plantilla fija un canal propio."));
    }
    items.push_back(kit::makeCard(width, "Canales que usa", {170, 190, 255}, rows));

    if (!m_draft.colors.empty()) {
        items.push_back(kit::makeHint(inner,
            "La plantilla guarda la paleta del nivel de origen. Al construir\n"
            "solo se importan los canales que las piezas usan de verdad."));
    }
    return items;
}

CCNode* TemplateEditorPopup::stepperRow(float width, char const* title, char const* desc,
                                        int value, int low, int high,
                                        std::function<void(int)> onChange) {
    constexpr float kRowH = 34.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    auto* label = CCLabelBMFont::create(title, "bigFont.fnt");
    label->setAnchorPoint({0.f, 1.f});
    label->limitLabelWidth(width - 170.f, 0.4f, 0.1f);
    label->setPosition({10.f, kRowH - 6.f});
    row->addChild(label);

    if (desc && desc[0] != '\0') {
        auto* hint = CCLabelBMFont::create(desc, "chatFont.fnt");
        hint->setAnchorPoint({0.f, 1.f});
        hint->setScale(0.36f);
        hint->setColor(kit::kDescColor);
        hint->setPosition({10.f, kRowH - 20.f});
        row->addChild(hint);
    }

    auto* valueLabel = CCLabelBMFont::create(std::to_string(value).c_str(), "bigFont.fnt");
    valueLabel->setScale(0.42f);
    valueLabel->setColor(kit::kValueColor);
    valueLabel->setPosition({width - 82.f, kRowH / 2.f});
    row->addChild(valueLabel);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    row->addChild(menu, 5);

    auto current = std::make_shared<int>(value);
    auto step = [current, valueLabel, low, high, onChange](int delta) {
        *current = std::clamp(*current + delta, low, high);
        valueLabel->setString(std::to_string(*current).c_str());
        if (onChange) onChange(*current);
    };

    struct StepButton {
        char const* text;
        int width;
        int delta;
        float x;
    };
    StepButton const buttons[] = {
        {"-10", 36, -10, width - 148.f},
        {"-1",  30,  -1, width - 116.f},
        {"+1",  30,   1, width -  50.f},
        {"+10", 36,  10, width -  16.f},
    };
    for (auto const& info : buttons) {
        auto* spr = ButtonSprite::create(info.text, info.width, true, "bigFont.fnt",
                                         "GJ_button_04.png", 18.f, 0.40f);
        if (!spr) continue;
        auto* button = CCMenuItemExt::createSpriteExtra(
            spr, [step, delta = info.delta](CCMenuItemSpriteExtra*) { step(delta); });
        button->setPosition({info.x, kRowH / 2.f});
        menu->addChild(button);
    }
    return row;
}

void TemplateEditorPopup::setTextPopupClosed(SetTextPopup*, gd::string text) {
    std::string value(text);
    if (m_remapFrom > 0) {
        int const from = m_remapFrom;
        m_remapFrom = 0;
        int to = geode::utils::numFromString<int>(value).unwrapOr(0);
        if (to < 1 || to > 999) {
            setStatus("El canal destino tiene que estar entre 1 y 999.", kWarn);
            scheduleRebuild();
            return;
        }
        int const changed = edit::remapChannel(m_draft, from, to);
        touch(fmt::format("{} objetos pasados al canal {}.", changed, to));
        return;
    }

    if (value.empty()) return;
    m_draft.name = std::move(value);
    touch("Plantilla renombrada.");
}

void TemplateEditorPopup::save() {
    if (!m_draft.valid()) {
        setStatus("No se puede guardar una plantilla vacia.", kWarn);
        scheduleRebuild();
        return;
    }
    if (m_draft.mode == Mode::Wave && m_draft.links.size() != m_draft.pieces.size()) {
        edit::rebuildLinks(m_draft);
    }
    TemplateStore::get().replace(m_index, m_draft);
    m_dirty = false;
    setStatus("Plantilla guardada.", kOk);
    scheduleRebuild();
}

void TemplateEditorPopup::showHelp() {
    kit::showAbove(FLAlertLayer::create(
        "Editar plantilla",
        "<cy>Plantilla</c> cambia el modo y quita de golpe todo lo de un tipo: "
        "pinchos, adornos, triggers.\n\n"
        "<cy>Piezas</c> abre una pieza para cambiar su <cg>peso</c> (cuanto sale), "
        "duplicarla o borrarla. En modo onda cada pieza tiene ocho "
        "<cl>bordes abiertos</c>: un borde abierto deja que la pieza quede al aire "
        "por ese lado, uno cerrado la obliga a tener vecino.\n\n"
        "<cy>Colores</c> mueve los canales de la plantilla sin tocar los fijos "
        "(1000 en adelante).\n\n"
        "Nada se escribe en disco hasta que pulsas <cg>Guardar</c>.",
        "OK"), this);
}

} // namespace paimon::autobuild
