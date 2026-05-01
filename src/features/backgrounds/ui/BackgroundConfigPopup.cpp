#include "BackgroundConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../pet/ui/PetConfigPopup.hpp"
#include "SameAsPickerPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../profiles/ui/ProfilePicEditorPopup.hpp"
#include "../../thumbnails/services/LocalThumbs.hpp"
#include "../services/LayerBackgroundManager.hpp"
#include "../../transitions/services/TransitionManager.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <filesystem>
#include "../../../utils/FileDialog.hpp"

using namespace geode::prelude;

BackgroundConfigPopup* BackgroundConfigPopup::create() {
    auto ret = new BackgroundConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool BackgroundConfigPopup::init() {
    if (!Popup::init(420.f, 290.f)) return false;

    this->setTitle("Configuration");

    auto center = m_mainLayer->getContentSize() / 2;

    // crear capas
    m_menuLayer = CCLayer::create();
    m_menuLayer->setContentSize(m_mainLayer->getContentSize());
    m_mainLayer->addChild(m_menuLayer);

    m_profileLayer = CCLayer::create();
    m_profileLayer->setContentSize(m_mainLayer->getContentSize());
    m_profileLayer->setVisible(false);
    m_mainLayer->addChild(m_profileLayer);

    m_petLayer = CCLayer::create();
    m_petLayer->setContentSize(m_mainLayer->getContentSize());
    m_petLayer->setVisible(false);
    m_mainLayer->addChild(m_petLayer);

    m_layerBgLayer = CCLayer::create();
    m_layerBgLayer->setContentSize(m_mainLayer->getContentSize());
    m_layerBgLayer->setVisible(false);
    m_mainLayer->addChild(m_layerBgLayer);

    // contenido
    m_menuLayer->addChild(this->createMenuTab());
    m_profileLayer->addChild(this->createProfileTab());
    m_petLayer->addChild(this->createPetTab());
    m_layerBgLayer->addChild(this->createLayerBgTab());

    // pestanas
    this->createTabs();

    paimon::markDynamicPopup(this);
    return true;
}

void BackgroundConfigPopup::createTabs() {
    auto winSize = m_mainLayer->getContentSize();
    float topY = winSize.height - 40.f; // bajo titulo
    float centerX = winSize.width / 2;
    float spacing = 90.f;
    float startX = centerX - spacing * 1.5f;

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu);

    char const* tabNames[] = {"Menu", "Profile", "Pet", "Layer Bg"};
    char const* tabIds[] = {"menu-tab-btn", "profile-tab-btn", "pet-tab-btn", "layerbg-tab-btn"};
    for (int i = 0; i < 4; i++) {
        auto spr = ButtonSprite::create(tabNames[i]);
        spr->setScale(0.5f);
        auto tab = CCMenuItemSpriteExtra::create(spr, this, menu_selector(BackgroundConfigPopup::onTab));
        tab->setTag(i);
        tab->setID(tabIds[i]);
        tab->setPosition({startX + spacing * i, topY});
        menu->addChild(tab);
        m_tabs.push_back(tab);
    }

    // inicia pestana
    this->onTab(m_tabs[0]);
}

void BackgroundConfigPopup::onTab(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    int tag = btn->getTag();
    m_selectedTab = tag;
    
    // cambia visibilidad
    m_menuLayer->setVisible(tag == 0);
    m_profileLayer->setVisible(tag == 1);
    m_petLayer->setVisible(tag == 2);
    if (m_layerBgLayer) m_layerBgLayer->setVisible(tag == 3);

    // actualiza visuales
    for (auto tab : m_tabs) {
        auto spr = typeinfo_cast<ButtonSprite*>(tab->getNormalImage());
        if (!spr) continue;
        if (tab->getTag() == tag) {
            spr->setColor({0, 255, 0});
            spr->setOpacity(255);
            tab->setEnabled(false);
        } else {
            spr->setColor({255, 255, 255});
            spr->setOpacity(150);
            tab->setEnabled(true);
        }
    }
}

