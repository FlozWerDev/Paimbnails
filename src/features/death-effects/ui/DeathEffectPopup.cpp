#include "DeathEffectPopup.hpp"

#include "../services/DeathEffectManager.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::death_effects {

namespace {
constexpr float kPopupWidth = 480.f;
constexpr float kPopupHeight = 300.f;
constexpr float kListWidth = 286.f;
constexpr float kListHeight = 164.f;
constexpr float kRowHeight = 39.f;

CCMenuItemToggler* makeToggle(
    CCObject* target,
    SEL_MenuHandler selector,
    bool checked
) {
    auto off = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    auto on = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    if (!off || !on) return nullptr;
    off->setScale(0.6f);
    on->setScale(0.6f);

    auto toggle = CCMenuItemToggler::create(off, on, target, selector);
    if (toggle) toggle->toggle(checked);
    return toggle;
}

CCMenuItemSpriteExtra* makeTextButton(
    std::string const& text,
    CCObject* target,
    SEL_MenuHandler selector,
    char const* texture = "GJ_button_01.png"
) {
    auto sprite = ButtonSprite::create(
        text.c_str(), "bigFont.fnt", texture, 0.7f
    );
    if (!sprite) return nullptr;
    sprite->setScale(0.55f);
    return CCMenuItemSpriteExtra::create(sprite, target, selector);
}

std::string formatDuration(unsigned int durationMs) {
    if (durationMs == 0) return "Unknown length";
    auto seconds = static_cast<float>(durationMs) / 1000.f;
    return seconds < 10.f
        ? fmt::format("{:.1f}s", seconds)
        : fmt::format("{}s", static_cast<int>(std::lround(seconds)));
}
}

