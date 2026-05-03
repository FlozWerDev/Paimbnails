#include "PetConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "PaimonShopPopup.hpp"
#include "../services/PetManager.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/InfoButton.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
bool allNonGameplayLayersSelected(std::set<std::string> const& selectedLayers) {
    for (auto const& opt : PET_LAYER_OPTIONS) {
        if (isPetGameplayLayer(opt)) continue;
        if (selectedLayers.count(opt) == 0) {
            return false;
        }
    }
    return true;
}

bool scrollLayerWithWheel(ScrollLayer* scrollLayer, float x, float y) {
#if !defined(GEODE_IS_WINDOWS) && !defined(GEODE_IS_MACOS)
    return false;
#else
    if (!scrollLayer) return false;

    CCPoint mousePos = geode::cocos::getMousePos();
    CCRect scrollRect = scrollLayer->boundingBox();
    scrollRect.origin = scrollLayer->getParent()->convertToWorldSpace(scrollRect.origin);
    if (!scrollRect.containsPoint(mousePos)) return false;

    float scrollAmount = y;
    if (std::abs(scrollAmount) < 0.001f) {
        scrollAmount = -x;
    }

    auto* contentLayer = scrollLayer->m_contentLayer;
    if (!contentLayer) return false;

    float newY = contentLayer->getPositionY() - scrollAmount * 6.f;
    float minY = scrollLayer->getContentSize().height - contentLayer->getContentSize().height;
    float maxY = 0.f;
    if (minY > maxY) minY = maxY;

    contentLayer->setPositionY(std::max(minY, std::min(maxY, newY)));
    return true;
#endif
}

class PetLayerPickerPopup final : public geode::Popup {
protected:
    WeakRef<PetConfigPopup> m_owner;
    ScrollLayer* m_scrollLayer = nullptr;
    std::vector<CCMenuItemToggler*> m_layerToggles;

    bool init(PetConfigPopup* owner) {
        if (!Popup::init(320.f, 250.f)) return false;

        m_owner = owner;
        this->setTitle("Custom Layers");
        this->setMouseEnabled(true);

        auto content = m_mainLayer->getContentSize();

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(menu, 10);

        if (auto infoBtn = PaimonInfo::createInfoBtn(
                "Custom Layers",
                "Choose the individual non-gameplay screens where the pet can appear.\n"
                "Gameplay is controlled separately by <cg>Show In Gameplay</c>.",
                this, 0.42f)) {
            infoBtn->setPosition({content.width / 2.f + 78.f, content.height - 20.f});
            menu->addChild(infoBtn);
        }

        m_scrollLayer = ScrollLayer::create({content.width - 16.f, content.height - 62.f});
        m_scrollLayer->setPosition({8.f, 28.f});
        m_mainLayer->addChild(m_scrollLayer, 5);

        size_t nonGameplayCount = 0;
        for (auto const& layerName : PET_LAYER_OPTIONS) {
            if (!isPetGameplayLayer(layerName)) {
                ++nonGameplayCount;
            }
        }

        CCNode* sc = m_scrollLayer->m_contentLayer;
        float totalH = std::max(260.f, 16.f * static_cast<float>(nonGameplayCount) + 24.f);
        sc->setContentSize({content.width - 16.f, totalH});

        auto scrollContent = CCLayer::create();
        scrollContent->setContentSize({content.width - 16.f, totalH});
        sc->addChild(scrollContent);
        sc = scrollContent;

        auto navMenu = CCMenu::create();
        navMenu->setPosition({0.f, 0.f});
        scrollContent->addChild(navMenu, 10);

        float cx = (content.width - 16.f) / 2.f;
        float y = totalH - 12.f;

        auto const& selectedLayers = PetManager::get().config().visibleLayers;
        for (auto const& layerName : PET_LAYER_OPTIONS) {
            if (isPetGameplayLayer(layerName)) continue;

            auto lbl = CCLabelBMFont::create(layerName.c_str(), "bigFont.fnt");
            lbl->setScale(0.3f);
            lbl->setAnchorPoint({0.f, 0.5f});
            lbl->setPosition({cx - 105.f, y});
            sc->addChild(lbl);

            auto toggle = CCMenuItemToggler::createWithStandardSprites(
                this, menu_selector(PetLayerPickerPopup::onLayerToggled), 0.32f);
            toggle->setPosition({cx + 105.f, y});
            toggle->toggle(selectedLayers.count(layerName) > 0);
            toggle->setUserObject(CCString::create(layerName));
            navMenu->addChild(toggle);
            m_layerToggles.push_back(toggle);

            y -= 16.f;
        }

        auto selectAllSpr = ButtonSprite::create("All", "goldFont.fnt", "GJ_button_01.png", 0.55f);
        selectAllSpr->setScale(0.45f);
        auto selectAllBtn = CCMenuItemSpriteExtra::create(
            selectAllSpr, this, menu_selector(PetLayerPickerPopup::onSelectAll));
        selectAllBtn->setPosition({content.width / 2.f - 55.f, 15.f});
        menu->addChild(selectAllBtn);

        auto clearSpr = ButtonSprite::create("None", "goldFont.fnt", "GJ_button_06.png", 0.55f);
        clearSpr->setScale(0.45f);
        auto clearBtn = CCMenuItemSpriteExtra::create(
            clearSpr, this, menu_selector(PetLayerPickerPopup::onClearAll));
        clearBtn->setPosition({content.width / 2.f + 55.f, 15.f});
        menu->addChild(clearBtn);

        m_scrollLayer->moveToTop();
        return true;
    }

    void syncOwner() {
        auto owner = m_owner.lock();
        if (!owner) return;
        static_cast<PetConfigPopup*>(owner.data())->refreshVisibleLayerControls();
        static_cast<PetConfigPopup*>(owner.data())->applyLive();
    }

    void onLayerToggled(CCObject* sender) {
        auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
        if (!toggle) return;

        auto* nameStr = typeinfo_cast<CCString*>(toggle->getUserObject());
        if (!nameStr) return;

        auto& layers = PetManager::get().config().visibleLayers;
        std::string const layerName = nameStr->getCString();
        bool const turnOn = !toggle->isToggled();

        if (turnOn) {
            layers.insert(layerName);
        } else {
            layers.erase(layerName);
        }

        syncOwner();
    }

    void onSelectAll(CCObject*) {
        auto& layers = PetManager::get().config().visibleLayers;
        for (auto const& layerName : PET_LAYER_OPTIONS) {
            if (isPetGameplayLayer(layerName)) continue;
            layers.insert(layerName);
        }
        for (auto* toggle : m_layerToggles) {
            if (toggle) toggle->toggle(true);
        }
        syncOwner();
    }

    void onClearAll(CCObject*) {
        auto& layers = PetManager::get().config().visibleLayers;
        for (auto const& layerName : PET_LAYER_OPTIONS) {
            if (isPetGameplayLayer(layerName)) continue;
            layers.erase(layerName);
        }
        for (auto* toggle : m_layerToggles) {
            if (toggle) toggle->toggle(false);
        }
        syncOwner();
    }

