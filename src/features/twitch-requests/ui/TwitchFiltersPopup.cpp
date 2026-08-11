#include "TwitchFiltersPopup.hpp"

#include "../TwitchRequestFilters.hpp"
#include "../TwitchRequestManager.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace cocos2d;
using namespace geode::prelude;

namespace kit = paimon::configkit;

namespace paimon::twitch {

namespace {

constexpr ccColor3B kTwitch = {145, 90, 255};
constexpr ccColor3B kOnColor = {255, 255, 255};
constexpr ccColor3B kOffColor = {96, 102, 120};

int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

void applyFilters(std::function<void(RequestFilters&)> const& change) {
    auto& manager = TwitchRequestManager::get();
    auto filters = manager.filters();
    change(filters);
    manager.setFilters(filters);
}

// Marcado = color normal; sin marcar = gris y translucido.
void tintIcon(CCNode* node, bool on) {
    if (!node) return;
    if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        sprite->setColor(on ? kOnColor : kOffColor);
        sprite->setOpacity(on ? 255 : 110);
    } else if (auto* label = typeinfo_cast<CCLabelBMFont*>(node)) {
        label->setColor(on ? kOnColor : kOffColor);
        label->setOpacity(on ? 255 : 110);
    }
    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) tintIcon(child, on);
    }
}

CCLabelBMFont* makeTitle(char const* text, float maxWidth) {
    auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setAnchorPoint({0.f, 1.f});
    label->setColor(kit::kTitleColor);
    label->limitLabelWidth(maxWidth, 0.40f, 0.1f);
    return label;
}

CCLabelBMFont* makeDesc(char const* text, float wrapWidth) {
    constexpr float kScale = 0.46f;
    auto* label = CCLabelBMFont::create(text, "chatFont.fnt", wrapWidth / kScale,
        kCCTextAlignmentLeft);
    label->setScale(kScale);
    label->setAnchorPoint({0.f, 1.f});
    label->setColor(kit::kDescColor);
    label->setOpacity(230);
    return label;
}

// Etiqueta suelta con area de toque comoda, para los chips de longitud.
CCNode* makeChip(char const* text) {
    auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
    label->setScale(0.5f);

    auto* holder = CCNode::create();
    auto const size = label->getScaledContentSize();
    holder->setContentSize({size.width + 12.f, 26.f});
    holder->setAnchorPoint({0.5f, 0.5f});
    holder->ignoreAnchorPointForPosition(false);
    label->setPosition(holder->getContentSize() / 2.f);
    holder->addChild(label);
    return holder;
}

std::string formatPerUser(double value) {
    int const count = static_cast<int>(std::lround(value));
    if (count == 0) return "Sin limite";
    return count == 1 ? "1 nivel" : fmt::format("{} niveles", count);
}

std::string formatCooldown(double value) {
    int const seconds = static_cast<int>(std::lround(value));
    if (seconds == 0) return "Apagado";
    if (seconds < 60) return fmt::format("{} s", seconds);
    if (seconds % 60 == 0) return fmt::format("{} min", seconds / 60);
    return fmt::format("{}m {}s", seconds / 60, seconds % 60);
}

