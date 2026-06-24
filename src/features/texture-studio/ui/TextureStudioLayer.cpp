#include "TextureStudioLayer.hpp"

#include "../engine/TextureLoaderInstaller.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "NewProjectPopup.hpp"
#include "ProjectEditorLayer.hpp"
#include "SlotsGridView.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

TextureStudioLayer* TextureStudioLayer::create() {
    auto* ret = new TextureStudioLayer();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool TextureStudioLayer::init() {
    constexpr float kW = 420.f;
    constexpr float kH = 290.f;
    if (!Popup::init(kW, kH)) return false;
    this->setTitle("Texture Studio");

    // New Pack button (top-right)
    if (m_buttonMenu) {
        if (auto* spr = ButtonSprite::create("+ New Pack", "bigFont.fnt", "GJ_button_01.png", 0.5f)) {
            if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                    [this](CCMenuItemSpriteExtra*) { this->onNewPack(nullptr); })) {
                m_buttonMenu->addChildAtPosition(btn, Anchor::TopRight, {-22.f, -24.f});
            }
        }
    }

    // Slot grid
    constexpr float kGridW = 384.f;
    constexpr float kGridH = 190.f;
    m_grid = SlotsGridView::create(kGridW, kGridH,
        [this](std::string const& id) { this->onApplySlot(id); },
        [this](std::string const& id) { this->onEditSlot(id);  },
        [this](std::string const& id) { this->onDeleteSlot(id); },
        [this]() { this->onNewPack(nullptr); });
    if (m_grid) {
        m_grid->setAnchorPoint({0.5f, 0.5f});
        m_mainLayer->addChildAtPosition(m_grid, Anchor::Center, {0.f, -6.f});
    }

    // Footer
    if (auto* activeLbl = CCLabelBMFont::create("Active: (none)", "bigFont.fnt")) {
        activeLbl->setScale(0.36f);
        activeLbl->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(activeLbl, Anchor::BottomLeft, {16.f, 16.f});
        m_activeLbl = activeLbl;
        refreshFooter();
    }

    if (m_buttonMenu) {
        if (auto* folderSpr = ButtonSprite::create("Folder", "bigFont.fnt", "GJ_button_05.png", 0.5f)) {
            if (auto* folderBtn = CCMenuItemExt::createSpriteExtra(folderSpr,
                    [this](CCMenuItemSpriteExtra*) { this->onOpenFolder(nullptr); })) {
                m_buttonMenu->addChildAtPosition(folderBtn, Anchor::BottomRight, {-42.f, 18.f});
            }
        }
    }

    return true;
}

void TextureStudioLayer::onNewPack(CCObject*) {
    auto* popup = NewProjectPopup::create([this](std::string const& slotId) {
        log::info("[texture-studio] new slot created: {}", slotId);
        SlotStore::get().setActiveSlot(slotId);
        if (m_grid) m_grid->refresh();
        this->refreshFooter();
        // Future (Phase 6): open ProjectEditorLayer here.
    });
    if (popup) popup->show();
}

void TextureStudioLayer::onApplySlot(std::string const& slotId) {
    SlotStore::get().setActiveSlot(slotId);
    refreshFooter();

    // Verify Texture Loader is present.
    if (!TextureLoaderInstaller::isInstalled()) {
        geode::createQuickPopup(
            "Texture Loader Required",
            "Install <cy>Texture Loader</c> to apply packs.\n"
            "Open the Geode mod browser?",
            "Cancel", "Open Index",
            [](FLAlertLayer*, bool yes) {
                if (yes) {
                    web::openLinkInBrowser(
                        "https://geode-sdk.org/mods/geode.texture-loader");
                }
            });
        return;
    }

    // The slot must have been built at least once.
    auto loaded = SlotStore::get().loadSlot(slotId);
    if (!loaded) {
        Notification::create(("Slot load failed: " + loaded.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return;
    }
    auto const& project = loaded.unwrap();
    if (!project.hasBuiltOnce) {
        Notification::create("Generate the pack first (Edit → Generate).",
            NotificationIcon::Warning, 2.5f)->show();
        return;
    }

    auto sourceZip = SlotPaths::outputZipFile(slotId);
    auto installRes = TextureLoaderInstaller::install(sourceZip, project.id);
    if (!installRes) {
        Notification::create(("Apply failed: " + installRes.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return;
    }

    geode::createQuickPopup(
        "Applied!",
        "Pack copied to Texture Loader.\n"
        "<cy>Reload the game</c> to see the changes.",
        "OK", nullptr,
        [](FLAlertLayer*, bool) {});
}

void TextureStudioLayer::onEditSlot(std::string const& slotId) {
    auto* editor = ProjectEditorLayer::create(slotId);
    if (editor) editor->show();
    // Do not refresh here: this method is invoked by a button owned by the
    // grid, so rebuilding it before the callback returns destroys the active
    // CCMenuItem and its lambda (use-after-free). The grid is refreshed when
    // Texture Studio is reopened; editor changes do not alter card identity.
}

void TextureStudioLayer::onDeleteSlot(std::string const& slotId) {
    geode::createQuickPopup(
        "Delete Slot",
        ("Delete <cy>" + slotId + "</c>?\nThis cannot be undone.").c_str(),
        "Cancel", "Delete",
        [this, slotId](FLAlertLayer*, bool yes) {
            if (!yes) return;
            auto r = SlotStore::get().deleteSlot(slotId);
            if (!r) {
                Notification::create(
                    ("Delete failed: " + r.unwrapErr()).c_str(),
                    NotificationIcon::Error, 3.0f)->show();
                return;
            }
            if (m_grid) m_grid->refresh();
            refreshFooter();
            Notification::create("Slot deleted.", NotificationIcon::Success, 1.5f)->show();
        });
}

void TextureStudioLayer::onOpenFolder(CCObject*) {
    auto path = SlotPaths::rootDir();
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    file::openFolder(path);
}

void TextureStudioLayer::refreshFooter() {
    if (!m_activeLbl) return;
    auto const& active = SlotStore::get().activeSlotId();
    if (active.empty()) {
        m_activeLbl->setString("Active: (none)");
        return;
    }
    // Show the friendly pack name rather than the raw slot id; fall back to
    // the id if the slot isn't in the index (e.g. just deleted).
    SlotStore::get().loadIndex();
    std::string display = active;
    for (auto const& entry : SlotStore::get().list()) {
        if (entry.id == active) {
            if (!entry.name.empty()) display = entry.name;
            break;
        }
    }
    m_activeLbl->setString(("Active: " + display).c_str());
}

}  // namespace paimon::texture_studio
