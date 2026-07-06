#include "PaimonIconsConfigPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/IconColorService.hpp"
#include "../services/IconConfigStore.hpp"
#include "../services/IconLockStyler.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace paimon::icons::ui {

namespace {

constexpr float kPopupW = 460.f;
constexpr float kPopupH = 290.f;
constexpr float kRowW   = 420.f;
constexpr float kRowH   = 26.f;

// Vanilla browser-item colors, shown when the feature is off.
constexpr ccColor3B kVanilla1{175, 175, 175};
constexpr ccColor3B kVanilla2{255, 255, 255};

// ---- catalog of what the UI offers -----------------------------------------
// One entry per mode/style: internal enum value + short Spanish name + a one
// line description. The selector cycles this list; everything else derives
// from it, so adding a mode is a single new row here plus its controls.

struct ModeInfo {
    ColorMode mode;
    const char* name;
    const char* desc;
};
constexpr std::array<ModeInfo, 8> kModes{{
    {ColorMode::Player,       "Tus Colores", "Todos los iconos usan los colores de tu jugador."},
    {ColorMode::CustomRGB,    "Color Fijo",  "Todos los iconos con los 3 colores que elijas."},
    {ColorMode::Gradient,     "Degradado",   "La lista se pinta de un color a otro."},
    {ColorMode::Rainbow,      "Arcoiris",    "Colores animados que cambian solos."},
    {ColorMode::RandomStable, "Aleatorio",   "Cada icono con su propio color, siempre el mismo."},
    {ColorMode::HueShift,     "Tono Girado", "Tus colores con el tono rotado los grados que marques."},
    {ColorMode::Inverted,     "Invertido",   "Los colores opuestos a los de tu jugador."},
    {ColorMode::Monochrome,   "Monocromo",   "Todo en tonos de un solo color base."},
}};

struct LockInfo {
    LockStyle style;
    const char* name;
    const char* desc;
};
constexpr std::array<LockInfo, 5> kLockStyles{{
    {LockStyle::Default,    "Normal",        "Los candados se ven como en el juego original."},
    {LockStyle::ShowDimmed, "Icono Visible", "Se ve el icono bloqueado atenuado, sin candado."},
    {LockStyle::TintedLock, "Candado Color", "Solo el candado, pintado del color que elijas."},
    {LockStyle::Silhouette, "Silueta",       "El icono como sombra de un solo color."},
    {LockStyle::HideBoth,   "Ocultar",       "Los iconos bloqueados no se muestran."},
}};

int modeIndexOf(ColorMode m) {
    for (int i = 0; i < static_cast<int>(kModes.size()); ++i) {
        if (kModes[i].mode == m) return i;
    }
    return 0;
}

int lockIndexOf(LockStyle s) {
    for (int i = 0; i < static_cast<int>(kLockStyles.size()); ++i) {
        if (kLockStyles[i].style == s) return i;
    }
    return 0;
}

// ---- small row builders -----------------------------------------------------
// Every row is a {kRowW x kRowH} node positioned by its center.

CCLabelBMFont* makeLabel(std::string const& text, const char* font = "bigFont.fnt", float scale = 0.45f) {
    auto* lbl = CCLabelBMFont::create(text.c_str(), font);
    if (lbl) lbl->setScale(scale);
    return lbl;
}

enum class ValueFmt { Int, Percent255, OneDecimal };

std::string formatValue(float v, ValueFmt kind) {
    switch (kind) {
        case ValueFmt::Percent255: return fmt::format("{}%", static_cast<int>(std::lround(v / 255.f * 100.f)));
        case ValueFmt::OneDecimal: return fmt::format("{:.1f}", v);
        case ValueFmt::Int:        break;
    }
    return fmt::format("{}", static_cast<int>(std::lround(v)));
}

// [label ......................... toggle]
CCNode* makeToggleRow(std::string const& label, bool initial, std::function<void(bool)> onChange) {
    auto* row = CCNode::create();
    row->setContentSize({kRowW, kRowH});

    if (auto* lbl = makeLabel(label)) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, kRowH / 2});
        lbl->limitLabelWidth(300.f, 0.45f, 0.25f);
        row->addChild(lbl);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    auto* tog = CCMenuItemExt::createTogglerWithStandardSprites(0.65f,
        [cb = std::move(onChange)](CCMenuItemToggler* t) {
            if (!t) return;
            // isToggled() is the state BEFORE this tap lands.
            if (cb) cb(!t->isToggled());
        });
    if (tog) {
        tog->toggle(initial);
        tog->setPosition({398.f, kRowH / 2});
        menu->addChild(tog);
    }
    row->addChild(menu);
    return row;
}

