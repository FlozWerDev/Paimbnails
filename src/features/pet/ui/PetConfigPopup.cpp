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
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/cocos.hpp>
#include <unordered_map>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
namespace kit = paimon::configkit;

std::string const& petLayerLabel(std::string const& layerName) {
    static std::unordered_map<std::string, std::string> const labels = {
        {"MenuLayer", "Menu principal"},
        {"LevelBrowserLayer", "Explorar niveles"},
        {"LevelInfoLayer", "Informacion del nivel"},
        {"CreatorLayer", "Crear"},
        {"LevelSearchLayer", "Buscar niveles"},
        {"GauntletSelectLayer", "Elegir gauntlet"},
        {"ProfilePage", "Perfil"},
        {"LevelListLayer", "Lista de niveles"},
        {"LevelEditorLayer", "Editor de niveles"},
        {"GJGarageLayer", "Iconos"},
        {"GJShopLayer", "Tienda"},
        {"SecretLayer", "Sala secreta"},
        {"TreasureRoomLayer", "Sala del tesoro"},
        {"ChallengesLayer", "Retos"},
        {"LevelAreaLayer", "Zona del nivel"},
        {"DailyLevelLayer", "Nivel diario"},
        {"WeeklyLevelLayer", "Nivel semanal"},
        {"GauntletLayer", "Gauntlet"},
        {"LeaderboardLayer", "Clasificacion general"},
        {"LevelLeaderboard", "Clasificacion del nivel"},
        {"CommentListLayer", "Comentarios"},
        {"InfoLayer", "Informacion"},
        {"SongInfoLayer", "Informacion de la cancion"},
        {"CustomSongLayer", "Canciones personalizadas"},
        {"GJMoreGamesLayer", "Mas juegos"},
        {"GJOptionsLayer", "Opciones de GD"},
        {"OptionsLayer", "Opciones"},
        {"MoreOptionsLayer", "Mas opciones"},
        {"AccountLayer", "Cuenta"},
        {"AccountLoginLayer", "Iniciar sesion"},
        {"GJAccountSettingsLayer", "Ajustes de cuenta"},
        {"GJScoreLayer", "Puntuacion del jugador"},
        {"FLAlertLayer", "Avisos"},
        {"GJDropDownLayer", "Menu desplegable"},
        {"SelectItemLayer", "Elegir objeto"},
        {"GJLocalLevelSelector", "Niveles locales"},
        {"TowerSelectorLayer", "Elegir torre"},
        {"GJPathsLayer", "Caminos"},
        {"GJPathPage", "Pagina de caminos"},
        {"GJMapPackLayer", "Packs de mapas"},
        {"PromoArtLayer", "Arte promocional"},
        {"SupportLayer", "Ayuda"},
        {"CreditsLayer", "Creditos"},
        {"GJChallengeLayer", "Desafio"},
        {"GJRewardLayer", "Recompensas"},
        {"LevelSelectLayer", "Elegir nivel"},
        {"GJFriendsLayer", "Amigos"},
        {"GJScoresLayer", "Marcadores"},
        {"LeaderboardsLayer", "Tablas de clasificacion"},
        {"GJCommentListLayer", "Comentarios del nivel"},
        {"FRequestProfilePage", "Solicitud de amistad"},
        {"GJLevelScoreCell", "Puntuacion del nivel"},
    };
    auto const it = labels.find(layerName);
    return it == labels.end() ? layerName : it->second;
}

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
                "Al elegir una, se desactiva <cy>En todos los menus</c>.\n"
                "El gameplay se controla aparte con <cg>Durante el juego</c>.",
                this, 0.42f)) {
            infoBtn->setPosition({content.width / 2.f + 78.f, content.height - 20.f});
            menu->addChild(infoBtn);
        }

        auto* hint = CCLabelBMFont::create("Elige donde aparece fuera del juego.", "chatFont.fnt");
        hint->setColor({255, 222, 150});
        hint->limitLabelWidth(content.width - 36.f, 0.48f, 0.1f);
        hint->setPosition({content.width / 2.f, 216.f});
        m_mainLayer->addChild(hint);

        m_scrollLayer = ScrollLayer::create({content.width - 16.f, content.height - 74.f});
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

            auto lbl = CCLabelBMFont::create(petLayerLabel(layerName).c_str(), "bigFont.fnt");
            lbl->limitLabelWidth(190.f, 0.34f, 0.1f);
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

        auto selectAllSpr = ButtonSprite::create("Todas", 55, true, "goldFont.fnt", "GJ_button_01.png", 18.f, 0.45f);
        auto selectAllBtn = CCMenuItemSpriteExtra::create(
            selectAllSpr, this, menu_selector(PetLayerPickerPopup::onSelectAll));
        selectAllBtn->setPosition({content.width / 2.f - 55.f, 15.f});
        menu->addChild(selectAllBtn);

        auto clearSpr = ButtonSprite::create("Ninguna", 69, true, "goldFont.fnt", "GJ_button_06.png", 18.f, 0.45f);
        auto clearBtn = CCMenuItemSpriteExtra::create(
            clearSpr, this, menu_selector(PetLayerPickerPopup::onClearAll));
        clearBtn->setPosition({content.width / 2.f + 55.f, 15.f});
        menu->addChild(clearBtn);

        m_scrollLayer->scrollToTop();
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

        auto& config = PetManager::get().config();
        auto& layers = config.visibleLayers;
        std::string const layerName = nameStr->getCString();
        bool const turnOn = !toggle->isToggled();

        if (turnOn) {
            layers.insert(layerName);
        } else {
            layers.erase(layerName);
        }

        config.allLayers = false;
        syncOwner();
    }

    void onSelectAll(CCObject*) {
        auto& config = PetManager::get().config();
        auto& layers = config.visibleLayers;
        for (auto const& layerName : PET_LAYER_OPTIONS) {
            if (isPetGameplayLayer(layerName)) continue;
            layers.insert(layerName);
        }
        for (auto* toggle : m_layerToggles) {
            if (toggle) toggle->toggle(true);
        }
        config.allLayers = true;
        syncOwner();
    }

    void onClearAll(CCObject*) {
        auto& config = PetManager::get().config();
        auto& layers = config.visibleLayers;
        for (auto const& layerName : PET_LAYER_OPTIONS) {
            if (isPetGameplayLayer(layerName)) continue;
            layers.erase(layerName);
        }
        for (auto* toggle : m_layerToggles) {
            if (toggle) toggle->toggle(false);
        }
        config.allLayers = false;
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

void addTabIntro(CCNode* tab, float width, char const* text) {
    auto* label = CCLabelBMFont::create(text, "chatFont.fnt");
    label->setColor({255, 229, 166});
    label->limitLabelWidth(width - 32.f, 0.55f, 0.1f);
    label->setPosition({width / 2.f, 224.f});
    tab->addChild(label);
}

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
    if (m_currentTab == 0 &&
        kit::queueWheelScroll(m_galleryScroll, x, y, m_galleryScrollTargetY, m_galleryScrollTargetSet)) return;
    if (m_currentTab == 1 &&
        kit::queueWheelScroll(m_scrollLayer, x, y, m_settingsScrollTargetY, m_settingsScrollTargetSet)) return;
    if (m_currentTab == 2 &&
        kit::queueWheelScroll(m_advancedScroll, x, y, m_advancedScrollTargetY, m_advancedScrollTargetSet)) return;
}

