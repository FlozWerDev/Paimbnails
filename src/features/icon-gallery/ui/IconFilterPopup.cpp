#include "IconFilterPopup.hpp"

#include "SegmentedBar.hpp"
#include "../../icon-maker/ui/IconMakerKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/SimplePlayer.hpp>

#include <algorithm>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;

namespace paimon::icon_gallery {

namespace {

constexpr float kWidth = 350.f;
constexpr float kHeight = 230.f;

// Chip de gamemode: el icono vanilla del modo dentro de un cuadro que se
// enciende al elegirlo. Mucho mas legible que una lista de nombres.
constexpr float kChipSize = 42.f;
constexpr float kChipGap = 6.f;
constexpr int kChipsPerRow = 5;

std::string tr(char const* key) {
    return Localization::get().getString(key);
}

}  // anonymous namespace

IconFilterPopup* IconFilterPopup::create(GalleryStore::Query current, ApplyCallback onApply) {
    auto* popup = new IconFilterPopup();
    if (popup->init(std::move(current), std::move(onApply))) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool IconFilterPopup::init(GalleryStore::Query current, ApplyCallback onApply) {
    if (!Popup::init(kWidth, kHeight)) return false;

    m_query = std::move(current);
    m_onApply = std::move(onApply);

    this->setTitle(tr("icon-gallery.filters.title").c_str());
    this->setID("icon-filter-popup"_spr);
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();
    buildTypes(content.width, content.height - 36.f);
    buildSort(content.width, content.height - 146.f);

    // "Limpiar" deja la tienda como recien abierta; "Aplicar" cierra.
    auto* menu = CCMenu::create();
    menu->setPosition({content.width / 2.f, 22.f});
    m_mainLayer->addChild(menu, 5);

    if (auto* spr = ButtonSprite::create(tr("icon-gallery.filters.clear").c_str(),
                                         "bigFont.fnt", "GJ_button_06.png", 0.55f)) {
        auto* btn = CCMenuItemExt::createSpriteExtra(spr, [this](CCMenuItemSpriteExtra*) {
            m_query.types.clear();
            m_query.sort = GallerySort::Newest;
            apply();
            this->onClose(nullptr);
        });
        btn->setPosition({-56.f, 0.f});
        menu->addChild(btn);
    }
    if (auto* spr = ButtonSprite::create(tr("icon-gallery.filters.apply").c_str(),
                                         "bigFont.fnt", "GJ_button_01.png", 0.55f)) {
        auto* btn = CCMenuItemExt::createSpriteExtra(spr, [this](CCMenuItemSpriteExtra*) {
            apply();
            this->onClose(nullptr);
        });
        btn->setPosition({56.f, 0.f});
        menu->addChild(btn);
    }

    return true;
}

void IconFilterPopup::buildTypes(float width, float top) {
    auto* label = CCLabelBMFont::create(tr("icon-gallery.filters.gamemode").c_str(),
                                        "goldFont.fnt");
    if (label) {
        label->setAnchorPoint({0.f, 0.5f});
        label->setScale(0.5f);
        label->setPosition({22.f, top});
        m_mainLayer->addChild(label, 2);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 3);

    int const count = static_cast<int>(std::size(kGamemodes));
    float const originY = top - 24.f - kChipSize / 2.f;

    for (int i = 0; i < count; ++i) {
        IconType const type = kGamemodes[i];
        int const col = i % kChipsPerRow;
        int const row = i / kChipsPerRow;

        // Las filas incompletas se centran solas (la ultima tiene 4 de 5).
        int const inThisRow = std::min(kChipsPerRow, count - row * kChipsPerRow);
        float const rowW = inThisRow * kChipSize + (inThisRow - 1) * kChipGap;
        float const rowX = (width - rowW) / 2.f + kChipSize / 2.f;

        CCPoint const spot{
            rowX + static_cast<float>(col) * (kChipSize + kChipGap),
            originY - static_cast<float>(row) * (kChipSize + kChipGap),
        };

        bool const on = m_query.types.count(static_cast<int>(type)) > 0;

        auto* face = CCNode::create();
        face->setContentSize({kChipSize, kChipSize});
        face->setAnchorPoint({0.5f, 0.5f});

        // Las dos placas se crean de una vez y solo se alterna cual se ve:
        // repintar dentro del propio callback del boton significaria destruir
        // el menu que el dispatcher todavia esta recorriendo.
        auto* offPanel = paimon::SpriteHelper::createColorPanel(
            kChipSize, kChipSize, {0, 0, 0}, 110, 6.f);
        auto* onPanel = paimon::SpriteHelper::createColorPanel(
            kChipSize, kChipSize, {92, 190, 120}, 220, 6.f);
        for (auto* panel : {offPanel, onPanel}) {
            if (!panel) continue;
            panel->setAnchorPoint({0.f, 0.f});
            panel->setPosition({0.f, 0.f});
            face->addChild(panel);
        }
        if (offPanel) offPanel->setVisible(!on);
        if (onPanel) onPanel->setVisible(on);

        // El icono vanilla del gamemode como cara del chip.
        SimplePlayer* player = SimplePlayer::create(1);
        if (player) {
            player->updatePlayerFrame(1, type);
            player->setScale(0.7f);
            player->setPosition({kChipSize / 2.f, kChipSize / 2.f + 4.f});
            player->setOpacity(on ? 255 : 150);
            face->addChild(player, 1);
        }
        CCLabelBMFont* name = CCLabelBMFont::create(iconTypeLabel(type).c_str(), "chatFont.fnt");
        if (name) {
            name->setAnchorPoint({0.5f, 0.5f});
            name->limitLabelWidth(kChipSize - 6.f, 0.34f, 0.18f);
            name->setPosition({kChipSize / 2.f, 8.f});
            name->setColor(on ? ccColor3B{255, 255, 255} : ccColor3B{170, 190, 215});
            face->addChild(name, 2);
        }

        int const raw = static_cast<int>(type);
        auto* btn = CCMenuItemExt::createSpriteExtra(face,
            [this, raw, offPanel, onPanel, player, name](CCMenuItemSpriteExtra*) {
                bool const nowOn = m_query.types.count(raw) == 0;
                if (nowOn) {
                    m_query.types.insert(raw);
                } else {
                    m_query.types.erase(raw);
                }
                if (offPanel) offPanel->setVisible(!nowOn);
                if (onPanel) onPanel->setVisible(nowOn);
                if (player) player->setOpacity(nowOn ? 255 : 150);
                if (name) {
                    name->setColor(nowOn ? ccColor3B{255, 255, 255}
                                         : ccColor3B{170, 190, 215});
                }
            });
        btn->setPosition(spot);
        menu->addChild(btn);
    }
}

void IconFilterPopup::buildSort(float width, float top) {
    auto* label = CCLabelBMFont::create(tr("icon-gallery.filters.sort").c_str(),
                                        "goldFont.fnt");
    if (label) {
        label->setAnchorPoint({0.f, 0.5f});
        label->setScale(0.5f);
        label->setPosition({22.f, top});
        m_mainLayer->addChild(label, 2);
    }

    m_sortHost = CCNode::create();
    m_sortHost->setContentSize({width - 44.f, ui::kSegmentedBarHeight});
    m_sortHost->setAnchorPoint({0.f, 0.5f});
    m_sortHost->setPosition({22.f, top - 26.f});
    m_mainLayer->addChild(m_sortHost, 3);

    std::vector<std::string> const labels = {
        tr("icon-gallery.sort.newest"),
        tr("icon-gallery.sort.oldest"),
        tr("icon-gallery.sort.name"),
        tr("icon-gallery.sort.author"),
    };
    int const selected = static_cast<int>(m_query.sort);

    auto* tabs = ui::makeSegmentedBar(width - 44.f, labels, selected, [this](int index) {
        m_query.sort = static_cast<GallerySort>(index);
    });
    if (tabs) {
        tabs->setPosition({0.f, 0.f});
        m_sortHost->addChild(tabs);
    }
}

void IconFilterPopup::apply() {
    if (m_onApply) m_onApply(m_query);
}

}  // namespace paimon::icon_gallery