DeathEffectPopup* DeathEffectPopup::create() {
    auto* ret = new DeathEffectPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool DeathEffectPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;

    this->setTitle("Death Effects", "goldFont.fnt", 0.75f);
    this->setID("death-effects-popup"_spr);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.38f);
    m_statusLabel->setPosition({kPopupWidth / 2.f, 248.f});
    m_mainLayer->addChild(m_statusLabel);

    auto libraryTitle = CCLabelBMFont::create("Sound Library", "goldFont.fnt");
    libraryTitle->setAnchorPoint({0.f, 0.5f});
    libraryTitle->setScale(0.48f);
    libraryTitle->setPosition({18.f, 224.f});
    m_mainLayer->addChild(libraryTitle);

    auto infoSprite = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
    if (infoSprite) {
        infoSprite->setScale(0.55f);
        auto infoButton = CCMenuItemSpriteExtra::create(
            infoSprite, this, menu_selector(DeathEffectPopup::onInfo)
        );
        infoButton->setPosition({292.f, 224.f});
        m_buttonMenu->addChild(infoButton);
    }

    auto listPanel = paimon::SpriteHelper::createDarkPanel(
        kListWidth + 8.f, kListHeight + 8.f, 90
    );
    if (listPanel) {
        listPanel->setPosition({14.f, 47.f});
        m_mainLayer->addChild(listPanel);
    }

    m_scroll = ScrollLayer::create({kListWidth, kListHeight});
    m_scroll->setPosition({18.f, 51.f});
    m_scroll->setID("sound-library-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 2);

    auto borders = ListBorders::create();
    borders->setContentSize({kListWidth + 4.f, kListHeight + 2.f});
    borders->setAnchorPoint({0.f, 0.f});
    borders->setPosition({16.f, 50.f});
    m_mainLayer->addChild(borders, 3);

    auto settingsPanel = paimon::SpriteHelper::createDarkPanel(154.f, 172.f, 90);
    if (settingsPanel) {
        settingsPanel->setPosition({312.f, 47.f});
        m_mainLayer->addChild(settingsPanel);
    }

    auto playbackTitle = CCLabelBMFont::create("Playback", "goldFont.fnt");
    playbackTitle->setScale(0.48f);
    playbackTitle->setPosition({389.f, 204.f});
    m_mainLayer->addChild(playbackTitle);

    auto orderLabel = CCLabelBMFont::create("Order", "bigFont.fnt");
    orderLabel->setAnchorPoint({0.f, 0.5f});
    orderLabel->setScale(0.34f);
    orderLabel->setPosition({322.f, 177.f});
    m_mainLayer->addChild(orderLabel);

    m_orderButton = makeTextButton(
        "Random", this, menu_selector(DeathEffectPopup::onCycleOrder)
    );
    if (m_orderButton) {
        m_orderButton->setPosition({424.f, 177.f});
        m_buttonMenu->addChild(m_orderButton);
    }

    auto repeatLabel = CCLabelBMFont::create("Avoid repeats", "bigFont.fnt");
    repeatLabel->setAnchorPoint({0.f, 0.5f});
    repeatLabel->setScale(0.31f);
    repeatLabel->setPosition({322.f, 149.f});
    m_mainLayer->addChild(repeatLabel);

    m_avoidRepeatToggle = makeToggle(
        this,
        menu_selector(DeathEffectPopup::onToggleAvoidRepeats),
        DeathEffectManager::get().avoidRepeats()
    );
    if (m_avoidRepeatToggle) {
        m_avoidRepeatToggle->setPosition({447.f, 149.f});
        m_buttonMenu->addChild(m_avoidRepeatToggle);
    }

    auto resetLabel = CCLabelBMFont::create("Stop on restart", "bigFont.fnt");
    resetLabel->setAnchorPoint({0.f, 0.5f});
    resetLabel->setScale(0.31f);
    resetLabel->setPosition({322.f, 124.f});
    m_mainLayer->addChild(resetLabel);

    m_stopOnResetToggle = makeToggle(
        this,
        menu_selector(DeathEffectPopup::onToggleStopOnReset),
        DeathEffectManager::get().stopOnReset()
    );
    if (m_stopOnResetToggle) {
        m_stopOnResetToggle->setPosition({447.f, 124.f});
        m_buttonMenu->addChild(m_stopOnResetToggle);
    }

    m_volumeLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_volumeLabel->setAnchorPoint({0.f, 0.5f});
    m_volumeLabel->setScale(0.31f);
    m_volumeLabel->setPosition({322.f, 96.f});
    m_mainLayer->addChild(m_volumeLabel);

    m_volumeSlider = Slider::create(
        this, menu_selector(DeathEffectPopup::onVolumeChanged), 0.48f
    );
    if (m_volumeSlider) {
        m_volumeSlider->setPosition({389.f, 86.f});
        m_mainLayer->addChild(m_volumeSlider);
    }

    m_pitchLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_pitchLabel->setAnchorPoint({0.f, 0.5f});
    m_pitchLabel->setScale(0.31f);
    m_pitchLabel->setPosition({322.f, 70.f});
    m_mainLayer->addChild(m_pitchLabel);

    m_pitchSlider = Slider::create(
        this, menu_selector(DeathEffectPopup::onPitchChanged), 0.48f
    );
    if (m_pitchSlider) {
        m_pitchSlider->setPosition({389.f, 54.f});
        m_mainLayer->addChild(m_pitchSlider);
    }

    auto importFile = makeTextButton(
        "Import File", this, menu_selector(DeathEffectPopup::onImportFile)
    );
    if (importFile) {
        importFile->setPosition({70.f, 25.f});
        m_buttonMenu->addChild(importFile);
    }

    auto importFolder = makeTextButton(
        "Import Folder", this, menu_selector(DeathEffectPopup::onImportFolder)
    );
    if (importFolder) {
        importFolder->setPosition({179.f, 25.f});
        m_buttonMenu->addChild(importFolder);
    }

    auto folderSprite = CCSprite::createWithSpriteFrameName("folderIcon_001.png");
    if (folderSprite) {
        folderSprite->setScale(0.65f);
        auto openFolder = CCMenuItemSpriteExtra::create(
            folderSprite, this, menu_selector(DeathEffectPopup::onOpenFolder)
        );
        openFolder->setPosition({275.f, 25.f});
        m_buttonMenu->addChild(openFolder);
    }

    m_originalButton = makeTextButton(
        "Use Original",
        this,
        menu_selector(DeathEffectPopup::onUseOriginal),
        "GJ_button_06.png"
    );
    if (m_originalButton) {
        m_originalButton->setPosition({399.f, 25.f});
        m_buttonMenu->addChild(m_originalButton);
    }

    refreshControls();
    rebuildList();
    return true;
}

