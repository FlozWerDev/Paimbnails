#include "PaimonModulesLayer.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/PaimonNotification.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace {

struct ModuleEntry {
    std::string key;          // bool setting key in mod.json
    std::string name;
    std::string desc;
    std::string category;
};

// Every feature that exposes a master on/off bool. Toggling here writes the
// same setting Geode's settings panel does, so the feature reacts live.
std::vector<ModuleEntry> getModules() {
    return {
        // General
        {"mod-previews-enable",      "Mod Previews",            "Imagenes de preview para mods de Geode.",        "General"},
        {"realtime-search-preview",  "Busqueda en Tiempo Real", "Resultados mientras escribes en el buscador.",   "General"},
        {"smooth-scroll",            "Smooth Scroll",           "Scroll suave con la rueda en menus y listas.",   "General"},
        {"auto-update",              "Auto Update",             "Descarga e instala actualizaciones al cerrar.",  "General"},
        {"incognito-mode",           "Modo Incognito",          "Oculta y deja de guardar el historial de busqueda.", "General"},

        // Miniaturas
        {"levelcell-hover-effects",  "Efectos Hover",           "Anima las celdas de nivel al pasar el mouse.",   "Miniaturas"},
        {"compact-list-mode",        "Modo Compacto",           "Celdas mas cortas para ver mas niveles.",        "Miniaturas"},
        {"auto-preview-enable",      "Auto Previews",           "Genera miniatura para niveles que no tienen.",   "Miniaturas"},
        {"enable-thumbnail-taking",  "Boton de Captura",        "Boton para capturar thumbnails en la pausa.",    "Miniaturas"},

        // Nivel
        {"dynamic-song",             "Cancion Dinamica",        "Reproduce la cancion del nivel al ver su info.", "Nivel"},
        {"profile-redesign-enabled", "Rediseno de Perfil",      "Layout moderno de la pagina de perfil.",         "Nivel"},

        // Audio
        {"profile-music-enabled",    "Musica de Perfil",        "Musica custom al ver perfiles.",                 "Audio"},
        {"menuMusicEnable",          "Reproductor de Menu",     "Boton de vinilo con biblioteca y playlists.",    "Audio"},
        {"editorMusicEnable",        "Musica en Editor",        "Tu biblioteca de musica dentro del editor.",     "Audio"},
        {"menuLoopConstantShuffle",  "Menu Loop Shuffle",       "Cambia el menu loop al terminar cada cancion.",  "Audio"},

        // Visual / Extras
        {"smooth-ui-enabled",        "Smooth UI",               "Popups y movimiento de botones suaves.",         "Visual"},
        {"colorful-icons-enabled",   "Paimon Icons",            "Recolorea los iconos con tus colores.",          "Visual"},
        {"global-icons-enabled",     "Global Icons",            "Muestra iconos custom de otros en su perfil.",   "Visual"},
        {"custom-slider-enabled",    "Slider Personalizado",    "Reemplaza el thumb del slider con tu icono.",    "Visual"},
        {"dynamic-popup-enabled",    "Popups Dinamicos",        "Animaciones de entrada/salida en los popups.",   "Visual"},
        {"popup-blur-enabled",       "Blur de Popups",          "Desenfoca el fondo detras de los popups.",       "Visual"},
        {"custom-cursor-enable",     "Cursor Personalizado",    "Reemplaza el cursor del sistema con imagenes.",  "Visual"},
        {"smooth-text-enabled",      "Smooth Text Input",       "Las letras aparecen y desaparecen con animacion al escribir.", "Visual"},

        // Editor / Niveles
        {"song-search-enable",       "Buscar Cancion x Nombre", "Escribe un nombre en la caja de song ID.",       "Editor"},
        {"editor-filters-enable",    "Filtros Mis Niveles",     "Filtra tus niveles creados por varios campos.",  "Editor"},
        {"menu-physics-enable",      "Fisica del Menu",         "Los botones del menu caen, rebotan y se arrastran.", "Editor"},
        {"editor-color-picker-enable","Color Picker (Editor)",  "Cuentagotas para tomar colores en el editor.",   "Editor"},
        {"texture-studio-enabled",   "Texture Studio",          "Generador de texture packs en el menu.",         "Editor"},

        // Notificaciones
        {"mentions-enabled",         "Menciones en Comentarios","Avisa cuando te mencionan en comentarios.",      "Notificaciones"},
        {"msgnotif-enabled",         "Notif. de Mensajes",      "Avisa de mensajes y solicitudes nuevas.",        "Notificaciones"},

        // Discord
        {"discord-rpc-enabled",      "Discord Rich Presence",   "Muestra tu actividad en tu perfil de Discord.",  "Discord"},

        // Rendimiento
        {"enable-disk-cache",        "Cache en Disco",          "Guarda thumbnails en disco para no re-bajarlos.", "Rendimiento"},
        {"gd-robtop-cache-enabled",  "Cache RobTop",            "Cachea respuestas de los servidores de GD.",     "Rendimiento"},
    };
}