// Fila de opciones marcables: los iconos ya vienen creados y sin escalar.
CCNode* makeMultiRow(
    float width,
    char const* title,
    char const* desc,
    std::vector<CCNode*> icons,
    uint32_t mask,
    std::function<void(uint32_t)> onChange
) {
    constexpr float kPad = 6.f;
    constexpr float kGap = 4.f;

    auto* titleLabel = makeTitle(title, width - 20.f);
    auto* descLabel = desc && desc[0] != '\0' ? makeDesc(desc, width - 20.f) : nullptr;

    float iconWidth = 0.f;
    float iconHeight = 0.f;
    for (auto* icon : icons) {
        iconWidth = std::max(iconWidth, icon->getContentSize().width);
        iconHeight = std::max(iconHeight, icon->getContentSize().height);
    }

    float const count = static_cast<float>(std::max<size_t>(icons.size(), 1));
    float const room = width - 16.f - kGap * (count - 1.f);
    float const scale = std::min(0.9f, room / (count * std::max(iconWidth, 1.f)));
    float const bandHeight = iconHeight * scale + 4.f;

    float const titleHeight = titleLabel->getScaledContentSize().height;
    float const descHeight = descLabel ? descLabel->getScaledContentSize().height + 2.f : 0.f;
    float const rowHeight = kPad + titleHeight + descHeight + 4.f + bandHeight + kPad;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, rowHeight});

    titleLabel->setPosition({10.f, rowHeight - kPad});
    row->addChild(titleLabel);
    if (descLabel) {
        descLabel->setPosition({10.f, rowHeight - kPad - titleHeight - 1.f});
        row->addChild(descLabel);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({width / 2.f, bandHeight / 2.f + kPad - 2.f});
    menu->setContentSize({width - 12.f, bandHeight});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu, 5);

    auto state = std::make_shared<uint32_t>(mask);
    auto items = std::make_shared<std::vector<CCNode*>>();
    auto restyle = [state, items] {
        for (size_t slot = 0; slot < items->size(); ++slot) {
            bool const on = (*state == 0) || (*state & (1u << slot));
            tintIcon((*items)[slot], on);
        }
    };

    for (size_t slot = 0; slot < icons.size(); ++slot) {
        auto* icon = icons[slot];
        icon->setScale(scale);
        items->push_back(icon);

        auto* button = CCMenuItemExt::createSpriteExtra(icon,
            [state, items, restyle, onChange, slot](CCMenuItemSpriteExtra*) {
                uint32_t const all = (1u << items->size()) - 1;
                // Vacio se dibuja como todo marcado, asi que aqui vale lo mismo.
                uint32_t next = (*state == 0 ? all : *state & all) ^ (1u << slot);
                if (next == 0) next = all;
                *state = next;
                restyle();
                if (onChange) onChange(next);
            });
        menu->addChild(button);
    }

    menu->setLayout(RowLayout::create()
        ->setGap(kGap)
        ->setAutoScale(false)
        ->setAxisAlignment(AxisAlignment::Center));
    restyle();
    return row;
}