void DeathEffectPopup::rebuildList() {
    if (!m_scroll) return;

    auto* content = m_scroll->m_contentLayer;
    content->removeAllChildrenWithCleanup(true);

    auto sounds = DeathEffectManager::get().scanLibrary();
    if (sounds.empty()) {
        content->setContentSize({kListWidth, kListHeight});

        auto empty = CCLabelBMFont::create("No custom sounds yet", "bigFont.fnt");
        empty->setScale(0.42f);
        empty->setOpacity(170);
        empty->setPosition({kListWidth / 2.f, kListHeight / 2.f + 10.f});
        content->addChild(empty);

        auto hint = CCLabelBMFont::create("Import a file or a folder below", "chatFont.fnt");
        hint->setScale(0.5f);
        hint->setOpacity(130);
        hint->setPosition({kListWidth / 2.f, kListHeight / 2.f - 12.f});
        content->addChild(hint);
        m_scroll->scrollToTop();
        refreshStatus();
        return;
    }

    auto totalHeight = std::max(kListHeight, kRowHeight * sounds.size());
    content->setContentSize({kListWidth, totalHeight});

    for (std::size_t index = 0; index < sounds.size(); ++index) {
        auto const& sound = sounds[index];
        auto bottom = totalHeight - kRowHeight * (index + 1);

        auto row = CCNode::create();
        row->setContentSize({kListWidth, kRowHeight});
        row->setPosition({0.f, bottom});
        content->addChild(row);

        auto background = paimon::SpriteHelper::createDarkPanel(
            kListWidth - 4.f, kRowHeight - 4.f, index % 2 == 0 ? 85 : 55, 3.f
        );
        if (background) {
            background->setPosition({2.f, 2.f});
            row->addChild(background);
        }

        auto name = CCLabelBMFont::create(sound.name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->limitLabelWidth(155.f, 0.38f, 0.18f);
        name->setPosition({10.f, 25.f});
        name->setColor(sound.playable ? ccWHITE : ccc3(255, 125, 125));
        row->addChild(name);

        auto extension = geode::utils::string::toUpper(
            geode::utils::string::pathToString(sound.path.extension())
        );
        auto details = fmt::format("{}  -  {}", extension, formatDuration(sound.durationMs));
        auto detailLabel = CCLabelBMFont::create(details.c_str(), "chatFont.fnt");
        detailLabel->setAnchorPoint({0.f, 0.5f});
        detailLabel->setScale(0.42f);
        detailLabel->setOpacity(160);
        detailLabel->setPosition({10.f, 11.f});
        row->addChild(detailLabel);

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(row->getContentSize());
        row->addChild(menu, 2);

        auto pathObject = CCString::create(
            geode::utils::string::pathToString(sound.path)
        );

        auto selected = makeToggle(
            this,
            menu_selector(DeathEffectPopup::onToggleSound),
            sound.selected
        );
        if (selected) {
            selected->setPosition({207.f, kRowHeight / 2.f});
            selected->setUserObject(pathObject);
            selected->setEnabled(sound.playable);
            menu->addChild(selected);
        }

        auto playSprite = CCSprite::createWithSpriteFrameName("GJ_playMusicBtn_001.png");
        if (playSprite) {
            playSprite->setScale(0.52f);
            auto play = CCMenuItemSpriteExtra::create(
                playSprite, this, menu_selector(DeathEffectPopup::onPreview)
            );
            play->setPosition({242.f, kRowHeight / 2.f});
            play->setUserObject(pathObject);
            play->setEnabled(sound.playable);
            menu->addChild(play);
        }

        auto deleteSprite = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        if (deleteSprite) {
            deleteSprite->setScale(0.47f);
            auto remove = CCMenuItemSpriteExtra::create(
                deleteSprite, this, menu_selector(DeathEffectPopup::onDelete)
            );
            remove->setPosition({272.f, kRowHeight / 2.f});
            remove->setUserObject(pathObject);
            menu->addChild(remove);
        }
    }

    m_scroll->scrollToTop();
    refreshStatus();
}

void DeathEffectPopup::refreshControls() {
    auto& manager = DeathEffectManager::get();
    auto random = manager.order() == PlaybackOrder::Random;

    if (m_orderButton) {
        auto sprite = ButtonSprite::create(
            random ? "Random" : "In Order",
            "bigFont.fnt",
            "GJ_button_01.png",
            0.7f
        );
        sprite->setScale(0.55f);
        m_orderButton->setNormalImage(sprite);
    }

    if (m_avoidRepeatToggle) {
        m_avoidRepeatToggle->toggle(manager.avoidRepeats());
        m_avoidRepeatToggle->setEnabled(random);
        m_avoidRepeatToggle->setOpacity(random ? 255 : 110);
    }
    if (m_stopOnResetToggle) {
        m_stopOnResetToggle->toggle(manager.stopOnReset());
    }
    if (m_volumeSlider) m_volumeSlider->setValue(manager.volume() / 2.f);
    if (m_pitchSlider) m_pitchSlider->setValue(manager.pitchVariation() / 0.5f);

    if (m_volumeLabel) {
        m_volumeLabel->setString(
            fmt::format("Volume  {}%", static_cast<int>(std::lround(manager.volume() * 100.f))).c_str()
        );
    }
    if (m_pitchLabel) {
        m_pitchLabel->setString(
            fmt::format("Pitch variation  +/-{}%", static_cast<int>(std::lround(manager.pitchVariation() * 100.f))).c_str()
        );
    }

    if (m_originalButton) {
        auto hasCustom = manager.selectedCount() > 0;
        m_originalButton->setEnabled(hasCustom);
        m_originalButton->setOpacity(hasCustom ? 255 : 120);
    }
    refreshStatus();
}

void DeathEffectPopup::refreshStatus() {
    if (!m_statusLabel) return;
    m_statusLabel->setScale(0.38f);

    auto& manager = DeathEffectManager::get();
    auto selected = manager.selectedCount();
    auto ready = manager.readyCount();

    if (selected == 0) {
        m_statusLabel->setString("Using Geometry Dash's original death sound");
        m_statusLabel->setColor({150, 255, 165});
        return;
    }
    if (ready < selected) {
        m_statusLabel->setString(
            fmt::format("{} of {} selected sounds are ready - original is the fallback", ready, selected).c_str()
        );
        m_statusLabel->setColor({255, 210, 105});
        m_statusLabel->limitLabelWidth(420.f, 0.38f, 0.22f);
        return;
    }

    auto orderText = manager.order() == PlaybackOrder::Random ? "Random" : "In Order";
    m_statusLabel->setString(
        fmt::format("{} selected - {} - replaces the original", selected, orderText).c_str()
    );
    m_statusLabel->setColor({120, 220, 255});
}

std::filesystem::path DeathEffectPopup::pathFromSender(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    auto* path = node ? typeinfo_cast<CCString*>(node->getUserObject()) : nullptr;
    return path ? std::filesystem::path(path->getCString()) : std::filesystem::path();
}

void DeathEffectPopup::onToggleSound(CCObject* sender) {
    auto path = pathFromSender(sender);
    if (path.empty()) return;

    auto& manager = DeathEffectManager::get();
    manager.setSoundSelected(path, !manager.isSelected(path));
    rebuildList();
    refreshControls();
}

void DeathEffectPopup::onPreview(CCObject* sender) {
    auto path = pathFromSender(sender);
    if (path.empty()) return;
    if (!DeathEffectManager::get().preview(path)) {
        PaimonNotify::show("Could not preview this sound.", NotificationIcon::Error);
    }
}

void DeathEffectPopup::onDelete(CCObject* sender) {
    auto path = pathFromSender(sender);
    if (path.empty()) return;

    auto name = geode::utils::string::pathToString(path.stem());
    WeakRef<DeathEffectPopup> self = this;
    createQuickPopup(
        "Delete Sound",
        fmt::format("Delete <cy>{}</c> from your Paimbnails sound library?", name),
        "Cancel",
        "Delete",
        [self, path](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup) return;

            if (DeathEffectManager::get().removeSound(path)) {
                PaimonNotify::show("Sound deleted.", NotificationIcon::Success);
                popup->rebuildList();
                popup->refreshControls();
            } else {
                PaimonNotify::show("Could not delete the sound.", NotificationIcon::Error);
            }
        }
    );
}

