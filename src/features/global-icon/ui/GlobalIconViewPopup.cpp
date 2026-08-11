#include "GlobalIconViewPopup.hpp"
#include "../services/GlobalIconStorage.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#define MORE_ICONS_EVENTS
#include <hiimjustin000.more_icons/include/MoreIcons.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::globalicon {

namespace {
    constexpr float kPopupWidth  = 360.f;
    constexpr float kCellSize    = 52.f;
    constexpr float kCellStride  = 59.f;
    constexpr int   kColumns     = 5;

    // Canonical gamemode order, so everyone's grid reads the same way.
    constexpr std::string_view kTypeOrder[] = {
        "cube", "ship", "ball", "ufo", "wave", "robot",
        "spider", "swing", "jetpack", "trail", "death", "fire",
    };

    // GD shows these names untranslated in the garage, so they stay as-is.
    std::string_view typeLabel(std::string_view type) {
        if (type == "cube")    return "Cube";
        if (type == "ship")    return "Ship";
        if (type == "ball")    return "Ball";
        if (type == "ufo")     return "UFO";
        if (type == "wave")    return "Wave";
        if (type == "robot")   return "Robot";
        if (type == "spider")  return "Spider";
        if (type == "swing")   return "Swing";
        if (type == "jetpack") return "Jetpack";
        if (type == "trail")   return "Trail";
        if (type == "death")   return "Death";
        if (type == "fire")    return "Fire";
        return "Icon";
    }

    // Types a SimplePlayer can actually draw; the rest get a generic sprite.
    bool isPreviewable(IconType type) {
        switch (type) {
            case IconType::Special:
            case IconType::DeathEffect:
            case IconType::ShipFire:
                return false;
            default:
                return true;
        }
    }

    std::vector<GlobalIconSlot> orderedSlots(GlobalIconMeta const& meta) {
        std::vector<GlobalIconSlot> out;
        out.reserve(meta.icons.size());
        for (auto const& type : kTypeOrder) {
            auto it = meta.icons.find(std::string(type));
            if (it != meta.icons.end()) out.push_back(it->second);
        }
        return out;
    }

    void applyLocalColors(SimplePlayer* sp) {
        if (!sp) return;
        auto* gm = GameManager::sharedState();
        if (!gm) return;
        sp->setColor(gm->colorForIdx(gm->getPlayerColor()));
        sp->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
        sp->m_hasGlowOutline = gm->getPlayerGlow();
        sp->updateColors();
    }
}

GlobalIconViewPopup* GlobalIconViewPopup::create(int accountID, std::string const& username, GlobalIconMeta const& meta) {
    auto ret = new GlobalIconViewPopup();
    if (ret && ret->init(accountID, username, meta)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool GlobalIconViewPopup::init(int accountID, std::string const& username, GlobalIconMeta const& meta) {
    auto slots = orderedSlots(meta);
    if (slots.empty()) return false;

    int rows = static_cast<int>((slots.size() + kColumns - 1) / kColumns);
    float height = 110.f + rows * kCellStride;

    if (!Popup::init(kPopupWidth, height)) return false;

    m_accountID = accountID;
    m_username = username;

    std::string title = Localization::get().getString("globalicon.view_title");
    if (auto pos = title.find("{}"); pos != std::string::npos) {
        title.replace(pos, 2, username.empty() ? "?" : username);
    }
    this->setTitle(title.c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    menu->setID("globalicon-grid"_spr);
    m_mainLayer->addChild(menu);

    buildGrid(menu, slots);

    m_captionLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_captionLabel) {
        m_captionLabel->setPosition({cx, 54.f});
        m_captionLabel->setScale(0.5f);
        m_captionLabel->setOpacity(190);
        m_mainLayer->addChild(m_captionLabel);
    }

    {
        auto spr = ButtonSprite::create(
            Localization::get().getString("globalicon.download").c_str(),
            "bigFont.fnt", "GJ_button_04.png", 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GlobalIconViewPopup::onDownload));
        btn->setPosition({cx - 72.f, 26.f});
        btn->setID("globalicon-download"_spr);
        menu->addChild(btn);
    }
    {
        auto spr = ButtonSprite::create(
            Localization::get().getString("globalicon.download_use").c_str(),
            "bigFont.fnt", "GJ_button_01.png", 0.6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GlobalIconViewPopup::onDownloadUse));
        btn->setPosition({cx + 72.f, 26.f});
        btn->setID("globalicon-download-use"_spr);
        menu->addChild(btn);
    }

    this->setID("global-icon-view-popup"_spr);
    paimon::markDynamicPopup(this);

    selectCell(0);
    loadPreviews();
    return true;
}

