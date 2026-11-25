#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "WallpaperPickerPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../services/LayerBackgroundManager.hpp"
#include "../services/WallpaperPlayback.hpp"
#include "../services/WallpaperThumbnailCache.hpp"
#include "../services/WebWallpaperRenderer.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;
namespace wp = paimon::wallpaper;

namespace {

constexpr float kRowHeight = 30.f;
constexpr float kRowGap = 3.f;
constexpr float kRowPitch = kRowHeight + kRowGap;
constexpr float kThumbW = 44.f;
constexpr float kThumbH = 25.f;
constexpr int kRowThumbMaxDim = 128;
constexpr int kPreviewMaxDim = 320;

struct FitMode { char const* id; char const* label; };
constexpr FitMode kFitModes[] = {{"cover", "Cubrir"}, {"contain", "Contener"}, {"stretch", "Estirar"}};

int fitIndexOf(std::string const& id) {
    for (int i = 0; i < 3; ++i) if (id == kFitModes[i].id) return i;
    return 0;
}

ccColor3B typeColor(wp::WallpaperType type) {
    switch (type) {
        case wp::WallpaperType::Video: return paimon::ui::palette::gold;
        case wp::WallpaperType::Web: return paimon::ui::palette::info;
        case wp::WallpaperType::Scene: return paimon::ui::palette::violet;
        case wp::WallpaperType::Gif:
        case wp::WallpaperType::Image: return paimon::ui::palette::success;
        default: return paimon::ui::palette::dim;
    }
}

char const* tabName(wp::WallpaperType type) {
    switch (type) {
        case wp::WallpaperType::Video: return "Videos";
        case wp::WallpaperType::Web: return "Web";
        case wp::WallpaperType::Scene: return "Escenas";
        case wp::WallpaperType::Gif: return "GIF/Img";
        default: return "Todos";
    }
}

bool matchesFilter(wp::WallpaperItem const& item, wp::WallpaperType filter) {
    if (filter == wp::WallpaperType::All) return true;
    if (filter == wp::WallpaperType::Gif) return item.type == wp::WallpaperType::Gif || item.type == wp::WallpaperType::Image;
    return item.type == filter;
}

float containScale(CCNode* node, float boxW, float boxH) {
    auto const size = node ? node->getContentSize() : CCSize{};
    if (size.width <= 0.f || size.height <= 0.f) return 1.f;
    return std::min(boxW / size.width, boxH / size.height);
}

ButtonSprite* buttonSpriteOf(CCMenuItemSpriteExtra* button) {
    return button ? typeinfo_cast<ButtonSprite*>(button->getNormalImage()) : nullptr;
}

} // namespace

