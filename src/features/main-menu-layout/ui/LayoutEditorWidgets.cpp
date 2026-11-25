#include "LayoutEditorWidgets.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menu_layout {

void CanvasSettings::load() {
    auto* mod = Mod::get();
    gridOn = mod->getSavedValue<bool>("main-menu-layout-grid", false);
    snapEnabled = mod->getSavedValue<bool>("main-menu-layout-snap-grid", false);
    snapToGrid = mod->getSavedValue<bool>("main-menu-layout-snap-to-grid", true);
    smartGuides = mod->getSavedValue<bool>("main-menu-layout-smart-guides", true);
    safeZone = mod->getSavedValue<bool>("main-menu-layout-safe-zone", false);
    showHiddenOutlines = mod->getSavedValue<bool>("main-menu-layout-hidden-outlines", true);
    autoHideHud = mod->getSavedValue<bool>("main-menu-layout-auto-ghost", true);
    gridStep = std::clamp(static_cast<float>(mod->getSavedValue<int64_t>("main-menu-layout-grid-step", 24)), 4.f, 96.f);
    snapDistance = std::clamp(static_cast<float>(mod->getSavedValue<int64_t>("main-menu-layout-snap-distance", 10)), 1.f, 48.f);
}

void CanvasSettings::save() const {
    auto* mod = Mod::get();
    mod->setSavedValue<bool>("main-menu-layout-grid", gridOn);
    mod->setSavedValue<bool>("main-menu-layout-snap-grid", snapEnabled);
    mod->setSavedValue<bool>("main-menu-layout-snap-to-grid", snapToGrid);
    mod->setSavedValue<bool>("main-menu-layout-smart-guides", smartGuides);
    mod->setSavedValue<bool>("main-menu-layout-safe-zone", safeZone);
    mod->setSavedValue<bool>("main-menu-layout-hidden-outlines", showHiddenOutlines);
    mod->setSavedValue<bool>("main-menu-layout-auto-ghost", autoHideHud);
    mod->setSavedValue<int64_t>("main-menu-layout-grid-step", static_cast<int64_t>(std::round(gridStep)));
    mod->setSavedValue<int64_t>("main-menu-layout-snap-distance", static_cast<int64_t>(std::round(snapDistance)));
}

