#include "ScrollKeybindsPopup.hpp"
#include "ExtendedKeybindEditPopup.hpp"

#include "../../../utils/ExtendedKeybind.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>

#include <optional>

using namespace cocos2d;
using namespace geode::prelude;
using paimon::keybinds::ExtendedKeybind;
using paimon::keybinds::ExtendedKind;
using paimon::keybinds::loadExtendedKeybind;
using paimon::keybinds::saveExtendedKeybind;

namespace paimon::volscroll {

namespace {
    // Layout
    constexpr float kPopupW    = 380.f;
    constexpr float kPopupH    = 260.f;
    constexpr float kScrollPad = 14.f;
    constexpr float kRowGap    = 3.f;
    constexpr float kHeaderH   = 20.f;
    constexpr float kRowH      = 28.f;
    constexpr float kBottomBarH = 30.f;

    constexpr char const* kMusicGameKey   = "volume-music-mod-game";
    constexpr char const* kSFXGameKey     = "volume-sfx-mod-game";
    constexpr char const* kMusicEditorKey = "volume-music-mod-editor";
    constexpr char const* kSFXEditorKey   = "volume-sfx-mod-editor";

    constexpr char const* kVolumeKeys[] = {
        kMusicGameKey, kSFXGameKey, kMusicEditorKey, kSFXEditorKey
    };

    bool isVolumeKey(std::string_view key) {
        for (auto const* k : kVolumeKeys) {
            if (key == k) return true;
        }
        return false;
    }

    // Lee el primer Keybind de un setting. Retorna nullopt si esta vacio.
    std::optional<Keybind> getFirstKeyboardKeybind(char const* settingKey) {
        auto* mod = Mod::get();
        if (!mod || !mod->hasSetting(settingKey)) return std::nullopt;
        auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(
            mod->getSetting(settingKey));
        if (!setting) return std::nullopt;
        auto const& binds = setting->getValue();
        if (binds.empty()) return std::nullopt;
        return binds.front();
    }

    void writeKeyboardKeybind(char const* settingKey, std::optional<Keybind> kb) {
        auto* mod = Mod::get();
        if (!mod || !mod->hasSetting(settingKey)) return;
        auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(
            mod->getSetting(settingKey));
        if (!setting) return;

        std::vector<Keybind> newBinds;
        if (kb.has_value() &&
            (kb->key != KEY_None || kb->modifiers != KeyboardModifier::None))
        {
            newBinds.push_back(*kb);
        }
        setting->setValue(newBinds);
    }

