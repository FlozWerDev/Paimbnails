#include "CopiedIconsPopup.hpp"

#include "IconSetRow.hpp"
#include "../IconCopyStore.hpp"
#include "../hooks/IconCopyGarageGlue.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "CopyIconsPopup.hpp"
#include "MyIconSetsPopup.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <vector>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

constexpr float kWidth = 420.f;
constexpr float kHeight = 280.f;
constexpr float kBodyWidth = 390.f;
constexpr float kBodyHeight = 190.f;
constexpr float kFolderButton = 30.f;

void fitTo(CCNode* node, float target) {
    if (!node) return;
    float const dim = std::max(node->getContentWidth(), node->getContentHeight());
    if (dim > 0.f) node->setScale(target / dim);
}

// Read where the folder sits before the touch handler tears anything down: the
// mod's entrance animation grows the new card out of it.
void openMySets(CCNode* button) {
    CCPoint origin{-1.f, -1.f};
    if (button && button->getParent()) {
        origin = button->getParent()->convertToWorldSpace(button->getPosition());
    }
    Loader::get()->queueInMainThread([origin] {
        if (paimon::isRuntimeShuttingDown()) return;
        auto* popup = MyIconSetsPopup::create();
        if (!popup) return;
        if (origin.x >= 0.f) paimon::storeButtonOrigin(origin);
        popup->show();
    });
}

}  // anonymous namespace

CopiedIconsPopup* CopiedIconsPopup::create() {
    auto* popup = new CopiedIconsPopup();
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool CopiedIconsPopup::init() {
    if (!Popup::init(kWidth, kHeight)) return false;

    this->setTitle("Copied Icons");
    this->setID("copied-icons-popup"_spr);
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();

    m_body = CCNode::create();
    m_body->setContentSize({kBodyWidth, kBodyHeight});
    m_body->setPosition({(content.width - kBodyWidth) / 2.f, 52.f});
    m_mainLayer->addChild(m_body);

    // The sets you built yourself live in their own list; the folder is the way
    // in, since this one only ever holds other people's icons.
    auto* folderSpr = paimon::SpriteHelper::safeCreateWithFrameName("gj_folderBtn_001.png");
    if (!folderSpr) folderSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
    if (folderSpr) {
        fitTo(folderSpr, kFolderButton);
        auto* btn = CCMenuItemExt::createSpriteExtra(folderSpr, [](CCMenuItemSpriteExtra* sender) {
            openMySets(sender);
        });
        btn->setID("my-icon-sets-button"_spr);
        btn->setPosition({content.width - 24.f, content.height - 24.f});
        m_buttonMenu->addChild(btn);
    }

    auto* clearSpr = ButtonSprite::create("Clear All", "bigFont.fnt", "GJ_button_06.png", 0.5f);
    if (clearSpr) {
        m_clearBtn = CCMenuItemSpriteExtra::create(
            clearSpr, this, menu_selector(CopiedIconsPopup::onClearAll));
        m_clearBtn->setID("clear-all-button"_spr);
        m_clearBtn->setPosition({content.width / 2.f, 26.f});
        m_buttonMenu->addChild(m_clearBtn);
    }

    rebuild();
    return true;
}

void CopiedIconsPopup::rebuild() {
    if (!m_body) return;
    m_body->removeAllChildren();

    auto const& sets = entries();
    if (m_clearBtn) m_clearBtn->setVisible(!sets.empty());

    if (sets.empty()) {
        auto* empty = CCLabelBMFont::create("Nothing copied yet", "bigFont.fnt");
        empty->setScale(0.55f);
        empty->setColor({200, 200, 210});
        empty->setPosition({kBodyWidth / 2.f, kBodyHeight / 2.f + 10.f});
        m_body->addChild(empty);

        auto* hint = CCLabelBMFont::create(
            "Open a profile and press the copy icons button", "chatFont.fnt");
        hint->setScale(0.45f);
        hint->setColor({150, 150, 170});
        hint->setPosition({kBodyWidth / 2.f, kBodyHeight / 2.f - 12.f});
        m_body->addChild(hint);
        return;
    }

    float const totalHeight = std::max(static_cast<float>(sets.size()) * kRowHeight, kBodyHeight);

    auto* container = CCLayer::create();
    container->setContentSize({kBodyWidth, totalHeight});

    for (size_t index = 0; index < sets.size(); ++index) {
        auto* row = makeRow(sets[index], index, kBodyWidth);
        if (!row) continue;
        row->setPosition({0.f, totalHeight - kRowHeight * (index + 1)});
        container->addChild(row);
    }

    auto* scroll = ScrollLayer::create({kBodyWidth, kBodyHeight});
    scroll->m_contentLayer->setContentSize({kBodyWidth, totalHeight});
    scroll->m_contentLayer->addChild(container);
    scroll->scrollToTop();
    m_body->addChild(scroll);
}

CCNode* CopiedIconsPopup::makeRow(IconSet const& set, size_t index, float width) {
    WeakRef<CopiedIconsPopup> self = this;

    std::vector<RowAction> actions{
        {makeRowTextFace("Use", "GJ_button_01.png"),
         [self, set] { if (auto popup = self.lock()) popup->use(set); }},
        {makeRowTextFace("Icons", "GJ_button_04.png"),
         [self, set] { if (auto popup = self.lock()) popup->showIcons(set); }},
    };

    if (set.accountID > 0) {
        int const accountID = set.accountID;
        actions.push_back({makeRowIconFace("GJ_profileButton_001.png"), [accountID] {
            Loader::get()->queueInMainThread([accountID] {
                if (paimon::isRuntimeShuttingDown()) return;
                if (auto* page = ProfilePage::create(accountID, false)) page->show();
            });
        }});
    }

    actions.push_back({makeRowIconFace("GJ_trashBtn_001.png"),
        [self, set] { if (auto popup = self.lock()) popup->erase(set); }});

    return makeSetRow(set, formatSetDate(set.copiedAt), index, width, actions);
}

void CopiedIconsPopup::use(IconSet const& set) {
    iconcopy::apply(set);
    garage::refreshVisibleGarage();
    PaimonNotify::show(fmt::format("Now using {}'s icons", set.label()),
                       NotificationIcon::Success);
    this->onClose(nullptr);
}

void CopiedIconsPopup::showIcons(IconSet const& set) {
    WeakRef<CopiedIconsPopup> self = this;
    Loader::get()->queueInMainThread([self, set] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (auto* popup = CopyIconsPopup::create(set, true)) popup->show();
    });
}

void CopiedIconsPopup::erase(IconSet const& set) {
    iconcopy::removeUser(set);
    queueRebuild();
}

void CopiedIconsPopup::onClearAll(CCObject*) {
    if (entries().empty()) return;

    WeakRef<CopiedIconsPopup> self = this;
    geode::createQuickPopup(
        "Clear All",
        "Remove <cr>every</c> copied icon set?",
        "Cancel", "Clear",
        [self](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            iconcopy::clear();
            if (auto popup = self.lock()) popup->queueRebuild();
        });
}

// Rebuilding tears down the buttons we are being called from, so wait until the
// touch dispatcher is done with them.
void CopiedIconsPopup::queueRebuild() {
    WeakRef<CopiedIconsPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (auto popup = self.lock()) popup->rebuild();
    });
}

}  // namespace paimon::iconcopy