    void scrollWheel(float x, float y) override {
        (void)scrollLayerWithWheel(m_scrollLayer, x, y);
    }

public:
    static PetLayerPickerPopup* create(PetConfigPopup* owner) {
        auto ret = new PetLayerPickerPopup();
        if (ret && ret->init(owner)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};
}

// ════════════════════════════════════════════════════════════
// create
// ════════════════════════════════════════════════════════════

PetConfigPopup* PetConfigPopup::create() {
    auto ret = new PetConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// ════════════════════════════════════════════════════════════
// init
// ════════════════════════════════════════════════════════════

bool PetConfigPopup::init() {
    if (!Popup::init(380.f, 270.f)) return false;

    this->setTitle("Pet / Mascot");
    this->setMouseEnabled(true);

    auto content = m_mainLayer->getContentSize();

    // ── tab layers ──
    m_galleryTab = CCNode::create();
    m_galleryTab->setID("gallery-tab"_spr);
    m_galleryTab->setContentSize(content);
    m_mainLayer->addChild(m_galleryTab, 5);

    m_settingsTab = CCNode::create();
    m_settingsTab->setID("settings-tab"_spr);
    m_settingsTab->setContentSize(content);
    m_settingsTab->setVisible(false);
    m_mainLayer->addChild(m_settingsTab, 5);

    m_advancedTab = CCNode::create();
    m_advancedTab->setID("advanced-tab"_spr);
    m_advancedTab->setContentSize(content);
    m_advancedTab->setVisible(false);
    m_mainLayer->addChild(m_advancedTab, 5);

    createTabButtons();
    buildGalleryTab();
    buildSettingsTab();
    buildAdvancedTab();

    paimon::markDynamicPopup(this);
    return true;
}

void PetConfigPopup::onExit() {
    this->unschedule(schedule_selector(PetConfigPopup::checkScrollPosition));
    this->unschedule(schedule_selector(PetConfigPopup::checkAdvancedScrollPosition));
    if (m_scrollArrow) {
        m_scrollArrow->stopAllActions();
    }
    if (m_advancedArrow) {
        m_advancedArrow->stopAllActions();
    }
    Popup::onExit();
}

void PetConfigPopup::scrollWheel(float x, float y) {
    if (m_currentTab == 1 && scrollLayerWithWheel(m_scrollLayer, x, y)) return;
    if (m_currentTab == 2 && scrollLayerWithWheel(m_advancedScroll, x, y)) return;
}


// ════════════════════════════════════════════════════════════
// tabs
// ════════════════════════════════════════════════════════════

void PetConfigPopup::createTabButtons() {
    auto content = m_mainLayer->getContentSize();
    float topY = content.height - 38.f;
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setID("tab-buttons-menu"_spr);
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    auto spr1 = ButtonSprite::create("Gallery");
    spr1->setScale(0.45f);
    auto tab1 = CCMenuItemSpriteExtra::create(spr1, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab1->setTag(0);
    tab1->setPosition({cx - 80.f, topY});
    menu->addChild(tab1);
    m_tabs.push_back(tab1);

    auto spr2 = ButtonSprite::create("Settings");
    spr2->setScale(0.45f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab2->setTag(1);
    tab2->setPosition({cx, topY});
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    auto spr3 = ButtonSprite::create("Advanced");
    spr3->setScale(0.45f);
    auto tab3 = CCMenuItemSpriteExtra::create(spr3, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab3->setTag(2);
    tab3->setPosition({cx + 80.f, topY});
    menu->addChild(tab3);
    m_tabs.push_back(tab3);

    // initial state
    onTabSwitch(tab1);
}

void PetConfigPopup::onTabSwitch(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();

    m_galleryTab->setVisible(m_currentTab == 0);
    m_settingsTab->setVisible(m_currentTab == 1);
    m_advancedTab->setVisible(m_currentTab == 2);

    for (auto* tab : m_tabs) {
        auto spr = typeinfo_cast<ButtonSprite*>(tab->getNormalImage());
        if (!spr) continue;
        if (tab->getTag() == m_currentTab) {
            spr->setColor({0, 255, 0});
            spr->setOpacity(255);
        } else {
            spr->setColor({255, 255, 255});
            spr->setOpacity(150);
        }
    }
}

// ════════════════════════════════════════════════════════════
// gallery tab
// ════════════════════════════════════════════════════════════

void PetConfigPopup::buildGalleryTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // auto-cleanup invalid/corrupt image files from gallery
    int cleaned = PetManager::get().cleanupInvalidImages();
    if (cleaned > 0) {
        log::info("[PetConfig] Cleaned up {} invalid image files from gallery", cleaned);
    }

    // preview area
    auto previewBg = paimon::SpriteHelper::createDarkPanel(80, 80, 80);
    previewBg->setPosition({cx - 40, content.height - 95.f - 40});
    m_galleryTab->addChild(previewBg);

    m_selectedLabel = CCLabelBMFont::create("No pet selected", "bigFont.fnt");
    m_selectedLabel->setScale(0.25f);
    m_selectedLabel->setPosition({cx, content.height - 145.f});
    m_galleryTab->addChild(m_selectedLabel);

    // gallery scroll area
    m_galleryContainer = CCNode::create();
    m_galleryContainer->setID("gallery-container"_spr);
    m_galleryContainer->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryContainer);

    // gallery menu
    m_galleryMenu = CCMenu::create();
    m_galleryMenu->setID("gallery-menu"_spr);
    m_galleryMenu->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryMenu, 10);

    // add button
    auto addSpr = ButtonSprite::create("+ Add", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    addSpr->setScale(0.55f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(PetConfigPopup::onAddImage));
    addBtn->setPosition({cx - 90.f, 25.f});
    m_galleryMenu->addChild(addBtn);

    // shop button
    auto shopSpr = ButtonSprite::create("Shop", "goldFont.fnt", "GJ_button_02.png", 0.7f);
    shopSpr->setScale(0.55f);
    auto shopBtn = CCMenuItemSpriteExtra::create(shopSpr, this, menu_selector(PetConfigPopup::onOpenShop));
    shopBtn->setPosition({cx - 15.f, 25.f});
    m_galleryMenu->addChild(shopBtn);

    // delete all button
    auto delAllSpr = ButtonSprite::create("Delete All", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    delAllSpr->setScale(0.55f);
    auto delAllBtn = CCMenuItemSpriteExtra::create(delAllSpr, this, menu_selector(PetConfigPopup::onDeleteAllImages));
    delAllBtn->setPosition({cx + 75.f, 25.f});
    m_galleryMenu->addChild(delAllBtn);

    refreshGallery();
}

void PetConfigPopup::refreshGallery() {
    // clear old gallery items (keep addBtn)
    // remove non-button gallery children
    if (m_galleryContainer) {
        m_galleryContainer->removeAllChildren();
    }

    // remove old gallery buttons from menu (but not the add button)
    auto toRemove = std::vector<CCNode*>();
    if (m_galleryMenu && m_galleryMenu->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(m_galleryMenu->getChildren())) {
            if (child->getTag() >= 100) toRemove.push_back(child);
        }
    }
    for (auto* n : toRemove) n->removeFromParent();

    auto& pet = PetManager::get();
    auto images = pet.getGalleryImages();
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    float startX = 35.f;
    float startY = content.height - 175.f;
    float cellSize = 48.f;
    float padding = 6.f;
    int cols = static_cast<int>((content.width - 30.f) / (cellSize + padding));
    if (cols < 1) cols = 1;

    for (int i = 0; i < (int)images.size(); i++) {
        float col = static_cast<float>(i % cols);
        float row = static_cast<float>(i / cols);
        float x = startX + col * (cellSize + padding) + cellSize / 2.f;
        float y = startY - row * (cellSize + padding);

        // background
        bool isSelected = (images[i] == pet.config().selectedImage);
        auto bg = paimon::SpriteHelper::createColorPanel(
            cellSize, cellSize,
            isSelected ? ccc3(0, 200, 0) : ccc3(50, 50, 50),
            isSelected ? 180 : 100);
        bg->setPosition({x - cellSize / 2, y - cellSize / 2});
        m_galleryContainer->addChild(bg);

        // thumbnail
        auto tex = pet.loadGalleryThumb(images[i]);
        if (tex) {
            auto thumbSpr = CCSprite::createWithTexture(tex);
            if (thumbSpr) {
                float maxDim = std::max(thumbSpr->getContentSize().width, thumbSpr->getContentSize().height);
                if (maxDim > 0) thumbSpr->setScale((cellSize - 8.f) / maxDim);
                thumbSpr->setPosition({x, y});
                m_galleryContainer->addChild(thumbSpr, 1);

                // GIF badge for animated images
                auto imgPath = pet.galleryDir() / images[i];
                if (ImageLoadHelper::isAnimatedImage(imgPath)) {
                    auto* gifLabel = CCLabelBMFont::create("GIF", "bigFont.fnt");
                    if (gifLabel) {
                        gifLabel->setScale(0.25f);
                        gifLabel->setOpacity(200);
                        gifLabel->setColor({255, 100, 100});
                        gifLabel->setPosition({x + cellSize / 2.f - 8.f, y - cellSize / 2.f + 6.f});
                        m_galleryContainer->addChild(gifLabel, 2);
                    }
                }
            }
            tex->release();
        }

        // select button (invisible overlay)
        auto selectArea = CCSprite::create();
        selectArea->setContentSize({cellSize, cellSize});
        selectArea->setOpacity(0);
        auto selectBtn = CCMenuItemSpriteExtra::create(selectArea, this, menu_selector(PetConfigPopup::onSelectImage));
        selectBtn->setContentSize({cellSize, cellSize});
        selectBtn->setPosition({x, y});
        selectBtn->setTag(100 + i);
        // store filename as user data
        auto* nameStr = CCString::create(images[i]);
        selectBtn->setUserObject(nameStr);
        m_galleryMenu->addChild(selectBtn);

        // delete btn (small X)
        auto xSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        if (xSpr) {
            xSpr->setScale(0.35f);
            auto xBtn = CCMenuItemSpriteExtra::create(xSpr, this, menu_selector(PetConfigPopup::onDeleteImage));
            xBtn->setPosition({x + cellSize / 2.f - 5.f, y + cellSize / 2.f - 5.f});
            xBtn->setTag(500 + i);
            auto* nameStr2 = CCString::create(images[i]);
            xBtn->setUserObject(nameStr2);
            m_galleryMenu->addChild(xBtn);
        }
    }

    // update preview
    auto& cfg = pet.config();
    if (!cfg.selectedImage.empty()) {
        // remove old preview
        if (m_previewSprite) {
            m_previewSprite->removeFromParent();
            m_previewSprite = nullptr;
        }
        auto tex = pet.loadGalleryThumb(cfg.selectedImage);
        if (tex) {
            m_previewSprite = CCSprite::createWithTexture(tex);
            if (m_previewSprite) {
                float maxDim = std::max(m_previewSprite->getContentSize().width, m_previewSprite->getContentSize().height);
                if (maxDim > 0) m_previewSprite->setScale(70.f / maxDim);
                m_previewSprite->setPosition({cx, content.height - 95.f});
                m_galleryTab->addChild(m_previewSprite, 5);
            }
            tex->release();
        }
        m_selectedLabel->setString(cfg.selectedImage.c_str());
    } else {
        if (m_previewSprite) {
            m_previewSprite->removeFromParent();
            m_previewSprite = nullptr;
        }
        m_selectedLabel->setString("No pet selected");
    }
}

void PetConfigPopup::onAddImage(CCObject*) {
    WeakRef<PetConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto filename = PetManager::get().addToGallery(*pathOpt);
        if (!filename.empty()) {
            PaimonNotify::create("Image added to gallery!", NotificationIcon::Success)->show();
            // auto-select if no image selected
            if (PetManager::get().config().selectedImage.empty()) {
                PetManager::get().setImage(filename);
            }
            popup->refreshGallery();
        } else {
            PaimonNotify::create("Failed to add image", NotificationIcon::Error)->show();
        }
    });
}

void PetConfigPopup::onDeleteImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();

