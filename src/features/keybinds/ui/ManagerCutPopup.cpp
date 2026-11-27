#include "ManagerCutPopup.hpp"
#include "../../gestures/GestureManager.hpp"
#include "../../quick-hub/services/QuickHubManager.hpp"
#include "../../volume-scroll/ui/ExtendedKeybindEditPopup.hpp"
#include "../../../utils/ExtendedKeybind.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>

using namespace cocos2d;
using namespace geode::prelude;
namespace kit = paimon::configkit;
using namespace paimon::gestures;

namespace paimon::keybinds {
namespace {

struct KeybindDef {
    char const* key;
    char const* name;
    char const* category;
    bool isVolume = false;
    bool isQuickHubHold = false;
};

constexpr KeybindDef kAllKeybinds[] = {
    // General
    {"settings-panel-keybind",    "Panel de Ajustes",           "General",  false, false},
    {"main-menu-layout-keybind",  "Editor de Layout",           "General",  false, false},
    {"quick-hub-hold-button",     "Quick Hub (Mantener)",       "General",  false, true},
    {"level-search-enter",        "Busqueda Rapida",            "General",  false, false},
    {"paimon-assistant-keybind",  "Asistente Paimon",           "General",  false, false},
    // Captura & Feedback
    {"capture-keybind",           "Capturar Miniatura",         "Captura",  false, false},
    {"capture-menu-keybind",      "Abrir Menu de Captura",      "Captura",  false, false},
    {"feedback-keybind",          "Paimon Feedback",            "Captura",  false, false},
    // Audio & Volumen
    {"volume-music-mod-game",     "Volumen Musica (Juego)",     "Audio",    true,  false},
    {"volume-sfx-mod-game",       "Volumen SFX (Juego)",        "Audio",    true,  false},
    {"volume-music-mod-editor",   "Volumen Musica (Editor)",    "Audio",    true,  false},
    {"volume-sfx-mod-editor",     "Volumen SFX (Editor)",       "Audio",    true,  false},
    // Gameplay (Pause Zoom & Versus)
    {"zoom-in-keybind",           "Pause Zoom: Acercar",        "Gameplay", false, false},
    {"zoom-out-keybind",          "Pause Zoom: Alejar",         "Gameplay", false, false},
    {"zoom-reset-keybind",        "Pause Zoom: Reset",          "Gameplay", false, false},
    {"zoom-toggle-menu-keybind",  "Pause Zoom: Ocultar Menu",   "Gameplay", false, false},
    {"versus-card-1-keybind",     "Versus: Carta 1",            "Gameplay", false, false},
    {"versus-card-2-keybind",     "Versus: Carta 2",            "Gameplay", false, false},
    // Editor
    {"editor-color-picker-keybind", "Editor: Color Picker",     "Editor",   false, false},
    {"gif-import-keybind",          "Editor: GIF to Objects",   "Editor",   false, false},
    {"editor-physics-keybind",      "Editor: Physics Lab",      "Editor",   false, false},
    {"editor-optimizer-keybind",    "Editor: Optimizer",        "Editor",   false, false},
    {"editor-music-keybind",        "Editor: Editor Music",     "Editor",   false, false},
    {"editor-animate-keybind",      "Editor: Paimon Animate",   "Editor",   false, false},
    {"editor-animate-play-key",     "Animate: Play / Pausa",    "Editor",   false, false},
    {"editor-animate-prev-key",     "Animate: Frame Anterior",  "Editor",   false, false},
    {"editor-animate-next-key",     "Animate: Frame Siguiente", "Editor",   false, false},
    {"editor-animate-new-frame-key",   "Animate: Nuevo Frame",  "Editor",   false, false},
    {"editor-animate-blank-frame-key", "Animate: Frame Vacio",  "Editor",   false, false},
};

constexpr char const* kKeyCats[]     = {"Todos", "General", "Captura", "Audio", "Gameplay", "Editor"};
constexpr char const* kGestureCats[] = {"Todos", "General", "Audio", "Captura", "Gameplay"};

std::optional<Keybind> getFirstKeyboardKeybind(char const* settingKey) {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting(settingKey)) return std::nullopt;
    auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(mod->getSetting(settingKey));
    if (!setting) return std::nullopt;
    auto const& binds = setting->getValue();
    if (binds.empty()) return std::nullopt;
    return binds.front();
}

void writeKeyboardKeybind(char const* settingKey, std::optional<Keybind> kb) {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting(settingKey)) return;
    auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(mod->getSetting(settingKey));
    if (!setting) return;

