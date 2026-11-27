#include "ProfileLevelsPanel.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/WebHelper.hpp"
#include "../../../utils/GDRobTopCache.hpp"
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/LevelCell.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <memory>

using namespace geode::prelude;

namespace paimon::profile_redesign {
namespace {
char const* localized(char const* spanish, char const* english) {
    return Localization::get().getLanguage() == Localization::Language::SPANISH ? spanish : english;
}

void prepareCellMenus(CCNode* node, int priority) {
    if (auto* menu = typeinfo_cast<CCMenu*>(node)) menu->setTouchPriority(priority);
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) prepareCellMenus(child, priority);
}

void storeLevelMetadata(std::string const& response) {
    auto const sections = geode::utils::string::split(response, "#");
    if (sections.size() < 3) return;
    GameLevelManager::get()->storeUserNames(sections[1]);
    // match GD 2.2091's online-level response handler; it normalizes both
    // legacy song records and the newer music-library metadata.
    if (!sections[2].empty()) MusicDownloadManager::sharedState()->createSongsInfo(sections[2], "");
}
}

ProfileLevelsPanel* ProfileLevelsPanel::create(int userID, CCSize size) {
    auto panel = std::make_unique<ProfileLevelsPanel>();
    if (!panel->initialize(userID, size)) return nullptr;
    panel->autorelease();
    return panel.release();
}

bool ProfileLevelsPanel::initialize(int userID, CCSize size) {
    if (!CCNode::init()) return false;
    m_userID = userID;
    setContentSize(size);
    m_scroll = ProfileScrollLayer::create({size.width, std::max(10.f, size.height - 20.f)});
    m_scroll->setID("rd-levels-scroll"_spr);
    m_scroll->setPosition({0.f, 20.f});
    m_scroll->setStealingTouches(true);
    addChild(m_scroll);

    auto* menu = ui::makeZeroMenu();
    addChild(menu, 2);
    m_previous = ui::makeFrameButton("GJ_arrow_01_001.png", .38f, [this] { changePage(-1); });
    if (m_previous) {
        m_previous->setID("rd-levels-previous"_spr);
        m_previous->setPosition({10.f, 9.f});
        menu->addChild(m_previous);
    }
    m_next = ui::makeFrameButton("GJ_arrow_01_001.png", .38f, [this] { changePage(1); });
    if (m_next) {
        m_next->setID("rd-levels-next"_spr);
        if (auto* spr = typeinfo_cast<CCSprite*>(m_next->getNormalImage())) {
            spr->setFlipX(true);
        }
        m_next->setPosition({size.width - 10.f, 9.f});
        menu->addChild(m_next);
    }
    m_retry = ui::makeButton(localized("Reintentar", "Retry"), [this] { retryLoading(); }, ui::Btn::Cyan, 70.f, .45f);
    if (m_retry) {
        m_retry->setID("rd-levels-retry"_spr);
        m_retry->setPosition({size.width * .5f, 9.f});
        m_retry->setVisible(false);
        menu->addChild(m_retry);
    }
    m_pageLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_pageLabel->setID("rd-levels-page-label"_spr);
    m_pageLabel->setScale(.4f);
    m_pageLabel->setPosition({size.width * .5f, 9.f});
    addChild(m_pageLabel);
    setActive(false);
    return true;
}

void ProfileLevelsPanel::setActive(bool active) {
    setVisible(active);
    m_scroll->setTouchEnabled(active);
    m_scroll->setMouseEnabled(active);
    if (active == m_active) return;
    m_active = active;
    if (!active) {
        cancelLoading();
        return;
    }
    if (!m_catalog.complete()) requestNextPage();
}

void ProfileLevelsPanel::onEnter() {
    CCNode::onEnter();
    if (m_active && !m_catalog.complete()) requestNextPage();
}

void ProfileLevelsPanel::onExit() {
    // a pushed level scene temporarily exits this tree. Preserve the selected
    // tab and its rows so returning to the profile restores the same view.
    cancelLoading();
    CCNode::onExit();
}