    WeakRef<PetConfigPopup> self = this;
    geode::createQuickPopup(
        "Delete Pet",
        "Are you sure you want to <cr>delete</c> this pet image?\n<cy>" + filename + "</c>",
        "Cancel", "Delete",
        [self, filename](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;
            PetManager::get().removeFromGallery(filename);
            PaimonNotify::create("Image removed", NotificationIcon::Info)->show();
            static_cast<PetConfigPopup*>(popup.data())->refreshGallery();
        }
    );
}

void PetConfigPopup::onDeleteAllImages(CCObject*) {
    auto images = PetManager::get().getGalleryImages();
    if (images.empty()) {
        PaimonNotify::create("Gallery is already empty", NotificationIcon::Info)->show();
        return;
    }

    std::string msg = fmt::format(
        "Are you sure you want to <cr>delete ALL</c> {} pet images?\nThis cannot be undone!",
        images.size()
    );

    WeakRef<PetConfigPopup> self = this;
    geode::createQuickPopup(
        "Delete All Pets",
        msg,
        "Cancel", "Delete All",
        [self](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;

            // also clean up any invalid images
            int cleaned = PetManager::get().cleanupInvalidImages();

            PetManager::get().removeAllFromGallery();

            std::string note = "All pet images deleted!";
            if (cleaned > 0) {
                note += fmt::format(" ({} corrupted files removed)", cleaned);
            }
            PaimonNotify::create(note, NotificationIcon::Success)->show();
            static_cast<PetConfigPopup*>(popup.data())->refreshGallery();
        }
    );
}

void PetConfigPopup::onSelectImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();
    PetManager::get().setImage(filename);
    PaimonNotify::create("Pet image set!", NotificationIcon::Success)->show();
    refreshGallery();
}

// ════════════════════════════════════════════════════════════
// settings tab
// ════════════════════════════════════════════════════════════