    std::vector<Keybind> newBinds;
    if (kb.has_value() &&
        (kb->key != KEY_None || kb->modifiers != KeyboardModifier::None)) {
        newBinds.push_back(*kb);
    }
    setting->setValue(newBinds);
}

std::string buildKeyBindingLabel(KeybindDef const& def) {
    if (def.isQuickHubHold) {
        if (!paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) return "(desactivado)";
        auto hold = paimon::quickhub::QuickHubManager::holdButton();
        return hold.isEmpty() ? "(sin asignar)" : hold.toDisplayString();
    }
    auto kb = getFirstKeyboardKeybind(def.key);
    auto ext = loadExtendedKeybind(def.key);
    std::string text;
    if (kb.has_value() &&
        (kb->key != KEY_None || kb->modifiers != KeyboardModifier::None)) {
        text = formatKeyboardKeybind(*kb);
    }
    if (!ext.isEmpty()) {
        if (!text.empty()) text += " / ";
        text += ext.toDisplayString();
    }
    if (text.empty()) {
        if (std::string_view(def.key) == "capture-menu-keybind") {
            auto* mod = Mod::get();
            if (mod && mod->hasSetting("capture-menu-rightclick")
                && mod->getSettingValue<bool>("capture-menu-rightclick")) {
                return "Right Click";
            }
        }
        return "(sin asignar)";
    }
    if (def.isVolume) text += " + Scroll";
    return text;
}

// Modal gesture selector popup shown when editing a single gesture action.
class GestureEditPopup : public PaimonPopup {
public:
    using OnPicked = std::function<void()>;

    static GestureEditPopup* create(GestureActionMeta const& meta, OnPicked onPicked) {
        auto ret = new GestureEditPopup();
        if (ret && ret->init(meta, std::move(onPicked))) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

protected:
    bool init(GestureActionMeta const& meta, OnPicked onPicked) {
        auto const win = CCDirector::get()->getWinSize();
        float const W = std::min(385.f, win.width - 36.f);
        float const H = std::min(248.f, win.height - 26.f);
        if (!PaimonPopup::init(W, H)) return false;
        paimon::markDynamicPopup(this);

        m_meta = meta;
        m_onPicked = std::move(onPicked);
        this->setTitle(fmt::format("Gesto: {}", meta.name).c_str(), "goldFont.fnt", 0.56f, 18.f);

        auto content = m_mainLayer->getContentSize();
        auto const current = GestureManager::get().getBinding(meta.id);

        auto* desc = CCLabelBMFont::create(
            fmt::format("{} (Actual: {})", meta.description, GestureManager::kindDisplayName(current)).c_str(),
            "chatFont.fnt");
        desc->setScale(0.42f);
        desc->setColor(paimon::ui::palette::muted);
        desc->setPosition({content.width * 0.5f, content.height - 34.f});
        desc->limitLabelWidth(content.width - 28.f, 0.44f, 0.22f);
        m_mainLayer->addChild(desc);

        float const boxX = 14.f;
        float const boxY = 36.f;
        float const boxW = content.width - 28.f;
        float const boxH = content.height - 78.f;

        auto* inset = paimon::ui::makeInset({boxW + 4.f, boxH + 4.f}, 85);
        inset->setPosition({boxX - 2.f, boxY - 2.f});
        m_mainLayer->addChild(inset, 0);

        auto* scroll = ScrollLayer::create({boxW, boxH});
        scroll->setAnchorPoint({0.f, 0.f});
        scroll->setPosition({boxX, boxY});
        m_mainLayer->addChild(scroll, 1);

        auto const& choices = meta.supportsContinuousDrag
            ? GestureManager::allKinds()
            : GestureManager::discreteKinds();

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});

        constexpr int kCols = 3;
        constexpr float kGap = 6.f;
        constexpr float kCellH = 26.f;
        float const cellW = (boxW - kGap * (kCols + 1)) / kCols;
        int const rows = static_cast<int>((choices.size() + kCols - 1) / kCols);
        float const totalH = std::max(boxH, rows * (kCellH + kGap) + 8.f);

        scroll->m_contentLayer->setContentSize({boxW, totalH});
        menu->setContentSize({boxW, totalH});
        scroll->m_contentLayer->addChild(menu);

