#include "PaimonMultiSettingsPanel.hpp"
#include "SettingsCategoryBuilder.hpp"
#include "SettingsControls.hpp"
#include "../services/SettingsPanelManager.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../blur/PopupBlurService.hpp"
#include "../../../core/Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

using namespace cocos2d;
using namespace geode::prelude;

namespace {
constexpr float kSidebarBtnScale = 0.46f;
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

    m_panelBg = paimon::SpriteHelper::safeCreateScale9("GJ_square06.png");
    if (!m_panelBg) {
        // Fallback when the GD panel texture is unavailable.
        m_panelBg = paimon::SpriteHelper::createColorPanel(
            PANEL_W, PANEL_H, cocos2d::ccColor3B{255, 255, 255}, 255, CORNER_RADIUS
        );
    }
    if (m_panelBg) {
        m_panelBg->setContentSize({PANEL_W, PANEL_H});
        m_panelBg->setAnchorPoint({0.f, 0.f});
        m_panelBg->setPosition({0.f, 0.f});
        m_panelBg->setColor({34, 46, 96});
        m_panelBg->setOpacity(255);
        m_panelContainer->addChild(m_panelBg, 0);
    }

    buildTitleBar();
    buildSidebar();
    buildContentArea();

    auto const& groups = paimon::settings_ui::getAllGroups();
    int clampedCategory = initialCategory;
    if (clampedCategory < 0 || clampedCategory >= static_cast<int>(groups.size())) {
        clampedCategory = 0;
    }
    selectCategory(clampedCategory);

    auto* dispatcher = CCDirector::get()->getTouchDispatcher();
    int basePrio = dispatcher->getTargetPrio();
    m_touchPrio = basePrio - 1;
    m_childTouchPrio = basePrio - 2;

    this->setTouchEnabled(true);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(m_touchPrio);
    this->setKeypadEnabled(true);

    runEntryAnimation();

    return true;
}

void PaimonMultiSettingsPanel::buildTitleBar() {
    m_titleBarBg = nullptr;

    m_titleLabel = CCLabelBMFont::create("Paimon Settings", "goldFont.fnt");
    m_titleLabel->setScale(0.45f);
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->setPosition({14.f, PANEL_H - TITLE_BAR_H / 2.f});
    m_panelContainer->addChild(m_titleLabel, 2);

    m_searchInput = geode::TextInput::create(150.f, "Search...");
    m_searchInput->setScale(0.55f);
    m_searchInput->setAnchorPoint({0.5f, 0.5f});
    m_searchInput->setPosition({PANEL_W / 2.f + 40.f, PANEL_H - TITLE_BAR_H / 2.f});
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
    if (!closeSpr) closeSpr = CCSprite::create();
    closeSpr->setScale(0.7f);
    auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(PaimonMultiSettingsPanel::onClose));
    closeBtn->setPosition({PANEL_W - 16.f, PANEL_H - 14.f});
    closeMenu->addChild(closeBtn);
}

void PaimonMultiSettingsPanel::buildSidebar() {
    m_sidebarBg = nullptr;
    m_sidebarAccent = nullptr;

    m_sidebarMenu = CCMenu::create();
    m_sidebarMenu->setPosition({0.f, 0.f});
    m_sidebarMenu->setTouchPriority(m_childTouchPrio);
    m_panelContainer->addChild(m_sidebarMenu, 2);

    auto const& groups = paimon::settings_ui::getAllGroups();
    float startY = CONTENT_H - 22.f;
    float spacing = 28.f;

    // Keep this order aligned with the settings groups.
    static const CircleBaseColor catColors[] = {
        CircleBaseColor::Gray,
        CircleBaseColor::Blue,
        CircleBaseColor::Cyan,
        CircleBaseColor::Pink,
        CircleBaseColor::Green,
        CircleBaseColor::DarkPurple,
        CircleBaseColor::DarkAqua,
    };
    constexpr int kCatColorCount = 7;

    constexpr float kCellW = SIDEBAR_W - 6.f;
    constexpr float kCellH = 26.f;

    for (size_t i = 0; i < groups.size(); i++) {
        float y = startY - static_cast<float>(i) * spacing;

        auto cell = paimon::SpriteHelper::createRoundedRect(
            kCellW, kCellH, 7.f, {0.f, 0.f, 0.f, 0.42f}
        );
        if (cell) {
            cell->setPosition({SIDEBAR_W / 2.f - kCellW / 2.f, y - kCellH / 2.f});
            m_panelContainer->addChild(cell, 1);
        }

        auto letter = CCLabelBMFont::create(
            groups[i].name.substr(0, 1).c_str(), "bigFont.fnt"
        );
        letter->setScale(0.8f);

        CCNode* topNode = CircleButtonSprite::create(
            letter, catColors[i % kCatColorCount], CircleBaseSize::Medium
        );
        if (!topNode) topNode = letter;

        auto btn = CCMenuItemExt::createSpriteExtra(topNode, [this, idx = static_cast<int>(i)](CCMenuItemSpriteExtra*) {
            selectCategory(idx);
        });
        btn->setPosition({SIDEBAR_W / 2.f, y});
        btn->setScale(kSidebarBtnScale);
        btn->m_scaleMultiplier = 1.f;

        m_sidebarMenu->addChild(btn);
        m_sidebarButtons.push_back(btn);
    }
}