void PetConfigPopup::buildSettingsTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 16.f;
    float scrollH = content.height - 52.f;
    float totalH = 920.f;

    m_scrollLayer = ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({8.f, 8.f});
    m_settingsTab->addChild(m_scrollLayer, 5);

    CCNode* sc = m_scrollLayer->m_contentLayer;
    sc->setContentSize({scrollW, totalH});

    auto scrollContent = CCLayer::create();
    scrollContent->setContentSize({scrollW, totalH});
    sc->addChild(scrollContent);
    sc = scrollContent;

    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    scrollContent->addChild(navMenu, 10);

    float cx = scrollW / 2.f;
    float y = totalH - 8.f;

    auto& cfg = PetManager::get().config();

    // helpers
    auto addTitle = [&](char const* text, char const* info = nullptr) {
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.4f);
        label->setPosition({cx, y});
        sc->addChild(label);

        if (info) {
            auto btn = PaimonInfo::createInfoBtn(text, info, this, 0.45f);
            if (btn) {
                float halfW = label->getContentSize().width * 0.4f / 2.f;
                btn->setPosition({cx + halfW + 10.f, y});
                navMenu->addChild(btn);
            }
        }
    };

    auto addSlider = [&](Slider*& slider, CCLabelBMFont*& label,
                         float val, float minV, float maxV,
                         SEL_MenuHandler cb, char const* fmt_str) {
        float norm = (maxV > minV) ? (val - minV) / (maxV - minV) : 0.f;
        slider = Slider::create(this, cb, 0.65f);
        slider->setPosition({cx, y});
        slider->setValue(norm);
        sc->addChild(slider);

        label = CCLabelBMFont::create(fmt::format(fmt::runtime(fmt_str), val).c_str(), "bigFont.fnt");
        label->setScale(0.35f);
        label->setPosition({cx + 95.f, y});
        sc->addChild(label);
    };

    auto addToggle = [&](char const* text, CCMenuItemToggler*& toggle,
                         bool value, SEL_MenuHandler cb, char const* info = nullptr) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        sc->addChild(lbl);

        if (info) {
            auto iBtn = PaimonInfo::createInfoBtn(text, info, this, 0.35f);
            if (iBtn) {
                float lblW = lbl->getContentSize().width * 0.35f;
                iBtn->setPosition({cx - 85.f + lblW + 7.f, y});
                navMenu->addChild(iBtn);
            }
        }

        toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.35f);
        toggle->setPosition({cx + 85.f, y});
        toggle->toggle(value);
        navMenu->addChild(toggle);
    };

    // ── Enable ──
    addTitle("General");
    y -= 18.f;
    addToggle("Enable Pet", m_enableToggle, cfg.enabled,
        menu_selector(PetConfigPopup::onEnableToggled),
        "Turns the pet mascot <cg>ON</c> or <cr>OFF</c>.\nWhen enabled, the pet sprite follows your cursor across all screens.");
    y -= 22.f;

    // ── Size & Movement ──
    addTitle("Size & Movement",
        "<cy>Scale</c>: size of the pet (0.1 = tiny, 3.0 = huge).\n"
        "<cy>Sensitivity</c>: how quickly the pet follows the cursor. Low = lazy, high = snappy.\n"
        "<cy>Opacity</c>: transparency of the pet (0 = invisible, 255 = fully opaque).");
    y -= 16.f;
    addSlider(m_scaleSlider, m_scaleLabel, cfg.scale, 0.1f, 3.f,
        menu_selector(PetConfigPopup::onScaleChanged), "{:.2f}");
    y -= 18.f;
    addSlider(m_sensitivitySlider, m_sensitivityLabel, cfg.sensitivity, 0.01f, 1.f,
        menu_selector(PetConfigPopup::onSensitivityChanged), "{:.2f}");
    y -= 18.f;
    addSlider(m_opacitySlider, m_opacityLabel, static_cast<float>(cfg.opacity), 0.f, 255.f,
        menu_selector(PetConfigPopup::onOpacityChanged), "{:.0f}");
    y -= 22.f;

    // ── Offset ──
    addTitle("Cursor Offset",
        "Offsets the pet position relative to the cursor.\n"
        "<cy>X</c>: horizontal offset (negative = left, positive = right).\n"
        "<cy>Y</c>: vertical offset (positive = above cursor).");
    y -= 16.f;
    addSlider(m_offsetXSlider, m_offsetXLabel, cfg.offsetX, -50.f, 50.f,
        menu_selector(PetConfigPopup::onOffsetXChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_offsetYSlider, m_offsetYLabel, cfg.offsetY, -50.f, 100.f,
        menu_selector(PetConfigPopup::onOffsetYChanged), "{:.0f}");
    y -= 22.f;

    // ── Bounce ──
    addTitle("Bounce & Animation",
        "Makes the pet bounce up and down as it moves.\n"
        "<cy>Height</c>: how high the bounce goes (pixels).\n"
        "<cy>Speed</c>: how fast it bounces (cycles per second).");
    y -= 16.f;
    addToggle("Bounce", m_bounceToggle, cfg.bounce,
        menu_selector(PetConfigPopup::onBounceToggled),
        "Enables a bouncing motion while the pet follows the cursor.");
    y -= 18.f;
    addSlider(m_bounceHeightSlider, m_bounceHeightLabel, cfg.bounceHeight, 0.f, 20.f,
        menu_selector(PetConfigPopup::onBounceHeightChanged), "{:.1f}");
    y -= 18.f;
    addSlider(m_bounceSpeedSlider, m_bounceSpeedLabel, cfg.bounceSpeed, 0.5f, 10.f,
        menu_selector(PetConfigPopup::onBounceSpeedChanged), "{:.1f}");
    y -= 22.f;

    // ── Idle Animation ──
    addTitle("Idle Animation",
        "Subtle breathing animation when the pet is idle (cursor not moving).\n"
        "<cy>Breath Scale</c>: how much the pet grows/shrinks.\n"
        "<cy>Breath Speed</c>: how fast the breathing cycles.");
    y -= 16.f;
    addToggle("Idle Breathing", m_idleToggle, cfg.idleAnimation,
        menu_selector(PetConfigPopup::onIdleToggled),
        "Enables a gentle breathing animation when idle.\nThe pet scales slightly up and down to look alive.");
    y -= 18.f;
    addSlider(m_breathScaleSlider, m_breathScaleLabel, cfg.idleBreathScale, 0.f, 0.15f,
        menu_selector(PetConfigPopup::onBreathScaleChanged), "{:.3f}");
    y -= 18.f;
    addSlider(m_breathSpeedSlider, m_breathSpeedLabel, cfg.idleBreathSpeed, 0.5f, 5.f,
        menu_selector(PetConfigPopup::onBreathSpeedChanged), "{:.1f}");
    y -= 22.f;

    // ── Rotation ──
    addTitle("Tilt & Rotation",
        "Controls how the pet tilts when moving.\n"
        "<cy>Flip on Direction</c>: mirrors the pet horizontally based on movement direction.\n"
        "<cy>Rotation Damping</c>: how smoothly the tilt returns to center.\n"
        "<cy>Max Tilt</c>: maximum rotation angle in degrees.");
    y -= 16.f;
    addToggle("Flip on Direction", m_flipToggle, cfg.flipOnDirection,
        menu_selector(PetConfigPopup::onFlipToggled),
        "Flips the pet sprite horizontally when it changes direction.\nMakes the pet always face the direction it's moving.");
    y -= 18.f;
    addSlider(m_rotDampSlider, m_rotDampLabel, cfg.rotationDamping, 0.f, 1.f,
        menu_selector(PetConfigPopup::onRotDampChanged), "{:.2f}");
    y -= 18.f;
    addSlider(m_maxTiltSlider, m_maxTiltLabel, cfg.maxTilt, 0.f, 45.f,
        menu_selector(PetConfigPopup::onMaxTiltChanged), "{:.0f}");
    y -= 22.f;

    // ── Trail ──
    addTitle("Trail Effect",
        "Leaves a fading trail behind the pet as it moves.\n"
        "<cy>Length</c>: how long the trail persists.\n"
        "<cy>Width</c>: thickness of the trail.");
    y -= 16.f;
    addToggle("Show Trail", m_trailToggle, cfg.showTrail,
        menu_selector(PetConfigPopup::onTrailToggled),
        "Displays a glowing motion trail behind the pet as it follows the cursor.");
    y -= 18.f;
    addSlider(m_trailLengthSlider, m_trailLengthLabel, cfg.trailLength, 5.f, 100.f,
        menu_selector(PetConfigPopup::onTrailLengthChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_trailWidthSlider, m_trailWidthLabel, cfg.trailWidth, 1.f, 20.f,
        menu_selector(PetConfigPopup::onTrailWidthChanged), "{:.1f}");
    y -= 22.f;

    // ── Squish ──
    addTitle("Squish on Land",
        "When the pet stops moving, it briefly squishes (flattens) like landing.\n"
        "<cy>Amount</c>: how much it squishes (0 = none, 0.5 = extreme).");
    y -= 16.f;
    addToggle("Squish Effect", m_squishToggle, cfg.squishOnLand,
        menu_selector(PetConfigPopup::onSquishToggled),
        "Adds a cartoon-like squash & stretch when the pet stops moving.\nMakes the pet feel more alive and bouncy.");
    y -= 18.f;
    addSlider(m_squishSlider, m_squishLabel, cfg.squishAmount, 0.f, 0.5f,
        menu_selector(PetConfigPopup::onSquishChanged), "{:.2f}");
    y -= 26.f;

    // ── Visible Layers ──
    addTitle("Visible Layers",
        "Choose where the pet appears.\n"
        "<cg>All Layers</c> shows it on all non-gameplay screens.\n"
        "<cg>Show In Gameplay</c> controls gameplay screens.\n"
        "<cg>Custom</c> opens the individual layer list.");
    y -= 16.f;

    // All layers toggle
    addToggle("All Layers", m_allLayersToggle, cfg.allLayers,
        menu_selector(PetConfigPopup::onAllLayersToggled),
        "Shows the pet on all non-gameplay screens.\n"
        "It does <cr>not</c> include gameplay screens.");
    y -= 18.f;

    addToggle("Show In Gameplay", m_showInGameplayToggle, cfg.showInGameplay,
        menu_selector(PetConfigPopup::onShowInGameplayToggled),
        "Controls gameplay-related layers like <cy>PlayLayer</c>, <cy>PauseLayer</c> and end-of-level screens.\n"
        "When disabled, the pet never appears during a run.");
    y -= 18.f;

    auto customLbl = CCLabelBMFont::create("Custom", "bigFont.fnt");
    customLbl->setScale(0.35f);
    customLbl->setAnchorPoint({0.f, 0.5f});
    customLbl->setPosition({cx - 85.f, y});
    sc->addChild(customLbl);

    if (auto customInfo = PaimonInfo::createInfoBtn(
            "Custom Layers",
            "Opens the individual non-gameplay layer list so you can choose exactly where the pet appears.\n"
            "Gameplay is controlled separately by <cg>Show In Gameplay</c>.",
            this, 0.35f)) {
        float lblW = customLbl->getContentSize().width * 0.35f;
        customInfo->setPosition({cx - 85.f + lblW + 7.f, y});
        navMenu->addChild(customInfo);
    }

    auto customSpr = ButtonSprite::create("Custom", "goldFont.fnt", "GJ_button_04.png", 0.6f);
    customSpr->setScale(0.45f);
    auto customBtn = CCMenuItemSpriteExtra::create(
        customSpr, this, menu_selector(PetConfigPopup::onOpenLayerPicker));
    customBtn->setPosition({cx + 85.f, y});
    navMenu->addChild(customBtn);
    y -= 18.f;

    refreshVisibleLayerControls();
    m_scrollLayer->moveToTop();

    // scroll indicator arrow
    auto scrollArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (scrollArrow) {
        scrollArrow->setRotation(-90.f);
        scrollArrow->setScale(0.3f);
        scrollArrow->setOpacity(150);
        scrollArrow->setPosition({content.width / 2.f, 16.f});
        m_scrollArrowBasePos = ccp(content.width / 2.f, 16.f);
        scrollArrow->setID("pet-scroll-arrow"_spr);
        m_settingsTab->addChild(scrollArrow, 20);

        auto bounce = CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr));
        scrollArrow->runAction(bounce);
        m_scrollArrow = scrollArrow;
        this->unschedule(schedule_selector(PetConfigPopup::checkScrollPosition));
        this->schedule(schedule_selector(PetConfigPopup::checkScrollPosition), 0.2f);
    }
}

void PetConfigPopup::checkScrollPosition(float dt) {
    if (!m_scrollArrow || !m_scrollLayer) return;
    float totalH = m_scrollLayer->m_contentLayer->getContentSize().height;
    float viewH = m_scrollLayer->getContentSize().height;
    float curY = m_scrollLayer->m_contentLayer->getPositionY();
    bool nearBottom = (curY <= -(totalH - viewH) + 20.f);

    if (nearBottom && m_scrollArrow->getOpacity() > 0) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->setPosition(m_scrollArrowBasePos);
        m_scrollArrow->runAction(CCFadeTo::create(0.3f, 0));
    } else if (!nearBottom && m_scrollArrow->getOpacity() < 150) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->setPosition(m_scrollArrowBasePos);
        m_scrollArrow->runAction(CCFadeTo::create(0.3f, 150));
        m_scrollArrow->runAction(CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr)));
    }
}

// ════════════════════════════════════════════════════════════
// apply live
// ════════════════════════════════════════════════════════════

void PetConfigPopup::applyLive() {
    auto& pet = PetManager::get();
    pet.applyConfigLive();

    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (pet.config().enabled && scene) {
        // attachToScene is idempotent: if already on this scene
        // it refreshes visibility; otherwise it reattaches.
        pet.attachToScene(scene);
    } else {
        pet.detachFromScene();
    }

}

void PetConfigPopup::refreshIconStateLabels() {
    static const char* stateNames[] = {"Idle", "Walk", "Sleep", "React"};
    static const PetIconState stateEnums[] = {
        PetIconState::Idle,
        PetIconState::Walk,
        PetIconState::Sleep,
        PetIconState::React,
    };

    if (!m_advancedScroll || !m_advancedScroll->m_contentLayer) return;

    auto* sc = m_advancedScroll->m_contentLayer;
    for (int i = 0; i < 4; ++i) {
        CCLabelBMFont* lbl = nullptr;
        if (auto* direct = sc->getChildByTag(600 + i)) {
            lbl = typeinfo_cast<CCLabelBMFont*>(direct);
        }
        if (!lbl) {
            for (auto* child : CCArrayExt<CCNode*>(sc->getChildren())) {
                if (!child) continue;
                if (auto* nested = child->getChildByTag(600 + i)) {
                    lbl = typeinfo_cast<CCLabelBMFont*>(nested);
                    if (lbl) break;
                }
            }
        }
        if (!lbl) continue;

        std::string currentImg = PetManager::get().getIconStateImage(stateEnums[i]);
        std::string displayText = fmt::format("{}: {}", stateNames[i],
            currentImg.empty() ? "(default)" : currentImg);
        lbl->setString(displayText.c_str());
    }
}