constexpr float kRowH = 52.f;
constexpr float kHeaderH = 28.f;

// Warm brown theme that matches the GJ_square01 wood frame. Cards are shades
// of brown; ON rows warm up and keep a coloured category accent so the list
// still reads with "combined colours" against the brown shell.
namespace pal {
    constexpr ccColor4B kBgTop       {112, 74, 44, 255};
    constexpr ccColor4B kBgBottom    {38, 24, 15, 255};

    constexpr ccColor3B kInset       {46, 30, 18};
    constexpr GLubyte    kInsetOpacity = 240;

    constexpr ccColor3B kCardOn      {96, 64, 36};
    constexpr ccColor3B kCardOff     {52, 35, 22};
    constexpr GLubyte    kCardOnOpacity  = 245;
    constexpr GLubyte    kCardOffOpacity = 215;

    constexpr ccColor3B kStateOn     {150, 255, 150};
    constexpr ccColor3B kStateOff    {180, 158, 130};

    constexpr ccColor3B kAccentOff   {120, 92, 60};

    constexpr ccColor3B kCount       {255, 236, 200};
    constexpr ccColor3B kName        {255, 244, 224};
    constexpr ccColor3B kDesc        {214, 190, 162};
}

ccColor3B categoryAccent(std::string const& category) {
    if (category == "Miniaturas")   return {120, 210, 255};
    if (category == "Nivel")        return {170, 190, 255};
    if (category == "Audio")        return {255, 165, 210};
    if (category == "Visual")       return {145, 240, 210};
    if (category == "Editor")       return {255, 190, 105};
    if (category == "Notificaciones") return {255, 140, 145};
    if (category == "Discord")      return {140, 160, 255};
    if (category == "Rendimiento")  return {190, 235, 120};
    return {245, 195, 110};
}

ccColor3B cardColorFor(bool enabled) {
    return enabled ? pal::kCardOn : pal::kCardOff;
}

// Solid brown gradient, no scenery: clean shell behind the wood frame.
void addDynamicBackground(CCLayer* layer) {
    auto win = CCDirector::get()->getWinSize();

    auto grad = CCLayerGradient::create(pal::kBgTop, pal::kBgBottom);
    if (grad) {
        grad->setContentSize(win);
        grad->setVector({0.2f, -1.f});
        layer->addChild(grad, -10);
    } else {
        auto solid = CCLayerColor::create(pal::kBgBottom);
        solid->setContentSize(win);
        layer->addChild(solid, -10);
    }
}

} // namespace