void PetConfigPopup::updateSmoothScroll(float dt) {
    kit::stepWheelScroll(m_galleryScroll, m_galleryScrollTargetY, m_galleryScrollTargetSet, dt);
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

    auto spr1 = ButtonSprite::create("1 Imagen", 84, true, "goldFont.fnt", "GJ_button_01.png", 18.f, 0.45f);
    auto tab1 = CCMenuItemSpriteExtra::create(spr1, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab1->setTag(0);
    tab1->setID("pet-gallery-tab-btn"_spr);
    tab1->setPosition({cx - 98.f, topY});
    menu->addChild(tab1);
    m_tabs.push_back(tab1);

    auto spr2 = ButtonSprite::create("2 Ajustes", 84, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.45f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab2->setTag(1);
    tab2->setID("pet-settings-tab-btn"_spr);
    tab2->setPosition({cx, topY});
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    auto spr3 = ButtonSprite::create("3 Extras", 84, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.45f);
    auto tab3 = CCMenuItemSpriteExtra::create(spr3, this, menu_selector(PetConfigPopup::onTabSwitch));
    tab3->setTag(2);
    tab3->setID("pet-advanced-tab-btn"_spr);
    tab3->setPosition({cx + 98.f, topY});
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
    if (m_currentTab == 0 && m_galleryScroll) refreshGallery();

    for (auto* tab : m_tabs) {
        auto spr = typeinfo_cast<ButtonSprite*>(tab->getNormalImage());
        if (!spr) continue;
        bool selected = tab->getTag() == m_currentTab;
        spr->updateBGImage(selected ? "GJ_button_01.png" : "GJ_button_04.png");
        spr->setOpacity(selected ? 255 : 205);
    }
}


void PetConfigPopup::buildGalleryTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    addTabIntro(m_galleryTab, content.width, "Elige una imagen para tu mascota y despues activala en Ajustes.");

    int cleaned = PetManager::get().cleanupInvalidImages();
    if (cleaned > 0) {
        log::info("[PetConfig] Cleaned up {} invalid image files from gallery", cleaned);
    }

    auto* previewPanel = cocos2d::extension::CCScale9Sprite::create("GJ_square02.png");
    previewPanel->setContentSize({140.f, 140.f});
    previewPanel->setPosition({82.f, 138.f});
    m_galleryTab->addChild(previewPanel);

    auto* galleryPanel = cocos2d::extension::CCScale9Sprite::create("GJ_square02.png");
    galleryPanel->setContentSize({246.f, 140.f});
    galleryPanel->setPosition({284.f, 138.f});
    m_galleryTab->addChild(galleryPanel);

    auto* previewTitle = CCLabelBMFont::create("Vista previa", "bigFont.fnt");
    previewTitle->setScale(0.38f);
    previewTitle->setPosition({82.f, 195.f});
    m_galleryTab->addChild(previewTitle);

    auto* previewFrame = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png");
    previewFrame->setContentSize({78.f, 70.f});
    previewFrame->setPosition({82.f, 146.f});
    m_galleryTab->addChild(previewFrame);

    m_emptyPreviewIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
    if (m_emptyPreviewIcon) {
        m_emptyPreviewIcon->setScale(0.55f);
        m_emptyPreviewIcon->setOpacity(155);
        m_emptyPreviewIcon->setPosition({82.f, 146.f});
        m_galleryTab->addChild(m_emptyPreviewIcon);
    }

    m_selectedLabel = CCLabelBMFont::create("Sin imagen", "bigFont.fnt");
    m_selectedLabel->setPosition({82.f, 101.f});
    m_selectedLabel->limitLabelWidth(120.f, 0.32f, 0.1f);
    m_galleryTab->addChild(m_selectedLabel);

    m_galleryStatusLabel = CCLabelBMFont::create("Elige una imagen", "chatFont.fnt");
    m_galleryStatusLabel->setColor({255, 222, 150});
    m_galleryStatusLabel->setPosition({82.f, 79.f});
    m_galleryStatusLabel->limitLabelWidth(124.f, 0.49f, 0.1f);
    m_galleryTab->addChild(m_galleryStatusLabel);

    m_galleryCountLabel = CCLabelBMFont::create("Tus imagenes", "bigFont.fnt");
    m_galleryCountLabel->setAnchorPoint({0.f, 0.5f});
    m_galleryCountLabel->setPosition({174.f, 195.f});
    m_galleryCountLabel->limitLabelWidth(130.f, 0.38f, 0.1f);
    m_galleryTab->addChild(m_galleryCountLabel);

    m_galleryHintLabel = CCLabelBMFont::create("Toca una imagen para seleccionarla.", "chatFont.fnt");
    m_galleryHintLabel->setAnchorPoint({0.f, 0.5f});
    m_galleryHintLabel->setColor({255, 222, 150});
    m_galleryHintLabel->setPosition({174.f, 178.f});
    m_galleryHintLabel->limitLabelWidth(216.f, 0.44f, 0.1f);
    m_galleryTab->addChild(m_galleryHintLabel);

    m_galleryScroll = ScrollLayer::create({226.f, 100.f});
    m_galleryScroll->setID("pet-gallery-scroll"_spr);
    m_galleryScroll->setPosition({171.f, 70.f});
    m_galleryTab->addChild(m_galleryScroll, 5);

    m_galleryMenu = CCMenu::create();
    m_galleryMenu->setID("gallery-menu"_spr);
    m_galleryMenu->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryMenu, 10);

    auto* refreshSpr = ButtonSprite::create("Actualizar", 75, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.37f);
    auto* refreshBtn = CCMenuItemExt::createSpriteExtra(
        refreshSpr, [this](CCMenuItemSpriteExtra*) { refreshGallery(); });
    refreshBtn->setPosition({356.f, 195.f});
    m_galleryMenu->addChild(refreshBtn);

    auto addSpr = ButtonSprite::create("Importar", 78, true, "goldFont.fnt", "GJ_button_01.png", 18.f, 0.45f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(PetConfigPopup::onAddImage));
    addBtn->setPosition({57.f, 45.f});
    m_galleryMenu->addChild(addBtn);

    auto shopSpr = ButtonSprite::create("Tienda", 70, true, "goldFont.fnt", "GJ_button_02.png", 18.f, 0.45f);
    auto shopBtn = CCMenuItemSpriteExtra::create(shopSpr, this, menu_selector(PetConfigPopup::onOpenShop));
    shopBtn->setPosition({148.f, 45.f});
    m_galleryMenu->addChild(shopBtn);

    auto delAllSpr = ButtonSprite::create("Borrar todo", 91, true, "goldFont.fnt", "GJ_button_06.png", 18.f, 0.42f);
    auto delAllBtn = CCMenuItemSpriteExtra::create(delAllSpr, this, menu_selector(PetConfigPopup::onDeleteAllImages));
    delAllBtn->setPosition({252.f, 45.f});
    m_galleryMenu->addChild(delAllBtn);

    auto nextSpr = ButtonSprite::create("Ajustes >", 82, true, "goldFont.fnt", "GJ_button_01.png", 18.f, 0.42f);
    auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(PetConfigPopup::onNextStep));
    nextBtn->setPosition({361.f, 45.f});
    m_galleryMenu->addChild(nextBtn);

    auto* footer = CCLabelBMFont::create("Importa una imagen tuya o busca una en la Tienda.", "chatFont.fnt");
    footer->setColor({255, 225, 175});
    footer->limitLabelWidth(content.width - 28.f, 0.47f, 0.1f);
    footer->setPosition({cx, 16.f});
    m_galleryTab->addChild(footer);

    refreshGallery();
}

