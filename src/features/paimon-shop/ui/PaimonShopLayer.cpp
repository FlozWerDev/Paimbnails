#include "PaimonShopLayer.hpp"
#include "ShopCreatePopup.hpp"
#include "ShopPresetPopup.hpp"
#include "ShopUI.hpp"
#include "../services/ShopConfiguration.hpp"
#include "../../community-base/services/CommunityClient.hpp"
#include "../../community-base/ui/CommunityShopLayer.hpp"
#include "../../../utils/Base64.hpp"
#include "../../../utils/BoundedFileRead.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>

using namespace geode::prelude;

namespace paimon::shop {
namespace {
constexpr size_t kPageSize = 4;
// room for the page arrows at both sides, like gd's level browser.
constexpr float kSideMargin = 48.f;
constexpr float kColumnGap = 12.f;
constexpr float kRowGap = 10.f;
// rows, measured from the top of the screen.
constexpr float kToolbarY = 68.f;
constexpr float kChipsY = 94.f;
constexpr float kCardsTop = 110.f;
constexpr float kCardsBottom = 42.f;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

float cardsMiddle(CCSize win) {
    return (win.height - kCardsTop + kCardsBottom) / 2.f;
}

ccColor3B categoryColor(Category category) {
    switch (category) {
        case Category::Layout: return paimon::ui::palette::info;
        case Category::RTX: return paimon::ui::palette::violet;
        case Category::BeatShaders: return paimon::ui::palette::warning;
        case Category::Pack: return paimon::ui::palette::success;
    }
    return paimon::ui::palette::info;
}

// civil date of a unix time (utc), without localtime: it is neither thread safe nor portable.
std::string dateText(int64_t seconds) {
    int64_t const z = seconds / 86400 + 719468;
    int64_t const era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t const doe = z - era * 146097;
    int64_t const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t const mp = (5 * doy + 2) / 153;
    int64_t const day = doy - (153 * mp + 2) / 5 + 1;
    int64_t const month = mp < 10 ? mp + 3 : mp - 9;
    int64_t const year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    return fmt::format("{:02}/{:02}/{}", day, month, year);
}

std::string ageText(int64_t createdAt) {
    if (createdAt <= 0) return {};
    auto const now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto const days = (now - createdAt) / 86400;
    // imported presets can come from a clock ahead of this one.
    if (days <= 0) return text("Hoy", "Today");
    if (days == 1) return text("Ayer", "Yesterday");
    if (days < 30) return fmt::format(fmt::runtime(text("Hace {} dias", "{} days ago")), days);
    return dateText(createdAt);
}
}

PaimonShopLayer* PaimonShopLayer::create() {
    auto layer = std::make_unique<PaimonShopLayer>();
    if (!layer->init()) return nullptr;
    layer->autorelease();
    return layer.release();
}

void PaimonShopLayer::open() {
    if (auto* layer = create()) geode::pushSceneWithLayer(layer);
}

PaimonShopLayer::~PaimonShopLayer() { paimon::ui::detachGeodeTextInput(m_search); }

bool PaimonShopLayer::init() {
    if (!CCLayer::init()) return false;
    setID("paimon-shop-layer"_spr);
    setKeypadEnabled(true);
    auto size = CCDirector::get()->getWinSize();
    float const left = kSideMargin;
    float const right = size.width - kSideMargin;

    paimon::ui::decorateScene(this, {38, 50, 100});
    paimon::ui::addSceneTitle(this, "Paimon Shop", size.width - 260.f);
    auto* subtitle = paimon::ui::makeLabel(text("Biblioteca local de configuraciones", "Local configuration library"),
        size.width - 260.f, 0.3f, paimon::ui::palette::info);
    subtitle->setPosition({size.width / 2.f, size.height - 42.f});
    addChild(subtitle);

    auto* menu = paimon::ui::makeZeroMenu();
    addChild(menu, 5);
    auto* back = paimon::ui::makeBackButton([this] { keyBackClicked(); });
    back->setPosition({26.f, size.height - 24.f});
    back->setScale(0.7f);
    menu->addChild(back);
    // paimon base: everything public (presets, gradients, icons, backgrounds). same corner as
    // the community layer's own action, so both screens read alike.
    auto* community = paimon::ui::makeButton(text("Comunidad", "Community"), [] {
        paimon::community::CommunityShopLayer::open();
    }, paimon::ui::Btn::Pink, 92.f, 0.4f, "bigFont.fnt");
    community->setPosition({size.width - 62.f, size.height - 23.f});
    menu->addChild(community);

    // toolbar: search on the left, library actions on the right.
    float const toolbarY = size.height - kToolbarY;
    float x = right;
    auto addAction = [&](char const* label, std::function<void()> onPress, paimon::ui::Btn skin, float width) {
        auto* button = paimon::ui::makeButton(label, std::move(onPress), skin, width, 0.62f, "bigFont.fnt");
        button->setPosition({x - width / 2.f, toolbarY});
        menu->addChild(button);
        x -= width + 6.f;
        return button;
    };
    m_restore = addAction(text("Restaurar", "Restore"), [this] { restoreBackup(); }, paimon::ui::Btn::Gray, 78.f);
    auto* import = addAction(text("Importar", "Import"), [this] { importPreset(); }, paimon::ui::Btn::Cyan, 72.f);
    auto* create = addAction(text("+ Crear", "+ Create"), [this] { createPreset(); }, paimon::ui::Btn::Green, 72.f);
    paimon::ui::matchButtonLabels({create, import, m_restore});

    constexpr float kSearchScale = 0.66f;
    float const searchW = x - 2.f - left;
    m_search = TextInput::create(searchW / kSearchScale, text("Buscar por nombre o autor...", "Search by name or author..."), "chatFont.fnt");
    m_search->setCommonFilter(CommonFilter::Text);
    m_search->setMaxCharCount(48);
    m_search->setScale(kSearchScale);
    m_search->setPosition({left + searchW / 2.f, toolbarY});
    addChild(m_search, 6);
    WeakRef<PaimonShopLayer> weak = this;
    m_search->setCallback([weak](std::string const&) { if (auto self = weak.lock()) { self->m_page = 0; self->refresh(); } });

    // category chips across the content width.
    float const slot = (right - left) / 5.f;
    for (int i = -1; i < 4; ++i) {
        auto* chip = paimon::ui::makeButton(i < 0 ? text("Todos", "All") : categoryName(static_cast<Category>(i)), [this, i] {
            if (m_filter == i) return;
            m_filter = i;
            m_page = 0;
            refresh(true);
        }, paimon::ui::Btn::Gray, slot - 6.f, 0.42f, "bigFont.fnt");
        chip->setPosition({left + slot * (i + 1.5f), size.height - kChipsY});
        menu->addChild(chip);
        m_filters.push_back(chip);
    }

    m_content = CCNode::create();
    addChild(m_content, 4);
    m_cards = paimon::ui::makeZeroMenu();
    addChild(m_cards, 5);

    m_status = paimon::ui::makeLabel("", size.width - 200.f, 0.3f, paimon::ui::palette::muted);
    m_status->setPosition({size.width / 2.f, 22.f});
    addChild(m_status);

    float const middle = cardsMiddle(size);
    m_previous = paimon::ui::makeFrameButton("GJ_arrow_03_001.png", 0.8f, [this] {
        if (m_page) { --m_page; refresh(true); }
    });
    m_previous->setPosition({24.f, middle});
    menu->addChild(m_previous);
    m_next = paimon::ui::makeFrameButton("GJ_arrow_03_001.png", 0.8f, [this] { ++m_page; refresh(true); });
    if (auto* arrow = typeinfo_cast<CCSprite*>(m_next->getNormalImage())) arrow->setFlipX(true);
    m_next->setPosition({size.width - 24.f, middle});
    menu->addChild(m_next);

    refresh(true);
    return true;
}

std::function<void()> PaimonShopLayer::refreshCallback() {
    WeakRef<PaimonShopLayer> weak = this;
    return [weak] { if (auto self = weak.lock(); self && self->getParent()) self->refresh(); };
}

void PaimonShopLayer::refresh(bool animate) {
    m_cards->removeAllChildren();
    m_content->removeAllChildren();
    {
        bool const restoreEnabled = hasConfigurationBackup();
        m_restore->setEnabled(restoreEnabled);
        m_restore->setOpacity(restoreEnabled ? 255 : 90);
    }
    for (size_t i = 0; i < m_filters.size(); ++i) {
        paimon::ui::setButtonSkin(m_filters[i], static_cast<int>(i) - 1 == m_filter ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    paimon::ui::matchButtonLabels(m_filters);
    // gd hides page arrows that lead nowhere.
    m_previous->setVisible(false);
    m_next->setVisible(false);

    auto entries = localLibrary().list();
    if (!entries) {
        m_status->setString("");
        showEmptyState(EmptyKind::Error, entries.unwrapErr());
        return;
    }
    auto& listings = entries.unwrap();
    size_t const total = listings.size();
    auto query = lower(m_search->getString());
    std::erase_if(listings, [this, &query](Listing const& item) {
        return (m_filter >= 0 && static_cast<int>(item.category) != m_filter)
            || (!query.empty() && lower(item.title + " " + item.author).find(query) == std::string::npos);
    });
    std::stable_sort(listings.begin(), listings.end(), [](auto const& a, auto const& b) { return a.createdAt > b.createdAt; });
    size_t pages = std::max(size_t{1}, (listings.size() + kPageSize - 1) / kPageSize);
    m_page = std::min(m_page, pages - 1);
    m_previous->setVisible(m_page > 0);
    m_next->setVisible(m_page + 1 < pages);

    bool const filtered = m_filter >= 0 || !query.empty();
    auto counted = filtered ? fmt::format("{} / {} presets", listings.size(), total) : fmt::format("{} presets", total);
    m_status->setString(fmt::format("{}  |  {} {} / {}", counted, text("pagina", "page"), m_page + 1, pages).c_str());

    if (listings.empty()) {
        showEmptyState(total > 0 ? EmptyKind::NoResults : EmptyKind::Library);
        return;
    }

    auto win = CCDirector::get()->getWinSize();
    float const top = win.height - kCardsTop;
    CCSize const card{(win.width - 2.f * kSideMargin - kColumnGap) / 2.f, (top - kCardsBottom - kRowGap) / 2.f};
    for (size_t i = m_page * kPageSize; i < std::min(listings.size(), (m_page + 1) * kPageSize); ++i) {
        auto slot = i - m_page * kPageSize;
        auto* button = makeCard(listings[i], card);
        button->setPosition({kSideMargin + card.width / 2.f + (slot % 2) * (card.width + kColumnGap),
            top - card.height / 2.f - (slot / 2) * (card.height + kRowGap)});
        m_cards->addChild(button);
        if (animate) paimon::ui::animateIn(button, 0.03f * static_cast<float>(slot));
    }
}

CCMenuItemSpriteExtra* PaimonShopLayer::makeCard(Listing const& entry, CCSize size) {
    auto* face = CCNode::create();
    face->setContentSize(size);
    face->addChild(paimon::ui::makeInset(size, 130));
    auto preset = localLibrary().read(entry.id);
    float const previewW = std::min(112.f, size.width * 0.46f);
    auto* preview = makePreview(preset ? preset.unwrap().preview : "", {previewW, size.height - 12.f});
    preview->setPosition({6.f, 6.f});
    face->addChild(preview);

    float const left = previewW + 14.f;
    float const textW = size.width - left - 8.f;
    auto addLabel = [face, left, textW](std::string const& value, float y, float scale, ccColor3B color) {
        auto* label = paimon::ui::makeLabel(value.c_str(), textW, scale, color);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({left, y});
        face->addChild(label);
    };
    addLabel(entry.title, size.height - 16.f, 0.36f, paimon::ui::palette::text);
    addLabel(fmt::format("{} {}", text("por", "by"), entry.author), size.height - 31.f, 0.26f, paimon::ui::palette::gold);
    if (auto age = ageText(entry.createdAt); !age.empty()) {
        addLabel(age, size.height - 44.f, 0.24f, paimon::ui::palette::dim);
    }

    // category pill, tinted with the category's own color.
    auto const color = categoryColor(entry.category);
    auto* tag = paimon::ui::makeLabel(categoryName(entry.category), textW - 12.f, 0.24f, color);
    float const pillW = tag->getScaledContentSize().width + 12.f;
    auto* pill = paimon::ui::makeInset({pillW, 15.f}, 70, color);
    pill->setPosition({left, 7.f});
    face->addChild(pill);
    tag->setPosition({left + pillW / 2.f, 14.5f});
    face->addChild(tag);

    auto* button = CCMenuItemExt::createSpriteExtra(face, [weak = WeakRef(this), id = entry.id](auto*) {
        if (auto self = weak.lock()) self->openPreset(id);
    });
    button->m_scaleMultiplier = 1.03f;
    return button;
}

void PaimonShopLayer::showEmptyState(EmptyKind kind, std::string const& detail) {
    auto size = CCDirector::get()->getWinSize();
    float const middle = cardsMiddle(size);
    float const centerX = size.width / 2.f;

    auto* mascot = CCSprite::create("paim_Paimon.png"_spr);
    if (mascot && !mascot->isUsingFallback()) {
        float const height = mascot->getContentSize().height;
        mascot->setScale(height > 0.f ? 52.f / height : 0.1f);
        mascot->setPosition({centerX, middle + 40.f});
        if (kind == EmptyKind::Error) mascot->setColor({255, 150, 150});
        m_content->addChild(mascot);
    }

    char const* title = kind == EmptyKind::Library ? text("Tu biblioteca esta vacia", "Your library is empty")
        : kind == EmptyKind::NoResults ? text("Sin resultados", "No results")
        : text("No se pudo abrir la biblioteca", "Could not open the library");
    std::string body = kind == EmptyKind::Library
        ? text("Guarda tus ajustes actuales como preset o importa un JSON compartido.",
               "Save your current settings as a preset or import a shared JSON.")
        : kind == EmptyKind::NoResults
        ? text("Ningun preset coincide con la busqueda o la categoria.", "No preset matches the search or category.")
        : detail;
    auto* heading = paimon::ui::makeTitle(title, size.width - 140.f, 0.55f);
    heading->setPosition({centerX, middle + 2.f});
    m_content->addChild(heading);
    auto* message = paimon::ui::makeText(body.c_str(), size.width - 180.f, 0.5f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
    message->setPosition({centerX, middle - 18.f});
    m_content->addChild(message);

    std::vector<CCMenuItemSpriteExtra*> actions;
    if (kind == EmptyKind::Library) {
        actions.push_back(paimon::ui::makeButton(text("+ Crear preset", "+ Create preset"), [this] { createPreset(); },
            paimon::ui::Btn::Green, 108.f, 0.55f, "bigFont.fnt"));
        actions.push_back(paimon::ui::makeButton(text("Importar JSON", "Import JSON"), [this] { importPreset(); },
            paimon::ui::Btn::Cyan, 108.f, 0.55f, "bigFont.fnt"));
    } else if (kind == EmptyKind::NoResults) {
        actions.push_back(paimon::ui::makeButton(text("Limpiar filtros", "Clear filters"), [this] { clearFilters(); },
            paimon::ui::Btn::Gray, 116.f, 0.55f, "bigFont.fnt"));
    }
    if (actions.empty()) return;
    paimon::ui::matchButtonLabels(actions);
    constexpr float kGap = 10.f;
    float rowW = kGap * static_cast<float>(actions.size() - 1);
    for (auto* action : actions) rowW += action->getContentSize().width;
    float x = centerX - rowW / 2.f;
    for (auto* action : actions) {
        float const width = action->getContentSize().width;
        action->setPosition({x + width / 2.f, middle - 46.f});
        m_cards->addChild(action);
        x += width + kGap;
    }
}

void PaimonShopLayer::clearFilters() {
    m_filter = -1;
    m_page = 0;
    m_search->setString("");
    refresh(true);
}

void PaimonShopLayer::openPreset(std::string const& id) {
    auto preset = localLibrary().read(id);
    if (!preset) { showShopError(preset.unwrapErr()); return; }
    if (auto* popup = ShopPresetPopup::create(id, preset.unwrap(), refreshCallback())) popup->show();
}

void PaimonShopLayer::createPreset() {
    if (auto* popup = ShopCreatePopup::create(refreshCallback())) popup->show();
}

void PaimonShopLayer::importPreset() {
    WeakRef<PaimonShopLayer> weak = this;
    pt::pickJson([weak](auto result) {
        auto self = weak.lock();
        if (!self || !self->getParent()) return;
        if (!result) { showShopError(result.unwrapErr()); return; }
        if (!result.unwrap()) return;
        auto bytes = paimon::file::readBytes(*result.unwrap(), kMaxPresetBytes);
        if (bytes.empty()) { showShopError(text("Archivo vacio, ilegible o mayor a 2 MB.", "File is empty, unreadable or larger than 2 MB.")); return; }
        auto json = parseBoundedJson(std::string_view(reinterpret_cast<char const*>(bytes.data()), bytes.size()));
        if (!json) { showShopError(text("JSON invalido.", "Invalid JSON.")); return; }
        auto preset = presetFromJson(json.unwrap());
        if (!preset) { showShopError(preset.unwrapErr()); return; }
        std::vector<uint8_t> png;
        paimon::base64Decode(preset.unwrap().preview, png);
        auto decoded = ImageLoadHelper::loadWithSTBFromMemory(png.data(), png.size(), false);
        if (!decoded.success || !decoded.texture) { showShopError(text("La preview PNG esta danada.", "PNG preview is damaged.")); return; }
        decoded.texture->release();
        auto added = localLibrary().add(preset.unwrap());
        if (!added) { showShopError(added.unwrapErr()); return; }
        self->clearFilters();
        PaimonNotify::show(text("Preset importado. Abrelo para aplicarlo.", "Preset imported. Open it to apply."), NotificationIcon::Success);
    });
}

void PaimonShopLayer::restoreBackup() {
    WeakRef<PaimonShopLayer> weak = this;
    geode::createQuickPopup("Paimon Shop", text("Restaurar los ajustes anteriores al ultimo preset aplicado?", "Restore the settings from before the last applied preset?"),
        text("Cancelar", "Cancel"), text("Restaurar", "Restore"), [weak](auto*, bool confirmed) {
            auto self = weak.lock();
            if (!confirmed || !self || !self->getParent()) return;
            auto result = restoreConfiguration();
            if (!result) { showShopError(result.unwrapErr()); return; }
            self->refresh();
            PaimonNotify::show(text("Ajustes anteriores restaurados.", "Previous settings restored."), NotificationIcon::Success);
        });
}

void PaimonShopLayer::keyBackClicked() { CCDirector::get()->popScene(); }
} // namespace paimon::shop