// [label ......... slider ......... value]
CCNode* makeSliderRow(std::string const& label, float minVal, float maxVal, float current,
                      ValueFmt kind, std::function<void(float)> onChange) {
    auto* row = CCNode::create();
    row->setContentSize({kRowW, kRowH});

    if (auto* lbl = makeLabel(label)) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, kRowH / 2});
        lbl->limitLabelWidth(155.f, 0.45f, 0.25f);
        row->addChild(lbl);
    }

    auto* valueLbl = makeLabel(formatValue(current, kind), "goldFont.fnt", 0.5f);
    if (valueLbl) {
        valueLbl->setAnchorPoint({1.f, 0.5f});
        valueLbl->setPosition({412.f, kRowH / 2});
        row->addChild(valueLbl);
    }
    Ref<CCLabelBMFont> valueRef = valueLbl;

    Slider* slider = Slider::create(nullptr, nullptr, 0.55f);
    if (slider) {
        if (maxVal > minVal) {
            slider->setValue(std::clamp((current - minVal) / (maxVal - minVal), 0.f, 1.f));
        }
        slider->setPosition({260.f, kRowH / 2});
        row->addChild(slider);

        // The bare Slider has no change callback, so poll at 15Hz like the
        // rest of the mod's sliders.
        struct Watcher : public CCNode {
            std::function<void(float)> cb;
            Ref<Slider> slider;
            Ref<CCLabelBMFont> valLbl;
            float minV = 0.f, maxV = 1.f;
            float lastNorm = -1.f;
            ValueFmt kind = ValueFmt::Int;
            void check(float) {
                if (!slider) return;
                float v = slider->getValue();
                if (std::fabs(v - lastNorm) < 1e-4f) return;
                lastNorm = v;
                float mapped = minV + (maxV - minV) * v;
                if (valLbl) valLbl->setString(formatValue(mapped, kind).c_str());
                if (cb) cb(mapped);
            }
        };
        auto* watcher = new Watcher();
        watcher->autorelease();
        watcher->cb     = std::move(onChange);
        watcher->slider = slider;
        watcher->valLbl = valueRef;
        watcher->minV   = minVal;
        watcher->maxV   = maxVal;
        watcher->kind   = kind;
        watcher->schedule(schedule_selector(Watcher::check), 1.f / 15.f);
        row->addChild(watcher);
    }
    return row;
}

// [label ..................... color swatch]
CCNode* makeColorRow(std::string const& label, ccColor3B initial,
                     std::function<void(ccColor3B)> onChange) {
    auto* row = CCNode::create();
    row->setContentSize({kRowW, kRowH});

    if (auto* lbl = makeLabel(label)) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, kRowH / 2});
        lbl->limitLabelWidth(300.f, 0.45f, 0.25f);
        row->addChild(lbl);
    }

    CCSprite* swatch = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
    if (!swatch) swatch = ColorChannelSprite::create();
    if (!swatch) return row;
    swatch->setColor(initial);
    swatch->setScale(0.62f);
    Ref<CCSprite> swatchRef = swatch;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    auto* btn = CCMenuItemExt::createSpriteExtra(swatch,
        [cb = std::move(onChange), swatchRef](CCMenuItemSpriteExtra*) {
            ccColor4B current{255, 255, 255, 255};
            if (swatchRef) {
                auto c = swatchRef->getColor();
                current = {c.r, c.g, c.b, 255};
            }
            auto* picker = ColorPickPopup::create(current);
            if (!picker) return;
            picker->setCallback([cb, swatchRef](ccColor4B const& picked) {
                if (swatchRef) swatchRef->setColor({picked.r, picked.g, picked.b});
                if (cb) cb({picked.r, picked.g, picked.b});
            });
            picker->show();
        });
    btn->setPosition({398.f, kRowH / 2});
    menu->addChild(btn);
    row->addChild(menu);
    return row;
}