        for (size_t i = 0; i < choices.size(); ++i) {
            GestureKind kind = choices[i];
            int r = static_cast<int>(i / kCols);
            int c = static_cast<int>(i % kCols);

            bool const isCurrent = (kind == current);
            auto conflict = GestureManager::get().findConflict(meta.id, kind);

            paimon::ui::Btn skin = paimon::ui::Btn::Gray;
            if (isCurrent) skin = paimon::ui::Btn::Green;
            else if (conflict.has_value()) skin = paimon::ui::Btn::Pink;
            else if (kind == GestureKind::None) skin = paimon::ui::Btn::Red;

            std::string label = GestureManager::kindDisplayName(kind);
            if (conflict.has_value()) label += " *";

            auto* btn = paimon::ui::makeButton(label.c_str(), [this, kind]() {
                this->selectKind(kind);
            }, skin, cellW, 0.42f, "bigFont.fnt");

            float const x = kGap + c * (cellW + kGap) + cellW * 0.5f;
            float const y = totalH - 6.f - r * (kCellH + kGap) - kCellH * 0.5f;
            btn->setPosition({x, y});
            menu->addChild(btn);
        }
        scroll->scrollToTop();

        auto* footerLbl = CCLabelBMFont::create(
            "* En uso por otra accion (tocar para reasignar aqui)",
            "chatFont.fnt");
        footerLbl->setScale(0.38f);
        footerLbl->setColor(paimon::ui::palette::warning);
        footerLbl->setPosition({content.width * 0.5f, 18.f});
        footerLbl->limitLabelWidth(content.width - 28.f, 0.38f, 0.2f);
        m_mainLayer->addChild(footerLbl);

        return true;
    }

    void selectKind(GestureKind kind) {
        auto conflict = GestureManager::get().findConflict(m_meta.id, kind);
        if (conflict.has_value()) {
            // Clear the previous owner cleanly so gestures don't silently overlap.
            GestureManager::get().setBinding(*conflict, GestureKind::None);
        }
        GestureManager::get().setBinding(m_meta.id, kind);
        if (m_onPicked) m_onPicked();
        this->onClose(nullptr);
    }

    GestureActionMeta m_meta{};
    OnPicked m_onPicked;
};

} // namespace

