#include "TemplatePickerPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 260.f;
constexpr int kCols = 8;
constexpr int kRows = 4;
constexpr int kPerPage = kCols * kRows;

}  // anonymous namespace

TemplatePickerPopup* TemplatePickerPopup::create(IconType type, PickedCallback onPicked) {
    auto* p = new TemplatePickerPopup();
    if (p->init(type, std::move(onPicked))) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

int TemplatePickerPopup::iconCount() const {
    auto* gm = GameManager::get();
    if (!gm) return 1;
    int count = gm->countForType(m_type);
    return std::max(count, 1);
}

bool TemplatePickerPopup::init(IconType type, PickedCallback onPicked) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    m_type = type;
    m_onPicked = std::move(onPicked);
    setTitle("Elige una plantilla");
    setID("icon-maker-template-popup"_spr);

    auto size = m_mainLayer->getContentSize();

    m_gridArea = CCNode::create();
    m_gridArea->setPosition({size.width / 2.f, size.height / 2.f + 4.f});
    m_mainLayer->addChild(m_gridArea);

    auto* navMenu = CCMenu::create();
    navMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(navMenu);

    if (auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        prevSpr->setScale(0.7f);
        if (auto* prevBtn = CCMenuItemExt::createSpriteExtra(prevSpr,
                [this](CCMenuItemSpriteExtra*) {
                    int pages = (iconCount() + kPerPage - 1) / kPerPage;
                    m_page = (m_page - 1 + pages) % pages;
                    rebuildPage();
                })) {
            prevBtn->setPosition({22.f, size.height / 2.f});
            navMenu->addChild(prevBtn);
        }
    }
    if (auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        nextSpr->setScale(0.7f);
        nextSpr->setFlipX(true);
        if (auto* nextBtn = CCMenuItemExt::createSpriteExtra(nextSpr,
                [this](CCMenuItemSpriteExtra*) {
                    int pages = (iconCount() + kPerPage - 1) / kPerPage;
                    m_page = (m_page + 1) % pages;
                    rebuildPage();
                })) {
            nextBtn->setPosition({size.width - 22.f, size.height / 2.f});
            navMenu->addChild(nextBtn);
        }
    }

    m_pageLabel = CCLabelBMFont::create("", "goldFont.fnt");
    if (m_pageLabel) {
        m_pageLabel->setScale(0.5f);
        m_pageLabel->setPosition({size.width / 2.f, 18.f});
        m_mainLayer->addChild(m_pageLabel);
    }

    rebuildPage();
    return true;
}

void TemplatePickerPopup::rebuildPage() {
    if (!m_gridArea) return;
    m_gridArea->removeAllChildren();

    int total = iconCount();
    int pages = (total + kPerPage - 1) / kPerPage;
    m_page = std::clamp(m_page, 0, pages - 1);
    int first = m_page * kPerPage + 1;

    if (m_pageLabel) {
        m_pageLabel->setString(
            fmt::format("Pagina {}/{}", m_page + 1, pages).c_str());
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_gridArea->addChild(menu);

    constexpr float spacingX = 42.f;
    constexpr float spacingY = 42.f;
    const float originX = -spacingX * (kCols - 1) / 2.f;
    const float originY = spacingY * (kRows - 1) / 2.f;

    for (int i = 0; i < kPerPage; ++i) {
        int iconId = first + i;
        if (iconId > total) break;

        auto* wrap = CCNode::create();
        wrap->setContentSize({36.f, 36.f});
        wrap->setAnchorPoint({0.5f, 0.5f});

        auto* player = SimplePlayer::create(1);
        if (player) {
            player->updatePlayerFrame(iconId, m_type);
            player->setPosition({18.f, 18.f});
            player->setScale(0.8f);
            wrap->addChild(player);
        }

        auto* btn = CCMenuItemExt::createSpriteExtra(wrap,
            [this, iconId](CCMenuItemSpriteExtra*) {
                auto callback = m_onPicked;
                onClose(nullptr);
                if (callback) callback(iconId);
            });
        if (!btn) continue;
        btn->setPosition({
            originX + spacingX * static_cast<float>(i % kCols),
            originY - spacingY * static_cast<float>(i / kCols),
        });
        menu->addChild(btn);
    }
}

}  // namespace paimon::icon_maker
