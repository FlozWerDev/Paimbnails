// adds "Vanilla" / "Paimbnails" tabs to GD's News popup. The vanilla page is
// left alone; the Paimbnails tab hides it and shows the announcements feed in
// the same spot.

#include "../services/PaimbnailsNewsService.hpp"
#include "../ui/NewsServerPickerPopup.hpp"
#include "../ui/PaimbnailsNewsPanel.hpp"
#include "../ui/TopNewsForumsLayer.hpp"

#include "../../../framework/HookConventions.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/modify/NewsPopup.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;
using paimon::loc::tr;

namespace {

constexpr float kTabMinWidth = 86.f;
constexpr float kTabMaxWidth = 170.f;
constexpr float kTabScale = 0.55f;
constexpr float kTabGap = 6.f;
constexpr float kTitleMaxWidth = 240.f;
constexpr float kEntranceStep = 0.04f;
// the "+" is as tall as the tabs. The button art is 30 units tall, so 30 * scale is its side.
constexpr float kPlusScale = kTabScale;
constexpr float kPlusSide = 30.f * kPlusScale;

// scales a button up from nothing, back-eased, once `delay` seconds have passed.
void popInButton(CCNode* button, float delay) {
    if (!button || !paimon::ui::motionEnabled()) return;
    float const target = button->getScale();
    button->setScale(0.f);
    button->runAction(CCSequence::create(
        CCDelayTime::create(paimon::ui::motionDuration(delay)),
        CCEaseBackOut::create(CCScaleTo::create(paimon::ui::motionDuration(0.3f), target)),
        nullptr));
}

} // namespace

class $modify(PaimonNewsPopup, NewsPopup) {
    struct TabEntry {
        std::string id;
        std::string label;
        std::string popupTitle;
        bool isVanilla = false;
        WeakRef<paimon::news::PaimbnailsNewsPanel> panel;
    };