WallpaperPickerPopup* WallpaperPickerPopup::create(
    std::string const& layerKey,
    geode::CopyableFunction<void(wp::WallpaperItem const&)> onPick
) {
    auto ret = new WallpaperPickerPopup();
    if (ret && ret->init(layerKey, std::move(onPick))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

std::string WallpaperPickerPopup::normalizedKey(std::string const& path) {
    if (path.empty()) return {};
    auto key = paimon::paths::toUtf8(paimon::assets::pathFromUtf8(path).lexically_normal());
    for (auto& c : key) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return key;
}

std::string WallpaperPickerPopup::playbackKey(wp::WallpaperItem const& item) {
    // mirrors what applyWallpaper stores in the layer config.
    bool const project = item.type == wp::WallpaperType::Web || item.type == wp::WallpaperType::Scene;
    auto const path = project ? item.rootPath / "project.json" : item.entryFile;
    return normalizedKey(paimon::paths::toUtf8(path));
}

bool WallpaperPickerPopup::init(
    std::string const& layerKey,
    geode::CopyableFunction<void(wp::WallpaperItem const&)> onPick
) {
    m_targetLayerKey = layerKey;
    m_onPick = std::move(onPick);
    m_webAvailable = wp::WebWallpaperRenderer::isAvailable();

    auto const cfg = LayerBackgroundManager::get().getConfig(m_targetLayerKey);
    if (cfg.type == "wallpaper" || cfg.type == "scene" || cfg.type == "video" || cfg.type == "custom") {
        m_appliedPath = normalizedKey(cfg.customPath);
    }

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    float const popupW = std::clamp(winSize.width - 24.f, 340.f, 470.f);
    float const popupH = std::clamp(winSize.height - 24.f, 250.f, 290.f);
    if (!PaimonPopup::init(popupW, popupH)) return false;

    this->setTitle("Fondos Responsivos");
    this->addCorners(geode::SideArtStyle::PopupGold);
    this->addInfoButton(
        "Fondos Responsivos",
        "Explora y aplica fondos de <cy>Wallpaper Engine</c> o tus propios archivos. "
        "<cy>No necesitas abrir Wallpaper Engine</c>.\n\n"
        "<cg>Videos, GIF e imagenes</c>: se reproducen directamente.\n"
        "<cg>Web</c>: navegador integrado de Windows (WebView2). Los cuadros se comparten por la "
        "<cy>GPU</c>, con mouse, clics y el audio del juego. Se pausa solo cuando no se ve.\n"
        "<cg>Escenas</c>: motor 2D propio con capas, jerarquias, videos, particulas, sprites animados, "
        "mascaras de opacidad, relojes y parallax. Los shaders animados y scripts de Wallpaper Engine aun no se reproducen.\n\n"
        "<cy>Steam</c>: detecta los fondos suscritos en tu biblioteca.\n"
        "<cr>Seguridad</c>: los ejecutables (.exe) se descartan.\n"
        "<cl>Creditos</c>: 'Steam' abre la pagina original del autor."
    );

    auto const content = m_mainLayer->getContentSize();
    float const pad = 10.f;
    float const bottomBar = 32.f;
    float const top = content.height - 56.f;
    float const bodyH = top - (bottomBar + 4.f);
    float const leftW = std::floor((content.width - pad * 3.f) * 0.56f);
    float const rightW = (content.width - pad * 3.f) - leftW;

    m_tabMenu = CCMenu::create();
    m_tabMenu->setPosition({content.width / 2.f, content.height - 44.f});
    m_tabMenu->setContentSize({content.width - 30.f, 20.f});
    m_tabMenu->setAnchorPoint({0.5f, 0.5f});
    m_tabMenu->ignoreAnchorPointForPosition(false);
    m_tabMenu->setLayout(RowLayout::create()->setGap(4.f)->setAxisAlignment(AxisAlignment::Center));
    m_tabMenu->setID("filter-menu"_spr);
    m_mainLayer->addChild(m_tabMenu, 3);
    buildTabs();

    m_listPanel = paimon::ui::makeInset({leftW, bodyH});
    m_listPanel->setPosition({pad, bottomBar + 4.f});
    m_listPanel->setID("wallpaper-list-panel"_spr);
    m_mainLayer->addChild(m_listPanel, 1);
    buildList(leftW, bodyH);

    m_detailPanel = paimon::ui::makeInset({rightW, bodyH});
    m_detailPanel->setPosition({pad * 2.f + leftW, bottomBar + 4.f});
    m_detailPanel->setID("wallpaper-detail-panel"_spr);
    m_mainLayer->addChild(m_detailPanel, 1);
    buildDetail(rightW, bodyH);

    auto bottomMenu = CCMenu::create();
    bottomMenu->setPosition({content.width / 2.f, 17.f});
    bottomMenu->setLayout(RowLayout::create()->setGap(8.f)->setAxisAlignment(AxisAlignment::Center));
    bottomMenu->setID("bottom-action-menu"_spr);
    m_mainLayer->addChild(bottomMenu, 2);
    bottomMenu->addChild(paimon::ui::makeButton("Importar...", [this] { onImportFolder(); },
        paimon::ui::Btn::Green, 80.f, 0.48f));
    bottomMenu->addChild(paimon::ui::makeButton("Abrir Carpeta", [this] { onOpenFolder(); },
        paimon::ui::Btn::Cyan, 85.f, 0.48f));
    bottomMenu->addChild(paimon::ui::makeButton("Refrescar", [this] { onRefresh(); },
        paimon::ui::Btn::Gray, 70.f, 0.48f));
    bottomMenu->updateLayout();

    this->schedule(schedule_selector(WallpaperPickerPopup::tick));
    startScan(false);
    return true;
}

void WallpaperPickerPopup::buildTabs() {
    if (!m_tabMenu) return;
    m_tabMenu->removeAllChildren();
    m_tabs.clear();
    std::vector<wp::WallpaperType> types{
        wp::WallpaperType::All, wp::WallpaperType::Video};
    if (wp::WebWallpaperRenderer::isAvailable()) {
        types.push_back(wp::WallpaperType::Web);
    } else if (m_currentFilter == wp::WallpaperType::Web) {
        m_currentFilter = wp::WallpaperType::All;
    }
    types.push_back(wp::WallpaperType::Scene);
    types.push_back(wp::WallpaperType::Gif);

    std::vector<size_t> counts(types.size(), 0);
    for (auto const& item : m_allWallpapers) {
        for (size_t i = 0; i < types.size(); ++i) {
            if (matchesFilter(item, types[i])) ++counts[i];
        }
    }
    float const tabW = std::min(82.f, (m_tabMenu->getContentWidth() - 16.f) / static_cast<float>(types.size()));
    for (size_t i = 0; i < types.size(); ++i) {
        auto const type = types[i];
        auto const text = m_allWallpapers.empty() && m_scanning
            ? std::string(tabName(type)) : fmt::format("{} {}", tabName(type), counts[i]);
        auto skin = type == m_currentFilter ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray;
        auto* button = paimon::ui::makeButton(text.c_str(), [this, type] { onFilterTab(type); }, skin, tabW, 0.55f);
        m_tabMenu->addChild(button);
        m_tabs.emplace_back(type, button);
    }
    m_tabMenu->updateLayout();
}

void WallpaperPickerPopup::buildList(float width, float height) {
    m_scrollLayer = geode::ScrollLayer::create({width - 6.f, height - 6.f});
    if (m_scrollLayer) m_scrollLayer->setAnchorPoint({0.f, 0.f});
    m_scrollLayer->setPosition({3.f, 3.f});
    m_scrollLayer->setID("wallpaper-scroll"_spr);
    m_listPanel->addChild(m_scrollLayer, 2);

    m_rowWidth = m_scrollLayer->getContentWidth() - 4.f;
    m_rowMenu = CCMenu::create();
    m_rowMenu->setPosition({0.f, 0.f});
    m_rowMenu->setID("wallpaper-items-menu"_spr);
    m_scrollLayer->m_contentLayer->addChild(m_rowMenu);

    // a fixed pool covering the viewport; rows are rebound while scrolling.
    size_t const poolSize = static_cast<size_t>(std::ceil(m_scrollLayer->getContentHeight() / kRowPitch)) + 2;
    m_rows.resize(poolSize);
    for (auto& row : m_rows) {
        auto* card = CCNode::create();
        card->setContentSize({m_rowWidth, kRowHeight});

        row.background = paimon::ui::makeInset({m_rowWidth, kRowHeight}, 70);
        card->addChild(row.background, 0);
        row.highlight = paimon::ui::makeInset({m_rowWidth, kRowHeight}, 110, {255, 190, 60});
        row.highlight->setVisible(false);
        card->addChild(row.highlight, 1);

        auto* thumbBox = paimon::ui::makeInset({kThumbW, kThumbH}, 120);
        thumbBox->setPosition({4.f, (kRowHeight - kThumbH) / 2.f});
        card->addChild(thumbBox, 2);
        row.placeholder = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
        if (row.placeholder) {
            row.placeholder->setScale(containScale(row.placeholder, kThumbH * 0.7f, kThumbH * 0.7f));
            row.placeholder->setOpacity(140);
            row.placeholder->setPosition({4.f + kThumbW / 2.f, kRowHeight / 2.f});
            card->addChild(row.placeholder, 3);
        }

        float const textX = 4.f + kThumbW + 6.f;
        row.title = CCLabelBMFont::create("", "chatFont.fnt");
        row.title->setAnchorPoint({0.f, 0.5f});
        row.title->setPosition({textX, kRowHeight / 2.f + 5.5f});
        card->addChild(row.title, 3);

        row.author = CCLabelBMFont::create("", "chatFont.fnt");
        row.author->setAnchorPoint({0.f, 0.5f});
        row.author->setColor(paimon::ui::palette::muted);
        row.author->setPosition({textX, kRowHeight / 2.f - 6.f});
        card->addChild(row.author, 3);

        row.badge = CCLabelBMFont::create("", "chatFont.fnt");
        row.badge->setAnchorPoint({1.f, 0.5f});
        row.badge->setPosition({m_rowWidth - 6.f, kRowHeight / 2.f + 5.5f});
        card->addChild(row.badge, 3);

        row.status = CCLabelBMFont::create("", "chatFont.fnt");
        row.status->setAnchorPoint({1.f, 0.5f});
        row.status->setPosition({m_rowWidth - 6.f, kRowHeight / 2.f - 6.f});
        card->addChild(row.status, 3);

        row.card = card;
        row.button = CCMenuItemExt::createSpriteExtra(card, [this](CCMenuItemSpriteExtra* sender) {
            onRowPressed(sender->getTag());
        });
        row.button->setContentSize({m_rowWidth, kRowHeight});
        row.button->setVisible(false);
        m_rowMenu->addChild(row.button);
    }

    m_emptyLabel = CCLabelBMFont::create("Buscando fondos...", "chatFont.fnt");
    m_emptyLabel->setAlignment(kCCTextAlignmentCenter);
    m_emptyLabel->setScale(0.5f);
    m_emptyLabel->setPosition({width / 2.f, height / 2.f});
    m_listPanel->addChild(m_emptyLabel, 3);
}

void WallpaperPickerPopup::buildDetail(float width, float height) {
    m_detailWidth = width;

    m_detailTitle = CCLabelBMFont::create("Selecciona un fondo", "goldFont.fnt");
    m_detailTitle->setPosition({width / 2.f, height - 12.f});
    m_detailTitle->setScale(0.5f);
    m_detailPanel->addChild(m_detailTitle, 2);

    m_detailAuthor = CCLabelBMFont::create("", "chatFont.fnt");
    m_detailAuthor->setScale(0.38f);
    m_detailAuthor->setColor(paimon::ui::palette::muted);
    m_detailAuthor->setPosition({width / 2.f, height - 25.f});
    m_detailPanel->addChild(m_detailAuthor, 2);

    // preview: as large as the panel allows between the header and the options.
    float const previewTop = height - 33.f;
    float const previewBottom = 89.f;
    float const maxW = width - 20.f;
    float const previewH = std::clamp(previewTop - previewBottom, 40.f, maxW * 9.f / 16.f);
    float const previewW = std::min(maxW, previewH * 16.f / 9.f);
    m_previewSize = CCSize(previewW, previewH);
    m_previewBox = CCNode::create();
    m_previewBox->setContentSize(m_previewSize);
    m_previewBox->setAnchorPoint({0.5f, 0.5f});
    m_previewBox->ignoreAnchorPointForPosition(false);
    m_previewBox->setPosition({width / 2.f, previewTop - previewH / 2.f});
    m_detailPanel->addChild(m_previewBox, 2);
    auto* frame = paimon::ui::makeInset(m_previewSize, 140);
    m_previewBox->addChild(frame, 0);
    m_previewPlaceholder = CCSprite::createWithSpriteFrameName("GJ_playBtn2_001.png");
    if (m_previewPlaceholder) {
        m_previewPlaceholder->setScale(containScale(m_previewPlaceholder, previewH * 0.45f, previewH * 0.45f));
        m_previewPlaceholder->setOpacity(140);
        m_previewPlaceholder->setPosition(m_previewSize / 2.f);
        m_previewBox->addChild(m_previewPlaceholder, 1);
    }
    m_previewBadge = CCLabelBMFont::create("", "bigFont.fnt");
    m_previewBadge->setScale(0.28f);
    m_previewBadge->setAnchorPoint({0.f, 0.f});
    m_previewBadge->setPosition({4.f, 3.f});
    m_previewBox->addChild(m_previewBadge, 3);

    m_detailInfo = CCLabelBMFont::create("", "chatFont.fnt");
    m_detailInfo->setScale(0.33f);
    m_detailInfo->setAlignment(kCCTextAlignmentCenter);
    m_detailInfo->setColor(paimon::ui::palette::muted);
    m_detailInfo->setPosition({width / 2.f, 76.f});
    m_detailPanel->addChild(m_detailInfo, 2);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, height});
    menu->setID("detail-menu"_spr);
    m_detailPanel->addChild(menu, 3);

    m_optionLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_optionLabel->setScale(0.4f);
    m_optionLabel->setAnchorPoint({0.f, 0.5f});
    m_optionLabel->setPosition({12.f, 57.f});
    m_detailPanel->addChild(m_optionLabel, 2);

    auto const cfg = LayerBackgroundManager::get().getConfig(m_targetLayerKey);
    m_audioToggle = paimon::ui::makeToggle(cfg.audioReactive, [this](bool on) { onToggleAudio(on); }, 0.5f);
    m_audioToggle->setPosition({width - 20.f, 57.f});
    m_audioToggle->setVisible(false);
    menu->addChild(m_audioToggle);
    m_parallaxToggle = paimon::ui::makeToggle(cfg.parallax, [this](bool on) { onToggleParallax(on); }, 0.5f);
    m_parallaxToggle->setPosition({width - 20.f, 57.f});
    m_parallaxToggle->setVisible(false);
    menu->addChild(m_parallaxToggle);

    m_fitLabel = CCLabelBMFont::create("Ajuste:", "chatFont.fnt");
    m_fitLabel->setScale(0.4f);
    m_fitLabel->setAnchorPoint({0.f, 0.5f});
    m_fitLabel->setPosition({12.f, 39.f});
    m_fitLabel->setVisible(false);
    m_detailPanel->addChild(m_fitLabel, 2);
    m_fitButton = paimon::ui::makeButton(kFitModes[fitIndexOf(cfg.fitMode)].label, [this] { onCycleFitMode(); },
        paimon::ui::Btn::Gray, 64.f, 0.45f);
    m_fitButton->setPosition({width - 40.f, 39.f});
    m_fitButton->setVisible(false);
    menu->addChild(m_fitButton);

    m_workshopBtn = paimon::ui::makeButton("Steam", [this] { onOpenWorkshop(); },
        paimon::ui::Btn::Cyan, 52.f, 0.5f);
    m_workshopBtn->setPosition({8.f + 26.f, 16.f});
    m_workshopBtn->setVisible(false);
    menu->addChild(m_workshopBtn);

    m_applyBtn = paimon::ui::makeButton("Aplicar Fondo", [this] { onApplyCurrent(); },
        paimon::ui::Btn::Green, width - 76.f, 0.6f);
    m_applyBtn->setPosition({width / 2.f, 16.f});
    m_applyBtn->setEnabled(false);
    m_applyBtn->setOpacity(150);
    menu->addChild(m_applyBtn);
}