    std::string buildBindingLabel(
        std::optional<Keybind> const& kb,
        ExtendedKeybind const& ext,
        bool appendScrollHint
    ) {
        std::string text;
        if (kb.has_value() &&
            (kb->key != KEY_None || kb->modifiers != KeyboardModifier::None))
        {
            // formatKeyboardKeybind maneja el caso "solo modificador"
            // (ej. "Ctrl") sin que aparezca "Ctrl+Unknown".
            text = paimon::keybinds::formatKeyboardKeybind(*kb);
        }
        if (!ext.isEmpty()) {
            if (!text.empty()) text += " / ";
            text += ext.toDisplayString();
        }
        if (text.empty()) return "(unset)";
        // Para los rows de volume scroll, hacer evidente la gesture
        // "hold + scroll" mostrando "<bind> + Scroll".
        if (appendScrollHint) {
            text += " + Scroll";
        }
        return text;
    }
}

ScrollKeybindsPopup* ScrollKeybindsPopup::create() {
    auto ret = new ScrollKeybindsPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ScrollKeybindsPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    this->setTitle("Atajos de Teclado");

    auto winSize = m_mainLayer->getContentSize();

    // Botonera inferior (Restaurar volumen)
    auto resetSpr = ButtonSprite::create(
        "Reset Volumen", "bigFont.fnt", "GJ_button_06.png", 0.4f);
    resetSpr->setScale(0.7f);
    auto resetBtn = CCMenuItemSpriteExtra::create(
        resetSpr, this, menu_selector(ScrollKeybindsPopup::onResetVolumeDefaults));
    auto bottomMenu = CCMenu::create();
    bottomMenu->addChild(resetBtn);
    bottomMenu->setPosition({winSize.width / 2.f, 14.f});
    m_mainLayer->addChild(bottomMenu);

    // ScrollLayer central
    float scrollX = kScrollPad;
    float scrollY = kBottomBarH;
    float scrollW = winSize.width - kScrollPad * 2.f;
    float scrollH = winSize.height - kBottomBarH - 38.f;

    if (auto bg = paimon::SpriteHelper::createDarkPanel(scrollW + 6.f, scrollH + 6.f, 80, 6.f)) {
        bg->setPosition({scrollX - 3.f, scrollY - 3.f});
        m_mainLayer->addChild(bg, 0);
    }

    m_scrollLayer = ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({scrollX, scrollY});
    m_mainLayer->addChild(m_scrollLayer, 1);

    auto* content = m_scrollLayer->m_contentLayer;

    // Construir filas
    std::vector<CCNode*> rows;
    auto addRow = [&](CCNode* n) { if (n) rows.push_back(n); };

    addRow(makeSectionHeader("Scroll de Volumen — Juego", scrollW));
    addRow(makeKeybindRow(kMusicGameKey, "Music Volume", scrollW, /*allowScroll=*/false));
    addRow(makeKeybindRow(kSFXGameKey,   "SFX Volume",   scrollW, /*allowScroll=*/false));

    addRow(makeSectionHeader("Scroll de Volumen — Editor", scrollW));
    addRow(makeKeybindRow(kMusicEditorKey, "Music Volume", scrollW, /*allowScroll=*/false));
    addRow(makeKeybindRow(kSFXEditorKey,   "SFX Volume",   scrollW, /*allowScroll=*/false));

    addRow(makeSectionHeader("Captura", scrollW));
    addRow(makeKeybindRow("capture-keybind", "Capturar", scrollW, /*allowScroll=*/true));

    addRow(makeSectionHeader("Pause Zoom", scrollW));
    addRow(makeKeybindRow("zoom-in-keybind",          "Zoom In",      scrollW, true));
    addRow(makeKeybindRow("zoom-out-keybind",         "Zoom Out",     scrollW, true));
    addRow(makeKeybindRow("zoom-reset-keybind",       "Zoom Reset",   scrollW, true));
    addRow(makeKeybindRow("zoom-toggle-menu-keybind", "Toggle Menu",  scrollW, true));

    addRow(makeSectionHeader("General", scrollW));
    addRow(makeKeybindRow("settings-panel-keybind",   "Settings Panel",   scrollW, true));
    addRow(makeKeybindRow("main-menu-layout-keybind", "Layout Editor",    scrollW, true));
    addRow(makeKeybindRow("level-search-enter",       "Quick Search",     scrollW, true));

    // Layout vertical
    float totalH = 0.f;
    for (auto* r : rows) {
        totalH += r->getContentSize().height + kRowGap;
    }
    if (totalH < scrollH) totalH = scrollH;

    content->setContentSize({scrollW, totalH});

    float y = totalH;
    for (auto* r : rows) {
        float h = r->getContentSize().height;
        r->setAnchorPoint({0.f, 0.f});
        r->setPosition({0.f, y - h});
        content->addChild(r);
        y -= (h + kRowGap);
    }

    m_scrollLayer->scrollToTop();

    return true;
}

void ScrollKeybindsPopup::onExit() {
    m_keybindNodes.clear();
    Popup::onExit();
}

CCNode* ScrollKeybindsPopup::makeSectionHeader(char const* title, float width) {
    auto row = CCNode::create();
    row->setContentSize({width, kHeaderH});
    row->setAnchorPoint({0.f, 0.f});

    auto sep = CCLayerColor::create({255, 255, 255, 35});
    sep->setContentSize({width - 6.f, 1.f});
    sep->setPosition({3.f, kHeaderH - 1.f});
    row->addChild(sep);

    auto label = CCLabelBMFont::create(title, "goldFont.fnt");
    label->setScale(0.36f);
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({6.f, kHeaderH / 2.f});
    row->addChild(label);

    return row;
}

CCNode* ScrollKeybindsPopup::makeKeybindRow(char const* settingKey, float width) {
    return makeKeybindRow(settingKey, settingKey, width, /*allowScroll=*/true);
}

CCNode* ScrollKeybindsPopup::makeKeybindRow(
    char const* settingKey,
    char const* displayName,
    float width,
    bool allowScroll
) {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting(settingKey)) return nullptr;