    struct Fields {
        WeakRef<GJCommentListLayer> vanillaList;
        WeakRef<CCLabelBMFont> pageLabel;
        WeakRef<CCLabelBMFont> title;
        WeakRef<paimon::news::NewsServerPickerPopup> serverPicker;
        std::vector<WeakRef<CCMenuItemSpriteExtra>> arrows;
        std::vector<CCMenuItemSpriteExtra*> nativeButtons;
        std::vector<CCMenuItemSpriteExtra*> tabs;
        std::vector<TabEntry> tabEntries;
        std::unordered_map<std::string, WeakRef<paimon::news::PaimbnailsNewsPanel>> panelsByChannel;
        CCMenuItemSpriteExtra* plusButton = nullptr;
        CCMenuItemSpriteExtra* topButton = nullptr;
        CCMenuItemSpriteExtra* refreshButton = nullptr;
        std::string vanillaTitle;
        std::string activeTabId;
        float vanillaTitleScale = 1.f;
        float bgCenterX = 0.f;
        float bgWidth = 420.f;
        float tabBottomY = 0.f;
        bool pageLabelWasVisible = true;
    };

    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "NewsPopup::init");
    }

    $override
    bool init() {
        if (!NewsPopup::init()) return false;
        this->setupPaimbnailsNewsTabs();
        return true;
    }

    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        // arrow keys flip vanilla pages, which are hidden on custom news tabs.
        if (m_fields->activeTabId != paimon::news::kVanillaSourceId && (key == KEY_Left || key == KEY_Right
                || key == CONTROLLER_Left || key == CONTROLLER_Right)) {
            return;
        }
        NewsPopup::keyDown(key, timestamp);
    }

    void setupPaimbnailsNewsTabs() {
        auto* layer = m_mainLayer;
        auto* menu = m_buttonMenu;
        if (!layer || !menu) return;

        // the page list is the only GJCommentListLayer in the popup. Without
        // it the layout is not the one this hook knows, so leave it untouched.
        auto* vanillaList = layer->getChildByType<GJCommentListLayer>(0);
        if (!vanillaList) {
            log::warn("[PaimbnailsNews] NewsPopup layout not recognised; tabs skipped");
            return;
        }
        auto* fields = m_fields.self();
        fields->vanillaList = vanillaList;

        // title ("News", centred) and page counter ("1 of 11", anchored right).
        CCLabelBMFont* pageLabel = nullptr;
        CCLabelBMFont* title = nullptr;
        for (auto* child : CCArrayExt<CCNode*>(layer->getChildren())) {
            auto* label = typeinfo_cast<CCLabelBMFont*>(child);
            if (!label) continue;
            if (label->getAnchorPoint().x > 0.99f) {
                pageLabel = label;
            } else if (std::string_view(label->getString()) == "News") {
                title = label;
                fields->vanillaTitle = label->getString();
                fields->vanillaTitleScale = label->getScale();
            }
        }
        // assign each WeakRef once per popup. Reassigning a raw pointer to a WeakRef that already
        // has a controller only swaps the pointer (Geode's WeakRefController::swap), so the old
        // target is never retained and can be freed while the WeakRef still points at it.
        if (pageLabel) {
            fields->pageLabel = pageLabel;
            fields->pageLabelWasVisible = pageLabel->isVisible();
        }
        if (title) fields->title = title;

        // gD's buttons in this menu are the page arrows and the close button. The arrows
        // follow the tab; the close button only joins the entrance animation.
        for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
            auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(child);
            if (!item) continue;
            if (item->getNormalImage()
                && geode::cocos::isSpriteFrameName(item->getNormalImage(), "GJ_arrow_01_001.png")) {
                fields->arrows.emplace_back(item);
            } else {
                fields->nativeButtons.push_back(item);
            }
        }

        auto const winSize = CCDirector::get()->getWinSize();
        auto const bg = paimon::SpriteHelper::resolveBgLayout(
            layer, {420.f, 280.f}, {winSize.width / 2.f, winSize.height / 2.f});
        fields->bgCenterX = bg.position.x + bg.size.width * (0.5f - bg.anchor.x);
        fields->bgWidth = bg.size.width;
        fields->tabBottomY = bg.position.y - bg.size.height * bg.anchor.y;

        float const bgRightX = fields->bgCenterX + fields->bgWidth * 0.5f;
        float const bgTopY = fields->tabBottomY + bg.size.height;

        // "+" opens the server picker. It sits in the tab row, on the same edge as the tabs.
        auto* plus = paimon::ui::makeButton("+", [this] { this->openNewsServerPicker(); },
            paimon::ui::Btn::Cyan, kPlusSide, kPlusScale, "bigFont.fnt");
        if (plus) {
            plus->setID("news-servers-button"_spr);
            menu->addChild(plus, 5);
            fields->plusButton = plus;
        }

        // "Top" opens the ranked Top News Forums layer.
        auto* topBtn = paimon::ui::makeButton("Top", [this] { this->openTopNewsForums(); },
            paimon::ui::Btn::Blue, 40.f, kPlusScale, "bigFont.fnt");
        if (topBtn) {
            topBtn->setID("news-top-forums-button"_spr);
            menu->addChild(topBtn, 5);
            fields->topButton = topBtn;
        }

        if (auto* refreshIcon = CCSprite::createWithSpriteFrameName("GJ_updateBtn_001.png")) {
            constexpr float kRefreshSize = 20.f;
            float const targetScale = refreshIcon->getContentSize().height > 0.f
                ? (kRefreshSize / refreshIcon->getContentSize().height)
                : 0.45f;
            refreshIcon->setScale(targetScale);
            auto* refresh = CCMenuItemSpriteExtra::create(
                refreshIcon, this, menu_selector(PaimonNewsPopup::onRefreshActiveNewsTab));
            if (refresh) {
                refresh->setID("news-refresh-button"_spr);
                refresh->setPosition(menu->convertToNodeSpace(
                    layer->convertToWorldSpace({bgRightX - 40.f, bgTopY - 21.f})));
                menu->addChild(refresh, 5);
                fields->refreshButton = refresh;
            }
        }

        auto& newsService = paimon::news::NewsService::get();
        auto const pendingChannel = newsService.consumePendingOpenChannel();
        if (!pendingChannel.empty()) {
            newsService.setSourceEnabled(pendingChannel, true);
            fields->activeTabId = pendingChannel;
        } else if (newsService.isVanillaEnabled()) {
            fields->activeTabId = std::string(paimon::news::kVanillaSourceId);
        } else if (auto enabled = newsService.enabledProviders(); !enabled.empty()) {
            fields->activeTabId = enabled.front().channelId;
        } else {
            fields->activeTabId = std::string(paimon::news::kVanillaSourceId);
        }

        this->rebuildNewsTabs(true);

        WeakRef<NewsPopup> weak = this;
        newsService.fetchProviders(false, [weak](bool ok, std::vector<paimon::news::NewsProvider> const&) {
            if (!ok) return;
            if (auto popup = weak.lock()) {
                static_cast<PaimonNewsPopup*>(popup.data())->rebuildNewsTabs(false);
            }
        });
    }

    paimon::news::PaimbnailsNewsPanel* ensurePanelForChannel(std::string const& channelId) {
        auto* fields = m_fields.self();
        if (auto it = fields->panelsByChannel.find(channelId); it != fields->panelsByChannel.end()) {
            if (auto existing = it->second.lock()) return existing.data();
        }
        auto vanillaList = fields->vanillaList.lock();
        if (!vanillaList || !m_mainLayer) return nullptr;

        auto* panel = paimon::news::PaimbnailsNewsPanel::create(vanillaList->getContentSize(), channelId);
        if (!panel) return nullptr;
        panel->setID(fmt::format("{}-{}", "paimbnails-news-panel"_spr, channelId));
        panel->setPosition(vanillaList->getPosition());
        panel->setVisible(false);
        m_mainLayer->addChild(panel, vanillaList->getZOrder());
        fields->panelsByChannel.insert_or_assign(channelId, WeakRef<paimon::news::PaimbnailsNewsPanel>(panel));
        return panel;
    }

    void rebuildNewsTabs(bool animateEntrance) {
        auto* layer = m_mainLayer;
        auto* menu = m_buttonMenu;
        if (!layer || !menu) return;
        auto* fields = m_fields.self();

        for (auto* button : fields->tabs) {
            if (button) button->removeFromParent();
        }
        fields->tabs.clear();
        fields->tabEntries.clear();

        auto& newsService = paimon::news::NewsService::get();
        if (auto const pending = newsService.consumePendingOpenChannel(); !pending.empty()) {
            newsService.setSourceEnabled(pending, true);
            fields->activeTabId = pending;
        }
        if (newsService.isVanillaEnabled()) {
            fields->tabEntries.push_back(TabEntry{
                .id = std::string(paimon::news::kVanillaSourceId),
                .label = tr("pai.news.tab.vanilla", "Vanilla"),
                .popupTitle = fields->vanillaTitle.empty() ? "News" : fields->vanillaTitle,
                .isVanilla = true,
                .panel = {},
            });
        }

        for (auto const& p : newsService.enabledProviders()) {
            auto* panel = this->ensurePanelForChannel(p.channelId);
            if (!panel) continue;
            std::string popupTitle = p.isDefault
                ? tr("pai.news.popup.title", "Paimbnails News")
                : fmt::format("{} News", p.name);
            fields->tabEntries.push_back(TabEntry{
                .id = p.channelId,
                .label = p.name,
                .popupTitle = std::move(popupTitle),
                .isVanilla = false,
                .panel = panel,
            });
        }

        // safety fallback if every source was somehow missing.
        if (fields->tabEntries.empty()) {
            fields->tabEntries.push_back(TabEntry{
                .id = std::string(paimon::news::kVanillaSourceId),
                .label = tr("pai.news.tab.vanilla", "Vanilla"),
                .popupTitle = fields->vanillaTitle.empty() ? "News" : fields->vanillaTitle,
                .isVanilla = true,
                .panel = {},
            });
        }

        // hide panels whose tab was removed.
        for (auto const& [cid, weakPanel] : fields->panelsByChannel) {
            bool const enabled = std::any_of(fields->tabEntries.begin(), fields->tabEntries.end(),
                [&](TabEntry const& entry) { return !entry.isVanilla && entry.id == cid; });
            if (!enabled) {
                if (auto p = weakPanel.lock()) p->setActive(false);
            }
        }

        bool const activeExists = std::any_of(fields->tabEntries.begin(), fields->tabEntries.end(),
            [&](TabEntry const& entry) { return entry.id == fields->activeTabId; });
        if (!activeExists) {
            fields->activeTabId = fields->tabEntries.front().id;
        }

        for (auto const& entry : fields->tabEntries) {
            std::string const tabId = entry.id;
            auto press = [this, tabId] { this->switchPaimbnailsNewsTab(tabId); };
            auto* button = paimon::ui::makeButton(
                entry.label.c_str(), press, paimon::ui::Btn::Gray, 0.f, kTabScale);
            if (!button) continue;
            float const naturalW = button->getContentSize().width * std::abs(button->getScaleX());
            float const clampedW = std::clamp(naturalW, kTabMinWidth, kTabMaxWidth);
            if (std::abs(clampedW - naturalW) > 1.f) {
                button = paimon::ui::makeButton(
                    entry.label.c_str(), press, paimon::ui::Btn::Gray, clampedW, kTabScale);
                if (!button) continue;
            }
            button->setID(fmt::format("{}-{}", "news-tab"_spr, tabId));
            menu->addChild(button, 5);
            fields->tabs.push_back(button);
        }

        std::vector<CCMenuItem*> rowItems;
        rowItems.reserve(fields->tabs.size() + 2);
        for (auto* button : fields->tabs) rowItems.push_back(button);
        if (fields->plusButton) {
            fields->plusButton->setScale(kPlusScale);
            fields->plusButton->m_baseScale = kPlusScale;
            rowItems.push_back(fields->plusButton);
        }
        if (fields->topButton) {
            fields->topButton->setScale(kPlusScale);
            fields->topButton->m_baseScale = kPlusScale;
            rowItems.push_back(fields->topButton);
        }

        float const maxHalf = std::max(80.f, fields->bgWidth * 0.5f - 14.f);
        auto const leftEdge = menu->convertToNodeSpace(
            layer->convertToWorldSpace({fields->bgCenterX - maxHalf, fields->tabBottomY}));
        auto const rightEdge = menu->convertToNodeSpace(
            layer->convertToWorldSpace({fields->bgCenterX + maxHalf, fields->tabBottomY}));
        paimon::ui::layoutButtonRow(
            rowItems, leftEdge.x, rightEdge.x, leftEdge.y, kTabGap, paimon::ui::RowAlign::Center);

        this->applyActiveNewsTab();

        if (animateEntrance) {
            for (auto* button : fields->nativeButtons) popInButton(button, 0.f);
            for (size_t i = 0; i < fields->tabs.size(); ++i) {
                popInButton(fields->tabs[i], kEntranceStep * static_cast<float>(i + 1));
            }
            if (fields->plusButton) {
                popInButton(fields->plusButton, kEntranceStep * static_cast<float>(fields->tabs.size() + 1));
            }
            if (fields->topButton) {
                popInButton(fields->topButton, kEntranceStep * static_cast<float>(fields->tabs.size() + 2));
            }
            if (fields->refreshButton && fields->refreshButton->isVisible()) {
                popInButton(fields->refreshButton, kEntranceStep * static_cast<float>(fields->tabs.size() + 3));
            }
        }
    }

    void onRefreshActiveNewsTab(CCObject*) {
        auto* fields = m_fields.self();
        for (auto const& entry : fields->tabEntries) {
            if (!entry.isVanilla && entry.id == fields->activeTabId) {
                if (auto panel = entry.panel.lock()) panel->refresh();
                break;
            }
        }
    }

    void openNewsServerPicker() {
        auto* fields = m_fields.self();
        if (auto existing = fields->serverPicker.lock(); existing && existing->getParent()) return;
        WeakRef<NewsPopup> weak = this;
        auto* popup = paimon::news::NewsServerPickerPopup::create([weak] {
            if (auto owner = weak.lock()) {
                static_cast<PaimonNewsPopup*>(owner.data())->rebuildNewsTabs(false);
            }
        });
        if (!popup) return;
        // store a new WeakRef, not a raw pointer through operator=. The second open reaches
        // this line with a controller left from the first popup; reusing it would skip
        // retaining this popup, which then dangles once it is closed.
        fields->serverPicker = WeakRef<paimon::news::NewsServerPickerPopup>(popup);
        paimon::configkit::showAbove(popup, this);
    }

    void openTopNewsForums() {
        WeakRef<NewsPopup> weak = this;
        paimon::news::TopNewsForumsLayer::open([weak](std::string const& channelId) {
            if (auto owner = weak.lock()) {
                auto* self = static_cast<PaimonNewsPopup*>(owner.data());
                self->m_fields->activeTabId = channelId;
                self->rebuildNewsTabs(false);
            }
        });
    }

    void refreshPaimbnailsNewsTabSkins() {
        auto* fields = m_fields.self();
        size_t const count = std::min(fields->tabs.size(), fields->tabEntries.size());
        for (size_t i = 0; i < count; ++i) {
            bool const selected = fields->tabEntries[i].id == fields->activeTabId;
            paimon::ui::setButtonSkin(fields->tabs[i], selected ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
        }
    }

    void applyActiveNewsTab() {
        auto* fields = m_fields.self();
        bool const isVanilla = fields->activeTabId == paimon::news::kVanillaSourceId;

        if (auto list = fields->vanillaList.lock()) list->setVisible(isVanilla);

        if (auto label = fields->pageLabel.lock()) {
            label->setVisible(isVanilla && fields->pageLabelWasVisible);
        }

        // vanilla toggles the arrows' visibility on every page rebuild, so
        // disable them and hide their sprite instead of touching visibility.
        for (auto const& weak : fields->arrows) {
            auto arrow = weak.lock();
            if (!arrow) continue;
            arrow->setEnabled(isVanilla);
            if (auto* image = arrow->getNormalImage()) image->setVisible(isVanilla);
        }

        std::string activeTitle = fields->vanillaTitle;
        for (auto const& entry : fields->tabEntries) {
            if (entry.id == fields->activeTabId) activeTitle = entry.popupTitle;
            if (!entry.isVanilla) {
                if (auto panel = entry.panel.lock()) {
                    panel->setActive(entry.id == fields->activeTabId);
                }
            }
        }

        if (auto title = fields->title.lock()) {
            if (isVanilla) {
                title->setString(fields->vanillaTitle.c_str());
                title->setScale(fields->vanillaTitleScale);
            } else {
                title->setString(activeTitle.c_str());
                title->limitLabelWidth(kTitleMaxWidth, fields->vanillaTitleScale, 0.3f);
            }
        }

        if (fields->refreshButton) {
            fields->refreshButton->setVisible(!isVanilla);
            fields->refreshButton->setEnabled(!isVanilla);
        }

        this->refreshPaimbnailsNewsTabSkins();
    }

    void switchPaimbnailsNewsTab(std::string const& tabId) {
        auto* fields = m_fields.self();
        if (fields->activeTabId == tabId) return;
        fields->activeTabId = tabId;
        this->applyActiveNewsTab();
    }
};