// [label ........... [<] value [>]]
CCNode* makeCycleRow(std::string const& label, std::vector<std::string> options,
                     int initialIdx, std::function<void(int)> onChange) {
    auto* row = CCNode::create();
    row->setContentSize({kRowW, kRowH});

    if (auto* lbl = makeLabel(label)) {
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({10.f, kRowH / 2});
        lbl->limitLabelWidth(150.f, 0.45f, 0.25f);
        row->addChild(lbl);
    }

    auto* valueLbl = makeLabel(
        initialIdx >= 0 && initialIdx < static_cast<int>(options.size())
            ? options[initialIdx] : "?",
        "bigFont.fnt", 0.45f);
    valueLbl->setPosition({325.f, kRowH / 2});
    valueLbl->limitLabelWidth(100.f, 0.45f, 0.25f);
    row->addChild(valueLbl);
    Ref<CCLabelBMFont> valueRef = valueLbl;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});

    auto changeBy = [valueRef, options = std::move(options), cb = std::move(onChange),
                     idxPtr = std::make_shared<int>(initialIdx)](int delta) {
        if (options.empty()) return;
        int newIdx = (*idxPtr + delta + static_cast<int>(options.size()))
                   % static_cast<int>(options.size());
        *idxPtr = newIdx;
        if (valueRef) {
            valueRef->setString(options[newIdx].c_str());
            valueRef->limitLabelWidth(100.f, 0.45f, 0.25f);
        }
        if (cb) cb(newIdx);
    };

    auto addArrow = [&](float x, bool flip, int delta) {
        auto* spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        if (spr) {
            spr->setScale(0.42f);
            spr->setFlipX(flip);
        }
        auto* btn = CCMenuItemExt::createSpriteExtra(
            spr ? static_cast<CCNode*>(spr) : CCSprite::create(),
            [changeBy, delta](CCMenuItemSpriteExtra*) { changeBy(delta); });
        btn->setPosition({x, kRowH / 2});
        menu->addChild(btn);
    };
    addArrow(250.f, false, -1);
    addArrow(400.f, true, 1);
    row->addChild(menu);
    return row;
}

}  // anonymous namespace

// ---- popup ------------------------------------------------------------------

PaimonIconsConfigPopup* PaimonIconsConfigPopup::open() {
    auto* p = new PaimonIconsConfigPopup();
    if (p->init()) {
        p->autorelease();
        p->show();
        return p;
    }
    delete p;
    return nullptr;
}

bool PaimonIconsConfigPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    IconConfigStore::get().load();
    setTitle("Paimon Icons");

    buildHeader();
    buildPreview();
    buildTabs();

    // Containers for the per-tab content (selector + description + rows).
    m_selectorArea = CCNode::create();
    m_selectorArea->setContentSize({kRowW, 52.f});
    m_selectorArea->setAnchorPoint({0.5f, 1.f});
    m_mainLayer->addChildAtPosition(m_selectorArea, Anchor::Top, {0.f, -136.f});

    m_controls = CCNode::create();
    m_controls->setContentSize({kRowW, 84.f});
    m_controls->setAnchorPoint({0.5f, 1.f});
    m_mainLayer->addChildAtPosition(m_controls, Anchor::Top, {0.f, -192.f});

    switchTab(Tab::Colors);

    // Drives the live preview (rainbow motion + slider drags).
    schedule(schedule_selector(PaimonIconsConfigPopup::refreshPreview), 1.f / 10.f);
    return true;
}