PaimonModulesLayer* PaimonModulesLayer::create() {
    auto ret = new PaimonModulesLayer();
    if (ret && ret->init()) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CCScene* PaimonModulesLayer::scene() {
    auto scene = CCScene::create();
    scene->addChild(PaimonModulesLayer::create());
    return scene;
}

bool PaimonModulesLayer::init() {
    if (!CCLayer::init()) return false;
    this->setKeypadEnabled(true);
    this->setTouchEnabled(true);

    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;
    float cy = winSize.height / 2.f;

    addDynamicBackground(this);

    // Main GD popup frame (blue square panel).
    float panelW = std::min(480.f, winSize.width - 40.f);
    float panelH = winSize.height - 36.f;
    float panelLeft = cx - panelW / 2.f;
    float panelRight = cx + panelW / 2.f;
    float panelTop = cy + panelH / 2.f;
    float panelBot = cy - panelH / 2.f;

    // Native GD popup frame.
    auto frame = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png");
    if (frame) {
        frame->setContentSize({panelW, panelH});
        frame->setPosition({cx, cy});
        this->addChild(frame, 0);
    } else {
        auto fallback = paimon::SpriteHelper::createColorPanel(panelW, panelH, {78, 52, 30}, 245, 8.f);
        fallback->setPosition({panelLeft, panelBot});
        this->addChild(fallback, 0);
    }

    m_menu = CCMenu::create();
    m_menu->setPosition({0, 0});
    this->addChild(m_menu, 20);

    auto title = CCLabelBMFont::create("Modulos", "goldFont.fnt");
    title->setPosition({cx, panelTop - 24.f});
    title->setScale(0.85f);
    this->addChild(title, 10);

    m_countLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_countLabel->setPosition({cx, panelTop - 46.f});
    m_countLabel->setScale(0.4f);
    m_countLabel->setColor(pal::kCount);
    this->addChild(m_countLabel, 10);

    // Back arrow in the top-left, GD style.
    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaimonModulesLayer::onBack));
    backBtn->setPosition({panelLeft - 4.f, panelTop - 2.f});
    m_menu->addChild(backBtn);

    // Bottom action buttons, like a native GD list footer.
    float footerY = panelBot + 28.f;
    float actionScale = panelW < 370.f ? 0.50f : 0.60f;
    float actionOffset = std::min(panelW * 0.24f, 118.f);
    auto allOnSpr = ButtonSprite::create("Activar Todo", "bigFont.fnt", "GJ_button_01.png", .8f);
    allOnSpr->setScale(actionScale);
    auto allOnBtn = CCMenuItemSpriteExtra::create(allOnSpr, this, menu_selector(PaimonModulesLayer::onAllOn));
    allOnBtn->setPosition({cx - actionOffset, footerY});
    m_menu->addChild(allOnBtn);

    auto allOffSpr = ButtonSprite::create("Apagar Todo", "bigFont.fnt", "GJ_button_06.png", .8f);
    allOffSpr->setScale(actionScale);
    auto allOffBtn = CCMenuItemSpriteExtra::create(allOffSpr, this, menu_selector(PaimonModulesLayer::onAllOff));
    allOffBtn->setPosition({cx + actionOffset, footerY});
    m_menu->addChild(allOffBtn);

    // Scroll list region with a subtle inset behind the cards.
    float listTop = panelTop - 58.f;
    float listBot = footerY + 26.f;
    float scrollW = panelW - 30.f;
    float scrollH = listTop - listBot;
    float scrollX = cx - scrollW / 2.f;

    auto listBg = paimon::SpriteHelper::createColorPanel(
        scrollW + 14.f, scrollH + 14.f, pal::kInset, pal::kInsetOpacity, 8.f);
    if (listBg) {
        listBg->setPosition({scrollX - 7.f, listBot - 7.f});
        this->addChild(listBg, 1);
    }

    m_scroll = ScrollLayer::create({scrollW, scrollH});
    m_scroll->setPosition({scrollX, listBot});
    this->addChild(m_scroll, 5);

    buildList();
    refreshCount();

    return true;
}

