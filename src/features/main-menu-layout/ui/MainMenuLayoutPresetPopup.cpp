#include "MainMenuLayoutPresetPopup.hpp"

#include "../services/MainMenuLayoutPresetManager.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {
namespace {
    constexpr int kSlotsPerPage = 6;
    constexpr float kPopupWidth = 404.f;
    constexpr float kPopupHeight = 274.f;
    constexpr float kCardWidth = 114.f;
    constexpr float kCardHeight = 76.f;
    constexpr float kActionStripH = 20.f;

    std::string tr(char const* key) {
        return Localization::get().getString(key);
    }

    bool isSpanish() {
        return Localization::get().getLanguage() == Localization::Language::SPANISH;
    }

    std::string shortSceneLabel(std::string const& scene) {
        if (scene == "MenuLayer") return "Menu";
        if (scene == "PauseLayer") return isSpanish() ? "Pausa" : "Pause";
        if (scene == "LevelInfoLayer") return isSpanish() ? "Nivel" : "Level";
        return scene;
    }

    void setButtonEnabled(CCMenuItemSpriteExtra* button, bool enabled) {
        if (!button) return;
        button->setEnabled(enabled);
        button->setOpacity(enabled ? 255 : 90);
    }

    class RenameSlotPopup : public PaimonPopup {
    public:
        static RenameSlotPopup* create(std::string const& currentName, CopyableFunction<void(std::string const&)> onSave) {
            auto* ret = new RenameSlotPopup();
            if (ret && ret->init(currentName, std::move(onSave))) {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

    protected:
        bool init(std::string const& currentName, CopyableFunction<void(std::string const&)> onSave) {
            if (!PaimonPopup::init(260.f, 138.f)) return false;
            m_onSave = std::move(onSave);
            this->setTitle(isSpanish() ? "Renombrar Slot" : "Rename Slot", "goldFont.fnt", 0.56f, 18.f);

            auto const sz = m_mainLayer->getContentSize();
            m_input = TextInput::create(210.f, isSpanish() ? "Nombre del preset..." : "Preset name...", "chatFont.fnt");
            m_input->setMaxCharCount(24);
            m_input->setString(currentName);
            m_input->setPosition({ sz.width * 0.5f, sz.height * 0.5f + 4.f });
            m_mainLayer->addChild(m_input, 4);

            auto* menu = paimon::ui::makeZeroMenu("rename-slot-menu"_spr);
            m_mainLayer->addChild(menu, 5);

            auto* cancelBtn = paimon::ui::makeButton(
                tr("general.cancel").c_str(),
                [this] { this->onClose(nullptr); },
                paimon::ui::Btn::Gray, 84.f, 0.55f, "goldFont.fnt");
            cancelBtn->setPosition({ sz.width * 0.5f - 52.f, 26.f });
            menu->addChild(cancelBtn);

            auto* okBtn = paimon::ui::makeButton(
                tr("menu_layout.save").c_str(),
                [this] {
                    if (m_onSave && m_input) m_onSave(m_input->getString());
                    this->onClose(nullptr);
                },
                paimon::ui::Btn::Green, 84.f, 0.55f, "goldFont.fnt");
            okBtn->setPosition({ sz.width * 0.5f + 52.f, 26.f });
            menu->addChild(okBtn);

            paimon::markDynamicPopup(this);
            return true;
        }

        TextInput* m_input = nullptr;
        CopyableFunction<void(std::string const&)> m_onSave;
    };
}

MainMenuLayoutPresetPopup* MainMenuLayoutPresetPopup::create(
    Mode mode, SelectCallback onSelect, std::string const& sceneTag) {
    auto* ret = new MainMenuLayoutPresetPopup();
    if (ret && ret->init(mode, std::move(onSelect), sceneTag)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MainMenuLayoutPresetPopup::init(Mode mode, SelectCallback onSelect, std::string const& sceneTag) {
    if (!PaimonPopup::init(kPopupWidth, kPopupHeight)) return false;

    m_mode = mode;
    m_sceneTag = sceneTag;
    m_onSelect = std::move(onSelect);

    this->setTitle(tr(mode == Mode::Save ? "menu_layout.presets_title_save" : "menu_layout.presets_title_load"));
    this->addInfoButton(
        tr(mode == Mode::Save ? "menu_layout.presets_title_save" : "menu_layout.presets_title_load"),
        tr(mode == Mode::Save ? "menu_layout.presets_hint_save" : "menu_layout.presets_hint_load"));

    auto content = m_mainLayer->getContentSize();

    m_hintLabel = CCLabelBMFont::create(
        tr(mode == Mode::Save ? "menu_layout.presets_hint_save" : "menu_layout.presets_hint_load").c_str(),
        "chatFont.fnt"
    );
    m_hintLabel->setScale(0.55f);
    m_hintLabel->setPosition({ content.width * 0.5f, content.height - 34.f });
    m_hintLabel->setColor({ 185, 210, 235 });
    m_mainLayer->addChild(m_hintLabel, 2);

    auto* menu = CCMenu::create();
    menu->setPosition({ 0.f, 0.f });
    m_mainLayer->addChild(menu, 5);

    float const startX = content.width * 0.5f - 122.f;
    float const startY = content.height - 92.f;
    float const gapX = 122.f;
    float const gapY = 84.f;

    for (int index = 0; index < kSlotsPerPage; ++index) {
        int const row = index / 3;
        int const col = index % 3;
        CCPoint const center{ startX + gapX * col, startY - gapY * row };

        auto* card = CCNode::create();
        card->setContentSize({ kCardWidth, kCardHeight });
        card->setAnchorPoint({ 0.5f, 0.5f });
        card->setPosition(center);
        m_mainLayer->addChild(card, 3);

        auto* border = paimon::ui::makeInset({ kCardWidth, kCardHeight }, 255, { 68, 82, 102 });
        card->addChild(border, 0);

        auto* fill = paimon::ui::makeInset({ kCardWidth - 4.f, kCardHeight - 4.f }, 225, { 12, 16, 24 });
        fill->setPosition({ 2.f, 2.f });
        card->addChild(fill, 1);

        if (auto* sep = paimon::ui::makeDivider(kCardWidth - 12.f, { 110, 145, 190 }, 75)) {
            sep->setPosition({ kCardWidth * 0.5f, kActionStripH + 2.f });
            card->addChild(sep, 2);
        }

        // Hit area covers the main body of the card (above the action strip)
        float const hitH = kCardHeight - kActionStripH - 2.f;
        auto* hitFace = CCNode::create();
        hitFace->setContentSize({ kCardWidth - 4.f, hitH });

        auto* indexLabel = CCLabelBMFont::create("", "bigFont.fnt");
        indexLabel->setScale(0.28f);
        indexLabel->setAnchorPoint({ 0.f, 0.5f });
        indexLabel->setPosition({ 6.f, hitH - 10.f });
        hitFace->addChild(indexLabel, 2);

        auto* sceneLabel = CCLabelBMFont::create("", "chatFont.fnt");
        sceneLabel->setScale(0.45f);
        sceneLabel->setAnchorPoint({ 1.f, 0.5f });
        sceneLabel->setPosition({ kCardWidth - 10.f, hitH - 10.f });
        hitFace->addChild(sceneLabel, 2);

        auto* titleLabel = CCLabelBMFont::create("", "goldFont.fnt");
        titleLabel->setScale(0.40f);
        titleLabel->setPosition({ (kCardWidth - 4.f) * 0.5f, hitH * 0.5f + 2.f });
        hitFace->addChild(titleLabel, 2);

        auto* infoLabel = CCLabelBMFont::create("", "chatFont.fnt");
        infoLabel->setScale(0.48f);
        infoLabel->setPosition({ (kCardWidth - 4.f) * 0.5f, 10.f });
        hitFace->addChild(infoLabel, 2);

        auto* hitBtn = CCMenuItemSpriteExtra::create(hitFace, this, menu_selector(MainMenuLayoutPresetPopup::onSlot));
        hitBtn->setTag(index);
        hitBtn->setPosition({ center.x, center.y + kActionStripH * 0.5f + 1.f });
        menu->addChild(hitBtn);

        // Action buttons along the bottom strip (Rename / Delete) using native GD buttons
        auto makeMiniGdBtn = [&](char const* text, paimon::ui::Btn skin, SEL_MenuHandler sel) {
            auto* spr = paimon::ui::makeButtonSprite(text, skin, 46.f, 0.42f, "bigFont.fnt");
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, sel);
            btn->setTag(index);
            return btn;
        };

        auto* renameBtn = makeMiniGdBtn(
            isSpanish() ? "Nombre" : "Rename",
            paimon::ui::Btn::Blue,
            menu_selector(MainMenuLayoutPresetPopup::onRenameSlot));
        renameBtn->setPosition({ center.x - 26.f, center.y - kCardHeight * 0.5f + 11.f });
        menu->addChild(renameBtn);

        auto* deleteBtn = makeMiniGdBtn(
            isSpanish() ? "Borrar" : "Delete",
            paimon::ui::Btn::Red,
            menu_selector(MainMenuLayoutPresetPopup::onDeleteSlot));
        deleteBtn->setPosition({ center.x + 26.f, center.y - kCardHeight * 0.5f + 11.f });
        menu->addChild(deleteBtn);

        m_slots[index] = {
            card,
            hitBtn,
            renameBtn,
            deleteBtn,
            border,
            fill,
            indexLabel,
            titleLabel,
            infoLabel,
            sceneLabel,
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

    auto defaultTitle = fmt::format(fmt::runtime(tr("menu_layout.presets_slot")), slotIndex + 1);
    auto title = (occupied && !preset->name.empty()) ? preset->name : defaultTitle;

    std::string info;
    if (!occupied) {
        info = tr("menu_layout.presets_empty");
    } else if (preset->shapeCount > 0) {
        info = fmt::format("{} btn / {} draw", preset->buttonCount, preset->shapeCount);
    } else {
        info = fmt::format(fmt::runtime(tr("menu_layout.presets_items")), preset->itemCount);
    }

    if (cell.indexLabel) {
        cell.indexLabel->setString(fmt::format("#{}", slotIndex + 1).c_str());
        cell.indexLabel->setColor(occupied ? ccColor3B{ 120, 225, 255 } : ccColor3B{ 120, 128, 140 });
    }

    if (cell.sceneLabel) {
        std::string sc = (occupied && !preset->sceneTag.empty()) ? shortSceneLabel(preset->sceneTag) : "";
        cell.sceneLabel->setString(sc.c_str());
        cell.sceneLabel->setColor({ 160, 220, 185 });
    }

    if (cell.titleLabel) {
        cell.titleLabel->setString(title.c_str());
        cell.titleLabel->limitLabelWidth(kCardWidth - 14.f, 0.40f, 0.14f);
        cell.titleLabel->setColor(occupied ? ccColor3B{ 255, 224, 130 } : ccColor3B{ 140, 148, 160 });
    }

    if (cell.infoLabel) {
        cell.infoLabel->setString(info.c_str());
        cell.infoLabel->limitLabelWidth(kCardWidth - 14.f, 0.48f, 0.2f);
        cell.infoLabel->setColor(occupied ? ccColor3B{ 120, 225, 255 } : ccColor3B{ 125, 132, 144 });
    }

    if (cell.border) {
        cell.border->setColor(occupied ? ccColor3B{ 78, 175, 225 } : ccColor3B{ 58, 68, 82 });
        cell.border->setOpacity(enabled ? 255 : 130);
    }

    if (cell.fill) {
        cell.fill->setColor(occupied ? ccColor3B{ 16, 28, 40 } : ccColor3B{ 12, 15, 21 });
        cell.fill->setOpacity(enabled ? 235 : 155);
    }

    if (cell.hitButton) {
        cell.hitButton->setTag(slotIndex);
        setButtonEnabled(cell.hitButton, enabled);
    }
    if (cell.renameButton) {
        cell.renameButton->setTag(slotIndex);
        cell.renameButton->setVisible(occupied);
        cell.renameButton->setEnabled(occupied);
    }
    if (cell.deleteButton) {
        cell.deleteButton->setTag(slotIndex);
        cell.deleteButton->setVisible(occupied);
        cell.deleteButton->setEnabled(occupied);
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
    if (m_page < this->usedPageCount() - 1) return true;
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
            if (m_onSelect) m_onSelect(slotIndex);
            this->Popup::onClose(nullptr);
            return;
        }

        auto title = loc.getString("menu_layout.preset_overwrite_title");
        auto cancel = loc.getString("general.cancel");
        auto confirm = loc.getString("menu_layout.preset_overwrite_confirm");
        auto body = fmt::format(fmt::runtime(loc.getString("menu_layout.preset_overwrite_body")), slotIndex + 1);
        PopupManager::get().quickPopup(title.c_str(), body, cancel.c_str(), confirm.c_str(), confirmSelection).showInstant();
        return;
    }

    if (!MainMenuLayoutPresetManager::get().hasPreset(slotIndex)) return;

    auto title = loc.getString("menu_layout.preset_load_title");
    auto cancel = loc.getString("general.cancel");
    auto confirm = loc.getString("menu_layout.preset_load_confirm");
    auto body = fmt::format(fmt::runtime(loc.getString("menu_layout.preset_load_body")), slotIndex + 1);
    PopupManager::get().quickPopup(title.c_str(), body, cancel.c_str(), confirm.c_str(), confirmSelection).showInstant();
}

void MainMenuLayoutPresetPopup::onRenameSlot(CCObject* sender) {
    auto* button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;
    int const slotIndex = button->getTag();
    auto preset = MainMenuLayoutPresetManager::get().getPreset(slotIndex);
    if (!preset) return;

    WeakRef<MainMenuLayoutPresetPopup> self = this;
    auto* popup = RenameSlotPopup::create(preset->name, [self, slotIndex](std::string const& newName) {
        MainMenuLayoutPresetManager::get().renamePreset(slotIndex, newName);
        if (auto* p = self.lock().data(); p && p->getParent()) {
            p->refresh();
        }
    });
    if (popup) popup->show();
}

void MainMenuLayoutPresetPopup::onDeleteSlot(CCObject* sender) {
    auto* button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;
    int const slotIndex = button->getTag();
    if (!MainMenuLayoutPresetManager::get().hasPreset(slotIndex)) return;

    WeakRef<MainMenuLayoutPresetPopup> self = this;
    PopupManager::get().quickPopup(
        isSpanish() ? "Borrar Slot" : "Delete Slot",
        fmt::format(isSpanish() ? "Borrar el preset del slot <cy>{}</c>?" : "Delete preset in slot <cy>{}</c>?", slotIndex + 1),
        tr("general.cancel").c_str(),
        isSpanish() ? "Borrar" : "Delete",
        [self, slotIndex](auto*, bool confirmed) {
            if (!confirmed) return;
            MainMenuLayoutPresetManager::get().deletePreset(slotIndex);
            if (auto* p = self.lock().data(); p && p->getParent()) {
                if (p->m_page >= p->totalPageCount()) {
                    p->m_page = std::max(0, p->totalPageCount() - 1);
                }
                p->refresh();
                PaimonNotify::show(
                    isSpanish() ? "Slot borrado" : "Slot deleted",
                    NotificationIcon::Info);
            }
        }).showInstant();
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