// ════════════════════════════════════════════════════════════
// slider callbacks (all follow same pattern: read -> map -> store -> apply)
// ════════════════════════════════════════════════════════════

// slider helpers: read slider -> map to range -> store in config -> update label -> apply
static float readSlider(Slider* s, float minV, float maxV) {
    float v = s->getThumb()->getValue();
    return minV + v * (maxV - minV);
}

void PetConfigPopup::onScaleChanged(CCObject*) {
    if (!m_scaleSlider) return;
    auto& c = PetManager::get().config();
    c.scale = readSlider(m_scaleSlider, 0.1f, 3.f);
    if (m_scaleLabel) m_scaleLabel->setString(fmt::format("{:.2f}", c.scale).c_str());
    applyLive();
}
void PetConfigPopup::onSensitivityChanged(CCObject*) {
    if (!m_sensitivitySlider) return;
    auto& c = PetManager::get().config();
    c.sensitivity = readSlider(m_sensitivitySlider, 0.01f, 1.f);
    if (m_sensitivityLabel) m_sensitivityLabel->setString(fmt::format("{:.2f}", c.sensitivity).c_str());
    applyLive();
}
void PetConfigPopup::onBounceHeightChanged(CCObject*) {
    if (!m_bounceHeightSlider) return;
    auto& c = PetManager::get().config();
    c.bounceHeight = readSlider(m_bounceHeightSlider, 0.f, 20.f);
    if (m_bounceHeightLabel) m_bounceHeightLabel->setString(fmt::format("{:.1f}", c.bounceHeight).c_str());
    applyLive();
}
void PetConfigPopup::onBounceSpeedChanged(CCObject*) {
    if (!m_bounceSpeedSlider) return;
    auto& c = PetManager::get().config();
    c.bounceSpeed = readSlider(m_bounceSpeedSlider, 0.5f, 10.f);
    if (m_bounceSpeedLabel) m_bounceSpeedLabel->setString(fmt::format("{:.1f}", c.bounceSpeed).c_str());
    applyLive();
}
void PetConfigPopup::onRotDampChanged(CCObject*) {
    if (!m_rotDampSlider) return;
    auto& c = PetManager::get().config();
    c.rotationDamping = readSlider(m_rotDampSlider, 0.f, 1.f);
    if (m_rotDampLabel) m_rotDampLabel->setString(fmt::format("{:.2f}", c.rotationDamping).c_str());
    applyLive();
}
void PetConfigPopup::onMaxTiltChanged(CCObject*) {
    if (!m_maxTiltSlider) return;
    auto& c = PetManager::get().config();
    c.maxTilt = readSlider(m_maxTiltSlider, 0.f, 45.f);
    if (m_maxTiltLabel) m_maxTiltLabel->setString(fmt::format("{:.0f}", c.maxTilt).c_str());
    applyLive();
}
void PetConfigPopup::onTrailLengthChanged(CCObject*) {
    if (!m_trailLengthSlider) return;
    auto& c = PetManager::get().config();
    c.trailLength = readSlider(m_trailLengthSlider, 5.f, 100.f);
    if (m_trailLengthLabel) m_trailLengthLabel->setString(fmt::format("{:.0f}", c.trailLength).c_str());
    applyLive();
}
void PetConfigPopup::onTrailWidthChanged(CCObject*) {
    if (!m_trailWidthSlider) return;
    auto& c = PetManager::get().config();
    c.trailWidth = readSlider(m_trailWidthSlider, 1.f, 20.f);
    if (m_trailWidthLabel) m_trailWidthLabel->setString(fmt::format("{:.1f}", c.trailWidth).c_str());
    applyLive();
}
void PetConfigPopup::onBreathScaleChanged(CCObject*) {
    if (!m_breathScaleSlider) return;
    auto& c = PetManager::get().config();
    c.idleBreathScale = readSlider(m_breathScaleSlider, 0.f, 0.15f);
    if (m_breathScaleLabel) m_breathScaleLabel->setString(fmt::format("{:.3f}", c.idleBreathScale).c_str());
    applyLive();
}
void PetConfigPopup::onBreathSpeedChanged(CCObject*) {
    if (!m_breathSpeedSlider) return;
    auto& c = PetManager::get().config();
    c.idleBreathSpeed = readSlider(m_breathSpeedSlider, 0.5f, 5.f);
    if (m_breathSpeedLabel) m_breathSpeedLabel->setString(fmt::format("{:.1f}", c.idleBreathSpeed).c_str());
    applyLive();
}
void PetConfigPopup::onSquishChanged(CCObject*) {
    if (!m_squishSlider) return;
    auto& c = PetManager::get().config();
    c.squishAmount = readSlider(m_squishSlider, 0.f, 0.5f);
    if (m_squishLabel) m_squishLabel->setString(fmt::format("{:.2f}", c.squishAmount).c_str());
    applyLive();
}

void PetConfigPopup::onOpacityChanged(CCObject*) {
    if (!m_opacitySlider) return;
    float v = m_opacitySlider->getThumb()->getValue();
    auto& cfg = PetManager::get().config();
    cfg.opacity = static_cast<int>(v * 255.f);
    cfg.opacity = std::max(0, std::min(255, cfg.opacity));
    if (m_opacityLabel) m_opacityLabel->setString(fmt::format("{}", cfg.opacity).c_str());
    applyLive();
}

void PetConfigPopup::onOffsetXChanged(CCObject*) {
    if (!m_offsetXSlider) return;
    float v = m_offsetXSlider->getThumb()->getValue();
    auto& cfg = PetManager::get().config();
    cfg.offsetX = -50.f + v * 100.f;
    if (m_offsetXLabel) m_offsetXLabel->setString(fmt::format("{:.0f}", cfg.offsetX).c_str());
    applyLive();
}

void PetConfigPopup::onOffsetYChanged(CCObject*) {
    if (!m_offsetYSlider) return;
    float v = m_offsetYSlider->getThumb()->getValue();
    auto& cfg = PetManager::get().config();
    cfg.offsetY = -50.f + v * 150.f;
    if (m_offsetYLabel) m_offsetYLabel->setString(fmt::format("{:.0f}", cfg.offsetY).c_str());
    applyLive();
}

// ════════════════════════════════════════════════════════════
// toggle callbacks
// ════════════════════════════════════════════════════════════

