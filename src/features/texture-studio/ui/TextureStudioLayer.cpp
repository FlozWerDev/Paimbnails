#include "TextureStudioLayer.hpp"

#include "../../../utils/FileDialog.hpp"
#include "../services/LiveSlotRuntime.hpp"
#include "../persist/ProjectShare.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "NewProjectPopup.hpp"
#include "LiveSlotEditor.hpp"
#include "SlotsGridView.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/PopupManager.hpp>
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

CCScene* TextureStudioLayer::scene() {
    auto* scene = CCScene::create();
    scene->addChild(TextureStudioLayer::create());
    return scene;
}

void TextureStudioLayer::open() {
    if (auto* layer = TextureStudioLayer::create()) {
        geode::pushSceneWithLayer(layer);
    }
}

bool TextureStudioLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setID("texture-studio-layer"_spr);

    auto winSize = CCDirector::get()->getWinSize();

    buildBackground();

    if (auto* title = CCLabelBMFont::create("Pack Gen", "bigFont.fnt")) {
        title->setScale(0.75f);
        title->setPosition({winSize.width / 2.f, winSize.height - 22.f});
        this->addChild(title, 5);
    }
    if (auto* subtitle = CCLabelBMFont::create(
            "Color slots applied live to the game", "chatFont.fnt")) {
        subtitle->setScale(0.55f);
        subtitle->setColor({185, 190, 200});
        subtitle->setPosition({winSize.width / 2.f, winSize.height - 40.f});
        this->addChild(subtitle, 5);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 10);

    if (auto* backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
        if (auto* backBtn = CCMenuItemExt::createSpriteExtra(backSpr,
                [this](CCMenuItemSpriteExtra*) { this->onBack(nullptr); })) {
            backBtn->setPosition({26.f, winSize.height - 24.f});
            menu->addChild(backBtn);
        }
    }

    if (auto* newSpr = ButtonSprite::create("+ New Slot", "bigFont.fnt", "GJ_button_01.png", 0.42f)) {
        if (auto* newBtn = CCMenuItemExt::createSpriteExtra(newSpr,
                [this](CCMenuItemSpriteExtra*) { this->onNewPack(nullptr); })) {
            newBtn->setPosition({winSize.width - 60.f, winSize.height - 24.f});
            menu->addChild(newBtn);
        }
    }
    if (auto* importSpr = ButtonSprite::create("Import JSON", "bigFont.fnt", "GJ_button_05.png", 0.36f)) {
        if (auto* importBtn = CCMenuItemExt::createSpriteExtra(importSpr,
                [this](CCMenuItemSpriteExtra*) { this->onImportJson(nullptr); })) {
            importBtn->setPosition({winSize.width - 210.f, winSize.height - 24.f});
            menu->addChild(importBtn);
        }
    }
    if (auto* folderSpr = ButtonSprite::create("Folder", "bigFont.fnt", "GJ_button_05.png", 0.4f)) {
        if (auto* folderBtn = CCMenuItemExt::createSpriteExtra(folderSpr,
                [this](CCMenuItemSpriteExtra*) { this->onOpenFolder(nullptr); })) {
            folderBtn->setPosition({winSize.width - 46.f, 20.f});
            menu->addChild(folderBtn);
        }
    }

    const float gridW = winSize.width - 70.f;
    const float gridH = winSize.height - 108.f;
    m_grid = SlotsGridView::create(gridW, gridH,
        [this](std::string const& id) { this->onApplySlot(id); },
        [this](std::string const& id) { this->onEditSlot(id);  },
        [this](std::string const& id) { this->onDeleteSlot(id); },
        [this]() { this->onNewPack(nullptr); });
    if (m_grid) {
        m_grid->setAnchorPoint({0.5f, 0.5f});
        m_grid->setPosition({winSize.width / 2.f, winSize.height / 2.f - 8.f});
        this->addChild(m_grid, 5);
    }

    if (auto* activeLbl = CCLabelBMFont::create("Active: (none)", "bigFont.fnt")) {
        activeLbl->setScale(0.36f);
        activeLbl->setAnchorPoint({0.f, 0.5f});
        activeLbl->setPosition({14.f, 20.f});
        this->addChild(activeLbl, 5);
        m_activeLbl = activeLbl;
        refreshFooter();
    }

    return true;
}