void WallpaperPickerPopup::startScan(bool bypassCache) {
    auto request = wp::WallpaperEngineManager::get().makeScanRequest();
    request.bypassCache = bypassCache;

    int const generation = ++m_scanGeneration;
    m_scanning = true;
    updateStatusLabel();

    WeakRef<WallpaperPickerPopup> weakSelf = this;
    bool const spawned = paimon::ThreadTracker::get().spawn([weakSelf, request, generation]() {
        geode::utils::thread::setName("PaimonWallpaperScan");
        if (paimon::isRuntimeShuttingDown()) return;
        auto items = wp::WallpaperEngineManager::get().scanWallpapers(request);
        if (paimon::isRuntimeShuttingDown()) return;
        Loader::get()->queueInMainThread([weakSelf, items = std::move(items), generation]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            auto self = weakSelf.lock();
            if (!self || self->m_scanGeneration != generation) return;
            self->onScanComplete(std::move(items));
        });
    });
    if (!spawned) {
        m_scanning = false;
        updateStatusLabel();
    }
}

void WallpaperPickerPopup::onScanComplete(std::vector<wp::WallpaperItem> items) {
    m_scanning = false;
    m_allWallpapers = std::move(items);
    buildTabs();
    applyFilter(false);
}

wp::WallpaperItem const* WallpaperPickerPopup::selectedItem() const {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_filtered.size())) return nullptr;
    return &m_allWallpapers[m_filtered[static_cast<size_t>(m_selected)]];
}

