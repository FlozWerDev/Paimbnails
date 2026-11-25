#include "CommunityShopLayer.hpp"
#include "CommunityBasePopup.hpp"
#include "CommunityItemPopup.hpp"
#include "CommunityWidgets.hpp"
#include "../services/CommunityClient.hpp"
#include "../services/CommunityItems.hpp"
#include "../../global-icon/GlobalIconTypes.hpp"
#include "../../paimon-shop/data/ShopPreset.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <array>
#include <memory>

using namespace geode::prelude;

namespace paimon::community {

namespace {
constexpr int kPageSize = 4;
constexpr std::array<char const*, 4> kTypes = {"presets", "gradients", "icons", "backgrounds"};
constexpr std::array<char const*, 3> kSorts = {"new", "popular", "top"};

void enable(CCMenuItemSpriteExtra* button, bool on) {
    if (!button) return;
    button->setEnabled(on);
    button->setOpacity(on ? 255 : 90);
}

char const* sortLabel(std::string const& sort) {
    if (sort == "popular") return tx("Mas descargados", "Most downloaded");
    if (sort == "top") return tx("Mejor valorados", "Top rated");
    return tx("Nuevos", "Newest");
}

std::string cardTag(matjson::Value const& item, std::string const& type) {
    if (type == "presets") {
        auto category = shop::parseCategory(item["category"].asString().unwrapOr(""));
        return category ? shop::categoryName(category.unwrap()) : "";
    }
    if (type == "gradients" || type == "icons") {
        auto iconType = paimon::globalicon::iconTypeFromString(item["iconType"].asString().unwrapOr(""));
        return iconType ? items::gamemodeName(*iconType) : "";
    }
    return "";
}

char const* emptyText(std::string const& type) {
    if (type == "gradients") return tx("Aun no hay gradientes publicados.", "No gradients published yet.");
    if (type == "icons") return tx("Aun no hay iconos publicados.", "No icons published yet.");
    if (type == "backgrounds") return tx("Aun no hay fondos publicados.", "No backgrounds published yet.");
    return tx("Aun no hay presets publicados.", "No presets published yet.");
}
} // namespace

CommunityShopLayer* CommunityShopLayer::create(std::string const& type) {
    auto layer = std::make_unique<CommunityShopLayer>();
    if (!layer->init(type)) return nullptr;
    layer->autorelease();
    return layer.release();
}

void CommunityShopLayer::open(std::string const& type) {
    if (auto* layer = create(type)) geode::pushSceneWithLayer(layer);
}

bool CommunityShopLayer::init(std::string const& type) {
    if (!CCLayer::init()) return false;
    setID("paimon-community-shop-layer"_spr);
    setKeypadEnabled(true);
    auto size = CCDirector::get()->getWinSize();
    paimon::ui::decorateScene(this, {70, 40, 110});
    paimon::ui::addSceneTitle(this, "Paimon Shop", size.width - 150.f);
    auto* subtitle = paimon::ui::makeLabel(tx("Comunidad: todo lo publico de Paimon Base", "Community: everything public on Paimon Base"),
        size.width - 100.f, 0.3f, paimon::ui::palette::info);
    subtitle->setPosition({size.width / 2.f, size.height - 43.f});
    addChild(subtitle);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    addChild(menu, 5);
    auto* back = paimon::ui::makeBackButton([this] { keyBackClicked(); });
    back->setPosition({26.f, size.height - 24.f});
    back->setScale(0.7f);
    menu->addChild(back);
    // suggesting lives in paimon base; the shop only shows what is public.
    auto* suggest = paimon::ui::makeButton(tx("Sugerir", "Suggest"), [] { CommunityBasePopup::open(); },
        paimon::ui::Btn::Pink, 84.f, 0.4f, "bigFont.fnt");
    suggest->setPosition({size.width - 62.f, size.height - 23.f});
    menu->addChild(suggest);

    float const rowW = size.width - 52.f;
    float const sortW = 120.f;
    float const tabW = (rowW - sortW - 8.f) / kTypes.size();
    for (size_t i = 0; i < kTypes.size(); ++i) {
        std::string id = kTypes[i];
        auto* tab = paimon::ui::makeButton(items::typeName(id), [this, id] { selectType(id); },
            paimon::ui::Btn::Gray, tabW - 6.f, 0.38f, "bigFont.fnt");
        tab->setPosition({26.f + tabW / 2.f + i * tabW, size.height - 68.f});
        menu->addChild(tab);
        m_tabs.emplace_back(id, tab);
    }
    m_sortButton = paimon::ui::makeButton(sortLabel(m_sort), [this] {
        size_t index = 0;
        while (index < kSorts.size() && m_sort != kSorts[index]) ++index;
        m_sort = kSorts[(index + 1) % kSorts.size()];
        m_offset = 0;
        load();
    }, paimon::ui::Btn::Cyan, sortW, 0.34f, "bigFont.fnt");
    m_sortButton->setPosition({size.width - 26.f - sortW / 2.f, size.height - 68.f});
    menu->addChild(m_sortButton);

    m_filterMenu = CCMenu::create();
    m_filterMenu->setPosition({0.f, 0.f});
    addChild(m_filterMenu, 5);

    m_cards = CCMenu::create();
    m_cards->setPosition({0.f, 0.f});
    addChild(m_cards, 4);

    m_message = paimon::ui::makeLabel("", size.width - 110.f, 0.45f, paimon::ui::palette::muted);
    m_message->setPosition({size.width / 2.f, (size.height - 68.f) / 2.f});
    addChild(m_message, 6);
    m_status = paimon::ui::makeLabel("", size.width - 160.f, 0.32f, paimon::ui::palette::muted);
    m_status->setPosition({size.width / 2.f, 27.f});
    addChild(m_status);
    m_prev = paimon::ui::makeButton("<", [this] {
        if (m_offset == 0) return;
        m_offset = std::max(0, m_offset - kPageSize);
        load();
    }, paimon::ui::Btn::Gray, 35.f, 0.4f);
    m_prev->setPosition({44.f, 27.f});
    menu->addChild(m_prev);
    m_next = paimon::ui::makeButton(">", [this] {
        if (!m_hasMore) return;
        m_offset += kPageSize;
        load();
    }, paimon::ui::Btn::Gray, 35.f, 0.4f);
    m_next->setPosition({size.width - 44.f, 27.f});
    menu->addChild(m_next);

    bool known = false;
    for (auto const* id : kTypes) known = known || type == id;
    selectType(known ? type : "presets");
    return true;
}

void CommunityShopLayer::selectType(std::string const& type) {
    m_type = type;
    m_filter.clear();
    m_offset = 0;
    for (auto const& [id, tab] : m_tabs) {
        paimon::ui::setButtonSkin(tab, id == m_type ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    buildFilters();
    load();
}

void CommunityShopLayer::buildFilters() {
    m_filterMenu->removeAllChildren();
    m_filters.clear();
    auto size = CCDirector::get()->getWinSize();
    std::vector<std::pair<std::string, std::string>> options;
    if (m_type == "presets") {
        options.emplace_back("", tx("Todos", "All"));
        for (int i = 0; i < 4; ++i) {
            auto category = static_cast<shop::Category>(i);
            options.emplace_back(shop::categoryId(category), shop::categoryName(category));
        }
    } else if (m_type == "gradients" || m_type == "icons") {
        options.emplace_back("", tx("Todos", "All"));
        for (auto type : items::gamemodes()) {
            options.emplace_back(std::string(paimon::globalicon::iconTypeToString(type)), items::gamemodeName(type));
        }
    }
    if (options.empty()) {
        auto* hint = paimon::ui::makeLabel(tx("Toca un fondo para usarlo en tus comentarios.", "Tap a background to use it for your comments."),
            size.width - 80.f, 0.32f, paimon::ui::palette::dim);
        hint->setPosition({size.width / 2.f, size.height - 98.f});
        m_filterMenu->addChild(hint);
        return;
    }
    float const slot = (size.width - 52.f) / options.size();
    std::vector<CCMenuItemSpriteExtra*> buttons;
    for (size_t i = 0; i < options.size(); ++i) {
        auto id = options[i].first;
        auto* button = paimon::ui::makeButton(options[i].second.c_str(), [this, id] {
            m_filter = id;
            m_offset = 0;
            load();
        }, paimon::ui::Btn::Gray, slot - 5.f, 0.32f, "bigFont.fnt");
        button->setPosition({26.f + slot / 2.f + i * slot, size.height - 98.f});
        m_filterMenu->addChild(button);
        m_filters.emplace_back(id, button);
        buttons.push_back(button);
    }
    paimon::ui::matchButtonLabels(buttons);
}

void CommunityShopLayer::load() {
    int token = ++m_token;
    m_cards->removeAllChildren();
    m_message->setString(tx("Cargando...", "Loading..."));
    m_message->setVisible(true);
    for (auto const& [id, button] : m_filters) {
        paimon::ui::setButtonSkin(button, id == m_filter ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    if (auto* sprite = typeinfo_cast<ButtonSprite*>(m_sortButton->getNormalImage())) sprite->setString(sortLabel(m_sort));
    enable(m_prev, false);
    enable(m_next, false);
    m_status->setString(fmt::format("{}  |  {} {}  |  {}", items::typeName(m_type), tx("pagina", "page"),
        m_offset / kPageSize + 1, tx("COMUNIDAD", "COMMUNITY")).c_str());
    WeakRef<CommunityShopLayer> weak = this;
    CommunityClient::get().listItems(m_type, m_filter, m_sort, m_offset, kPageSize,
        [weak, token](bool ok, matjson::Value const& json, std::string const& error) {
            auto self = weak.lock();
            if (!self || token != self->m_token || !self->getParent()) return;
            if (!ok) { self->m_message->setString(error.c_str()); return; }
            self->m_hasMore = json["hasMore"].asBool().unwrapOr(false);
            enable(self->m_prev, self->m_offset > 0);
            enable(self->m_next, self->m_hasMore);
            auto const& list = json["items"];
            if (!list.isArray() || list.size() == 0) {
                self->m_message->setString(self->m_offset > 0 ? tx("No hay mas.", "Nothing more.") : emptyText(self->m_type));
                return;
            }
            self->m_message->setVisible(false);
            self->showItems(list);
        });
}

void CommunityShopLayer::showItems(matjson::Value const& list) {
    auto win = CCDirector::get()->getWinSize();
    float const width = (win.width - 64.f) / 2.f;
    float const height = (win.height - 184.f) / 2.f;
    bool const square = m_type == "gradients" || m_type == "icons";
    int index = 0;
    for (auto const& item : list) {
        if (index >= kPageSize) break;
        auto* face = CCNode::create();
        face->setContentSize({width, height});
        face->addChild(paimon::ui::makeInset({width, height}, 125));
        float const previewW = square ? height - 12.f : std::min(102.f, width * 0.43f);
        // each card keeps its own gradient shader slot.
        auto* preview = makeItemPreview(item, {previewW, height - 12.f}, index);
        preview->setPosition({6.f, 6.f});
        face->addChild(preview);
        float const left = previewW + 15.f;
        float const textW = width - left - 8.f;
        auto addLabel = [face, left](std::string const& value, float y, float maxW, float scale, ccColor3B color) {
            auto* label = paimon::ui::makeLabel(value.c_str(), maxW, scale, color);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({left, y});
            face->addChild(label);
            return label;
        };
        addLabel(item["title"].asString().unwrapOr(""), height - 16.f, textW, 0.34f, paimon::ui::palette::text);
        auto tag = cardTag(item, m_type);
        float const tagW = tag.empty() ? 0.f : std::min(80.f, textW * 0.45f);
        addLabel(item["username"].asString().unwrapOr(""), height - 32.f, textW - tagW - 6.f, 0.28f, paimon::ui::palette::gold);
        if (!tag.empty()) {
            auto* tagLabel = paimon::ui::makeLabel(tag.c_str(), tagW, 0.26f, paimon::ui::palette::info);
            tagLabel->setAnchorPoint({1.f, 0.5f});
            tagLabel->setPosition({width - 8.f, height - 32.f});
            face->addChild(tagLabel);
        }
        auto* counters = makeCounters(static_cast<int>(item["likes"].asInt().unwrapOr(0)),
            static_cast<int>(item["dislikes"].asInt().unwrapOr(0)), static_cast<int>(item["downloads"].asInt().unwrapOr(0)), 0.3f);
        if (counters->getContentWidth() > textW) counters->setScale(textW / counters->getContentWidth());
        counters->setPosition({left, 15.f});
        face->addChild(counters);
        int const myVote = static_cast<int>(item["myVote"].asInt().unwrapOr(0));
        if (myVote != 0) {
            auto* mark = paimon::ui::makeLabel(myVote > 0 ? tx("Te gusta", "Liked") : tx("No te gusta", "Disliked"), 60.f, 0.22f,
                myVote > 0 ? paimon::ui::palette::success : paimon::ui::palette::danger);
            mark->setAnchorPoint({1.f, 0.5f});
            mark->setPosition({width - 8.f, 15.f});
            face->addChild(mark);
        }
        WeakRef<CommunityShopLayer> weak = this;
        auto* button = CCMenuItemExt::createSpriteExtra(face, [weak, item](auto*) {
            if (auto self = weak.lock()) self->openItem(item);
        });
        button->m_scaleMultiplier = 1.03f;
        int const col = index % 2, row = index / 2;
        button->setPosition({26.f + width / 2.f + col * (width + 12.f), win.height - 122.f - height / 2.f - row * (height + 10.f)});
        m_cards->addChild(button);
        index++;
    }
}

void CommunityShopLayer::openItem(matjson::Value const& item) {
    WeakRef<CommunityShopLayer> weak = this;
    auto* popup = CommunityItemPopup::create(item, [weak] {
        if (auto self = weak.lock(); self && self->getParent()) self->load();
    });
    if (popup) showOnTop(popup);
}

void CommunityShopLayer::keyBackClicked() { CCDirector::get()->popScene(); }

} // namespace paimon::community