void PaimonIconsConfigPopup::buildHeader() {
    // Master switch (mirrors the "colorful-icons-enabled" mod setting).
    auto* label = makeLabel("Activado", "goldFont.fnt", 0.5f);
    if (label) {
        label->setAnchorPoint({1.f, 0.5f});
        m_mainLayer->addChildAtPosition(label, Anchor::TopRight, {-54.f, -26.f});
    }

    auto* tog = CCMenuItemExt::createTogglerWithStandardSprites(0.6f,
        [](CCMenuItemToggler* t) {
            if (!t) return;
            // isToggled() is the state BEFORE this tap lands.
            IconConfigStore::get().setFeatureEnabled(!t->isToggled());
        });
    if (tog) {
        tog->toggle(IconConfigStore::get().isFeatureEnabled());
        if (m_buttonMenu) {
            m_buttonMenu->addChildAtPosition(tog, Anchor::TopRight, {-30.f, -26.f});
        }
    }

    // Reset, tucked next to the close button and behind a confirm dialog.
    auto* spr = ButtonSprite::create("Reset", "goldFont.fnt", "GJ_button_06.png", 0.8f);
    if (!spr) return;
    spr->setScale(0.5f);
    auto* btn = CCMenuItemExt::createSpriteExtra(spr,
        [self = WeakRef<PaimonIconsConfigPopup>(this)](CCMenuItemSpriteExtra*) {
            geode::createQuickPopup("Reset",
                "Volver <cy>Paimon Icons</c> a sus valores por defecto?",
                "Cancelar", "Reset",
                [self](FLAlertLayer*, bool yes) {
                    if (!yes) return;
                    IconConfigStore::get().resetToDefaults();
                    if (auto p = self.lock()) p->switchTab(p->m_tab);
                });
        });
    if (m_buttonMenu) {
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, {52.f, -26.f});
    }
}

void PaimonIconsConfigPopup::buildPreview() {
    auto* bg = CCScale9Sprite::create("square02b_001.png");
    if (!bg) return;
    bg->setContentSize({424.f, 60.f});
    bg->setColor({0, 0, 0});
    bg->setOpacity(90);
    m_previewPanel = bg;
    m_mainLayer->addChildAtPosition(bg, Anchor::Top, {0.f, -72.f});

    m_previewIcons = CCNode::create();
    m_previewIcons->setContentSize(bg->getContentSize());
    m_previewIcons->setAnchorPoint({0.f, 0.f});
    m_previewIcons->setPosition({0.f, 0.f});
    bg->addChild(m_previewIcons);

    m_disabledLabel = makeLabel("DESACTIVADO", "bigFont.fnt", 0.45f);
    if (m_disabledLabel) {
        m_disabledLabel->setColor({255, 110, 110});
        m_disabledLabel->setPosition(bg->getContentSize() / 2);
        m_disabledLabel->setZOrder(10);
        m_disabledLabel->setVisible(false);
        bg->addChild(m_disabledLabel);
    }
}

void PaimonIconsConfigPopup::buildTabs() {
    m_tabMenu = CCMenu::create();
    m_tabMenu->setContentSize({kRowW, 26.f});
    m_tabMenu->ignoreAnchorPointForPosition(false);

    static constexpr std::array<const char*, 3> kNames{"Colores", "Candados", "Donde"};
    for (int i = 0; i < static_cast<int>(kNames.size()); ++i) {
        auto* spr = ButtonSprite::create(kNames[i], "goldFont.fnt", "GJ_button_04.png", 0.8f);
        if (!spr) continue;
        spr->setScale(0.6f);
        m_tabSprites[i] = spr;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [self = WeakRef<PaimonIconsConfigPopup>(this), i](CCMenuItemSpriteExtra*) {
                if (auto p = self.lock()) p->switchTab(static_cast<Tab>(i));
            });
        m_tabMenu->addChild(btn);
    }
    m_tabMenu->setLayout(RowLayout::create()
        ->setGap(6.f)
        ->setAxisAlignment(AxisAlignment::Center));
    m_tabMenu->updateLayout();
    m_mainLayer->addChildAtPosition(m_tabMenu, Anchor::Top, {0.f, -120.f});
}