void WallpaperPickerPopup::applyFilter(bool keepSelection) {
    std::string previous;
    if (keepSelection) {
        if (auto const* item = selectedItem()) previous = playbackKey(*item);
    }
    m_filtered.clear();
    for (size_t i = 0; i < m_allWallpapers.size(); ++i) {
        if (matchesFilter(m_allWallpapers[i], m_currentFilter)) m_filtered.push_back(i);
    }

    // preferred selection: an import that just landed, the one in use, the
    // previous one, then the first row.
    std::string const pending = m_pendingSelectPath.empty() ? std::string{} : normalizedKey(m_pendingSelectPath);
    int target = m_filtered.empty() ? -1 : 0;
    int applied = -1, kept = -1, imported = -1;
    for (size_t i = 0; i < m_filtered.size(); ++i) {
        auto const& item = m_allWallpapers[m_filtered[i]];
        auto const key = playbackKey(item);
        if (!m_appliedPath.empty() && key == m_appliedPath && applied < 0) applied = static_cast<int>(i);
        if (!previous.empty() && key == previous && kept < 0) kept = static_cast<int>(i);
        if (!pending.empty() && imported < 0 &&
            normalizedKey(paimon::paths::toUtf8(item.rootPath)) == pending) imported = static_cast<int>(i);
    }
    if (imported >= 0) target = imported;
    else if (kept >= 0) target = kept;
    else if (applied >= 0) target = applied;
    if (imported >= 0) m_pendingSelectPath.clear();

    m_contentHeight = std::max(m_scrollLayer->getContentHeight(),
        static_cast<float>(m_filtered.size()) * kRowPitch + 4.f);
    m_scrollLayer->m_contentLayer->setContentSize({m_scrollLayer->getContentWidth(), m_contentHeight});
    m_rowMenu->setContentSize({m_scrollLayer->getContentWidth(), m_contentHeight});
    m_scrollLayer->m_contentLayer->setPositionY(m_scrollLayer->getContentHeight() - m_contentHeight);
    m_selected = -1;
    m_firstBound = -1;
    for (auto& row : m_rows) row.index = -1;

    if (target >= 0) {
        select(target);
        scrollToIndex(target);
    } else {
        select(-1);
    }
    layoutRows(true);
    updateStatusLabel();
}

