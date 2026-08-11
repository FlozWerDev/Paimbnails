#include "IconStoreLayer.hpp"

#include "IconDetailPopup.hpp"
#include "IconFilterPopup.hpp"
#include "IconStoreCard.hpp"
#include "SegmentedBar.hpp"
#include "../services/GalleryInstaller.hpp"
#include "../../icon-maker/ui/IconMakerKit.hpp"
#include "../../icon-maker/ui/IconMakerUI.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;
namespace mkui = paimon::icon_maker::ui;

namespace paimon::icon_gallery {

namespace {

constexpr float kCardW = 104.f;
constexpr float kCardH = 118.f;
constexpr float kCardGap = 8.f;

    // Paginate to keep live nodes and per-page downloads bounded.
constexpr int kPageSize = 24;

constexpr float kHeaderH = 76.f;
constexpr float kFooterH = 40.f;

std::string tr(char const* key) {
    return Localization::get().getString(key);
}

}

IconStoreLayer* IconStoreLayer::create() {
    auto* ret = new IconStoreLayer();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void IconStoreLayer::open() {
    if (auto* layer = IconStoreLayer::create()) {
        geode::pushSceneWithLayer(layer);
    }
}

IconStoreLayer::~IconStoreLayer() {
    // Store callbacks must not outlive this layer.
    GalleryStore::get().clearOnIconReady();
}

bool IconStoreLayer::init() {
    if (!CCLayer::init()) return false;
    setKeypadEnabled(true);
    setID("icon-store"_spr);

    // Sort by registry name; file dates are absent or unstable on first load.
    m_query.sort = GallerySort::NameAsc;

    buildBackground();
    buildHeader();

    auto win = CCDirector::get()->getWinSize();
    m_scrollHost = CCNode::create();
    m_scrollHost->setPosition({20.f, kFooterH});
    addChild(m_scrollHost, 5);

    m_messageHost = CCNode::create();
    m_messageHost->setPosition({win.width / 2.f, win.height / 2.f - 10.f});
    addChild(m_messageHost, 6);

    buildFooter();
    installStoreCallback();
    startLoading();

    scheduleUpdate();
    return true;
}

void IconStoreLayer::installStoreCallback() {
    Ref<IconStoreLayer> self = this;
    GalleryStore::get().setOnIconReady([self](std::string const& slug) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        if (!self->getParent()) return;
        self->onIconReady(slug);
    });
}

void IconStoreLayer::buildBackground() {
    auto win = CCDirector::get()->getWinSize();

    if (auto* bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setScaleX(win.width / bg->getContentSize().width);
        bg->setScaleY(win.height / bg->getContentSize().height);
        bg->setColor({32, 40, 104});
        addChild(bg, -5);
    } else {
        auto* flat = CCLayerColor::create(ccc4(28, 36, 96, 255));
        flat->setContentSize(win);
        addChild(flat, -5);
    }

    for (bool right : {false, true}) {
        auto* art = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sideArt_001.png");
        if (!art) continue;
        art->setAnchorPoint({right ? 1.f : 0.f, 0.f});
        art->setPosition({right ? win.width + 2.f : -2.f, -2.f});
        art->setFlipX(right);
        art->setOpacity(120);
        addChild(art, -1);
    }
}

void IconStoreLayer::buildHeader() {
    auto win = CCDirector::get()->getWinSize();
    float const rowY = win.height - 22.f;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    addChild(menu, 10);

    if (auto* spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
        spr->setScale(0.72f);
        auto* back = CCMenuItemExt::createSpriteExtra(spr,
            [this](CCMenuItemSpriteExtra*) { this->onBack(); });
        back->setPosition({20.f, rowY});
        menu->addChild(back);
    }

    if (auto* title = CCLabelBMFont::create(tr("icon-gallery.title").c_str(), "goldFont.fnt")) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setScale(0.68f);
        title->setPosition({42.f, rowY});
        addChild(title, 5);
    }

    float x = win.width - 22.f;