void DeathEffectPopup::onImportFile(CCObject*) {
    WeakRef<DeathEffectPopup> self = this;
    pt::pickAudio([self](Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;

        if (!result) {
            PaimonNotify::show(result.unwrapErr(), NotificationIcon::Error);
            return;
        }
        auto selected = result.unwrap();
        if (!selected) return;

        auto imported = DeathEffectManager::get().importSound(*selected);
        if (!imported) {
            PaimonNotify::show(imported.unwrapErr(), NotificationIcon::Error);
            return;
        }

        auto path = imported.unwrap();
        DeathEffectManager::get().setSoundSelected(path, true);
        PaimonNotify::show("Sound imported and selected.", NotificationIcon::Success);
        popup->rebuildList();
        popup->refreshControls();
    });
}

void DeathEffectPopup::onImportFolder(CCObject*) {
    WeakRef<DeathEffectPopup> self = this;
    pt::pickFolder([self](Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;

        if (!result) {
            PaimonNotify::show(result.unwrapErr(), NotificationIcon::Error);
            return;
        }
        auto selected = result.unwrap();
        if (!selected) return;

        auto imported = DeathEffectManager::get().importFolder(*selected);
        DeathEffectManager::get().addSelected(imported.imported);

        if (imported.imported.empty()) {
            PaimonNotify::show(
                "No playable audio files were found.",
                NotificationIcon::Info
            );
        } else {
            PaimonNotify::show(
                fmt::format(
                    "Imported and selected {} sound{}.",
                    imported.imported.size(),
                    imported.imported.size() == 1 ? "" : "s"
                ),
                imported.skipped ? NotificationIcon::Warning : NotificationIcon::Success
            );
        }

        popup->rebuildList();
        popup->refreshControls();
    });
}