void WallpaperPickerPopup::updateStatusLabel() {
    if (!m_emptyLabel) return;
    if (!m_filtered.empty()) {
        m_emptyLabel->setVisible(false);
        return;
    }
    if (m_scanning) {
        m_emptyLabel->setString("Buscando fondos...");
    } else if (m_allWallpapers.empty()) {
        m_emptyLabel->setString("No se encontraron fondos.\nUsa 'Importar' o copia fondos\na la carpeta del mod.");
    } else {
        m_emptyLabel->setString("No hay fondos de este tipo.");
    }
    m_emptyLabel->setVisible(true);
}

void WallpaperPickerPopup::scrollToIndex(int index) {
    if (index < 0 || !m_scrollLayer) return;
    float const viewH = m_scrollLayer->getContentHeight();
    float const center = m_contentHeight - 2.f - (static_cast<float>(index) + 0.5f) * kRowPitch;
    float const y = std::clamp(viewH / 2.f - center, viewH - m_contentHeight, 0.f);
    m_scrollLayer->m_contentLayer->setPositionY(y);
    layoutRows(true);
}

void WallpaperPickerPopup::layoutRows(bool force) {
    if (!m_scrollLayer || m_rows.empty()) return;
    float const contentY = m_scrollLayer->m_contentLayer->getPositionY();
    if (!force && std::abs(contentY - m_lastScrollY) < 0.25f) return;
    m_lastScrollY = contentY;

    float const viewH = m_scrollLayer->getContentHeight();
    int const count = static_cast<int>(m_filtered.size());
    float const visibleTop = -contentY + viewH;
    int const first = std::max(0, static_cast<int>(std::floor((m_contentHeight - 2.f - visibleTop) / kRowPitch)));
    if (!force && first == m_firstBound) return;
    m_firstBound = first;

    for (size_t slot = 0; slot < m_rows.size(); ++slot) {
        int const index = first + static_cast<int>(slot);
        auto& row = m_rows[slot];
        if (index >= count) {
            row.index = -1;
            row.button->setVisible(false);
            continue;
        }
        if (row.index != index || force) bindRow(row, index);
    }
}

