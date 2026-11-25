#include "PaimonMultiSettingsPanel.hpp"
#include "SettingsCategoryBuilder.hpp"
#include "SettingsControls.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/FluidReveal.hpp"
#include "../../../utils/Localization.hpp"
#include "../services/SettingsPanelManager.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../blur/PopupBlurService.hpp"
#include "../../../core/Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace cocos2d;
using namespace geode::prelude;

namespace {
constexpr float kSidebarBtnScale = 1.f;
}

PaimonMultiSettingsPanel* PaimonMultiSettingsPanel::create(CCSprite* blurBg, int initialCategory) {
    auto ret = new PaimonMultiSettingsPanel();
    if (ret && ret->init(blurBg, initialCategory)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PaimonMultiSettingsPanel::init(CCSprite* blurBg, int initialCategory) {
    if (!CCLayer::init()) return false;

    this->setID("paimon-multisettings-panel"_spr);
    auto winSize = CCDirector::get()->getWinSize();

    if (blurBg) {
        m_blurBg = blurBg;
        m_blurBg->setPosition(winSize * 0.5f);
        m_blurBg->setOpacity(0);
        this->addChild(m_blurBg, -2);
    }

    m_darkOverlay = CCLayerColor::create(ccc4(0, 0, 0, 0));
    m_darkOverlay->setContentSize(winSize);
    this->addChild(m_darkOverlay, -1);

    m_panelContainer = CCNode::create();
    m_panelContainer->setContentSize({PANEL_W, PANEL_H});
    m_panelContainer->setAnchorPoint({0.5f, 0.5f});
    m_panelContainer->setPosition(winSize * 0.5f);
    this->addChild(m_panelContainer, 1);

    auto* panelBg = NineSlice::create("GJ_square01.png");
    panelBg->setContentSize({PANEL_W, PANEL_H});
    panelBg->setAnchorPoint({0.f, 0.f});
    m_panelBg = panelBg;
    m_panelContainer->addChild(m_panelBg);
    m_panelScale = std::min({1.f, (winSize.width - 24.f) / PANEL_W, (winSize.height - 24.f) / PANEL_H});
    m_panelContainer->setScale(m_panelScale);

    auto* dispatcher = CCDirector::get()->getTouchDispatcher();
    int basePrio = dispatcher->getTargetPrio();
    m_touchPrio = basePrio - 1;
    m_childTouchPrio = basePrio - 2;

    buildTitleBar();
    buildSidebar();
    buildContentArea();

    auto const& groups = paimon::settings_ui::getAllGroups();
    int clampedCategory = initialCategory;
    if (clampedCategory < 0 || clampedCategory >= static_cast<int>(groups.size())) {
        clampedCategory = 0;
    }
    selectCategory(clampedCategory);

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(m_touchPrio);
    this->setKeypadEnabled(true);

    runEntryAnimation();

    return true;
}

void PaimonMultiSettingsPanel::buildTitleBar() {
    m_titleBarBg = nullptr;

    m_titleLabel = paimon::ui::makeTitle("Paimon Settings", 200.f, 0.58f);
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->setPosition({30.f, PANEL_H - TITLE_BAR_H / 2.f - 2.f});
    m_panelContainer->addChild(m_titleLabel, 2);

    m_searchInput = geode::TextInput::create(210.f, "Search...", "chatFont.fnt");
    m_searchInput->setScale(0.65f);
    m_searchInput->setAnchorPoint({0.5f, 0.5f});
    m_searchInput->setPosition({PANEL_W - 82.f, PANEL_H - TITLE_BAR_H / 2.f - 2.f});
    m_searchInput->setCallback(
        paimon::ui::safeTextInputCallback<PaimonMultiSettingsPanel>(
            this, &PaimonMultiSettingsPanel::onSearchChanged
        )
    );
    m_panelContainer->addChild(m_searchInput, 2);

    auto closeMenu = CCMenu::create();
    closeMenu->setPosition({0.f, 0.f});
    closeMenu->setTouchPriority(m_childTouchPrio);
    m_panelContainer->addChild(closeMenu, 2);

    auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
    closeSpr->setScale(0.6f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(PaimonMultiSettingsPanel::onClose));
    closeBtn->setPosition({4.f, PANEL_H - 4.f});
    closeMenu->addChild(closeBtn);
}

void PaimonMultiSettingsPanel::buildSidebar() {
    m_sidebarBg = paimon::ui::makeInset({SIDEBAR_W - 12.f, CONTENT_H - 12.f}, 90);
    m_sidebarBg->setPosition({6.f, 6.f});
    m_panelContainer->addChild(m_sidebarBg, 1);
    m_sidebarMenu = CCMenu::create();
    m_sidebarMenu->setPosition({0.f, 0.f});
    m_sidebarMenu->setTouchPriority(m_childTouchPrio);
    m_panelContainer->addChild(m_sidebarMenu, 2);
    auto const& groups = paimon::settings_ui::getAllGroups();
    // the old fixed 29-unit step pushed the 9th group past the panel edge.
    float const firstY = CONTENT_H - 20.f;
    float const lastY = 20.f;
    float const step = std::min(24.f,
        (firstY - lastY) / std::max(1.f, static_cast<float>(groups.size()) - 1.f));
    float const scale = std::clamp(step / 30.f * 0.8f, 0.42f, 0.6f);
    for (size_t i = 0; i < groups.size(); ++i) {
        auto* face = paimon::ui::makeButtonSprite(groups[i].name.c_str(), paimon::ui::Btn::Gray,
            SIDEBAR_W - 24.f, scale, "bigFont.fnt");
        auto* button = CCMenuItemExt::createSpriteExtra(face, [this, i](CCMenuItemSpriteExtra*) {
            selectCategory(static_cast<int>(i));
        });
        button->setPosition({SIDEBAR_W / 2.f, firstY - step * static_cast<float>(i)});
        button->m_scaleMultiplier = 1.f;
        m_sidebarMenu->addChild(button);
        m_sidebarButtons.push_back(button);
    }
}

void PaimonMultiSettingsPanel::buildContentArea() {
    float scrollW = CONTENT_W;
    float scrollH = CONTENT_H;

    m_scrollLayer = geode::ScrollLayer::create({scrollW, scrollH});
    if (m_scrollLayer) m_scrollLayer->setAnchorPoint({0.f, 0.f});
    m_scrollLayer->setPosition({SIDEBAR_W, 0.f});
    m_scrollLayer->setTouchEnabled(true);
    m_scrollLayer->setTouchPriority(m_childTouchPrio);
    m_panelContainer->addChild(m_scrollLayer, 2);
}

void PaimonMultiSettingsPanel::selectCategory(int index) {
    auto const& groups = paimon::settings_ui::getAllGroups();
    if (index < 0 || index >= static_cast<int>(groups.size())) return;

    m_selectedCategory = index;

    if (m_searchInput) m_searchInput->setString("");

    updateSidebarAccent();
    rebuildContent();
}

void PaimonMultiSettingsPanel::rebuildContent() {
    cancelSearch();
    if (!m_scrollLayer) return;

    auto contentLayer = m_scrollLayer->m_contentLayer;
    contentLayer->removeAllChildren();

    auto const& groups = paimon::settings_ui::getAllGroups();
    if (m_selectedCategory < 0 || m_selectedCategory >= static_cast<int>(groups.size())) return;

    auto const& group = groups[m_selectedCategory];

    std::vector<CCNode*> allRows;

    for (auto const& sub : group.subcategories) {

        auto subContainer = CCNode::create();
        subContainer->setAnchorPoint({0.f, 0.f});
        sub.buildContent(subContainer, CONTENT_W);


        float subH = 0.f;
        if (auto children = subContainer->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                subH += child->getContentSize().height;
            }
        }
        subContainer->setContentSize({CONTENT_W, subH});


        float yPos = subH;
        if (auto children = subContainer->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                yPos -= child->getContentSize().height;
                child->setPosition({0.f, yPos});
            }
        }


        auto header = paimon::settings_ui::createCollapsibleHeader(
            sub.name.c_str(), CONTENT_W, subContainer, true,
            [this]() { relayoutContent(); }
        );

        allRows.push_back(header);
        allRows.push_back(subContainer);
    }


    float totalH = 0.f;
    for (auto* row : allRows) {
        if (row->isVisible()) {
            totalH += row->getContentSize().height;
        }
    }

    float contentH = std::max(totalH, CONTENT_H);
    m_scrollLayer->setContentLayerSize({CONTENT_W, contentH});

    float currentY = contentH;
    for (auto* row : allRows) {
        contentLayer->addChild(row, 0);
        if (row->isVisible()) {
            float h = row->getContentSize().height;
            currentY -= h;
            row->setPosition({0.f, currentY});
        }
    }

    m_scrollLayer->moveToTop();
}