void PaimonMultiSettingsPanel::buildContentArea() {
    float scrollW = CONTENT_W;
    float scrollH = CONTENT_H;

    m_scrollLayer = geode::ScrollLayer::create({scrollW, scrollH});
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
    m_searchQuery.clear();
    m_isSearchActive = false;

    updateSidebarAccent();
    rebuildContent();
}

void PaimonMultiSettingsPanel::rebuildContent() {
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

    // Virtualized children may be hidden, so use their content sizes directly.
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

void PaimonMultiSettingsPanel::relayoutScrollContent() {
    relayoutContent();
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
        if (auto* img = btn->getNormalImage()) {
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(img)) {
                rgba->setCascadeOpacityEnabled(true);
                rgba->setOpacity(sel ? 255 : 110);
            }
        }
    }
}

void PaimonMultiSettingsPanel::onSearchChanged(std::string const& query) {
    m_searchQuery = query;

    std::string lowerQuery = geode::utils::string::toLower(query);

    if (lowerQuery.empty()) {
        m_isSearchActive = false;
        rebuildContent();
        return;
    }

    m_isSearchActive = true;
    buildSearchResults(lowerQuery);
}

void PaimonMultiSettingsPanel::buildSearchResults(std::string const& query) {
    if (!m_scrollLayer) return;

    auto contentLayer = m_scrollLayer->m_contentLayer;
    contentLayer->removeAllChildren();

    std::vector<CCNode*> matchingRows;
    auto const& groups = paimon::settings_ui::getAllGroups();

    for (auto const& group : groups) {
        for (auto const& sub : group.subcategories) {
            auto tempContainer = CCNode::create();
            sub.buildContent(tempContainer, CONTENT_W);

            auto children = tempContainer->getChildren();
            if (!children) continue;

            std::vector<CCNode*> toExtract;
            for (auto* child : CCArrayExt<CCNode*>(children)) {

                bool matches = false;
                auto rowChildren = child->getChildren();
                if (rowChildren) {
                    for (auto* subChild : CCArrayExt<CCNode*>(rowChildren)) {
                        auto label = typeinfo_cast<CCLabelBMFont*>(subChild);
                        if (label) {
                            std::string labelText = geode::utils::string::toLower(label->getString());
                            if (labelText.find(query) != std::string::npos) {
                                matches = true;
                                break;
                            }
                        }
                    }
                }

                if (matches) {
                    toExtract.push_back(child);
                }
            }

            for (auto* row : toExtract) {
                row->retain();
                row->removeFromParent();
                matchingRows.push_back(row);
            }
        }
    }

    float totalH = 0.f;
    for (auto* row : matchingRows) totalH += row->getContentSize().height;

    float contentH = std::max(totalH, CONTENT_H);
    m_scrollLayer->setContentLayerSize({CONTENT_W, contentH});

    float currentY = contentH;
    for (auto* row : matchingRows) {
        float h = row->getContentSize().height;
        currentY -= h;
        row->setPosition({0.f, currentY});
        contentLayer->addChild(row, 0);
        row->release();
    }

    m_scrollLayer->moveToTop();
}

void PaimonMultiSettingsPanel::runEntryAnimation() {
    auto cfg = paimon::popupblur::getConfig();
    int targetDarkness = std::clamp(static_cast<int>(std::round(cfg.darkness * 255.f)), 100, 220);
    m_darkOverlay->runAction(CCFadeTo::create(0.2f, targetDarkness));

    if (m_blurBg) {
        m_blurBg->runAction(CCFadeTo::create(0.2f, 255));
    }

    m_panelContainer->setScale(0.92f);
    auto scaleAction = CCEaseExponentialOut::create(CCScaleTo::create(0.25f, 1.0f));
    m_panelContainer->runAction(scaleAction);
}

void PaimonMultiSettingsPanel::animateClose() {
    if (m_isClosing) return;
    m_isClosing = true;

    paimon::ui::detachGeodeTextInput(m_searchInput);

    this->setTouchEnabled(false);

    if (m_darkOverlay) {
        m_darkOverlay->runAction(CCFadeTo::create(0.15f, 0));
    }

    if (m_blurBg) {
        m_blurBg->runAction(CCFadeTo::create(0.15f, 0));
    }

    if (m_panelContainer) {
        auto scaleAction = CCEaseExponentialIn::create(CCScaleTo::create(0.15f, 0.92f));
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

bool PaimonMultiSettingsPanel::ccTouchBegan(CCTouch* touch, CCEvent* event) {
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

void PaimonMultiSettingsPanel::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_isDragging) return;
    auto touchPos = touch->getLocation();
    m_panelContainer->setPosition(ccpAdd(touchPos, m_dragOffset));
}

void PaimonMultiSettingsPanel::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    m_isDragging = false;
}

void PaimonMultiSettingsPanel::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_isDragging = false;
}