void WallpaperPickerPopup::bindRow(Row& row, int index) {
    auto const& item = m_allWallpapers[m_filtered[static_cast<size_t>(index)]];
    bool const rebind = row.index != index;
    row.index = index;
    row.button->setTag(index);
    row.button->setVisible(true);
    float const center = m_contentHeight - 2.f - (static_cast<float>(index) + 0.5f) * kRowPitch;
    row.button->setPosition({m_rowWidth / 2.f + 2.f, center});

    if (rebind) {
        float const textW = m_rowWidth - (4.f + kThumbW + 6.f) - 58.f;
        row.title->setString(item.title.empty() ? item.id.c_str() : item.title.c_str());
        row.title->setScale(0.45f);
        paimon::ui::shrinkLabelToFit(row.title, textW);
        row.author->setString(("por " + item.author).c_str());
        row.author->setScale(0.34f);
        paimon::ui::shrinkLabelToFit(row.author, textW);
        row.badge->setString(wp::WallpaperEngineManager::typeToString(item.type));
        row.badge->setScale(0.32f);
        row.badge->setColor(typeColor(item.type));
        if (row.thumb) {
            row.thumb->removeFromParent();
            row.thumb = nullptr;
        }
        row.thumbPath = item.previewPath;
        row.thumbReady = false;
        if (row.placeholder) row.placeholder->setVisible(true);
    }
    refreshRowThumb(row);
    refreshRowState(row);
}

void WallpaperPickerPopup::refreshRowThumb(Row& row) {
    if (row.thumbReady || row.thumbPath.empty() || row.index < 0) return;
    auto* texture = wp::WallpaperThumbnailCache::get().request(row.thumbPath, kRowThumbMaxDim);
    if (!texture) return;
    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return;
    sprite->setScale(containScale(sprite, kThumbW - 2.f, kThumbH - 2.f));
    sprite->setPosition({4.f + kThumbW / 2.f, kRowHeight / 2.f});
    row.card->addChild(sprite, 3);
    row.thumb = sprite;
    row.thumbReady = true;
    if (row.placeholder) row.placeholder->setVisible(false);
}

void WallpaperPickerPopup::refreshRowState(Row& row) {
    if (row.index < 0) return;
    auto const& item = m_allWallpapers[m_filtered[static_cast<size_t>(row.index)]];
    bool const selected = row.index == m_selected;
    row.highlight->setVisible(selected);
    row.background->setOpacity(selected ? 40 : 70);
    bool const inUse = !m_appliedPath.empty() && playbackKey(item) == m_appliedPath;
    bool const blocked = !item.playable || (item.type == wp::WallpaperType::Web && !m_webAvailable);
    if (inUse) {
        row.status->setString("EN USO");
        row.status->setColor(paimon::ui::palette::success);
    } else if (blocked) {
        row.status->setString("NO COMPATIBLE");
        row.status->setColor(paimon::ui::palette::danger);
    } else {
        row.status->setString("");
    }
    row.status->setScale(0.3f);
    row.title->setColor(blocked ? paimon::ui::palette::dim : paimon::ui::palette::text);
}

void WallpaperPickerPopup::tick(float) {
    layoutRows(false);
    auto& cache = wp::WallpaperThumbnailCache::get();
    cache.pump();
    auto const revision = cache.revision();
    if (revision == m_thumbRevision) return;
    m_thumbRevision = revision;
    for (auto& row : m_rows) refreshRowThumb(row);
    refreshPreview();
}