void DeathEffectPopup::onOpenFolder(CCObject*) {
    auto path = DeathEffectManager::get().libraryDir();
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec || !geode::utils::file::openFolder(path)) {
        PaimonNotify::show("Could not open the sound library folder.", NotificationIcon::Error);
    }
}

void DeathEffectPopup::onUseOriginal(CCObject*) {
    DeathEffectManager::get().useOriginal();
    rebuildList();
    refreshControls();
    PaimonNotify::show("Original death sound restored.", NotificationIcon::Success);
}

void DeathEffectPopup::onCycleOrder(CCObject*) {
    auto& manager = DeathEffectManager::get();
    manager.setOrder(
        manager.order() == PlaybackOrder::Random
            ? PlaybackOrder::Sequential
            : PlaybackOrder::Random
    );
    refreshControls();
}

void DeathEffectPopup::onToggleAvoidRepeats(CCObject*) {
    auto& manager = DeathEffectManager::get();
    manager.setAvoidRepeats(!manager.avoidRepeats());
    refreshControls();
}

void DeathEffectPopup::onToggleStopOnReset(CCObject*) {
    auto& manager = DeathEffectManager::get();
    manager.setStopOnReset(!manager.stopOnReset());
    refreshControls();
}

void DeathEffectPopup::onVolumeChanged(CCObject*) {
    if (!m_volumeSlider || !m_volumeSlider->getThumb()) return;
    auto value = std::clamp(m_volumeSlider->getThumb()->getValue(), 0.f, 1.f);
    DeathEffectManager::get().setVolume(value * 2.f);
    refreshControls();
}

void DeathEffectPopup::onPitchChanged(CCObject*) {
    if (!m_pitchSlider || !m_pitchSlider->getThumb()) return;
    auto value = std::clamp(m_pitchSlider->getThumb()->getValue(), 0.f, 1.f);
    DeathEffectManager::get().setPitchVariation(value * 0.5f);
    refreshControls();
}

void DeathEffectPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        "Death Effects",
        "<cy>Selected sounds replace the normal death sound.</c> "
        "Select several for a random or ordered pool. If a file is missing or unreadable, "
        "Geometry Dash automatically plays its <cg>original sound</c> instead.\n\n"
        "Imported files are copied into Paimbnails; the game's files are never changed.",
        "OK"
    )->show();
}

void DeathEffectPopup::onClose(CCObject* sender) {
    DeathEffectManager::get().stopPreview();
    Popup::onClose(sender);
}

} // namespace paimon::death_effects