CCNode* BackgroundConfigPopup::createMenuTab() {
    auto node = CCNode::create();
    auto size = m_mainLayer->getContentSize();
    
    // diseno
    float centerY = size.height / 2;
    float centerX = size.width / 2;

    // seccion fuente
    auto bgSection = paimon::SpriteHelper::createDarkPanel(380, 110, 60);
    bgSection->setPosition({centerX - 190, centerY + 20 - 55});
    node->addChild(bgSection);

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    node->addChild(btnMenu);

    // botones fuente
    createBtn("Custom Image", {centerX - 90, centerY + 40}, menu_selector(BackgroundConfigPopup::onCustomImage), btnMenu);
    createBtn("Random Levels", {centerX + 90, centerY + 40}, menu_selector(BackgroundConfigPopup::onDownloadedThumbnails), btnMenu);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Source Buttons",
            "<cy>Custom Image</c>: open a file picker to select a local\nimage (PNG/JPG/GIF/WEBP) as the menu background.\n\n"
            "<cy>Random Levels</c>: uses a random downloaded thumbnail\nfrom your cache as the background. Changes each restart.\n\n"
            "<cy>Set ID</c>: type a level ID below and use its thumbnail\nas the menu background.", this, 0.3f);
        if (iBtn) {
            iBtn->setPosition({centerX + 180.f, centerY + 40});
            btnMenu->addChild(iBtn);
        }
    }



    // entrada id
    auto inputBg = paimon::SpriteHelper::createDarkPanel(100, 30, 100);
    inputBg->setPosition({centerX - 40 - 50, centerY - 10 - 15});
    node->addChild(inputBg);

    m_idInput = TextInput::create(90, "Level ID");
    m_idInput->setPosition({centerX - 40, centerY - 10});
    m_idInput->setCommonFilter(geode::CommonFilter::Uint);
    m_idInput->setMaxCharCount(10);
    m_idInput->setScale(0.8f);
    node->addChild(m_idInput);
    
    createBtn("Set ID", {centerX + 50, centerY - 10}, menu_selector(BackgroundConfigPopup::onSetID), btnMenu);

    // seccion opciones
    float optionsY = centerY - 70;
    
    // modo oscuro
    bool darkMode = Mod::get()->getSavedValue<bool>("bg-dark-mode", false);
    auto darkToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(BackgroundConfigPopup::onDarkMode), 0.7f);
    darkToggle->toggle(darkMode);
    darkToggle->setPosition({centerX - 100, optionsY});
    btnMenu->addChild(darkToggle);

    auto darkLbl = CCLabelBMFont::create("Dark Mode", "bigFont.fnt");
    darkLbl->setScale(0.4f);
    darkLbl->setPosition({centerX - 100, optionsY + 25});
    node->addChild(darkLbl);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Dark Mode",
            "Adds a dark overlay to the menu background.\n"
            "Useful if the custom image or thumbnail is too bright.\n"
            "Combine with <cy>Intensity</c> slider to adjust.", this, 0.3f);
        if (iBtn) {
            iBtn->setPosition({centerX - 100 + 55.f, optionsY + 25});
            btnMenu->addChild(iBtn);
        }
    }

    // colores adaptativos
    bool adaptive = Mod::get()->getSavedValue<bool>("bg-adaptive-colors", false);
    auto adaptToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(BackgroundConfigPopup::onAdaptiveColors), 0.7f);
    adaptToggle->toggle(adaptive);
    adaptToggle->setPosition({centerX, optionsY});
    btnMenu->addChild(adaptToggle);

    auto adaptLbl = CCLabelBMFont::create("Adaptive Colors", "bigFont.fnt");
    adaptLbl->setScale(0.4f);
    adaptLbl->setPosition({centerX, optionsY + 25});
    node->addChild(adaptLbl);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Adaptive Colors",
            "Extracts dominant colors from the background image\n"
            "and applies them to menu UI elements.\n"
            "Creates a cohesive color theme that matches your background.", this, 0.3f);
        if (iBtn) {
            iBtn->setPosition({centerX + 70.f, optionsY + 25});
            btnMenu->addChild(iBtn);
        }
    }

    // intensidad
    m_slider = Slider::create(this, menu_selector(BackgroundConfigPopup::onIntensityChanged), 0.6f);
    m_slider->setPosition({centerX + 100, optionsY}); // der
    float intensity = Mod::get()->getSavedValue<float>("bg-dark-intensity", 0.5f);
    m_slider->setValue(intensity);
    node->addChild(m_slider); 

    auto intLbl = CCLabelBMFont::create("Intensity", "bigFont.fnt");
    intLbl->setScale(0.4f);
    intLbl->setPosition({centerX + 100, optionsY + 25});
    node->addChild(intLbl);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Intensity",
            "Controls the strength of the Dark Mode overlay.\n"
            "<cy>0</c> = no darkening.\n"
            "<cy>1</c> = maximum darkness.\n"
            "Only visible when Dark Mode is enabled.", this, 0.3f);
        if (iBtn) {
            iBtn->setPosition({centerX + 100 + 40.f, optionsY + 25});
            btnMenu->addChild(iBtn);
        }
    }

    // btn aplicar
    auto applySpr = ButtonSprite::create("Apply Changes", "goldFont.fnt", "GJ_button_01.png", .8f);
    auto applyBtn = CCMenuItemSpriteExtra::create(applySpr, this, menu_selector(BackgroundConfigPopup::onApply));
    applyBtn->setID("apply-changes-btn"_spr);
    applyBtn->setPosition({centerX - 60, 30}); // izq
    btnMenu->addChild(applyBtn);

    // btn por defecto
    auto defSpr = ButtonSprite::create("Default", "goldFont.fnt", "GJ_button_04.png", .7f);
    defSpr->setScale(0.7f);
    auto defBtn = CCMenuItemSpriteExtra::create(defSpr, this, menu_selector(BackgroundConfigPopup::onDefaultMenu));
    defBtn->setID("default-menu-btn"_spr);
    defBtn->setPosition({centerX + 60, 30}); // der del aplicar
    btnMenu->addChild(defBtn);

    return node;
}