void WallpaperPickerPopup::onRowPressed(int index) {
    // rows bound just outside the clipped list must not take clicks meant for
    // the tabs or the bottom bar.
    for (auto const& row : m_rows) {
        if (row.index != index) continue;
        auto const world = row.button->convertToWorldSpaceAR({0.f, 0.f});
        auto const local = m_scrollLayer->convertToNodeSpace(world);
        if (local.y < 0.f || local.y > m_scrollLayer->getContentHeight()) return;
        break;
    }
    select(index);
}

void WallpaperPickerPopup::select(int index) {
    if (index >= static_cast<int>(m_filtered.size())) index = -1;
    m_selected = index;
    for (auto& row : m_rows) refreshRowState(row);

    auto const* item = selectedItem();
    if (!item) {
        m_detailTitle->setString(m_scanning ? "Buscando..." : "Sin fondos");
        m_detailTitle->setScale(0.5f);
        m_detailAuthor->setString("");
        m_detailInfo->setString("");
        m_previewBadge->setString("");
        m_previewPath.clear();
        m_previewReady = false;
        if (m_previewSprite) { m_previewSprite->removeFromParent(); m_previewSprite = nullptr; }
        if (m_previewPlaceholder) m_previewPlaceholder->setVisible(true);
        m_workshopBtn->setVisible(false);
        m_optionLabel->setVisible(false);
        m_audioToggle->setVisible(false);
        m_parallaxToggle->setVisible(false);
        m_fitLabel->setVisible(false);
        m_fitButton->setVisible(false);
        m_applyBtn->setEnabled(false);
        m_applyBtn->setOpacity(150);
        return;
    }

    float const textW = m_detailWidth - 14.f;
    m_detailTitle->setString(item->title.empty() ? item->id.c_str() : item->title.c_str());
    m_detailTitle->setScale(0.5f);
    paimon::ui::shrinkLabelToFit(m_detailTitle, textW);
    m_detailAuthor->setString(("por " + item->author).c_str());
    m_detailAuthor->setScale(0.38f);
    paimon::ui::shrinkLabelToFit(m_detailAuthor, textW);
    m_previewBadge->setString(wp::WallpaperEngineManager::typeToString(item->type));
    m_previewBadge->setColor(typeColor(item->type));

    if (m_previewPath != item->previewPath || !m_previewReady) {
        if (m_previewSprite) { m_previewSprite->removeFromParent(); m_previewSprite = nullptr; }
        if (m_previewPlaceholder) m_previewPlaceholder->setVisible(true);
        m_previewPath = item->previewPath;
        m_previewReady = false;
        refreshPreview();
    }

    bool canApply = item->playable;
    std::string info;
    switch (item->type) {
        case wp::WallpaperType::Web:
            canApply = canApply && m_webAvailable;
            info = !m_webAvailable ? "Requiere Microsoft Edge WebView2 Runtime."
                : canApply ? "Navegador integrado con aceleracion GPU.\nMouse, clics y audio del juego."
                : "No se encontro el HTML del fondo.";
            break;
        case wp::WallpaperType::Scene:
            info = canApply ? "Escena 2D: capas, videos, particulas y relojes.\nShaders y scripts de WE: no disponibles."
                : item->scene3D ? "Escena 3D: el motor 2D no puede mostrarla."
                : "La escena no tiene un paquete legible.";
            break;
        case wp::WallpaperType::Video:
            info = canApply ? "Video con el reproductor integrado." : "No se encontro el archivo de video.";
            break;
        case wp::WallpaperType::Gif:
            info = canApply ? "GIF animado." : "No se encontro el GIF.";
            break;
        case wp::WallpaperType::Image:
            info = canApply ? "Imagen estatica." : "No se encontro la imagen.";
            break;
        default:
            info = "Tipo no compatible.\nImporta un video, GIF o imagen.";
            canApply = false;
            break;
    }
    if (playbackKey(*item) == m_appliedPath && canApply) info += "\nEste fondo ya esta en uso.";
    m_detailInfo->setString(info.c_str());
    m_detailInfo->setScale(0.33f);
    paimon::ui::shrinkLabelToFit(m_detailInfo, textW);

    refreshOptions(*item);

    bool const workshop = item->workshopId > 0;
    m_workshopBtn->setVisible(workshop);
    m_applyBtn->setPositionX(workshop ? m_detailWidth / 2.f + 30.f : m_detailWidth / 2.f);
    m_applyBtn->setEnabled(canApply);
    m_applyBtn->setOpacity(canApply ? 255 : 150);
}

void WallpaperPickerPopup::refreshOptions(wp::WallpaperItem const& item) {
    bool const web = item.type == wp::WallpaperType::Web;
    bool const scene = item.type == wp::WallpaperType::Scene;
    m_optionLabel->setVisible(web || scene);
    m_optionLabel->setString(web ? "Audio reactivo" : "Parallax de la escena");
    m_audioToggle->setVisible(web);
    m_parallaxToggle->setVisible(scene);
    m_fitLabel->setVisible(web || scene);
    m_fitButton->setVisible(web || scene);
}

