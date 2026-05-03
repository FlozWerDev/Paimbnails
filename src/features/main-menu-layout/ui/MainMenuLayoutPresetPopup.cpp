#include "MainMenuLayoutPresetPopup.hpp"

#include "../services/MainMenuLayoutPresetManager.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/binding/ButtonSprite.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    constexpr int kSlotsPerPage = 6;
    constexpr float kPopupWidth = 330.f;
    constexpr float kPopupHeight = 240.f;
    constexpr float kSlotWidth = 94.f;
    constexpr float kSlotHeight = 54.f;

    std::string tr(char const* key) {
        return Localization::get().getString(key);
    }

    void setButtonEnabled(CCMenuItemSpriteExtra* button, bool enabled) {
        if (!button) return;
        button->setEnabled(enabled);
        button->setOpacity(enabled ? 255 : 110);
    }
}

MainMenuLayoutPresetPopup* MainMenuLayoutPresetPopup::create(Mode mode, SelectCallback onSelect) {
    auto* ret = new MainMenuLayoutPresetPopup();
    if (ret && ret->init(mode, std::move(onSelect))) {
        ret->autorelease();
        return ret;
    }

    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MainMenuLayoutPresetPopup::init(Mode mode, SelectCallback onSelect) {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;

    m_mode = mode;
    m_onSelect = std::move(onSelect);

    this->setTitle(tr(mode == Mode::Save ? "menu_layout.presets_title_save" : "menu_layout.presets_title_load"));

    auto content = m_mainLayer->getContentSize();

    m_hintLabel = CCLabelBMFont::create(
        tr(mode == Mode::Save ? "menu_layout.presets_hint_save" : "menu_layout.presets_hint_load").c_str(),
        "chatFont.fnt"
    );
    m_hintLabel->setScale(0.58f);
    m_hintLabel->setPosition({ content.width * 0.5f, content.height - 34.f });
    m_hintLabel->setColor({ 185, 210, 230 });
    m_mainLayer->addChild(m_hintLabel, 2);

    auto* menu = CCMenu::create();
    menu->setPosition({ 0.f, 0.f });
    m_mainLayer->addChild(menu, 5);

    float startX = content.width * 0.5f - 102.f;
    float startY = content.height - 82.f;
    float gapX = 102.f;
    float gapY = 62.f;

    for (int index = 0; index < kSlotsPerPage; ++index) {
        int row = index / 3;
        int col = index % 3;

        auto* container = CCNode::create();
        container->setContentSize({ kSlotWidth, kSlotHeight });

        auto* border = CCLayerColor::create({ 70, 82, 96, 255 });
        border->ignoreAnchorPointForPosition(false);
        border->setAnchorPoint({ 0.5f, 0.5f });
        border->setContentSize({ kSlotWidth, kSlotHeight });
        border->setPosition({ kSlotWidth * 0.5f, kSlotHeight * 0.5f });
        container->addChild(border, 0);

        auto* fill = CCLayerColor::create({ 10, 12, 16, 220 });
        fill->ignoreAnchorPointForPosition(false);
        fill->setAnchorPoint({ 0.5f, 0.5f });
        fill->setContentSize({ kSlotWidth - 4.f, kSlotHeight - 4.f });
        fill->setPosition({ kSlotWidth * 0.5f, kSlotHeight * 0.5f });
        container->addChild(fill, 1);

        auto* titleLabel = CCLabelBMFont::create("", "goldFont.fnt");
        titleLabel->setScale(0.38f);
        titleLabel->setPosition({ kSlotWidth * 0.5f, kSlotHeight - 16.f });
        container->addChild(titleLabel, 2);

        auto* infoLabel = CCLabelBMFont::create("", "chatFont.fnt");
        infoLabel->setScale(0.55f);
        infoLabel->setPosition({ kSlotWidth * 0.5f, 15.f });
        container->addChild(infoLabel, 2);

        auto* button = CCMenuItemSpriteExtra::create(container, this, menu_selector(MainMenuLayoutPresetPopup::onSlot));
        button->setTag(index);
        button->setPosition({ startX + gapX * col, startY - gapY * row });
        menu->addChild(button);

        m_slots[index] = {
            button,
            border,
            fill,
            titleLabel,
            infoLabel,
        };
    }

    if (auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        prevSpr->setScale(0.56f);
        m_prevButton = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(MainMenuLayoutPresetPopup::onPrev));
        m_prevButton->setPosition({ 34.f, 20.f });
        menu->addChild(m_prevButton);
    }

    if (auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
        nextSpr->setFlipX(true);
        nextSpr->setScale(0.56f);
        m_nextButton = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(MainMenuLayoutPresetPopup::onNext));
        m_nextButton->setPosition({ content.width - 34.f, 20.f });
        menu->addChild(m_nextButton);
    }

    m_pageLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_pageLabel->setScale(0.42f);
    m_pageLabel->setPosition({ content.width * 0.5f, 21.f });
    m_mainLayer->addChild(m_pageLabel, 5);

    this->refresh();
    paimon::markDynamicPopup(this);
    return true;
}

void MainMenuLayoutPresetPopup::refresh() {
    for (int index = 0; index < kSlotsPerPage; ++index) {
        this->updateSlotCell(index);
    }

    if (m_pageLabel) {
        auto text = fmt::format(
            fmt::runtime(tr("menu_layout.presets_page")),
            m_page + 1,
            this->totalPageCount()
        );
        m_pageLabel->setString(text.c_str());
    }

    setButtonEnabled(m_prevButton, m_page > 0);
    setButtonEnabled(m_nextButton, this->canGoNext());
}