void BackgroundConfigPopup::onDefaultMenu(CCObject*) {
    // Escribir en formato unificado (lo que lee MenuLayer)
    LayerBgConfig cfg;
    cfg.type = "default";
    LayerBackgroundManager::get().saveConfig("menu", cfg);
    // Tambien limpiar legacy keys para consistencia
    Mod::get()->setSavedValue("bg-type", std::string("default"));
    Mod::get()->setSavedValue("bg-custom-path", std::string(""));
    Mod::get()->setSavedValue("bg-id", 0);
    paimon::requestDeferredModSave();
    PaimonNotify::create("Menu Reverted to Default", NotificationIcon::Success)->show();
}

void BackgroundConfigPopup::onAdaptiveColors(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    Mod::get()->setSavedValue("bg-adaptive-colors", !toggle->isToggled());
    paimon::requestDeferredModSave();
}

CCMenuItemSpriteExtra* BackgroundConfigPopup::createBtn(char const* text, CCPoint pos, SEL_MenuHandler handler, CCNode* parent) {
    auto spr = ButtonSprite::create(text);
    spr->setScale(0.6f);
    auto btn = CCMenuItemSpriteExtra::create(spr, this, handler);
    btn->setPosition(pos);
    parent->addChild(btn);
    return btn;
}

// menu

void BackgroundConfigPopup::onCustomImage(CCObject*) {
    WeakRef<BackgroundConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto pathStr = geode::utils::string::pathToString(*pathOpt);
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

        // Escribir en formato unificado
        LayerBgConfig cfg = LayerBackgroundManager::get().getConfig("menu");
        cfg.type = "custom";
        cfg.customPath = pathStr;
        LayerBackgroundManager::get().saveConfig("menu", cfg);
        // Tambien legacy para compat
        Mod::get()->setSavedValue<std::string>("bg-type", "custom");
        Mod::get()->setSavedValue<std::string>("bg-custom-path", pathStr);
        paimon::requestDeferredModSave();
        PaimonNotify::create("Custom Menu Image Set", NotificationIcon::Success)->show();
    });
}

void BackgroundConfigPopup::onDownloadedThumbnails(CCObject*) {
    // Escribir en formato unificado
    LayerBgConfig cfg = LayerBackgroundManager::get().getConfig("menu");
    cfg.type = "random";
    LayerBackgroundManager::get().saveConfig("menu", cfg);
    // Tambien legacy para compat
    Mod::get()->setSavedValue("bg-type", std::string("thumbnails"));
    paimon::requestDeferredModSave();
    PaimonNotify::create("Menu set to Random", NotificationIcon::Success)->show();
}