void PetConfigPopup::onEnableToggled(CCObject*) {
    PetManager::get().config().enabled = !m_enableToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onFlipToggled(CCObject*) {
    PetManager::get().config().flipOnDirection = !m_flipToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onTrailToggled(CCObject*) {
    PetManager::get().config().showTrail = !m_trailToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onIdleToggled(CCObject*) {
    PetManager::get().config().idleAnimation = !m_idleToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onBounceToggled(CCObject*) {
    PetManager::get().config().bounce = !m_bounceToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onSquishToggled(CCObject*) {
    PetManager::get().config().squishOnLand = !m_squishToggle->isToggled();
    applyLive();
}

void PetConfigPopup::refreshVisibleLayerControls() {
    auto& cfg = PetManager::get().config();

    if (m_allLayersToggle) {
        m_allLayersToggle->toggle(cfg.allLayers);
    }
    if (m_showInGameplayToggle) {
        m_showInGameplayToggle->toggle(cfg.showInGameplay);
    }
}

void PetConfigPopup::onLayerToggled(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    auto* nameStr = typeinfo_cast<CCString*>(toggle->getUserObject());
    if (!nameStr) return;

    std::string layerName = nameStr->getCString();
    bool nowOn = !toggle->isToggled(); // before toggle flips

    auto& layers = PetManager::get().config().visibleLayers;
    if (nowOn) {
        layers.insert(layerName);
    } else {
        layers.erase(layerName);
    }

    refreshVisibleLayerControls();
    applyLive();
}

void PetConfigPopup::onAllLayersToggled(CCObject*) {
    if (!m_allLayersToggle) return;
    auto& cfg = PetManager::get().config();
    cfg.allLayers = !m_allLayersToggle->isToggled();

    if (!cfg.allLayers && allNonGameplayLayersSelected(cfg.visibleLayers)) {
        for (auto const& layerName : PET_LAYER_OPTIONS) {
            if (isPetGameplayLayer(layerName)) continue;
            cfg.visibleLayers.erase(layerName);
        }
    }

    refreshVisibleLayerControls();
    applyLive();
}

void PetConfigPopup::onShowInGameplayToggled(CCObject*) {
    if (!m_showInGameplayToggle) return;
    PetManager::get().config().showInGameplay = !m_showInGameplayToggle->isToggled();
    refreshVisibleLayerControls();
    applyLive();
}

void PetConfigPopup::onOpenLayerPicker(CCObject*) {
    auto popup = PetLayerPickerPopup::create(this);
    if (popup) popup->show();
}

void PetConfigPopup::onOpenShop(CCObject*) {
    auto shop = PaimonShopPopup::create();
    if (shop) shop->show();
}

// ════════════════════════════════════════════════════════════
// advanced tab
// ════════════════════════════════════════════════════════════

void PetConfigPopup::buildAdvancedTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 16.f;
    float scrollH = content.height - 52.f;
    float totalH = 1600.f;

    m_advancedScroll = ScrollLayer::create({scrollW, scrollH});
    m_advancedScroll->setPosition({8.f, 8.f});
    m_advancedTab->addChild(m_advancedScroll, 5);

    CCNode* sc = m_advancedScroll->m_contentLayer;
    sc->setContentSize({scrollW, totalH});

    auto scrollContent = CCLayer::create();
    scrollContent->setContentSize({scrollW, totalH});
    sc->addChild(scrollContent);
    sc = scrollContent;

    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    scrollContent->addChild(navMenu, 10);

    float cx = scrollW / 2.f;
    float y = totalH - 8.f;

    auto& cfg = PetManager::get().config();

    // helpers (same pattern as settings tab)
    auto addTitle = [&](char const* text, char const* info = nullptr) {
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.4f);
        label->setPosition({cx, y});
        sc->addChild(label);

        if (info) {
            auto btn = PaimonInfo::createInfoBtn(text, info, this, 0.45f);
            if (btn) {
                float halfW = label->getContentSize().width * 0.4f / 2.f;
                btn->setPosition({cx + halfW + 10.f, y});
                navMenu->addChild(btn);
            }
        }
    };

    auto addSlider = [&](Slider*& slider, CCLabelBMFont*& label,
                         float val, float minV, float maxV,
                         SEL_MenuHandler cb, char const* fmt_str) {
        float norm = (maxV > minV) ? (val - minV) / (maxV - minV) : 0.f;
        slider = Slider::create(this, cb, 0.65f);
        slider->setPosition({cx, y});
        slider->setValue(norm);
        sc->addChild(slider);

        label = CCLabelBMFont::create(fmt::format(fmt::runtime(fmt_str), val).c_str(), "bigFont.fnt");
        label->setScale(0.22f);
        label->setPosition({cx + 78.f, y});
        sc->addChild(label);
    };

    auto addToggle = [&](char const* text, CCMenuItemToggler*& toggle,
                         bool value, SEL_MenuHandler cb, char const* info = nullptr) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        sc->addChild(lbl);

        toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.35f);
        toggle->setPosition({cx + 85.f, y});
        toggle->toggle(value);
        navMenu->addChild(toggle);
    };

    // ── Pet Icon States ──
    addTitle("Pet Icon States",
        "Set different images for each pet state!\n"
        "If a state image is empty, the <cy>default</c> image is used.\n"
        "<cy>Idle</c>: standing still  <cy>Walk</c>: moving\n"
        "<cy>Sleep</c>: idle too long  <cy>React</c>: game event");
    y -= 18.f;

    // icon state selector buttons
    static const char* stateNames[] = {"Idle", "Walk", "Sleep", "React"};
    static const PetIconState stateEnums[] = {PetIconState::Idle, PetIconState::Walk, PetIconState::Sleep, PetIconState::React};
    for (int i = 0; i < 4; i++) {
        std::string currentImg = PetManager::get().getIconStateImage(stateEnums[i]);
        std::string displayText = fmt::format("{}: {}", stateNames[i],
            currentImg.empty() ? "(default)" : currentImg);

        auto lbl = CCLabelBMFont::create(displayText.c_str(), "chatFont.fnt");
        lbl->setScale(0.4f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        lbl->setTag(600 + i);
        sc->addChild(lbl);

        auto btnSpr = ButtonSprite::create("Set", "bigFont.fnt", "GJ_button_01.png", 0.6f);
        btnSpr->setScale(0.5f);
        auto btn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(PetConfigPopup::onSetIconStateImage));
        btn->setPosition({cx + 85.f, y});
        btn->setTag(600 + i);
        auto* tagStr = CCString::create(std::to_string(static_cast<int>(stateEnums[i])));
        btn->setUserObject(tagStr);
        navMenu->addChild(btn);
        y -= 20.f;
    }
    y -= 8.f;

    // ── Shadow ──
    addTitle("Shadow",
        "Adds a soft shadow beneath the pet.\n"
        "Adjust <cy>offset</c>, <cy>opacity</c>, and <cy>scale</c> to customize.");
    y -= 16.f;
    addToggle("Show Shadow", m_shadowToggle, cfg.showShadow,
        menu_selector(PetConfigPopup::onShadowToggled));
    y -= 18.f;
    addSlider(m_shadowOffXSlider, m_shadowOffXLabel, cfg.shadowOffsetX, -20.f, 20.f,
        menu_selector(PetConfigPopup::onShadowOffXChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_shadowOffYSlider, m_shadowOffYLabel, cfg.shadowOffsetY, -20.f, 20.f,
        menu_selector(PetConfigPopup::onShadowOffYChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_shadowOpacitySlider, m_shadowOpacityLabel, static_cast<float>(cfg.shadowOpacity), 0.f, 200.f,
        menu_selector(PetConfigPopup::onShadowOpacityChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_shadowScaleSlider, m_shadowScaleLabel, cfg.shadowScale, 0.5f, 2.f,
        menu_selector(PetConfigPopup::onShadowScaleChanged), "{:.2f}");
    y -= 22.f;

    // ── Particles ──
    addTitle("Pet Particles",
        "Emit particles from your pet!\n"
        "Choose from <cy>Sparkles</c>, <cy>Hearts</c>, <cy>Stars</c>, <cy>Snowflakes</c>, <cy>Bubbles</c>.\n"
        "Adjust <cy>rate</c>, <cy>size</c>, <cy>gravity</c>, and <cy>lifetime</c>.");
    y -= 16.f;
    addToggle("Show Particles", m_particleToggle, cfg.showParticles,
        menu_selector(PetConfigPopup::onParticleToggled));
    y -= 18.f;

    // particle type selector
    {
        auto typeLbl = CCLabelBMFont::create("Type:", "bigFont.fnt");
        typeLbl->setScale(0.35f);
        typeLbl->setAnchorPoint({0.f, 0.5f});
        typeLbl->setPosition({cx - 85.f, y});
        sc->addChild(typeLbl);

        static const char* pTypeNames[] = {"Sparkle", "Heart", "Star", "Snow", "Bubble"};
        float btnX = cx - 30.f;
        for (int i = 0; i < 5; i++) {
            auto bSpr = ButtonSprite::create(pTypeNames[i], "bigFont.fnt",
                i == cfg.particleType ? "GJ_button_02.png" : "GJ_button_04.png", 0.5f);
            bSpr->setScale(0.32f);
            auto bBtn = CCMenuItemSpriteExtra::create(bSpr, this, menu_selector(PetConfigPopup::onParticleTypeChanged));
            bBtn->setPosition({btnX, y});
            bBtn->setTag(700 + i);
            navMenu->addChild(bBtn);
            btnX += 28.f;
        }
        y -= 20.f;
    }

    // particle color picker
    {
        auto colorLbl = CCLabelBMFont::create("Color:", "bigFont.fnt");
        colorLbl->setScale(0.35f);
        colorLbl->setAnchorPoint({0.f, 0.5f});
        colorLbl->setPosition({cx - 85.f, y});
        sc->addChild(colorLbl);

        // color preview square
        auto colorPreview = CCLayerColor::create(ccc4(cfg.particleColor.r, cfg.particleColor.g, cfg.particleColor.b, 255), 24.f, 24.f);
        colorPreview->setPosition({cx - 25.f, y - 12.f});
        colorPreview->setID("particle-color-preview"_spr);
        sc->addChild(colorPreview);

        auto pickSpr = ButtonSprite::create("Pick", "bigFont.fnt", "GJ_button_04.png", 0.5f);
        pickSpr->setScale(0.32f);
        auto colorBtn = CCMenuItemSpriteExtra::create(
            pickSpr, this, menu_selector(PetConfigPopup::onParticleColorPicked));
        colorBtn->setPosition({cx + 30.f, y});
        navMenu->addChild(colorBtn);
        y -= 20.f;
    }


    addSlider(m_particleRateSlider, m_particleRateLabel, cfg.particleRate, 1.f, 30.f,
        menu_selector(PetConfigPopup::onParticleRateChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_particleSizeSlider, m_particleSizeLabel, cfg.particleSize, 1.f, 10.f,
        menu_selector(PetConfigPopup::onParticleSizeChanged), "{:.1f}");
    y -= 18.f;
    addSlider(m_particleGravitySlider, m_particleGravityLabel, cfg.particleGravity, -50.f, 50.f,
        menu_selector(PetConfigPopup::onParticleGravityChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_particleLifetimeSlider, m_particleLifetimeLabel, cfg.particleLifetime, 0.5f, 5.f,
        menu_selector(PetConfigPopup::onParticleLifetimeChanged), "{:.1f}");
    y -= 22.f;

    // ── Speech Bubbles ──
    addTitle("Speech Bubbles",
        "Your pet talks! Shows contextual speech bubbles.\n"
        "<cy>Idle</c>: random chatter when bored.\n"
        "<cy>Level Complete</c>: celebration messages.\n"
        "<cy>Death</c>: encouragement messages.\n"
        "<cy>Click</c>: reaction when you click the pet.");
    y -= 16.f;
    addToggle("Enable Speech", m_speechToggle, cfg.enableSpeech,
        menu_selector(PetConfigPopup::onSpeechToggled));
    y -= 18.f;
    addSlider(m_speechIntervalSlider, m_speechIntervalLabel, cfg.speechInterval, 5.f, 120.f,
        menu_selector(PetConfigPopup::onSpeechIntervalChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_speechDurationSlider, m_speechDurationLabel, cfg.speechDuration, 1.f, 10.f,
        menu_selector(PetConfigPopup::onSpeechDurationChanged), "{:.1f}");
    y -= 18.f;
    addSlider(m_speechScaleSlider, m_speechScaleLabel, cfg.speechBubbleScale, 0.2f, 1.f,
        menu_selector(PetConfigPopup::onSpeechScaleChanged), "{:.2f}");
    y -= 22.f;

    // ── Sleep Mode ──
    addTitle("Sleep Mode",
        "The pet falls asleep after being idle too long.\n"
        "Shows a <cy>Zzz</c> indicator and uses the <cy>Sleep</c> icon state.\n"
        "Moving or clicking wakes it up instantly.");
    y -= 16.f;
    addToggle("Enable Sleep", m_sleepToggle, cfg.enableSleep,
        menu_selector(PetConfigPopup::onSleepToggled));
    y -= 18.f;
    addSlider(m_sleepAfterSlider, m_sleepAfterLabel, cfg.sleepAfterSeconds, 10.f, 300.f,
        menu_selector(PetConfigPopup::onSleepAfterChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_sleepBobSlider, m_sleepBobLabel, cfg.sleepBobAmount, 0.f, 10.f,
        menu_selector(PetConfigPopup::onSleepBobChanged), "{:.1f}");
    y -= 22.f;

    // ── Click Interaction ──
    addTitle("Click Interaction",
        "Click on your pet for a reaction!\n"
        "The pet will <cy>jump</c> and show a <cy>speech bubble</c>.\n"
        "Also wakes the pet from sleep.");
    y -= 16.f;
    addToggle("Enable Click", m_clickToggle, cfg.enableClickInteraction,
        menu_selector(PetConfigPopup::onClickToggled));
    y -= 18.f;
    addSlider(m_clickDurationSlider, m_clickDurationLabel, cfg.clickReactionDuration, 0.5f, 5.f,
        menu_selector(PetConfigPopup::onClickDurationChanged), "{:.1f}");
    y -= 18.f;
    addSlider(m_clickJumpSlider, m_clickJumpLabel, cfg.clickJumpHeight, 5.f, 50.f,
        menu_selector(PetConfigPopup::onClickJumpChanged), "{:.0f}");
    y -= 22.f;

    // ── Game Event Reactions ──
    addTitle("Game Event Reactions",
        "The pet reacts to in-game events!\n"
        "<cy>Level Complete</c>: jumps and spins with celebration.\n"
        "<cy>Death</c>: shows encouragement speech.\n"
        "<cy>Practice Exit</c>: reacts when exiting practice mode.\n"
        "Uses the <cy>React</c> icon state during reactions.");
    y -= 16.f;
    addToggle("Level Complete", m_reactCompleteToggle, cfg.reactToLevelComplete,
        menu_selector(PetConfigPopup::onReactCompleteToggled));
    y -= 18.f;
    addToggle("Death", m_reactDeathToggle, cfg.reactToDeath,
        menu_selector(PetConfigPopup::onReactDeathToggled));
    y -= 18.f;
    addToggle("Practice Exit", m_reactPracticeToggle, cfg.reactToPracticeExit,
        menu_selector(PetConfigPopup::onReactPracticeToggled));
    y -= 18.f;
    addSlider(m_reactionDurationSlider, m_reactionDurationLabel, cfg.reactionDuration, 0.5f, 5.f,
        menu_selector(PetConfigPopup::onReactionDurationChanged), "{:.1f}");
    y -= 18.f;
    addSlider(m_reactionJumpSlider, m_reactionJumpLabel, cfg.reactionJumpHeight, 5.f, 60.f,
        menu_selector(PetConfigPopup::onReactionJumpChanged), "{:.0f}");
    y -= 18.f;
    addSlider(m_reactionSpinSlider, m_reactionSpinLabel, cfg.reactionSpinSpeed, 90.f, 720.f,
        menu_selector(PetConfigPopup::onReactionSpinChanged), "{:.0f}");
    y -= 26.f;

    m_advancedScroll->moveToTop();

    // scroll indicator arrow
    auto scrollArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (scrollArrow) {
        scrollArrow->setRotation(-90.f);
        scrollArrow->setScale(0.3f);
        scrollArrow->setOpacity(150);
        scrollArrow->setPosition({content.width / 2.f, 16.f});
        m_advancedArrowBasePos = ccp(content.width / 2.f, 16.f);
        scrollArrow->setID("advanced-scroll-arrow"_spr);
        m_advancedTab->addChild(scrollArrow, 20);

        auto bounce = CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr));
        scrollArrow->runAction(bounce);
        m_advancedArrow = scrollArrow;
        this->unschedule(schedule_selector(PetConfigPopup::checkAdvancedScrollPosition));
        this->schedule(schedule_selector(PetConfigPopup::checkAdvancedScrollPosition), 0.2f);
    }
}

void PetConfigPopup::checkAdvancedScrollPosition(float dt) {
    if (!m_advancedArrow || !m_advancedScroll) return;
    float totalH = m_advancedScroll->m_contentLayer->getContentSize().height;
    float viewH = m_advancedScroll->getContentSize().height;
    float curY = m_advancedScroll->m_contentLayer->getPositionY();
    bool nearBottom = (curY <= -(totalH - viewH) + 20.f);

    if (nearBottom && m_advancedArrow->getOpacity() > 0) {
        m_advancedArrow->stopAllActions();
        m_advancedArrow->setPosition(m_advancedArrowBasePos);
        m_advancedArrow->runAction(CCFadeTo::create(0.3f, 0));
    } else if (!nearBottom && m_advancedArrow->getOpacity() < 150) {
        m_advancedArrow->stopAllActions();
        m_advancedArrow->setPosition(m_advancedArrowBasePos);
        m_advancedArrow->runAction(CCFadeTo::create(0.3f, 150));
        m_advancedArrow->runAction(CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr)));
    }
}

// ════════════════════════════════════════════════════════════
// advanced toggle callbacks
// ════════════════════════════════════════════════════════════

void PetConfigPopup::onShadowToggled(CCObject*) {
    PetManager::get().config().showShadow = !m_shadowToggle->isToggled();
    applyLive();
}
void PetConfigPopup::onParticleToggled(CCObject*) {
    PetManager::get().config().showParticles = !m_particleToggle->isToggled();
    applyLive();
}
void PetConfigPopup::onParticleTypeChanged(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    int type = btn->getTag() - 700;
    PetManager::get().config().particleType = type;
    applyLive();
}
void PetConfigPopup::onParticleColorPicked(CCObject*) {
    auto& cfg = PetManager::get().config();
    auto currentColor = ccc4(cfg.particleColor.r, cfg.particleColor.g, cfg.particleColor.b, 255);

    auto* popup = geode::ColorPickPopup::create(currentColor);
    if (!popup) return;

    popup->setCallback([this](ccColor4B const& color) {
        auto& cfg = PetManager::get().config();
        cfg.particleColor = ccc3(color.r, color.g, color.b);
        applyLive();

        // update color preview
        if (auto* sc = m_advancedScroll ? m_advancedScroll->m_contentLayer : nullptr) {
            auto* preview = sc->getChildByID("particle-color-preview"_spr);
            if (!preview) {
                for (auto* child : CCArrayExt<CCNode*>(sc->getChildren())) {
                    if (!child) continue;
                    preview = child->getChildByID("particle-color-preview"_spr);
                    if (preview) break;
                }
            }
            if (preview) {
                if (auto* layerColor = typeinfo_cast<CCLayerColor*>(preview)) {
                    layerColor->setColor(cfg.particleColor);
                }
            }
        }
    });
    popup->show();
}
void PetConfigPopup::onSpeechToggled(CCObject*) {
    PetManager::get().config().enableSpeech = !m_speechToggle->isToggled();
    applyLive();
}
void PetConfigPopup::onSleepToggled(CCObject*) {
    PetManager::get().config().enableSleep = !m_sleepToggle->isToggled();
    applyLive();
}
void PetConfigPopup::onClickToggled(CCObject*) {
    PetManager::get().config().enableClickInteraction = !m_clickToggle->isToggled();
    applyLive();
}
void PetConfigPopup::onReactCompleteToggled(CCObject*) {
    PetManager::get().config().reactToLevelComplete = !m_reactCompleteToggle->isToggled();
    applyLive();
}
void PetConfigPopup::onReactDeathToggled(CCObject*) {
    PetManager::get().config().reactToDeath = !m_reactDeathToggle->isToggled();
    applyLive();
}
void PetConfigPopup::onReactPracticeToggled(CCObject*) {
    PetManager::get().config().reactToPracticeExit = !m_reactPracticeToggle->isToggled();
    applyLive();
}

void PetConfigPopup::onSetIconStateImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* tagStr = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!tagStr) return;
    int stateInt = geode::utils::numFromString<int>(tagStr->getCString()).unwrapOr(0);
    auto state = static_cast<PetIconState>(stateInt);

    WeakRef<PetConfigPopup> self = this;
    pt::pickImage([self, state](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto filename = PetManager::get().addToGallery(*pathOpt);
        if (!filename.empty()) {
            PetManager::get().setIconStateImage(state, filename);
            PaimonNotify::create("Icon state image set!", NotificationIcon::Success)->show();
            static_cast<PetConfigPopup*>(popup.data())->refreshIconStateLabels();
            static_cast<PetConfigPopup*>(popup.data())->refreshGallery();
        } else {
            PaimonNotify::create("Failed to add image", NotificationIcon::Error)->show();
        }
    });
}

// ════════════════════════════════════════════════════════════
// advanced slider callbacks
// ════════════════════════════════════════════════════════════

void PetConfigPopup::onShadowOffXChanged(CCObject*) {
    if (!m_shadowOffXSlider) return;
    auto& c = PetManager::get().config();
    c.shadowOffsetX = readSlider(m_shadowOffXSlider, -20.f, 20.f);
    if (m_shadowOffXLabel) m_shadowOffXLabel->setString(fmt::format("{:.0f}", c.shadowOffsetX).c_str());
    applyLive();
}
void PetConfigPopup::onShadowOffYChanged(CCObject*) {
    if (!m_shadowOffYSlider) return;
    auto& c = PetManager::get().config();
    c.shadowOffsetY = readSlider(m_shadowOffYSlider, -20.f, 20.f);
    if (m_shadowOffYLabel) m_shadowOffYLabel->setString(fmt::format("{:.0f}", c.shadowOffsetY).c_str());
    applyLive();
}
void PetConfigPopup::onShadowOpacityChanged(CCObject*) {
    if (!m_shadowOpacitySlider) return;
    auto& c = PetManager::get().config();
    c.shadowOpacity = static_cast<int>(readSlider(m_shadowOpacitySlider, 0.f, 200.f));
    if (m_shadowOpacityLabel) m_shadowOpacityLabel->setString(fmt::format("{}", c.shadowOpacity).c_str());
    applyLive();
}
void PetConfigPopup::onShadowScaleChanged(CCObject*) {
    if (!m_shadowScaleSlider) return;
    auto& c = PetManager::get().config();
    c.shadowScale = readSlider(m_shadowScaleSlider, 0.5f, 2.f);
    if (m_shadowScaleLabel) m_shadowScaleLabel->setString(fmt::format("{:.2f}", c.shadowScale).c_str());
    applyLive();
}
void PetConfigPopup::onParticleRateChanged(CCObject*) {
    if (!m_particleRateSlider) return;
    auto& c = PetManager::get().config();
    c.particleRate = readSlider(m_particleRateSlider, 1.f, 30.f);
    if (m_particleRateLabel) m_particleRateLabel->setString(fmt::format("{:.0f}", c.particleRate).c_str());
    applyLive();
}
void PetConfigPopup::onParticleSizeChanged(CCObject*) {
    if (!m_particleSizeSlider) return;
    auto& c = PetManager::get().config();
    c.particleSize = readSlider(m_particleSizeSlider, 1.f, 10.f);
    if (m_particleSizeLabel) m_particleSizeLabel->setString(fmt::format("{:.1f}", c.particleSize).c_str());
    applyLive();
}
void PetConfigPopup::onParticleGravityChanged(CCObject*) {
    if (!m_particleGravitySlider) return;
    auto& c = PetManager::get().config();
    c.particleGravity = readSlider(m_particleGravitySlider, -50.f, 50.f);
    if (m_particleGravityLabel) m_particleGravityLabel->setString(fmt::format("{:.0f}", c.particleGravity).c_str());
    applyLive();
}
void PetConfigPopup::onParticleLifetimeChanged(CCObject*) {
    if (!m_particleLifetimeSlider) return;
    auto& c = PetManager::get().config();
    c.particleLifetime = readSlider(m_particleLifetimeSlider, 0.5f, 5.f);
    if (m_particleLifetimeLabel) m_particleLifetimeLabel->setString(fmt::format("{:.1f}", c.particleLifetime).c_str());
    applyLive();
}
void PetConfigPopup::onSpeechIntervalChanged(CCObject*) {
    if (!m_speechIntervalSlider) return;
    auto& c = PetManager::get().config();
    c.speechInterval = readSlider(m_speechIntervalSlider, 5.f, 120.f);
    if (m_speechIntervalLabel) m_speechIntervalLabel->setString(fmt::format("{:.0f}", c.speechInterval).c_str());
    applyLive();
}
void PetConfigPopup::onSpeechDurationChanged(CCObject*) {
    if (!m_speechDurationSlider) return;
    auto& c = PetManager::get().config();
    c.speechDuration = readSlider(m_speechDurationSlider, 1.f, 10.f);
    if (m_speechDurationLabel) m_speechDurationLabel->setString(fmt::format("{:.1f}", c.speechDuration).c_str());
    applyLive();
}
void PetConfigPopup::onSpeechScaleChanged(CCObject*) {
    if (!m_speechScaleSlider) return;
    auto& c = PetManager::get().config();
    c.speechBubbleScale = readSlider(m_speechScaleSlider, 0.2f, 1.f);
    if (m_speechScaleLabel) m_speechScaleLabel->setString(fmt::format("{:.2f}", c.speechBubbleScale).c_str());
    applyLive();
}
void PetConfigPopup::onSleepAfterChanged(CCObject*) {
    if (!m_sleepAfterSlider) return;
    auto& c = PetManager::get().config();
    c.sleepAfterSeconds = readSlider(m_sleepAfterSlider, 10.f, 300.f);
    if (m_sleepAfterLabel) m_sleepAfterLabel->setString(fmt::format("{:.0f}", c.sleepAfterSeconds).c_str());
    applyLive();
}
void PetConfigPopup::onSleepBobChanged(CCObject*) {
    if (!m_sleepBobSlider) return;
    auto& c = PetManager::get().config();
    c.sleepBobAmount = readSlider(m_sleepBobSlider, 0.f, 10.f);
    if (m_sleepBobLabel) m_sleepBobLabel->setString(fmt::format("{:.1f}", c.sleepBobAmount).c_str());
    applyLive();
}
void PetConfigPopup::onClickDurationChanged(CCObject*) {
    if (!m_clickDurationSlider) return;
    auto& c = PetManager::get().config();
    c.clickReactionDuration = readSlider(m_clickDurationSlider, 0.5f, 5.f);
    if (m_clickDurationLabel) m_clickDurationLabel->setString(fmt::format("{:.1f}", c.clickReactionDuration).c_str());
    applyLive();
}
void PetConfigPopup::onClickJumpChanged(CCObject*) {
    if (!m_clickJumpSlider) return;
    auto& c = PetManager::get().config();
    c.clickJumpHeight = readSlider(m_clickJumpSlider, 5.f, 50.f);
    if (m_clickJumpLabel) m_clickJumpLabel->setString(fmt::format("{:.0f}", c.clickJumpHeight).c_str());
    applyLive();
}
void PetConfigPopup::onReactionDurationChanged(CCObject*) {
    if (!m_reactionDurationSlider) return;
    auto& c = PetManager::get().config();
    c.reactionDuration = readSlider(m_reactionDurationSlider, 0.5f, 5.f);
    if (m_reactionDurationLabel) m_reactionDurationLabel->setString(fmt::format("{:.1f}", c.reactionDuration).c_str());
    applyLive();
}
void PetConfigPopup::onReactionJumpChanged(CCObject*) {
    if (!m_reactionJumpSlider) return;
    auto& c = PetManager::get().config();
    c.reactionJumpHeight = readSlider(m_reactionJumpSlider, 5.f, 60.f);
    if (m_reactionJumpLabel) m_reactionJumpLabel->setString(fmt::format("{:.0f}", c.reactionJumpHeight).c_str());
    applyLive();
}
void PetConfigPopup::onReactionSpinChanged(CCObject*) {
    if (!m_reactionSpinSlider) return;
    auto& c = PetManager::get().config();
    c.reactionSpinSpeed = readSlider(m_reactionSpinSlider, 90.f, 720.f);
    if (m_reactionSpinLabel) m_reactionSpinLabel->setString(fmt::format("{:.0f}", c.reactionSpinSpeed).c_str());
    applyLive();
}