void MainMenuLayoutPresetPopup::updateSlotCell(int index) {
    if (index < 0 || index >= kSlotsPerPage) return;

    auto& cell = m_slots[index];
    auto slotIndex = this->slotIndexForCell(index);
    auto preset = MainMenuLayoutPresetManager::get().getPreset(slotIndex);
    bool occupied = preset.has_value();
    bool enabled = m_mode == Mode::Save || occupied;

    auto title = fmt::format(fmt::runtime(tr("menu_layout.presets_slot")), slotIndex + 1);
    auto info = occupied
        ? fmt::format(fmt::runtime(tr("menu_layout.presets_items")), preset->itemCount)
        : tr("menu_layout.presets_empty");

    if (cell.titleLabel) {
        cell.titleLabel->setString(title.c_str());
        cell.titleLabel->setColor(occupied ? ccColor3B{ 255, 226, 132 } : ccColor3B{ 160, 165, 174 });
    }

    if (cell.infoLabel) {
        cell.infoLabel->setString(info.c_str());
        cell.infoLabel->setColor(occupied ? ccColor3B{ 115, 232, 255 } : ccColor3B{ 140, 145, 155 });
    }

    if (cell.border) {
        cell.border->setColor(occupied ? ccColor3B{ 75, 180, 225 } : ccColor3B{ 70, 82, 96 });
        cell.border->setOpacity(enabled ? 255 : 135);
    }

    if (cell.fill) {
        cell.fill->setColor(occupied ? ccColor3B{ 20, 38, 48 } : ccColor3B{ 12, 14, 18 });
        cell.fill->setOpacity(enabled ? 220 : 150);
    }

    if (cell.button) {
        cell.button->setTag(slotIndex);
        setButtonEnabled(cell.button, enabled);
    }
}

int MainMenuLayoutPresetPopup::usedPageCount() const {
    auto maxUsedSlot = MainMenuLayoutPresetManager::get().maxUsedSlot();
    return maxUsedSlot < 0 ? 1 : maxUsedSlot / kSlotsPerPage + 1;
}

int MainMenuLayoutPresetPopup::totalPageCount() const {
    return std::max(this->usedPageCount(), m_page + 1);
}

bool MainMenuLayoutPresetPopup::isPageFull(int page) const {
    if (page < 0) return false;

    auto start = page * kSlotsPerPage;
    for (int index = 0; index < kSlotsPerPage; ++index) {
        if (!MainMenuLayoutPresetManager::get().hasPreset(start + index)) {
            return false;
        }
    }

    return true;
}

bool MainMenuLayoutPresetPopup::canGoNext() const {
    if (m_page < this->usedPageCount() - 1) {
        return true;
    }

    return m_mode == Mode::Save && this->isPageFull(m_page);
}

int MainMenuLayoutPresetPopup::slotIndexForCell(int index) const {
    return m_page * kSlotsPerPage + index;
}

void MainMenuLayoutPresetPopup::onSlot(CCObject* sender) {
    auto* button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;

    auto slotIndex = button->getTag();
    auto& loc = Localization::get();
    WeakRef<MainMenuLayoutPresetPopup> self = this;

    auto confirmSelection = [self, slotIndex](auto*, bool confirmed) {
        if (!confirmed) return;

        auto popupRef = self.lock();
        auto* popup = static_cast<MainMenuLayoutPresetPopup*>(popupRef.data());
        if (!popup || !popup->getParent()) return;

        if (popup->m_onSelect) {
            popup->m_onSelect(slotIndex);
        }
        popup->Popup::onClose(nullptr);
    };

    if (m_mode == Mode::Save) {
        if (!MainMenuLayoutPresetManager::get().hasPreset(slotIndex)) {
            if (m_onSelect) {
                m_onSelect(slotIndex);
            }
            this->Popup::onClose(nullptr);
            return;
        }

        auto title = loc.getString("menu_layout.preset_overwrite_title");
        auto cancel = loc.getString("general.cancel");
        auto confirm = loc.getString("menu_layout.preset_overwrite_confirm");
        auto body = fmt::format(
            fmt::runtime(loc.getString("menu_layout.preset_overwrite_body")),
            slotIndex + 1
        );
        geode::createQuickPopup(
            title.c_str(),
            body,
            cancel.c_str(),
            confirm.c_str(),
            confirmSelection
        );
        return;
    }

    if (!MainMenuLayoutPresetManager::get().hasPreset(slotIndex)) {
        return;
    }

    auto title = loc.getString("menu_layout.preset_load_title");
    auto cancel = loc.getString("general.cancel");
    auto confirm = loc.getString("menu_layout.preset_load_confirm");
    auto body = fmt::format(
        fmt::runtime(loc.getString("menu_layout.preset_load_body")),
        slotIndex + 1
    );
    geode::createQuickPopup(
        title.c_str(),
        body,
        cancel.c_str(),
        confirm.c_str(),
        confirmSelection
    );
}

void MainMenuLayoutPresetPopup::onPrev(CCObject*) {
    if (m_page <= 0) return;
    --m_page;
    this->refresh();
}

void MainMenuLayoutPresetPopup::onNext(CCObject*) {
    if (!this->canGoNext()) return;
    ++m_page;
    this->refresh();
}

} // namespace paimon::menu_layout