void BackgroundConfigPopup::onSetID(CCObject*) {
    std::string idStr = m_idInput->getString();
    if (idStr.empty()) return;

    if (auto res = geode::utils::numFromString<int>(idStr)) {
        int id = res.unwrap();
        // Escribir en formato unificado
        LayerBgConfig cfg = LayerBackgroundManager::get().getConfig("menu");
        cfg.type = "id";
        cfg.levelId = id;
        LayerBackgroundManager::get().saveConfig("menu", cfg);
        // Tambien legacy para compat
        Mod::get()->setSavedValue("bg-type", std::string("id"));
        Mod::get()->setSavedValue("bg-id", id);
        paimon::requestDeferredModSave();
        PaimonNotify::create("Menu set to Level ID", NotificationIcon::Success)->show();
    } else {
        PaimonNotify::create("Invalid ID", NotificationIcon::Error)->show();
    }
}

void BackgroundConfigPopup::onDarkMode(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    bool dark = !toggle->isToggled();
    // Escribir unificado
    LayerBgConfig cfg = LayerBackgroundManager::get().getConfig("menu");
    cfg.darkMode = dark;
    LayerBackgroundManager::get().saveConfig("menu", cfg);
    // Tambien legacy
    Mod::get()->setSavedValue("bg-dark-mode", dark);
    paimon::requestDeferredModSave();
}

void BackgroundConfigPopup::onIntensityChanged(CCObject* sender) {
    if (m_slider) {
        float intensity = m_slider->getValue();
        // Escribir unificado
        LayerBgConfig cfg = LayerBackgroundManager::get().getConfig("menu");
        cfg.darkIntensity = intensity;
        LayerBackgroundManager::get().saveConfig("menu", cfg);
        // Tambien legacy
        Mod::get()->setSavedValue("bg-dark-intensity", intensity);
        paimon::requestDeferredModSave();
    }
}

void BackgroundConfigPopup::onApply(CCObject*) {
    TransitionManager::get().replaceScene(MenuLayer::scene(false));
    this->onClose(nullptr);
}

// perfil

CCNode* BackgroundConfigPopup::createProfileTab() {
    auto node = CCNode::create();
    auto size = m_mainLayer->getContentSize();
    float centerX = size.width / 2;
    float centerY = size.height / 2;

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    node->addChild(btnMenu);

    // seccion fondo de perfil
    auto bgSection = paimon::SpriteHelper::createDarkPanel(380, 110, 60);
    bgSection->setPosition({centerX - 190, centerY + 20 - 55});
    node->addChild(bgSection);

    // botones
    createBtn("Custom Image", {centerX - 110, centerY + 30}, menu_selector(BackgroundConfigPopup::onProfileCustomImage), btnMenu);
    createBtn("Customize Photo", {centerX + 10, centerY + 30}, menu_selector(BackgroundConfigPopup::onCustomizePhoto), btnMenu);
    createBtn("Clear", {centerX + 120, centerY + 30}, menu_selector(BackgroundConfigPopup::onProfileClear), btnMenu);

    // descripcion
    auto info = CCLabelBMFont::create("Set a custom background image\nfor your profile page.", "chatFont.fnt");
    info->setAlignment(kCCTextAlignmentCenter);
    info->setScale(0.6f);
    info->setColor({200, 200, 200});
    info->setPosition({centerX, centerY - 30});
    node->addChild(info);

    return node;
}

// mascota

