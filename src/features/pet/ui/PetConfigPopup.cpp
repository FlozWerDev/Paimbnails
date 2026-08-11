#include "PetConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "PaimonShopPopup.hpp"
#include "../services/PetManager.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
namespace kit = paimon::configkit;

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
        this->setTitle("Elegir pantallas");
        this->setMouseEnabled(true);

        auto content = m_mainLayer->getContentSize();

        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(menu, 10);

        if (auto infoBtn = PaimonInfo::createInfoBtn(
                "Elegir pantallas",
                "Marca las pantallas (fuera del gameplay) donde quieres ver la mascota.\n"
                "El gameplay se controla aparte con <cg>Durante el juego</c>.",
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

        auto selectAllSpr = ButtonSprite::create("Todas", "goldFont.fnt", "GJ_button_01.png", 0.55f);
        selectAllSpr->setScale(0.45f);
        auto selectAllBtn = CCMenuItemSpriteExtra::create(
            selectAllSpr, this, menu_selector(PetLayerPickerPopup::onSelectAll));
        selectAllBtn->setPosition({content.width / 2.f - 55.f, 15.f});
        menu->addChild(selectAllBtn);

        auto clearSpr = ButtonSprite::create("Ninguna", "goldFont.fnt", "GJ_button_06.png", 0.55f);
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

constexpr int kIconStateCount = 4;
char const* kIconStateNames[kIconStateCount] = {
    "Normal", "Caminando", "Durmiendo", "Reaccionando"
};
PetIconState kIconStateEnums[kIconStateCount] = {
    PetIconState::Idle, PetIconState::Walk, PetIconState::Sleep, PetIconState::React
};

}


PetConfigPopup* PetConfigPopup::create() {
    auto ret = new PetConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}


bool PetConfigPopup::init() {
    if (!Popup::init(420.f, 290.f)) return false;

    this->setTitle("Mascota");
    this->setMouseEnabled(true);

    auto content = m_mainLayer->getContentSize();

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

    this->schedule(schedule_selector(PetConfigPopup::updateSmoothScroll));

    paimon::markDynamicPopup(this);
    return true;
}

void PetConfigPopup::onExit() {
    this->unschedule(schedule_selector(PetConfigPopup::updateSmoothScroll));
    Popup::onExit();
}

void PetConfigPopup::scrollWheel(float x, float y) {
    if (m_currentTab == 1 &&
        kit::queueWheelScroll(m_scrollLayer, x, y, m_settingsScrollTargetY, m_settingsScrollTargetSet)) return;
    if (m_currentTab == 2 &&
        kit::queueWheelScroll(m_advancedScroll, x, y, m_advancedScrollTargetY, m_advancedScrollTargetSet)) return;
}

void PetConfigPopup::updateSmoothScroll(float dt) {
    kit::stepWheelScroll(m_scrollLayer, m_settingsScrollTargetY, m_settingsScrollTargetSet, dt);
    kit::stepWheelScroll(m_advancedScroll, m_advancedScrollTargetY, m_advancedScrollTargetSet, dt);
}


void PetConfigPopup::createTabButtons() {
    auto content = m_mainLayer->getContentSize();
    float topY = content.height - 38.f;
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setID("tab-buttons-menu"_spr);
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    auto spr1 = ButtonSprite::create("Galeria");
    spr1->setScale(0.45f);
    auto tab1 = CCMenuItemSpriteExtra::create(spr1, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab1->setTag(0);
    tab1->setID("pet-gallery-tab-btn"_spr);
    tab1->setPosition({cx - 90.f, topY});
    menu->addChild(tab1);
    m_tabs.push_back(tab1);

    auto spr2 = ButtonSprite::create("Ajustes");
    spr2->setScale(0.45f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab2->setTag(1);
    tab2->setID("pet-settings-tab-btn"_spr);
    tab2->setPosition({cx, topY});
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    auto spr3 = ButtonSprite::create("Avanzado");
    spr3->setScale(0.45f);
    auto tab3 = CCMenuItemSpriteExtra::create(spr3, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab3->setTag(2);
    tab3->setID("pet-advanced-tab-btn"_spr);
    tab3->setPosition({cx + 90.f, topY});
    menu->addChild(tab3);
    m_tabs.push_back(tab3);

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


void PetConfigPopup::buildGalleryTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

// Remove corrupt gallery files automatically.
    int cleaned = PetManager::get().cleanupInvalidImages();
    if (cleaned > 0) {
        log::info("[PetConfig] Cleaned up {} invalid image files from gallery", cleaned);
    }

    auto previewBg = paimon::SpriteHelper::createDarkPanel(80, 80, 80);
    previewBg->setPosition({cx - 40, content.height - 95.f - 40});
    m_galleryTab->addChild(previewBg);

    m_selectedLabel = CCLabelBMFont::create("Sin mascota elegida", "bigFont.fnt");
    m_selectedLabel->setScale(0.25f);
    m_selectedLabel->setPosition({cx, content.height - 145.f});
    m_galleryTab->addChild(m_selectedLabel);

    m_galleryContainer = CCNode::create();
    m_galleryContainer->setID("gallery-container"_spr);
    m_galleryContainer->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryContainer);

    m_galleryMenu = CCMenu::create();
    m_galleryMenu->setID("gallery-menu"_spr);
    m_galleryMenu->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryMenu, 10);

    auto addSpr = ButtonSprite::create("+ Anadir", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    addSpr->setScale(0.55f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(PetConfigPopup::onAddImage));
    addBtn->setPosition({cx - 100.f, 25.f});
    m_galleryMenu->addChild(addBtn);

    auto shopSpr = ButtonSprite::create("Tienda", "goldFont.fnt", "GJ_button_02.png", 0.7f);
    shopSpr->setScale(0.55f);
    auto shopBtn = CCMenuItemSpriteExtra::create(shopSpr, this, menu_selector(PetConfigPopup::onOpenShop));
    shopBtn->setPosition({cx - 15.f, 25.f});
    m_galleryMenu->addChild(shopBtn);

    auto delAllSpr = ButtonSprite::create("Borrar todo", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    delAllSpr->setScale(0.55f);
    auto delAllBtn = CCMenuItemSpriteExtra::create(delAllSpr, this, menu_selector(PetConfigPopup::onDeleteAllImages));
    delAllBtn->setPosition({cx + 85.f, 25.f});
    m_galleryMenu->addChild(delAllBtn);

    refreshGallery();
}

void PetConfigPopup::refreshGallery() {
    if (m_galleryContainer) {
        m_galleryContainer->removeAllChildren();
    }

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

        bool isSelected = (images[i] == pet.config().selectedImage);
        auto bg = paimon::SpriteHelper::createColorPanel(
            cellSize, cellSize,
            isSelected ? ccc3(0, 200, 0) : ccc3(50, 50, 50),
            isSelected ? 180 : 100);
        bg->setPosition({x - cellSize / 2, y - cellSize / 2});
        m_galleryContainer->addChild(bg);

        auto tex = pet.loadGalleryThumb(images[i]);
        if (tex) {
            auto thumbSpr = CCSprite::createWithTexture(tex);
            if (thumbSpr) {
                float maxDim = std::max(thumbSpr->getContentSize().width, thumbSpr->getContentSize().height);
                if (maxDim > 0) thumbSpr->setScale((cellSize - 8.f) / maxDim);
                thumbSpr->setPosition({x, y});
                m_galleryContainer->addChild(thumbSpr, 1);

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

        auto selectArea = CCSprite::create();
        selectArea->setContentSize({cellSize, cellSize});
        selectArea->setOpacity(0);
        auto selectBtn = CCMenuItemSpriteExtra::create(selectArea, this, menu_selector(PetConfigPopup::onSelectImage));
        selectBtn->setContentSize({cellSize, cellSize});
        selectBtn->setPosition({x, y});
        selectBtn->setTag(100 + i);
        selectBtn->setUserObject(CCString::create(images[i]));
        m_galleryMenu->addChild(selectBtn);

        auto xSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        if (xSpr) {
            xSpr->setScale(0.35f);
            auto xBtn = CCMenuItemSpriteExtra::create(xSpr, this, menu_selector(PetConfigPopup::onDeleteImage));
            xBtn->setPosition({x + cellSize / 2.f - 5.f, y + cellSize / 2.f - 5.f});
            xBtn->setTag(500 + i);
            xBtn->setUserObject(CCString::create(images[i]));
            m_galleryMenu->addChild(xBtn);
        }
    }

    auto& cfg = pet.config();
    if (!cfg.selectedImage.empty()) {
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
        m_selectedLabel->setString("Sin mascota elegida");
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
            PaimonNotify::create("Imagen anadida a la galeria!", NotificationIcon::Success)->show();
            if (PetManager::get().config().selectedImage.empty()) {
                PetManager::get().setImage(filename);
            }
            popup->refreshGallery();
        } else {
            PaimonNotify::create("No se pudo anadir la imagen", NotificationIcon::Error)->show();
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
    PopupManager::get().quickPopup(
        "Borrar Imagen",
        "Seguro que quieres <cr>borrar</c> esta imagen?\n<cy>" + filename + "</c>",
        "Cancelar", "Borrar",
        [self, filename](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;
            PetManager::get().removeFromGallery(filename);
            PaimonNotify::create("Imagen eliminada", NotificationIcon::Info)->show();
            static_cast<PetConfigPopup*>(popup.data())->refreshGallery();
        }
    ).showInstant();
}

void PetConfigPopup::onDeleteAllImages(CCObject*) {
    auto images = PetManager::get().getGalleryImages();
    if (images.empty()) {
        PaimonNotify::create("La galeria ya esta vacia", NotificationIcon::Info)->show();
        return;
    }

    std::string msg = fmt::format(
        "Seguro que quieres <cr>borrar TODAS</c> las {} imagenes?\nEsto no se puede deshacer!",
        images.size()
    );

    WeakRef<PetConfigPopup> self = this;
    PopupManager::get().quickPopup(
        "Borrar Todas",
        msg,
        "Cancelar", "Borrar todo",
        [self](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;

            int cleaned = PetManager::get().cleanupInvalidImages();
            PetManager::get().removeAllFromGallery();

            std::string note = "Todas las imagenes borradas!";
            if (cleaned > 0) {
                note += fmt::format(" ({} archivos corruptos eliminados)", cleaned);
            }
            PaimonNotify::create(note, NotificationIcon::Success)->show();
            static_cast<PetConfigPopup*>(popup.data())->refreshGallery();
        }
    ).showInstant();
}

void PetConfigPopup::onSelectImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();
    PetManager::get().setImage(filename);
    PaimonNotify::create("Mascota elegida!", NotificationIcon::Success)->show();
    refreshGallery();
}

void PetConfigPopup::onOpenShop(CCObject*) {
    auto shop = PaimonShopPopup::create();
    if (shop) shop->show();
}


void PetConfigPopup::buildSettingsTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 58.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto& cfg = PetManager::get().config();

    auto fmtTimes = [](double v) { return fmt::format("x{:.2f}", v); };
    auto fmtPlain = [](double v) { return fmt::format("{:.2f}", v); };
    auto fmtInt   = [](double v) { return fmt::format("{:.0f}", v); };
    auto fmtPct255 = [](double v) {
        return fmt::format("{}%", static_cast<int>(v / 255.0 * 100.0));
    };

    auto* hero = kit::makeHeroToggle(scrollW,
        "Mascota en pantalla",
        "Un companero que sigue tu cursor por los menus.",
        cfg.enabled,
        [this](bool v) {
            auto& c = PetManager::get().config();
            c.enabled = v;
            applyLive();
            if (v && c.selectedImage.empty()) {
                PaimonNotify::create(
                    "Elige primero una imagen en la pestana Galeria.",
                    NotificationIcon::Info
                )->show();
            }
        },
        &m_enableToggle, &m_enableStateLabel);

    auto* lookCard = kit::makeCard(scrollW, "Apariencia", {120, 210, 255}, {
        kit::makeSliderRow(innerW,
            "Tamano", "Que tan grande se ve la mascota.",
            cfg.scale, 0.1, 3.0, fmtTimes,
            [this](double v) {
                PetManager::get().config().scale = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Opacidad", "100% = solida, menos = transparente.",
            static_cast<double>(cfg.opacity), 0.0, 255.0, fmtPct255,
            [this](double v) {
                auto& c = PetManager::get().config();
                c.opacity = std::max(0, std::min(255, static_cast<int>(v)));
                applyLive();
            }),
    });

    auto* moveCard = kit::makeCard(scrollW, "Movimiento", {130, 240, 170}, {
        kit::makeSliderRow(innerW,
            "Velocidad de seguimiento",
            "Bajo = perezosa, alto = pegada al cursor.",
            cfg.sensitivity, 0.01, 1.0, fmtPlain,
            [this](double v) {
                PetManager::get().config().sensitivity = static_cast<float>(v);
                applyLive();
            }),
        kit::makeToggleRow(innerW,
            "Mirar hacia donde va",
            "Se voltea segun la direccion del movimiento.",
            cfg.flipOnDirection,
            [this](bool v) {
                PetManager::get().config().flipOnDirection = v;
                applyLive();
            }),
        kit::makeToggleRow(innerW,
            "Rebotar al moverse",
            "Da saltitos mientras sigue al cursor.",
            cfg.bounce,
            [this](bool v) {
                PetManager::get().config().bounce = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Altura del rebote", "Cuanto sube en cada saltito.",
            cfg.bounceHeight, 0.0, 20.0, fmtInt,
            [this](double v) {
                PetManager::get().config().bounceHeight = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Velocidad del rebote", "Saltitos por segundo.",
            cfg.bounceSpeed, 0.5, 10.0, fmtPlain,
            [this](double v) {
                PetManager::get().config().bounceSpeed = static_cast<float>(v);
                applyLive();
            }),
    });

    auto* whereCard = kit::makeCard(scrollW, "Donde aparece", {255, 200, 100}, {
        kit::makeToggleRow(innerW,
            "En todos los menus",
            "Muestra la mascota en todas las pantallas fuera del juego.",
            cfg.allLayers,
            [this](bool v) {
                auto& c = PetManager::get().config();
                c.allLayers = v;
                if (!v && allNonGameplayLayersSelected(c.visibleLayers)) {
                    for (auto const& layerName : PET_LAYER_OPTIONS) {
                        if (isPetGameplayLayer(layerName)) continue;
                        c.visibleLayers.erase(layerName);
                    }
                }
                applyLive();
            },
            &m_allLayersToggle),
        kit::makeToggleRow(innerW,
            "Durante el juego",
            "Tambien visible mientras juegas un nivel.",
            cfg.showInGameplay,
            [this](bool v) {
                PetManager::get().config().showInGameplay = v;
                applyLive();
            },
            &m_showInGameplayToggle),
        kit::makeButtonRow(innerW,
            "Elegir pantallas",
            "Escoge una por una las pantallas donde aparece.",
            "Abrir",
            [this] { openLayerPicker(); }),
    });

    auto* footer = kit::makeHint(scrollW,
        "Consejo: en la pestana Avanzado hay mas efectos (sombra, particulas, "
        "dialogos, sueno y reacciones).");

    m_scrollLayer = kit::makeScrollStack({scrollW, scrollH},
        {hero, lookCard, moveCard, whereCard, footer});
    m_scrollLayer->setPosition({12.f, 8.f});
    m_settingsTab->addChild(m_scrollLayer, 5);
}


void PetConfigPopup::buildAdvancedTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 58.f;
    float innerW = kit::cardInnerWidth(scrollW);

    auto& cfg = PetManager::get().config();

    auto fmtTimes = [](double v) { return fmt::format("x{:.2f}", v); };
    auto fmtPlain = [](double v) { return fmt::format("{:.2f}", v); };
    auto fmtF1    = [](double v) { return fmt::format("{:.1f}", v); };
    auto fmtInt   = [](double v) { return fmt::format("{:.0f}", v); };
    auto fmtSecs  = [](double v) { return fmt::format("{:.0f}s", v); };
    auto fmtDeg   = [](double v) { return fmt::format("{:.0f} gr", v); };

    auto makeStateRow = [this, innerW](int idx) -> CCNode* {
        auto* row = CCNode::create();
        row->setAnchorPoint({0.f, 0.f});
        row->setContentSize({innerW, 30.f});

        auto* title = CCLabelBMFont::create(kIconStateNames[idx], "bigFont.fnt");
        title->setAnchorPoint({0.f, 1.f});
        title->setColor(kit::kTitleColor);
        title->limitLabelWidth(innerW - 90.f, 0.40f, 0.1f);
        title->setPosition({10.f, 27.f});
        row->addChild(title);

        std::string current = PetManager::get().getIconStateImage(kIconStateEnums[idx]);
        auto* value = CCLabelBMFont::create(
            fmt::format("Imagen: {}", current.empty() ? "(la de la galeria)" : current).c_str(),
            "chatFont.fnt");
        value->setAnchorPoint({0.f, 0.5f});
        value->setScale(0.42f);
        value->setColor(kit::kDescColor);
        value->limitLabelWidth(innerW - 90.f, 0.42f, 0.1f);
        value->setPosition({10.f, 8.f});
        row->addChild(value);
        m_iconStateValueLabels[static_cast<size_t>(idx)] = value;

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(
            CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        row->addChild(menu, 5);

        auto* spr = ButtonSprite::create("Cambiar", "goldFont.fnt", "GJ_button_04.png", 0.7f);
        if (spr) spr->setScale(0.5f);
        auto* btn = CCMenuItemExt::createSpriteExtra(
            spr, [this, idx](CCMenuItemSpriteExtra*) { pickIconStateImage(idx); });
        btn->setPosition({innerW - 14.f - btn->getScaledContentSize().width / 2.f, 15.f});
        menu->addChild(btn);

        return row;
    };

    auto* statesCard = kit::makeCard(scrollW, "Imagenes por estado", {255, 140, 220}, {
        kit::makeHint(innerW,
            "Usa una imagen distinta cuando la mascota camina, duerme o reacciona. "
            "Si un estado esta vacio, se usa la imagen de la Galeria."),
        makeStateRow(0),
        makeStateRow(1),
        makeStateRow(2),
        makeStateRow(3),
    });

    auto* animCard = kit::makeCard(scrollW, "Animaciones extra", {120, 210, 255}, {
        kit::makeToggleRow(innerW,
            "Respirar en reposo",
            "Crece y encoge suavemente cuando esta quieta.",
            cfg.idleAnimation,
            [this](bool v) {
                PetManager::get().config().idleAnimation = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Cuanto respira", "Cuanto crece en cada respiracion.",
            cfg.idleBreathScale, 0.0, 0.15,
            [](double v) { return fmt::format("{:.3f}", v); },
            [this](double v) {
                PetManager::get().config().idleBreathScale = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Velocidad al respirar", "Respiraciones por segundo.",
            cfg.idleBreathSpeed, 0.5, 5.0, fmtF1,
            [this](double v) {
                PetManager::get().config().idleBreathSpeed = static_cast<float>(v);
                applyLive();
            }),
        kit::makeToggleRow(innerW,
            "Aplastarse al frenar",
            "Efecto de dibujo animado al detenerse.",
            cfg.squishOnLand,
            [this](bool v) {
                PetManager::get().config().squishOnLand = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Cuanto se aplasta", "0 = nada, 0.5 = mucho.",
            cfg.squishAmount, 0.0, 0.5, fmtPlain,
            [this](double v) {
                PetManager::get().config().squishAmount = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Inclinacion maxima", "Cuanto se ladea al girar.",
            cfg.maxTilt, 0.0, 45.0, fmtDeg,
            [this](double v) {
                PetManager::get().config().maxTilt = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Suavidad del giro", "Que tan suave vuelve a enderezarse.",
            cfg.rotationDamping, 0.0, 1.0, fmtPlain,
            [this](double v) {
                PetManager::get().config().rotationDamping = static_cast<float>(v);
                applyLive();
            }),
    });

    auto* offsetCard = kit::makeCard(scrollW, "Posicion respecto al cursor", {130, 240, 170}, {
        kit::makeSliderRow(innerW,
            "Desplazamiento X", "Negativo = izquierda, positivo = derecha.",
            cfg.offsetX, -50.0, 50.0, fmtInt,
            [this](double v) {
                PetManager::get().config().offsetX = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Desplazamiento Y", "Positivo = por encima del cursor.",
            cfg.offsetY, -50.0, 100.0, fmtInt,
            [this](double v) {
                PetManager::get().config().offsetY = static_cast<float>(v);
                applyLive();
            }),
    });

    auto* trailCard = kit::makeCard(scrollW, "Estela", {255, 200, 100}, {
        kit::makeToggleRow(innerW,
            "Mostrar estela",
            "Deja un rastro brillante al moverse.",
            cfg.showTrail,
            [this](bool v) {
                PetManager::get().config().showTrail = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Largo", "Cuanto dura el rastro.",
            cfg.trailLength, 5.0, 100.0, fmtInt,
            [this](double v) {
                PetManager::get().config().trailLength = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Grosor", "Ancho de la estela.",
            cfg.trailWidth, 1.0, 20.0, fmtF1,
            [this](double v) {
                PetManager::get().config().trailWidth = static_cast<float>(v);
                applyLive();
            }),
    });

    auto* shadowCard = kit::makeCard(scrollW, "Sombra", {170, 170, 255}, {
        kit::makeToggleRow(innerW,
            "Mostrar sombra",
            "Una sombra suave debajo de la mascota.",
            cfg.showShadow,
            [this](bool v) {
                PetManager::get().config().showShadow = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Sombra X", "Mueve la sombra a los lados.",
            cfg.shadowOffsetX, -20.0, 20.0, fmtInt,
            [this](double v) {
                PetManager::get().config().shadowOffsetX = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Sombra Y", "Mueve la sombra arriba o abajo.",
            cfg.shadowOffsetY, -20.0, 20.0, fmtInt,
            [this](double v) {
                PetManager::get().config().shadowOffsetY = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Opacidad de la sombra", "Que tan oscura se ve.",
            static_cast<double>(cfg.shadowOpacity), 0.0, 200.0, fmtInt,
            [this](double v) {
                PetManager::get().config().shadowOpacity = static_cast<int>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Tamano de la sombra", "Relativo al tamano de la mascota.",
            cfg.shadowScale, 0.5, 2.0, fmtTimes,
            [this](double v) {
                PetManager::get().config().shadowScale = static_cast<float>(v);
                applyLive();
            }),
    });

    int particleIdx = std::max(0, std::min(4, cfg.particleType));
    auto* particleCard = kit::makeCard(scrollW, "Particulas", {255, 170, 120}, {
        kit::makeToggleRow(innerW,
            "Soltar particulas",
            "La mascota emite particulas al moverse.",
            cfg.showParticles,
            [this](bool v) {
                PetManager::get().config().showParticles = v;
                applyLive();
            }),
        kit::makeSelectRow(innerW,
            "Tipo", "Forma de las particulas.",
            {"Chispas", "Corazones", "Estrellas", "Nieve", "Burbujas"}, particleIdx,
            [this](int idx) {
                PetManager::get().config().particleType = idx;
                applyLive();
            }),
        kit::makeButtonRow(innerW,
            "Color", "Elige el color de las particulas.",
            "Elegir",
            [this] {
                auto& c = PetManager::get().config();
                auto currentColor = ccc4(c.particleColor.r, c.particleColor.g, c.particleColor.b, 255);
                auto* picker = geode::ColorPickPopup::create(currentColor);
                if (!picker) return;
                picker->setCallback([this](ccColor4B const& color) {
                    auto& cc = PetManager::get().config();
                    cc.particleColor = ccc3(color.r, color.g, color.b);
                    applyLive();
                });
                picker->show();
            }),
        kit::makeSliderRow(innerW,
            "Cantidad", "Particulas por segundo.",
            cfg.particleRate, 1.0, 30.0, fmtInt,
            [this](double v) {
                PetManager::get().config().particleRate = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Tamano", "Tamano de cada particula.",
            cfg.particleSize, 1.0, 10.0, fmtF1,
            [this](double v) {
                PetManager::get().config().particleSize = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Gravedad", "Negativa = caen, positiva = flotan.",
            cfg.particleGravity, -50.0, 50.0, fmtInt,
            [this](double v) {
                PetManager::get().config().particleGravity = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Duracion", "Segundos que vive cada particula.",
            cfg.particleLifetime, 0.5, 5.0, fmtF1,
            [this](double v) {
                PetManager::get().config().particleLifetime = static_cast<float>(v);
                applyLive();
            }),
    });

    auto* speechCard = kit::makeCard(scrollW, "Dialogos", {170, 255, 170}, {
        kit::makeToggleRow(innerW,
            "Hablar de vez en cuando",
            "Muestra burbujas de dialogo segun lo que pasa en el juego.",
            cfg.enableSpeech,
            [this](bool v) {
                PetManager::get().config().enableSpeech = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Cada cuanto habla", "Segundos entre frases.",
            cfg.speechInterval, 5.0, 120.0, fmtSecs,
            [this](double v) {
                PetManager::get().config().speechInterval = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Duracion de la frase", "Segundos que dura la burbuja.",
            cfg.speechDuration, 1.0, 10.0, fmtSecs,
            [this](double v) {
                PetManager::get().config().speechDuration = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Tamano de la burbuja", "Escala del globo de texto.",
            cfg.speechBubbleScale, 0.2, 1.0, fmtTimes,
            [this](double v) {
                PetManager::get().config().speechBubbleScale = static_cast<float>(v);
                applyLive();
            }),
    });

    auto* sleepCard = kit::makeCard(scrollW, "Sueno", {200, 180, 255}, {
        kit::makeToggleRow(innerW,
            "Dormirse si no haces nada",
            "Tras un rato quieta se duerme con un Zzz. Se despierta al mover el raton.",
            cfg.enableSleep,
            [this](bool v) {
                PetManager::get().config().enableSleep = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Se duerme tras", "Segundos sin actividad.",
            cfg.sleepAfterSeconds, 10.0, 300.0, fmtSecs,
            [this](double v) {
                PetManager::get().config().sleepAfterSeconds = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Balanceo al dormir", "Cuanto se mece mientras duerme.",
            cfg.sleepBobAmount, 0.0, 10.0, fmtF1,
            [this](double v) {
                PetManager::get().config().sleepBobAmount = static_cast<float>(v);
                applyLive();
            }),
    });

    auto* clickCard = kit::makeCard(scrollW, "Al hacerle click", {255, 220, 130}, {
        kit::makeToggleRow(innerW,
            "Reaccionar al click",
            "Salta y dice algo cuando le haces click.",
            cfg.enableClickInteraction,
            [this](bool v) {
                PetManager::get().config().enableClickInteraction = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Duracion", "Segundos que dura la reaccion.",
            cfg.clickReactionDuration, 0.5, 5.0, fmtSecs,
            [this](double v) {
                PetManager::get().config().clickReactionDuration = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Altura del salto", "Cuanto salta de alegria.",
            cfg.clickJumpHeight, 5.0, 50.0, fmtInt,
            [this](double v) {
                PetManager::get().config().clickJumpHeight = static_cast<float>(v);
                applyLive();
            }),
    });

    auto* reactCard = kit::makeCard(scrollW, "Reacciones del juego", {255, 140, 140}, {
        kit::makeToggleRow(innerW,
            "Al completar un nivel",
            "Celebra con saltos y giros.",
            cfg.reactToLevelComplete,
            [this](bool v) {
                PetManager::get().config().reactToLevelComplete = v;
                applyLive();
            }),
        kit::makeToggleRow(innerW,
            "Al morir",
            "Te anima cuando pierdes.",
            cfg.reactToDeath,
            [this](bool v) {
                PetManager::get().config().reactToDeath = v;
                applyLive();
            }),
        kit::makeToggleRow(innerW,
            "Al salir del modo practica",
            "Reacciona al terminar la practica.",
            cfg.reactToPracticeExit,
            [this](bool v) {
                PetManager::get().config().reactToPracticeExit = v;
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Duracion", "Segundos que dura la celebracion.",
            cfg.reactionDuration, 0.5, 5.0, fmtSecs,
            [this](double v) {
                PetManager::get().config().reactionDuration = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Altura del salto", "Cuanto salta al celebrar.",
            cfg.reactionJumpHeight, 5.0, 60.0, fmtInt,
            [this](double v) {
                PetManager::get().config().reactionJumpHeight = static_cast<float>(v);
                applyLive();
            }),
        kit::makeSliderRow(innerW,
            "Velocidad del giro", "Grados por segundo al girar.",
            cfg.reactionSpinSpeed, 90.0, 720.0, fmtDeg,
            [this](double v) {
                PetManager::get().config().reactionSpinSpeed = static_cast<float>(v);
                applyLive();
            }),
    });

    m_advancedScroll = kit::makeScrollStack({scrollW, scrollH},
        {statesCard, animCard, offsetCard, trailCard, shadowCard,
         particleCard, speechCard, sleepCard, clickCard, reactCard});
    m_advancedScroll->setPosition({12.f, 8.f});
    m_advancedTab->addChild(m_advancedScroll, 5);
}


void PetConfigPopup::openLayerPicker() {
    auto popup = PetLayerPickerPopup::create(this);
    if (popup) popup->show();
}

void PetConfigPopup::pickIconStateImage(int stateIdx) {
    if (stateIdx < 0 || stateIdx >= kIconStateCount) return;
    auto state = kIconStateEnums[stateIdx];

    WeakRef<PetConfigPopup> self = this;
    pt::pickImage([self, state](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto filename = PetManager::get().addToGallery(*pathOpt);
        if (!filename.empty()) {
            PetManager::get().setIconStateImage(state, filename);
            PaimonNotify::create("Imagen del estado asignada!", NotificationIcon::Success)->show();
            auto* p = static_cast<PetConfigPopup*>(popup.data());
            p->refreshIconStateLabels();
            p->refreshGallery();
        } else {
            PaimonNotify::create("No se pudo anadir la imagen", NotificationIcon::Error)->show();
        }
    });
}

void PetConfigPopup::refreshIconStateLabels() {
    for (int i = 0; i < kIconStateCount; ++i) {
        auto* lbl = m_iconStateValueLabels[static_cast<size_t>(i)];
        if (!lbl) continue;
        std::string current = PetManager::get().getIconStateImage(kIconStateEnums[i]);
        lbl->setString(fmt::format("Imagen: {}",
            current.empty() ? "(la de la galeria)" : current).c_str());
    }
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


void PetConfigPopup::applyLive() {
    auto& pet = PetManager::get();
    pet.applyConfigLive();

    auto scene = CCDirector::get()->getRunningScene();
    if (pet.config().enabled && scene) {
// attachToScene is idempotent; refresh visibility when already attached.
        pet.attachToScene(scene);
    } else {
        pet.detachFromScene();
    }
}