void PaimonIconsConfigPopup::switchTab(Tab tab) {
    m_tab = tab;
    // Highlight the active tab in place -- the buttons themselves are never
    // rebuilt, so switching from a tab button callback is safe.
    for (int i = 0; i < static_cast<int>(m_tabSprites.size()); ++i) {
        if (m_tabSprites[i]) {
            m_tabSprites[i]->updateBGImage(
                i == static_cast<int>(tab) ? "GJ_button_01.png" : "GJ_button_04.png");
        }
    }
    rebuildPreviewSlots();
    rebuildSelector();
    rebuildControls();
    refreshPreview(0.f);
}

void PaimonIconsConfigPopup::rebuildPreviewSlots() {
    if (!m_previewIcons) return;
    m_previewIcons->removeAllChildren();
    m_slots.clear();

    auto size = m_previewIcons->getContentSize();

    struct Def { SlotRole role; const char* caption; };
    std::vector<Def> defs;
    if (m_tab == Tab::Locks) {
        defs = {{SlotRole::Colored,      "Normal"},
                {SlotRole::Locked,       "Bloqueado"},
                {SlotRole::Unobtainable, "Imposible"}};
    } else {
        defs.assign(5, {SlotRole::Colored, nullptr});
    }
    bool const withCaptions = m_tab == Tab::Locks;
    float const iconY = withCaptions ? size.height / 2 + 6.f : size.height / 2;

    static constexpr std::array<int, 5> kPreviewCubes{1, 7, 22, 34, 48};

    int n = static_cast<int>(defs.size());
    for (int i = 0; i < n; ++i) {
        float x = size.width * (i + 0.5f) / n;

        PreviewSlot slot;
        slot.role         = defs[i].role;
        slot.iconID       = kPreviewCubes[i % kPreviewCubes.size()];
        slot.displayIndex = i;

        auto* sp = SimplePlayer::create(slot.iconID);
        if (!sp) continue;
        sp->setPosition({x, iconY});
        sp->setScale(0.85f);
        m_previewIcons->addChild(sp);
        slot.player = sp;

        if (auto* lock = CCSprite::createWithSpriteFrameName("GJ_lock_001.png")) {
            lock->setPosition({x, iconY});
            lock->setScale(0.6f);
            lock->setVisible(false);
            m_previewIcons->addChild(lock, 5);
            slot.lock = lock;
        }

        if (defs[i].caption) {
            if (auto* cap = makeLabel(defs[i].caption, "chatFont.fnt", 0.45f)) {
                cap->setPosition({x, 9.f});
                cap->setColor({200, 200, 200});
                m_previewIcons->addChild(cap);
            }
        }
        m_slots.push_back(slot);
    }
}