    if (auto* base = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_infoIcon_001.png", 1.f, CircleBaseColor::Cyan, CircleBaseSize::Small)) {
        base->setScale(0.78f);
        auto* info = CCMenuItemExt::createSpriteExtra(base, [this](CCMenuItemSpriteExtra*) {
            showMessage("", "");
            FLAlertLayer::create(tr("icon-gallery.about.title").c_str(),
                                 tr("icon-gallery.about.body").c_str(), "OK")->show();
        });
        x -= info->getScaledContentSize().width / 2.f;
        info->setPosition({x, rowY});
        menu->addChild(info);
        x -= info->getScaledContentSize().width / 2.f + 8.f;
    }

    if (auto* spr = ButtonSprite::create(tr("icon-gallery.filters.button").c_str(),
                                         "goldFont.fnt", "GJ_button_05.png", 0.8f)) {
        spr->setScale(0.6f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this](CCMenuItemSpriteExtra*) { this->onFilters(); });
        x -= btn->getScaledContentSize().width / 2.f;
        btn->setPosition({x, rowY});
        menu->addChild(btn);
    }

    float const searchW = std::min(230.f, win.width * 0.44f);
    m_search = TextInput::create(searchW, tr("icon-gallery.search").c_str(), "chatFont.fnt");
    if (m_search) {
        m_search->setPosition({24.f + searchW / 2.f, win.height - 54.f});
        Ref<IconStoreLayer> self = this;
        m_search->setCallback([self](std::string const& value) {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            self->m_query.search = value;
    // Rebuild after the input callback; the dispatcher may still walk old nodes.
            Loader::get()->queueInMainThread([self] {
                if (paimon::isRuntimeShuttingDown() || !self) return;
                if (!self->getParent()) return;
                self->setPage(0);
            });
        });
        addChild(m_search, 6);
    }

    float const tabsW = std::min(170.f, win.width * 0.32f);
    auto* tabs = ui::makeSegmentedBar(tabsW,
        {tr("icon-gallery.tab.all"), tr("icon-gallery.tab.installed")}, 0,
        [this](int index) {
            m_query.onlyInstalled = index == 1;
            Ref<IconStoreLayer> self = this;
            Loader::get()->queueInMainThread([self] {
                if (paimon::isRuntimeShuttingDown() || !self) return;
                if (!self->getParent()) return;
                self->setPage(0);
            });
        });
    if (tabs) {
        tabs->setPosition({win.width - 22.f - tabsW,
                           win.height - 54.f - ui::kSegmentedBarHeight / 2.f});
        addChild(tabs, 6);
    }
}

void IconStoreLayer::buildFooter() {
    auto win = CCDirector::get()->getWinSize();

    m_footer = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_footer) {
        m_footer->setAnchorPoint({0.f, 0.5f});
        m_footer->setScale(0.42f);
        m_footer->setColor({170, 196, 232});
        m_footer->setPosition({22.f, 18.f});
        addChild(m_footer, 6);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    addChild(menu, 10);

    m_pageLabel = CCLabelBMFont::create("", "goldFont.fnt");
    if (m_pageLabel) {
        m_pageLabel->setAnchorPoint({0.5f, 0.5f});
        m_pageLabel->setScale(0.44f);
        m_pageLabel->setPosition({win.width / 2.f, 18.f});
        addChild(m_pageLabel, 6);
    }

    auto makeArrow = [&](bool forward) -> CCMenuItemSpriteExtra* {
        auto* spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_03_001.png");
        if (!spr) return nullptr;
        spr->setScale(0.6f);
        spr->setFlipX(forward);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this, forward](CCMenuItemSpriteExtra*) {
                this->setPage(m_page + (forward ? 1 : -1));
            });
        btn->setPosition({win.width / 2.f + (forward ? 54.f : -54.f), 18.f});
        menu->addChild(btn);
        return btn;
    };
    m_prevBtn = makeArrow(false);
    m_nextBtn = makeArrow(true);
}