void ProfileLevelsPanel::cancelLoading() {
    ++m_generation;
    m_request.cancel();
    m_loading = false;
}

void ProfileLevelsPanel::showStatus(std::string text) {
    m_scroll->m_contentLayer->removeAllChildren();
    m_visibleLevels.clear();
    auto const size = m_scroll->getContentSize();
    m_scroll->m_contentLayer->setContentSize(size);
    auto* label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    label->limitLabelWidth(size.width - 12.f, .5f, .2f);
    label->setPosition(size / 2.f);
    m_scroll->m_contentLayer->addChild(label);
    m_scroll->scrollToTop();
    m_previous->setVisible(false);
    m_next->setVisible(false);
}

void ProfileLevelsPanel::showError(std::string message) {
    m_loading = false;
    showStatus(message.empty() ? localized("No se pudieron cargar los niveles", "Could not load levels") : std::move(message));
    m_pageLabel->setVisible(false);
    m_retry->setVisible(true);
}

void ProfileLevelsPanel::retryLoading() {
    if (m_catalog.complete()) {
        m_catalog = ProfileLevelCatalog{};
        m_page = 0;
    }
    requestNextPage();
}

void ProfileLevelsPanel::requestNextPage() {
    if (!m_active || m_loading || m_catalog.complete()) return;
    if (m_userID <= 0) {
        log::warn("[ProfileLevels] Cannot request levels without a creator user ID");
        showError();
        return;
    }
    auto const url = std::string(gd::kRobTopBaseUrl) + "getGJLevels21.php";
    if (!paimon::ban::authorizeWebRequest("POST", url)) {
        log::warn("[ProfileLevels] Waiting for the mod's verified online session");
        showError(localized("Esperando conexión del mod. Vuelve a intentarlo.", "Waiting for the mod connection. Please retry."));
        return;
    }
    m_loading = true;
    m_retry->setVisible(false);
    showStatus(localized("Cargando niveles...", "Loading levels..."));
    m_pageLabel->setVisible(true);
    m_pageLabel->setString(m_catalog.total() ? fmt::format("{} / {}", m_catalog.levels().size(), m_catalog.total()).c_str() : "");
    auto request = web::WebRequest();
    request.timeout(asp::Duration::fromSecs(15));
    request.header("Content-Type", "application/x-www-form-urlencoded");
    request.header("User-Agent", "");
    request.bodyString(fmt::format("gameVersion=22&binaryVersion=51&gdw=0&type=5&str={}&page={}&total=0&secret=Wmfd2893gb7",
        m_userID, m_catalog.nextPage()));
    auto const generation = m_generation;
    WeakRef<ProfileLevelsPanel> self = this;
    WebHelper::dispatchOwned(m_request, std::move(request), "POST", url,
        [self, generation](web::WebResponse response) {
            auto panel = self.lock();
            if (!panel || !panel->m_active || generation != panel->m_generation) return;
            if (!response.ok()) log::warn("[ProfileLevels] Page {} request failed (HTTP {})", panel->m_catalog.nextPage(), response.code());
            panel->acceptPage(response.ok(), response.string().unwrapOr(""));
        });
}

void ProfileLevelsPanel::acceptPage(bool ok, std::string response) {
    m_loading = false;
    // responses may end in CR/LF; preserve spaces inside level names.
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) response.pop_back();
    if (!ok) { showError(); return; }
    if (!m_catalog.appendResponse(response, m_userID)) {
        log::warn("[ProfileLevels] Invalid response on page {} ({} bytes)", m_catalog.nextPage(), response.size());
        showError();
        return;
    }
    if (response != "-1") storeLevelMetadata(response);
    if (m_catalog.complete()) { renderPage(); return; }
    m_pageLabel->setString(fmt::format("{} / {}", m_catalog.levels().size(), m_catalog.total()).c_str());
    auto const generation = m_generation;
    WeakRef<ProfileLevelsPanel> self = this;
    paimon::scheduleMainThreadDelay(.1f, [self, generation] {
        auto panel = self.lock();
        if (panel && panel->m_active && generation == panel->m_generation) panel->requestNextPage();
    });
}