void WallpaperPickerPopup::refreshPreview() {
    if (m_previewReady || m_previewPath.empty() || !m_previewBox) return;
    auto* texture = wp::WallpaperThumbnailCache::get().request(m_previewPath, kPreviewMaxDim);
    if (!texture) return;
    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return;
    sprite->setScale(containScale(sprite, m_previewSize.width - 4.f, m_previewSize.height - 4.f));
    sprite->setPosition(m_previewSize / 2.f);
    m_previewBox->addChild(sprite, 2);
    m_previewSprite = sprite;
    m_previewReady = true;
    if (m_previewPlaceholder) m_previewPlaceholder->setVisible(false);
}

void WallpaperPickerPopup::onFilterTab(wp::WallpaperType type) {
    if (m_currentFilter == type) return;
    m_currentFilter = type;
    for (auto const& [tabType, button] : m_tabs) {
        paimon::ui::setButtonSkin(button, tabType == type ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    // filtering is in-memory: rescanning the filesystem per tab stalled the game.
    applyFilter(true);
}

void WallpaperPickerPopup::onApplyCurrent() {
    auto const* selected = selectedItem();
    if (!selected) return;
    auto const item = *selected;
    if (wp::WallpaperEngineManager::get().applyWallpaper(m_targetLayerKey, item)) {
        PaimonNotify::show(fmt::format("Fondo '{}' configurado.", item.title), geode::NotificationIcon::Success, 2.5f);
        if (m_onPick) m_onPick(item);
        this->onClose(nullptr);
    } else {
        PaimonNotify::show("No hay un renderizador integrado compatible con este fondo.", geode::NotificationIcon::Error);
    }
}

void WallpaperPickerPopup::onOpenWorkshop() {
    if (auto const* item = selectedItem()) wp::WallpaperEngineManager::get().openWorkshopPage(*item);
}

void WallpaperPickerPopup::onImportFolder() {
    WeakRef<WallpaperPickerPopup> weakSelf = this;
    pt::pickFolder([weakSelf](Result<std::optional<std::filesystem::path>> res) {
        auto self = weakSelf.lock();
        if (!self) return;
        auto pathOpt = std::move(res).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;
        auto itemOpt = wp::WallpaperEngineManager::get().importWallpaperFolder(*pathOpt);
        if (itemOpt) {
            PaimonNotify::show(fmt::format("Fondo '{}' detectado!", itemOpt->title), geode::NotificationIcon::Success);
            self->m_pendingSelectPath = paimon::paths::toUtf8(itemOpt->rootPath.lexically_normal());
            self->startScan(true);
        } else {
            PaimonNotify::show("No se encontro un fondo valido en esa carpeta.", geode::NotificationIcon::Warning);
        }
    });
}

void WallpaperPickerPopup::onOpenFolder() {
    auto path = wp::WallpaperEngineManager::get().getPortableWallpaperDir();
#if defined(GEODE_IS_MOBILE)
    geode::utils::clipboard::write(paimon::paths::toUtf8(path));
    PaimonNotify::show(fmt::format("Carpeta de fondos (ruta copiada): {}", paimon::paths::toUtf8(path)),
        NotificationIcon::Info, 3.0f);
#else
    geode::utils::file::openFolder(paimon::paths::toSdkPath(path));
#endif
}

void WallpaperPickerPopup::onRefresh() {
    PaimonNotify::show("Actualizando lista de fondos...", geode::NotificationIcon::Info, 1.5f);
    startScan(true);
}

void WallpaperPickerPopup::onClose(cocos2d::CCObject* sender) {
    this->unschedule(schedule_selector(WallpaperPickerPopup::tick));
    PaimonPopup::onClose(sender);
}

void WallpaperPickerPopup::onToggleAudio(bool on) {
    auto& mgr = LayerBackgroundManager::get();
    auto cfg = mgr.getConfig(m_targetLayerKey);
    cfg.audioReactive = on;
    mgr.saveConfig(m_targetLayerKey, cfg);
}

void WallpaperPickerPopup::onToggleParallax(bool on) {
    auto& mgr = LayerBackgroundManager::get();
    auto cfg = mgr.getConfig(m_targetLayerKey);
    cfg.parallax = on;
    mgr.saveConfig(m_targetLayerKey, cfg);
}

void WallpaperPickerPopup::onCycleFitMode() {
    auto& mgr = LayerBackgroundManager::get();
    auto cfg = mgr.getConfig(m_targetLayerKey);
    int const next = (fitIndexOf(cfg.fitMode) + 1) % 3;
    cfg.fitMode = kFitModes[next].id;
    mgr.saveConfig(m_targetLayerKey, cfg);
    if (auto* sprite = buttonSpriteOf(m_fitButton)) sprite->setString(kFitModes[next].label);
}