void TextureStudioLayer::buildBackground() {
    auto winSize = CCDirector::get()->getWinSize();

    auto* bg = CCLayerColor::create(ccc4(16, 14, 26, 255));
    bg->setContentSize(winSize);
    this->addChild(bg, -5);

    auto* gradient = CCLayerGradient::create(
        ccc4(52, 30, 74, 110), ccc4(8, 6, 16, 160));
    gradient->setContentSize(winSize);
    gradient->setVector({0, -1});
    this->addChild(gradient, -4);

    if (auto* bottomLeft = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png")) {
        bottomLeft->setAnchorPoint({0, 0});
        bottomLeft->setPosition({-2, -2});
        bottomLeft->setOpacity(70);
        this->addChild(bottomLeft, -1);
    }
    if (auto* bottomRight = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png")) {
        bottomRight->setAnchorPoint({1, 0});
        bottomRight->setPosition({winSize.width + 2, -2});
        bottomRight->setFlipX(true);
        bottomRight->setOpacity(70);
        this->addChild(bottomRight, -1);
    }
}

void TextureStudioLayer::onEnter() {
    CCLayer::onEnter();
    // Refresh on return: project may have changed in editor.
    if (m_enteredOnce) {
        if (m_grid) m_grid->refresh();
        refreshFooter();
    }
    m_enteredOnce = true;
}

void TextureStudioLayer::keyBackClicked() {
    onBack(nullptr);
}

void TextureStudioLayer::onBack(CCObject*) {
    CCDirector::get()->popSceneWithTransition(0.4f, PopTransition::kPopTransitionFade);
}

void TextureStudioLayer::onNewPack(CCObject*) {
    auto* popup = NewProjectPopup::create([this](std::string const& slotId) {
        log::info("[texture-studio] new slot created: {}", slotId);
        if (m_grid) m_grid->refresh();
        this->refreshFooter();
        LiveSlotEditor::open(slotId);
    });
    if (popup) popup->show();
}

void TextureStudioLayer::onImportJson(CCObject*) {
    WeakRef<TextureStudioLayer> weakSelf(this);
    pt::pickJson([weakSelf](
            geode::Result<std::optional<std::filesystem::path>> result) {
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto imported = ProjectShare::importFrom(*pathOpt);
        if (!imported) {
            Notification::create(("Import failed: " + imported.unwrapErr()).c_str(),
                NotificationIcon::Error, 4.0f)->show();
            return;
        }
        auto slotId = imported.unwrap();
        log::info("[texture-studio] imported pack json as slot '{}'", slotId);
        Notification::create("Pack imported.", NotificationIcon::Success, 2.f)->show();

        Loader::get()->queueInMainThread([weakSelf, slotId]() {
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (self->m_grid) self->m_grid->refresh();
            self->refreshFooter();
        });
    });
}

void TextureStudioLayer::onApplySlot(std::string const& slotId) {
    auto& store = SlotStore::get();
    if (store.activeSlotId() == slotId) {
        auto result = LiveSlotRuntime::get().disable();
        if (!result) {
            Notification::create(result.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
    } else {
        auto loaded = store.loadSlot(slotId);
        if (!loaded) {
            Notification::create(loaded.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
        auto project = loaded.unwrap();
        if (!project.liveRendering) {
            LiveSlotEditor::open(slotId);
            return;
        }
        auto result = LiveSlotRuntime::get().activate(project);
        if (!result) {
            Notification::create(result.unwrapErr(), NotificationIcon::Error)->show();
            return;
        }
    }
    if (m_grid) m_grid->refresh();
    refreshFooter();
}

void TextureStudioLayer::onEditSlot(std::string const& slotId) {
    LiveSlotEditor::open(slotId);
}

void TextureStudioLayer::onDeleteSlot(std::string const& slotId) {
    PopupManager::get().quickPopup(
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
            LiveSlotRuntime::get().restoreSaved();
            if (m_grid) m_grid->refresh();
            refreshFooter();
            Notification::create("Slot deleted.", NotificationIcon::Success, 1.5f)->show();
        }).showInstant();
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