CCNode* BackgroundConfigPopup::createPetTab() {
    auto node = CCNode::create();
    auto size = m_mainLayer->getContentSize();
    float centerX = size.width / 2;
    float centerY = size.height / 2;

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    node->addChild(btnMenu);

    // icono mascota
    auto petIcon = CCSprite::createWithSpriteFrameName("GJ_hammerIcon_001.png");
    if (petIcon) {
        petIcon->setScale(1.2f);
        petIcon->setPosition({centerX, centerY + 50});
        node->addChild(petIcon);
    }

    // descripcion
    auto info = CCLabelBMFont::create("Add a cute pet that follows\nyour cursor everywhere!", "chatFont.fnt");
    info->setAlignment(kCCTextAlignmentCenter);
    info->setScale(0.7f);
    info->setColor({200, 200, 200});
    info->setPosition({centerX, centerY + 5});
    node->addChild(info);

    // boton configurar
    auto cfgSpr = ButtonSprite::create("Configure Pet", "goldFont.fnt", "GJ_button_01.png", .8f);
    cfgSpr->setScale(0.7f);
    auto cfgBtn = CCMenuItemSpriteExtra::create(cfgSpr, this, menu_selector(BackgroundConfigPopup::onOpenPetConfig));
    cfgBtn->setID("configure-pet-btn"_spr);
    cfgBtn->setPosition({centerX, centerY - 50});
    btnMenu->addChild(cfgBtn);

    return node;
}

void BackgroundConfigPopup::onOpenPetConfig(CCObject*) {
    auto popup = PetConfigPopup::create();
    if (popup) popup->show();
}

// perfil

void BackgroundConfigPopup::onProfileCustomImage(CCObject*) {
    WeakRef<BackgroundConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto pathStr = geode::utils::string::pathToString(*pathOpt);
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

        Mod::get()->setSavedValue<std::string>("profile-bg-type", "custom");
        Mod::get()->setSavedValue<std::string>("profile-bg-path", pathStr);
        auto res = Mod::get()->saveData();
        if (res.isErr()) {
            PaimonNotify::create("Failed to save settings!", NotificationIcon::Error)->show();
        } else {
            PaimonNotify::create("Profile Background Set!", NotificationIcon::Success)->show();
        }
    });
}

void BackgroundConfigPopup::onCustomizePhoto(CCObject*) {
    auto popup = ProfilePicEditorPopup::create();
    if (popup) popup->show();
}

void BackgroundConfigPopup::onProfileClear(CCObject*) {
    Mod::get()->setSavedValue<std::string>("profile-bg-type", "none");
    Mod::get()->setSavedValue<std::string>("profile-bg-path", "");
    paimon::requestDeferredModSave();
    PaimonNotify::create("Profile Background Cleared", NotificationIcon::Success)->show();
}

// ═══════════════════════════════════════════════════════════
// Layer Background Tab
// ═══════════════════════════════════════════════════════════