ManagerCutPopup* ManagerCutPopup::create(Mode initialMode) {
    auto ret = new ManagerCutPopup();
    if (ret && ret->init(initialMode)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ManagerCutPopup::init(Mode initialMode) {
    auto const win = CCDirector::get()->getWinSize();
    float const popupW = std::min(430.f, win.width - 26.f);
    float const popupH = std::min(282.f, win.height - 18.f);

    if (!PaimonPopup::init(popupW, popupH)) return false;
    paimon::markDynamicPopup(this);

    m_mode = initialMode;

    this->setTitle("Manager Cut", "goldFont.fnt", 0.68f, 18.f);
    this->addInfoButton("Manager Cut",
        "<cy>Teclas</c> muestra todos los controles de teclado, raton y scroll del mod.\n"
        "<cg>Gestos</c> te permite cambiar a los gestos tactiles y personalizar cada accion "
        "(mantener 1-3 dedos, doble toque, deslizar o arrastrar) ademas de su sensibilidad.\n"
        "Pulsa el boton superior derecho o la barra de modos para cambiar entre Teclas y Gestos.");

    auto content = m_mainLayer->getContentSize();
    float const sideMargin = 14.f;

    m_subtitleLbl = CCLabelBMFont::create("", "chatFont.fnt");
    m_subtitleLbl->setScale(0.4f);
    m_subtitleLbl->setColor(paimon::ui::palette::muted);
    m_subtitleLbl->setAnchorPoint({0.5f, 0.5f});
    m_subtitleLbl->setPosition({content.width * 0.5f, content.height - 32.f});
    m_mainLayer->addChild(m_subtitleLbl, 2);

    float const headerY = content.height - 50.f;
    float const catY = headerY - 22.f;
    float const footerH = 34.f;
    float const listTop = catY - 14.f;

    m_listW = content.width - sideMargin * 2.f;
    m_listH = std::max(80.f, listTop - footerH);

    auto* bg = paimon::ui::makeInset({m_listW + 6.f, m_listH + 6.f}, 90);
    bg->setPosition({sideMargin - 3.f, footerH - 3.f});
    m_mainLayer->addChild(bg, 0);

    m_scrollLayer = ScrollLayer::create({m_listW, m_listH});
    m_scrollLayer->setAnchorPoint({0.f, 0.f});
    m_scrollLayer->setPosition({sideMargin, footerH});
    m_mainLayer->addChild(m_scrollLayer, 2);

    rebuildHeaderControls();
    rebuildFooterControls();
    rebuildList();

    return true;
}

void ManagerCutPopup::setMode(Mode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    m_categoryFilter = 0;
    rebuildHeaderControls();
    rebuildFooterControls();
    rebuildList();
}

void ManagerCutPopup::setCategoryFilter(int index) {
    if (m_categoryFilter == index) return;
    m_categoryFilter = index;
    rebuildHeaderControls();
    rebuildList();
}

void ManagerCutPopup::rebuildHeaderControls() {
    if (m_tabBar) { m_tabBar->removeFromParent(); m_tabBar = nullptr; }
    if (m_catBar) { m_catBar->removeFromParent(); m_catBar = nullptr; }

    auto content = m_mainLayer->getContentSize();
    float const sideMargin = 14.f;
    float const rowW = content.width - sideMargin * 2.f;
    float const headerY = content.height - 50.f;
    float const catY = headerY - 22.f;

    if (m_subtitleLbl) {
        m_subtitleLbl->setString(m_mode == Mode::Keys
            ? "Todos los atajos de teclado y mouse del mod"
            : "Gestos tactiles personalizables y ergonomia movil");
        m_subtitleLbl->limitLabelWidth(rowW, 0.42f, 0.2f);
    }

    int const keyCount = static_cast<int>(std::size(kAllKeybinds));
    int const gestCount = static_cast<int>(GestureManager::actions().size());
    std::vector<std::string> tabs = {
        fmt::format("Teclas ({})", keyCount),
        fmt::format("Gestos ({})", gestCount),
    };
    m_tabBar = kit::makeTabBar(rowW, tabs, m_mode == Mode::Keys ? 0 : 1, [this](int idx) {
        this->setMode(idx == 0 ? Mode::Keys : Mode::Gestures);
    });
    m_tabBar->setPosition({sideMargin, headerY - kit::kTabBarHeight * 0.5f});
    m_mainLayer->addChild(m_tabBar, 5);

    std::vector<std::string> cats;
    if (m_mode == Mode::Keys) {
        for (auto const* c : kKeyCats) cats.emplace_back(c);
    } else {
        for (auto const* c : kGestureCats) cats.emplace_back(c);
    }
    if (m_categoryFilter >= static_cast<int>(cats.size())) m_categoryFilter = 0;

    m_catBar = kit::makeTabBar(rowW, cats, m_categoryFilter, [this](int idx) {
        this->setCategoryFilter(idx);
    });
    m_catBar->setScale(0.88f);
    m_catBar->setPosition({sideMargin + rowW * 0.06f, catY - kit::kTabBarHeight * 0.44f});
    m_mainLayer->addChild(m_catBar, 5);
}

void ManagerCutPopup::rebuildFooterControls() {
    if (m_footerMenu) { m_footerMenu->removeFromParent(); m_footerMenu = nullptr; }

    auto content = m_mainLayer->getContentSize();
    m_footerMenu = CCMenu::create();
    m_footerMenu->setContentSize({content.width - 28.f, 26.f});
    m_footerMenu->ignoreAnchorPointForPosition(false);
    m_footerMenu->setAnchorPoint({0.5f, 0.5f});
    m_footerMenu->setPosition({content.width * 0.5f, 17.f});
    m_footerMenu->setLayout(
        RowLayout::create()
            ->setAutoScale(false)
            ->setGap(12.f)
            ->setAxisAlignment(AxisAlignment::Center));
    m_mainLayer->addChild(m_footerMenu, 5);

    char const* switchLabel = (m_mode == Mode::Keys)
        ? "Cambiar a Gestos"
        : "Cambiar a Teclas";
    m_switchModeBtn = paimon::ui::makeButton(switchLabel, [this]() {
        this->setMode(m_mode == Mode::Keys ? Mode::Gestures : Mode::Keys);
    }, paimon::ui::Btn::Cyan, 132.f, 0.48f, "bigFont.fnt");
    m_footerMenu->addChild(m_switchModeBtn);

    char const* resetLabel = (m_mode == Mode::Keys) ? "Reset Teclas" : "Reset Gestos";
    m_resetBtn = paimon::ui::makeButton(resetLabel, [this]() {
        this->onResetAll();
    }, paimon::ui::Btn::Red, 108.f, 0.48f, "bigFont.fnt");
    m_footerMenu->addChild(m_resetBtn);

    m_footerMenu->updateLayout();
}

void ManagerCutPopup::rebuildList() {
    if (!m_scrollLayer || !m_scrollLayer->m_contentLayer) return;
    auto* content = m_scrollLayer->m_contentLayer;
    content->removeAllChildren();

    std::vector<CCNode*> rows;
    auto addRow = [&](CCNode* n) { if (n) rows.push_back(n); };

    if (m_mode == Mode::Keys) {
        std::string_view wanted = (m_categoryFilter > 0 && m_categoryFilter < static_cast<int>(std::size(kKeyCats)))
            ? std::string_view(kKeyCats[m_categoryFilter])
            : std::string_view{};

        std::string lastCat;
        for (auto const& def : kAllKeybinds) {
            if (!wanted.empty() && wanted != def.category) continue;
            if (lastCat != def.category) {
                lastCat = def.category;
                addRow(makeSectionHeader(def.category, m_listW));
            }
            addRow(makeKeyRow(def.key, def.name, def.isVolume, def.isQuickHubHold, m_listW));
        }
    } else {
        std::string_view wanted = (m_categoryFilter > 0 && m_categoryFilter < static_cast<int>(std::size(kGestureCats)))
            ? std::string_view(kGestureCats[m_categoryFilter])
            : std::string_view{};

        if (wanted.empty() || wanted == "General") {
            addRow(makeSectionHeader("Ergonomia y Confort", m_listW));
            for (auto* c : buildGestureComfortCards(m_listW)) addRow(c);
        }

        auto const& acts = GestureManager::actions();
        std::string lastCat;
        for (size_t i = 0; i < acts.size(); ++i) {
            auto const& a = acts[i];
            if (!wanted.empty() && wanted != a.category) continue;
            if (lastCat != a.category) {
                lastCat = a.category;
                addRow(makeSectionHeader(a.category, m_listW));
            }
            addRow(makeGestureRow(static_cast<int>(i), m_listW));
        }
    }

    constexpr float kRowGap = 3.f;
    float totalH = 4.f;
    for (auto* r : rows) totalH += r->getContentSize().height + kRowGap;
    if (totalH < m_listH) totalH = m_listH;

    content->setContentSize({m_listW, totalH});

    float y = totalH - 2.f;
    for (auto* r : rows) {
        float h = r->getContentSize().height;
        r->setAnchorPoint({0.f, 0.f});
        r->setPosition({0.f, y - h});
        content->addChild(r);
        y -= (h + kRowGap);
    }

    m_scrollLayer->scrollToTop();
}

CCNode* ManagerCutPopup::makeSectionHeader(char const* title, float width) {
    constexpr float kH = 19.f;
    auto* row = CCNode::create();
    row->setContentSize({width, kH});
    row->setAnchorPoint({0.f, 0.f});

    auto* sep = paimon::ui::makeDivider(width - 8.f, paimon::ui::palette::gold, 95);
    sep->setPosition({width * 0.5f, kH - 1.f});
    row->addChild(sep);

    auto* label = CCLabelBMFont::create(title, "goldFont.fnt");
    label->setScale(0.35f);
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({6.f, kH * 0.5f});
    row->addChild(label);
    return row;
}

CCNode* ManagerCutPopup::makeKeyRow(
    std::string const& settingKey,
    std::string const& displayName,
    bool isVolume,
    bool isQuickHubHold,
    float width
) {
    constexpr float kRowH = 26.f;
    auto* mod = Mod::get();
    if (!isQuickHubHold && (!mod || !mod->hasSetting(settingKey))) return nullptr;

    KeybindDef def{settingKey.c_str(), displayName.c_str(), "", isVolume, isQuickHubHold};

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    auto* bg = CCLayerColor::create({255, 255, 255, 13});
    bg->setContentSize({width - 6.f, kRowH - 3.f});
    bg->setPosition({3.f, 1.5f});
    row->addChild(bg, 0);

    auto* nameLbl = CCLabelBMFont::create(displayName.c_str(), "bigFont.fnt");
    nameLbl->setScale(0.35f);
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setPosition({8.f, kRowH * 0.5f});
    nameLbl->limitLabelWidth(width * 0.44f, 0.36f, 0.16f);
    row->addChild(nameLbl, 1);

    std::string bindText = buildKeyBindingLabel(def);
    auto* bindLbl = CCLabelBMFont::create(bindText.c_str(), "chatFont.fnt");
    bindLbl->setAnchorPoint({0.5f, 0.5f});
    bindLbl->setPosition({width * 0.60f, kRowH * 0.5f});
    bindLbl->setColor(bindText == "(sin asignar)"
        ? paimon::ui::palette::dim
        : paimon::ui::palette::info);
    bindLbl->limitLabelWidth(width * 0.25f, 0.46f, 0.22f);
    row->addChild(bindLbl, 1);

    auto* btnMenu = CCMenu::create();
    btnMenu->setContentSize({80.f, kRowH});
    btnMenu->ignoreAnchorPointForPosition(false);
    btnMenu->setAnchorPoint({1.f, 0.5f});
    btnMenu->setPosition({width - 6.f, kRowH * 0.5f});
    btnMenu->setLayout(
        RowLayout::create()
            ->setAutoScale(false)
            ->setGap(4.f)
            ->setAxisAlignment(AxisAlignment::End));

    auto* clrBtn = paimon::ui::makeButton("Off", [this, settingKey, isQuickHubHold]() {
        if (isQuickHubHold) {
            paimon::quickhub::QuickHubManager::setHoldCtrlEnabled(false);
        } else {
            writeKeyboardKeybind(settingKey.c_str(), std::nullopt);
            saveExtendedKeybind(settingKey, ExtendedKeybind{});
            if (settingKey == "capture-menu-keybind") {
                if (auto* m = Mod::get(); m && m->hasSetting("capture-menu-rightclick")) {
                    m->setSettingValue<bool>("capture-menu-rightclick", false);
                }
            }
        }
        this->rebuildList();
    }, paimon::ui::Btn::Gray, 32.f, 0.45f, "bigFont.fnt");
    btnMenu->addChild(clrBtn);

    auto* setBtn = paimon::ui::makeButton("Set", [this, settingKey, displayName, isVolume, isQuickHubHold]() {
        this->openKeyEditor(settingKey, displayName, isVolume, isQuickHubHold);
    }, paimon::ui::Btn::Cyan, 38.f, 0.45f, "bigFont.fnt");
    btnMenu->addChild(setBtn);

    btnMenu->updateLayout();
    row->addChild(btnMenu, 1);

    return row;
}

CCNode* ManagerCutPopup::makeGestureRow(int actionIndex, float width) {
    auto const& acts = GestureManager::actions();
    if (actionIndex < 0 || actionIndex >= static_cast<int>(acts.size())) return nullptr;
    auto const& meta = acts[static_cast<size_t>(actionIndex)];

    constexpr float kRowH = 30.f;
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    auto* bg = CCLayerColor::create({255, 255, 255, 13});
    bg->setContentSize({width - 6.f, kRowH - 3.f});
    bg->setPosition({3.f, 1.5f});
    row->addChild(bg, 0);

    auto* nameLbl = CCLabelBMFont::create(meta.name, "bigFont.fnt");
    nameLbl->setScale(0.35f);
    nameLbl->setAnchorPoint({0.f, 0.5f});
    nameLbl->setPosition({8.f, kRowH * 0.65f});
    nameLbl->limitLabelWidth(width * 0.45f, 0.36f, 0.16f);
    row->addChild(nameLbl, 1);

    auto* descLbl = CCLabelBMFont::create(meta.description, "chatFont.fnt");
    descLbl->setScale(0.36f);
    descLbl->setColor(paimon::ui::palette::muted);
    descLbl->setAnchorPoint({0.f, 0.5f});
    descLbl->setPosition({8.f, kRowH * 0.30f});
    descLbl->limitLabelWidth(width * 0.45f, 0.36f, 0.18f);
    row->addChild(descLbl, 1);

    GestureKind const bound = GestureManager::get().getBinding(meta.id);
    auto* bindLbl = CCLabelBMFont::create(GestureManager::kindDisplayName(bound), "chatFont.fnt");
    bindLbl->setAnchorPoint({0.5f, 0.5f});
    bindLbl->setPosition({width * 0.60f, kRowH * 0.5f});
    bindLbl->setColor(bound == GestureKind::None
        ? paimon::ui::palette::dim
        : paimon::ui::palette::success);
    bindLbl->limitLabelWidth(width * 0.25f, 0.46f, 0.22f);
    row->addChild(bindLbl, 1);

    auto* btnMenu = CCMenu::create();
    btnMenu->setContentSize({80.f, kRowH});
    btnMenu->ignoreAnchorPointForPosition(false);
    btnMenu->setAnchorPoint({1.f, 0.5f});
    btnMenu->setPosition({width - 6.f, kRowH * 0.5f});
    btnMenu->setLayout(
        RowLayout::create()
            ->setAutoScale(false)
            ->setGap(4.f)
            ->setAxisAlignment(AxisAlignment::End));

    auto* offBtn = paimon::ui::makeButton("Off", [this, id = meta.id]() {
        GestureManager::get().setBinding(id, GestureKind::None);
        this->rebuildList();
    }, paimon::ui::Btn::Gray, 32.f, 0.45f, "bigFont.fnt");
    btnMenu->addChild(offBtn);

    auto* setBtn = paimon::ui::makeButton("Set", [this, actionIndex]() {
        this->openGestureEditor(actionIndex);
    }, paimon::ui::Btn::Cyan, 38.f, 0.45f, "bigFont.fnt");
    btnMenu->addChild(setBtn);

    btnMenu->updateLayout();
    row->addChild(btnMenu, 1);

    return row;
}

std::vector<CCNode*> ManagerCutPopup::buildGestureComfortCards(float width) {
    std::vector<CCNode*> out;
    auto const comf = GestureManager::get().comfort();

    out.push_back(kit::makeSliderRow(
        width - 6.f,
        "Tiempo de Hold (Quick Hub / Layout)",
        "Segundos manteniendo antes de activar el gesto",
        comf.holdSeconds,
        0.3,
        3.0,
        [](double v) { return fmt::format("{:.1f}s", v); },
        [](double v) { GestureManager::get().setHoldSeconds(static_cast<float>(v)); }
    ));

    out.push_back(kit::makeSliderRow(
        width - 6.f,
        "Sensibilidad Deslizar / Volumen",
        "Ajusta la facilidad al deslizar o controlar volumen",
        comf.swipeSensitivity,
        0.5,
        2.0,
        [](double v) { return fmt::format("{:.1f}x", v); },
        [](double v) { GestureManager::get().setSwipeSensitivity(static_cast<float>(v)); }
    ));

    out.push_back(kit::makeSliderRow(
        width - 6.f,
        "Tolerancia al mantener dedo quieto",
        "Margen en puntos UI antes de cancelar un hold",
        comf.moveTolerancePt,
        8.0,
        32.0,
        [](double v) { return fmt::format("{}pt", static_cast<int>(std::lround(v))); },
        [](double v) { GestureManager::get().setMoveTolerancePt(static_cast<float>(v)); }
    ));

    out.push_back(kit::makeToggleRow(
        width - 6.f,
        "Indicador visual de gestos",
        "Muestra la barra superior y avisos al activar un gesto",
        comf.hudFeedback,
        [](bool on) { GestureManager::get().setHudFeedback(on); }
    ));

    return out;
}

void ManagerCutPopup::openKeyEditor(
    std::string const& settingKey,
    std::string const& displayName,
    bool isVolume,
    bool isQuickHubHold
) {
    std::optional<Keybind> kb;
    ExtendedKeybind ext;

    if (isQuickHubHold) {
        ext = paimon::quickhub::QuickHubManager::holdButton();
        if (ext.kind == ExtendedKind::Keyboard && ext.key != KEY_None) {
            kb = Keybind{ext.key, ext.modifiers};
        }
    } else {
        kb = getFirstKeyboardKeybind(settingKey.c_str());
        ext = loadExtendedKeybind(settingKey);
        if (settingKey == "capture-menu-keybind"
            && (!kb.has_value() || (kb->key == KEY_None && kb->modifiers == KeyboardModifier::None))
            && ext.isEmpty()) {
            if (auto* m = Mod::get(); m && m->hasSetting("capture-menu-rightclick")
                && m->getSettingValue<bool>("capture-menu-rightclick")) {
                ext.kind = ExtendedKind::Mouse;
                ext.button = MouseButton::Right;
            }
        }
    }

    WeakRef<ManagerCutPopup> weakSelf = this;
    bool const allowScroll = !isVolume && !isQuickHubHold;

    auto* popup = paimon::volscroll::ExtendedKeybindEditPopup::create(
        settingKey,
        displayName,
        kb,
        ext,
        allowScroll,
        [weakSelf, settingKey, isQuickHubHold](
            std::optional<Keybind> newKb,
            ExtendedKeybind newExt
        ) {
            if (isQuickHubHold) {
                ExtendedKeybind chosen{};
                if (newExt.isMouseHold()) {
                    chosen = newExt;
                } else if (newKb.has_value() && newKb->key != KEY_None) {
                    chosen.kind = ExtendedKind::Keyboard;
                    chosen.key = newKb->key;
                    chosen.modifiers = KeyboardModifier::None;
                }
                if (chosen.isEmpty()) {
                    paimon::quickhub::QuickHubManager::setHoldCtrlEnabled(false);
                } else {
                    paimon::quickhub::QuickHubManager::setHoldCtrlEnabled(true);
                    paimon::quickhub::QuickHubManager::setHoldButton(chosen);
                }
            } else {
                writeKeyboardKeybind(settingKey.c_str(), newKb);
                saveExtendedKeybind(settingKey, newExt);
                if (settingKey == "capture-menu-keybind") {
                    bool const hasAny = (newKb.has_value()
                        && (newKb->key != KEY_None || newKb->modifiers != KeyboardModifier::None))
                        || !newExt.isEmpty();
                    bool const isExplicitRight = newExt.kind == ExtendedKind::Mouse
                        && newExt.button == MouseButton::Right
                        && newExt.modifiers.value == 0;
                    if (auto* m = Mod::get(); m && m->hasSetting("capture-menu-rightclick")) {
                        m->setSettingValue<bool>("capture-menu-rightclick", hasAny && isExplicitRight);
                    }
                }
            }
            if (auto self = weakSelf.lock()) {
                self->rebuildList();
            }
        }
    );

    if (popup) popup->show();
}

void ManagerCutPopup::openGestureEditor(int actionIndex) {
    auto const& acts = GestureManager::actions();
    if (actionIndex < 0 || actionIndex >= static_cast<int>(acts.size())) return;

    WeakRef<ManagerCutPopup> weakSelf = this;
    auto* popup = GestureEditPopup::create(acts[static_cast<size_t>(actionIndex)], [weakSelf]() {
        if (auto self = weakSelf.lock()) {
            self->rebuildList();
        }
    });
    if (popup) popup->show();
}

void ManagerCutPopup::onResetAll() {
    WeakRef<ManagerCutPopup> weakSelf = this;
    bool const keysMode = (m_mode == Mode::Keys);
    geode::createQuickPopup(
        keysMode ? "Resetear Teclas" : "Resetear Gestos",
        keysMode
            ? "Restaurar <cy>todas las teclas y botones</c> a su valor por defecto?"
            : "Restaurar <cg>todos los gestos y ajustes de confort</c> a su valor por defecto?",
        "Cancelar",
        "Restaurar",
        [weakSelf, keysMode](auto*, bool confirmed) {
            if (!confirmed) return;
            auto self = weakSelf.lock();
            if (!self) return;
            if (keysMode) {
                auto* mod = Mod::get();
                for (auto const& def : kAllKeybinds) {
                    if (def.isQuickHubHold) {
                        saveExtendedKeybind("quick-hub-hold-button", ExtendedKeybind{});
                        paimon::quickhub::QuickHubManager::setHoldCtrlEnabled(true);
                        continue;
                    }
                    if (mod && mod->hasSetting(def.key)) {
                        if (auto s = cast::typeinfo_pointer_cast<KeybindSettingV3>(mod->getSetting(def.key))) {
                            s->reset();
                        }
                    }
                    saveExtendedKeybind(def.key, ExtendedKeybind{});
                }
                if (mod && mod->hasSetting("capture-menu-rightclick")) {
                    mod->setSettingValue<bool>("capture-menu-rightclick", true);
                }
                PaimonNotify::show("Controles de teclado restaurados", NotificationIcon::Success, 1.5f);
            } else {
                GestureManager::get().resetAllBindings();
                PaimonNotify::show("Gestos restaurados por defecto", NotificationIcon::Success, 1.5f);
            }
            self->rebuildList();
        }
    );
}

} // namespace paimon::keybinds