void ProfileLevelsPanel::changePage(int direction) {
    if (!m_active || !m_catalog.complete()) return;
    if (direction < 0 && m_page > 0) --m_page;
    else if (direction > 0 && m_page + 1 < m_catalog.pageCount()) ++m_page;
    else return;
    renderPage();
}

void ProfileLevelsPanel::renderPage() {
    m_retry->setVisible(false);
    m_pageLabel->setVisible(true);
    if (m_catalog.levels().empty()) {
        showStatus(localized("Este creador no tiene niveles publicados", "This creator has no published levels"));
        m_pageLabel->setString("");
        return;
    }
    m_scroll->m_contentLayer->removeAllChildren();
    m_visibleLevels.clear();
    auto const levels = m_catalog.page(m_page);
    if (levels.empty()) {
        showStatus(localized("Este creador no tiene niveles publicados", "This creator has no published levels"));
        return;
    }
    float const width = m_scroll->getContentWidth();
    constexpr float naturalHeight = 44.f;
    constexpr float rowGap = 3.f;
    float const naturalWidth = std::max(350.f, width);
    float const scale = std::min(width / naturalWidth,
        std::max(1.f, (m_scroll->getContentHeight() - rowGap) * .5f) / naturalHeight);
    float const height = naturalHeight * scale;
    float const totalHeight = std::max(m_scroll->getContentHeight(),
        static_cast<float>(levels.size()) * height + static_cast<float>(levels.size() - 1) * rowGap);
    m_scroll->m_contentLayer->setContentSize({width, totalHeight});
    float y = totalHeight;
    for (auto const& entry : levels) {
        auto* dictionary = GameLevelManager::responseToDict(entry.encoded, false);
        auto* level = dictionary ? GJGameLevel::create(dictionary, true) : nullptr;
        if (!level) {
            log::warn("[ProfileLevels] Could not decode level {}", entry.id);
            showError();
            return;
        }
        m_visibleLevels.emplace_back(level);
        auto* cell = LevelCell::create(naturalWidth, naturalHeight);
        if (!cell) { showError(); return; }
        cell->setContentSize({naturalWidth, naturalHeight});
        cell->setScale(scale);
        cell->ignoreAnchorPointForPosition(false);
        cell->setAnchorPoint({0.f, 0.f});
        cell->setPosition({(width - naturalWidth * scale) * .5f, 0.f});

        // Geode culls direct scroll children by their bounds. Keep those bounds
        // unscaled so rows remain visible while their cells cross the viewport.
        auto* row = CCNode::create();
        row->setContentSize({width, height});
        row->ignoreAnchorPointForPosition(false);
        row->setAnchorPoint({0.f, 0.f});
        y -= height;
        row->setPosition({0.f, y});
        m_scroll->m_contentLayer->addChild(row);
        row->addChild(cell);
        // the LevelCell hook needs its profile ancestors on the first load to
        // choose the compact layout without rebuilding the cell onEnter.
        cell->loadFromLevel(level);
        prepareCellMenus(cell, m_scroll->getTouchPriority() - 1);
        y -= rowGap;
    }
    m_scroll->scrollToTop();
    m_previous->setVisible(true);
    m_next->setVisible(true);
    m_previous->setEnabled(m_page > 0);
    m_next->setEnabled(m_page + 1 < m_catalog.pageCount());
    m_previous->setOpacity(m_page > 0 ? 255 : 90);
    m_next->setOpacity(m_page + 1 < m_catalog.pageCount() ? 255 : 90);
    m_pageLabel->setString(fmt::format("{} | {}/{} ({})", localized("Más likes", "Most liked"),
        m_page + 1, m_catalog.pageCount(), m_catalog.levels().size()).c_str());
    m_pageLabel->limitLabelWidth(width - 46.f, .4f, .2f);
}

} // namespace paimon::profile_redesign