CCNode* BackgroundConfigPopup::createLayerBgTab() {
    auto node = CCNode::create();
    auto size = m_mainLayer->getContentSize();
    float cx = size.width / 2;
    float cy = size.height / 2;

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({0, 0});
    node->addChild(btnMenu, 10);

    // ── selector de layer: RowLayout con auto-wrap para que los 8 botones
    // entren dentro del popup (antes "Browser/Search/etc" salian fuera del marco)
    float selY = cy + 55;

    auto selMenu = CCMenu::create();
    selMenu->setContentSize({size.width - 40.f, 50.f});
    selMenu->setAnchorPoint({0.5f, 0.5f});
    selMenu->setPosition({cx, selY});
    selMenu->setLayout(
        RowLayout::create()
            ->setGap(4.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    node->addChild(selMenu, 11);

    for (int i = 0; i < (int)LayerBackgroundManager::LAYER_OPTIONS.size(); i++) {
        auto& [key, name] = LayerBackgroundManager::LAYER_OPTIONS[i];
        // usar nombre corto
        std::string shortName;
        if (key == "creator") shortName = "Creator";
        else if (key == "browser") shortName = "Browser";
        else if (key == "search") shortName = "Search";
        else if (key == "leaderboards") shortName = "Leaderboard";
        else shortName = name;

        auto spr = ButtonSprite::create(shortName.c_str(), "bigFont.fnt", "GJ_button_04.png", .7f);
        spr->setScale(0.45f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(BackgroundConfigPopup::onLayerSelect));
        btn->setTag(i);
        selMenu->addChild(btn);
        m_layerSelectBtns.push_back(btn);
    }
    selMenu->updateLayout();

    // ── botones de accion ──
    float actionY = cy + 10;
    createBtn("Custom Image", {cx - 90, actionY}, menu_selector(BackgroundConfigPopup::onLayerCustomImage), btnMenu);
    createBtn("Random", {cx + 10, actionY}, menu_selector(BackgroundConfigPopup::onLayerRandom), btnMenu);
    createBtn("Same as...", {cx + 100, actionY}, menu_selector(BackgroundConfigPopup::onLayerSameAs), btnMenu);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Layer Backgrounds",
            "<cy>Custom Image</c>: local PNG/JPG/GIF.\n"
            "<cy>Random</c>: random cached thumbnail.\n"
            "<cy>Same as...</c>: copy from another layer.\n"
            "<cy>Set ID</c>: use a level's thumbnail.\n"
            "<cy>Default</c>: original GD background.", this, 0.3f);
        if (iBtn) {
            iBtn->setPosition({cx + 180.f, actionY});
            btnMenu->addChild(iBtn);
        }
    }

    // ── Level ID input ──
    float idY = actionY - 40;
    auto inputBg = paimon::SpriteHelper::createDarkPanel(100, 30, 100);
    inputBg->setPosition({cx - 40 - 50, idY - 15});
    node->addChild(inputBg);

    m_layerIdInput = TextInput::create(90, "Level ID");
    m_layerIdInput->setPosition({cx - 40, idY});
    m_layerIdInput->setCommonFilter(geode::CommonFilter::Uint);
    m_layerIdInput->setMaxCharCount(10);
    m_layerIdInput->setScale(0.8f);
    node->addChild(m_layerIdInput);

    createBtn("Set ID", {cx + 50, idY}, menu_selector(BackgroundConfigPopup::onLayerSetID), btnMenu);

    // ── dark mode ──
    float optY = idY - 45;
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedLayerKey);

    m_layerDarkToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(BackgroundConfigPopup::onLayerDarkMode), 0.6f);
    m_layerDarkToggle->toggle(cfg.darkMode);
    m_layerDarkToggle->setPosition({cx - 80, optY});
    btnMenu->addChild(m_layerDarkToggle);

    auto darkLbl = CCLabelBMFont::create("Dark Mode", "bigFont.fnt");
    darkLbl->setScale(0.35f);
    darkLbl->setPosition({cx - 80, optY + 20});
    node->addChild(darkLbl);

    {
        auto iBtn = PaimonInfo::createInfoBtn("Dark Mode",
            "Adds a dark overlay on top of the layer background.\n"
            "Useful when the image is too bright.\n"
            "Use the <cy>Intensity</c> slider to control how dark it gets.", this, 0.3f);
        if (iBtn) {
            iBtn->setPosition({cx - 80 + 50.f, optY + 20});
            btnMenu->addChild(iBtn);
        }
    }

    // intensity slider
    m_layerDarkSlider = Slider::create(this, menu_selector(BackgroundConfigPopup::onLayerDarkIntensity), 0.5f);
    m_layerDarkSlider->setPosition({cx + 60, optY});
    m_layerDarkSlider->setValue(cfg.darkIntensity);
    node->addChild(m_layerDarkSlider);

    auto intLbl = CCLabelBMFont::create("Intensity", "bigFont.fnt");
    intLbl->setScale(0.35f);
    intLbl->setPosition({cx + 60, optY + 20});
    node->addChild(intLbl);

    // ── boton default (quitar fondo custom) ──
    auto defSpr = ButtonSprite::create("Default (GD)", "goldFont.fnt", "GJ_button_05.png", .7f);
    defSpr->setScale(0.55f);
    auto defBtn = CCMenuItemSpriteExtra::create(defSpr, this, menu_selector(BackgroundConfigPopup::onLayerDefault));
    defBtn->setID("layer-default-btn"_spr);
    defBtn->setPosition({cx, optY - 40});
    btnMenu->addChild(defBtn);

    // inicializar seleccion
    updateLayerSelectButtons();

    return node;
}