void PetConfigPopup::refreshGallery() {
    if (!m_galleryScroll || !m_galleryScroll->m_contentLayer) return;

    auto* scrollContent = m_galleryScroll->m_contentLayer;
    scrollContent->removeAllChildren();

    auto& pet = PetManager::get();
    auto images = pet.getGalleryImages();
    auto const& cfg = pet.config();

    m_galleryCountLabel->setString(fmt::format("Tus imagenes ({})", images.size()).c_str());
    m_galleryCountLabel->limitLabelWidth(130.f, 0.38f, 0.1f);
    m_galleryHintLabel->setString(images.empty()
        ? "Importa una imagen o entra en Tienda."
        : "Toca una imagen para seleccionarla.");
    m_galleryHintLabel->limitLabelWidth(216.f, 0.44f, 0.1f);

    float const scrollW = m_galleryScroll->getContentSize().width;
    float const scrollH = m_galleryScroll->getContentSize().height;
    float constexpr cellSize = 42.f;
    float constexpr gap = 6.f;
    int constexpr cols = 4;
    int const rows = (static_cast<int>(images.size()) + cols - 1) / cols;
    float const gridH = std::max(scrollH, 10.f + rows * cellSize + std::max(0, rows - 1) * gap);
    scrollContent->setContentSize({scrollW, gridH});

    if (images.empty()) {
        auto* empty = CCLabelBMFont::create("Aun no hay imagenes.\nUsa Importar o Tienda.", "chatFont.fnt");
        empty->setAlignment(kCCTextAlignmentCenter);
        empty->setColor({255, 229, 180});
        empty->setScale(0.58f);
        empty->setPosition({scrollW / 2.f, scrollH / 2.f});
        scrollContent->addChild(empty);
    }

    float const left = (scrollW - (cols * cellSize + (cols - 1) * gap)) / 2.f;
    for (int i = 0; i < static_cast<int>(images.size()); ++i) {
        float const x = left + (i % cols) * (cellSize + gap) + cellSize / 2.f;
        float const y = gridH - 5.f - (i / cols) * (cellSize + gap) - cellSize / 2.f;
        bool const selected = images[i] == cfg.selectedImage;

        auto* cell = CCNode::create();
        cell->setContentSize({cellSize, cellSize});
        cell->setPosition({x - cellSize / 2.f, y - cellSize / 2.f});
        scrollContent->addChild(cell);

        auto* bg = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({cellSize, cellSize});
        bg->setPosition({cellSize / 2.f, cellSize / 2.f});
        bg->setColor(selected ? ccc3(120, 240, 130) : ccc3(255, 255, 255));
        cell->addChild(bg);

        if (auto* tex = pet.loadGalleryThumb(images[i])) {
            if (auto* thumb = CCSprite::createWithTexture(tex)) {
                float const maxDim = std::max(thumb->getContentSize().width, thumb->getContentSize().height);
                if (maxDim > 0.f) thumb->setScale(35.f / maxDim);
                thumb->setPosition({cellSize / 2.f, cellSize / 2.f});
                cell->addChild(thumb, 1);
            }
            tex->release();
        }

        if (selected) {
            if (auto* check = paimon::SpriteHelper::safeCreateWithFrameName("GJ_checkOn_001.png")) {
                check->setScale(0.28f);
                check->setPosition({8.f, 8.f});
                cell->addChild(check, 2);
            }
        } else if (ImageLoadHelper::isAnimatedImage(pet.galleryDir() / images[i])) {
            auto* gif = CCLabelBMFont::create("GIF", "bigFont.fnt");
            gif->setColor({255, 180, 100});
            gif->setScale(0.21f);
            gif->setPosition({10.f, 8.f});
            cell->addChild(gif, 2);
        }

        auto* cellMenu = CCMenu::create();
        cellMenu->setPosition({0.f, 0.f});
        cell->addChild(cellMenu, 5);

        auto* selectArea = CCSprite::create();
        selectArea->setContentSize({cellSize, cellSize});
        selectArea->setOpacity(0);
        auto* selectBtn = CCMenuItemSpriteExtra::create(selectArea, this, menu_selector(PetConfigPopup::onSelectImage));
        selectBtn->setPosition({cellSize / 2.f, cellSize / 2.f});
        selectBtn->setUserObject(CCString::create(images[i]));
        cellMenu->addChild(selectBtn);

        if (auto* deleteIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteIcon_001.png")) {
            deleteIcon->setScale(0.32f);
            auto* deleteArea = CCSprite::create();
            deleteArea->setContentSize({18.f, 18.f});
            deleteArea->setOpacity(0);
            deleteIcon->setPosition({9.f, 9.f});
            deleteArea->addChild(deleteIcon);
            auto* deleteBtn = CCMenuItemSpriteExtra::create(
                deleteArea, this, menu_selector(PetConfigPopup::onDeleteImage));
            deleteBtn->setPosition({cellSize - 6.f, cellSize - 6.f});
            deleteBtn->setUserObject(CCString::create(images[i]));
            cellMenu->addChild(deleteBtn, 10);
        }
    }

    m_galleryScroll->scrollToTop();
    m_galleryScrollTargetSet = false;

    if (m_previewSprite) {
        m_previewSprite->removeFromParent();
        m_previewSprite = nullptr;
    }
    if (!cfg.selectedImage.empty()) {
        if (auto* tex = pet.loadGalleryThumb(cfg.selectedImage)) {
            m_previewSprite = CCSprite::createWithTexture(tex);
            if (m_previewSprite) {
                float const maxDim = std::max(
                    m_previewSprite->getContentSize().width, m_previewSprite->getContentSize().height);
                if (maxDim > 0.f) m_previewSprite->setScale(62.f / maxDim);
                m_previewSprite->setPosition({82.f, 146.f});
                m_galleryTab->addChild(m_previewSprite, 5);
            }
            tex->release();
        }
    }
    if (m_emptyPreviewIcon) m_emptyPreviewIcon->setVisible(m_previewSprite == nullptr);
    m_selectedLabel->setString(cfg.selectedImage.empty() ? "Sin imagen" : cfg.selectedImage.c_str());
    m_selectedLabel->limitLabelWidth(120.f, 0.32f, 0.1f);
    m_galleryStatusLabel->setString(cfg.selectedImage.empty()
        ? "Elige una imagen"
        : (cfg.enabled ? "Mascota activada" : "Activala en Ajustes"));
    m_galleryStatusLabel->limitLabelWidth(124.f, 0.49f, 0.1f);
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

void PetConfigPopup::onNextStep(CCObject*) {
    if (m_tabs.size() > 1) onTabSwitch(m_tabs[1]);
}


void PetConfigPopup::buildSettingsTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 89.f;
    float innerW = kit::cardInnerWidth(scrollW);

    addTabIntro(m_settingsTab, content.width, "Activa la mascota y decide donde quieres verla.");

    auto& cfg = PetManager::get().config();

    auto fmtTimes = [](double v) { return fmt::format("x{:.2f}", v); };
    auto fmtPlain = [](double v) { return fmt::format("{:.2f}", v); };
    auto fmtInt   = [](double v) { return fmt::format("{:.0f}", v); };
    auto fmtPct255 = [](double v) {
        return fmt::format("{}%", static_cast<int>(v / 255.0 * 100.0));
    };

    auto* hero = kit::makeHeroToggle(scrollW,
        "Activar mascota",
        "Tu imagen elegida seguira al cursor.",
        cfg.enabled,
        [this](bool v) {
            auto& c = PetManager::get().config();
            c.enabled = v;
            applyLive();
            if (v && c.selectedImage.empty()) {
                PaimonNotify::create(
                    "Elige primero una imagen en la pestana Imagen.",
                    NotificationIcon::Info
                )->show();
            }
        },
        nullptr, nullptr);

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
            "Aparece en todos los menus y pantallas fuera del juego.",
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
            "Tambien aparece mientras juegas un nivel.",
            cfg.showInGameplay,
            [this](bool v) {
                PetManager::get().config().showInGameplay = v;
                applyLive();
            },
            &m_showInGameplayToggle),
        kit::makeButtonRow(innerW,
            "Elegir pantallas",
            "Elige una por una las pantallas fuera del juego.",
            "Abrir",
            [this] { openLayerPicker(); }),
    });

    auto* footer = kit::makeHint(scrollW,
        "En Extras puedes cambiar animaciones, efectos y reacciones.");

    m_scrollLayer = kit::makeScrollStack({scrollW, scrollH},
        {hero, whereCard, lookCard, moveCard, footer});
    m_scrollLayer->setPosition({12.f, 8.f});
    m_settingsTab->addChild(m_scrollLayer, 5);
}