namespace studio_ui {
namespace {
    std::string cleanIdentifier(std::string raw) {
        if (auto pos = raw.find_last_of('/'); pos != std::string::npos) {
            raw = raw.substr(pos + 1);
        }
        auto eraseSuffix = [&](std::string const& suf) {
            if (raw.size() > suf.size() && raw.compare(raw.size() - suf.size(), suf.size(), suf) == 0) {
                raw.erase(raw.size() - suf.size());
            }
        };
        eraseSuffix("-button");
        eraseSuffix("_button");
        eraseSuffix("-btn");
        eraseSuffix("_btn");
        eraseSuffix("_001.png");
        eraseSuffix(".png");
        if (raw.rfind("flozwer.paimbnails2/", 0) == 0) {
            raw.erase(0, 20);
        }

        std::string out;
        out.reserve(raw.size());
        bool capNext = true;
        for (char c : raw) {
            if (c == '-' || c == '_' || c == ' ') {
                if (!out.empty() && out.back() != ' ') out.push_back(' ');
                capNext = true;
            } else {
                out.push_back(capNext ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
                capNext = false;
            }
        }
        return out.empty() ? "Item" : out;
    }

    std::string friendlyKnownId(std::string const& id, bool es) {
        static std::unordered_map<std::string, std::pair<char const*, char const*>> const kMap = {
            {"play-button", {"Play", "Jugar"}},
            {"garage-button", {"Garage / Icons", "Garage / Iconos"}},
            {"editor-button", {"Create / Editor", "Crear / Editor"}},
            {"profile-button", {"Profile", "Perfil"}},
            {"player-username", {"Username", "Usuario"}},
            {"main-title", {"GD Title", "Titulo GD"}},
            {"daily-chest-button", {"Daily Chest", "Cofre Diario"}},
            {"settings-button", {"Settings", "Ajustes"}},
            {"achievements-button", {"Achievements", "Logros"}},
            {"stats-button", {"Stats", "Estadisticas"}},
            {"newgrounds-button", {"Newgrounds", "Newgrounds"}},
            {"robtop-logo-button", {"RobTop Logo", "Logo RobTop"}},
            {"more-games-button", {"More Games", "Mas Juegos"}},
            {"facebook-button", {"Facebook", "Facebook"}},
            {"twitter-button", {"Twitter / X", "Twitter / X"}},
            {"youtube-button", {"YouTube", "YouTube"}},
            {"twitch-button", {"Twitch", "Twitch"}},
            {"discord-button", {"Discord", "Discord"}},
            {"close-button", {"Exit Game", "Salir"}},
            {"back-button", {"Back", "Atras"}},
            {"retry-button", {"Restart", "Reiniciar"}},
            {"practice-button", {"Practice", "Practica"}},
            {"exit-button", {"Exit Level", "Salir Nivel"}},
            {"edit-button", {"Edit Level", "Editar Nivel"}},
            {"full-restart-button", {"Full Restart", "Reinicio Total"}},
            {"options-button", {"Options", "Opciones"}},
            {"music-slider", {"Music Volume", "Vol. Musica"}},
            {"sfx-slider", {"SFX Volume", "Vol. SFX"}},
            {"progress-bar", {"Progress Bar", "Barra Progreso"}},
            {"normal-progress-bar", {"Normal Bar", "Barra Normal"}},
            {"practice-progress-bar", {"Practice Bar", "Barra Practica"}},
            {"coin-container", {"Coins", "Monedas"}},
            {"startpos-container", {"StartPos", "StartPos"}},
        };
        auto it = kMap.find(id);
        if (it != kMap.end()) {
            return es ? it->second.second : it->second.first;
        }
        return "";
    }

    class NumericInputPopup : public PaimonPopup {
    public:
        static NumericInputPopup* create(
            std::string const& title,
            float currentValue,
            float minValue,
            float maxValue,
            bool integerOnly,
            std::function<void(float)> onApply) {
            auto* ret = new NumericInputPopup();
            if (ret && ret->init(title, currentValue, minValue, maxValue, integerOnly, std::move(onApply))) {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

    protected:
        bool init(
            std::string const& title,
            float currentValue,
            float minValue,
            float maxValue,
            bool integerOnly,
            std::function<void(float)> onApply) {
            if (!PaimonPopup::init(244.f, 134.f)) return false;
            m_min = minValue;
            m_max = maxValue;
            m_integerOnly = integerOnly;
            m_onApply = std::move(onApply);

            this->setTitle(title, "goldFont.fnt", 0.54f, 18.f);

            auto const sz = m_mainLayer->getContentSize();
            std::string initial = integerOnly
                ? fmt::format("{}", static_cast<int>(std::round(currentValue)))
                : fmt::format("{:.2f}", currentValue);

            m_input = TextInput::create(180.f, "0", "bigFont.fnt");
            m_input->setFilter("0123456789.-");
            m_input->setMaxCharCount(12);
            m_input->setString(initial);
            m_input->setPosition({ sz.width * 0.5f, sz.height * 0.5f + 6.f });
            m_mainLayer->addChild(m_input, 4);

            auto* menu = paimon::ui::makeZeroMenu("numeric-input-menu"_spr);
            m_mainLayer->addChild(menu, 5);

            auto* cancelBtn = paimon::ui::makeButton(
                isSpanish() ? "Cancelar" : "Cancel",
                [this] { this->onClose(nullptr); },
                paimon::ui::Btn::Gray, 82.f, 0.54f, "goldFont.fnt");
            cancelBtn->setPosition({ sz.width * 0.5f - 48.f, 25.f });
            menu->addChild(cancelBtn);

            auto* applyBtn = paimon::ui::makeButton(
                isSpanish() ? "Aplicar" : "Apply",
                [this] { this->commit(); },
                paimon::ui::Btn::Green, 82.f, 0.54f, "goldFont.fnt");
            applyBtn->setPosition({ sz.width * 0.5f + 48.f, 25.f });
            menu->addChild(applyBtn);

            paimon::markDynamicPopup(this);
            return true;
        }

        void commit() {
            if (m_input && m_onApply) {
                std::string raw = m_input->getString();
                if (!raw.empty()) {
                    char* endPtr = nullptr;
                    double parsed = std::strtod(raw.c_str(), &endPtr);
                    if (endPtr != raw.c_str() && std::isfinite(parsed)) {
                        float val = std::clamp(static_cast<float>(parsed), m_min, m_max);
                        if (m_integerOnly) val = std::round(val);
                        m_onApply(val);
                    }
                }
            }
            this->onClose(nullptr);
        }

        float m_min = 0.f;
        float m_max = 100.f;
        bool m_integerOnly = false;
        TextInput* m_input = nullptr;
        std::function<void(float)> m_onApply;
    };

    class CanvasSettingsPopup : public PaimonPopup {
    public:
        static CanvasSettingsPopup* create(
            CanvasSettings const& current,
            std::function<void(CanvasSettings const&)> onChanged) {
            auto* ret = new CanvasSettingsPopup();
            if (ret && ret->init(current, std::move(onChanged))) {
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

    protected:
        bool init(CanvasSettings const& current, std::function<void(CanvasSettings const&)> onChanged) {
            if (!PaimonPopup::init(348.f, 268.f)) return false;
            m_settings = current;
            m_onChanged = std::move(onChanged);

            bool const es = isSpanish();
            this->setTitle(es ? "Opciones de Lienzo" : "Canvas Settings", "goldFont.fnt", 0.6f, 18.f);

            auto const sz = m_mainLayer->getContentSize();
            auto* card = paimon::ui::makeInset({ sz.width - 28.f, sz.height - 56.f }, 95);
            card->setPosition({ 14.f, 16.f });
            m_mainLayer->addChild(card, 1);

            auto* menu = paimon::ui::makeZeroMenu("canvas-settings-menu"_spr);
            m_mainLayer->addChild(menu, 5);

            float y = sz.height - 56.f;
            auto* stepLbl = CCLabelBMFont::create(es ? "Cuadricula (px):" : "Grid Step (px):", "bigFont.fnt");
            stepLbl->setScale(0.32f);
            stepLbl->setAnchorPoint({ 0.f, 0.5f });
            stepLbl->setPosition({ 24.f, y });
            m_mainLayer->addChild(stepLbl, 3);

            static constexpr std::array<int, 6> kSteps = { 8, 12, 16, 24, 32, 48 };
            float px = 152.f;
            for (int step : kSteps) {
                bool active = std::abs(m_settings.gridStep - step) < 0.5f;
                auto* btn = makeGdButton(
                    fmt::format("{}", step), 26.f, 18.f,
                    active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray,
                    [this, step] {
                        m_settings.gridStep = static_cast<float>(step);
                        this->refreshStepButtons();
                        this->emit();
                    });
                btn->setPosition({ px, y });
                menu->addChild(btn);
                m_stepButtons.emplace_back(step, btn);
                px += 29.f;
            }

            y -= 28.f;
            auto* snapLbl = CCLabelBMFont::create(es ? "Rango Iman:" : "Snap Range:", "bigFont.fnt");
            snapLbl->setScale(0.32f);
            snapLbl->setAnchorPoint({ 0.f, 0.5f });
            snapLbl->setPosition({ 24.f, y });
            m_mainLayer->addChild(snapLbl, 3);

            static constexpr std::array<int, 5> kSnaps = { 4, 8, 10, 14, 20 };
            px = 152.f;
            for (int dist : kSnaps) {
                bool active = std::abs(m_settings.snapDistance - dist) < 0.5f;
                auto* btn = makeGdButton(
                    fmt::format("{}", dist), 30.f, 18.f,
                    active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray,
                    [this, dist] {
                        m_settings.snapDistance = static_cast<float>(dist);
                        this->refreshSnapButtons();
                        this->emit();
                    });
                btn->setPosition({ px, y });
                menu->addChild(btn);
                m_snapButtons.emplace_back(dist, btn);
                px += 34.f;
            }

            auto addToggleRow = [&](float rowY, char const* labelText, bool initial, std::function<void(bool)> onToggle) {
                auto* lbl = CCLabelBMFont::create(labelText, "chatFont.fnt");
                lbl->setScale(0.56f);
                lbl->setColor(kTextPrimary);
                lbl->setAnchorPoint({ 0.f, 0.5f });
                lbl->setPosition({ 24.f, rowY });
                m_mainLayer->addChild(lbl, 3);

                auto* tog = paimon::ui::makeToggle(initial, [this, fn = std::move(onToggle)](bool v) {
                    fn(v);
                    this->emit();
                }, 0.65f);
                tog->setPosition({ sz.width - 36.f, rowY });
                menu->addChild(tog);
            };

            addToggleRow(y - 30.f, es ? "Ajustar a cuadricula (cuando Snap esta ON)" : "Snap to grid (when Snap is ON)",
                m_settings.snapToGrid, [this](bool v) { m_settings.snapToGrid = v; });
            addToggleRow(y - 56.f, es ? "Guias inteligentes (cuando Snap esta ON)" : "Smart guides (when Snap is ON)",
                m_settings.smartGuides, [this](bool v) { m_settings.smartGuides = v; });
            addToggleRow(y - 82.f, es ? "Marco de zona segura" : "Safe zone guide frame",
                m_settings.safeZone, [this](bool v) { m_settings.safeZone = v; });
            addToggleRow(y - 108.f, es ? "Mostrar cajas de elementos ocultos" : "Show outlines on hidden items",
                m_settings.showHiddenOutlines, [this](bool v) { m_settings.showHiddenOutlines = v; });
            addToggleRow(y - 134.f, es ? "Ocultar toda la UI al mover objetos" : "Hide all HUD while dragging items",
                m_settings.autoHideHud, [this](bool v) { m_settings.autoHideHud = v; });

            paimon::markDynamicPopup(this);
            return true;
        }

        void refreshStepButtons() {
            for (auto const& [step, btn] : m_stepButtons) {
                bool active = std::abs(m_settings.gridStep - step) < 0.5f;
                setGdButtonState(btn, active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
            }
        }

        void refreshSnapButtons() {
            for (auto const& [dist, btn] : m_snapButtons) {
                bool active = std::abs(m_settings.snapDistance - dist) < 0.5f;
                setGdButtonState(btn, active ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
            }
        }

        void emit() {
            m_settings.save();
            if (m_onChanged) m_onChanged(m_settings);
        }

        CanvasSettings m_settings;
        std::function<void(CanvasSettings const&)> m_onChanged;
        std::vector<std::pair<int, CCMenuItemSpriteExtra*>> m_stepButtons;
        std::vector<std::pair<int, CCMenuItemSpriteExtra*>> m_snapButtons;
    };
}

bool isSpanish() {
    return Localization::get().getLanguage() == Localization::Language::SPANISH;
}

FriendlyElementInfo describeButtonElement(EditableMenuButton const& eb) {
    bool const es = isSpanish();
    std::string nodeId = eb.node ? std::string(eb.node->getID()) : std::string();
    std::string menuId = eb.menu ? std::string(eb.menu->getID()) : std::string();

    if (eb.node && typeinfo_cast<CCLabelBMFont*>(eb.node.data()) && !eb.menu) {
        std::string known = friendlyKnownId(nodeId, es);
        if (!known.empty()) {
            return { known, es ? "Texto" : "Text", ElementCategory::Label };
        }
        std::string text = eb.label;
        if (text.rfind("Label: ", 0) == 0) text.erase(0, 7);
        if (text.empty() && !nodeId.empty()) text = cleanIdentifier(nodeId);
        return { text.empty() ? (es ? "Texto" : "Label") : text, es ? "Texto" : "Label", ElementCategory::Label };
    }

    if (eb.node && typeinfo_cast<CCSprite*>(eb.node.data()) && !eb.menu) {
        std::string known = friendlyKnownId(nodeId, es);
        if (!known.empty()) {
            return { known, es ? "Arte" : "Sprite", ElementCategory::Sprite };
        }
        std::string text = eb.label;
        if (text.rfind("Sprite: ", 0) == 0) text.erase(0, 8);
        return { cleanIdentifier(text.empty() ? nodeId : text), es ? "Arte" : "Sprite", ElementCategory::Sprite };
    }

    std::string known = friendlyKnownId(nodeId, es);
    std::string group = menuId.empty() ? (es ? "Boton" : "Button") : cleanIdentifier(menuId);
    if (!known.empty()) {
        return { known, group, ElementCategory::Button };
    }

    if (!nodeId.empty()) {
        return { cleanIdentifier(nodeId), group, ElementCategory::Button };
    }
    return { cleanIdentifier(eb.label), group, ElementCategory::Button };
}

FriendlyElementInfo describeShapeElement(DrawShapeLayout const& shape, int index) {
    bool const es = isSpanish();
    char const* kindName = nullptr;
    switch (shape.kind) {
        case DrawShapeKind::Rectangle: kindName = es ? "Rectangulo" : "Rect"; break;
        case DrawShapeKind::Circle: kindName = es ? "Circulo" : "Circle"; break;
        case DrawShapeKind::RoundedRect: default: kindName = es ? "Curvo" : "Rounded"; break;
    }
    return {
        fmt::format("{} #{}", kindName, index + 1),
        "Draw",
        ElementCategory::Shape,
    };
}

ccColor3B categoryColor(ElementCategory cat) {
    switch (cat) {
        case ElementCategory::Button: return kAccentCyan;
        case ElementCategory::Label:  return kAccentGold;
        case ElementCategory::Sprite: return kAccentMint;
        case ElementCategory::Shape:  return kAccentPurple;
    }
    return kAccentCyan;
}

ClippedScrollMenu* ClippedScrollMenu::create(ScrollLayer* scroll) {
    auto* ret = new ClippedScrollMenu();
    if (ret && ret->init()) {
        ret->m_scroll = scroll;
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ClippedScrollMenu::containsTouch(CCTouch* touch) const {
    if (!m_scroll || !touch) return false;
    auto const pt = m_scroll->convertToNodeSpace(touch->getLocation());
    return CCRect{ CCPointZero, m_scroll->getContentSize() }.containsPoint(pt);
}

bool ClippedScrollMenu::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    return this->containsTouch(touch) && CCMenu::ccTouchBegan(touch, event);
}

void ClippedScrollMenu::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (this->containsTouch(touch)) CCMenu::ccTouchEnded(touch, event);
    else CCMenu::ccTouchCancelled(touch, event);
}

CCNode* makeGdWindow(CCSize size, paimon::ui::Bg bg, GLubyte insetOpacity) {
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->setAnchorPoint({ 0.f, 0.f });

    auto* frame = NineSlice::create(paimon::ui::popupTexture(bg));
    if (frame) {
        frame->setScaleMultiplier(0.62f);
        frame->setContentSize(size);
        frame->setAnchorPoint({ 0.f, 0.f });
        frame->setPosition({ 0.f, 0.f });
        root->addChild(frame, 0);
    }

    if (insetOpacity > 0 && size.width > 14.f && size.height > 14.f) {
        auto* inset = paimon::ui::makeInset({ size.width - 10.f, size.height - 10.f }, insetOpacity);
        inset->setPosition({ 5.f, 5.f });
        root->addChild(inset, 1);
    }
    return root;
}

CCMenuItemSpriteExtra* makeGdButton(
    std::string const& text,
    float width,
    float height,
    paimon::ui::Btn skin,
    std::function<void()> onPress,
    char const* font) {
    float const scale = std::clamp(height / 30.f, 0.36f, 0.85f);
    auto* btn = paimon::ui::makeButton(text.c_str(), std::move(onPress), skin, width, scale, font);
    return btn;
}

void setGdButtonState(
    CCMenuItemSpriteExtra* btn,
    paimon::ui::Btn skin,
    std::string const& newText) {
    if (!btn) return;
    paimon::ui::setButtonSkin(btn, skin);
    if (!newText.empty()) {
        if (auto* sprite = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            if (sprite->m_label && newText != sprite->m_label->getString()) {
                sprite->setString(newText.c_str());
                float const inner = std::max(8.f, sprite->getContentSize().width - 14.f);
                sprite->m_label->limitLabelWidth(inner, sprite->m_scale, 0.1f);
            }
        }
    }
}

CCSprite* createBestSprite(
    std::initializer_list<char const*> frames,
    float maxExtent,
    ccColor3B tint) {
    for (char const* frame : frames) {
        if (!frame || frame[0] == '\0') continue;
        if (auto* spr = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
            auto const sz = spr->getContentSize();
            float const longest = std::max({ sz.width, sz.height, 1.f });
            spr->setScale(std::min(1.f, maxExtent / longest));
            spr->setColor(tint);
            return spr;
        }
    }
    auto* spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    if (spr) {
        auto const sz = spr->getContentSize();
        float const longest = std::max({ sz.width, sz.height, 1.f });
        spr->setScale(std::min(1.f, maxExtent / longest));
        spr->setColor(tint);
    }
    return spr;
}

CCMenuItemSpriteExtra* makeDockTool(
    char const* primaryFrame,
    char const* fallbackFrame,
    std::string const& caption,
    float width,
    float height,
    CCObject* target,
    SEL_MenuHandler handler) {
    auto* face = CCNode::create();
    face->setContentSize({ width, height });

    auto* activeBg = paimon::ui::makeInset({ width - 2.f, height - 2.f }, 140, { 70, 210, 110 });
    if (activeBg) {
        activeBg->setID("tool-active-bg");
        activeBg->setPosition({ 1.f, 1.f });
        activeBg->setVisible(false);
        face->addChild(activeBg, 0);
    }

    if (auto* icon = createBestSprite({ primaryFrame, fallbackFrame }, 17.f)) {
        icon->setID("tool-icon");
        icon->setPosition({ width * 0.5f, height - 12.f });
        face->addChild(icon, 1);
    }

    auto* lbl = CCLabelBMFont::create(caption.c_str(), "goldFont.fnt");
    lbl->setID("tool-caption");
    lbl->setAnchorPoint({ 0.5f, 0.f });
    lbl->setPosition({ width * 0.5f, 2.f });
    lbl->limitLabelWidth(std::max(8.f, width - 4.f), 0.28f, 0.12f);
    face->addChild(lbl, 2);

    auto* item = CCMenuItemSpriteExtra::create(face, target, handler);
    item->m_baseScale = 1.f;
    return item;
}

void setDockToolState(CCMenuItemSpriteExtra* tool, bool active, bool enabled) {
    if (!tool) return;
    tool->setEnabled(enabled);
    tool->setOpacity(enabled ? 255 : 95);
    auto* face = tool->getNormalImage();
    if (!face) return;
    if (auto* bg = face->getChildByID("tool-active-bg")) {
        bg->setVisible(active);
    }
    if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(face->getChildByID("tool-caption"))) {
        lbl->setColor(active ? kAccentMint : (enabled ? ccColor3B{ 255, 255, 255 } : kTextMuted));
    }
    if (auto* icon = typeinfo_cast<CCRGBAProtocol*>(face->getChildByID("tool-icon"))) {
        icon->setOpacity(enabled ? 255 : 100);
    }
}

void openNumericInputPopup(
    std::string const& title,
    float currentValue,
    float minValue,
    float maxValue,
    bool integerOnly,
    std::function<void(float)> onApply) {
    if (auto* popup = NumericInputPopup::create(title, currentValue, minValue, maxValue, integerOnly, std::move(onApply))) {
        popup->show();
    }
}

void openCanvasSettingsPopup(
    CanvasSettings const& current,
    std::function<void(CanvasSettings const&)> onChanged) {
    if (auto* popup = CanvasSettingsPopup::create(current, std::move(onChanged))) {
        popup->show();
    }
}

} // namespace studio_ui
} // namespace paimon::menu_layout