void BackgroundConfigPopup::updateLayerSelectButtons() {
    for (int i = 0; i < (int)m_layerSelectBtns.size(); i++) {
        auto btn = m_layerSelectBtns[i];
        auto spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage());
        if (!spr) continue;
        auto& [key, name] = LayerBackgroundManager::LAYER_OPTIONS[i];
        if (key == m_selectedLayerKey) {
            spr->setColor({0, 255, 0});
        } else {
            spr->setColor({255, 255, 255});
        }
    }

    // actualizar toggles/sliders con la config del layer seleccionado
    auto cfg = LayerBackgroundManager::get().getConfig(m_selectedLayerKey);
    if (m_layerDarkToggle) m_layerDarkToggle->toggle(cfg.darkMode);
    if (m_layerDarkSlider) m_layerDarkSlider->setValue(cfg.darkIntensity);
}

void BackgroundConfigPopup::onLayerSelect(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx >= 0 && idx < (int)LayerBackgroundManager::LAYER_OPTIONS.size()) {
        m_selectedLayerKey = LayerBackgroundManager::LAYER_OPTIONS[idx].first;
        updateLayerSelectButtons();
    }
}

void BackgroundConfigPopup::onLayerCustomImage(CCObject*) {
    WeakRef<BackgroundConfigPopup> self = this;
    std::string layerKey = m_selectedLayerKey;
    pt::pickImage([self, layerKey](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto pathStr = geode::utils::string::pathToString(*pathOpt);
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

        LayerBgConfig cfg = LayerBackgroundManager::get().getConfig(layerKey);
        cfg.type = "custom";
        cfg.customPath = pathStr;
        LayerBackgroundManager::get().saveConfig(layerKey, cfg);
        PaimonNotify::create("Background set for layer!", NotificationIcon::Success)->show();
    });
}

void BackgroundConfigPopup::onLayerRandom(CCObject*) {
    LayerBgConfig cfg = LayerBackgroundManager::get().getConfig(m_selectedLayerKey);
    cfg.type = "random";
    LayerBackgroundManager::get().saveConfig(m_selectedLayerKey, cfg);
    PaimonNotify::create("Random background set!", NotificationIcon::Success)->show();
}

void BackgroundConfigPopup::onLayerSameAs(CCObject*) {
    std::string selectedKey = m_selectedLayerKey;
    auto popup = SameAsPickerPopup::create(selectedKey, [selectedKey](std::string const& pickedKey) {
        LayerBgConfig cfg = LayerBackgroundManager::get().getConfig(selectedKey);
        cfg.type = pickedKey;
        LayerBackgroundManager::get().saveConfig(selectedKey, cfg);

        std::string msg = "Using same bg as " + pickedKey + "!";
        PaimonNotify::create(msg.c_str(), NotificationIcon::Success)->show();
    });
    if (popup) popup->show();
}

void BackgroundConfigPopup::onLayerDefault(CCObject*) {
    LayerBgConfig cfg;
    cfg.type = "default";
    LayerBackgroundManager::get().saveConfig(m_selectedLayerKey, cfg);
    PaimonNotify::create("Reverted to default!", NotificationIcon::Success)->show();
}

void BackgroundConfigPopup::onLayerSetID(CCObject*) {
    if (!m_layerIdInput) return;
    std::string idStr = m_layerIdInput->getString();
    if (idStr.empty()) return;

    if (auto res = geode::utils::numFromString<int>(idStr)) {
        LayerBgConfig cfg = LayerBackgroundManager::get().getConfig(m_selectedLayerKey);
        cfg.type = "id";
        cfg.levelId = res.unwrap();
        LayerBackgroundManager::get().saveConfig(m_selectedLayerKey, cfg);
        PaimonNotify::create("Level ID set!", NotificationIcon::Success)->show();
    } else {
        PaimonNotify::create("Invalid ID", NotificationIcon::Error)->show();
    }
}

void BackgroundConfigPopup::onLayerDarkMode(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    LayerBgConfig cfg = LayerBackgroundManager::get().getConfig(m_selectedLayerKey);
    cfg.darkMode = !toggle->isToggled();
    LayerBackgroundManager::get().saveConfig(m_selectedLayerKey, cfg);
}

void BackgroundConfigPopup::onLayerDarkIntensity(CCObject*) {
    if (!m_layerDarkSlider) return;
    LayerBgConfig cfg = LayerBackgroundManager::get().getConfig(m_selectedLayerKey);
    cfg.darkIntensity = m_layerDarkSlider->getValue();
    LayerBackgroundManager::get().saveConfig(m_selectedLayerKey, cfg);
}