class AddVideoRulePopup : public geode::Popup {
public:
    static AddVideoRulePopup* create(std::function<void()> onAdded) {
        auto* ret = new AddVideoRulePopup();
        if (ret->init(onAdded)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

protected:
    bool init(std::function<void()> onAdded) {
        if (!Popup::init(400.f, 250.f)) return false;
        m_onAdded = std::move(onAdded);
        setTitle("Agregar regla de video");
        paimon::markDynamicPopup(this);

        auto const content = m_mainLayer->getContentSize();
        float const innerW = kit::cardInnerWidth(content.width - 24.f);

        std::vector<CCNode*> faces;
        for (int slot = 0; slot < kDifficultySlotCount; ++slot) {
            CCNode* icon = GJDifficultySprite::create(
                difficultySlotSprite(slot), GJDifficultyName::Short);
            if (!icon) icon = makeChip(difficultySlotName(slot));
            faces.push_back(icon);
        }

        std::vector<CCNode*> rows;
        rows.push_back(kit::makeSelectRow(
            innerW,
            "Tipo de nivel",
            "Clasico, Plataformas o Todos.",
            modeFilterNames(), static_cast<int>(m_mode),
            [this](int index) {
                m_mode = static_cast<ModeFilter>(std::clamp(index, 0, kModeFilterCount - 1));
            }
        ));

        rows.push_back(makeMultiRow(
            innerW,
            "Dificultades",
            "Dificultades que exigiran enlace a video.",
            std::move(faces), m_difficulties,
            [this](uint32_t mask) {
                m_difficulties = mask;
            }
        ));

        rows.push_back(kit::makeButtonRow(
            innerW,
            "Guardar regla",
            "Agregar regla a los filtros.",
            "Agregar",
            [this] {
                applyFilters([this](RequestFilters& filters) {
                    VideoRequirementRule rule;
                    rule.mode = m_mode;
                    rule.difficulties = m_difficulties;
                    filters.videoRules.push_back(rule);
                });
                if (m_onAdded) m_onAdded();
                onClose(nullptr);
            }
        ));

        auto* card = kit::makeCard(content.width - 24.f, "Nueva regla", {255, 120, 150}, rows);
        card->setPosition({12.f, 15.f});
        m_mainLayer->addChild(card);

        return true;
    }

    ModeFilter m_mode = ModeFilter::All;
    uint32_t m_difficulties = (1u << 6);
    std::function<void()> m_onAdded;
};

} // namespace

TwitchFiltersPopup* TwitchFiltersPopup::create() {
    auto* ret = new TwitchFiltersPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool TwitchFiltersPopup::init() {
    if (!Popup::init(420.f, 280.f)) return false;
    setTitle("Filtros de requests");
    paimon::markDynamicPopup(this);
    rebuild();
    return true;
}

void TwitchFiltersPopup::rebuild() {
    if (m_scroll) {
        m_scroll->removeFromParent();
        m_scroll = nullptr;
    }

    auto const filters = TwitchRequestManager::get().filters();
    auto const content = m_mainLayer->getContentSize();
    float const scrollW = content.width - 24.f;
    float const scrollH = content.height - 44.f;
    float const innerW = kit::cardInnerWidth(scrollW);

    std::vector<CCNode*> faces;
    for (int slot = 0; slot < kDifficultySlotCount; ++slot) {
        // Short: la unica variante que existe para NA y Auto, y en demon dice
        // "Demon" en vez de un tier concreto.
        CCNode* icon = GJDifficultySprite::create(
            difficultySlotSprite(slot), GJDifficultyName::Short);
        // El indice es el bit, asi que ningun slot puede faltar.
        if (!icon) icon = makeChip(difficultySlotName(slot));
        faces.push_back(icon);
    }

    std::vector<CCNode*> chips;
    for (int slot = 0; slot < kLengthSlotCount; ++slot) {
        chips.push_back(makeChip(lengthSlotName(slot)));
    }

    std::vector<CCNode*> rows;
    rows.push_back(kit::makeSelectRow(
        innerW,
        "Modo",
        "Solo clasicos, solo plataformas o los dos.",
        modeFilterNames(), static_cast<int>(filters.mode),
        [](int index) {
            applyFilters([index](RequestFilters& filters) {
                filters.mode = static_cast<ModeFilter>(
                    std::clamp(index, 0, kModeFilterCount - 1));
            });
        }
    ));

    rows.push_back(makeMultiRow(
        innerW,
        "Dificultad",
        "Sin rate usa las estrellas pedidas; con rate usa la dificultad actual.",
        std::move(faces), filters.difficulties,
        [](uint32_t mask) {
            applyFilters([mask](RequestFilters& filters) { filters.difficulties = mask; });
        }
    ));

    rows.push_back(makeMultiRow(
        innerW,
        "Longitud",
        "Solo cuenta en niveles clasicos.",
        std::move(chips), filters.lengths,
        [](uint32_t mask) {
            applyFilters([mask](RequestFilters& filters) { filters.lengths = mask; });
        }
    ));

    std::vector<CCNode*> items;
    items.push_back(kit::makeCard(scrollW, "Que niveles aceptas", kTwitch, rows));

    std::vector<CCNode*> videoRows;
    for (size_t i = 0; i < filters.videoRules.size(); ++i) {
        auto const& rule = filters.videoRules[i];
        videoRows.push_back(kit::makeButtonRow(
            innerW,
            fmt::format("Regla #{}", i + 1).c_str(),
            videoRuleSummary(rule).c_str(),
            "Quitar",
            [this, i] {
                applyFilters([i](RequestFilters& filters) {
                    if (i < filters.videoRules.size()) {
                        filters.videoRules.erase(filters.videoRules.begin() + i);
                    }
                });
                this->rebuild();
            }
        ));
    }

    videoRows.push_back(kit::makeButtonRow(
        innerW,
        "Agregar regla de video",
        "Exige video obligatorio para tipos o dificultades concretas.",
        "+",
        [this] {
            AddVideoRulePopup::create([this] {
                this->rebuild();
            })->show();
        }
    ));

    items.push_back(kit::makeCard(scrollW, "Video obligatorio", {255, 120, 150}, videoRows));

    items.push_back(kit::makeCard(scrollW, "Limites por usuario", {255, 175, 95}, {
        kit::makeToggleRow(
            innerW,
            "Solo usuarios verificados",
            "Exige una cuenta de GD verificada por la pagina web.",
            filters.verifiedOnly,
            [](bool value) {
                applyFilters([value](RequestFilters& filters) {
                    filters.verifiedOnly = value;
                });
            }
        ),
        kit::makeSliderRow(
            innerW,
            "Niveles por usuario",
            "Maximo de pedidos pendientes a la vez.",
            filters.maxPerUser, 0.0, 20.0,
            formatPerUser,
            [](double value) {
                int const count = static_cast<int>(std::lround(value));
                applyFilters([count](RequestFilters& filters) {
                    filters.maxPerUser = count;
                });
            }
        ),
        kit::makeSliderRow(
            innerW,
            "Cooldown por usuario",
            "Espera entre dos pedidos aceptados de la misma persona.",
            filters.cooldownSeconds, 0.0, 300.0,
            formatCooldown,
            [](double value) {
                int const seconds = static_cast<int>(std::lround(value));
                applyFilters([seconds](RequestFilters& filters) {
                    filters.cooldownSeconds = seconds;
                });
            }
        ),
        kit::makeToggleRow(
            innerW,
            "Bloquear niveles repetidos",
            "No deja que la misma ID entre dos veces en la cola.",
            filters.blockDuplicates,
            [](bool value) {
                applyFilters([value](RequestFilters& filters) {
                    filters.blockDuplicates = value;
                });
            }
        ),
    }));

    items.push_back(kit::makeCard(scrollW, "Requests", {115, 215, 255}, {
        kit::makeButtonRow(
            innerW,
            "Quitar los ocultos",
            fmt::format("Ahora hay {} pedidos fuera del filtro.",
                TwitchRequestManager::get().filteredCount()).c_str(),
            "Quitar",
            [this] { this->onRemoveFiltered(); }
        ),
    }));

    items.push_back(kit::makeHint(
        scrollW,
        "Lo que no encaja se queda guardado pero no se ve en la lista, y Jugar siguiente lo salta. "
        "Mientras un nivel se esta cargando siempre se muestra, porque todavia no sabemos como es."
    ));
    items.push_back(kit::makeHint(
        scrollW,
        "Los chats no pueden comprobar una cuenta de Geometry Dash. Si activas Solo usuarios "
        "verificados, solo entran pedidos verificados desde tu pagina web."
    ));

    m_scroll = kit::makeScrollStack({scrollW, scrollH}, items);
    m_scroll->setPosition({12.f, 8.f});
    m_mainLayer->addChild(m_scroll);
}

void TwitchFiltersPopup::onRemoveFiltered() {
    size_t const removed = TwitchRequestManager::get().removeFiltered();
    if (removed == 0) {
        PaimonNotify::show("No hay pedidos ocultos", NotificationIcon::Info);
        return;
    }
    PaimonNotify::show(
        fmt::format("{} pedidos quitados", removed), NotificationIcon::Success);

    // Nunca desde el callback: rebuild() borra el menu que se esta tocando.
    Ref<TwitchFiltersPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self && self->getParent()) self->rebuild();
    });
}

} // namespace paimon::twitch