void IconStoreLayer::startLoading() {
    m_loading = true;

    auto win = CCDirector::get()->getWinSize();
    m_loadSpinner = LoadingSpinner::create(40.f);
    if (m_loadSpinner) {
        m_loadSpinner->setPosition({win.width / 2.f, win.height / 2.f - 6.f});
        addChild(m_loadSpinner, 7);
    }

    Ref<IconStoreLayer> self = this;
    GalleryStore::get().loadRegistry([self](Result<> res) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
    // Catalog hits may complete synchronously during init(); do not require a parent.
        self->m_loading = false;
        if (self->m_loadSpinner) {
            self->m_loadSpinner->removeFromParent();
            self->m_loadSpinner = nullptr;
        }

        if (!res) {
            self->showMessage(tr("icon-gallery.error.title"), res.unwrapErr());
            self->refreshFooter();
            return;
        }

    // Restore our leaves after More Icons rebuilds its catalog.
        GalleryInstaller::registerAllInstalled();
        self->setPage(0);
    });
}

void IconStoreLayer::showMessage(std::string const& title, std::string const& body) {
    if (!m_messageHost) return;
    m_messageHost->removeAllChildren();
    if (title.empty() && body.empty()) return;

    auto win = CCDirector::get()->getWinSize();
    if (auto* node = mkui::makeEmptyState(win.width - 80.f, title.c_str(), body.c_str())) {
        node->setPosition({-(win.width - 80.f) / 2.f, 0.f});
        m_messageHost->addChild(node);
    }
}


int IconStoreLayer::pageCount() const {
    if (m_results.empty()) return 1;
    return static_cast<int>((m_results.size() + kPageSize - 1) / kPageSize);
}

void IconStoreLayer::setPage(int page) {
    m_results = GalleryStore::get().query(m_query);

    int const pages = pageCount();
    m_page = std::clamp(page, 0, pages - 1);

    rebuildGrid();
    refreshFooter();
    requestVisiblePage();
}

void IconStoreLayer::rebuildGrid() {
    if (!m_scrollHost) return;

    m_cards.clear();
    m_scrollHost->removeAllChildren();
    m_scroll = nullptr;
    m_wheelTargetSet = false;

    auto win = CCDirector::get()->getWinSize();
    float const scrollW = win.width - 40.f;
    float const scrollH = win.height - kHeaderH - kFooterH;

    if (m_results.empty()) {
        showMessage(
            m_query.onlyInstalled ? tr("icon-gallery.empty.installed.title")
                                  : tr("icon-gallery.empty.title"),
            m_query.onlyInstalled ? tr("icon-gallery.empty.installed.body")
                                  : tr("icon-gallery.empty.body"));
        return;
    }
    showMessage("", "");

    auto const start = static_cast<std::size_t>(m_page) * kPageSize;
    auto const end = std::min(m_results.size(), start + kPageSize);

    int const perRow = std::max(1,
        static_cast<int>((scrollW + kCardGap) / (kCardW + kCardGap)));
    int const shown = static_cast<int>(end - start);
    int const rowCount = (shown + perRow - 1) / perRow;
    float const gridW = static_cast<float>(perRow) * (kCardW + kCardGap) - kCardGap;
    float const originX = (scrollW - gridW) / 2.f;

    m_scroll = ScrollLayer::create({scrollW, scrollH});
    if (!m_scroll) return;
    m_scroll->setPosition({0.f, 0.f});
    m_scrollHost->addChild(m_scroll);

    auto* content = m_scroll->m_contentLayer;
    float const contentH = std::max(scrollH,
        static_cast<float>(rowCount) * (kCardH + kCardGap) - kCardGap + 8.f);
    content->setContentSize({scrollW, contentH});

    auto const& icons = GalleryStore::get().icons();
    for (std::size_t i = start; i < end; ++i) {
        auto const index = m_results[i];
        if (index >= icons.size()) continue;

        auto slug = icons[index].slug;
        Ref<IconStoreLayer> self = this;
        auto* card = IconStoreCard::create(slug, kCardW, kCardH,
            [self](std::string const& pressed) {
                if (paimon::isRuntimeShuttingDown() || !self) return;
                self->onIconPressed(pressed);
            });
        if (!card) continue;

        int const slot = static_cast<int>(i - start);
        int const col = slot % perRow;
        int const row = slot / perRow;
        card->setPosition({
            originX + static_cast<float>(col) * (kCardW + kCardGap),
            contentH - 4.f - static_cast<float>(row + 1) * kCardH
                - static_cast<float>(row) * kCardGap,
        });

        card->setScale(0.75f);
        card->setOpacity(0);
        float const delay = static_cast<float>(slot) * 0.014f;
        card->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CCSpawn::create(
                CCEaseBackOut::create(CCScaleTo::create(0.18f, 1.0f)),
                CCFadeIn::create(0.12f),
                nullptr
            ),
            nullptr
        ));

        content->addChild(card);
        m_cards[slug] = card;
    }

    m_scroll->moveToTop();
}