void PaimonModulesLayer::buildList() {
    auto modules = getModules();
    float scrollW = m_scroll->getContentSize().width;
    float scrollH = m_scroll->getContentSize().height;

    m_keys.clear();
    m_togglers.clear();
    m_accents.clear();
    m_cards.clear();
    m_stateLabels.clear();
    m_accentColors.clear();

    int headerCount = 0;
    std::string lastCat;
    for (auto const& m : modules) {
        if (m.category != lastCat) { headerCount++; lastCat = m.category; }
    }

    float totalH = headerCount * kHeaderH + modules.size() * kRowH + 10.f;
    if (totalH < scrollH) totalH = scrollH;

    auto content = m_scroll->m_contentLayer;
    content->removeAllChildren();
    content->setContentSize({scrollW, totalH});

    auto togMenu = CCMenu::create();
    togMenu->setPosition({0, 0});
    togMenu->setContentSize({scrollW, totalH});
    content->addChild(togMenu, 3);

    float y = totalH;
    lastCat.clear();
    int tag = 0;

    for (auto const& m : modules) {
        if (m.category != lastCat) {
            lastCat = m.category;
            y -= kHeaderH;
            float hcy = y + kHeaderH / 2.f;
            auto accentColor = categoryAccent(m.category);

            auto tick = CCLayerColor::create(ccc4(accentColor.r, accentColor.g, accentColor.b, 255));
            tick->setContentSize({4.f, kHeaderH - 14.f});
            tick->setPosition({11.f, hcy - (kHeaderH - 14.f) / 2.f});
            content->addChild(tick, 2);

            auto header = CCLabelBMFont::create(m.category.c_str(), "goldFont.fnt");
            header->setAnchorPoint({0.f, 0.5f});
            header->setScale(0.5f);
            header->setPosition({22.f, hcy});
            content->addChild(header, 2);

            auto line = CCLayerColor::create(ccc4(255, 255, 255, 28));
            line->setContentSize({scrollW - 24.f, 1.f});
            line->setPosition({12.f, y + 1.f});
            content->addChild(line, 1);
        }

        y -= kRowH;
        float rowCenterY = y + kRowH / 2.f;
        bool on = Mod::get()->getSettingValue<bool>(m.key);
        auto accentColor = categoryAccent(m.category);

        float cardW = scrollW - 6.f;
        float cardH = kRowH - 7.f;
        float cardX = (scrollW - cardW) / 2.f;
        float cardY = y + (kRowH - cardH) / 2.f;

        auto card = paimon::SpriteHelper::createColorPanel(
            cardW, cardH, cardColorFor(on),
            on ? pal::kCardOnOpacity : pal::kCardOffOpacity, 7.f);
        if (card) {
            card->setPosition({cardX, cardY});
            content->addChild(card, 0);
        }

        auto accent = CCLayerColor::create(ccc4(
            on ? accentColor.r : pal::kAccentOff.r,
            on ? accentColor.g : pal::kAccentOff.g,
            on ? accentColor.b : pal::kAccentOff.b, 255));
        accent->setContentSize({4.f, cardH - 14.f});
        accent->setPosition({cardX + 9.f, cardY + 7.f});
        content->addChild(accent, 1);

        bool showStateLabel = cardW >= 365.f;
        float rightReserve = showStateLabel ? 116.f : 76.f;

        auto name = CCLabelBMFont::create(m.name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setScale(0.43f);
        name->limitLabelWidth(cardW - rightReserve, 0.43f, 0.2f);
        name->setColor(pal::kName);
        name->setPosition({cardX + 24.f, rowCenterY + 8.f});
        content->addChild(name, 2);

        auto desc = CCLabelBMFont::create(m.desc.c_str(), "chatFont.fnt");
        desc->setAnchorPoint({0.f, 0.5f});
        desc->setScale(0.48f);
        desc->limitLabelWidth(cardW - rightReserve + 8.f, 0.48f, 0.2f);
        desc->setColor(pal::kDesc);
        desc->setPosition({cardX + 24.f, rowCenterY - 9.f});
        content->addChild(desc, 2);

        CCLabelBMFont* state = nullptr;
        if (showStateLabel) {
            state = CCLabelBMFont::create(on ? "ON" : "OFF", "chatFont.fnt");
            state->setAnchorPoint({1.f, 0.5f});
            state->setScale(0.46f);
            state->setColor(on ? pal::kStateOn : pal::kStateOff);
            state->setPosition({cardX + cardW - 47.f, rowCenterY - 1.f});
            content->addChild(state, 2);
        }

        auto toggler = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(PaimonModulesLayer::onToggle), 0.74f);
        toggler->setPosition({cardX + cardW - 23.f, rowCenterY});
        toggler->setTag(tag);
        toggler->toggle(on);
        togMenu->addChild(toggler);

        m_keys.push_back(m.key);
        m_togglers.push_back(toggler);
        m_accents.push_back(accent);
        m_cards.push_back(card);
        m_stateLabels.push_back(state);
        m_accentColors.push_back(accentColor);
        tag++;
    }

    m_scroll->moveToTop();
}