void PaimonIconsConfigPopup::rebuildSelector() {
    if (!m_selectorArea) return;
    m_selectorArea->removeAllChildren();

    auto const& cfg = IconConfigStore::get().config();
    float const w     = m_selectorArea->getContentSize().width;
    float const rowY  = 36.f;
    float const descY = 10.f;

    auto* desc = makeLabel("", "chatFont.fnt", 0.55f);
    if (!desc) return;
    desc->setPosition({w / 2, descY});
    desc->setColor({185, 205, 255});
    m_selectorArea->addChild(desc);
    Ref<CCLabelBMFont> descRef = desc;

    if (m_tab == Tab::Areas) {
        // No selector: a static heading is enough here.
        if (auto* head = makeLabel("Donde se aplica", "bigFont.fnt", 0.5f)) {
            head->setColor({255, 210, 80});
            head->setPosition({w / 2, rowY});
            m_selectorArea->addChild(head);
        }
        desc->setString("El recolor solo toca los lugares que actives.");
        desc->limitLabelWidth(400.f, 0.55f, 0.3f);
        return;
    }

    auto* name = makeLabel("", "bigFont.fnt", 0.55f);
    if (!name) return;
    name->setColor({255, 210, 80});
    name->setPosition({w / 2, rowY});
    m_selectorArea->addChild(name);
    Ref<CCLabelBMFont> nameRef = name;

    auto setTexts = [nameRef, descRef](const char* n, const char* d) {
        if (nameRef) {
            nameRef->setString(n);
            nameRef->limitLabelWidth(190.f, 0.55f, 0.3f);
        }
        if (descRef) {
            descRef->setString(d);
            descRef->limitLabelWidth(400.f, 0.55f, 0.3f);
        }
    };

    // The arrows only mutate labels and the controls container (never the
    // selector row itself), so stepping from an arrow callback is safe.
    std::function<void(int)> step;
    if (m_tab == Tab::Colors) {
        auto idx = std::make_shared<int>(modeIndexOf(cfg.mode));
        setTexts(kModes[*idx].name, kModes[*idx].desc);
        step = [this, idx, setTexts](int delta) {
            *idx = (*idx + delta + static_cast<int>(kModes.size()))
                 % static_cast<int>(kModes.size());
            IconConfigStore::get().update([&](PaimonIconConfig& c) {
                c.mode = kModes[*idx].mode;
            });
            setTexts(kModes[*idx].name, kModes[*idx].desc);
            rebuildControls();
        };
    } else {
        auto idx = std::make_shared<int>(lockIndexOf(cfg.lockStyle));
        setTexts(kLockStyles[*idx].name, kLockStyles[*idx].desc);
        step = [this, idx, setTexts](int delta) {
            *idx = (*idx + delta + static_cast<int>(kLockStyles.size()))
                 % static_cast<int>(kLockStyles.size());
            IconConfigStore::get().update([&](PaimonIconConfig& c) {
                c.lockStyle = kLockStyles[*idx].style;
            });
            setTexts(kLockStyles[*idx].name, kLockStyles[*idx].desc);
            rebuildControls();
        };
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    auto addArrow = [&](float x, bool flip, int delta) {
        auto* spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        if (spr) {
            spr->setScale(0.55f);
            spr->setFlipX(flip);
        }
        auto* btn = CCMenuItemExt::createSpriteExtra(
            spr ? static_cast<CCNode*>(spr) : CCSprite::create(),
            [step, delta](CCMenuItemSpriteExtra*) { if (step) step(delta); });
        btn->setPosition({x, rowY});
        menu->addChild(btn);
    };
    addArrow(90.f, false, -1);
    addArrow(w - 90.f, true, 1);
    m_selectorArea->addChild(menu);
}

void PaimonIconsConfigPopup::rebuildControls() {
    if (!m_controls) return;
    m_controls->removeAllChildren();

    auto const& cfg = IconConfigStore::get().config();
    float const w = m_controls->getContentSize().width;
    float y = m_controls->getContentSize().height - kRowH / 2;

    auto addRow = [&](CCNode* row) {
        if (!row) return;
        row->setAnchorPoint({0.5f, 0.5f});
        row->ignoreAnchorPointForPosition(false);
        row->setPosition({w / 2, y});
        m_controls->addChild(row);
        y -= kRowH;
    };
    auto addHint = [&](const char* text) {
        if (auto* lbl = makeLabel(text, "chatFont.fnt", 0.5f)) {
            lbl->setColor({140, 150, 170});
            lbl->setPosition({w / 2, y});
            m_controls->addChild(lbl);
            y -= kRowH;
        }
    };
    auto set = [](auto&& mutator) {
        IconConfigStore::get().update(mutator);
    };

    switch (m_tab) {
        case Tab::Colors: {
            switch (cfg.mode) {
                case ColorMode::Player:
                case ColorMode::Inverted:
                    addHint("Este modo no tiene opciones extra.");
                    break;
                case ColorMode::CustomRGB:
                    addRow(makeColorRow("Color 1", cfg.custom1, [set](ccColor3B c) {
                        set([&](PaimonIconConfig& g) { g.custom1 = c; });
                    }));
                    addRow(makeColorRow("Color 2", cfg.custom2, [set](ccColor3B c) {
                        set([&](PaimonIconConfig& g) { g.custom2 = c; });
                    }));
                    addRow(makeColorRow("Glow", cfg.customGlow, [set](ccColor3B c) {
                        set([&](PaimonIconConfig& g) { g.customGlow = c; });
                    }));
                    break;
                case ColorMode::HueShift:
                    addRow(makeSliderRow("Giro de tono", 0.f, 360.f, cfg.hueShiftDegrees,
                        ValueFmt::Int, [set](float v) {
                            set([&](PaimonIconConfig& g) { g.hueShiftDegrees = v; });
                        }));
                    break;
                case ColorMode::Rainbow:
                    addRow(makeSliderRow("Velocidad", 0.1f, 3.f, cfg.rainbowSpeed,
                        ValueFmt::OneDecimal, [set](float v) {
                            set([&](PaimonIconConfig& g) { g.rainbowSpeed = v; });
                        }));
                    addRow(makeSliderRow("Variedad", 0.f, 180.f, cfg.rainbowSpread,
                        ValueFmt::Int, [set](float v) {
                            set([&](PaimonIconConfig& g) { g.rainbowSpread = v; });
                        }));
                    break;
                case ColorMode::Gradient:
                    addRow(makeColorRow("Color inicial", cfg.gradientStart, [set](ccColor3B c) {
                        set([&](PaimonIconConfig& g) { g.gradientStart = c; });
                    }));
                    addRow(makeColorRow("Color final", cfg.gradientEnd, [set](ccColor3B c) {
                        set([&](PaimonIconConfig& g) { g.gradientEnd = c; });
                    }));
                    break;
                case ColorMode::RandomStable:
                    addRow(makeCycleRow("Paleta", {"Vibrante", "Pastel", "Neon", "Tierra"},
                        static_cast<int>(cfg.randomPalette), [set](int i) {
                            set([&](PaimonIconConfig& g) {
                                g.randomPalette = static_cast<RandomPalette>(i);
                            });
                        }));
                    break;
                case ColorMode::Monochrome:
                    addRow(makeColorRow("Color base", cfg.monochromeBase, [set](ccColor3B c) {
                        set([&](PaimonIconConfig& g) { g.monochromeBase = c; });
                    }));
                    break;
            }
            break;
        }
        case Tab::Locks: {
            bool showUnobtainable = true;
            switch (cfg.lockStyle) {
                case LockStyle::Default:
                case LockStyle::HideBoth:
                    addHint("Este estilo no tiene opciones extra.");
                    showUnobtainable = false;
                    break;
                case LockStyle::ShowDimmed:
                    addRow(makeSliderRow("Opacidad", 0.f, 255.f, static_cast<float>(cfg.dimOpacity),
                        ValueFmt::Percent255, [set](float v) {
                            set([&](PaimonIconConfig& g) { g.dimOpacity = static_cast<int>(v); });
                        }));
                    break;
                case LockStyle::TintedLock:
                    addRow(makeColorRow("Color del candado", cfg.lockTint, [set](ccColor3B c) {
                        set([&](PaimonIconConfig& g) { g.lockTint = c; });
                    }));
                    break;
                case LockStyle::Silhouette:
                    addRow(makeColorRow("Color de la silueta", cfg.silhouetteColor, [set](ccColor3B c) {
                        set([&](PaimonIconConfig& g) { g.silhouetteColor = c; });
                    }));
                    break;
            }
            if (showUnobtainable) {
                // "Imposibles": icons that can no longer be obtained.
                addRow(makeToggleRow("Oscurecer imposibles", cfg.dimUnobtainable, [set](bool v) {
                    set([&](PaimonIconConfig& g) { g.dimUnobtainable = v; });
                }));
                addRow(makeSliderRow("Opacidad imposibles", 0.f, 255.f,
                    static_cast<float>(cfg.unobtainableOpacity),
                    ValueFmt::Percent255, [set](float v) {
                        set([&](PaimonIconConfig& g) { g.unobtainableOpacity = static_cast<int>(v); });
                    }));
            }
            break;
        }
        case Tab::Areas: {
            addRow(makeToggleRow("Garage (kit de iconos)", cfg.apply.kit, [set](bool v) {
                set([&](PaimonIconConfig& g) { g.apply.kit = v; });
            }));
            addRow(makeToggleRow("Tiendas", cfg.apply.shops, [set](bool v) {
                set([&](PaimonIconConfig& g) { g.apply.shops = v; });
            }));
            break;
        }
    }
}

void PaimonIconsConfigPopup::refreshPreview(float) {
    auto& store = IconConfigStore::get();
    bool const enabled = store.isFeatureEnabled();
    if (m_disabledLabel) m_disabledLabel->setVisible(!enabled);

    auto const& cfg = store.config();
    auto& svc = IconColorService::get();

    int totalColored = 0;
    for (auto const& s : m_slots) {
        if (s.role == SlotRole::Colored) ++totalColored;
    }
    totalColored = std::max(1, totalColored);

    for (auto& slot : m_slots) {
        auto* sp = slot.player;
        if (!sp) continue;

        // Reset everything a style variant may have touched last tick.
        sp->setVisible(true);
        IconLockStyler::fadeAllParts(sp, 255);
        if (slot.lock) slot.lock->setVisible(false);

        if (slot.role == SlotRole::Colored) {
            if (!enabled) {
                sp->setColors(kVanilla1, kVanilla2);
                sp->disableGlowOutline();
                continue;
            }
            IconDescriptor desc;
            desc.unlockTypeRaw = static_cast<int>(UnlockType::Cube);
            desc.iconID        = slot.iconID;
            desc.displayIndex  = slot.displayIndex;
            desc.totalCount    = totalColored;
            auto t = svc.resolve(desc);
            sp->setColors(t.primary, t.secondary);
            if (t.hasGlow) sp->setGlowOutline(t.glow);
            else           sp->disableGlowOutline();
            continue;
        }

        // Locked / unobtainable samples mirror IconLockStyler's styles on the
        // plain preview nodes.
        sp->setColors(kVanilla1, kVanilla2);
        sp->disableGlowOutline();
        bool const unob = slot.role == SlotRole::Unobtainable && cfg.dimUnobtainable;
        LockStyle const style = enabled ? cfg.lockStyle : LockStyle::Default;

        switch (style) {
            case LockStyle::Default:
                IconLockStyler::fadeAllParts(sp, 120);
                if (slot.lock) {
                    slot.lock->setVisible(true);
                    slot.lock->setColor({255, 255, 255});
                }
                break;
            case LockStyle::ShowDimmed:
                IconLockStyler::fadeAllParts(sp, static_cast<unsigned char>(std::clamp(
                    unob ? cfg.unobtainableOpacity : cfg.dimOpacity, 0, 255)));
                if (unob) IconLockStyler::tintAllParts(sp, cfg.unobtainableTint);
                break;
            case LockStyle::TintedLock:
                sp->setVisible(false);
                if (slot.lock) {
                    slot.lock->setVisible(true);
                    slot.lock->setColor(unob ? cfg.unobtainableTint : cfg.lockTint);
                }
                break;
            case LockStyle::Silhouette:
                IconLockStyler::fadeAllParts(sp, 230);
                IconLockStyler::tintAllParts(sp, unob ? cfg.unobtainableTint : cfg.silhouetteColor);
                break;
            case LockStyle::HideBoth:
                sp->setVisible(false);
                break;
        }
    }
}

}  // namespace paimon::icons::ui