void IconStoreLayer::refreshFooter() {
    auto& store = GalleryStore::get();

    if (m_footer) {
        m_footer->setString(fmt::format("{} {}  -  {} {}",
            store.icons().size(), tr("icon-gallery.footer.icons"),
            store.installedCount(), tr("icon-gallery.footer.installed")).c_str());
    }
    if (m_pageLabel) {
        m_pageLabel->setString(
            fmt::format("{} / {}", m_page + 1, pageCount()).c_str());
        m_pageLabel->setVisible(!m_results.empty());
    }
    if (m_prevBtn) m_prevBtn->setVisible(m_page > 0);
    if (m_nextBtn) m_nextBtn->setVisible(m_page + 1 < pageCount());
}

void IconStoreLayer::requestVisiblePage() {
    auto& store = GalleryStore::get();
    for (auto const& [slug, card] : m_cards) {
        store.requestIcon(slug);
    }
    // Cache hits may callback before attachment; repaint here as well.
    for (auto const& [slug, card] : m_cards) {
        if (card) card->refresh();
    }
}


void IconStoreLayer::onIconReady(std::string const& slug) {
    // Empty id means the whole catalog changed.
    if (slug.empty()) {
        setPage(m_page);
        return;
    }
    if (auto it = m_cards.find(slug); it != m_cards.end() && it->second) {
        it->second->refresh();
    }
    refreshFooter();
}

void IconStoreLayer::onIconPressed(std::string const& slug) {
    Ref<IconStoreLayer> self = this;
    auto* popup = IconDetailPopup::create(slug, [self, slug] {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        if (!self->getParent()) return;
        if (auto it = self->m_cards.find(slug); it != self->m_cards.end() && it->second) {
            it->second->refresh();
        }
        self->refreshFooter();
    });
    if (popup) popup->show();
}

void IconStoreLayer::onFilters() {
    Ref<IconStoreLayer> self = this;
    auto* popup = IconFilterPopup::create(m_query,
        [self](GalleryStore::Query const& updated) {
            if (paimon::isRuntimeShuttingDown() || !self) return;
            if (!self->getParent()) return;
            auto search = self->m_query.search;
            auto onlyInstalled = self->m_query.onlyInstalled;
            self->m_query = updated;
            self->m_query.search = search;
            self->m_query.onlyInstalled = onlyInstalled;
            self->setPage(0);
        });
    if (popup) popup->show();
}


void IconStoreLayer::onEnter() {
    CCLayer::onEnter();

    Ref<IconStoreLayer> self = this;
    GalleryStore::get().setOnIconReady([self](std::string const& slug) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        if (!self->getParent()) return;
        self->onIconReady(slug);
    });

    // Installed state may have changed while the detail view was open.
    if (m_enteredOnce && !m_loading) {
        setPage(m_page);
    }
    m_enteredOnce = true;
}

void IconStoreLayer::onExit() {
    GalleryStore::get().clearOnIconReady();
    CCLayer::onExit();
}

void IconStoreLayer::update(float dt) {
    kit::stepWheelScroll(m_scroll, m_wheelTargetY, m_wheelTargetSet, dt);
}

void IconStoreLayer::scrollWheel(float x, float y) {
    if (kit::queueWheelScroll(m_scroll, x, y, m_wheelTargetY, m_wheelTargetSet)) {
        return;
    }
    CCLayer::scrollWheel(x, y);
}

void IconStoreLayer::keyBackClicked() {
    onBack();
}

void IconStoreLayer::onBack() {
    GalleryStore::get().clearOnIconReady();
    CCDirector::get()->popSceneWithTransition(0.5f, PopTransition::kPopTransitionFade);
}

}