void PaimonMultiSettingsPanel::relayoutContent() {
    if (!m_scrollLayer) return;

    auto contentLayer = m_scrollLayer->m_contentLayer;
    auto children = contentLayer->getChildren();
    if (!children) return;

    // virtualized children may be hidden, so use their content sizes directly.
    float totalH = 0.f;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        totalH += child->getContentSize().height;
    }

    float contentH = std::max(totalH, CONTENT_H);
    m_scrollLayer->setContentLayerSize({CONTENT_W, contentH});

    float currentY = contentH;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        float h = child->getContentSize().height;
        currentY -= h;
        child->setPosition({0.f, currentY});
    }
    m_scrollLayer->doConstraintContent(true);
}

void PaimonMultiSettingsPanel::setSelectedCategory(int index) {
    selectCategory(index);
}

void PaimonMultiSettingsPanel::updateSidebarAccent() {
    if (m_selectedCategory < 0 || m_selectedCategory >= static_cast<int>(m_sidebarButtons.size())) return;

    for (size_t i = 0; i < m_sidebarButtons.size(); i++) {
        bool const sel = (static_cast<int>(i) == m_selectedCategory);
        auto* btn = m_sidebarButtons[i];
        if (!btn) continue;
        btn->stopAllActions();
        btn->setScale(kSidebarBtnScale);
        paimon::ui::setButtonSkin(btn, sel ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    paimon::ui::matchButtonLabels(m_sidebarButtons);
}

void PaimonMultiSettingsPanel::onSearchChanged(std::string const& query) {
    cancelSearch();
    if (m_isClosing) return;
    m_searchQuery = geode::utils::string::toLower(query);
    if (m_searchQuery.empty()) {
        rebuildContent();
        return;
    }
    scheduleOnce(schedule_selector(PaimonMultiSettingsPanel::startPendingSearch), 0.12f);
}

void PaimonMultiSettingsPanel::cancelSearch() {
    unschedule(schedule_selector(PaimonMultiSettingsPanel::startPendingSearch));
    unschedule(schedule_selector(PaimonMultiSettingsPanel::processSearchBatch));
    m_searchJobs.clear();
    m_searchRows.clear();
    m_searchJobIndex = 0;
}

void PaimonMultiSettingsPanel::startPendingSearch(float) {
    buildSearchResults(m_searchQuery);
}

void PaimonMultiSettingsPanel::buildSearchResults(std::string const& query) {
    cancelSearch();
    if (!m_scrollLayer || m_isClosing) return;
    m_searchQuery = query;
    auto const& groups = paimon::settings_ui::getAllGroups();
    for (auto const& group : groups) {
        for (auto const& sub : group.subcategories) m_searchJobs.push_back(&sub);
    }
    schedule(schedule_selector(PaimonMultiSettingsPanel::processSearchBatch));
}

void PaimonMultiSettingsPanel::processSearchBatch(float) {
    if (m_isClosing || !m_scrollLayer) { cancelSearch(); return; }
    // build only one subcategory per frame. Temporary controls are released at
    // the end of this frame; only matching rows survive until publication.
    if (m_searchJobIndex < m_searchJobs.size()) {
        auto* container = CCNode::create();
        m_searchJobs[m_searchJobIndex++]->buildContent(container, CONTENT_W);
        std::vector<CCNode*> matches;
        for (auto* row : CCArrayExt<CCNode*>(container->getChildren())) {
            for (auto* child : CCArrayExt<CCNode*>(row->getChildren())) {
                auto* label = typeinfo_cast<CCLabelBMFont*>(child);
                if (label && geode::utils::string::toLower(label->getString()).find(m_searchQuery) != std::string::npos) {
                    matches.push_back(row);
                    break;
                }
            }
        }
        for (auto* row : matches) {
            m_searchRows.emplace_back(row);
            row->removeFromParent();
        }
    }
    if (m_searchJobIndex == m_searchJobs.size()) showSearchResults();
}

void PaimonMultiSettingsPanel::showSearchResults() {
    auto* contentLayer = m_scrollLayer->m_contentLayer;
    contentLayer->removeAllChildren();
    if (m_searchRows.empty()) {
        auto* hint = paimon::settings_ui::createHintRow(
            Localization::get().getLanguage() == Localization::Language::SPANISH
                ? "Sin resultados. Prueba otro nombre." : "No results. Try another name.", CONTENT_W);
        m_searchRows.emplace_back(hint);
    }
    float totalH = 0.f;
    for (auto const& row : m_searchRows) totalH += row->getContentSize().height;

    float contentH = std::max(totalH, CONTENT_H);
    m_scrollLayer->setContentLayerSize({CONTENT_W, contentH});

    float currentY = contentH;
    for (auto const& row : m_searchRows) {
        float h = row->getContentSize().height;
        currentY -= h;
        row->setPosition({0.f, currentY});
        contentLayer->addChild(row.data(), 0);
    }

    m_scrollLayer->moveToTop();
    cancelSearch();
}

void PaimonMultiSettingsPanel::runEntryAnimation() {
    auto cfg = paimon::popupblur::getConfig();
    int const darkness = std::clamp(static_cast<int>(std::round(cfg.darkness * 255.f)), 100, 220);
    if (!paimon::ui::motionEnabled()) {
        m_darkOverlay->setOpacity(darkness);
        if (m_blurBg) m_blurBg->setOpacity(255);
        return;
    }
    float const duration = paimon::ui::motionDuration(0.20f);
    m_darkOverlay->runAction(CCFadeTo::create(duration, darkness));
    if (m_blurBg) m_blurBg->runAction(CCFadeTo::create(duration, 255));
    m_panelContainer->setScale(m_panelScale * 0.97f);
    m_panelContainer->runAction(CCEaseSineOut::create(CCScaleTo::create(duration, m_panelScale)));
}

void PaimonMultiSettingsPanel::animateClose() {
    if (m_isClosing) return;
    m_isClosing = true;
    cancelSearch();

    paimon::ui::detachGeodeTextInput(m_searchInput);

    this->setTouchEnabled(false);
    if (!paimon::ui::motionEnabled()) { onCloseFinished(); return; }
    m_panelContainer->stopAllActions();

    if (m_darkOverlay) {
        m_darkOverlay->runAction(CCFadeTo::create(0.15f, 0));
    }

    if (m_blurBg) {
        m_blurBg->runAction(CCFadeTo::create(0.15f, 0));
    }

    if (m_panelContainer) {
        auto scaleAction = CCEaseExponentialIn::create(CCScaleTo::create(paimon::ui::motionDuration(0.15f), m_panelScale * 0.97f));
        auto callback = CCCallFunc::create(this, callfunc_selector(PaimonMultiSettingsPanel::onCloseFinished));
        auto seq = CCSequence::create(scaleAction, callback, nullptr);
        m_panelContainer->runAction(seq);
    } else {
        onCloseFinished();
    }
}

void PaimonMultiSettingsPanel::onCloseFinished() {
    float fadeDur = std::clamp(
        static_cast<float>(paimon::settings::popupblur::fadeDuration()),
        0.0f, 0.6f);
    paimon::popupblur::cleanupWithFade(this, fadeDur);
    SettingsPanelManager::get().notifyPanelRemoved();
    this->removeFromParent();
}

void PaimonMultiSettingsPanel::onClose(CCObject*) {
    animateClose();
}

void PaimonMultiSettingsPanel::onExit() {
    cancelSearch();
    paimon::ui::detachGeodeTextInput(m_searchInput);
    m_searchInput = nullptr;
    paimon::popupblur::cleanup(this);
    SettingsPanelManager::get().notifyPanelRemoved();
    CCLayer::onExit();
}

bool PaimonMultiSettingsPanel::isTouchInTitleBar(CCPoint const& worldPos) {
    if (!m_panelContainer) return false;
    auto panelWorldPos = m_panelContainer->convertToWorldSpace({0.f, PANEL_H - TITLE_BAR_H});
    CCRect titleRect(panelWorldPos.x, panelWorldPos.y, PANEL_W * m_panelContainer->getScaleX(), TITLE_BAR_H * m_panelContainer->getScaleY());
    return titleRect.containsPoint(worldPos);
}

bool PaimonMultiSettingsPanel::isTouchInPanel(CCPoint const& worldPos) {
    if (!m_panelContainer) return false;
    auto panelWorldPos = m_panelContainer->convertToWorldSpace({0.f, 0.f});
    float scX = m_panelContainer->getScaleX();
    float scY = m_panelContainer->getScaleY();
    CCRect panelRect(panelWorldPos.x, panelWorldPos.y, PANEL_W * scX, PANEL_H * scY);
    return panelRect.containsPoint(worldPos);
}

bool PaimonMultiSettingsPanel::isTouchInSearchInput(CCPoint const& worldPos) const {
    if (!m_searchInput || !m_searchInput->isVisible() || !m_panelContainer) return false;
    auto localPos = m_panelContainer->convertToNodeSpace(worldPos);
    return m_searchInput->boundingBox().containsPoint(localPos);
}

void PaimonMultiSettingsPanel::keyBackClicked() {
    animateClose();
}

bool PaimonMultiSettingsPanel::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (m_isClosing) return false;

    auto touchPos = touch->getLocation();

    if (isTouchInTitleBar(touchPos) && !isTouchInSearchInput(touchPos)) {
        m_isDragging = true;
        m_dragOffset = ccpSub(m_panelContainer->getPosition(), touchPos);
        return true;
    }

    if (isTouchInPanel(touchPos)) {
        return true;
    }

    animateClose();
    return true;
}

void PaimonMultiSettingsPanel::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_isDragging) return;
    auto touchPos = touch->getLocation();
    auto const win = CCDirector::get()->getWinSize();
    auto position = ccpAdd(touchPos, m_dragOffset);
    float const halfW = PANEL_W * m_panelScale / 2.f;
    float const halfH = PANEL_H * m_panelScale / 2.f;
    position.x = std::clamp(position.x, halfW + 6.f, std::max(halfW + 6.f, win.width - halfW - 6.f));
    position.y = std::clamp(position.y, halfH + 6.f, std::max(halfH + 6.f, win.height - halfH - 6.f));
    m_panelContainer->setPosition(position);
}

void PaimonMultiSettingsPanel::ccTouchEnded(CCTouch*, CCEvent*) {
    m_isDragging = false;
}

void PaimonMultiSettingsPanel::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_isDragging = false;
}