void PetConfigPopup::buildAdvancedTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 24.f;
    float scrollH = content.height - 89.f;
    float innerW = kit::cardInnerWidth(scrollW);

    addTabIntro(m_advancedTab, content.width, "Opcional: ajusta los efectos y las reacciones de tu mascota.");

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

        auto* spr = ButtonSprite::create("Cambiar", 69, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.45f);
        auto* btn = CCMenuItemExt::createSpriteExtra(
            spr, [this, idx](CCMenuItemSpriteExtra*) { pickIconStateImage(idx); });
        btn->setPosition({innerW - 14.f - btn->getScaledContentSize().width / 2.f, 15.f});
        menu->addChild(btn);

        return row;
    };

    auto* statesCard = kit::makeCard(scrollW, "Imagenes para cada estado", {255, 140, 220}, {
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
        {offsetCard, animCard, trailCard, shadowCard, particleCard,
         speechCard, sleepCard, clickCard, reactCard, statesCard});
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

    if (m_galleryStatusLabel) {
        auto const& cfg = pet.config();
        m_galleryStatusLabel->setString(cfg.selectedImage.empty()
            ? "Elige una imagen"
            : (cfg.enabled ? "Mascota activada" : "Activala en Ajustes"));
        m_galleryStatusLabel->limitLabelWidth(124.f, 0.49f, 0.1f);
    }

    auto scene = CCDirector::get()->getRunningScene();
    if (pet.config().enabled && scene) {
        // Reattach to refresh visibility in the current scene.
        pet.attachToScene(scene);
    } else {
        pet.detachFromScene();
    }
}