void PaimonModulesLayer::refreshCount() {
    int on = 0;
    for (auto const& key : m_keys) {
        if (Mod::get()->getSettingValue<bool>(key)) on++;
    }
    m_countLabel->setString(
        fmt::format("{} de {} modulos activos", on, m_keys.size()).c_str());
}

void PaimonModulesLayer::refreshRowVisual(int index, bool enabled) {
    if (index < 0 || index >= static_cast<int>(m_keys.size())) return;

    ccColor3B accentColor = index < static_cast<int>(m_accentColors.size())
        ? m_accentColors[index] : ccColor3B{255, 255, 255};

    if (index < static_cast<int>(m_accents.size()) && m_accents[index]) {
        m_accents[index]->setColor(enabled ? accentColor : pal::kAccentOff);
    }
    if (index < static_cast<int>(m_cards.size()) && m_cards[index]) {
        m_cards[index]->setColor(cardColorFor(enabled));
        m_cards[index]->setOpacity(enabled ? pal::kCardOnOpacity : pal::kCardOffOpacity);
    }
    if (index < static_cast<int>(m_stateLabels.size()) && m_stateLabels[index]) {
        m_stateLabels[index]->setString(enabled ? "ON" : "OFF");
        m_stateLabels[index]->setColor(enabled ? pal::kStateOn : pal::kStateOff);
    }
}

void PaimonModulesLayer::onToggle(CCObject* sender) {
    auto toggler = static_cast<CCMenuItemToggler*>(sender);
    int tag = toggler->getTag();
    if (tag < 0 || tag >= static_cast<int>(m_keys.size())) return;

    auto const& key = m_keys[tag];
    bool newState = !Mod::get()->getSettingValue<bool>(key);
    Mod::get()->setSettingValue<bool>(key, newState);
    refreshRowVisual(tag, newState);
    refreshCount();
}

void PaimonModulesLayer::onAllOn(CCObject*) {
    for (size_t i = 0; i < m_keys.size(); i++) {
        Mod::get()->setSettingValue<bool>(m_keys[i], true);
        refreshRowVisual(static_cast<int>(i), true);
    }
    for (auto* t : m_togglers) t->toggle(true);
    refreshCount();
    PaimonNotify::create("Todos los modulos activados.", NotificationIcon::Success)->show();
}

void PaimonModulesLayer::onAllOff(CCObject*) {
    for (size_t i = 0; i < m_keys.size(); i++) {
        Mod::get()->setSettingValue<bool>(m_keys[i], false);
        refreshRowVisual(static_cast<int>(i), false);
    }
    for (auto* t : m_togglers) t->toggle(false);
    refreshCount();
    PaimonNotify::create("Todos los modulos desactivados.", NotificationIcon::Info)->show();
}

void PaimonModulesLayer::onBack(CCObject*) {
    CCDirector::get()->popScene();
}

void PaimonModulesLayer::keyBackClicked() {
    CCDirector::get()->popScene();
}