    auto row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowH});

    // Fondo sutil para distinguir cada fila
    auto rowBg = CCLayerColor::create({255, 255, 255, 12});
    rowBg->setContentSize({width - 6.f, kRowH - 4.f});
    rowBg->setPosition({3.f, 2.f});
    row->addChild(rowBg, 0);

    // Nombre
    auto nameLabel = CCLabelBMFont::create(displayName, "bigFont.fnt");
    nameLabel->setScale(0.38f);
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->setPosition({8.f, kRowH / 2.f});
    nameLabel->limitLabelWidth(width * 0.42f, 0.42f, 0.18f);
    row->addChild(nameLabel, 1);

    // Label del binding actual
    auto kb = getFirstKeyboardKeybind(settingKey);
    auto ext = loadExtendedKeybind(settingKey);
    bool const isVolumeRow = isVolumeKey(settingKey);
    auto bindingText = buildBindingLabel(kb, ext, /*appendScrollHint=*/isVolumeRow);

    auto bindingLabel = CCLabelBMFont::create(bindingText.c_str(), "chatFont.fnt");
    bindingLabel->setScale(0.48f);
    bindingLabel->setAnchorPoint({0.5f, 0.5f});
    bindingLabel->setPosition({width * 0.62f, kRowH / 2.f});
    bindingLabel->limitLabelWidth(width * 0.34f, 0.5f, 0.26f);
    row->addChild(bindingLabel, 1);

    // Boton "Set"
    auto setSpr = ButtonSprite::create("Set", "bigFont.fnt", "GJ_button_01.png", 0.45f);
    setSpr->setScale(0.5f);

    // Captura por valor para usar dentro del callback
    std::string keyCopy = settingKey;
    std::string nameCopy = displayName;

    auto setBtn = CCMenuItemExt::createSpriteExtra(
        setSpr,
        [this, keyCopy, nameCopy, allowScroll, bindingLabel](CCMenuItemSpriteExtra*) {
            this->openEditPopup(keyCopy, nameCopy, allowScroll, bindingLabel);
        }
    );

    auto btnMenu = CCMenu::create();
    btnMenu->addChild(setBtn);
    btnMenu->setContentSize({44.f, kRowH});
    btnMenu->setAnchorPoint({1.f, 0.5f});
    btnMenu->setPosition({width - 8.f, kRowH / 2.f});
    btnMenu->setLayout(RowLayout::create()->setAxisAlignment(AxisAlignment::End));
    btnMenu->updateLayout();
    row->addChild(btnMenu, 1);

    // Guardamos el label y el key del setting en el row para poder
    // refrescarlos despues sin reabrir el popup.
    row->setUserObject("paimon-binding-label"_spr, bindingLabel);
    row->setUserObject("paimon-binding-key"_spr, CCString::create(settingKey));

    return row;
}

void ScrollKeybindsPopup::openEditPopup(
    std::string settingKey,
    std::string displayName,
    bool allowScroll,
    cocos2d::CCLabelBMFont* labelToRefresh
) {
    auto kb = getFirstKeyboardKeybind(settingKey.c_str());
    auto ext = loadExtendedKeybind(settingKey);

    auto labelRef = Ref<CCLabelBMFont>(labelToRefresh);

    auto popup = ExtendedKeybindEditPopup::create(
        settingKey,
        displayName,
        kb,
        ext,
        allowScroll,
        [settingKey, labelRef](
            std::optional<Keybind> newKb,
            ExtendedKeybind newExt
        ) {
            writeKeyboardKeybind(settingKey.c_str(), newKb);
            saveExtendedKeybind(settingKey, newExt);

            if (auto* l = labelRef.data()) {
                bool const isVolumeRow = isVolumeKey(settingKey);
                auto text = buildBindingLabel(newKb, newExt, /*appendScrollHint=*/isVolumeRow);
                l->setString(text.c_str());
            }
        }
    );

    if (popup) popup->show();
}

void ScrollKeybindsPopup::onResetVolumeDefaults(CCObject*) {
    auto* mod = Mod::get();

    // Reset cada keybind del scroll de volumen al default de mod.json y
    // borrar cualquier extended bind asociado.
    for (auto const* key : kVolumeKeys) {
        if (!mod->hasSetting(key)) continue;
        auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(
            mod->getSetting(key));
        if (setting) {
            setting->reset();
        }
        // Limpiar el extended bind tambien.
        saveExtendedKeybind(key, ExtendedKeybind{});
    }

    // Refrescar los labels visibles. Recorremos las filas dentro del
    // scrollLayer (cada fila guarda su label en un userObject conocido)
    // y reescribimos su texto desde el storage actualizado.
    if (m_scrollLayer && m_scrollLayer->m_contentLayer) {
        auto children = m_scrollLayer->m_contentLayer->getChildren();
        if (children) {
            for (int i = 0; i < static_cast<int>(children->count()); ++i) {
                auto* row = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
                if (!row) continue;
                auto* label = typeinfo_cast<CCLabelBMFont*>(
                    row->getUserObject("paimon-binding-label"_spr));
                if (!label) continue;
                // Buscar el setting key asociado tampoco es trivial sin
                // guardarlo en el row, asi que solo refrescamos los 4
                // volumes (los demas no son afectados por reset).
                //
                // Para simplificar, reescribimos TODOS los labels de la
                // lista — leen del storage actual asi que es seguro.
                //
                // Nos hace falta saber a que setting pertenece este label.
                // Lo guardamos como otro userObject "paimon-binding-key".
                auto* keyObj = typeinfo_cast<CCString*>(
                    row->getUserObject("paimon-binding-key"_spr));
                if (!keyObj) continue;
                std::string settingKey = keyObj->getCString();
                auto kb = getFirstKeyboardKeybind(settingKey.c_str());
                auto ext = loadExtendedKeybind(settingKey);
                bool const isVolumeRow = isVolumeKey(settingKey);
                label->setString(buildBindingLabel(kb, ext, /*appendScrollHint=*/isVolumeRow).c_str());
            }
        }
    }

    Notification::create("Volume keybinds reset", NotificationIcon::Success, 1.5f)->show();
}

void ScrollKeybindsPopup::reopenAfterReset(float) {
    // No usado actualmente — refrescamos los labels in-place. Se mantiene
    // como punto de extension futuro.
}

} // namespace paimon::volscroll