void GlobalIconViewPopup::buildGrid(CCMenu* menu, std::vector<GlobalIconSlot> const& slots) {
    auto content = m_mainLayer->getContentSize();
    int count = static_cast<int>(slots.size());
    float gridTop = content.height - 48.f;

    m_cells.reserve(slots.size());

    for (int i = 0; i < count; i++) {
        int row = i / kColumns;
        int col = i % kColumns;
        // The last row is centred on its own item count, not on kColumns.
        int inRow = std::min(kColumns, count - row * kColumns);
        float rowWidth = inRow * kCellStride - (kCellStride - kCellSize);
        float startX = (content.width - rowWidth) / 2.f + kCellSize / 2.f;

        auto container = CCNode::create();
        container->setContentSize({kCellSize, kCellSize});
        container->setAnchorPoint({0.5f, 0.5f});

        auto bg = CCScale9Sprite::create("square02b_001.png");
        if (bg) {
            bg->setContentSize({kCellSize, kCellSize});
            bg->setPosition({kCellSize / 2.f, kCellSize / 2.f});
            bg->setOpacity(70);
            container->addChild(bg, -1);
        }

        auto typeOpt = iconTypeFromString(slots[i].type);
        Cell cell;
        cell.slot = slots[i];
        cell.container = container;

        if (typeOpt && isPreviewable(*typeOpt)) {
            auto preview = SimplePlayer::create(1);
            if (preview) {
                preview->updatePlayerFrame(1, *typeOpt);
                applyLocalColors(preview);
                preview->setPosition({kCellSize / 2.f, kCellSize / 2.f + 5.f});
                preview->setScale(0.72f);
                preview->setOpacity(90); // dimmed until the real icon lands
                container->addChild(preview, 1);
                cell.preview = preview;
            }
        } else {
            auto placeholder = CCSprite::createWithSpriteFrameName("GJ_sTrailIcon_001.png");
            if (placeholder) {
                placeholder->setPosition({kCellSize / 2.f, kCellSize / 2.f + 5.f});
                placeholder->setScale(0.7f);
                container->addChild(placeholder, 1);
                cell.placeholder = placeholder;
            }
        }

        auto label = CCLabelBMFont::create(std::string(typeLabel(slots[i].type)).c_str(), "chatFont.fnt");
        if (label) {
            label->setPosition({kCellSize / 2.f, 9.f});
            label->setScale(0.36f);
            label->setOpacity(170);
            container->addChild(label, 2);
        }

        auto item = CCMenuItemExt::createSpriteExtra(container, [this, i](CCMenuItemSpriteExtra*) {
            selectCell(i);
        });
        if (!item) continue;
        item->setContentSize({kCellSize, kCellSize});
        item->setPosition({startX + col * kCellStride, gridTop - kCellSize / 2.f - row * kCellStride});
        item->setID(fmt::format("globalicon-cell-{}", slots[i].type));
        menu->addChild(item);

        m_cells.push_back(cell);
    }

    auto ring = CCScale9Sprite::create("GJ_square07.png");
    if (ring) {
        ring->setContentSize({kCellSize + 6.f, kCellSize + 6.f});
        ring->setOpacity(0);
        ring->setColor({255, 226, 120});
        // Behind the grid menu: a highlight backdrop, not a veil over the icon.
        m_mainLayer->addChild(ring, -1);
        m_selectionRing = ring;
    }
}

void GlobalIconViewPopup::selectCell(int index) {
    if (index < 0 || index >= static_cast<int>(m_cells.size())) return;
    m_selected = index;

    if (m_selectionRing) {
        auto* container = m_cells[index].container;
        auto* item = container ? container->getParent() : nullptr;
        if (item) {
            // The ring lives on m_mainLayer, the cells on the grid menu; the
            // menu sits at the origin so the item position maps straight over.
            m_selectionRing->setPosition(item->getPosition());
            m_selectionRing->setOpacity(200);
        }
    }
    updateCaption();
}

void GlobalIconViewPopup::updateCaption() {
    if (!m_captionLabel) return;
    if (m_selected < 0 || m_selected >= static_cast<int>(m_cells.size())) {
        m_captionLabel->setString("");
        return;
    }
    auto const& slot = m_cells[m_selected].slot;
    std::string caption = slot.name;
    if (!slot.packName.empty() && slot.packName != slot.name) {
        caption += " - " + slot.packName;
    }
    m_captionLabel->setString(caption.c_str());
    // Long pack names would otherwise run past the popup edge.
    float maxWidth = kPopupWidth - 40.f;
    float width = m_captionLabel->getContentSize().width;
    m_captionLabel->setScale(width > 0.f ? std::min(0.5f, maxWidth / width) : 0.5f);
}

void GlobalIconViewPopup::loadPreviews() {
    if (!GlobalIconStorage::available() || m_cells.empty()) return;

    std::vector<GlobalIconSlot> slots;
    slots.reserve(m_cells.size());
    for (auto const& cell : m_cells) slots.push_back(cell.slot);

    auto spinner = LoadingSpinner::create(24.f);
    if (spinner) {
        spinner->setPosition({m_mainLayer->getContentSize().width / 2.f, 54.f});
        m_mainLayer->addChild(spinner, 6);
        m_spinner = spinner;
    }

    WeakRef<GlobalIconViewPopup> self = this;
    GlobalIconStorage::get().ensureIcons(m_accountID, slots, [self](int succeeded, int total) {
        auto popup = self.lock();
        if (!popup) return;
        if (popup->m_spinner) {
            popup->m_spinner->removeFromParent();
            popup->m_spinner = nullptr;
        }
        popup->refreshPreviews();
        if (succeeded == 0 && total > 0) {
            PaimonNotify::create(
                Localization::get().getString("globalicon.download_failed").c_str(),
                NotificationIcon::Error)->show();
        }
    });
}

void GlobalIconViewPopup::refreshPreviews() {
    for (auto& cell : m_cells) {
        if (!cell.preview) continue;
        auto typeOpt = iconTypeFromString(cell.slot.type);
        if (!typeOpt) continue;
        auto regName = GlobalIconStorage::registeredName(m_accountID, cell.slot);
        auto* info = more_icons::getIcon(regName, *typeOpt);
        if (!info) continue;
        more_icons::updateSimplePlayer(cell.preview, info);
        applyLocalColors(cell.preview);
        cell.preview->setOpacity(255);
    }
}

void GlobalIconViewPopup::withSelectedIcon(geode::CopyableFunction<void(std::string const&, IconType)> action) {
    if (m_busy) return;
    if (m_selected < 0 || m_selected >= static_cast<int>(m_cells.size())) return;
    if (!GlobalIconStorage::available()) {
        PaimonNotify::create(
            Localization::get().getString("globalicon.requires_moreicons_title").c_str(),
            NotificationIcon::Error)->show();
        return;
    }

    auto slot = m_cells[m_selected].slot;
    auto typeOpt = iconTypeFromString(slot.type);
    if (!typeOpt) return;
    IconType type = *typeOpt;

    m_busy = true;
    WeakRef<GlobalIconViewPopup> self = this;
    GlobalIconStorage::get().ensureIcon(m_accountID, slot,
        [self, type, action = std::move(action)](bool ok, std::string const& iconName) {
            if (auto popup = self.lock()) popup->m_busy = false;
            if (!ok || iconName.empty()) {
                PaimonNotify::create(
                    Localization::get().getString("globalicon.download_failed").c_str(),
                    NotificationIcon::Error)->show();
                return;
            }
            if (action) action(iconName, type);
        });
}

void GlobalIconViewPopup::onDownload(CCObject*) {
    withSelectedIcon([](std::string const&, IconType) {
        PaimonNotify::create(
            Localization::get().getString("globalicon.downloaded").c_str(),
            NotificationIcon::Success)->show();
    });
}

void GlobalIconViewPopup::onDownloadUse(CCObject*) {
    withSelectedIcon([](std::string const& iconName, IconType type) {
        auto* info = more_icons::getIcon(iconName, type);
        if (!info) {
            PaimonNotify::create(
                Localization::get().getString("globalicon.download_failed").c_str(),
                NotificationIcon::Error)->show();
            return;
        }
        more_icons::setIcon(info, type);
        PaimonNotify::create(
            Localization::get().getString("globalicon.applied").c_str(),
            NotificationIcon::Success)->show();
    });
}

} // namespace paimon::globalicon
